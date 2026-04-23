/*
 * Game state to FPGA shape table renderer (v3)
 *
 * Converts the game state into shape table entries and background cells,
 * then writes them to the FPGA via ioctls.
 *
 * v3 shape-index allocation (higher index = drawn on top via painter's):
 *   0-31   Plants                 (one slot per grid cell, r*8 + c)
 *   32-36  Zombies                (5 concurrent slots)
 *   37-42  Peas                   (6 slots)
 *   43     Plant-card highlight   (drawn behind cards so each card forms
 *                                  a 2-pixel yellow border on top of it)
 *   44-46  Plant cards            (Peashooter / Sunflower / Wall-nut)
 *   47-50  Sun counter digits     (up to 4 digits, max 9999 sun)
 *   51-62  Reserved (hidden every frame)
 *   63     Cursor (always on top)
 */

#include <stdio.h>
#include <sys/ioctl.h>
#include "render.h"
#include "pvz.h"

static int fd;

/* Shape index allocation */
#define IDX_PLANT_START     0
#define IDX_PLANT_COUNT     32
#define IDX_ZOMBIE_START    32
#define IDX_ZOMBIE_COUNT    5
#define IDX_PEA_START       37
#define IDX_PEA_COUNT       6
#define IDX_CARD_HIGHLIGHT  43
#define IDX_CARD_START      44
#define IDX_CARD_COUNT      3
#define IDX_HUD_DIGIT_START 47
#define IDX_HUD_DIGIT_COUNT 4
#define IDX_RESERVED_START  51
#define IDX_RESERVED_COUNT  12
#define IDX_CURSOR          63

/* Plant-card layout */
#define CARD_Y              5
#define CARD_W              55
#define CARD_H              45
#define CARD_X0             10
#define CARD_PITCH          65
#define HIGHLIGHT_Y         3
#define HIGHLIGHT_W         59
#define HIGHLIGHT_H         49
#define HIGHLIGHT_OFFSET_X  (-2)   /* two pixels left of the selected card */

/* Color indices (must match color_palette.sv) */
#define COL_BLACK        0
#define COL_DARK_GREEN   1
#define COL_LIGHT_GREEN  2
#define COL_BROWN        3
#define COL_YELLOW       4
#define COL_RED          5
#define COL_DARK_RED     6
#define COL_GREEN        7
#define COL_DARK_GREEN2  8
#define COL_BRIGHT_GREEN 9
#define COL_WHITE        10
#define COL_GRAY         11
#define COL_ORANGE       12

static void write_bg(int row, int col, int color)
{
    pvz_bg_arg_t bg = { .row = row, .col = col, .color = color };
    ioctl(fd, PVZ_WRITE_BG, &bg);
}

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

static void hide_shape(int index)
{
    write_shape(index, SHAPE_RECT, 0, 0, 0, 0, 0, 0);
}

int render_init(int fpga_fd)
{
    fd = fpga_fd;
    return 0;
}

static void render_background(const game_state_t *gs)
{
    (void)gs;
    for (int r = 0; r < GRID_ROWS; r++)
        for (int c = 0; c < GRID_COLS; c++)
            write_bg(r, c, ((r + c) % 2 == 0) ? COL_DARK_GREEN : COL_LIGHT_GREEN);
}

static void render_cursor(const game_state_t *gs)
{
    int x = gs->cursor_col * CELL_WIDTH;
    int y = GAME_AREA_Y + gs->cursor_row * CELL_HEIGHT;
    write_shape(IDX_CURSOR, SHAPE_RECT, 1, x, y, CELL_WIDTH, CELL_HEIGHT, COL_YELLOW);
}

static void render_plants(const game_state_t *gs)
{
    for (int r = 0; r < GRID_ROWS; r++) {
        for (int c = 0; c < GRID_COLS; c++) {
            int idx = IDX_PLANT_START + r * GRID_COLS + c;
            const plant_t *p = &gs->grid[r][c];

            switch (p->type) {
            case PLANT_PEASHOOTER: {
                /* Sprite is 32x32, rendered at 2x scale -> 64x64 on screen.
                 * Cell is 80x90; center => offset (8, 13). */
                int cx = c * CELL_WIDTH + 8;
                int cy = GAME_AREA_Y + r * CELL_HEIGHT + 13;
                write_shape(idx, SHAPE_SPRITE, 1, cx, cy, 64, 64, 0);
                break;
            }
            case PLANT_SUNFLOWER: {
                int cx = c * CELL_WIDTH + 10;
                int cy = GAME_AREA_Y + r * CELL_HEIGHT + 15;
                write_shape(idx, SHAPE_CIRCLE, 1, cx, cy, 60, 60, COL_YELLOW);
                break;
            }
            case PLANT_WALLNUT: {
                int cx = c * CELL_WIDTH + 5;
                int cy = GAME_AREA_Y + r * CELL_HEIGHT + 10;
                write_shape(idx, SHAPE_CIRCLE, 1, cx, cy, 70, 70, COL_BROWN);
                break;
            }
            default:
                hide_shape(idx);
                break;
            }
        }
    }
}

static int zombie_color(int type)
{
    switch (type) {
    case ZOMBIE_CONEHEAD:   return COL_ORANGE;
    case ZOMBIE_BUCKETHEAD: return COL_GRAY;
    case ZOMBIE_BASIC:
    default:                return COL_RED;
    }
}

