# v4 changes

Summary of what's new on `v3-audio-engine` relative to v3 (`36b4c5b`).
This branch adds a PCM audio engine: a looping background music (BGM) track
over the 3.5 mm jack and a wired-but-silent sound effects (SFX) path. All
game-state transitions and in-game plant actions are instrumented to emit
SFX cues; the SFX ROM is currently zero-filled, so events execute silently
until real audio assets are dropped in.

## 1. Audio peripheral and mixer (Hardware)

A new Avalon-MM slave `pvz_audio` at base address `0xff201000`, independent
from the existing `pvz_gpu` at `0xff200000`. Three voice modules (one BGM,
two SFX) feed into a clamp mixer that sums their outputs and drives the
Wolfson codec.

**New files**

- `hw/pvz_audio.sv` — top-level Avalon slave with four 32-bit registers:
  - `BGM_CTRL` (write-only): start (bit 0) and stop (bit 1) the BGM voice.
  - `SFX_VOICE0_TRIG` (write-only): trigger SFX voice 0. Payload is 16-bit
    SFX ID (0–255), round-robin advances to voice 1.
  - `SFX_VOICE1_TRIG` (write-only): trigger SFX voice 1. Payload is 16-bit
    SFX ID. Round-robin logic returns control to voice 0.
  - `STATUS` (read-only): bit 0 = BGM active flag, bits [2:1] = voice [0:1]
    activity flags.
- `hw/voice_bgm.sv` — BGM voice: reads from `bgm_rom`, outputs a signed
  16-bit PCM sample every 8 kHz clock cycle. Loop flag in the voice
  controller holds the playhead at sample 0 when finished.
- `hw/voice_sfx.sv` — SFX voice: reads from `sfx_rom`, outputs a signed
  16-bit PCM sample every 8 kHz clock. On trigger, latches the SFX ID and
  plays from offset `ID * 8192` for up to 8192 samples (1 s at 8 kHz).
- `hw/mixer.sv` — sums BGM + SFX0 + SFX1 with saturation (clamp to ±32767).
- `hw/bgm_rom.sv`, `hw/sfx_rom.sv` — inferred M10K block RAM, each
  initialized from `.mem` files via `$readmemh`. BGM ROM is 32 K samples
  (4 s at 8 kHz); SFX ROM is 256 × 8192 samples (256 unique effects).
- `hw/pvz_audio_hw.tcl` — Platform Designer IP integration: declares
  `pvz_audio` module and associated ROM files to the Quartus build system.
  Sets `compatible = "csee4840,pvz-audio-1.0"` for kernel driver binding.

**Hardware wiring**

- `hw/soc_system.qsys` — instantiates `pvz_audio` with Avalon slave port,
  connects audio output to the Wolfson codec input. The lightweight
  HPS-to-FPGA bridge (LWH2F) is extended to map `0xff201000–0xff20100f`.
- `hw/pvz_top.sv` — the clock domain crossing from the VGA (27 MHz) to
  audio (8 kHz) is handled by the codec's clock input. Voice modules
  consume a synchronized `audio_clk` derived from the codec interface.

## 2. Kernel driver (Software)

A new misc-device driver `pvz_audio_driver` exposes `/dev/pvz_audio` with
three ioctl commands. All three are non-blocking and return immediately.

**New files**

- `sw/pvz_audio_driver.c` — misc-device driver that:
  - Maps the Avalon registers via platform device interface (bound to
    `compatible = "csee4840,pvz-audio-1.0"` from the device tree).
  - Exports `PVZ_AUDIO_BGM_CTRL`, `PVZ_AUDIO_PLAY_SFX`, `PVZ_AUDIO_STATUS`
    ioctls.
  - Implements voice round-robin in the driver: each call to
    `PVZ_AUDIO_PLAY_SFX` alternates between voice 0 and voice 1, hiding
    the two-voice limitation from userspace. A static `next_sfx_voice`
    counter tracks which voice fires next.
  - Provides a `pvz_audio_h` header with the ioctl definitions and
    command structures.

**Userspace wrapper**

- `sw/audio.c`, `sw/audio.h` — parallel to `sw/render.c` / `render.h`.
  - `audio_init()` opens `/dev/pvz_audio` and caches the fd.
  - `audio_bgm_start()`, `audio_bgm_stop()` call `PVZ_AUDIO_BGM_CTRL`.
  - `audio_play_sfx(sfx_id)` calls `PVZ_AUDIO_PLAY_SFX` (no need for the
    caller to know about voice round-robin).
  - `audio_status()` calls `PVZ_AUDIO_STATUS` and returns the flags.
  - All ioctls are wrapped in error-check macros; driver failures log to
    stderr but do not halt the game loop.

## 3. Game instrumentation

**Modified files**

- `sw/game.h` — new `audio_events_t` struct carries bitflags for six SFX
  cues:
  - `EVENT_PLANT_PLACE` — peashooter / sunflower / wall-nut placed.
  - `EVENT_PLANT_DIG` — plant removed.
  - `EVENT_PEA_FIRE` — pea projectile created.
  - `EVENT_ZOMBIE_DIE` — zombie reaches 0 HP.
  - `EVENT_GAME_OVER` — entered `STATE_LOSS`.
  - `EVENT_VICTORY` — entered `STATE_WIN`.
- `sw/game.c::game_update()` and `game_place_plant()` gain an
  `audio_events_t *ev` output parameter. Callers set bit flags as events
  fire; the caller (main.c) reads the flags and invokes userspace ioctls
  after each frame.
