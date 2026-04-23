/*
 * Test program: push test patterns to the FPGA via ioctl.
 *
 * Draws a checkerboard background plus sample rectangle, circle,
 * and digit shapes.
 *
 * How it fits in:
 *   On-board smoke test for the full driver + hardware path (design-
 *   doc section "On-Board Integration"). test_shapes uses the same
 *   PVZ_WRITE_BG and PVZ_WRITE_SHAPE ioctls as render.c, but with
 *   hand-picked stimulus so a human can eyeball the VGA output and
 *   confirm every shape type renders. If the rectangle and circle
 *   show up at the right coordinates, the Avalon write path, the
 *   shape table, and the linebuffer render FSM are all alive.
 *
 * No cleanup path:
 *   Once the shapes are committed the process sits in a
 *   `for (;;) sleep(1)` so the display keeps showing the pattern.
 *   Ctrl+C kills the process, but the shape table keeps its last
 *   committed state - the hardware does not clear on process exit.
 *   Reload the pvz_driver module or rerun this tool with different
 *   inputs to reset.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "../pvz.h"

static int pvz_fd;

static void write_bg(int row, int col, int color)
{
    pvz_bg_arg_t bg = { .row = row, .col = col, .color = color };
    if (ioctl(pvz_fd, PVZ_WRITE_BG, &bg)) {
        perror("PVZ_WRITE_BG");
        exit(1);
    }
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
    if (ioctl(pvz_fd, PVZ_WRITE_SHAPE, &s)) {
        perror("PVZ_WRITE_SHAPE");
        exit(1);
    }
}

int main()
{
    pvz_fd = open("/dev/pvz", O_RDWR);
    if (pvz_fd < 0) {
        perror("open /dev/pvz");
        return 1;
    }

    /* Stimulus 1: checkerboard background. Writes all 4 x 8 cells
     * alternating between colour indices 1 and 2. Confirms the
     * PVZ_WRITE_BG ioctl path and the background palette lookup
     * work. */
    printf("Setting up checkerboard background...\n");
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 8; c++)
            write_bg(r, c, ((r + c) % 2 == 0) ? 1 : 2);

    /* Stimulus 2: one of each shape type across a few slots. The
     * slot indices (0..5) are arbitrary; they just need to be
     * unique. This isn't running a game, it's only exercising the
     * SHAPE_* renderers. */
    printf("Drawing test shapes...\n");

    /* Rectangle: green body at (100, 100), 60x40 */
    write_shape(0, SHAPE_RECT, 1, 100, 100, 60, 40, 7);

    /* Circle: bright green pea at (300, 200), 20x20 */
    write_shape(1, SHAPE_CIRCLE, 1, 290, 190, 20, 20, 9);

    /* Digit '5' at (400, 70), white */
    // For this test the raw digit value 5 (< 16) doubles as the
    // width and the 7-seg value. render.c uses the 32+digit idiom
    // instead.
    write_shape(2, SHAPE_DIGIT, 1, 400, 70, 5, 30, 10);

    /* Red rectangle (zombie) at (500, 150), 30x70 */
    write_shape(3, SHAPE_RECT, 1, 500, 150, 30, 70, 5);

    /* Yellow cursor rectangle at (80, 60), 80x90 */
    write_shape(4, SHAPE_RECT, 1, 0, 60, 80, 90, 4);

    /* Invisible shape (should not appear) */
    // visible = 0 has to suppress the shape entirely. If you can
    // see an orange rectangle, the visibility gate is broken.
    write_shape(5, SHAPE_RECT, 0, 200, 200, 50, 50, 12);

    printf("Test patterns written. Press Ctrl+C to exit.\n");

    /* Keep running so display stays active */
    for (;;)
        sleep(1);

    close(pvz_fd);
    return 0;
}
