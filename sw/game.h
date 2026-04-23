#ifndef _GAME_H
#define _GAME_H

/*
 * PvZ game-state constants and public API.
 *
 * How it fits in:
 *   Shared between game.c (the simulation), render.c (reads gs to
 *   paint shapes), main.c (drives the loop), and the host-compiled
 *   test/test_game.c. The #defines here are the single source of
 *   truth for grid layout and balance numbers. See design-doc
 *   section "Display Layout" for coordinate mapping and section 3.1
 *   for the full loop.
 *
 * Grid layout:
 *   The playfield is GRID_ROWS x GRID_COLS cells. Cell (r, c) maps
 *   to screen pixels at x = c * CELL_WIDTH, y = GAME_AREA_Y + r *
 *   CELL_HEIGHT. The HUD owns the top GAME_AREA_Y pixels, so no
 *   gameplay elements live above that band.
 *
 * Balance numbers:
 *   Every cooldown and interval is in 60 Hz frames, keeping the
 *   simulation clock-free. "2 seconds" is literally 2 * 60.
 *
 * Public API: game_init, game_update, game_place_plant,
 * game_remove_plant.
 */

/* Grid dimensions */
/* See design-doc "Display Layout". 4 rows x 8 cols of 80x90 cells
 * starting at y = 60 fills the middle 360 px of the 480 px display. */
#define GRID_ROWS     4
#define GRID_COLS     8

/* Game area pixel coordinates */
#define GAME_AREA_Y   60   // HUD band height; grid origin row in pixels
#define CELL_WIDTH    80   // cell pitch on x
#define CELL_HEIGHT   90   // cell pitch on y

/* Screen dimensions */
#define SCREEN_W      640
#define SCREEN_H      480

/* Plant constants */
#define PLANT_COST         50
#define PLANT_FIRE_COOLDOWN 120  /* frames (2 seconds at 60fps) */
#define PLANT_HP            3    /* hits before a plant is destroyed */

/* Zombie constants */
/* MAX_ZOMBIES bounds the size of gs->zombies and by extension the
 * shape-table slots (32..36) that render.c reserves for zombies. */
#define MAX_ZOMBIES          5
#define ZOMBIE_HP            3
#define ZOMBIE_SPEED_FRAMES  3   /* move 1 pixel every N frames (~20 px/s) */
#define ZOMBIE_WIDTH         30
#define ZOMBIE_HEIGHT        70
#define TOTAL_ZOMBIES        5
#define ZOMBIE_SPAWN_MIN    (8 * 60)  /* 8 seconds in frames */
#define ZOMBIE_SPAWN_MAX    (15 * 60) /* 15 seconds in frames */
#define ZOMBIE_EAT_COOLDOWN 60       /* frames between bites (~1 sec at 60fps) */

/* Projectile constants */
/* Pool size. Only the first 6 active peas get drawn (render.c
 * allocates shape slots 37..42); the simulation still tracks the
 * rest for collision. */
#define MAX_PROJECTILES     16
#define PEA_SPEED           2   /* pixels per frame (~120 px/s) */
#define PEA_DAMAGE          1
#define PEA_SIZE            8

/* Sun economy */
#define INITIAL_SUN         100
#define SUN_INCREMENT       25
#define SUN_INTERVAL        (8 * 60)  /* 8 seconds in frames */

/* Game states */
#define STATE_PLAYING  0
#define STATE_WIN      1
#define STATE_LOSE     2

/* Plant types */
#define PLANT_NONE        0
#define PLANT_PEASHOOTER  1

/*
 * A plant in one grid cell. Stored densely in game_state_t::grid
 * [r][c], so "no plant" is PLANT_NONE rather than a null pointer.
 */
typedef struct {
    int type;           /* PLANT_NONE or PLANT_PEASHOOTER */
    int fire_cooldown;  /* frames until next shot */
    int hp;             /* hit points remaining */
} plant_t;

/*
 * A zombie in the sparse pool. Slots with active == 0 are free for
 * update_spawning to reuse. x_pixel moves left every
 * ZOMBIE_SPEED_FRAMES frames; eating flips to 1 on contact.
 */
typedef struct {
    int active;
    int row;
    int x_pixel;        /* pixel x position (moves leftward) */
    int hp;
    int move_counter;   /* counts frames until next pixel move */
    int eating;         /* 1 if currently eating a plant */
    int eat_timer;      /* frames until next bite */
} zombie_t;

/*
 * A pea in the sparse pool. Spawned at the right edge of a
 * peashooter's cell, moves right PEA_SPEED pixels per frame.
 */
typedef struct {
    int active;
    int row;
    int x_pixel;        /* pixel x position (moves rightward) */
} projectile_t;

/*
 * The full game state. Passed by pointer to every game_* function
 * and to render_frame. No hidden globals, so the simulation is pure
 * and can be checkpointed or unit-tested on the host.
 */
typedef struct {
    /* Grid */
    plant_t grid[GRID_ROWS][GRID_COLS];

    /* Entities */
    zombie_t zombies[MAX_ZOMBIES];
    projectile_t projectiles[MAX_PROJECTILES];

    /* Cursor */
    int cursor_row;
    int cursor_col;

    /* Economy */
    int sun;
    int sun_timer;      /* frames until next sun increment */

    /* Spawn */
    int zombies_spawned;
    int spawn_timer;    /* frames until next zombie spawn */

    /* Game state */
    int state;          /* STATE_PLAYING, STATE_WIN, STATE_LOSE */
    int frame_count;
} game_state_t;

/*
 * Initialize game state to starting values.
 */
void game_init(game_state_t *gs);

/*
 * Process one frame of game logic.
 * Call after input has been processed.
 */
void game_update(game_state_t *gs);

/*
 * Attempt to place a peashooter at the cursor position.
 * Returns 1 on success, 0 if blocked.
 */
int game_place_plant(game_state_t *gs);

/*
 * Remove the plant at the cursor position.
 * Returns 1 if a plant was removed, 0 if cell was empty.
 */
int game_remove_plant(game_state_t *gs);

#endif /* _GAME_H */
