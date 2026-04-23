/*
 * Plants vs Zombies - main game loop.
 *
 * Sets up the FPGA driver, keyboard input, and game state, then runs
 * a 60 Hz loop: input -> update -> render -> write to FPGA.
 *
 * Usage: ./pvz [/dev/input/eventN]
 *
 * How it fits in:
 *   Top-level HPS executable. It wires render.c (shape-table ioctl
 *   writes), input.c (non-blocking keyboard reads), and game.c
 *   (plant/zombie/pea simulation) together. Talks to the FPGA via
 *   /dev/pvz, which pvz_driver.ko exposes (see pvz_driver.c / pvz.h
 *   for the ioctl contract). See design-doc section 3.1 "Game Loop
 *   (Software)".
 *
 * Fixed-rate 60 Hz loop:
 *   One frame every 16.67 ms to stay aligned with the VGA refresh.
 *   We grab t0 from gettimeofday, do Input -> Update -> Render, then
 *   usleep for whatever is left of the 16667 us budget. If a frame
 *   overruns we skip the sleep and fall straight into the next
 *   iteration, which is just a dropped frame.
 *
 * Exit via game state:
 *   game.c sets gs.state to STATE_WIN or STATE_LOSE when the last
 *   zombie dies or one makes it to the left edge. process_input()
 *   uses gs.state = -1 as the user-quit sentinel. The while-loop
 *   condition gs.state >= 0 picks that up. The STATE_WIN / STATE_LOSE
 *   branches render one last frame and sleep 5 s so the banner is
 *   visible before cleanup.
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

/* Frame budget in microseconds: 1_000_000 / 60 ~= 16667. Input +
 * update + render has to fit in this window or we start holding up
 * the next VGA refresh. */
#define FRAME_USEC 16667  /* ~60 fps */

static int pvz_fd;

/* Get current time in microseconds */
static long long get_time_usec(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000LL + tv.tv_usec;
}

/*
 * Drain every pending key event this frame and apply them to the
 * game state. input_poll() is non-blocking and returns INPUT_NONE
 * when the kernel buffer is empty, so the loop bails quickly. We
 * drain rather than consume one per frame so cursor motion stays
 * responsive when the user mashes arrow keys between frames.
 */
static void process_input(game_state_t *gs)
{
    int key;

    while ((key = input_poll()) != INPUT_NONE) {
        switch (key) {
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
        case INPUT_SPACE:
            game_place_plant(gs);
            break;
        case INPUT_D:
            game_remove_plant(gs);
            break;
        case INPUT_ESC:
            /* Out-of-range sentinel; the main loop's
             * gs.state >= 0 guard picks it up and breaks. */
            gs->state = -1; /* signal exit */
            return;
        }
    }
}

int main(int argc, char *argv[])
{
    const char *input_dev = "/dev/input/event0";
    game_state_t gs;

    if (argc > 1)
        input_dev = argv[1];

    /* Seed the PRNG. game.c uses it for zombie row selection and
     * spawn timing. */
    srand((unsigned)time(NULL));

    /*
     * Init sequence, order matters:
     *   1. open /dev/pvz         - misc device from pvz_driver.ko,
     *                              backing store for the shape-table ioctls
     *   2. input_init(input_dev) - open keyboard event device O_NONBLOCK
     *   3. render_init(pvz_fd)   - stash the fd; renderer has no other state
     *   4. game_init(&gs)        - zero grid, set starting sun, set state
     * On any failure, tear down whatever was already opened and return 1.
     */
    /* Open FPGA device */
    pvz_fd = open("/dev/pvz", O_RDWR);
    if (pvz_fd < 0) {
        perror("open /dev/pvz");
        fprintf(stderr, "Is the pvz_driver module loaded?\n");
        return 1;
    }

    /* Initialize keyboard input */
    if (input_init(input_dev) < 0) {
        fprintf(stderr, "Failed to open %s\n", input_dev);
        fprintf(stderr, "Usage: %s [/dev/input/eventN]\n", argv[0]);
        close(pvz_fd);
        return 1;
    }

    /* Initialize renderer */
    render_init(pvz_fd);

    /* Initialize game state */
    game_init(&gs);

    printf("Plants vs Zombies MVP\n");
    printf("Controls: Arrow keys=move cursor, Space=place plant, D=remove plant, ESC=quit\n");
    printf("Sun: %d | Plant cost: %d\n\n", gs.sun, PLANT_COST);

    /*
     * Main loop. One iteration per displayed frame:
     *   1. Input   - drain keyboard events, update cursor / sun
     *   2. Update  - game.c runs sun, spawns, firing, movement,
     *                collisions, win/lose
     *   3. Render  - render_frame writes the 48 shape-table slots and
     *                4 x 8 background cells via ioctl
     *   4. Pace    - sleep whatever is left of the 16667 us budget
     * Exit paths: STATE_WIN, STATE_LOSE, ESC (gs.state = -1).
     */
    /* Main game loop */
    long long frame_start;

    while (gs.state >= 0) {
        frame_start = get_time_usec();

        /* 1. Process input */
        process_input(&gs);
        if (gs.state < 0)
            break;

        /* 2. Update game logic */
        game_update(&gs);

        /* 3. Render to FPGA */
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

        /* 4. Frame timing: sleep for remainder of frame */
        /* If the frame overran (elapsed >= FRAME_USEC) we skip the
         * sleep and the next iteration starts right away. That's a
         * dropped frame, but simulation time keeps up. */
        long long elapsed = get_time_usec() - frame_start;
        if (elapsed < FRAME_USEC)
            usleep(FRAME_USEC - elapsed);
    }

    printf("\nCleaning up...\n");
    input_close();
    close(pvz_fd);
    return 0;
}
