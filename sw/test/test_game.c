/*
 * Host-compilable unit tests for game logic (v3)
 *
 * Covers: initial state, placement per selected plant type, plant
 * removal, sun economy (passive drip + sunflower production), peashooter
 * firing, per-zombie-type HP tiers, wave-table determinism, selection
 * cycling, win/lose conditions.
 *
 * Compile and run on host: gcc -o test_game test_game.c ../game.c && ./test_game
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../game.h"
#include "../pvz.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(cond, msg) do { \
    if (cond) { \
        tests_passed++; \
    } else { \
        printf("FAIL: %s (line %d)\n", msg, __LINE__); \
        tests_failed++; \
    } \
} while (0)

/* Put the wave beyond the game_update horizon so tests that don't care
 * about spawning don't have random zombies appearing mid-test. */
static void park_waves(game_state_t *gs)
{
    gs->wave_index = TOTAL_ZOMBIES;
}

static void test_init(void)
{
    game_state_t gs;
    game_init(&gs);

    ASSERT(gs.sun == INITIAL_SUN, "initial sun should be 100");
    ASSERT(gs.state == STATE_PLAYING, "initial state should be PLAYING");
    ASSERT(gs.cursor_row == 0, "cursor row should start at 0");
    ASSERT(gs.cursor_col == 0, "cursor col should start at 0");
    ASSERT(gs.zombies_spawned == 0, "no zombies spawned initially");
    ASSERT(gs.selected_plant == 0, "initial selected_plant is 0 (peashooter)");
    ASSERT(gs.wave_index == 0, "wave_index starts at 0");

    /* All grid cells should be empty */
    for (int r = 0; r < GRID_ROWS; r++)
        for (int c = 0; c < GRID_COLS; c++)
            ASSERT(gs.grid[r][c].type == PLANT_NONE, "grid cell should be empty");

    printf("  test_init: OK\n");
}

static void test_place_plant(void)
{
    game_state_t gs;
    game_init(&gs);
    park_waves(&gs);

    gs.cursor_row = 0;
    gs.cursor_col = 0;
    gs.selected_plant = 0; /* Peashooter */
    int result = game_place_plant(&gs, NULL);

    ASSERT(result == 1, "plant placement should succeed");
    ASSERT(gs.grid[0][0].type == PLANT_PEASHOOTER, "cell should have peashooter");
    ASSERT(gs.grid[0][0].hp == plant_max_hp(PLANT_PEASHOOTER), "peashooter HP should be 3");
    ASSERT(gs.sun == INITIAL_SUN - plant_cost(PLANT_PEASHOOTER),
           "sun should decrease by peashooter cost");

    /* Try to place another plant on same cell */
    gs.sun = 100;
    result = game_place_plant(&gs, NULL);
    ASSERT(result == 0, "placing on occupied cell should fail");

    /* Try to place with insufficient sun */
    gs.cursor_col = 1;
    gs.sun = 10;
    result = game_place_plant(&gs, NULL);
    ASSERT(result == 0, "placing with insufficient sun should fail");

    printf("  test_place_plant: OK\n");
}

static void test_place_selects_type(void)
{
    game_state_t gs;
    game_init(&gs);
    park_waves(&gs);
    gs.sun = 500;

    gs.cursor_row = 0; gs.cursor_col = 0;
    gs.selected_plant = 0;
    game_place_plant(&gs, NULL);
    ASSERT(gs.grid[0][0].type == PLANT_PEASHOOTER,
           "selected_plant=0 places a peashooter");
    ASSERT(gs.grid[0][0].hp == 3, "peashooter HP = 3");

    gs.cursor_col = 1;
    gs.selected_plant = 1;
    game_place_plant(&gs, NULL);
    ASSERT(gs.grid[0][1].type == PLANT_SUNFLOWER,
           "selected_plant=1 places a sunflower");
    ASSERT(gs.grid[0][1].hp == 2, "sunflower HP = 2");

    gs.cursor_col = 2;
    gs.selected_plant = 2;
    game_place_plant(&gs, NULL);
    ASSERT(gs.grid[0][2].type == PLANT_WALLNUT,
           "selected_plant=2 places a wall-nut");
    ASSERT(gs.grid[0][2].hp == 8, "wall-nut HP = 8");

    printf("  test_place_selects_type: OK\n");
}

