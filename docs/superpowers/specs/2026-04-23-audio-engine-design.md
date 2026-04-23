# Audio Engine — Design Document

- **Date:** 2026-04-23
- **Feature branch:** `audio-engine` (off `v3-controller-and-roster`)
- **Status:** Draft — awaiting user review before implementation plan.

---

## 1. Overview

Add a PCM audio playback engine to the PvZ FPGA project. **Scope A** (what this
feature ships) delivers a looping PvZ theme song out the DE1-SoC's 3.5 mm jack,
gated on `STATE_PLAYING`. **Scope C** (a future asset-only follow-up) layers 8
sound-effect cues over the theme using two mixing voices, without any hardware
or game-logic change. Nothing between Scope A and Scope C touches Verilog, the
Linux driver, or `game.c` — the transition is purely *drop in WAV files, re-run
the generator, commit*.

The design uses:

- **PCM in on-chip BRAM**, not MIDI synthesis. A MIDI / chiptune synth path was
  considered and rejected in the brainstorm: it would be a new ~2-week hardware
  module built from scratch, it couldn't produce the zombie-groan class of
  effects (requiring a PCM path alongside anyway), and it carries non-trivial
  "compiles but sounds bad" risk. PCM is a strictly smaller novel-hardware
  surface at the cost of a few hundred KB of BRAM.
- **The Intel audio IP stack** (`altera_up_avalon_audio_pll`,
  `altera_up_avalon_audio_and_video_config`,
  `altera_up_avalon_audio`) to handle WM8731 codec I²C configuration and I²S
  streaming. This matches the bubble-bobble reference team's approach and
  eliminates the clock-domain crossing as a custom concern.
- **Two SFX voices** with driver-side round-robin slot allocation. PvZ's normal
  gameplay loop produces pea-fire, pea-hit, and zombie-bite events that
  overlap constantly; one voice would audibly clip. Four is overkill.
- **A second Avalon-slave peripheral** (`pvz_audio`) alongside the existing
  `pvz_gpu`, not a submodule. Keeps VGA timing closure insulated from audio
  changes and matches the block diagram in the design document.

## 2. Goals and non-goals

### Goals (Scope A — ships in this feature)

- Looping PvZ theme song audible on the 3.5 mm jack when in `STATE_PLAYING`,
  silent on all other states.
- Full end-to-end SFX path wired and exercised: ioctl → driver → register →
  voice module → mixer → codec. `sfx_rom.mem` is populated with zero samples,
  so SFX ioctls succeed but nothing is audible yet.
- Every planned SFX cue hook point fired correctly from game logic, provable
  by `test_game.c` assertions.
- Design document audio section updated from "(Planned)" to a real spec that
  matches what actually shipped.

### Goals (Scope C — follow-up, zero hardware/software delta)

- `hw/audio/assets/` populated with 8 real SFX WAVs.
- Generator regenerates `sfx_rom.mem` + `sfx_offsets.svh`.
- Audible SFX during gameplay with two-voice overlap and driver round-robin.

### Non-goals

- No MIDI synthesis or FPGA chiptune engine.
- No volume / gain / master-mute registers.
- No ducking (BGM attenuating during SFX) or fade-in/out.
- No multiple BGM tracks (menu / win / lose loops).
- No SDRAM-backed audio storage. BRAM is sufficient at 8 kHz / 16-bit.

## 3. Architecture

A new Avalon-slave peripheral `pvz_audio` lives behind the same HPS-to-FPGA
lightweight bridge as `pvz_gpu`, at a separate base address (`0xff210000`,
span 16 bytes = 4 × 32-bit words).

```
                     ┌───────────────────────────────────────────────┐
Avalon-MM slave ──►  │ audio_regs (4 regs, WORDS-addressed)          │
(from HPS)           │   ├─ bgm_play  ─► voice_bgm  ──┐              │
                     │   ├─ sfx0_trig ─► voice_sfx[0] ├─► mixer ───► │──► avalon_streaming
                     │   └─ sfx1_trig ─► voice_sfx[1] ┘              │    (16-bit L/R, ready-handshake)
                     └───────────────────────────────────────────────┘        │
                                                                              ▼
                                                                altera_up_avalon_audio
                                                                              │
                                                              audio_and_video_config (I²C to WM8731)
                                                                              │
                                                                              ▼
                                                                  WM8731 codec → 3.5 mm jack
```

