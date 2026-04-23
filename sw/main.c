/*
 * Plants vs Zombies — Main game loop
 *
 * Initializes the FPGA driver, input auto-detect (gamepad > keyboard),
 * and game state, then runs a 60 Hz loop: input -> update -> render ->
 * write to FPGA.
 *
 * Usage: ./pvz
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#include "pvz.h"
#include "game.h"
#include "input.h"
#include "render.h"
#include "audio.h"

#define FRAME_USEC 16667  /* ~60 fps */

static int pvz_fd;

/* Get current time in microseconds */
static long long get_time_usec(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000LL + tv.tv_usec;
}

static void process_input(game_state_t *gs, audio_events_t *ev)
{
    input_action_t action;

    while ((action = input_poll()) != INPUT_NONE) {
        switch (action) {
        case INPUT_UP:
            if (gs->cursor_row > 0) gs->cursor_row--;
            break;
        case INPUT_DOWN:
            if (gs->cursor_row < GRID_ROWS - 1) gs->cursor_row++;
            break;
        case INPUT_LEFT:
            if (gs->cursor_col > 0) gs->cursor_col--;
            break;
        case INPUT_RIGHT:
            if (gs->cursor_col < GRID_COLS - 1) gs->cursor_col++;
            break;
        case INPUT_PLACE:
            game_place_plant(gs, ev);
            break;
        case INPUT_DIG:
            game_remove_plant(gs);
            break;
        case INPUT_CYCLE_PREV:
            cycle_plant_prev(gs);
            break;
        case INPUT_CYCLE_NEXT:
            cycle_plant_next(gs);
            break;
        case INPUT_QUIT:
            gs->state = -1; /* signal exit */
            return;
        case INPUT_NONE:
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    game_state_t gs;

    /* Seed random number generator */
    srand((unsigned)time(NULL));

    /* Open FPGA device */
    pvz_fd = open("/dev/pvz", O_RDWR);
    if (pvz_fd < 0) {
        perror("open /dev/pvz");
        fprintf(stderr, "Is the pvz_driver module loaded?\n");
        return 1;
    }

    /* Initialize input (auto-detect gamepad or keyboard) */
    if (input_init() < 0) {
        fprintf(stderr, "No gamepad or keyboard detected\n");
        close(pvz_fd);
        return 1;
    }

    /* Initialize renderer */
    render_init(pvz_fd);

    /* Initialize audio (non-fatal: keep playing silent on failure) */
    if (audio_init() < 0) {
        fprintf(stderr, "audio_init failed, continuing without audio\n");
    }

    /* Initialize game state */
    game_init(&gs);

    printf("Plants vs Zombies v3\n");
    if (input_is_gamepad()) {
        printf("Controls (gamepad): D-pad=move, A=place, B=remove, LB/RB=cycle plant, Start=quit\n");
    } else {
        printf("Controls (keyboard): Arrows=move, Space=place, D=remove, [/]=cycle plant, ESC=quit\n");
    }
    printf("Sun: %d | Plant cost: %d\n\n", gs.sun, PLANT_COST);

    /* Start BGM immediately (game begins in STATE_PLAYING) */
    audio_bgm_start();

    /* Main game loop */
    long long frame_start;
    int last_state = STATE_PLAYING;

    while (gs.state >= 0) {
        frame_start = get_time_usec();

        audio_events_t ev = { .flags = 0 };

        /* 1. Process input (may emit PLANT_PLACE) */
        process_input(&gs, &ev);
        if (gs.state < 0)
            break;

        /* 2. Update game logic (may emit other in-game events) */
        game_update(&gs, &ev);

        /* 3. BGM state-edge tracker */
        if (gs.state != last_state) {
            if (gs.state == STATE_PLAYING) {
                audio_bgm_start();
            } else if (last_state == STATE_PLAYING) {
                audio_bgm_stop();
            }
            if (last_state == STATE_PLAYING && gs.state == STATE_LOSE)
                audio_play_sfx(PVZ_SFX_GAME_OVER);
            if (last_state == STATE_PLAYING && gs.state == STATE_WIN)
                audio_play_sfx(PVZ_SFX_VICTORY);
            last_state = gs.state;
        }

        /* 4. Fan-out in-game SFX events (cues 1..6) */
        for (pvz_sfx_cue_t c = PVZ_SFX_PEA_FIRE; c <= PVZ_SFX_WAVE_START; c++) {
            if (ev.flags & PVZ_AUDIO_EVENT(c)) audio_play_sfx(c);
        }

        /* 5. Render to FPGA */
        render_frame(&gs);

        /* Print status periodically */
        if (gs.frame_count % 60 == 0) {
            printf("\rSun: %3d | Zombies: %d/%d | Frame: %d",
                   gs.sun, gs.zombies_spawned, TOTAL_ZOMBIES,
                   gs.frame_count);
            fflush(stdout);
        }

        /* Check game over */
        if (gs.state == STATE_WIN) {
            printf("\n\n*** YOU WIN! All zombies defeated! ***\n");
            render_frame(&gs); /* Render win state */
            sleep(5);
            break;
        }
        if (gs.state == STATE_LOSE) {
            printf("\n\n*** GAME OVER! A zombie reached your house! ***\n");
            render_frame(&gs); /* Render lose state */
            sleep(5);
            break;
        }

        /* 6. Frame timing: sleep for remainder of frame */
        long long elapsed = get_time_usec() - frame_start;
        if (elapsed < FRAME_USEC)
            usleep(FRAME_USEC - elapsed);
    }

    printf("\nCleaning up...\n");
    audio_close();
    input_close();
    close(pvz_fd);
    return 0;
}
