## 1. VGA Timing and Linebuffer Foundation

- [x] 1.1 Create `hw/vga_counters.sv` — extract VGA 640x480@60Hz timing generator from lab3 `vga_ball.sv` (hcount, vcount, hsync, vsync, blanking signals)
- [x] 1.2 Create `hw/linebuffer.sv` — dual 640x8-bit line RAMs with write port, read port, and hsync swap logic
- [x] 1.3 Create `hw/color_palette.sv` — 256-entry hardcoded LUT mapping 8-bit index to 24-bit RGB (populate 13 MVP colors)

## 2. Background Grid and Shape Table

- [x] 2.1 Create `hw/bg_grid.sv` — 4x8 cell color array with shadow/active registers, outputs cell color for any (x,y) coordinate in game area
- [x] 2.2 Create `hw/shape_table.sv` — 48-entry register file (type, visible, x, y, w, h, color per entry), shadow/active double-buffering latched at vsync

## 3. Shape Renderer

- [x] 3.1 Create `hw/shape_renderer.sv` — scanline-based shape iteration: for each visible shape overlapping current line, write color to linebuffer (filled rectangle logic)
- [x] 3.2 Add filled circle rendering to shape_renderer (bounding box + distance check: dx^2 + dy^2 <= r^2)
- [x] 3.3 Add 7-segment digit rendering to shape_renderer (digit value in width[3:0], segment rectangles at known offsets)

## 4. Avalon-MM Top-Level and Platform Designer Integration

- [x] 4.1 Create `hw/pvz_top.sv` — Avalon-MM agent decoding 5 registers (BG_CELL, SHAPE_ADDR, SHAPE_DATA0, SHAPE_DATA1, SHAPE_COMMIT), wiring vga_counters + linebuffer + bg_grid + shape_table + shape_renderer + color_palette
- [x] 4.2 Create `hw/pvz_top_hw.tcl` — Platform Designer component descriptor with Avalon-MM slave interface and `embeddedsw.dts.compatible "csee4840,pvz_gpu-1.0"`
- [x] 4.3 Create `hw/soc_system_top.sv` — board top-level wiring soc_system instance to VGA pins, clocks, and HPS (adapt from lab3)
- [x] 4.4 Create `hw/Makefile` — targets for qsys, quartus, rbf, dtb (adapt from lab3 hw/Makefile)

## 5. Hardware Testbenches

- [x] 5.1 Create `hw/tb/tb_vga_counters.sv` — verify hsync/vsync timing, hcount/vcount ranges, blanking
- [x] 5.2 Create `hw/tb/tb_linebuffer.sv` — verify write/swap/read cycle, boundary pixels
- [x] 5.3 Create `hw/tb/tb_bg_grid.sv` — verify cell color output for known coordinates
- [x] 5.4 Create `hw/tb/tb_shape_renderer.sv` — verify rectangle, circle, digit rendering, z-order, invisible shapes
- [x] 5.5 Create `hw/tb/tb_pvz_top.sv` — end-to-end Avalon writes through to VGA pixel output

## 6. Kernel Driver

- [x] 6.1 Create `sw/pvz.h` — shared ioctl definitions, shape/bg structs (PVZ_WRITE_BG, PVZ_WRITE_SHAPE, PVZ_COMMIT_SHAPES)
- [x] 6.2 Create `sw/pvz_driver.h` — driver internal header
- [x] 6.3 Create `sw/pvz_driver.c` — misc device kernel module: probe/remove, ioctl dispatch, iowrite32 to Avalon registers, compatible = "csee4840,pvz_gpu-1.0"
- [x] 6.4 Create `sw/Makefile` — kbuild two-pass for pvz_driver.ko + gcc for userspace programs

## 7. Test Programs

- [x] 7.1 Create `sw/test/test_shapes.c` — write test patterns to FPGA via ioctl (checkerboard bg, sample rect/circle/digit shapes)
- [x] 7.2 Create `sw/test/test_input.c` — print keyboard events from /dev/input/eventX to console

## 8. Game Input

- [x] 8.1 Create `sw/input.c` + `sw/input.h` — non-blocking keyboard reader using /dev/input/eventX, arrow keys, space, D, ESC mapping

## 9. Game Logic

- [x] 9.1 Create `sw/game.h` — game state structs (plant, zombie, projectile, grid, game_state), constants (grid size, HP, speeds, costs)
- [x] 9.2 Create `sw/game.c` — game_init, game_update (zombie movement, peashooter firing, pea movement, collision detection, sun economy, spawn logic, win/lose checks)

## 10. Game Rendering

- [x] 10.1 Create `sw/render.h` + `sw/render.c` — game state to shape table conversion: allocate shape indices, convert plants/zombies/peas/cursor/HUD to shape entries, write via ioctl

## 11. Main Game Loop

- [x] 11.1 Create `sw/main.c` — init driver fd, init game state, 60Hz loop (input -> update -> render -> write to FPGA), cleanup on exit

## 12. Software Unit Tests

- [x] 12.1 Create `sw/test/test_game.c` — host-compilable tests for collision, sun economy, zombie spawn, win/lose conditions
