/*
 * PvZ game logic.
 *
 * Owns the 4x8 grid, zombie spawn/movement, peashooter firing,
 * projectile movement, collision detection, sun economy, and
 * win/lose conditions.
 *
 * How it fits in:
 *   Callers are main.c (per-frame game_update plus place/remove from
 *   cursor commands) and the host-side test_game.c harness. All
 *   constants and struct layouts are in game.h. This file produces
 *   the pure game state the renderer reads out each frame; no I/O,
 *   so it's safe to compile on the host for unit tests. See design-
 *   doc section 3.1.
 *
 * Frame-based time:
 *   No wall-clock calls in here. Every duration is counted in
 *   frames, and main.c calls game_update() once per 60 Hz tick. So
 *   "8 seconds" is 8 * 60 frames, not a time_t. The simulation stays
 *   deterministic for host-compiled tests.
 *
 * The 4 x 8 grid:
 *   gs->grid[row][col] is a plant_t. Rows 0..3 map to pixel rows at
 *   y = 60 + r * 90, cols 0..7 to x = c * 80 (see game.h). Zombies
 *   and peas live in flat arrays (gs->zombies, gs->projectiles) with
 *   an active flag - a sparse pool rather than per-cell ownership.
 *
 * Sun economy:
 *   The player starts with INITIAL_SUN. A background timer adds
 *   SUN_INCREMENT every SUN_INTERVAL frames. Placing a plant
 *   deducts PLANT_COST; placement is refused when sun < PLANT_COST.
 *   (Design-doc section 3.1 "Sun Economy" specifies 150 start and a
 *   10 s / +25 cadence; the values in game.h are the current MVP's
 *   100 / 8 s settings - see the README for status.)
 *
 * Public API:
 *   game_init, game_update, game_place_plant, game_remove_plant.
 */

#include <stdlib.h>
#include <string.h>
#include "game.h"

/* Simple pseudo-random using the C library rand() */
static int random_range(int min, int max)
{
    return min + (rand() % (max - min + 1));
}

void game_init(game_state_t *gs)
{
    memset(gs, 0, sizeof(*gs));

    gs->sun = INITIAL_SUN;
    gs->sun_timer = SUN_INTERVAL;
    gs->state = STATE_PLAYING;
    gs->cursor_row = 0;
    gs->cursor_col = 0;
    gs->zombies_spawned = 0;
    gs->spawn_timer = random_range(ZOMBIE_SPAWN_MIN, ZOMBIE_SPAWN_MAX);
    gs->frame_count = 0;
}

/*
 * Try to spend PLANT_COST sun to put a peashooter under the cursor.
 * Refused when the cell is taken or the player can't afford it. The
 * "refuse if not enough sun" branch is what keeps the economy loop
 * from design-doc section "Sun Economy" intact.
 */
int game_place_plant(game_state_t *gs)
{
    int r = gs->cursor_row;
    int c = gs->cursor_col;

    if (gs->grid[r][c].type != PLANT_NONE)
        return 0;
    if (gs->sun < PLANT_COST)
        return 0; // not enough sun -> no placement, no deduction

    gs->grid[r][c].type = PLANT_PEASHOOTER;
    gs->grid[r][c].fire_cooldown = PLANT_FIRE_COOLDOWN;
    gs->grid[r][c].hp = PLANT_HP;
    gs->sun -= PLANT_COST; // only charge the player once placement succeeds
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
    return 1;
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
static void spawn_pea(game_state_t *gs, int row, int col)
{
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!gs->projectiles[i].active) {
            gs->projectiles[i].active = 1;
            gs->projectiles[i].row = row;
            /* Start at the right edge of the plant's cell */
            gs->projectiles[i].x_pixel = (col + 1) * CELL_WIDTH;
            return;
        }
    }
    /* No free slot; pea is lost */
}

/*
 * Step every active zombie through its move schedule.
 *
 * Each zombie has a move_counter that ticks up each frame; when it
 * reaches ZOMBIE_SPEED_FRAMES the zombie moves 1 pixel left and the
 * counter resets. Speed works out to 1 / ZOMBIE_SPEED_FRAMES pixels
 * per frame (design-doc pseudocode in section 3.1 just says "zombies
 * move left"; game.h has the concrete units).
 *
 * Two other behaviours here besides movement:
 *  - Lose: a zombie whose x_pixel drops to 0 has reached the house.
 *    Set STATE_LOSE and bail out of the update.
 *  - Eating: when a zombie walks into a column with a plant it flips
 *    eating = 1 and chews on a ZOMBIE_EAT_COOLDOWN frame timer, one
 *    HP per bite. While eating it does not advance. If another
 *    zombie already destroyed the plant this frame the top-of-loop
 *    check unlatches eating and movement resumes.
 */
