/*
 * Game state -> FPGA shape-table renderer.
 *
 * Turns game state into shape-table entries and background cells,
 * then pushes them to the FPGA via ioctls.
 *
 * How it fits in:
 *   Called once per frame from main.c with the current game_state_t.
 *   The only side channel is the FPGA fd stashed at render_init()
 *   time. ioctls go through pvz_driver.ko (see pvz.h for
 *   PVZ_WRITE_BG and PVZ_WRITE_SHAPE) and land as Avalon register
 *   writes to the shape table in the FPGA. See design-doc section
 *   3.1 "State-Aware Rendering and Shape-Index Allocation".
 *
 * Shape table and ioctl wrappers:
 *   The renderer sees the FPGA as 48 shape slots plus a 4 x 8
 *   background grid. write_bg() issues one PVZ_WRITE_BG ioctl, which
 *   the driver turns into one BG_CELL register write (background
 *   Avalon path is a single register, so one ioctl equals one Avalon
 *   write). write_shape() issues one PVZ_WRITE_SHAPE ioctl, which
 *   the driver expands into the 4-write Avalon sequence against the
 *   shape table: SHAPE_ADDR, SHAPE_DATA0, SHAPE_DATA1, SHAPE_COMMIT.
 *   The commit write latches the entry atomically so the scanline
 *   fetcher never sees a half-updated slot and the frame doesn't
 *   tear.
 *
 * Painter's-algorithm z-order:
 *   The hardware shape renderer walks the table in index order, and
 *   later writes cover earlier ones - straight painter's algorithm.
 *   Higher index means drawn on top. So we allocate indices by layer
 *   (plants low, cursor on top).
 *
 * Shape table index allocation (original sketch):
 *   0-15:  Plants (2 shapes each, 8 slots = 16 shapes max)
 *          For 4 rows x 8 cols = 32 cells, but max ~8 plants expected
 *          Actually allocate by grid position:
 *          index = (row * GRID_COLS + col) for body (0-31)
 *   0-31:  Plant shapes (body at even, stem at odd: 2 per cell, 32 cells)
 *   32-41: Zombies (2 shapes each: body + head, 5 zombies)
 *   42-47: Projectiles (1 shape each, up to 6 visible)
 *          Any extra projectiles beyond 6 are not drawn
 *
 * Revised allocation to fit 48 entries (higher index = drawn on top):
 *   0-31:  Plants: 32 grid cells, 1 shape each (simplified body)
 *   32-36: Zombies: 5 zombies, 1 shape each (simplified body)
 *   37-42: Projectiles: 6 peas, 1 shape each
 *   43-46: HUD digits (up to 4 digits for sun counter)
 *   47:    Cursor (1 shape)
 */

#include <stdio.h>
#include <sys/ioctl.h>
#include "render.h"
#include "pvz.h"

static int fd;

/* Shape index allocation (higher index = drawn on top via painter's algorithm) */
#define IDX_PLANT_START   0
#define IDX_PLANT_COUNT   32  /* one per grid cell */
#define IDX_ZOMBIE_START  32
#define IDX_ZOMBIE_COUNT  5
#define IDX_PEA_START     37
#define IDX_PEA_COUNT     6
#define IDX_HUD_START     43
#define IDX_HUD_COUNT     4
/* Cursor sits at slot 47, the highest in the table, so the painter's
 * pass draws it last and it can never hide behind a plant. This is
 * the fix from commit dc297d4; an earlier version put the cursor at
 * a low index and plants in the same cell covered it. Design-doc
 * section 3.1 calls this out explicitly. */
#define IDX_CURSOR        47

/* Color indices (must match color_palette.sv) */
#define COL_BLACK       0
#define COL_DARK_GREEN  1
#define COL_LIGHT_GREEN 2
#define COL_BROWN       3
#define COL_YELLOW      4
#define COL_RED         5
#define COL_DARK_RED    6
#define COL_GREEN       7
#define COL_DARK_GREEN2 8
#define COL_BRIGHT_GREEN 9
#define COL_WHITE       10
#define COL_GRAY        11
#define COL_ORANGE      12

/*
 * One PVZ_WRITE_BG ioctl writes one background cell. The driver
 * packs (row, col, color) into a single BG_CELL register write on
 * the Avalon bus. One-to-one mapping.
 */
static void write_bg(int row, int col, int color)
{
    pvz_bg_arg_t bg = { .row = row, .col = col, .color = color };
    ioctl(fd, PVZ_WRITE_BG, &bg);
}

/*
 * One PVZ_WRITE_SHAPE ioctl updates one shape-table slot. The driver
 * expands it into the 4-write Avalon sequence from design-doc
 * section 3.1: SHAPE_ADDR (select slot), SHAPE_DATA0 (type/visible/
 * x/y), SHAPE_DATA1 (w/h/color), SHAPE_COMMIT (latch). The commit
 * write is what makes the new entry atomic against the scanline
 * fetcher.
 */