static void test_remove_plant(void)
{
    game_state_t gs;
    game_init(&gs);
    park_waves(&gs);

    gs.cursor_row = 1;
    gs.cursor_col = 2;
    gs.selected_plant = 0;
    game_place_plant(&gs, NULL);
    int result = game_remove_plant(&gs);

    ASSERT(result == 1, "removal should succeed");
    ASSERT(gs.grid[1][2].type == PLANT_NONE, "cell should be empty after removal");

    result = game_remove_plant(&gs);
    ASSERT(result == 0, "removing from empty cell should fail");

    printf("  test_remove_plant: OK\n");
}

static void test_sun_economy(void)
{
    game_state_t gs;
    game_init(&gs);
    park_waves(&gs); /* don't let wave spawns interfere */

    int initial_sun = gs.sun;
    /* Run frames until passive sun increments */
    for (int i = 0; i < SUN_INTERVAL + 1; i++)
        game_update(&gs, NULL);

    ASSERT(gs.sun == initial_sun + SUN_INCREMENT,
           "sun should increase by 25 after 8 seconds");

    printf("  test_sun_economy: OK\n");
}

static void test_sunflower_produces_sun(void)
{
    game_state_t gs;
    game_init(&gs);
    park_waves(&gs);
    gs.sun = 500;

    /* Place a sunflower */
    gs.cursor_row = 0; gs.cursor_col = 0;
    gs.selected_plant = 1;
    game_place_plant(&gs, NULL);
    int sun_after_place = gs.sun;

    /* SUNFLOWER_PRODUCE_COOLDOWN is 600.  After the passive drip (at
     * frame SUN_INTERVAL = 480) the sunflower hasn't produced yet.
     * After exactly 600 more frames of simulation starting right after
     * place we should see +25 from the sunflower.  The passive drip
     * also fires every SUN_INTERVAL = 480 frames; account for both. */

    /* Snapshot sun after ticking once to check counter initialised. */
    game_update(&gs, NULL); /* frame 1: sunflower timer goes 600 -> 599 */
    ASSERT(gs.grid[0][0].fire_cooldown == SUNFLOWER_PRODUCE_COOLDOWN - 1,
           "sunflower timer decrements once per frame");

    /* Tick up to frame 600: sunflower should fire at frame 600, giving
     * us +25.  Compute expected passive drip between (frame 1) and
     * (frame 600) — that's one drip at frame 480 => +25. */
    int before = sun_after_place;
    while (gs.frame_count < SUNFLOWER_PRODUCE_COOLDOWN)
        game_update(&gs, NULL);

    /* After 600 frames: one drip (+25) and one sunflower produce (+25). */
    ASSERT(gs.sun == before + 2 * SUN_INCREMENT,
           "sunflower + passive drip produced +50 after 600 frames");

    /* Next sunflower tick (frame 1200): another +25 from sunflower and
     * another +25 from the passive drip at frame 960 (since previous
     * drip reset at 480 and interval is 480). */
    before = gs.sun;
    while (gs.frame_count < 2 * SUNFLOWER_PRODUCE_COOLDOWN)
        game_update(&gs, NULL);
    ASSERT(gs.sun == before + 2 * SUN_INCREMENT,
           "sunflower + passive drip produced +50 more after another 600 frames");

    printf("  test_sunflower_produces_sun: OK\n");
}

static void test_sunflower_and_wallnut_do_not_fire(void)
{
    game_state_t gs;
    game_init(&gs);
    park_waves(&gs);
    gs.sun = 500;

    /* Sunflower at (0,0), wall-nut at (1,0) */
    gs.cursor_row = 0; gs.cursor_col = 0;
    gs.selected_plant = 1;
    game_place_plant(&gs, NULL);
    gs.cursor_row = 1; gs.cursor_col = 0;
    gs.selected_plant = 2;
    game_place_plant(&gs, NULL);

    /* Put a zombie in each row so that a peashooter would fire, and
     * confirm no peas are spawned. */
    gs.zombies[0].active = 1;
    gs.zombies[0].type = ZOMBIE_BASIC;
    gs.zombies[0].row = 0;
    gs.zombies[0].x_pixel = 500;
    gs.zombies[0].hp = 3;
    gs.zombies[1].active = 1;
    gs.zombies[1].type = ZOMBIE_BASIC;
    gs.zombies[1].row = 1;
    gs.zombies[1].x_pixel = 500;
    gs.zombies[1].hp = 3;

    /* Run plenty of frames */
    for (int i = 0; i < SUNFLOWER_PRODUCE_COOLDOWN + 120; i++)
        game_update(&gs, NULL);

    for (int i = 0; i < MAX_PROJECTILES; i++)
        ASSERT(gs.projectiles[i].active == 0,
               "no pea should have been spawned by sunflower or wall-nut");

    printf("  test_sunflower_and_wallnut_do_not_fire: OK\n");
}

