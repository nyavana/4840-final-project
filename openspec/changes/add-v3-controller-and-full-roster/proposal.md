## Why

The v2Dino baseline on `main` has a playable MVP with one plant (Peashooter sprite), one zombie type (Basic, red rectangle), and keyboard-only control. The original proposal calls for three plants, three zombies, and a USB gamepad. v3 delivers that expanded gameplay and adds Xbox 360 controller support, so the game matches the roster and feel of the shipped design while leaving room for sprites to land incrementally in a later change.

## What Changes

- Add two plant types to the game model: Sunflower (periodic +25 sun, 2 HP, 50 sun cost) and Wall-nut (no attack, 8 HP, 50 sun cost). Keep Peashooter unchanged (3 HP, fires every 2 s, 50 sun cost).
- Add two zombie types: Conehead (6 HP) and Buckethead (12 HP). Basic zombie remains at 3 HP. All three share the current walk speed (~20 px/s).
- Replace the random-interval spawn loop with a precomputed wave table: 10 zombies per level with hand-tuned spawn frames and types, each spawn jittered by ±60 frames at `game_init` so runs feel different without breaking difficulty pacing.
- Add a plant-selector HUD along the top of the screen: three cards (Peashooter, Sunflower, Wall-nut), one highlighted, cycled with shoulder buttons on the gamepad or `[` / `]` on the keyboard. Place-plant uses the currently selected card.
- Rewrite `sw/input.c` to auto-detect a USB gamepad at startup. If an Xbox 360–compatible controller (`BTN_GAMEPAD` / `BTN_SOUTH` capability bits via evdev) is present, use it. Otherwise fall back to the first keyboard-capable event device. Emit a unified `input_action_t` enum so game code never branches on device.
- **BREAKING** Grow the FPGA shape table from 48 to 64 entries (`shape_table.sv`, `shape_renderer.sv`, `tb_*`, driver range-check). The Avalon register layout and compatible string are unchanged because `SHAPE_ADDR[5:0]` already carries 6 bits. Shape-index allocation in `render.c` is re-budgeted.
- Render new plants and zombies as primitive shapes (circle / rectangle, color-coded by type) since sprites only exist for Peashooter today. Sprite ROMs for the new entities land in a later change.
- Add host-compilable unit tests covering plant types, zombie HP tiers, wave-table determinism, and plant-selection cycling. Add a new on-board utility `test_input_devices.c` that reports what evdev devices are present and which one the auto-detect picked. Extend existing ModelSim TBs to cover the expanded 64-entry shape table.

## Capabilities

### New Capabilities
- `controller-input`: evdev-based USB gamepad reader for Xbox 360–compatible controllers, exposing d-pad and face / shoulder buttons to the game as unified action codes.
- `plant-selector-ui`: top-HUD row of plant cards with a cycle-driven highlight, backed by a `selected_plant` field on the game state that `place_plant` reads instead of hardcoding Peashooter.
- `wave-spawn-system`: precomputed per-level spawn schedule with per-entry jitter applied at `game_init`, replacing the random-interval spawn loop.

### Modified Capabilities
- `game-logic`: plant entities gain a `type` field (Peashooter / Sunflower / Wall-nut) with per-type HP and per-type action-timer behavior (fire / sun-gen / idle). Zombie entities gain a `type` field (Basic / Conehead / Buckethead) with per-type max HP. The fixed `TOTAL_ZOMBIES = 5` constant grows to `10` to fit the expanded wave table.
- `game-rendering`: `render_plants` branches on plant type (peashooter sprite, sunflower circle, wall-nut circle). `render_zombies` branches on zombie type for color-coded rectangles. New render paths for plant cards, selection highlight, and the expanded shape-index budget.
- `keyboard-input`: adds `[` and `]` bindings for cycling the selected plant. Renamed internally to part of the unified `input` pipeline; existing bindings (arrows, space, D, ESC) keep the same semantics.
- `fpga-display-engine`: `shape_table` grows to 64 entries; `shape_renderer` iterates 0..63 instead of 0..47. The shadow / active latch-at-vsync discipline is preserved.
- `avalon-register-interface`: no register-layout change, but the driver's index-bounds check in `PVZ_WRITE_SHAPE` widens from `< 48` to `< 64`.

## Impact

- **Hardware**: `hw/shape_table.sv` (entry count), `hw/shape_renderer.sv` (iteration bound), `hw/tb/tb_shape_renderer.sv` and `hw/tb/tb_pvz_top.sv` (cover indices ≥ 48). No changes to `pvz_top_hw.tcl`, compatible string, or register map. Requires re-synthesis (`make qsys && make quartus && make rbf`).
- **Kernel driver**: `sw/pvz_driver.c` range-check widens. No ABI change; ioctl numbers and argument structs stay the same.
- **Userspace software**: `sw/game.{h,c}`, `sw/render.{h,c}`, `sw/input.{h,c}`, `sw/main.c`, `sw/pvz.h`, `sw/test/test_game.c`, new `sw/test/test_input_devices.c`.
- **Documentation**: `doc/v3-changes.md` (new) summarising the diff from v2Dino, `CLAUDE.md` updates for the 48 → 64 invariant and the new input API. Prose passed through `/humanizer` before landing.
- **Git / branching**: work happens on a new worktree-backed branch `v3-controller-and-roster` off `main`. Initial commit after the data-model phase, a second commit after the renderer phase, more for plant-selector / input / docs. Branch pushed to `nyavana/pvz-fpga` at the end.
- **Known-issues discipline**: any defect surfaced during integration that is not fixed in-branch gets an entry in `doc/v3-known-issues.md` with severity and a proposed fix before the branch is pushed.