static void write_shape(int index, int type, int visible,
                        int x, int y, int w, int h, int color)
{
    pvz_shape_arg_t s = {
        .index   = index,
        .type    = type,
        .visible = visible,
        .x       = x,
        .y       = y,
        .w       = w,
        .h       = h,
        .color   = color
    };
    ioctl(fd, PVZ_WRITE_SHAPE, &s);
}

/*
 * Clear a slot by committing an entry with visible = 0. The hardware
 * still walks it, but the visibility bit short-circuits the pixel
 * write. Leaving visible=0 slots in place is cheaper than compacting
 * the table.
 */
static void hide_shape(int index)
{
    write_shape(index, SHAPE_RECT, 0, 0, 0, 0, 0, 0);
}

int render_init(int fpga_fd)
{
    fd = fpga_fd;
    return 0;
}

/*
 * Rewrite every background cell every frame. A one-shot init at
 * startup would be enough, but 32 ioctls per frame is cheap and
 * makes the driver path behave the same way during play and during
 * win/lose.
 *
 * (r + c) parity gives the checkerboard from design-doc "Display
 * Layout".
 */
static void render_background(const game_state_t *gs)
{
    for (int r = 0; r < GRID_ROWS; r++)
        for (int c = 0; c < GRID_COLS; c++)
            write_bg(r, c, ((r + c) % 2 == 0) ? COL_DARK_GREEN : COL_LIGHT_GREEN);
}

/*
 * Cursor is the top-of-stack shape (slot 47). Grid-to-pixel mapping
 * is from design-doc section "Display Layout":
 *   x = c * CELL_WIDTH, y = GAME_AREA_Y + r * CELL_HEIGHT.
 */
static void render_cursor(const game_state_t *gs)
{
    int x = gs->cursor_col * CELL_WIDTH;
    int y = GAME_AREA_Y + gs->cursor_row * CELL_HEIGHT;
    /* Draw cursor as a yellow border rectangle */
    write_shape(IDX_CURSOR, SHAPE_RECT, 1, x, y, CELL_WIDTH, CELL_HEIGHT, COL_YELLOW);
}

/*
 * Emit one shape per grid cell: index 0..31 == row * 8 + col. Empty
 * cells hide their slot so no stale sprite lingers after a zombie
 * eats a plant.
 */
static void render_plants(const game_state_t *gs)
{
    /* Peashooter sprite: 32x32 source rendered at 2x -> 64x64 on screen.
     * Cell is CELL_WIDTH x CELL_HEIGHT (80x90). Center the 64x64 in the cell:
     *   x offset = (80 - 64) / 2 = 8
     *   y offset = (90 - 64) / 2 = 13 */
    for (int r = 0; r < GRID_ROWS; r++) {
        for (int c = 0; c < GRID_COLS; c++) {
            int idx = IDX_PLANT_START + r * GRID_COLS + c;
            const plant_t *p = &gs->grid[r][c];

            if (p->type == PLANT_PEASHOOTER) {
                int cx = c * CELL_WIDTH + 8;
                int cy = GAME_AREA_Y + r * CELL_HEIGHT + 13;
                write_shape(idx, SHAPE_SPRITE, 1, cx, cy, 64, 64, 0);
            } else {
                hide_shape(idx);
            }
        }
    }
}

/*
 * Emit one shape per zombie pool slot. Inactive slots are hidden.
 * The shape index is pool-position based (IDX_ZOMBIE_START + i), not
 * z-order based, so slot reuse in update_spawning doesn't force the
 * renderer to reshuffle anything.
 */
static void render_zombies(const game_state_t *gs)
{
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        int idx = IDX_ZOMBIE_START + i;
        const zombie_t *z = &gs->zombies[i];

        if (z->active) {
            int y = GAME_AREA_Y + z->row * CELL_HEIGHT + 10;
            /* Zombie body: red rectangle */
            write_shape(idx, SHAPE_RECT, 1, z->x_pixel, y,
                       ZOMBIE_WIDTH, ZOMBIE_HEIGHT, COL_RED);
        } else {
            hide_shape(idx);
        }
    }
}

/*
 * Only IDX_PEA_COUNT (6) pea shape slots exist, but MAX_PROJECTILES
 * (16) is the pool size. If more than 6 peas are live at once the
 * extras still get simulated (collision and movement) but not
 * drawn. The "drawn" counter enforces the cap, and any surplus
 * slots past drawn get hidden below.
 */