static void test_peashooter_fires(void)
{
    game_state_t gs;
    game_init(&gs);
    park_waves(&gs);
    gs.sun = 500;

    /* Peashooter at (0,0), zombie in row 0 */
    gs.cursor_row = 0; gs.cursor_col = 0;
    gs.selected_plant = 0;
    game_place_plant(&gs, NULL);

    gs.zombies[0].active = 1;
    gs.zombies[0].type = ZOMBIE_BASIC;
    gs.zombies[0].row = 0;
    gs.zombies[0].x_pixel = 500;
    gs.zombies[0].hp = 3;

    /* Run exactly PEASHOOTER_FIRE_COOLDOWN frames; a pea should spawn. */
    for (int i = 0; i < PEASHOOTER_FIRE_COOLDOWN; i++)
        game_update(&gs, NULL);

    int fired = 0;
    for (int i = 0; i < MAX_PROJECTILES; i++)
        if (gs.projectiles[i].active && gs.projectiles[i].row == 0)
            fired = 1;
    ASSERT(fired, "peashooter should have fired a pea after cooldown");

    printf("  test_peashooter_fires: OK\n");
}

static void test_conehead_takes_six_hits(void)
{
    game_state_t gs;
    game_init(&gs);
    park_waves(&gs);

    gs.zombies[0].active = 1;
    gs.zombies[0].type = ZOMBIE_CONEHEAD;
    gs.zombies[0].row = 0;
    gs.zombies[0].x_pixel = 300;
    gs.zombies[0].hp = zombie_max_hp(ZOMBIE_CONEHEAD);

    /* Manually land 5 hits; zombie should still be alive */
    for (int h = 0; h < 5; h++) {
        gs.projectiles[0].active = 1;
        gs.projectiles[0].row = 0;
        gs.projectiles[0].x_pixel = 300;
        game_update(&gs, NULL);
    }
    ASSERT(gs.zombies[0].active == 1, "conehead alive after 5 peas");

    /* 6th pea kills */
    gs.projectiles[0].active = 1;
    gs.projectiles[0].row = 0;
    gs.projectiles[0].x_pixel = 300;
    game_update(&gs, NULL);
    ASSERT(gs.zombies[0].active == 0, "conehead dies after 6 peas");

    printf("  test_conehead_takes_six_hits: OK\n");
}

static void test_buckethead_takes_twelve_hits(void)
{
    game_state_t gs;
    game_init(&gs);
    park_waves(&gs);

    gs.zombies[0].active = 1;
    gs.zombies[0].type = ZOMBIE_BUCKETHEAD;
    gs.zombies[0].row = 0;
    gs.zombies[0].x_pixel = 300;
    gs.zombies[0].hp = zombie_max_hp(ZOMBIE_BUCKETHEAD);

    for (int h = 0; h < 11; h++) {
        gs.projectiles[0].active = 1;
        gs.projectiles[0].row = 0;
        gs.projectiles[0].x_pixel = 300;
        game_update(&gs, NULL);
    }
    ASSERT(gs.zombies[0].active == 1, "buckethead alive after 11 peas");

    gs.projectiles[0].active = 1;
    gs.projectiles[0].row = 0;
    gs.projectiles[0].x_pixel = 300;
    game_update(&gs, NULL);
    ASSERT(gs.zombies[0].active == 0, "buckethead dies after 12 peas");

    printf("  test_buckethead_takes_twelve_hits: OK\n");
}