### Modules we author

| File | Purpose |
|---|---|
| `hw/pvz_audio.sv` | Top + `audio_regs` register file |
| `hw/voice_bgm.sv` | BGM address counter + looping FSM |
| `hw/voice_sfx.sv` | SFX cue-id → address-range lookup + one-shot FSM (×2 instances) |
| `hw/mixer.sv` | 3-input signed sum + clamp |
| `hw/bgm_rom.sv` | Synchronous BRAM wrapper, loaded from `bgm_rom.mem` |
| `hw/sfx_rom.sv` | Synchronous BRAM wrapper, loaded from `sfx_rom.mem` |
| `hw/pvz_audio_hw.tcl` | Qsys component descriptor |
| `sw/pvz_audio_driver.c/h` | Linux misc-device driver, exposes `/dev/pvz_audio` |
| `sw/audio.c/h` | Userspace wrapper (`audio_bgm_start/stop()`, `audio_play_sfx()`) |

### Modules from Intel IP

- `altera_up_avalon_audio` — streaming sink + FIFO + I²S serializer.
- `altera_up_avalon_audio_and_video_config` — I²C codec configuration.
- `altera_up_avalon_audio_pll` — audio-domain clock generation.

### Clocking

Fabric runs on the existing 50 MHz main clock. The audio IP handles the CDC to
the audio-PLL domain via its `ready` handshake on the streaming interface —
same pattern bubble-bobble proved works, no custom CDC required. Voice and
mixer modules emit a new sample only on the rising edge of a `ready` pulse
(8 kHz at 50 MHz = 6250 cycles per sample, trivially enough for sequential
ROM reads).

### Scope A ↔ Scope C boundary

Scope A ships the hardware, the driver, and the userspace wrappers at full
Scope C completeness. The SFX trigger lines are wired to the `audio_regs`
trigger registers; the voice modules are live; the mixer sums all three
voices. The only thing that makes Scope A silent on SFX is **`sfx_rom.mem`
contains all zeros**, which `voice_sfx` happily reads and emits as silence.

Therefore the Scope A → Scope C delta is:

1. Drop 8 real WAVs into `hw/audio/assets/`.
2. Run `make audio` in `hw/`.
3. Commit the regenerated `sfx_rom.mem` + `sfx_offsets.svh`.

No Verilog edits, no driver edits, no `game.c` edits, no re-synthesis of the
Quartus project's logic placement (only a BRAM `.mem` reload — a much faster
incremental build in practice).

## 4. FPGA module details

### Sample pacing

`altera_up_avalon_audio` asserts `left_chan_ready` / `right_chan_ready` once
per codec sample period. Voices + mixer advance their address counters and
emit a new sample only on the rising edge of the combined ready signal.
Codec is configured for **8 kHz / 16-bit / stereo** (mono content duplicated
to both channels).

### `voice_bgm.sv`

Two-state FSM (IDLE, PLAYING) plus one counter.

- Inputs: `clk`, `rst`, `play`, `sample_tick` (edge-detected ready).
- Output: signed 16-bit `sample`.
- State: `addr` in `[0, BGM_LEN-1]`.
- On `sample_tick` with `play==1`: emit `bgm_rom[addr]`,
  `addr ← (addr == BGM_LEN-1) ? 0 : addr + 1` (free-running loop).
- On `play==0`: `addr ← 0`, output `16'sd0` (restart-from-zero on next
  enable — intentional, matches "theme restarts per round" semantics).
- BRAM's 1-cycle read latency is absorbed by registering the address one
  tick ahead, same pattern as `shape_renderer`'s pending-pixel buffer.

### `voice_sfx.sv`

Two-state FSM (IDLE, PLAYING), instantiated twice.

