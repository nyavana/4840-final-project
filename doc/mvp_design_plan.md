# PvZ FPGA MVP — Design & Implementation Plan

## Context

This is the Minimum Viable Product milestone for a Plants vs Zombies game on the Terasic DE1-SoC (Cyclone V FPGA). The goal is a playable demo with primitive-shape graphics (no sprites/audio), keyboard input, and core game mechanics. This foundation will later be extended with sprites, audio, USB gamepad, and multiple plant/zombie types.

**MVP scope:** Single level, 5 zombies, 1 plant type (Peashooter), keyboard control, 640x480 VGA with rectangles/circles/7-segment digits rendered in hardware.

---

## 1. System Architecture

### HW/SW Split

| Layer | Responsibility |
|-------|---------------|
| **FPGA (SystemVerilog)** | VGA timing (640x480@60Hz), background color grid (4x8), shape table renderer (48 entries: rect, circle, 7-seg digit), linebuffer double-buffering, Avalon-MM register interface |
| **HPS (Linux C)** | Game loop (~60Hz), grid state, zombie/plant/projectile logic, collision detection, sun economy, USB keyboard via `/dev/input/eventX`, writes shape table + bg grid via kernel driver ioctls |
| **Kernel driver** | Misc device exposing Avalon registers via ioctls (lab3 `vga_ball.c` pattern) |

### Data Flow Per Frame

1. HPS game loop updates game state (move zombies, fire peas, check collisions)
2. HPS converts game state -> shape table entries (position, size, color, type)
3. HPS writes shape table to FPGA via ioctl (driver does `iowrite32` to Avalon registers)
4. FPGA latches shape table at vsync (shadow -> active, same as lab3)
5. FPGA renders each scanline: background grid first, then shapes in order into linebuffer
6. VGA output reads from display linebuffer, through color palette to RGB

### Register Interface (lightweight HPS-to-FPGA bridge, base 0xFF200000)

| Offset | Name | Description |
|--------|------|-------------|
| 0x00 | BG_CELL | [12:8] color index, [4:3] row, [2:0] col — write one background cell color |
| 0x04 | SHAPE_ADDR | [5:0] shape index (0-47) — select shape entry to write |
| 0x08 | SHAPE_DATA0 | type[1:0], visible[2], x[12:3], y[21:13] — shape descriptor word 0 |
| 0x0C | SHAPE_DATA1 | width[8:0], height[17:9], color[25:18] — shape descriptor word 1 |
| 0x10 | SHAPE_COMMIT | write 1 to commit shape at SHAPE_ADDR into shadow table |

Shape types: 0 = filled rectangle, 1 = filled circle, 2 = 7-segment digit (value encoded in width field bits [3:0])

---

## 2. FPGA Display Engine

### Module Breakdown

| Module | File | Purpose |
|--------|------|---------|
| `vga_counters` | `hw/vga_counters.sv` | 640x480@60Hz timing generator (reuse from lab3) |
| `linebuffer` | `hw/linebuffer.sv` | Two 640x8-bit line RAMs, swap on hsync |
| `bg_grid` | `hw/bg_grid.sv` | 4x8 color array, outputs cell color for any pixel coordinate |
| `shape_renderer` | `hw/shape_renderer.sv` | Iterates 48-entry shape table, writes overlapping pixels to linebuffer |
| `shape_table` | `hw/shape_table.sv` | 48-entry register file, shadow/active double-buffered at vsync |
| `color_palette` | `hw/color_palette.sv` | 256-entry LUT: 8-bit index -> 24-bit RGB (hardcoded, ~20 colors used) |
| `pvz_top` | `hw/pvz_top.sv` | Top-level Avalon-MM agent wiring everything together |
| `soc_system_top` | `hw/soc_system_top.sv` | Board top-level (clocks, VGA pins, HPS, bridge -> pvz_top) |

### Rendering Pipeline Per Scanline

1. `hcount == 0`: `bg_grid` fills draw buffer — 640 pixels, 1 per clock cycle
2. After bg_grid done: `shape_renderer` iterates shapes, overwrites pixels where shapes overlap current line
3. `hcount == 1598`: swap linebuffers (2 cycles early for memory latency)
4. During active display: read from display buffer -> `color_palette` -> VGA RGB output

### Shape Rendering Detail