static void test_wave_determinism(void)
{
    game_state_t a, b;
    srand(42);
    game_init(&a);
    srand(42);
    game_init(&b);

    for (int i = 0; i < TOTAL_ZOMBIES; i++) {
        ASSERT(a.wave[i].spawn_frame == b.wave[i].spawn_frame,
               "wave spawn_frame matches under same seed");
        ASSERT(a.wave[i].type == b.wave[i].type,
               "wave type matches under same seed");
    }

    /* Also verify the jitter bound is respected */
    int template[] = { 480, 780, 1080, 1440, 1800, 2160, 2580, 3000, 3420, 3900 };
    for (int i = 0; i < TOTAL_ZOMBIES; i++) {
        int d = a.wave[i].spawn_frame - template[i];
        if (d < 0) d = -d;
        ASSERT(d <= 60, "wave spawn jitter within +/- 60 frames");
    }

    printf("  test_wave_determinism: OK\n");
}

static void test_cycle_plant_wrap(void)
{
    game_state_t gs;
    game_init(&gs);

    ASSERT(gs.selected_plant == 0, "initially peashooter");
    cycle_plant_next(&gs);
    ASSERT(gs.selected_plant == 1, "next -> sunflower");
    cycle_plant_next(&gs);
    ASSERT(gs.selected_plant == 2, "next -> wall-nut");
    cycle_plant_next(&gs);
    ASSERT(gs.selected_plant == 0, "next wraps back to peashooter");

    cycle_plant_prev(&gs);
    ASSERT(gs.selected_plant == 2, "prev from 0 wraps to 2");
    cycle_plant_prev(&gs);
    ASSERT(gs.selected_plant == 1, "prev -> sunflower");

    /* Game over freezes the selection */
    gs.state = STATE_WIN;
    cycle_plant_next(&gs);
    ASSERT(gs.selected_plant == 1, "no cycle while not playing");
    cycle_plant_prev(&gs);
    ASSERT(gs.selected_plant == 1, "no cycle while not playing (prev)");

    printf("  test_cycle_plant_wrap: OK\n");
}

static void test_collision(void)
{
    game_state_t gs;
    game_init(&gs);
    park_waves(&gs);

    gs.zombies[0].active = 1;
    gs.zombies[0].type = ZOMBIE_BASIC;
    gs.zombies[0].row = 0;
    gs.zombies[0].x_pixel = 300;
    gs.zombies[0].hp = 3;

    gs.projectiles[0].active = 1;
    gs.projectiles[0].row = 0;
    gs.projectiles[0].x_pixel = 300;

    gs.zombies_spawned = 1;

    game_update(&gs, NULL);

    ASSERT(gs.zombies[0].hp == ZOMBIE_HP - PEA_DAMAGE,
           "zombie HP should decrease by 1");
    ASSERT(gs.projectiles[0].active == 0,
           "pea should be deactivated after hit");

    printf("  test_collision: OK\n");
}

static void test_zombie_death(void)
{
    game_state_t gs;
    game_init(&gs);
    park_waves(&gs);

    gs.zombies[0].active = 1;
    gs.zombies[0].type = ZOMBIE_BASIC;
    gs.zombies[0].row = 0;
    gs.zombies[0].x_pixel = 300;
    gs.zombies[0].hp = 1;
    gs.zombies[0].move_counter = 0;

    gs.projectiles[0].active = 1;
    gs.projectiles[0].row = 0;
    gs.projectiles[0].x_pixel = 300;

    gs.zombies_spawned = 1;

    game_update(&gs, NULL);

    ASSERT(gs.zombies[0].active == 0, "zombie should be dead");

    printf("  test_zombie_death: OK\n");
}

static void test_lose_condition(void)
{
    game_state_t gs;
    game_init(&gs);
    park_waves(&gs);

    gs.zombies[0].active = 1;
    gs.zombies[0].type = ZOMBIE_BASIC;
    gs.zombies[0].row = 0;
    gs.zombies[0].x_pixel = 1;
    gs.zombies[0].hp = 3;
    gs.zombies[0].move_counter = ZOMBIE_SPEED_FRAMES - 1;

    gs.zombies_spawned = 1;

    game_update(&gs, NULL);

    ASSERT(gs.state == STATE_LOSE, "game should be in LOSE state");

    printf("  test_lose_condition: OK\n");
}

static void test_win_condition(void)
{
    game_state_t gs;
    game_init(&gs);

    /* All waves consumed, all zombies dead */
    gs.zombies_spawned = TOTAL_ZOMBIES;
    gs.wave_index = TOTAL_ZOMBIES;
    for (int i = 0; i < MAX_ZOMBIES; i++)
        gs.zombies[i].active = 0;

    game_update(&gs, NULL);

    ASSERT(gs.state == STATE_WIN, "game should be in WIN state");

    printf("  test_win_condition: OK\n");
}

