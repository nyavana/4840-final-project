# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Plants vs Zombies on the Terasic DE1-SoC (Cyclone V) — a CSEE4840 Embedded Systems final project. The full design is described in `doc/proposal/proposal.md` (read this first for gameplay rules, balance numbers, and the HW/SW split). Target: 640x480 VGA @ 60 Hz with hardware sprite blitting, USB gamepad input, and audio over the Wolfson codec.

**Current state:** Milestone 1 (MVP) is complete. v3 (branch `v3-controller-and-roster`) adds the full roster of three plants and three zombies, a wave-table spawn system, a plant-selector HUD, and USB gamepad auto-detect with keyboard fallback. CI/CD via GitHub Actions cross-compiles software for ARM on every push and creates releases on `v*` tags. Board deployment is automated via `deploy.sh`.

## Repository layout

- `hw/` — FPGA design (SystemVerilog, Quartus/Platform Designer flow)
- `sw/` — HPS software (kernel driver, game loop, tests). Makefile supports cross-compilation via `CC`, `ARCH`, `CROSS_COMPILE`, `KERNEL_SOURCE` variables.
- `.github/workflows/build.yml` — CI: cross-compiles SW for ARM on push, creates GitHub Releases on `v*` tags.
- `deploy.sh` — board-side deployment script (setup/download/install/run/test/status).
- `worktree.sh` — git worktree helper for parallel feature development.
- `doc/proposal/proposal.md` — the authoritative spec for the project (game rules, architecture split, major tasks).
- `doc/manual/DE1-SoC_User_manual.md` — DE1-SoC board reference (pinouts, peripherals, codec, etc.).
- `doc/reference-design/lab3/` — the class-provided Lab 3 skeleton (VGA ball peripheral + Linux driver). Key files:
  - `hw/vga_ball.sv` — minimal SystemVerilog Avalon-MM peripheral example.
  - `hw/soc_system_top.sv`, `hw/soc_system.qsys`, `hw/Makefile` — the Quartus/Platform Designer flow.
  - `sw/vga_ball.c`, `sw/vga_ball.h`, `sw/hello.c`, `sw/Makefile` — kernel-module driver + userspace test program.
  - `description/description.md` — full write-up of how to wire a new peripheral into Platform Designer, generate the dtb, and load the module.
- `doc/reference-design/bubble-bobble/` — a prior team's full project (sprite/tile engine, audio mixer, USB gamepad game loop). Use this as the architectural reference for our sprite engine, line-buffer strategy, and HPS↔FPGA register layout. See `report.md` §2.1 (VGA/linebuffer), §4 (interfaces), and `code/` for working SystemVerilog, kernel modules, and a libusb game loop.

The outer directory (`../proposal/`, `../references/`, `../reports/`, `../scratch/`, `../worktrees/`) sits *outside* this git repo and is the user's personal workspace. Do not put project source files there.

## Architecture (from proposal)

Hard split between FPGA and HPS, communicating via memory-mapped registers on the lightweight HPS-to-FPGA bridge (base `0xff200000`):

- **FPGA (SystemVerilog):** VGA timing generator, double-buffered frame buffer (or line buffer + tile/sprite ROMs, bubble-bobble style), sprite blitting engine that reads a sprite table populated by software at vsync, audio sample streaming to the Wolfson codec, and an Avalon-MM agent exposing the register interface. Frame buffer bit depth (8-bit indexed vs 4-bit vs tile-based) is an open design decision — see proposal §Display.
- **HPS (Linux, C):** game loop, 5×9 grid state, plant/zombie/projectile logic, grid-level collision detection, sun economy, USB gamepad polling via libusb, and writes to the sprite table / audio registers each frame. The kernel driver exposes the Avalon registers as a misc device with `ioctl`s (follow the `vga_ball.c` pattern).