- **Filled rectangle**: `(x <= px < x+w) && (y <= line < y+h)` -> write color
- **Filled circle**: bounding box check, then `(px-cx)^2 + (py-cy)^2 <= r^2` -> write color. Radius = width/2.
- **7-segment digit**: digit value (0-9) encoded in width[3:0]. Each segment is a small filled rectangle at known offset from (x,y). One shape entry = one digit character.

### Timing Budget

- Background fill: 640 cycles
- Shape rendering: ~20 cycles/shape x 48 shapes = 960 cycles
- Total: ~1600 cycles — fits within one line period (800 pixel clocks x 2 since linebuffer is one line ahead)

### Color Palette (MVP subset)

| Index | Color | Use |
|-------|-------|-----|
| 0 | Black (0,0,0) | Screen border, text |
| 1 | Dark green (34,139,34) | Grid cell dark |
| 2 | Light green (50,205,50) | Grid cell light |
| 3 | Brown (139,69,19) | HUD background |
| 4 | Yellow (255,255,0) | Sun counter, cursor highlight |
| 5 | Red (255,0,0) | Zombie body |
| 6 | Dark red (139,0,0) | Zombie head |
| 7 | Green (0,200,0) | Peashooter body |
| 8 | Dark green (0,128,0) | Peashooter stem |
| 9 | Bright green (0,255,0) | Pea projectile |
| 10 | White (255,255,255) | Digits, text |
| 11 | Gray (128,128,128) | Status bar bg |
| 12 | Orange (255,165,0) | Sun drops |

---

## 3. Screen Layout

**640x480 divided into 4 regions:**

| Region | Y range | Height | Content |
|--------|---------|--------|---------|
| Top HUD | 0-59 | 60px | Plant card (Peashooter), sun counter digits |
| Game grid | 60-419 | 360px | 4 rows x 8 columns, each cell 80x90px, checkerboard green |
| Status bar | 420-459 | 40px | Level indicator, sun count, zombies remaining |
| Control hints | 460-479 | 20px | "ARROWS:MOVE SPACE:PLANT D:DIG" (optional, skip if tight) |

**Grid cell mapping:** cell (r,c) occupies pixels x=[c*80, c*80+79], y=[60+r*90, 60+r*90+89]

---

## 4. Game Logic (HPS Software)

### Game State

```c
#define GRID_ROWS 4
#define GRID_COLS 8
#define MAX_ZOMBIES 5
#define MAX_PROJECTILES 16

#define START_SUN 100
#define SUN_RATE 25          // sun gained per interval
#define SUN_INTERVAL 8       // seconds between sun income
#define PEASHOOTER_COST 50
#define PEASHOOTER_FIRE_RATE 2  // seconds between shots
#define PEA_DAMAGE 1
#define PEA_SPEED 2          // pixels per frame (~120 px/sec)
#define ZOMBIE_HP 3
#define ZOMBIE_SPEED_FRAMES 3 // move 1 pixel every N frames (~20 px/sec)
#define ZOMBIE_SPAWN_MIN 8   // seconds
#define ZOMBIE_SPAWN_MAX 15  // seconds
#define TOTAL_ZOMBIES 5
```

### Entity Structures

- **Plant**: `{row, col, type, fire_cooldown_frames}`
- **Zombie**: `{row, x_pixel, hp, move_counter, active}`
- **Projectile**: `{row, x_pixel, active}`

### Game Loop (60 Hz via usleep)

1. Read keyboard input (`/dev/input/eventX` — arrow keys, space, D, ESC)
2. Update timers (sun production, spawn countdown)
3. Spawn zombie if timer elapsed and zombies_spawned < TOTAL_ZOMBIES
4. Move zombies leftward (decrement x_pixel every ZOMBIE_SPEED_FRAMES frames)
5. Peashooters fire: if zombie exists in same row and cooldown expired, spawn pea
6. Move projectiles rightward (+PEA_SPEED pixels per frame)
7. Collision: pea bounding box overlaps zombie bounding box -> zombie HP--, deactivate pea
8. Remove dead zombies (HP <= 0), remove off-screen projectiles
9. Check win: zombies_spawned == TOTAL_ZOMBIES && zombies_alive == 0
10. Check lose: any zombie x_pixel < grid left edge (x=0)
11. Convert game state -> shape table entries (render.c)
12. Write to FPGA via ioctl

### Input Mapping

| Key | Action |
|-----|--------|
| Arrow keys | Move cursor on grid |
| Space | Place peashooter (if enough sun and cell empty) |
| D | Remove plant at cursor |
| ESC | Quit game |

