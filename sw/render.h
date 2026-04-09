#ifndef _RENDER_H
#define _RENDER_H

#include "game.h"

/*
 * Initialize the renderer (set up the FPGA fd).
 * Returns 0 on success, -1 on failure.
 */
int render_init(int fpga_fd);

/*
 * Render the full game state to the FPGA.
 * Converts game state into background cells + shape table entries
 * and writes them via ioctls.
 */
void render_frame(const game_state_t *gs);

#endif /* _RENDER_H */
