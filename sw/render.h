#ifndef _RENDER_H
#define _RENDER_H

/*
 * Renderer public API.
 *
 * How it fits in:
 *   main.c calls render_init once to stash the /dev/pvz fd, then
 *   render_frame once per frame with the current game_state_t. All
 *   drawing goes through pvz_driver.ko ioctls. Implementation lives
 *   in render.c; see design-doc section 3.1 "State-Aware Rendering
 *   and Shape-Index Allocation" for the slot allocation and z-order
 *   rationale.
 */

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
