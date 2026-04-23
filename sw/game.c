/*
 * PvZ game logic (v3)
 *
 * Manages the 4x8 grid, zombie spawning via a pre-computed wave table,
 * per-plant-type action timers (peashooter fires, sunflower produces sun,
 * wall-nut absorbs), per-zombie-type HP tiers, projectile movement,
 * collision detection, sun economy, and win/lose conditions.
 */

#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "pvz.h"

/* Per-plant-type stats.  Indexed by plant_type_t. */
static const struct {
    int max_hp;
    int cost;
} plant_stats[] = {
    [PLANT_NONE]       = { 0, 0 },
    [PLANT_PEASHOOTER] = { 3, 50 },
    [PLANT_SUNFLOWER]  = { 2, 50 },
    [PLANT_WALLNUT]    = { 8, 50 },
};

/* Per-zombie-type max HP.  Indexed by zombie_type_t. */
static const int zombie_hp_table[] = {
    [ZOMBIE_BASIC]      = 3,
    [ZOMBIE_CONEHEAD]   = 6,
    [ZOMBIE_BUCKETHEAD] = 12,
};

/* Level wave template: 10 hand-tuned zombies spread over ~65 seconds.
 * Each spawn_frame is jittered by +/- 60 frames at game_init. */
static const wave_entry_t WAVE_TEMPLATE[TOTAL_ZOMBIES] = {
    { .spawn_frame =  480, .type = ZOMBIE_BASIC      },
    { .spawn_frame =  780, .type = ZOMBIE_BASIC      },
    { .spawn_frame = 1080, .type = ZOMBIE_BASIC      },
    { .spawn_frame = 1440, .type = ZOMBIE_CONEHEAD   },
    { .spawn_frame = 1800, .type = ZOMBIE_BASIC      },
    { .spawn_frame = 2160, .type = ZOMBIE_CONEHEAD   },
    { .spawn_frame = 2580, .type = ZOMBIE_BASIC      },
    { .spawn_frame = 3000, .type = ZOMBIE_CONEHEAD   },
    { .spawn_frame = 3420, .type = ZOMBIE_BUCKETHEAD },
    { .spawn_frame = 3900, .type = ZOMBIE_BASIC      },
};

int plant_max_hp(int type)
{
    if (type < 0 || type > PLANT_WALLNUT)
        return 0;
    return plant_stats[type].max_hp;
}

int plant_cost(int type)
{
    if (type < 0 || type > PLANT_WALLNUT)
        return 0;
    return plant_stats[type].cost;
}

int zombie_max_hp(int type)
{
    if (type < 0 || type > ZOMBIE_BUCKETHEAD)
        return 0;
    return zombie_hp_table[type];
}

/* Uniform jitter in [-60, +60] using rand() */
static int wave_jitter(void)
{
    return (rand() % 121) - 60;
}

void game_init(game_state_t *gs)
{
    memset(gs, 0, sizeof(*gs));

    gs->sun = INITIAL_SUN;
    gs->sun_timer = SUN_INTERVAL;
    gs->state = STATE_PLAYING;
    gs->cursor_row = 0;
    gs->cursor_col = 0;
    gs->selected_plant = 0;
    gs->zombies_spawned = 0;
    gs->wave_index = 0;
    gs->frame_count = 0;

    for (int i = 0; i < TOTAL_ZOMBIES; i++) {
        gs->wave[i].spawn_frame = WAVE_TEMPLATE[i].spawn_frame + wave_jitter();
        gs->wave[i].type = WAVE_TEMPLATE[i].type;
    }
}

int game_place_plant(game_state_t *gs, audio_events_t *ev)
{
    int r = gs->cursor_row;
    int c = gs->cursor_col;

    /* Map selected_plant (0/1/2) to PLANT_PEASHOOTER/_SUNFLOWER/_WALLNUT */
    int types[NUM_PLANT_TYPES] = {
        PLANT_PEASHOOTER, PLANT_SUNFLOWER, PLANT_WALLNUT
    };
    int sel = gs->selected_plant;
    if (sel < 0 || sel >= NUM_PLANT_TYPES)
        return 0;
    int type = types[sel];
    int cost = plant_cost(type);

    if (gs->grid[r][c].type != PLANT_NONE)
        return 0;
    if (gs->sun < cost)
        return 0;

    gs->grid[r][c].type = type;
    gs->grid[r][c].hp = plant_max_hp(type);
    /* Initial action timer:
     *   Peashooter: full fire cooldown (first shot after 2 s)
     *   Sunflower:  full produce cooldown (first sun after 10 s)
     *   Wall-nut:   zero (no action)
     */
    switch (type) {
    case PLANT_PEASHOOTER:
        gs->grid[r][c].fire_cooldown = PEASHOOTER_FIRE_COOLDOWN;
        break;
    case PLANT_SUNFLOWER:
        gs->grid[r][c].fire_cooldown = SUNFLOWER_PRODUCE_COOLDOWN;
        break;
    default:
        gs->grid[r][c].fire_cooldown = 0;
        break;
    }
    gs->sun -= cost;
    if (ev) ev->flags |= PVZ_AUDIO_EVENT(PVZ_SFX_PLANT_PLACE);
    return 1;
}