**Audio (shipped via `v3-audio-engine`):** A second Avalon-slave peripheral
`pvz_audio` at `0xff201000` (separate from `pvz_gpu`'s `0xff200000`) plays a
looping PvZ theme song over the WM8731 codec during `STATE_PLAYING`. Hardware
path: `voice_bgm` + 2× `voice_sfx` feed a 3-input clamp `mixer`, then the Intel
`altera_up_avalon_audio` IP streams to the codec over I²S. BGM samples live in
`bgm_rom.mem`; SFX samples in `sfx_rom.mem`; both are `$readmemh`-loaded BRAMs.
`sfx_rom.mem` currently contains zero samples (silent SFX) — adding audible
SFX is an asset-only change via `hw/audio/build_audio_mif.py` (see
`hw/audio/README.md`).

Every new hardware peripheral needs a matching Platform Designer component, a `set_module_assignment embeddedsw.dts.*` block in its `_hw.tcl` so it shows up in the generated dts as `compatible = "csee4840,<name>-1.0"`, and a kernel module whose `of_device_id` table matches that compatible string. This three-way tie (Verilog top-level, `_hw.tcl`, kernel driver) is the thing most likely to silently break.

### Gotchas worth remembering up front

- **Avalon address units follow `writedata` width.** If you widen `writedata` from 8 → 16 → 32 bits, the Avalon address port switches to word offsets, but the bus is still byte-addressed from the CPU side — the kernel driver must use byte offsets (`iowrite32` at `base + 4*N`), not word offsets. The bubble-bobble report §6 explicitly calls this out as something that cost them debugging time.
- **Shadow/active register latching at vsync** is how the lab3 ball avoids tearing (`vga_ball.sv` lines 36–63). Any register that affects what's drawn must be latched at `vcount == VACTIVE && hcount == 0`, not on the Avalon write.
- **BRAM reads are 1–2 cycles.** Disable registered output on memory IP or account for the extra cycle in pipelines — another bubble-bobble lesson (§6).
- **Linebuffer approach** (bubble-bobble): draw line N+1 into buffer B while displaying line N from buffer A, swap on hsync. Gives 1600 cycles per line to produce pixels and makes sprite transparency trivial. A full 640×480×8bpp frame buffer in on-chip SRAM is also viable but tighter on memory — pick deliberately, see proposal §Display.
- **Shape-table size is 64** (as of v3). `shape_table.sv`, `shape_renderer.sv` iteration bound, and `pvz_driver.c`'s range check all agree on 0..63. `SHAPE_ADDR` has always been 6 bits so no register-layout change was needed; only the pack arrays and the bound moved. Any new HUD element picks from the reserved slots 51–62; the cursor at slot 63 must stay on top.
- **Z-order by shape index.** The renderer is painter's-algorithm: slot `i` paints on top of slot `j` when `i > j`. The v3 plant-selector uses this intentionally — the yellow highlight sits at slot 43 below the cards at 44–46, so each card covers the centre of the highlight and only a 2-pixel yellow border around the selected card remains visible.
- **Wave-table spawn discipline** (v3). `sw/game.c::game_init` seeds `gs->wave[]` from a compile-time template with ±60 frame jitter. `update_spawning` walks the array by `frame_count`. If every zombie slot is busy when the scheduled frame hits, the spawn is held — `wave_index` only advances once the spawn lands. Tests rely on this determinism under a fixed `srand` seed.
- **`gs->selected_plant` drives placement** (v3). `game_place_plant` no longer hardcodes Peashooter; it reads `selected_plant ∈ {0,1,2}` and maps it to PLANT_PEASHOOTER / _SUNFLOWER / _WALLNUT. `main.c`'s `INPUT_CYCLE_PREV` / `INPUT_CYCLE_NEXT` handlers call `cycle_plant_prev` / `cycle_plant_next`, which modulo-3 the selection and no-op while game-over.
- **Input auto-detect** (v3). `input_init()` takes no arguments. It enumerates `/dev/input/event0..31`, picks the first device advertising `BTN_SOUTH` as a gamepad, falls back to the first device advertising `KEY_SPACE`, and logs the chosen path. The new `input_action_t` enum (`INPUT_UP/_DOWN/_LEFT/_RIGHT/_PLACE/_DIG/_CYCLE_PREV/_CYCLE_NEXT/_QUIT/_NONE`) is the unified action space — game code never branches on device. On the board image, `modprobe xpad` is required for Xbox 360–compatible controllers; without it, auto-detect silently falls back to keyboard. `sw/test/test_input_devices` prints the enumeration verdict and is the first thing to run after plugging or unplugging a controller.
- **Three-way compatible-string tie (second peripheral).** Like `pvz_gpu`:
  `hw/pvz_audio_hw.tcl`'s `embeddedsw.dts.compatible "csee4840,pvz_audio-1.0"`,
  the generated dtb, and `sw/pvz_audio_driver.c`'s `of_device_id` must
  agree character-for-character.
- **Audio event bus.** `game_update()` and `game_place_plant()` take an
  `audio_events_t *ev` output parameter (NULL-safe). main.c clears
  `ev.flags = 0` once per frame, passes the same struct to both calls, and
  fans out set bits to `audio_play_sfx()` after game logic. game.c sets bits
  for in-game events (PEA_FIRE/PEA_HIT/ZOMBIE_BITE/ZOMBIE_DEATH/PLANT_PLACE/
  WAVE_START); main.c also emits GAME_OVER/VICTORY directly on state-edge
  transitions.
- **Scope A silent SFX ROM.** `sfx_rom.mem` is populated with zero samples
  on purpose. The full ioctl → driver → register → voice → mixer path is
  exercised; making SFX audible is a matter of running `make audio` with
  real WAVs in `hw/audio/assets/` and committing the regenerated `.mem` +
  `.svh` files.

## Build & deploy workflow

### CI/CD (GitHub Actions)

`.github/workflows/build.yml` runs on every push:
- **`build-sw`**: cross-compiles `pvz_driver.ko`, `pvz`, and test binaries for ARM using `arm-linux-gnueabihf-gcc` + kernel headers 4.19.0
- **`test-host`**: compiles and runs `test_game` natively (the only test with no hardware dependency)
- **`release`** (on `v*` tag push only): creates a GitHub Release with all ARM binaries + `deploy.sh` attached

FPGA artifacts (`.rbf`, `.dtb`) are built on Columbia workstations and uploaded manually: `gh release upload <tag> output_files/soc_system.rbf soc_system.dtb`

### Hardware (Quartus / Platform Designer), on a workstation:
```bash
cd hw/
make qsys      # regenerate Verilog from soc_system.qsys
make quartus   # full synthesis → output_files/soc_system.sof
make rbf       # convert .sof → .rbf for SD card
make dtb       # sopc2dts + dtc → soc_system.dtb (requires embedded_command_shell.sh in PATH)
```

### Software (cross-compile or on-board)

`sw/Makefile` supports cross-compilation via `CC`, `ARCH`, `CROSS_COMPILE`, and `KERNEL_SOURCE` variables:
```bash
cd sw/
make                          # on-board native build (default)
make CC=arm-linux-gnueabihf-gcc \
     ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- \
     KERNEL_SOURCE=/path/to/headers   # cross-compile
```

### Board deployment

`deploy.sh` runs on the DE1-SoC via serial console:
```bash
./deploy.sh setup      # first-time: install kernel headers
./deploy.sh download   # download latest release from GitHub
./deploy.sh install    # load kernel module
./deploy.sh run        # start the game
./deploy.sh test <n>   # run a test (test_shapes, test_input, test_input_devices, test_game)
./deploy.sh status     # show current state
```

### Development workflow (worktrees)

`worktree.sh` manages parallel feature branches:
```bash
./worktree.sh new <name>     # create ../worktrees/<name> + branch feature/<name>
./worktree.sh list           # list active worktrees
./worktree.sh clean <name>   # remove worktree + branch
```

### Useful on-board debugging:
```bash
echo 8 > /proc/sys/kernel/printk   # show kernel module pr_info output on console
dmesg | tail                        # or read it from dmesg
cat /proc/iomem                     # confirm the peripheral is mapped at ff20xxxx
ls /proc/device-tree/sopc@0/...     # confirm the dtb entry is present
dmesg | grep pvz_audio             # audio driver probe messages
ls /dev/pvz_audio                  # should exist after insmod
grep ff20 /proc/iomem              # audio peripheral region (ff201000)
```
The board's serial console is `screen /dev/ttyUSB0 115200` from the workstation over the mini-USB cable.