- Cue-id → address lookup is a SystemVerilog `parameter` array of 8 entries ×
  2 × 17-bit (start, end), indexed 1..8. Baked in at synthesis from
  `sfx_offsets.svh`, which is generated from `manifest.json`.
- Trigger interface: `trig_valid` pulse + 4-bit `cue_id`.
- On trigger with `cue_id ∈ 1..8`: `addr ← sfx_start[cue_id]`,
  `stop_addr ← sfx_end[cue_id]`, state → PLAYING — even if already playing
  a different cue (retrigger semantics: the newer cue cancels the older
  one in the same voice).
- On trigger with `cue_id == 0`: state → IDLE immediately (matches the
  register map's "0 = stop" semantic). The SFX_START / SFX_END parameter
  tables are not indexed in this path, so their 1..8 indexing is safe.
- Any `cue_id ∈ 9..15` is treated as a no-op (ignore the trigger); these
  values are reserved. A testbench assertion covers this.
- On `sample_tick` while PLAYING: emit `sfx_rom[addr]`; if
  `addr >= stop_addr` → IDLE; else `addr += 1`.
- IDLE output is `16'sd0`, so the mixer sees silence for inactive voices.

### Voice-slot allocation lives in the driver

Not in hardware, not in userspace. On `PVZ_AUDIO_PLAY_SFX`, the driver tracks
"which voice last started" and writes the other voice's trigger register
(round-robin). A third concurrent SFX steals the older slot. This keeps
`game.c`'s call sites trivially simple (`audio_play_sfx(SFX_PEA_FIRE)`) and
keeps hardware dumb.

### `mixer.sv`

Purely combinational. Input: three signed 16-bit samples. Output:
`clamp(bgm + sfx0 + sfx1, -32768, +32767)`. No gain / volume in v1.

### `bgm_rom.sv` / `sfx_rom.sv`

Altera synchronous BRAM wrappers, 16-bit wide, initialized from `.mem` files
via `$readmemh` — same inference style as the existing `sprite_rom.sv` /
`peas_idx.mem`.

- BGM ROM: 120K words ≈ 1.92 Mbit (15 s at 8 kHz).
- SFX ROM: bounded at ~800 Kbit, size determined by manifest packing.

Separate BRAMs rather than one combined — BGM and SFX have different access
patterns (BGM is game-state-driven, SFX is event-driven), and separation
avoids any round-robin read contention even though at 8 kHz we'd technically
have thousands of cycles per sample to sequence reads.

### Deferred to Scope C or later

Clicks on BGM start/stop at non-zero amplitude. Simple fix is a 10 ms linear
fade-in/out in `voice_bgm`. Zero interface impact; add if the unfaded
transition sounds bad on the board.

## 5. Avalon register map and driver

### Register map

4 × 32-bit words, `addressUnits WORDS`, 32-bit writedata. Matches the
existing `pvz_gpu` convention (not bubble-bobble's 8-bit).

| N | CPU byte offset | Name              | Dir | Bits    | Meaning |
|---|-----------------|-------------------|-----|---------|---------|
| 0 | `0x00`          | `BGM_CTRL`        | W   | `[0]`   | 1 = BGM playing, 0 = stopped (restart from addr 0 on next 1) |
| 1 | `0x04`          | `SFX_VOICE0_TRIG` | W   | `[3:0]` | Cue ID 1–8; write pulses the voice's `trig_valid`; 0 = stop |
| 2 | `0x08`          | `SFX_VOICE1_TRIG` | W   | `[3:0]` | Same, for voice 1 |
| 3 | `0x0C`          | `STATUS`          | R   | `[2:0]` | `{sfx1_active, sfx0_active, bgm_active}` readback |

Upper bits of every register are reserved-zero.

Trigger-register semantics: a 1-cycle `trig_valid` pulse is generated inside
`audio_regs` on every write to register 1 or 2, regardless of whether the
cue ID changed. Software can retrigger on every fire without toggling
through 0.

### Compatible-string tie (the "classic debugging sink")

Three places must agree character-for-character, per the CLAUDE.md invariant
that the v3 branch reinforces:

- `hw/pvz_audio_hw.tcl`:
  `set_module_assignment embeddedsw.dts.compatible "csee4840,pvz_audio-1.0"`
- Generated `soc_system.dtb` (automatic via `make dtb`) contains that string.
- `sw/pvz_audio_driver.c`: `.compatible = "csee4840,pvz_audio-1.0"` in the
  `of_device_id` table.

Base address `0xff210000` on the lightweight HPS-to-FPGA bridge, separate
from `pvz_gpu`'s `0xff200000`. Span 16 bytes.

### Linux driver

`sw/pvz_audio_driver.c`, misc-device at `/dev/pvz_audio`, structural mirror
of `sw/pvz_driver.c`.

```c
#define PVZ_AUDIO_MAGIC      'A'
#define PVZ_AUDIO_BGM_CTRL   _IOW(PVZ_AUDIO_MAGIC, 1, uint32_t)  /* arg: 0 stop / 1 play */
#define PVZ_AUDIO_PLAY_SFX   _IOW(PVZ_AUDIO_MAGIC, 2, uint32_t)  /* arg: cue_id 1..8     */
#define PVZ_AUDIO_STATUS     _IOR(PVZ_AUDIO_MAGIC, 3, uint32_t)  /* readback             */
```

Driver holds internal state `last_voice_used ∈ {0, 1}`; each
`PLAY_SFX` ioctl writes the opposite voice's trigger register and flips the
tracker.

### Userspace wrapper

`sw/audio.c` + `sw/audio.h`, paralleling `render.c` / `render.h`. Opens
`/dev/pvz_audio` once at startup, exposes:

```c
int  audio_init(void);
void audio_bgm_start(void);
void audio_bgm_stop(void);
void audio_play_sfx(pvz_sfx_cue_t cue);
void audio_close(void);
```

The enum `pvz_sfx_cue_t` lives in `pvz.h` so `game.c` can reference cues
without pulling in ioctl headers.

## 6. Game-state integration

### BGM lifecycle — state-driven, edge-detected in `main.c`

The rule mirrors `render.c`'s existing `state != STATE_PLAYING` discipline:

```c
static pvz_state_t last_state = STATE_MENU;  /* or whatever the boot state is */

/* after each game_step() */
if (gs->state != last_state) {
    if (gs->state == STATE_PLAYING)       audio_bgm_start();
    else if (last_state == STATE_PLAYING) audio_bgm_stop();

    if (last_state == STATE_PLAYING && gs->state == STATE_LOSE)
        audio_play_sfx(PVZ_SFX_GAME_OVER);
    if (last_state == STATE_PLAYING && gs->state == STATE_WIN)
        audio_play_sfx(PVZ_SFX_VICTORY);

    last_state = gs->state;
}
```

Entering `STATE_PLAYING` restarts BGM from sample 0 (not resume mid-loop) —
this is intentional and falls out of `voice_bgm`'s `play==0` reset semantics.

### SFX events — emitted by `game.c` as data, fired by `main.c` as ioctls

`game.c` stays I/O-free (keeps `test_game.c` running on the host with no
driver dependency). `game_step()` populates a bitmask of events that
happened during the step; `main.c` consumes the bitmask after the step and
issues one `audio_play_sfx()` per set bit.

```c
/* in pvz.h, shared by game.c, main.c, test_game.c */
typedef enum {
    PVZ_SFX_PEA_FIRE     = 1,
    PVZ_SFX_PEA_HIT      = 2,
    PVZ_SFX_ZOMBIE_BITE  = 3,
    PVZ_SFX_ZOMBIE_DEATH = 4,
    PVZ_SFX_PLANT_PLACE  = 5,
    PVZ_SFX_WAVE_START   = 6,
    PVZ_SFX_GAME_OVER    = 7,
    PVZ_SFX_VICTORY      = 8,
} pvz_sfx_cue_t;

/* in game.h */
typedef struct { uint32_t flags; } audio_events_t;
#define AUDIO_EVENT(cue) (1u << ((cue) - 1))
```

`game_step()` takes `audio_events_t *out` — caller clears before the call
(matches how `render_frame`'s output buffer is caller-owned).

### Event emission points

| Cue             | Emitted by | Location |
|-----------------|------------|----------|
| `PEA_FIRE`      | `game.c`   | inside the peashooter fire tick, next to projectile spawn |
| `PEA_HIT`       | `game.c`   | inside the pea×zombie collision handler, before the pea slot is freed |
| `ZOMBIE_BITE`   | `game.c`   | inside the zombie-eats-plant per-bite interval — NOT per-tick damage |
| `ZOMBIE_DEATH`  | `game.c`   | where a zombie's HP reaches ≤ 0, before the slot is freed |
| `PLANT_PLACE`   | `game.c`   | inside `place_plant()` success path |
| `WAVE_START`    | `game.c`   | inside the wave-manager when the next wave is dispatched |
| `GAME_OVER`     | `main.c`   | on `STATE_PLAYING → STATE_LOSE` transition |
| `VICTORY`       | `main.c`   | on `STATE_PLAYING → STATE_WIN` transition |

`GAME_OVER` / `VICTORY` come from `main.c` because `main.c` already tracks
state transitions for BGM — no reason to duplicate that edge detection in
`game.c`.

### What Scope A ships on the software side

- `game.c` populates the audio-events bitmask **fully** (all 6 in-game cues).
- `main.c` wires BGM start/stop **and** `audio_play_sfx()` for every bit.
- SFX calls succeed end-to-end — they hit the driver, write the trigger
  register, the voice module plays the cue — but `sfx_rom.mem` is entirely
  zeros, so they're silent.

This is load-bearing for the Scope A → Scope C story: by the time Scope A
ships, the full SFX path has been exercised once per cue. If Scope C gets
de-scoped for time, we lose zero work.

## 7. Asset pipeline

### Directory layout (new, under `hw/`)

```
hw/
  audio/
    assets/
      bgm_theme.wav              # 8 kHz 16-bit mono PCM
      sfx_pea_fire.wav           # same format
      sfx_pea_hit.wav
      sfx_zombie_bite.wav
      sfx_zombie_death.wav
      sfx_plant_place.wav
      sfx_wave_start.wav
      sfx_game_over.wav
      sfx_victory.wav
    manifest.json
    build_audio_mif.py           # Python 3, stdlib only
    README.md
  bgm_rom.mem                    # GENERATED, committed (parallels peas_idx.mem)
  sfx_rom.mem                    # GENERATED, committed
  sfx_offsets.svh                # GENERATED, committed (parameter table)
```

### File format

`.mem`, not `.mif`. Matches `hw/peas_idx.mem` and loads via `$readmemh` in
the existing sprite_rom pattern. One 16-bit hex sample per line.

### Assets are pre-processed externally

Contributors convert source audio to 8 kHz 16-bit mono WAV using sox /
ffmpeg / Audacity **before** dropping files in `hw/audio/assets/`. The
generator does not resample or downmix — it verifies format and refuses
with a clear error otherwise. This keeps the script stdlib-only (Python
`wave` + `struct`, no scipy / numpy / sox dependency) and the build
deterministic regardless of which tools each Columbia workstation has.
`hw/audio/README.md` documents the one-liner:

```
sox input.mp3 -r 8000 -c 1 -b 16 output.wav
```

### Generator behavior

`python3 audio/build_audio_mif.py` reads `manifest.json`, loads each WAV,
verifies format, emits `bgm_rom.mem` + `sfx_rom.mem` + `sfx_offsets.svh`.

If a SFX WAV is missing, the script fills its cue slot with `silent_samples`
zeros (default 2400 = 0.3 s) rather than failing. This is what makes Scope A
→ Scope C an asset-only change.

### Generated `sfx_offsets.svh`

```systemverilog
/* AUTOGENERATED from hw/audio/manifest.json by build_audio_mif.py */
parameter logic [16:0] SFX_START [1:8] = '{17'd0, 17'd2400, 17'd4800, ...};
parameter logic [16:0] SFX_END   [1:8] = '{17'd2399, 17'd4799, 17'd7199, ...};
```

`voice_sfx.sv` ``` `include ``` s this file; regenerating the ROM and the
offsets is always one script run, can never desync.

### Build integration

The generator is a contributor tool, not part of the Quartus flow — same
pattern as `peas_idx.mem`. A `make audio` convenience target in
`hw/Makefile` runs the generator. `hw/audio/README.md`: *"change a WAV →
run `make audio` → commit the three generated files alongside the WAV."*

### Open: BGM asset sourcing

Explicitly not pinned by this spec. The `bgm_theme.wav` can be an online
arrangement, an in-DAW composition, or a MIDI-rendered chiptune. The
feature branch ships with either a real arrangement or a placeholder
tone-sweep while real asset work proceeds in parallel.

## 8. Testing plan

### ModelSim testbenches (self-checking, match existing `tb_*.sv` convention)

- `hw/tb/tb_voice_bgm.sv` — load 16-sample ramp fixture, pulse `sample_tick`
  32 times with `play=1`, verify sample order, wrap at 16, reset on `play=0`.
- `hw/tb/tb_voice_sfx.sv` — load 3-clip fixture, verify cue start/end, mid-
  clip retrigger, cross-cue retrigger, cue 0 = stop semantic.
- `hw/tb/tb_mixer.sv` — drive signed extremes, verify clamp at `±32767`,
  pass-through of in-range sums. Combinational, no clock.
- `hw/tb/tb_pvz_audio.sv` — end-to-end mirror of `tb_pvz_top.sv`: Avalon
  writes to the 3 trigger registers, synthetic ready pulses into the
  streaming source, check `sample_data_l` / `sample_data_r` match expected.
  This is the one that catches the Avalon-WORDS-vs-bytes trap.

### Host-side tests (`sw/test/test_game.c` additions)

Mechanical because events are plain data on the `game_state` side:

- `test_pea_fire_emits_event` — peashooter placed, step past fire interval,
  assert `PEA_FIRE` bit set.
- `test_pea_hit_emits_event` — pea+zombie in overlap, one step, assert
  `PEA_HIT` bit set.
- `test_zombie_bite_emits_event_once_per_bite_interval` — eating zombie on
  plant, step past bite interval N times, assert exactly N bite events
  (NOT per-tick).
- `test_zombie_death_emits_event` — zombie at 1 HP, pea hits, assert both
  `PEA_HIT` and `ZOMBIE_DEATH` set that tick.
- `test_plant_place_emits_event` — `place_plant()` success path, assert
  `PLANT_PLACE` bit.
- `test_wave_start_emits_event` — advance wave timer past threshold, assert
  `WAVE_START` bit.
- `test_audio_events_cleared_by_caller` — populate bitmask manually, run
  `game_step()`, assert old bits cleared before new bits set.

`GAME_OVER` / `VICTORY` aren't in `test_game.c` (they fire in `main.c` on
state transitions) — best tested on-board.

### On-board verification (Scope A)

1. `insmod pvz_driver.ko pvz_audio_driver.ko` → `dmesg` shows both probes
   succeeding, `/dev/pvz` and `/dev/pvz_audio` exist, `/proc/iomem` shows
   both `ff20xxxx` and `ff21xxxx` regions claimed.
2. `./pvz` → BGM theme audible on speaker within ~1 s of reaching
   `STATE_PLAYING`.
3. Intentionally lose → BGM stops the same frame `STATE_LOSE` is entered.
4. Intentionally win → BGM stops on `STATE_WIN`.
5. Restart → BGM starts again from sample 0 (verify by listening that it's
   the theme's intro, not the middle).

Plus a **standalone audio smoke-test** (`sw/test/test_audio.c`) that opens
`/dev/pvz_audio`, starts BGM, sleeps 3 s, stops, and exits — useful for
debugging audio without running the full game. Add to `sw/Makefile`.

### On-board verification (Scope C, future)

Fire each SFX cue via `test_audio --sfx CUE_ID`, confirm audible; play the
full game and confirm pea-fire + pea-hit overlap correctly (the driver's
voice round-robin working → no dropped sounds).

## 9. Scope A ship list

### New files

- `hw/pvz_audio.sv`, `voice_bgm.sv`, `voice_sfx.sv`, `mixer.sv`,
  `bgm_rom.sv`, `sfx_rom.sv`
- `hw/pvz_audio_hw.tcl`
- `hw/bgm_rom.mem`, `sfx_rom.mem`, `sfx_offsets.svh` (generated, committed)
- `hw/audio/assets/bgm_theme.wav` + 8 sfx stubs (stubs absent in Scope A →
  silent ROM)
- `hw/audio/manifest.json`, `build_audio_mif.py`, `README.md`
- `hw/tb/tb_voice_bgm.sv`, `tb_voice_sfx.sv`, `tb_mixer.sv`, `tb_pvz_audio.sv`
  + small fixture `.mem` files
- `sw/pvz_audio_driver.c`, `pvz_audio_driver.h`
- `sw/audio.c`, `audio.h`
- `sw/test/test_audio.c`

### Modified files

- `hw/soc_system.qsys` — add `pvz_audio`, `audio_0`, `audio_and_video_config_0`,
  `audio_pll_0` instances + wiring to WM8731 pins.
- `hw/soc_system_top.sv` — route WM8731 pins from the board into Qsys.
- `hw/soc_system.qsf` — pin assignments for the WM8731 (AUD_*, FPGA_I2C_*).
- `hw/Makefile` — add `make audio` target.
- `sw/Makefile` — build + install the new kernel module, compile `audio.c`,
  build `test_audio`.
- `sw/game.c`, `game.h` — `audio_events_t` output param on `game_step()`,
  event emission at the six in-game hook points.
- `sw/main.c` — state-edge BGM start/stop, SFX ioctl fan-out from the
  events bitmask, `last_state` tracker.
- `sw/pvz.h` — `pvz_sfx_cue_t` enum.
- `sw/test/test_game.c` — seven new assertions.
- `CLAUDE.md` — document the new second peripheral, the new compatible-string
  tie, the new base address, the event-bitmask contract, the
  `Scope A = silent SFX ROM` convention.
- `doc/design-document/design-document.tex` — update audio section from
  "(Planned)" to a real spec.

## 10. Success criteria

- All four ModelSim testbenches green:
  `vsim -do "run -all; quit" work.tb_voice_bgm`, `work.tb_voice_sfx`,
  `work.tb_mixer`, `work.tb_pvz_audio`.
- All seven new `test_game.c` assertions passing: `make test_game && ./test_game`.
- `make qsys && make quartus && make rbf && make dtb` succeeds; generated
  dtb contains a `pvz_audio@...` node.
- On-board: both driver probes succeed, `/dev/pvz` and `/dev/pvz_audio`
  present, `/proc/iomem` shows both regions claimed.
- **Audible:** `./pvz` on the board plays the PvZ theme on the 3.5 mm jack
  when in `STATE_PLAYING`, silent otherwise.
- `./test_audio` standalone: 3-second BGM play-then-stop works without the
  game running.
- Resource check: `make quartus` report shows total M10K utilization close
  to the design-document target of 80.2%. The 80.2% figure in
  `design-document.tex` Table "On-Chip SRAM Summary" already accounts for
  2.83 Mbit of audio wave ROM — this feature brings the implementation up
  to the planned budget, not beyond it. Hard ceiling: ≤ 85%.

## 11. Known risks

- **WM8731 I²C configuration.** The Intel `audio_and_video_config` IP may
  need specific sample-rate / word-length parameters to drive 8 kHz 16-bit
  stereo. Wrong parameters → garbage output. *Mitigation:* copy
  bubble-bobble's Qsys parameters verbatim as the starting point, only
  diverge if needed.
- **WM8731 pin assignments.** Pins (`AUD_*`, `FPGA_I2C_*`) are standard for
  the DE1-SoC but currently absent from `soc_system.qsf`. *Mitigation:*
  copy from the DE1-SoC reference pin file, verify against
  `doc/manual/DE1-SoC_User_manual.md`.
- **BGM asset sourcing.** No concrete source identified yet. Could block
  Scope A ship if the chosen arrangement can't be produced. *Mitigation:*
  ship Scope A with a placeholder tone-sweep WAV while real asset work
  proceeds in parallel.
- **Total resource utilization.** The design doc plans 80.2% total M10K
  usage including audio. If actual synthesis lands meaningfully above
  that — e.g. due to BRAM packing inefficiencies for the 120K-word BGM
  ROM — routing could get tight. *Mitigation:* first lever is dropping
  sample rate to 4 kHz (halves footprint); second lever is shortening the
  BGM loop duration. Both preserve the interface unchanged.

## 12. Implementation workflow

### Feature worktree

All work happens in `/homes/user/stud/spring26/cy2822/4840-final/worktrees/audio-engine`,
branch `audio-engine`, branched from `v3-controller-and-roster`. Primary
`pvz-fpga` tree is not touched.

### Subagent decomposition

The implementation is split into six independent chunks. Each is dispatched
to a subagent running in its own isolated worktree (Agent tool with
`isolation: "worktree"`). The parent session collects and merges:

| # | Chunk | Depends on | Runs in |
|---|-------|------------|---------|
| 1 | HW modules — `voice_bgm.sv`, `voice_sfx.sv`, `mixer.sv`, `bgm_rom.sv`, `sfx_rom.sv`, 4 testbenches + fixture `.mem` files | — | isolated worktree |
| 2 | Qsys integration — `pvz_audio.sv` top, `pvz_audio_hw.tcl`, `soc_system.qsys` + `soc_system_top.sv` + `.qsf` edits, audio_pll + audio_and_video_config + audio_0 instances | Chunk 1 | isolated worktree |
| 3 | Linux driver — `pvz_audio_driver.c/h` | — | isolated worktree |
| 4 | Userspace integration — `audio.c/h`, `pvz.h` enum, `game.c` event emission, `main.c` state-edge BGM + ioctl fan-out, `test_game.c` assertions | — | isolated worktree |
| 5 | Asset pipeline — `manifest.json`, `build_audio_mif.py`, `README.md`, placeholder `bgm_theme.wav`, generated `bgm_rom.mem` + `sfx_rom.mem` + `sfx_offsets.svh` | — | isolated worktree |
| 6 | Docs — `CLAUDE.md`, `design-document.tex` audio section rewrite, `doc/v4-changes.md` summary | Chunks 1–5 landed | isolated worktree |

Chunks 1, 3, 4, 5 dispatch **in parallel** (one message, four `Agent`
tool calls). Chunk 2 runs after 1. Chunk 6 runs last.

### Commit convention

Single short title line. No body, no description, no Co-Authored-By
trailer. Prefix `audio:`. Suggested sequence on the feature branch:

- `audio: voice and mixer modules + testbenches`
- `audio: pvz_audio peripheral + qsys integration`
- `audio: linux driver`
- `audio: userspace integration and event bus`
- `audio: asset pipeline and placeholder bgm`
- `audio: documentation`

### Test-before-ship gate

Before the final docs commit (Chunk 6), the parent session runs the full
relevant suite and confirms green:

- All `tb_*.sv` in `hw/tb/` via `vsim` (every testbench, not just the new
  ones — catches whole-system interaction bugs per the v3 testbench ledger
  convention).
- `make test_game && ./test_game` in `sw/`.
- `make qsys && make quartus && make rbf && make dtb` in `hw/`.
- `make` in `sw/` (host build) — cross-compile path is optional unless
  the board is available.
- If hardware is available: on-board smoke test. If not: log under
  `doc/v4-known-issues.md` with severity + proposed fix, per the v3
  `doc/v3-known-issues.md` convention.