---

## 5. Kernel Driver

**File:** `sw/pvz_driver.c` (follows lab3 `vga_ball.c` pattern exactly)

- `compatible = "csee4840,pvz_gpu-1.0"` matching `pvz_top_hw.tcl`
- Misc device at `/dev/pvz`
- Three ioctl commands:
  - `PVZ_WRITE_BG` — write background cell (row, col, color)
  - `PVZ_WRITE_SHAPE` — write shape entry (index, type, x, y, w, h, color, visible)
  - `PVZ_COMMIT_SHAPES` — trigger commit (write 1 to SHAPE_COMMIT register)
- Driver maps Avalon registers via `devm_ioremap_resource`, writes with `iowrite32`

---

## 6. File Structure

```
hw/
  Makefile                  # make qsys, make quartus, make rbf, make dtb
  soc_system.qsys           # Platform Designer (add pvz_top as Avalon agent)
  soc_system_top.sv          # Board top-level
  pvz_top.sv                 # Avalon-MM agent, register decode, wires display modules
  pvz_top_hw.tcl             # Platform Designer component descriptor
  vga_counters.sv            # 640x480@60Hz timing (from lab3)
  linebuffer.sv              # Dual 640x8-bit line RAM
  bg_grid.sv                 # 4x8 cell color array
  shape_renderer.sv          # Shape table iteration + pixel generation
  shape_table.sv             # 48-entry shadow/active register file
  color_palette.sv           # 8-bit -> 24-bit RGB LUT
  tb/
    tb_vga_counters.sv
    tb_linebuffer.sv
    tb_bg_grid.sv
    tb_shape_renderer.sv
    tb_pvz_top.sv

sw/
  Makefile                   # Kbuild driver + gcc userspace
  pvz_driver.c               # Kernel module
  pvz_driver.h               # Driver internals
  pvz.h                      # Shared ioctl defs, shape/bg structs
  main.c                     # Game loop, init, cleanup
  game.c / game.h            # Game state, entity logic, collision
  render.c / render.h        # Game state -> shape table conversion
  input.c / input.h          # Keyboard via /dev/input/eventX
  test/
    test_shapes.c            # Visual test: write shapes to FPGA
    test_input.c             # Print key events
    test_game.c              # Host-compilable game logic unit tests
```

---

## 7. Testing & Verification

### Hardware Testbenches (ModelSim)

| Testbench | Verifies |
|-----------|----------|
| `tb_vga_counters` | hsync/vsync timing, hcount/vcount ranges, blanking periods |
| `tb_linebuffer` | Write/swap/read cycle, boundary pixels (0, 639) |
| `tb_bg_grid` | Cell color output for known coordinates, cell boundary pixels |
| `tb_shape_renderer` | All 3 shape types, overlapping shapes (z-order), invisible shapes, scanline correctness |
| `tb_pvz_top` | End-to-end: Avalon writes -> full frame -> verify VGA pixel output |

### On-Board Testing

| Test | Method |
|------|--------|
| Driver load | `insmod pvz_driver.ko`, verify `/dev/pvz` exists, `dmesg` clean |
| Background grid | `test_shapes` sets checkerboard, visual confirm on monitor |
| Shape types | `test_shapes` draws rect, circle, digit at known positions |
| Input | `test_input` prints key events to console |
| Full game | Play through: place plants, kill zombies, verify win/lose |

### Software Unit Tests (host-compilable)

| Test | Verifies |
|------|----------|
| `test_collision` | Pea + zombie overlap -> HP decrease, pea deactivated |
| `test_sun_economy` | Income timing, plant cost deduction, can't overspend |
| `test_zombie_spawn` | Count, random row, interval bounds |
| `test_win_lose` | Win when all dead, lose when zombie reaches col 0 |

---

## 8. Implementation Order

### Phase 1: FPGA Display Foundation
1. `hw/vga_counters.sv` — extract from lab3 `vga_ball.sv`
2. `hw/linebuffer.sv` — dual line RAM with swap
3. `hw/color_palette.sv` — hardcoded LUT
4. `hw/bg_grid.sv` — 4x8 cell color array
5. `hw/pvz_top.sv` — minimal Avalon agent, wires vga_counters + linebuffer + bg_grid + palette
6. `hw/soc_system_top.sv` — adapt from lab3
7. `hw/pvz_top_hw.tcl` — Platform Designer component
8. `hw/Makefile` — adapt from lab3
9. Testbenches: `tb_vga_counters`, `tb_linebuffer`, `tb_bg_grid`
10. **Checkpoint:** synthesize, load on board, see checkerboard grid on VGA monitor

