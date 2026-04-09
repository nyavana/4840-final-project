## Why

The CSEE4840 final project requires a working Plants vs Zombies game running on the DE1-SoC FPGA board with VGA output. No project source code exists yet — only documentation and reference designs. We need to build the full hardware display engine, kernel driver, and game software from scratch to deliver a playable MVP demo with primitive-shape graphics, keyboard input, and core PvZ mechanics (plant placement, zombie waves, projectile combat, sun economy).

## What Changes

- Add FPGA display engine (SystemVerilog): VGA 640x480@60Hz timing, dual linebuffer, background grid renderer, 48-entry shape table with shadow/active double-buffering, shape renderer (filled rectangles, filled circles, 7-segment digits), 256-entry color palette LUT
- Add Avalon-MM register interface on the lightweight HPS-to-FPGA bridge for CPU-driven shape/background writes
- Add Platform Designer component (`pvz_top_hw.tcl`) and board top-level wiring (`soc_system_top.sv`)
- Add Linux kernel driver (`pvz_driver.c`) exposing the Avalon registers as `/dev/pvz` with ioctls for background cell writes, shape entry writes, and shape commit
- Add game software (C, userspace): game loop at 60Hz, 4x8 grid state, peashooter/zombie/projectile entities, collision detection, sun economy, keyboard input via `/dev/input/eventX`, game-state-to-shape-table rendering
- Add hardware build infrastructure (Makefile for Quartus/Platform Designer/dtb flow)
- Add test programs and testbenches for verification at each layer

## Capabilities

### New Capabilities
- `fpga-display-engine`: VGA timing, linebuffer, background grid, shape table, shape renderer, color palette — all SystemVerilog modules composing the FPGA display pipeline
- `avalon-register-interface`: Avalon-MM agent in `pvz_top.sv` decoding CPU writes into background grid and shape table updates, with vsync-latched shadow registers
- `kernel-driver`: Linux kernel module exposing the FPGA registers as a misc character device with ioctl commands for background, shape, and commit operations
- `game-logic`: Core PvZ game state management — grid, entities (plants, zombies, projectiles), update loop, collision detection, sun economy, win/lose conditions
- `game-rendering`: Conversion of game state into shape table entries written to the FPGA each frame
- `keyboard-input`: Reading keyboard events from `/dev/input/eventX` for cursor movement, plant placement, and plant removal
- `hw-build-system`: Quartus/Platform Designer/dtb Makefile targets and Platform Designer component descriptor

### Modified Capabilities
<!-- No existing capabilities to modify — this is a greenfield project. -->

## Impact

- **Hardware**: New SystemVerilog modules under `hw/`, new Platform Designer component, modified `soc_system.qsys` and `soc_system_top.sv`. Requires Quartus synthesis and FPGA programming.
- **Kernel**: New loadable kernel module under `sw/`. Requires matching device tree entry generated from Platform Designer.
- **Software**: New C source files under `sw/` for game logic, rendering, input, and main loop. Links against libusb (future USB gamepad) and pthreads.
- **Build**: New Makefiles for both `hw/` (Quartus flow) and `sw/` (kbuild + userspace). Must run on Columbia `micro*.ee.columbia.edu` workstations.
- **Three-way tie**: `pvz_top.sv` ports, `pvz_top_hw.tcl` interface definitions, and `pvz_driver.c` compatible string must all stay in sync.
