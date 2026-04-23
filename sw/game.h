#ifndef _GAME_H
#define _GAME_H

/* Grid dimensions */
#define GRID_ROWS     4
#define GRID_COLS     8

/* Game area pixel coordinates */
#define GAME_AREA_Y   60
#define CELL_WIDTH    80
#define CELL_HEIGHT   90

/* Screen dimensions */
#define SCREEN_W      640
#define SCREEN_H      480

/* Plant constants */
#define PLANT_COST         50     /* universal per-plant sun cost */
#define PEASHOOTER_FIRE_COOLDOWN    120  /* frames (2 seconds at 60fps) */
#define SUNFLOWER_PRODUCE_COOLDOWN  600  /* frames (10 seconds at 60fps) */
/* Legacy alias kept for any out-of-tree consumers */
#define PLANT_FIRE_COOLDOWN PEASHOOTER_FIRE_COOLDOWN

/* Zombie constants */
#define MAX_ZOMBIES          5
#define ZOMBIE_SPEED_FRAMES  3   /* move 1 pixel every N frames (~20 px/s) */
#define ZOMBIE_WIDTH         30
#define ZOMBIE_HEIGHT        70
#define TOTAL_ZOMBIES        10
#define ZOMBIE_EAT_COOLDOWN  60  /* frames between bites (~1 sec at 60fps) */

/* Projectile constants */
#define MAX_PROJECTILES     16
#define PEA_SPEED           2   /* pixels per frame (~120 px/s) */
#define PEA_DAMAGE          1
#define PEA_SIZE            8

/* Sun economy */
#define INITIAL_SUN         100
#define SUN_INCREMENT       25
#define SUN_INTERVAL        (8 * 60)  /* passive sun drip every 8 seconds */

/* Game states */
#define STATE_PLAYING  0
#define STATE_WIN      1
#define STATE_LOSE     2

/* Plant types */
typedef enum {
    PLANT_NONE       = 0,
    PLANT_PEASHOOTER = 1,
    PLANT_SUNFLOWER  = 2,
    PLANT_WALLNUT    = 3
} plant_type_t;

/* Number of selectable plant types (excludes PLANT_NONE) */
#define NUM_PLANT_TYPES 3

/* Zombie types */
typedef enum {
    ZOMBIE_BASIC      = 0,
    ZOMBIE_CONEHEAD   = 1,
    ZOMBIE_BUCKETHEAD = 2
} zombie_type_t;

/* Legacy Basic-zombie HP constant; per-type HP lives in game.c */
#define ZOMBIE_HP 3

typedef struct {
    int type;            /* plant_type_t */
    int fire_cooldown;   /* generic per-plant action timer (fire / produce sun / idle) */
    int hp;              /* hit points remaining */
} plant_t;

typedef struct {
    int active;
    int type;           /* zombie_type_t */
    int row;
    int x_pixel;        /* pixel x position (moves leftward) */
    int hp;
    int move_counter;   /* counts frames until next pixel move */
    int eating;         /* 1 if currently eating a plant */
    int eat_timer;      /* frames until next bite */
} zombie_t;

typedef struct {
    int active;
    int row;
    int x_pixel;        /* pixel x position (moves rightward) */
} projectile_t;

typedef struct {
    int spawn_frame;    /* frame_count at which this zombie should spawn */
    int type;           /* zombie_type_t */
} wave_entry_t;

typedef struct {
    /* Grid */
    plant_t grid[GRID_ROWS][GRID_COLS];

    /* Entities */
    zombie_t zombies[MAX_ZOMBIES];
    projectile_t projectiles[MAX_PROJECTILES];

    /* Cursor */
    int cursor_row;
    int cursor_col;

    /* HUD selection (0=Peashooter, 1=Sunflower, 2=Wallnut) */
    int selected_plant;

    /* Economy */
    int sun;
    int sun_timer;      /* frames until next sun increment */

    /* Wave spawn */
    wave_entry_t wave[TOTAL_ZOMBIES];
    int wave_index;
    int zombies_spawned;

    /* Game state */
    int state;          /* STATE_PLAYING, STATE_WIN, STATE_LOSE */
    int frame_count;
} game_state_t;

/*
 * Per-plant-type lookup: max HP and sun cost.
 * Declared here so tests and renderer can inspect the table.
 */
int plant_max_hp(int type);
int plant_cost(int type);
int zombie_max_hp(int type);

/*
 * Initialize game state to starting values.  Reads rand() to jitter
 * wave spawn frames; callers that need determinism should seed first.
 */
void game_init(game_state_t *gs);

/*
 * Process one frame of game logic.
 * Call after input has been processed.
 */
void game_update(game_state_t *gs);

/*
 * Attempt to place the currently selected plant at the cursor position.
 * Returns 1 on success, 0 if blocked.
 */
int game_place_plant(game_state_t *gs);

/*
 * Remove the plant at the cursor position.
 * Returns 1 if a plant was removed, 0 if cell was empty.
 */
int game_remove_plant(game_state_t *gs);

/*
 * Cycle the selected plant one slot backward or forward, modulo
 * NUM_PLANT_TYPES.  Called by main.c on INPUT_CYCLE_PREV / _NEXT.
 * No-op when state != STATE_PLAYING so HUD stays frozen on win/lose.
 */
void cycle_plant_prev(game_state_t *gs);
void cycle_plant_next(game_state_t *gs);

#endif /* _GAME_H */