static void test_zombie_stops_at_plant(void)
{
    game_state_t gs;
    game_init(&gs);
    park_waves(&gs);

    gs.grid[0][3].type = PLANT_PEASHOOTER;
    gs.grid[0][3].hp = plant_max_hp(PLANT_PEASHOOTER);
    gs.grid[0][3].fire_cooldown = 99999;

    gs.zombies[0].active = 1;
    gs.zombies[0].type = ZOMBIE_BASIC;
    gs.zombies[0].row = 0;
    gs.zombies[0].x_pixel = 241;
    gs.zombies[0].hp = 3;
    gs.zombies[0].move_counter = ZOMBIE_SPEED_FRAMES - 1;
    gs.zombies[0].eating = 0;

    gs.zombies_spawned = TOTAL_ZOMBIES;

    game_update(&gs, NULL);

    ASSERT(gs.zombies[0].eating == 1, "zombie should be eating the plant");
    ASSERT(gs.zombies[0].x_pixel == 240, "zombie should have stopped at x=240");

    int prev_x = gs.zombies[0].x_pixel;
    game_update(&gs, NULL);
    ASSERT(gs.zombies[0].x_pixel == prev_x,
           "zombie should not move while eating");

    printf("  test_zombie_stops_at_plant: OK\n");
}

static void test_zombie_eats_and_destroys_plant(void)
{
    game_state_t gs;
    game_init(&gs);
    park_waves(&gs);

    gs.grid[1][2].type = PLANT_PEASHOOTER;
    gs.grid[1][2].hp = 1;
    gs.grid[1][2].fire_cooldown = 99999;

    gs.zombies[0].active = 1;
    gs.zombies[0].type = ZOMBIE_BASIC;
    gs.zombies[0].row = 1;
    gs.zombies[0].x_pixel = 2 * CELL_WIDTH;
    gs.zombies[0].hp = 3;
    gs.zombies[0].eating = 1;
    gs.zombies[0].eat_timer = 1;

    gs.zombies_spawned = TOTAL_ZOMBIES;

    game_update(&gs, NULL);

    ASSERT(gs.grid[1][2].type == PLANT_NONE,
           "plant should be destroyed after last HP eaten");
    ASSERT(gs.zombies[0].eating == 0,
           "zombie should stop eating after plant destroyed");

    printf("  test_zombie_eats_and_destroys_plant: OK\n");
}

static void test_zombie_resumes_after_eating(void)
{
    game_state_t gs;
    game_init(&gs);
    park_waves(&gs);

    gs.zombies[0].active = 1;
    gs.zombies[0].type = ZOMBIE_BASIC;
    gs.zombies[0].row = 0;
    gs.zombies[0].x_pixel = 3 * CELL_WIDTH;
    gs.zombies[0].hp = 3;
    gs.zombies[0].move_counter = ZOMBIE_SPEED_FRAMES - 1;
    gs.zombies[0].eating = 1;
    gs.zombies[0].eat_timer = 10;

    gs.grid[0][3].type = PLANT_NONE;

    gs.zombies_spawned = TOTAL_ZOMBIES;

    game_update(&gs, NULL);

    ASSERT(gs.zombies[0].eating == 0,
           "zombie should clear eating state when plant is gone");
    ASSERT(gs.zombies[0].x_pixel == 3 * CELL_WIDTH - 1,
           "zombie should resume moving after plant is gone");

    printf("  test_zombie_resumes_after_eating: OK\n");
}

static void test_two_zombies_eat_same_plant(void)
{
    game_state_t gs;
    game_init(&gs);
    park_waves(&gs);

    gs.grid[0][4].type = PLANT_PEASHOOTER;
    gs.grid[0][4].hp = 2;
    gs.grid[0][4].fire_cooldown = 99999;

    for (int i = 0; i < 2; i++) {
        gs.zombies[i].active = 1;
        gs.zombies[i].type = ZOMBIE_BASIC;
        gs.zombies[i].row = 0;
        gs.zombies[i].x_pixel = 4 * CELL_WIDTH + i;
        gs.zombies[i].hp = 3;
        gs.zombies[i].eating = 1;
        gs.zombies[i].eat_timer = 1;
    }

    gs.zombies_spawned = TOTAL_ZOMBIES;

    game_update(&gs, NULL);

    ASSERT(gs.grid[0][4].type == PLANT_NONE,
           "plant should be destroyed by two simultaneous bites");
    ASSERT(gs.zombies[1].eating == 0,
           "zombie 1 should stop eating after plant destroyed");

    game_update(&gs, NULL);
    ASSERT(gs.zombies[0].eating == 0,
           "zombie 0 should stop eating once it notices plant is gone");

    printf("  test_two_zombies_eat_same_plant: OK\n");
}