/* Update zombie positions, eating, and check for lose condition */
static void update_zombies(game_state_t *gs)
{
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        zombie_t *z = &gs->zombies[i];
        if (!z->active)
            continue;

        if (z->eating) {
            /* Make sure the plant is still there; another zombie
             * may have destroyed it this frame. */
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

/*
 * Shove every active pea right by PEA_SPEED pixels; when one crosses
 * the right edge the slot flips inactive so spawn_pea can reuse it.
 * Peas are deactivated (not destroyed) because gs->projectiles is a
 * fixed-size pool.
 */
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

/*
 * Peashooter firing pass. A peashooter fires when
 *   (a) its per-plant fire_cooldown has reached 0, and
 *   (b) at least one zombie is in the same row.
 * After the shot the cooldown resets to PLANT_FIRE_COOLDOWN
 * (120 frames == 2 s at 60 fps).
 */
/* Fire peas from peashooters that have zombies in their row */
static void update_firing(game_state_t *gs)
{
    for (int r = 0; r < GRID_ROWS; r++) {
        for (int c = 0; c < GRID_COLS; c++) {
            plant_t *p = &gs->grid[r][c];
            if (p->type != PLANT_PEASHOOTER)
                continue;

            if (p->fire_cooldown > 0)
                p->fire_cooldown--;

            if (p->fire_cooldown == 0 && zombie_in_row(gs, r)) {
                spawn_pea(gs, r, c);
                p->fire_cooldown = PLANT_FIRE_COOLDOWN;
            }
        }
    }
}

/*
 * Pea-zombie collision pass.
 *
 * For each active pea, walk every active zombie in the same row and
 * check whether their bounding boxes overlap horizontally (same-row
 * is enough because a pea never changes rows). The overlap test is
 * the one from design-doc section "Collision Detection":
 *      pea_x + pea_w >= zombie_x  AND  pea_x <= zombie_x + zombie_w.
 * On hit the pea dies, the zombie loses PEA_DAMAGE HP, and if the
 * zombie's HP hits 0 it deactivates. Each pea can hit at most one
 * zombie per frame; the break enforces that.
 *
 * Zombie-plant "collision" lives in update_zombies, where a zombie
 * entering a populated cell flips to eating mode. That's the second
 * overlap rule in the same design-doc section.
 */
/* Check pea-zombie collisions */
static void check_collisions(game_state_t *gs)
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
                /* Hit! */
                z->hp -= PEA_DAMAGE;
                p->active = 0;

                if (z->hp <= 0)
                    z->active = 0;

                break; /* Each pea hits only one zombie */
            }
        }
    }
}

/*
 * Zombie spawner. Caps the total at TOTAL_ZOMBIES (the MVP's stand-
 * in for a full wave table; compare design-doc section "Wave System"
 * which specifies a time-offset table indexed by type). When
 * spawn_timer hits 0 we grab the first free slot in gs->zombies,
 * drop the zombie at the right edge (x = SCREEN_W - 1) in a random
 * row, and re-roll the timer between ZOMBIE_SPAWN_MIN..MAX frames.
 */
/* Spawn zombies on a timer */
static void update_spawning(game_state_t *gs)
{
    if (gs->zombies_spawned >= TOTAL_ZOMBIES)
        return;

    gs->spawn_timer--;
    if (gs->spawn_timer <= 0) {
        /* Find a free zombie slot */
        for (int i = 0; i < MAX_ZOMBIES; i++) {
            if (!gs->zombies[i].active) {
                gs->zombies[i].active = 1;
                gs->zombies[i].row = random_range(0, GRID_ROWS - 1);
                gs->zombies[i].x_pixel = SCREEN_W - 1;
                gs->zombies[i].hp = ZOMBIE_HP;
                gs->zombies[i].move_counter = 0;
                gs->zombies[i].eating = 0;
                gs->zombies[i].eat_timer = 0;
                gs->zombies_spawned++;
                break;
            }
        }
        gs->spawn_timer = random_range(ZOMBIE_SPAWN_MIN, ZOMBIE_SPAWN_MAX);
    }
}

/*
 * Passive sun income. sun_timer counts frames down from SUN_INTERVAL;
 * when it hits 0 the player gets SUN_INCREMENT more sun and the timer
 * resets. Sunflowers would add per-plant ticks here (design-doc
 * section "Sun Economy"); the MVP only has the passive drip.
 */
/* Update sun economy */
static void update_sun(game_state_t *gs)
{
    gs->sun_timer--;
    if (gs->sun_timer <= 0) {
        gs->sun += SUN_INCREMENT;
        gs->sun_timer = SUN_INTERVAL;
    }
}

/*
 * Win check: the full wave has spawned (zombies_spawned reached
 * TOTAL_ZOMBIES) AND no zombie slot is still active. The lose branch
 * is inline in update_zombies when a zombie crosses x = 0. Together
 * they are design-doc "check_win_lose".
 */
/* Check win condition */
static void check_win(game_state_t *gs)
{
    if (gs->zombies_spawned < TOTAL_ZOMBIES)
        return;

    for (int i = 0; i < MAX_ZOMBIES; i++) {
        if (gs->zombies[i].active)
            return;
    }

    gs->state = STATE_WIN;
}

/*
 * One frame of simulation. The pass order is deliberate:
 *   sun -> spawn -> firing -> projectiles -> zombies -> collisions -> win
 * Firing and movement run before collision detection so a pea fired
 * this frame can collide this frame instead of next. Zombies move
 * before check_collisions for the same reason: a zombie walking into
 * a stationary pea should register as a hit. When gs->state is not
 * PLAYING (win/lose/exit sentinel) we short-circuit the whole thing.
 */
void game_update(game_state_t *gs)
{
    if (gs->state != STATE_PLAYING)
        return;

    gs->frame_count++;

    update_sun(gs);
    update_spawning(gs);
    update_firing(gs);
    update_projectiles(gs);
    update_zombies(gs);
    check_collisions(gs);
    check_win(gs);
}