static void render_projectiles(const game_state_t *gs)
{
    int drawn = 0;
    for (int i = 0; i < MAX_PROJECTILES && drawn < IDX_PEA_COUNT; i++) {
        const projectile_t *p = &gs->projectiles[i];
        if (p->active) {
            int idx = IDX_PEA_START + drawn;
            int y = GAME_AREA_Y + p->row * CELL_HEIGHT + (CELL_HEIGHT / 2) - (PEA_SIZE / 2);
            /* Pea: bright green circle */
            write_shape(idx, SHAPE_CIRCLE, 1, p->x_pixel, y,
                       PEA_SIZE, PEA_SIZE, COL_BRIGHT_GREEN);
            drawn++;
        }
    }
    /* Hide unused pea slots */
    for (int i = drawn; i < IDX_PEA_COUNT; i++)
        hide_shape(IDX_PEA_START + i);
}

/*
 * Render the sun counter as up to IDX_HUD_COUNT decimal digits.
 * We split digits off the low end of the integer and reverse them so
 * the most-significant digit comes first for left-to-right screen
 * layout. SHAPE_DIGIT is decoded in hardware by the 7-segment
 * renderer.
 */
static void render_hud(const game_state_t *gs)
{
    /* Display sun counter as 7-segment digits in the HUD area (y=10) */
    int sun = gs->sun;
    int digits[IDX_HUD_COUNT];
    int num_digits = 0;

    if (sun == 0) {
        digits[0] = 0;
        num_digits = 1;
    } else {
        int temp = sun;
        while (temp > 0 && num_digits < IDX_HUD_COUNT) {
            digits[num_digits++] = temp % 10;
            temp /= 10;
        }
        /* Reverse */
        for (int i = 0; i < num_digits / 2; i++) {
            int t = digits[i];
            digits[i] = digits[num_digits - 1 - i];
            digits[num_digits - 1 - i] = t;
        }
    }

    /* Draw digits left-to-right at top of screen.
     * The hardware draw loop uses s_w as the pixel width bound, but
     * decode_7seg reads the digit value from s_w[3:0].  Pass w = 32 + digit
     * so the draw loop covers the full 20-pixel glyph (32 > 20) while
     * s_w[3:0] still equals the digit value (32 & 0xF == 0). */
    for (int i = 0; i < IDX_HUD_COUNT; i++) {
        int idx = IDX_HUD_START + i;
        if (i < num_digits) {
            int x = 10 + i * 25;
            // 7-seg encoding trick: the w field carries both the
            // bounding width (upper bits, >= 20 so the renderer
            // covers the whole glyph) and the digit value in the low
            // 4 bits. 32 + digit gives both, since (32 + d) & 0xF
            // == d for d in 0..9.
            write_shape(idx, SHAPE_DIGIT, 1, x, 15,
                        32 + digits[i], 30, COL_WHITE);
        } else {
            hide_shape(idx);
        }
    }
}

/*
 * Win/lose banner. We reuse slot IDX_CURSOR (47) for two reasons:
 * render_frame() hides every other slot before getting here so 47
 * is the only active one, and slot 47 sits on top of the painter's
 * order so the banner can't be occluded.
 */
static void render_game_over(const game_state_t *gs)
{
    /* Simple game-over indicator: large colored rectangle in center */
    if (gs->state == STATE_WIN) {
        /* Green "WIN" indicator */
        // 200 x 80 rectangle centred roughly at (320, 240): x = 220
        // puts the left edge 100 px left of centre, y = 200 puts the
        // top 40 px above centre.
        write_shape(IDX_CURSOR, SHAPE_RECT, 1, 220, 200, 200, 80, COL_BRIGHT_GREEN);
    } else if (gs->state == STATE_LOSE) {
        /* Red "LOSE" indicator */
        write_shape(IDX_CURSOR, SHAPE_RECT, 1, 220, 200, 200, 80, COL_RED);
    }
}

/*
 * Entry point called once per frame from main.c.
 *
 * Two modes, picked off gs->state:
 *   STATE_PLAYING  - full stack: background, plants, zombies, peas,
 *                    cursor, HUD. Every shape slot gets rewritten
 *                    every frame (even if unchanged) because the
 *                    hardware only renders from what's committed.
 *   STATE_WIN/LOSE - hide every entity slot so only the banner is
 *                    visible, then draw the banner via
 *                    render_game_over. The background stays up since
 *                    render_background always runs.
 */
void render_frame(const game_state_t *gs)
{
    render_background(gs);

    if (gs->state == STATE_PLAYING) {
        render_plants(gs);
        render_zombies(gs);
        render_projectiles(gs);
        render_cursor(gs);
        render_hud(gs);
    } else {
        /* Hide all entity slots so they don't cover the panel */
        for (int i = 0; i < IDX_PLANT_COUNT; i++)
            hide_shape(IDX_PLANT_START + i);
        for (int i = 0; i < IDX_ZOMBIE_COUNT; i++)
            hide_shape(IDX_ZOMBIE_START + i);
        for (int i = 0; i < IDX_PEA_COUNT; i++)
            hide_shape(IDX_PEA_START + i);
        render_game_over(gs);
    }
}