static void test_wave_spawn_types(void)
{
    game_state_t gs;
    srand(42);
    game_init(&gs);

    /* Walk until first wave entry scheduled frame.  Validate type +
     * HP of the first spawned zombie matches the template. */
    int first_spawn_frame = gs.wave[0].spawn_frame;
    int first_type = gs.wave[0].type;

    for (int i = 0; i < first_spawn_frame + 5; i++)
        game_update(&gs, NULL);

    /* Find the spawned zombie */
    int found = -1;
    for (int i = 0; i < MAX_ZOMBIES; i++)
        if (gs.zombies[i].active) { found = i; break; }
    ASSERT(found >= 0, "first wave entry spawned a zombie");
    if (found >= 0) {
        ASSERT(gs.zombies[found].type == first_type,
               "spawned zombie type matches wave entry");
        ASSERT(gs.zombies[found].hp == zombie_max_hp(first_type),
               "spawned zombie HP matches type");
    }
    ASSERT(gs.wave_index == 1, "wave_index advanced exactly once");

    printf("  test_wave_spawn_types: OK\n");
}

/* ---------- audio event tests ---------- */

static void test_pea_fire_emits_event(void)
{
    game_state_t gs;
    game_init(&gs);
    park_waves(&gs);

    gs.grid[0][0].type = PLANT_PEASHOOTER;
    gs.grid[0][0].hp = plant_max_hp(PLANT_PEASHOOTER);
    gs.grid[0][0].fire_cooldown = 0; /* ready to fire this tick */

    /* Zombie in same row so peashooter has a target */
    gs.zombies[0].active = 1;
    gs.zombies[0].type = ZOMBIE_BASIC;
    gs.zombies[0].row = 0;
    gs.zombies[0].x_pixel = SCREEN_W - 100;
    gs.zombies[0].hp = zombie_max_hp(ZOMBIE_BASIC);

    audio_events_t ev = { .flags = 0 };
    game_update(&gs, &ev);

    ASSERT(ev.flags & PVZ_AUDIO_EVENT(PVZ_SFX_PEA_FIRE),
           "PEA_FIRE event emitted when peashooter fires");
    printf("  test_pea_fire_emits_event: OK\n");
}

static void test_pea_hit_emits_event(void)
{
    game_state_t gs;
    game_init(&gs);
    park_waves(&gs);

    /* Pea already in flight, positioned at the zombie */
    gs.projectiles[0].active = 1;
    gs.projectiles[0].row = 0;
    gs.projectiles[0].x_pixel = 300;

    gs.zombies[0].active = 1;
    gs.zombies[0].type = ZOMBIE_BASIC;
    gs.zombies[0].row = 0;
    gs.zombies[0].x_pixel = 300;
    gs.zombies[0].hp = 3; /* survive the hit */

    audio_events_t ev = { .flags = 0 };
    game_update(&gs, &ev);

    ASSERT(ev.flags & PVZ_AUDIO_EVENT(PVZ_SFX_PEA_HIT),
           "PEA_HIT event emitted on pea-zombie collision");
    printf("  test_pea_hit_emits_event: OK\n");
}

static void test_zombie_death_emits_event(void)
{
    game_state_t gs;
    game_init(&gs);
    park_waves(&gs);

    /* Pea at zombie position, zombie at 1 HP (lethal hit) */
    gs.projectiles[0].active = 1;
    gs.projectiles[0].row = 0;
    gs.projectiles[0].x_pixel = 300;

    gs.zombies[0].active = 1;
    gs.zombies[0].type = ZOMBIE_BASIC;
    gs.zombies[0].row = 0;
    gs.zombies[0].x_pixel = 300;
    gs.zombies[0].hp = 1;

    gs.zombies_spawned = 1;

    audio_events_t ev = { .flags = 0 };
    game_update(&gs, &ev);

    ASSERT(gs.zombies[0].active == 0, "zombie is dead (precondition)");
    ASSERT(ev.flags & PVZ_AUDIO_EVENT(PVZ_SFX_ZOMBIE_DEATH),
           "ZOMBIE_DEATH event emitted when zombie HP reaches 0");
    printf("  test_zombie_death_emits_event: OK\n");
}