int game_remove_plant(game_state_t *gs)
{
    int r = gs->cursor_row;
    int c = gs->cursor_col;

    if (gs->grid[r][c].type == PLANT_NONE)
        return 0;

    gs->grid[r][c].type = PLANT_NONE;
    gs->grid[r][c].fire_cooldown = 0;
    gs->grid[r][c].hp = 0;
    return 1;
}

void cycle_plant_prev(game_state_t *gs)
{
    if (gs->state != STATE_PLAYING)
        return;
    gs->selected_plant = (gs->selected_plant + NUM_PLANT_TYPES - 1)
                         % NUM_PLANT_TYPES;
}

void cycle_plant_next(game_state_t *gs)
{
    if (gs->state != STATE_PLAYING)
        return;
    gs->selected_plant = (gs->selected_plant + 1) % NUM_PLANT_TYPES;
}

/* Check if any active zombie is in the given row */
static int zombie_in_row(game_state_t *gs, int row)
{
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        if (gs->zombies[i].active && gs->zombies[i].row == row)
            return 1;
    }
    return 0;
}

/* Spawn a new pea projectile at the given grid cell */
static void spawn_pea(game_state_t *gs, int row, int col, audio_events_t *ev)
{
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!gs->projectiles[i].active) {
            gs->projectiles[i].active = 1;
            gs->projectiles[i].row = row;
            /* Start at the right edge of the plant's cell */
            gs->projectiles[i].x_pixel = (col + 1) * CELL_WIDTH;
            if (ev) ev->flags |= PVZ_AUDIO_EVENT(PVZ_SFX_PEA_FIRE);
            return;
        }
    }
    /* No free slot; pea is lost */
}

/* Update zombie positions, eating, and check for lose condition */
static void update_zombies(game_state_t *gs, audio_events_t *ev)
{
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        zombie_t *z = &gs->zombies[i];
        if (!z->active)
            continue;

        if (z->eating) {
            /* Re-check that the plant still exists (another zombie may
             * have destroyed it) */
            int col = z->x_pixel / CELL_WIDTH;
            if (col < 0 || col >= GRID_COLS ||
                gs->grid[z->row][col].type == PLANT_NONE) {
                z->eating = 0;
                z->eat_timer = 0;
                /* Fall through to movement below */
            } else {
                /* Continue eating: deal damage on timer */
                z->eat_timer--;
                if (z->eat_timer <= 0) {
                    /* Bite lands — emit ZOMBIE_BITE */
                    if (ev) ev->flags |= PVZ_AUDIO_EVENT(PVZ_SFX_ZOMBIE_BITE);
                    gs->grid[z->row][col].hp--;
                    if (gs->grid[z->row][col].hp <= 0) {
                        gs->grid[z->row][col].type = PLANT_NONE;
                        gs->grid[z->row][col].fire_cooldown = 0;
                        gs->grid[z->row][col].hp = 0;
                        z->eating = 0;
                    } else {
                        z->eat_timer = ZOMBIE_EAT_COOLDOWN;
                    }
                }
                continue; /* Don't move while eating */
            }
        }

        /* Movement */
        z->move_counter++;
        if (z->move_counter >= ZOMBIE_SPEED_FRAMES) {
            z->move_counter = 0;
            z->x_pixel--;

            /* Lose condition: zombie reached left edge */
            if (z->x_pixel <= 0) {
                gs->state = STATE_LOSE;
                return;
            }

            /* Check for plant collision after moving */
            int col = z->x_pixel / CELL_WIDTH;
            if (col >= 0 && col < GRID_COLS &&
                gs->grid[z->row][col].type != PLANT_NONE) {
                z->eating = 1;
                z->eat_timer = ZOMBIE_EAT_COOLDOWN;
            }
        }
    }
}

/* Update projectile positions */
static void update_projectiles(game_state_t *gs)
{
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        projectile_t *p = &gs->projectiles[i];
        if (!p->active)
            continue;

        p->x_pixel += PEA_SPEED;

        /* Remove if off-screen */
        if (p->x_pixel > SCREEN_W)
            p->active = 0;
    }
}

