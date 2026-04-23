# v4 changes

Summary of what's new on `v3-audio-engine` relative to v3 (`36b4c5b`).
This branch adds a PCM audio engine: a looping background music (BGM) track
over the 3.5 mm jack and a wired-but-silent sound effects (SFX) path. All
in-game events and state transitions are instrumented to emit SFX cues; the
SFX ROM is currently zero-filled, so events execute silently until real
audio assets are dropped in.

## 1. Audio peripheral and mixer (Hardware)

A new Avalon-MM slave `pvz_audio` at base address `0xff201000`, independent
from the existing `pvz_gpu` at `0xff200000`. Three voice modules (one BGM,
two SFX) feed into a clamp mixer that sums their outputs and streams to the
Wolfson WM8731 codec via the Intel `altera_up_avalon_audio` IP over I²S.

**New files**

- `hw/pvz_audio.sv` — top-level Avalon slave with four 32-bit registers
  (address units = WORDS):
  - `BGM_CTRL` (write-only): bit 0 = play (1) / stop (0).
  - `SFX_VOICE0_TRIG` (write-only): bits [3:0] = cue id (1..8). Writing
    pulses `trig_valid` on voice 0. Cue id 0 forces the voice idle.
  - `SFX_VOICE1_TRIG` (write-only): same semantics for voice 1.
  - `STATUS` (read-only): bit 0 = `bgm_active`, bit 1 = `sfx0_active`,
    bit 2 = `sfx1_active`.
- `hw/voice_bgm.sv` — BGM voice: address counter + play FSM. On every
  `sample_tick` while `play=1`, walks `addr` through `[0, BGM_LEN-1]` and
  wraps. Output forced to 0 when `play=0`.
- `hw/voice_sfx.sv` — SFX voice: IDLE/PLAYING FSM, cue-triggered.
  Trigger with cue id 1..8 loads `SFX_START[cue]..SFX_END[cue]` and
  enters PLAYING. On the last sample a `done` flag holds `playing` high
  for one more cycle so the tail sample reaches the mixer. Cue id 0
  forces IDLE; cue ids 9..15 are reserved no-ops.
- `hw/mixer.sv` — 3-input signed adder with symmetric saturation to int16
  (`±32767`). Combinational.
- `hw/bgm_rom.sv`, `hw/sfx_rom.sv` — inferred M10K block RAM, each
  initialized from a `.mem` file via `$readmemh`. Production depth:
  `bgm_rom` 131 072 samples (room for ~16 s at 8 kHz; current BGM uses
  120 000 = 15 s), `sfx_rom` 32 768 samples (8 cue slots × up to
  ~4 s total shared across slots; current build packs 8 × 2 400 = 19 200
  samples).
- `hw/pvz_audio_hw.tcl` — Platform Designer component descriptor. Declares
  clock/reset/Avalon-MM-slave/two Avalon-ST sources (8 bits/symbol × 16-bit
  data). Sets `embeddedsw.dts.compatible "csee4840,pvz_audio-1.0"` for
  kernel driver binding.

**Hardware wiring**

- `hw/soc_system.qsys` — adds `audio_pll_0` (12 MHz MCLK),
  `audio_and_video_config_0` (one-time I²C setup of the codec),
  `audio_0` (`altera_up_avalon_audio`, streaming 16-bit), and
  `pvz_audio_0` at LWH2F offset `0x1000` → CPU byte address
  `0xff201000`. Added headlessly via qsys-script + direct XML edits.
- `hw/soc_system_top.sv` — routes the WM8731 conduits
  (`AUD_XCK / AUD_BCLK / AUD_DACDAT / AUD_DACLRCK /
   FPGA_I2C_SCLK / FPGA_I2C_SDAT`) from the Qsys-exported ports to the
  board pins. Dummy assignments for the now-driven pins were removed.

## 2. Kernel driver (Software)

A new misc-device driver `pvz_audio_driver` exposes `/dev/pvz_audio` with
three ioctls, matching the `pvz_driver` pattern.

**New files**

- `sw/pvz_audio_driver.h` — three ioctl macros: `PVZ_AUDIO_BGM_CTRL`,
  `PVZ_AUDIO_PLAY_SFX`, `PVZ_AUDIO_STATUS` (all 32-bit `uint32_t` arg).