### Phase 2: Shape Renderer
11. `hw/shape_table.sv` — 48-entry shadow/active register file
12. `hw/shape_renderer.sv` — filled rect + filled circle + 7-seg digit rendering
13. Extend `hw/pvz_top.sv` with shape table registers and renderer
14. Testbenches: `tb_shape_renderer`, `tb_pvz_top`
15. **Checkpoint:** write shape entries via SignalTap or test driver, see shapes on screen

### Phase 3: Kernel Driver
16. `sw/pvz.h` — shared ioctl definitions
17. `sw/pvz_driver.c` + `pvz_driver.h` — kernel module
18. `sw/Makefile` — kbuild + userspace targets
19. `sw/test/test_shapes.c` — write test patterns via ioctl
20. **Checkpoint:** `insmod`, run test_shapes, see shapes on VGA from userspace

### Phase 4: Game Software
21. `sw/input.c` + `input.h` — keyboard reader
22. `sw/test/test_input.c` — verify input works
23. `sw/game.c` + `game.h` — game state, entities, update logic, collision
24. `sw/render.c` + `render.h` — game state -> shape table
25. `sw/main.c` — game loop tying everything together
26. `sw/test/test_game.c` — unit tests for game logic
27. **Checkpoint:** playable game on board

### Phase 5: Polish & Documentation
28. Tune gameplay parameters (zombie speed, spawn rate, HP, costs)
29. Add win/lose screen (shape-based "YOU WIN" / "GAME OVER" text via 7-seg digits)
30. Write documentation covering architecture, build steps, and test results

---

## 9. Key Reference Files

| Reference | Path | What to reuse |
|-----------|------|---------------|
| Lab3 VGA ball | `doc/reference-design/lab3/hw/vga_ball.sv` | `vga_counters` module, Avalon register pattern, shadow/active latch |
| Lab3 driver | `doc/reference-design/lab3/sw/vga_ball.c` | Kernel module structure, ioctl pattern, `of_device_id` |
| Lab3 header | `doc/reference-design/lab3/sw/vga_ball.h` | ioctl macro pattern |
| Lab3 hw Makefile | `doc/reference-design/lab3/hw/Makefile` | Quartus/qsys build targets |
| Lab3 sw Makefile | `doc/reference-design/lab3/sw/Makefile` | Kbuild two-pass pattern |
| Lab3 hw.tcl | `doc/reference-design/lab3/hw/` | Platform Designer component descriptor pattern |
| Lab3 soc_system_top | `doc/reference-design/lab3/hw/soc_system_top.sv` | Top-level wiring pattern |
| Bubble-bobble linebuffer | `doc/reference-design/bubble-bobble/code/vga_audio_integration/vga/linebuffer.sv` | Dual-buffer swap logic |
| Bubble-bobble sprite loader | `doc/reference-design/bubble-bobble/code/vga_audio_integration/vga/sprite_loader.sv` | Shape iteration pattern |
| Bubble-bobble VGA top | `doc/reference-design/bubble-bobble/code/vga_audio_integration/vga/vga_top.sv` | Rendering pipeline orchestration |
| Bubble-bobble vga_interface | `doc/reference-design/bubble-bobble/code/game/vga_interface.c` | Userspace ioctl wrapper pattern |
| Bubble-bobble report | `doc/reference-design/bubble-bobble/report.md` | Avalon addressing gotchas (section 6) |

---

## 10. Gotchas & Reminders

- **Avalon address units**: With 32-bit writedata, Avalon uses word offsets but CPU uses byte offsets. Driver must use `iowrite32(val, base + 4*N)`.
- **Shadow/active latch**: All shape table and bg_grid registers must latch at `vcount == 480 && hcount == 0`, not on Avalon write.
- **BRAM read latency**: 1-2 cycles. Account for this in linebuffer read pipeline (read address = hcount + 1).
- **Linebuffer swap timing**: Swap 2 cycles before end of line to account for memory pipeline (bubble-bobble pattern).
- **Three-way tie**: `pvz_top.sv` port names <-> `pvz_top_hw.tcl` interface <-> `pvz_driver.c` compatible string must all match.
- **`dts` generation**: After adding pvz_top to Platform Designer, must regenerate dtb with `make dtb`.