/* Per-plant-type update: fire peas, produce sun, or no-op */
static void update_plants(game_state_t *gs, audio_events_t *ev)
{
    for (int r = 0; r < GRID_ROWS; r++) {
        for (int c = 0; c < GRID_COLS; c++) {
            plant_t *p = &gs->grid[r][c];

            switch (p->type) {
            case PLANT_PEASHOOTER:
                if (p->fire_cooldown > 0)
                    p->fire_cooldown--;
                if (p->fire_cooldown == 0 && zombie_in_row(gs, r)) {
                    spawn_pea(gs, r, c, ev);
                    p->fire_cooldown = PEASHOOTER_FIRE_COOLDOWN;
                }
                break;

            case PLANT_SUNFLOWER:
                if (p->fire_cooldown > 0)
                    p->fire_cooldown--;
                if (p->fire_cooldown == 0) {
                    gs->sun += SUN_INCREMENT;
                    p->fire_cooldown = SUNFLOWER_PRODUCE_COOLDOWN;
                }
                break;

            case PLANT_WALLNUT:
            default:
                /* No action; timer stays at 0 */
                break;
            }
        }
    }
}

/* Check pea-zombie collisions */
static void check_collisions(game_state_t *gs, audio_events_t *ev)
{
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        projectile_t *p = &gs->projectiles[i];
        if (!p->active)
            continue;

        for (int j = 0; j < MAX_ZOMBIES; j++) {
            zombie_t *z = &gs->zombies[j];
            if (!z->active || z->row != p->row)
                continue;

            /* Collision: pea overlaps zombie bounding box */
            int z_left = z->x_pixel;
            int z_right = z->x_pixel + ZOMBIE_WIDTH;
            int p_left = p->x_pixel;
            int p_right = p->x_pixel + PEA_SIZE;

            if (p_right >= z_left && p_left <= z_right) {
                /* Hit! — emit PEA_HIT before deactivating pea */
                if (ev) ev->flags |= PVZ_AUDIO_EVENT(PVZ_SFX_PEA_HIT);
                z->hp -= PEA_DAMAGE;
                p->active = 0;

                if (z->hp <= 0) {
                    z->active = 0;
                    if (ev) ev->flags |= PVZ_AUDIO_EVENT(PVZ_SFX_ZOMBIE_DEATH);
                }

                break; /* Each pea hits only one zombie */
            }
        }
    }
}

/* Spawn zombies from the wave table */
static void update_spawning(game_state_t *gs, audio_events_t *ev)
{
    if (gs->wave_index >= TOTAL_ZOMBIES)
        return;

    if (gs->frame_count < gs->wave[gs->wave_index].spawn_frame)
        return;

    /* Find a free zombie slot */
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        if (!gs->zombies[i].active) {
            int type = gs->wave[gs->wave_index].type;
            gs->zombies[i].active = 1;
            gs->zombies[i].type = type;
            gs->zombies[i].row = rand() % GRID_ROWS;
            gs->zombies[i].x_pixel = SCREEN_W - 1;
            gs->zombies[i].hp = zombie_max_hp(type);
            gs->zombies[i].move_counter = 0;
            gs->zombies[i].eating = 0;
            gs->zombies[i].eat_timer = 0;
            gs->zombies_spawned++;
            gs->wave_index++;
            if (ev) ev->flags |= PVZ_AUDIO_EVENT(PVZ_SFX_WAVE_START);
            return;
        }
    }
    /* No free slot: hold the scheduled spawn for next frame (wave_index
     * stays the same, so we'll retry until a slot frees up). */
}

/* Update sun economy (passive drip) */
static void update_sun(game_state_t *gs)
{
    gs->sun_timer--;
    if (gs->sun_timer <= 0) {
        gs->sun += SUN_INCREMENT;
        gs->sun_timer = SUN_INTERVAL;
    }
}

/* Check win condition */
static void check_win(game_state_t *gs)
{
    if (gs->zombies_spawned < TOTAL_ZOMBIES)
        return;
    if (gs->wave_index < TOTAL_ZOMBIES)
        return;

    for (int i = 0; i < MAX_ZOMBIES; i++) {
        if (gs->zombies[i].active)
            return;
    }

    gs->state = STATE_WIN;
}

void game_update(game_state_t *gs, audio_events_t *ev)
{
    if (gs->state != STATE_PLAYING)
        return;

    gs->frame_count++;

    update_sun(gs);
    update_spawning(gs, ev);
    update_plants(gs, ev);
    update_projectiles(gs);
    update_zombies(gs, ev);
    check_collisions(gs, ev);
    check_win(gs);
}