- `sw/pvz_audio_driver.c` — misc-device driver:
  - Binds to `.compatible = "csee4840,pvz_audio-1.0"`.
  - Maps the Avalon slave, tracks `last_voice` for round-robin: each
    `PVZ_AUDIO_PLAY_SFX` writes to whichever voice did NOT last play,
    hiding the two-voice limitation from userspace.
  - `PVZ_AUDIO_STATUS` reads the STATUS register into the user-provided
    `uint32_t`.

**Userspace wrapper**

- `sw/audio.c`, `sw/audio.h` — parallels `sw/render.c` / `render.h`:
  - `audio_init()` opens `/dev/pvz_audio` and caches the fd.
  - `audio_bgm_start()`, `audio_bgm_stop()` call `PVZ_AUDIO_BGM_CTRL`.
  - `audio_play_sfx(pvz_sfx_cue_t)` calls `PVZ_AUDIO_PLAY_SFX`;
    `PVZ_SFX_NONE` is a safe no-op.
  - `audio_close()` closes the fd.
  - All ioctls are NULL-safe (no-op if init failed) so the game loop
    stays alive with audio silent on driver failure.

## 3. Game instrumentation

**Modified files**

- `sw/pvz.h` — adds `pvz_sfx_cue_t` enum (`PVZ_SFX_NONE=0`,
  `PVZ_SFX_PEA_FIRE=1`, `PVZ_SFX_PEA_HIT=2`, `PVZ_SFX_ZOMBIE_BITE=3`,
  `PVZ_SFX_ZOMBIE_DEATH=4`, `PVZ_SFX_PLANT_PLACE=5`,
  `PVZ_SFX_WAVE_START=6`, `PVZ_SFX_GAME_OVER=7`, `PVZ_SFX_VICTORY=8`) and
  the `PVZ_AUDIO_EVENT(cue)` bitmask macro.
- `sw/game.h`, `sw/game.c` — new `audio_events_t { uint32_t flags; }`
  struct; `game_update()` and `game_place_plant()` gain an
  `audio_events_t *ev` output parameter (NULL-safe). The contract is
  **caller-clears** (main.c zeros `ev.flags` once per frame) and
  **functions OR** new events in — never overwrite. Six in-game events
  are emitted: `PEA_FIRE` (pea spawned), `PEA_HIT` (pea×zombie
  collision), `ZOMBIE_BITE` (per bite interval), `ZOMBIE_DEATH` (zombie
  HP ≤ 0), `PLANT_PLACE` (place success), `WAVE_START` (each scheduled
  zombie spawn).
- `sw/main.c`:
  - `audio_init()` called after `render_init()`; `audio_close()` before
    input cleanup. Non-fatal on failure.
  - `audio_bgm_start()` called once after `game_init()` so BGM plays
    from frame 0.
  - State-edge tracker (`last_state`) fires `audio_bgm_stop()` + one
    of `PVZ_SFX_GAME_OVER` / `PVZ_SFX_VICTORY` on the transition out of
    `STATE_PLAYING`.
  - `audio_events_t ev = {0}` allocated once per frame; passed to both
    `process_input(&gs, &ev)` (which forwards to `game_place_plant`) and
    `game_update(&gs, &ev)`. After the game step, a fan-out loop over
    cues 1..6 calls `audio_play_sfx(c)` for each set bit.

## 4. Audio asset pipeline

A new `hw/audio/` directory encapsulates asset generation.

**New files**