static void render_zombies(const game_state_t *gs)
{
    for (int i = 0; i < MAX_ZOMBIES && i < IDX_ZOMBIE_COUNT; i++) {
        int idx = IDX_ZOMBIE_START + i;
        const zombie_t *z = &gs->zombies[i];

        if (z->active) {
            int y = GAME_AREA_Y + z->row * CELL_HEIGHT + 10;
            write_shape(idx, SHAPE_RECT, 1, z->x_pixel, y,
                       ZOMBIE_WIDTH, ZOMBIE_HEIGHT, zombie_color(z->type));
        } else {
            hide_shape(idx);
        }
    }
}

static void render_projectiles(const game_state_t *gs)
{
    int drawn = 0;
    for (int i = 0; i < MAX_PROJECTILES && drawn < IDX_PEA_COUNT; i++) {
        const projectile_t *p = &gs->projectiles[i];
        if (p->active) {
            int idx = IDX_PEA_START + drawn;
            int y = GAME_AREA_Y + p->row * CELL_HEIGHT + (CELL_HEIGHT / 2) - (PEA_SIZE / 2);
            write_shape(idx, SHAPE_CIRCLE, 1, p->x_pixel, y,
                       PEA_SIZE, PEA_SIZE, COL_BRIGHT_GREEN);
            drawn++;
        }
    }
    for (int i = drawn; i < IDX_PEA_COUNT; i++)
        hide_shape(IDX_PEA_START + i);
}

static int card_color(int slot)
{
    switch (slot) {
    case 0:  return COL_GREEN;   /* Peashooter */
    case 1:  return COL_YELLOW;  /* Sunflower */
    case 2:  return COL_BROWN;   /* Wall-nut */
    default: return COL_BLACK;
    }
}

static void render_plant_cards(const game_state_t *gs)
{
    /* Highlight drawn first (lower index) so the card above it leaves
     * a 2-pixel yellow border around the selected card only. */
    int sel = gs->selected_plant;
    if (sel < 0 || sel >= IDX_CARD_COUNT)
        sel = 0;
    int hl_x = CARD_X0 + sel * CARD_PITCH + HIGHLIGHT_OFFSET_X;
    write_shape(IDX_CARD_HIGHLIGHT, SHAPE_RECT, 1,
                hl_x, HIGHLIGHT_Y, HIGHLIGHT_W, HIGHLIGHT_H, COL_YELLOW);

    for (int i = 0; i < IDX_CARD_COUNT; i++) {
        int idx = IDX_CARD_START + i;
        int x = CARD_X0 + i * CARD_PITCH;
        write_shape(idx, SHAPE_RECT, 1, x, CARD_Y, CARD_W, CARD_H,
                    card_color(i));
    }
}

static void hide_plant_cards(void)
{
    hide_shape(IDX_CARD_HIGHLIGHT);
    for (int i = 0; i < IDX_CARD_COUNT; i++)
        hide_shape(IDX_CARD_START + i);
}

static void render_hud(const game_state_t *gs)
{
    /* Display sun counter as 7-segment digits across slots 47-50 */
    int sun = gs->sun;
    if (sun < 0) sun = 0;
    if (sun > 9999) sun = 9999;

    int digits[IDX_HUD_DIGIT_COUNT];
    int num_digits = 0;

    if (sun == 0) {
        digits[0] = 0;
        num_digits = 1;
    } else {
        int temp = sun;
        while (temp > 0 && num_digits < IDX_HUD_DIGIT_COUNT) {
            digits[num_digits++] = temp % 10;
            temp /= 10;
        }
        for (int i = 0; i < num_digits / 2; i++) {
            int t = digits[i];
            digits[i] = digits[num_digits - 1 - i];
            digits[num_digits - 1 - i] = t;
        }
    }

    /* Position digits to the right of the plant cards: start at x=220,
     * step 25 px per digit, y=15.  decode_7seg reads the digit value
     * from s_w[3:0]; pass w = 32 + digit so the draw loop covers the
     * full 20-pixel glyph (32 > 20) while w[3:0] == digit. */
    for (int i = 0; i < IDX_HUD_DIGIT_COUNT; i++) {
        int idx = IDX_HUD_DIGIT_START + i;
        if (i < num_digits) {
            int x = 220 + i * 25;
            write_shape(idx, SHAPE_DIGIT, 1, x, 15,
                        32 + digits[i], 30, COL_WHITE);
        } else {
            hide_shape(idx);
        }
    }
}

static void hide_reserved(void)
{
    for (int i = 0; i < IDX_RESERVED_COUNT; i++)
        hide_shape(IDX_RESERVED_START + i);
}

static void render_game_over(const game_state_t *gs)
{
    if (gs->state == STATE_WIN) {
        write_shape(IDX_CURSOR, SHAPE_RECT, 1, 220, 200, 200, 80, COL_BRIGHT_GREEN);
    } else if (gs->state == STATE_LOSE) {
        write_shape(IDX_CURSOR, SHAPE_RECT, 1, 220, 200, 200, 80, COL_RED);
    }
}

void render_frame(const game_state_t *gs)
{
    render_background(gs);
    hide_reserved();

    if (gs->state == STATE_PLAYING) {
        render_plants(gs);
        render_zombies(gs);
        render_projectiles(gs);
        render_cursor(gs);
        render_plant_cards(gs);
        render_hud(gs);
    } else {
        for (int i = 0; i < IDX_PLANT_COUNT; i++)
            hide_shape(IDX_PLANT_START + i);
        for (int i = 0; i < IDX_ZOMBIE_COUNT; i++)
            hide_shape(IDX_ZOMBIE_START + i);
        for (int i = 0; i < IDX_PEA_COUNT; i++)
            hide_shape(IDX_PEA_START + i);
        hide_plant_cards();
        for (int i = 0; i < IDX_HUD_DIGIT_COUNT; i++)
            hide_shape(IDX_HUD_DIGIT_START + i);
        render_game_over(gs);
    }
}