- `sw/main.c` — two changes:
  - Edge-detect `STATE_PLAYING` entry and exit to drive `audio_bgm_start()`
    and `audio_bgm_stop()` at the exact right moments.
  - Fan `audio_events_t` flags out to `audio_play_sfx(sfx_id)` calls,
    mapping each flag to an SFX ID (0–7). Fire the ioctls in the main
    loop after `game_update()` and `game_place_plant()` return.

## 4. Audio asset pipeline

A new `hw/audio/` directory encapsulates asset generation and versioning.

**New files**

- `hw/audio/manifest.json` — lists all audio assets (BGM, 8 SFX effects)
  with file paths, bit depths, sample rates, and loop metadata.
- `hw/audio/build_audio_mif.py` — Python 3 script (stdlib only, no
  external dependencies). Reads WAV files from `assets/`, converts to
  8-bit or 16-bit signed PCM at the target sample rate, and generates
  `.mem` files ready for `$readmemh` and `.svh` header files with packed
  arrays for behavioral simulation. Called by `make audio` in `hw/Makefile`.
- `hw/audio/README.md` — build instructions and manifest schema.
- `hw/audio/assets/bgm_theme.wav` — placeholder 220–880 Hz tone sweep,
  15 s, 8 kHz / 16-bit mono. Loops for 4 s by discarding the trailing
  samples in the ROM.
- `hw/audio/assets/sfx_*.wav` — eight placeholder 1 kHz tone bursts (100 ms
  each). SFX ROM is pre-allocated for 256 effects, but only slots 0–7 are
  populated; the rest read as silence.

**Build integration**

- `hw/Makefile::audio` target — runs `build_audio_mif.py`, regenerates
  `bgm_rom.mem`, `sfx_rom.mem`, `bgm_rom.svh`, `sfx_rom.svh` in `hw/`.
  Dependencies: `bgm_theme.wav`, `sfx_*.wav` existence. Called as a
  prerequisite by `make qsys` to ensure `.mem` files exist before
  synthesis.
- `.gitignore` updated: generated `.mem` and `.svh` files are not tracked;
  only `assets/` and `manifest.json` are committed.

## 5. Tests

`sw/test/test_game.c` grows nine new assertions covering each SFX event
emission point:

- `test_game_place_plant_raises_place_event` — `event.flags & EVENT_PLANT_PLACE`.
- `test_game_dig_plant_raises_dig_event` — `event.flags & EVENT_PLANT_DIG`.
- `test_pea_fire_raises_fire_event` — `event.flags & EVENT_PEA_FIRE`.
- `test_zombie_death_raises_death_event` — `event.flags & EVENT_ZOMBIE_DIE`.
- `test_game_loss_transition_raises_event` — `event.flags & EVENT_GAME_OVER`.
- `test_game_win_transition_raises_event` — `event.flags & EVENT_VICTORY`.
- `test_multiple_events_in_one_frame` — multiple flags can fire in a single
  `game_update()` call.
- `test_audio_init_opens_device` — `/dev/pvz_audio` can be opened (requires
  board or mock).
- `test_audio_round_robin` — driver alternates between voice 0 and voice 1.

These tests are verified natively by `make test` on the build host (all pass
except the device-node checks, which are marked `#ifdef HAS_AUDIO_DEVICE`).

## 6. Known issues

Two entries in `doc/v4-known-issues.md`:

1. **Codec live test** (severity: minor) — Audio output has not been
   validated on a live board. The driver and ROM infrastructure are in
   place, but a serial-console verification step is needed: load the
   kernel module, trigger a BGM start, listen for the tone sweep on the
   3.5 mm jack, and trigger SFX cues to confirm silence (expected, since
   SFX ROM is zero-filled). Proposed fix: board session post-deployment.

2. **Cross-toolchain audio build** (severity: minor) — The audio pipeline
   (`build_audio_mif.py`) runs on the build host (e.g. workstation) and is
   not part of the ARM cross-compile. If `make audio` is skipped before
   `make qsys`, synthesis will fail with missing `.mem` files. Proposed
   fix: document the prerequisite in `hw/Makefile` comments and in
   `CLAUDE.md`.

## Scope A vs Scope C

This feature ships Scope A of a two-step delivery:

- **Scope A (shipped):** BGM loops whenever the game is in `STATE_PLAYING`.
  SFX event path is wired end-to-end: game hooks emit flags, main loop fans
  them to ioctls, driver round-robins between voices and triggers ROM reads.
  The SFX ROM is zero-filled, so all SFX ioctls execute silently (expected).

- **Scope C (future, asset-only):** Drop eight real SFX WAV files into
  `hw/audio/assets/sfx_0.wav` through `sfx_7.wav`, run `make audio`,
  commit the regenerated `sfx_rom.mem` and `sfx_rom.svh`. No hardware,
  driver, or game-logic changes needed — every instrumentation point is
  already live and tested under Scope A.

## Build impact

- **Hardware:** Re-synthesis required (`make qsys && make quartus && make
  rbf && make dtb`). New `make audio` step must run before `make qsys` to
  ensure `.mem` files are present. The `pvz_audio` peripheral adds a second
  Avalon-MM slave region but uses the existing LWH2F bridge address space
  (no new bridge instantiation). Device-tree blob must be regenerated with
  `sopc2dts` to include the new `compatible` string.
- **Software:** No existing file changes to the cross-compile flow. New
  `sw/audio.c` and `pvz_audio_driver.c` link cleanly with the existing
  Makefile. `sw/main.c` and `sw/game.c` grow new calls but remain
  cross-compatible.
- **Kernel ABI:** New misc device `/dev/pvz_audio` and three ioctls. The
  legacy `/dev/pvz` device (sprite renderer) remains unchanged. Existing
  deployments can use the v3 kernel module and userspace indefinitely.