- `hw/audio/manifest.json` — one BGM entry + 8 SFX cue entries
  (`cue_id` 1..8, names matching the enum), plus
  `silent_samples_per_missing_cue: 2400` (0.3 s of silence to substitute
  for any WAV that isn't on disk).
- `hw/audio/build_audio_mif.py` — Python 3 stdlib-only generator:
  validates WAV format (8 kHz / 16-bit / mono, refuses anything else),
  packs BGM into `hw/bgm_rom.mem`, packs the 8 SFX clips back-to-back
  into `hw/sfx_rom.mem`, and emits `hw/sfx_offsets.svh` (an SV include
  with `parameter logic [14:0] SFX_START[1:8]` and `SFX_END[1:8]`
  arrays).
- `hw/audio/README.md` — asset format requirements, regeneration
  workflow, and the Scope A "missing asset" convention.
- `hw/audio/assets/bgm_theme.wav` — placeholder 220 → 880 Hz tone sweep,
  15 s, 8 kHz / 16-bit mono. Replace with a real PvZ-theme arrangement
  and re-run `make audio`.

**Build integration**

- `hw/Makefile::audio` phony target runs
  `python3 audio/build_audio_mif.py` and regenerates the three files
  (`hw/bgm_rom.mem`, `hw/sfx_rom.mem`, `hw/sfx_offsets.svh`). The three
  generated files are **committed** to git (unlike build artifacts) so
  that `make qsys` and `make quartus` work without the asset pipeline
  running first.
- `sw/Makefile` — `obj-m` now builds both `pvz_driver.o` and
  `pvz_audio_driver.o`; `pvz` links `audio.c`; new `test_audio` target
  compiles the on-board smoke test.

## 5. Tests

**ModelSim testbenches**

- `hw/tb/tb_mixer.sv` — 8 corner cases including positive/negative clip.
- `hw/tb/tb_voice_bgm.sv` — address walk, wrap, stop, restart.
- `hw/tb/tb_voice_sfx.sv` — idle, per-cue playback, retrigger, cue 0
  stops, cue 9 no-op. Requires `vlog +define+TB_VOICE_SFX` to pick up
  the fixture offsets instead of the production silent ones.
- `hw/tb/tb_pvz_audio.sv` — end-to-end Avalon-MM writes into the audio
  registers, Avalon-ST handshake.

**Host tests**

- `sw/test/test_game.c` — gains nine new assertions (one per in-game
  cue, plus combined and accumulation variants). Total: 145 passing
  assertions, up from 136.
- `sw/test/test_audio.c` — standalone on-board smoke test:
  `./test_audio` plays BGM for 3 s; `./test_audio --sfx N` triggers one
  SFX cue.

## 6. Known issues

See `doc/v4-known-issues.md` for the full list. Remaining items:

- **ARM kernel-module cross-compile** (minor): workstation used for this
  branch does not have `arm-linux-gnueabihf-gcc` on PATH. Deferred to a
  Columbia workstation with the cross-toolchain; `pvz_audio_driver.ko`
  host-side build passes syntax but has no cross-compile artefact yet.
- **On-board deployment smoke test** (minor): DE1-SoC board not
  accessible this session. Audio path has been verified in simulation
  and full Quartus compile succeeds, but a live WM8731 listening test
  has not been run.
- **`sfx_activeN` STATUS heuristic**: the STATUS flags derive
  `sfxN_active = (sfxN_s != 0)`. With `sfx_rom.mem` all zero (Scope A),
  these flags always read 0. A future fix is to expose `playing` from
  `voice_sfx` as a dedicated output.

## Scope A vs Scope C

- **Scope A (shipped):** BGM loops whenever the game is in
  `STATE_PLAYING`. SFX event path is wired end-to-end (game hooks → main
  fan-out → ioctl → driver → register → voice → mixer); `sfx_rom.mem`
  is zero-filled, so cues execute silently.
- **Scope C (future, asset-only):** Drop real 8 kHz / 16-bit / mono WAVs
  into `hw/audio/assets/sfx_*.wav`, run `make audio`, commit the
  regenerated `sfx_rom.mem` + `sfx_offsets.svh`. No hardware, driver, or
  game-logic changes needed — every instrumentation point is already
  live and tested under Scope A.

## Build impact

- **Hardware:** resynthesis required (`make qsys && make quartus &&
  quartus_cpf -c output_files/soc_system.sof output_files/soc_system.rbf`,
  then `sopc2dts` + `dtc` for the DTB). The `pvz_audio` peripheral
  adds one Avalon-MM slave and two Avalon-ST sources under the existing
  LWH2F bridge (no new bridge). Block memory utilisation rises to 78 %
  (3 168 256 / 4 065 280 bits), well under the 85 % safety limit.
- **Software:** `sw/Makefile` updates are backward-compatible with the
  existing host build and ARM cross-compile flow. Existing tests keep
  passing.
- **Kernel ABI:** new misc device `/dev/pvz_audio` and three ioctls
  (`PVZ_AUDIO_BGM_CTRL`, `PVZ_AUDIO_PLAY_SFX`, `PVZ_AUDIO_STATUS`). The
  existing `/dev/pvz` device is unchanged.