static void test_zombie_bite_emits_event(void)
{
    game_state_t gs;
    game_init(&gs);
    park_waves(&gs);

    /* Plant at col 2, zombie eating it with eat_timer=1 (expires this tick) */
    gs.grid[0][2].type = PLANT_PEASHOOTER;
    gs.grid[0][2].hp = 3;
    gs.grid[0][2].fire_cooldown = 99999;

    gs.zombies[0].active = 1;
    gs.zombies[0].type = ZOMBIE_BASIC;
    gs.zombies[0].row = 0;
    gs.zombies[0].x_pixel = 2 * CELL_WIDTH;
    gs.zombies[0].hp = 3;
    gs.zombies[0].eating = 1;
    gs.zombies[0].eat_timer = 1; /* expires on this tick */

    gs.zombies_spawned = TOTAL_ZOMBIES;

    audio_events_t ev = { .flags = 0 };
    game_update(&gs, &ev);

    ASSERT(ev.flags & PVZ_AUDIO_EVENT(PVZ_SFX_ZOMBIE_BITE),
           "ZOMBIE_BITE event emitted when zombie deals a bite");
    printf("  test_zombie_bite_emits_event: OK\n");
}

static void test_plant_place_emits_event(void)
{
    game_state_t gs;
    game_init(&gs);
    park_waves(&gs);

    gs.cursor_row = 0;
    gs.cursor_col = 0;
    gs.selected_plant = 0; /* Peashooter */
    /* Enough sun (default INITIAL_SUN=100 >= cost 50) */

    audio_events_t ev = { .flags = 0 };
    int result = game_place_plant(&gs, &ev);

    ASSERT(result == 1, "plant placement succeeded (precondition)");
    ASSERT(ev.flags & PVZ_AUDIO_EVENT(PVZ_SFX_PLANT_PLACE),
           "PLANT_PLACE event emitted on successful placement");
    printf("  test_plant_place_emits_event: OK\n");
}

static void test_wave_start_emits_event(void)
{
    /*
     * WAVE_START fires on every zombie spawn (one wave entry = one zombie).
     * The wave template has no grouping concept; each scheduled spawn is
     * independent, so the event fires each time a spawn lands.
     */
    game_state_t gs;
    srand(42);
    game_init(&gs);

    int first_spawn_frame = gs.wave[0].spawn_frame;

    /* Advance frame_count to one before the scheduled spawn */
    gs.frame_count = first_spawn_frame - 1;

    audio_events_t ev = { .flags = 0 };
    game_update(&gs, &ev); /* this tick reaches first_spawn_frame */

    ASSERT(gs.wave_index == 1, "first zombie spawned (precondition)");
    ASSERT(ev.flags & PVZ_AUDIO_EVENT(PVZ_SFX_WAVE_START),
           "WAVE_START event emitted when first zombie of wave spawns");
    printf("  test_wave_start_emits_event: OK\n");
}

int main(void)
{
    srand(42); /* Deterministic for testing */

    printf("Running game logic tests...\n\n");

    test_init();
    test_place_plant();
    test_place_selects_type();
    test_remove_plant();
    test_sun_economy();
    test_sunflower_produces_sun();
    test_sunflower_and_wallnut_do_not_fire();
    test_peashooter_fires();
    test_conehead_takes_six_hits();
    test_buckethead_takes_twelve_hits();
    test_wave_determinism();
    test_cycle_plant_wrap();
    test_collision();
    test_zombie_death();
    test_lose_condition();
    test_win_condition();
    test_zombie_stops_at_plant();
    test_zombie_eats_and_destroys_plant();
    test_zombie_resumes_after_eating();
    test_two_zombies_eat_same_plant();
    test_wave_spawn_types();

    /* audio event emission tests */
    test_pea_fire_emits_event();
    test_pea_hit_emits_event();
    test_zombie_death_emits_event();
    test_zombie_bite_emits_event();
    test_plant_place_emits_event();
    test_wave_start_emits_event();

    printf("\nResults: %d passed, %d failed\n",
           tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
