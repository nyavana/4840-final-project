# Audio Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a PCM audio engine to the PvZ FPGA project that plays a looping PvZ theme song over the 3.5 mm jack during gameplay, with a full SFX mixing path wired end-to-end but silent until audio assets are dropped in.

**Architecture:** A second Avalon-slave peripheral `pvz_audio` alongside the existing `pvz_gpu`, at base `0xff210000`. Inside: three voice modules (1 BGM, 2 SFX) feeding a 3-input clamp mixer. Storage is on-chip BRAM loaded from `.mem` files. The Intel `altera_up_avalon_audio*` IP stack handles WM8731 codec I²C config and I²S streaming.

**Tech Stack:** SystemVerilog (Quartus 21+), ModelSim, Platform Designer (Qsys), Intel FPGA IPs, C (Linux misc-device driver + userspace), Python 3 stdlib (asset generator), GNU Make.

**Source spec:** `docs/superpowers/specs/2026-04-23-audio-engine-design.md` in this branch.

**Worktree:** `/homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine` (branch `v3-audio-engine`, off `v3-controller-and-roster`). Already exists, spec committed at HEAD.

**Commit convention:** Single short title, `audio:` prefix. **No body, no Co-Authored-By trailer.** One commit per task (numbered 1.1 through 7.1 below).

**Subagent convention:** Each task below is sized for a single subagent working in an isolated worktree (Agent tool with `isolation: "worktree"`). Tasks within a chunk may share a subagent when the files are tightly coupled (noted where applicable). Independent chunks (1, 3, 4, 5) can run in parallel.

---

## Decomposition and chunk dependencies

| Chunk | Tasks | Depends on | Can run in parallel with |
|-------|-------|------------|--------------------------|
| 1. HW modules | 1.1 – 1.5 | — | 3, 4, 5 |
| 2. Qsys integration | 2.1 – 2.6 | Chunk 1 | 3, 4, 5 (post-1) |
| 3. Linux driver | 3.1 – 3.3 | — | 1, 4, 5 |
| 4. Userspace integration | 4.1 – 4.8 | — (contract from driver header is in-spec) | 1, 3, 5 |
| 5. Asset pipeline | 5.1 – 5.5 | — | 1, 3, 4 |
| 6. Docs | 6.1 – 6.3 | 1–5 all landed | — |
| 7. Test-before-ship + push | 7.1 – 7.3 | 1–6 all landed | — |

---

## File structure

### Files created in this plan

```
hw/
  mixer.sv
  voice_bgm.sv
  voice_sfx.sv
  bgm_rom.sv
  sfx_rom.sv
  pvz_audio.sv
  pvz_audio_hw.tcl
  bgm_rom.mem                   # generated, committed
  sfx_rom.mem                   # generated, committed
  sfx_offsets.svh               # generated, committed
  audio/
    manifest.json
    build_audio_mif.py
    README.md
    assets/
      bgm_theme.wav             # placeholder tone sweep in scope A
  tb/
    tb_mixer.sv
    tb_voice_bgm.sv
    tb_voice_sfx.sv
    tb_pvz_audio.sv
    fixtures/
      bgm_test.mem
      sfx_test.mem
      sfx_offsets_test.svh

sw/
  pvz_audio_driver.c
  pvz_audio_driver.h
  audio.c
  audio.h
  test/
    test_audio.c

doc/
  v4-changes.md
  v4-known-issues.md            # created empty at first, populated if board unavailable
```

### Files modified

```
hw/
  soc_system.qsys               # +pvz_audio, audio_0, audio_and_video_config_0, audio_pll_0
  soc_system_top.sv             # route WM8731 pins from conduit to board
  soc_system.qsf                # WM8731 pin assignments
  Makefile                      # +audio target
sw/
  Makefile                      # +pvz_audio_driver module, +audio.c link, +test_audio target
  game.c                        # audio_events_t output param + 6 event emission points
  game.h                        # audio_events_t struct, new game_step signature
  main.c                        # state-edge BGM tracker, SFX ioctl fan-out
  pvz.h                         # pvz_sfx_cue_t enum
  test/test_game.c              # 7 new assertions
CLAUDE.md                       # second peripheral, new compatible tie, event bus, scope-A convention
doc/design-document/design-document.tex  # audio section rewrite, "(Planned)" → actual spec
```

---

## Pre-flight checks (run once before starting chunk 1)

- [ ] **Confirm worktree state.** Run:
  ```bash
  git -C /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine status --short
  git -C /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine log --oneline -2
  ```
  Expected: working tree clean; HEAD is `audio: design spec` on top of `v3: mark tasks.md push step complete`.

- [ ] **Confirm ModelSim + Quartus on PATH** (on a Columbia workstation, with `source embedded_command_shell.sh` already sourced):
  ```bash
  which vsim vlib vlog qsys-generate quartus_sh
  ```
  Expected: all five resolve. If not, the subagent must note the gap in `doc/v4-known-issues.md` per the v3 convention rather than silently skip simulation.

---

## Chunk 1 — Hardware modules + testbenches

### Task 1.1 — `mixer.sv` + testbench

**Files:**
- Create: `hw/mixer.sv`
- Create: `hw/tb/tb_mixer.sv`

**Dependency:** none.

- [ ] **Step 1: Write the failing testbench** at `hw/tb/tb_mixer.sv`.

```systemverilog
/*
 * Testbench for mixer — 3-input signed 16-bit adder with saturation clamp.
 */
`timescale 1ns/1ps

module tb_mixer;
    logic signed [15:0] bgm, sfx0, sfx1;
    logic signed [15:0] mixed;

    mixer dut(.*);

    integer errors = 0;

    task check(input signed [15:0] a, b, c, expected);
        bgm = a; sfx0 = b; sfx1 = c;
        #1;
        if (mixed !== expected) begin
            $display("FAIL: mix(%0d,%0d,%0d) = %0d, expected %0d", a, b, c, mixed, expected);
            errors++;
        end else begin
            $display("PASS: mix(%0d,%0d,%0d) = %0d", a, b, c, mixed);
        end
    endtask

    initial begin
        check(16'sd0,      16'sd0,      16'sd0,      16'sd0);           // all zero
        check(16'sd100,    16'sd200,    16'sd300,    16'sd600);         // small positive
        check(-16'sd500,   -16'sd500,   -16'sd500,   -16'sd1500);       // small negative
        check(16'sd30000,  16'sd20000,  16'sd0,      16'sd32767);       // positive clip
        check(-16'sd30000, -16'sd20000, 16'sd0,      -16'sd32768);      // negative clip
        check(16'sd32767,  16'sd32767,  16'sd32767,  16'sd32767);       // max clip
        check(-16'sd32768, -16'sd32768, -16'sd32768, -16'sd32768);      // min clip
        check(16'sd32767,  -16'sd100,   -16'sd100,   16'sd32567);       // near-max cancel

        if (errors == 0) $display("TEST PASSED: tb_mixer");
        else             $display("TEST FAILED: tb_mixer (%0d errors)", errors);
        $finish;
    end
endmodule
```

- [ ] **Step 2: Verify it fails** (module not defined).

  Run from `hw/`:
  ```bash
  cd /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine/hw
  vlib work && vlog tb/tb_mixer.sv
  ```
  Expected: compilation error, `Module 'mixer' is not defined` or similar.

- [ ] **Step 3: Write the module** at `hw/mixer.sv`.

```systemverilog
/*
 * 3-input signed adder with symmetric saturation clamp to int16 range.
 * Combinational. Used inside pvz_audio to mix BGM + 2 SFX voices.
 */
module mixer(
    input  logic signed [15:0] bgm,
    input  logic signed [15:0] sfx0,
    input  logic signed [15:0] sfx1,
    output logic signed [15:0] mixed
);
    logic signed [17:0] sum;  // 3 × 16-bit signed summed safely into 18 bits

    always_comb begin
        sum = $signed({{2{bgm [15]}},  bgm })
            + $signed({{2{sfx0[15]}},  sfx0})
            + $signed({{2{sfx1[15]}},  sfx1});
        if      (sum >  18'sd32767)  mixed = 16'sd32767;
        else if (sum < -18'sd32768)  mixed = -16'sd32768;
        else                         mixed = sum[15:0];
    end
endmodule
```

- [ ] **Step 4: Verify it passes.**

  ```bash
  vlib work && vlog mixer.sv tb/tb_mixer.sv
  vsim -do "run -all; quit" work.tb_mixer 2>&1 | tail -20
  ```
  Expected: `TEST PASSED: tb_mixer`, zero errors.

- [ ] **Step 5: Commit.**

  ```bash
  git add hw/mixer.sv hw/tb/tb_mixer.sv
  git commit -m "audio: mixer module and testbench"
  ```

---

### Task 1.2 — `bgm_rom.sv` + `sfx_rom.sv` (simple BRAM wrappers) + fixture `.mem` files

**Files:**
- Create: `hw/bgm_rom.sv`
- Create: `hw/sfx_rom.sv`
- Create: `hw/tb/fixtures/bgm_test.mem`
- Create: `hw/tb/fixtures/sfx_test.mem`
- Create: `hw/tb/fixtures/sfx_offsets_test.svh`

**Dependency:** none. No standalone testbench for these wrappers — they're exercised as part of the voice testbenches (Tasks 1.3 and 1.4). The fixtures are used by those testbenches.

- [ ] **Step 1: Write `hw/bgm_rom.sv`.**

```systemverilog
/*
 * BGM PCM ROM — 16-bit signed samples.
 *
 * Depth is parameterized; production build uses 131072 (2^17, enough for
 * 15 s @ 8 kHz = 120000 samples plus some headroom). Testbenches instantiate
 * with a small depth and a fixture .mem file.
 *
 * Read latency: 1 clock (inferred M10K block RAM), matching sprite_rom.sv.
 */
module bgm_rom #(
    parameter int DEPTH    = 131072,
    parameter int AW       = 17,
    parameter     MEM_FILE = "bgm_rom.mem"
)(
    input  logic                clk,
    input  logic [AW-1:0]       addr,
    output logic signed [15:0]  sample
);
    logic [15:0] rom [0:DEPTH-1];

    initial begin
        $readmemh(MEM_FILE, rom);
    end

    always_ff @(posedge clk)
        sample <= $signed(rom[addr]);
endmodule
```

- [ ] **Step 2: Write `hw/sfx_rom.sv`.**

```systemverilog
/*
 * SFX PCM ROM — 16-bit signed samples, all 8 SFX clips packed back-to-back.
 * Depth parameterized; production build uses 32768 (2^15).
 *
 * Read latency: 1 clock (inferred M10K block RAM).
 */
module sfx_rom #(
    parameter int DEPTH    = 32768,
    parameter int AW       = 15,
    parameter     MEM_FILE = "sfx_rom.mem"
)(
    input  logic                clk,
    input  logic [AW-1:0]       addr,
    output logic signed [15:0]  sample
);
    logic [15:0] rom [0:DEPTH-1];

    initial begin
        $readmemh(MEM_FILE, rom);
    end

    always_ff @(posedge clk)
        sample <= $signed(rom[addr]);
endmodule
```

- [ ] **Step 3: Write `hw/tb/fixtures/bgm_test.mem`** — 16-sample ramp for `tb_voice_bgm`.

```
0000
0001
0002
0003
0004
0005
0006
0007
0008
0009
000a
000b
000c
000d
000e
000f
```

- [ ] **Step 4: Write `hw/tb/fixtures/sfx_test.mem`** — three tiny fixture clips back-to-back.

  Clip 1 (offsets 0–3): short ramp `0000 0001 0002 0003`
  Clip 2 (offsets 4–7): short reverse ramp `0003 0002 0001 0000`
  Clip 3 (offsets 8–11): constant `000a 000a 000a 000a`

```
0000
0001
0002
0003
0003
0002
0001
0000
000a
000a
000a
000a
```

- [ ] **Step 5: Write `hw/tb/fixtures/sfx_offsets_test.svh`** — 3-entry fixture for `voice_sfx` instantiated in `tb_voice_sfx`.

```systemverilog
/* Test fixture offsets for tb_voice_sfx. 3 cues, indices 1..3; 4..8 unused. */
parameter logic [14:0] SFX_START [1:8] = '{
    15'd0, 15'd4, 15'd8, 15'd0, 15'd0, 15'd0, 15'd0, 15'd0
};
parameter logic [14:0] SFX_END   [1:8] = '{
    15'd3, 15'd7, 15'd11, 15'd0, 15'd0, 15'd0, 15'd0, 15'd0
};
```

- [ ] **Step 6: Smoke-check compilation.** These modules and fixtures have no standalone testbench, but they must compile. Run:
  ```bash
  cd /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine/hw
  vlib work && vlog bgm_rom.sv sfx_rom.sv
  ```
  Expected: clean compile, no errors.

- [ ] **Step 7: Commit.**

  ```bash
  git add hw/bgm_rom.sv hw/sfx_rom.sv hw/tb/fixtures/
  git commit -m "audio: BRAM wrappers and testbench fixtures"
  ```

---

### Task 1.3 — `voice_bgm.sv` + testbench

**Files:**
- Create: `hw/voice_bgm.sv`
- Create: `hw/tb/tb_voice_bgm.sv`

**Dependency:** Task 1.2 (uses `bgm_rom.sv` + `bgm_test.mem`).

- [ ] **Step 1: Write the failing testbench** at `hw/tb/tb_voice_bgm.sv`.

```systemverilog
/*
 * Testbench for voice_bgm.
 * Drives sample_tick pulses and verifies:
 *  - While play=1, addr walks 0..N-1 then wraps to 0.
 *  - While play=0, output is 0 and addr stays reset to 0.
 *  - play 0→1 restarts from addr 0 regardless of prior state.
 */
`timescale 1ns/1ps

module tb_voice_bgm;
    logic clk, rst, play, sample_tick;
    logic signed [15:0] sample;

    // 16-sample fixture ROM (bgm_test.mem), AW = 4 (depth 16)
    voice_bgm #(.DEPTH(16), .AW(4), .BGM_LEN(16), .MEM_FILE("tb/fixtures/bgm_test.mem")) dut(.*);

    initial clk = 0;
    always #5 clk = ~clk;  // 100 MHz for faster sim (functionality is tick-gated)

    integer errors = 0;

    task pulse_tick;
        sample_tick = 1;
        @(posedge clk);
        sample_tick = 0;
        @(posedge clk);
    endtask

    task expect_sample(input signed [15:0] want, input string label);
        // sample is registered output of ROM, available one cycle after addr settles
        @(posedge clk);
        if (sample !== want) begin
            $display("FAIL (%s): got %0d, want %0d", label, sample, want);
            errors++;
        end else begin
            $display("PASS (%s): got %0d", label, sample);
        end
    endtask

    initial begin
        rst = 1; play = 0; sample_tick = 0;
        repeat (4) @(posedge clk);
        rst = 0;
        @(posedge clk);

        // play=0: sample should be 0 regardless of ticks
        pulse_tick;
        expect_sample(16'sd0, "play=0 tick emits 0");

        // Enable play, step through all 16 samples, check wrap
        play = 1;
        @(posedge clk);
        for (int i = 0; i < 16; i++) begin
            pulse_tick;
            expect_sample(i[15:0], $sformatf("play tick %0d", i));
        end
        // 17th tick should wrap to sample 0
        pulse_tick;
        expect_sample(16'sd0, "wrap to 0");

        // Stop play, verify silence
        play = 0;
        pulse_tick;
        expect_sample(16'sd0, "stop restores silence");

        // Restart — should begin from 0 again (not resume)
        play = 1;
        @(posedge clk);
        pulse_tick;
        expect_sample(16'sd0, "restart from 0");
        pulse_tick;
        expect_sample(16'sd1, "play tick 1 after restart");

        if (errors == 0) $display("TEST PASSED: tb_voice_bgm");
        else             $display("TEST FAILED: tb_voice_bgm (%0d errors)", errors);
        $finish;
    end
endmodule
```

- [ ] **Step 2: Verify it fails.**

  ```bash
  cd /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine/hw
  vlib work && vlog bgm_rom.sv tb/tb_voice_bgm.sv
  ```
  Expected: `Module 'voice_bgm' is not defined`.

- [ ] **Step 3: Write the module** at `hw/voice_bgm.sv`.

```systemverilog
/*
 * voice_bgm — address counter + simple FSM for the background-music voice.
 *
 * On every sample_tick pulse while play=1, walks addr through [0, BGM_LEN-1]
 * and wraps. Output is the ROM sample at the CURRENT addr (not the one we
 * just advanced to), which means addr must be issued one cycle before the
 * sample becomes available at the mixer — BRAM read latency is 1 cycle.
 *
 * When play=0 the addr is held at 0 so the next enable starts from sample 0.
 * Output is forced to 0 whenever play=0 so the mixer doesn't hear stale data.
 */
module voice_bgm #(
    parameter int DEPTH     = 131072,
    parameter int AW        = 17,
    parameter int BGM_LEN   = 120000,
    parameter     MEM_FILE  = "bgm_rom.mem"
)(
    input  logic                clk,
    input  logic                rst,
    input  logic                play,
    input  logic                sample_tick,
    output logic signed [15:0]  sample
);
    logic [AW-1:0]       addr;
    logic signed [15:0]  rom_sample;

    bgm_rom #(.DEPTH(DEPTH), .AW(AW), .MEM_FILE(MEM_FILE)) rom_i(
        .clk(clk), .addr(addr), .sample(rom_sample)
    );

    always_ff @(posedge clk) begin
        if (rst) begin
            addr <= '0;
        end else if (!play) begin
            addr <= '0;
        end else if (sample_tick) begin
            if (addr == BGM_LEN - 1) addr <= '0;
            else                     addr <= addr + 1'b1;
        end
    end

    assign sample = play ? rom_sample : 16'sd0;
endmodule
```

- [ ] **Step 4: Verify it passes.**

  ```bash
  vlib work && vlog bgm_rom.sv voice_bgm.sv tb/tb_voice_bgm.sv
  vsim -do "run -all; quit" work.tb_voice_bgm 2>&1 | tail -40
  ```
  Expected: `TEST PASSED: tb_voice_bgm`, zero errors.

- [ ] **Step 5: Commit.**

  ```bash
  git add hw/voice_bgm.sv hw/tb/tb_voice_bgm.sv
  git commit -m "audio: voice_bgm module and testbench"
  ```

---

### Task 1.4 — `voice_sfx.sv` + testbench

**Files:**
- Create: `hw/voice_sfx.sv`
- Create: `hw/tb/tb_voice_sfx.sv`

**Dependency:** Task 1.2 (uses `sfx_rom.sv` + `sfx_test.mem` + `sfx_offsets_test.svh`).

- [ ] **Step 1: Write the failing testbench** at `hw/tb/tb_voice_sfx.sv`.

```systemverilog
/*
 * Testbench for voice_sfx.
 * Uses tb/fixtures/sfx_offsets_test.svh (3 cues in the offset table) and
 * tb/fixtures/sfx_test.mem (12 samples packed as 3 × 4-sample clips).
 */
`timescale 1ns/1ps

module tb_voice_sfx;
    logic clk, rst, sample_tick, trig_valid;
    logic [3:0] cue_id;
    logic signed [15:0] sample;

    voice_sfx #(
        .DEPTH(16), .AW(4),
        .MEM_FILE("tb/fixtures/sfx_test.mem"),
        .OFFSETS_FILE("tb/fixtures/sfx_offsets_test.svh")
    ) dut(.*);

    initial clk = 0;
    always #5 clk = ~clk;

    integer errors = 0;

    task pulse_tick;
        sample_tick = 1; @(posedge clk); sample_tick = 0; @(posedge clk);
    endtask

    task trigger(input [3:0] id);
        cue_id = id;
        trig_valid = 1;
        @(posedge clk);
        trig_valid = 0;
        @(posedge clk);
    endtask

    task expect_sample(input signed [15:0] want, input string label);
        @(posedge clk);
        if (sample !== want) begin
            $display("FAIL (%s): got %0d, want %0d", label, sample, want);
            errors++;
        end else begin
            $display("PASS (%s): got %0d", label, sample);
        end
    endtask

    initial begin
        rst = 1; trig_valid = 0; cue_id = 0; sample_tick = 0;
        repeat (4) @(posedge clk);
        rst = 0;
        @(posedge clk);

        // Idle: sample is 0
        pulse_tick;
        expect_sample(16'sd0, "idle");

        // Trigger cue 1 (offsets 0..3 = ramp 0..3)
        trigger(4'd1);
        pulse_tick;
        expect_sample(16'sd0, "cue1 sample 0");
        pulse_tick;
        expect_sample(16'sd1, "cue1 sample 1");
        pulse_tick;
        expect_sample(16'sd2, "cue1 sample 2");
        pulse_tick;
        expect_sample(16'sd3, "cue1 sample 3");
        // After stop_addr reached, voice returns to IDLE (silent)
        pulse_tick;
        expect_sample(16'sd0, "cue1 done -> idle");

        // Trigger cue 2 (offsets 4..7 = reverse ramp 3..0)
        trigger(4'd2);
        pulse_tick;
        expect_sample(16'sd3, "cue2 sample 0");
        pulse_tick;
        expect_sample(16'sd2, "cue2 sample 1");
        // Retrigger mid-playback: cue 3 (constant 10)
        trigger(4'd3);
        pulse_tick;
        expect_sample(16'sd10, "cue3 after retrigger");
        pulse_tick;
        expect_sample(16'sd10, "cue3 sample 1");

        // cue_id == 0 forces IDLE
        trigger(4'd0);
        pulse_tick;
        expect_sample(16'sd0, "cue 0 stops");

        // Out-of-range cue (5..8 have zero offsets; 9..15 reserved no-op)
        trigger(4'd9);
        pulse_tick;
        expect_sample(16'sd0, "cue 9 is no-op");

        if (errors == 0) $display("TEST PASSED: tb_voice_sfx");
        else             $display("TEST FAILED: tb_voice_sfx (%0d errors)", errors);
        $finish;
    end
endmodule
```

- [ ] **Step 2: Verify it fails.**

  ```bash
  cd /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine/hw
  vlib work && vlog sfx_rom.sv tb/tb_voice_sfx.sv
  ```
  Expected: `Module 'voice_sfx' is not defined`.

- [ ] **Step 3: Write the module** at `hw/voice_sfx.sv`.

```systemverilog
/*
 * voice_sfx — one SFX voice. Two states: IDLE, PLAYING.
 *
 * On trig_valid with cue_id ∈ 1..8 → load SFX_START[cue_id], SFX_END[cue_id],
 * go PLAYING. On trig_valid with cue_id == 0 → force IDLE. On trig_valid with
 * cue_id ∈ 9..15 → no-op (reserved).
 *
 * Per-sample_tick in PLAYING: emit rom sample, advance addr; when addr crosses
 * stop_addr, return to IDLE. IDLE output is 0.
 */
module voice_sfx #(
    parameter int DEPTH        = 32768,
    parameter int AW           = 15,
    parameter     MEM_FILE     = "sfx_rom.mem",
    parameter     OFFSETS_FILE = "sfx_offsets.svh"
)(
    input  logic                clk,
    input  logic                rst,
    input  logic                sample_tick,
    input  logic                trig_valid,
    input  logic [3:0]          cue_id,
    output logic signed [15:0]  sample
);
    `include `"OFFSETS_FILE`"     // defines SFX_START[1:8], SFX_END[1:8]

    logic [AW-1:0] addr, stop_addr;
    logic          playing;
    logic signed [15:0] rom_sample;

    sfx_rom #(.DEPTH(DEPTH), .AW(AW), .MEM_FILE(MEM_FILE)) rom_i(
        .clk(clk), .addr(addr), .sample(rom_sample)
    );

    always_ff @(posedge clk) begin
        if (rst) begin
            playing   <= 1'b0;
            addr      <= '0;
            stop_addr <= '0;
        end else if (trig_valid) begin
            if (cue_id == 4'd0) begin
                playing <= 1'b0;
                addr    <= '0;
            end else if (cue_id >= 4'd1 && cue_id <= 4'd8) begin
                addr      <= SFX_START[cue_id][AW-1:0];
                stop_addr <= SFX_END  [cue_id][AW-1:0];
                playing   <= 1'b1;
            end
            // cue_id ∈ 9..15 → reserved, no-op (FSM unchanged)
        end else if (playing && sample_tick) begin
            if (addr >= stop_addr) begin
                playing <= 1'b0;
            end else begin
                addr <= addr + 1'b1;
            end
        end
    end

    assign sample = playing ? rom_sample : 16'sd0;
endmodule
```

  **Note on the `\`include` of a parameterized filename:** SystemVerilog doesn't allow string-parameter substitution in `` `include `` directly; the pattern above relies on macro-expansion of `OFFSETS_FILE` and works in Questa/ModelSim. If the tool rejects it, the fallback is: drop the parameter and hard-code `` `include "sfx_offsets.svh" ``, then copy `sfx_offsets_test.svh` to `sfx_offsets.svh` in the fixtures directory and run the testbench from that directory. Either way, the production path (sfx_offsets.svh is generated by the asset pipeline, committed alongside `voice_sfx.sv`) works unchanged.

- [ ] **Step 4: Verify it passes.**

  ```bash
  vlib work && vlog sfx_rom.sv voice_sfx.sv tb/tb_voice_sfx.sv
  vsim -do "run -all; quit" work.tb_voice_sfx 2>&1 | tail -40
  ```
  Expected: `TEST PASSED: tb_voice_sfx`, zero errors.

  If `` `include `` with a parameter macro fails, apply the fallback from the note above (hardcoded include path), then re-run.

- [ ] **Step 5: Commit.**

  ```bash
  git add hw/voice_sfx.sv hw/tb/tb_voice_sfx.sv
  git commit -m "audio: voice_sfx module and testbench"
  ```

---

### Task 1.5 — `pvz_audio.sv` top module (no testbench yet — tested end-to-end in Task 2.5)

**Files:**
- Create: `hw/pvz_audio.sv`

**Dependency:** Tasks 1.1, 1.3, 1.4.

- [ ] **Step 1: Write `hw/pvz_audio.sv`.**

```systemverilog
/*
 * pvz_audio — second Avalon-slave peripheral for PCM audio playback.
 *
 * Register map (4 × 32-bit words, addressUnits WORDS):
 *   0x00 BGM_CTRL        W  bit [0]    1=play, 0=stop
 *   0x04 SFX_VOICE0_TRIG W  bits [3:0] cue_id; write pulses trig_valid on voice 0
 *   0x08 SFX_VOICE1_TRIG W  bits [3:0] cue_id; write pulses trig_valid on voice 1
 *   0x0C STATUS          R  bits [2:0] {sfx1_active, sfx0_active, bgm_active}
 *
 * Avalon-streaming out: 16-bit signed L+R (mono duplicated), with
 * left_chan_ready / right_chan_ready inputs from altera_up_avalon_audio.
 */
module pvz_audio(
    input  logic         clk,
    input  logic         reset,

    // Avalon-MM slave
    input  logic [2:0]   address,
    input  logic         chipselect,
    input  logic         write,
    input  logic         read,
    input  logic [31:0]  writedata,
    output logic [31:0]  readdata,

    // Avalon-ST sink side (from audio IP)
    input  logic         left_chan_ready,
    input  logic         right_chan_ready,
    output logic signed [15:0] sample_data_l,
    output logic signed [15:0] sample_data_r,
    output logic         sample_valid_l,
    output logic         sample_valid_r
);
    // -- audio_regs: latched control, trigger pulses --
    logic        bgm_play;
    logic        trig0_valid, trig1_valid;
    logic [3:0]  cue0_id,     cue1_id;
    logic        bgm_active, sfx0_active, sfx1_active;

    always_ff @(posedge clk) begin
        if (reset) begin
            bgm_play    <= 1'b0;
            trig0_valid <= 1'b0;
            trig1_valid <= 1'b0;
            cue0_id     <= 4'd0;
            cue1_id     <= 4'd0;
        end else begin
            // trigger valids are 1-cycle pulses
            trig0_valid <= 1'b0;
            trig1_valid <= 1'b0;

            if (chipselect && write) begin
                case (address)
                    3'd0: bgm_play <= writedata[0];
                    3'd1: begin cue0_id <= writedata[3:0]; trig0_valid <= 1'b1; end
                    3'd2: begin cue1_id <= writedata[3:0]; trig1_valid <= 1'b1; end
                    default: ;
                endcase
            end
        end
    end

    // Readback: STATUS at word 3
    always_comb begin
        readdata = 32'd0;
        if (chipselect && read && address == 3'd3) begin
            readdata = {29'd0, sfx1_active, sfx0_active, bgm_active};
        end
    end

    // -- sample_tick: rising edge of combined channel ready --
    logic ready, ready_d;
    assign ready = left_chan_ready & right_chan_ready;
    always_ff @(posedge clk) ready_d <= ready;
    logic sample_tick;
    assign sample_tick = ready & ~ready_d;

    // -- voices --
    logic signed [15:0] bgm_s, sfx0_s, sfx1_s, mixed_s;

    voice_bgm bgm_i(
        .clk(clk), .rst(reset), .play(bgm_play), .sample_tick(sample_tick),
        .sample(bgm_s)
    );

    voice_sfx sfx0_i(
        .clk(clk), .rst(reset), .sample_tick(sample_tick),
        .trig_valid(trig0_valid), .cue_id(cue0_id), .sample(sfx0_s)
    );

    voice_sfx sfx1_i(
        .clk(clk), .rst(reset), .sample_tick(sample_tick),
        .trig_valid(trig1_valid), .cue_id(cue1_id), .sample(sfx1_s)
    );

    mixer mix_i(.bgm(bgm_s), .sfx0(sfx0_s), .sfx1(sfx1_s), .mixed(mixed_s));

    // -- activity flags for STATUS --
    assign bgm_active  = bgm_play;
    assign sfx0_active = (sfx0_s != 16'sd0);
    assign sfx1_active = (sfx1_s != 16'sd0);

    // -- streaming out: mono duplicated, valid while ready --
    always_ff @(posedge clk) begin
        if (reset) begin
            sample_data_l  <= 16'sd0;
            sample_data_r  <= 16'sd0;
            sample_valid_l <= 1'b0;
            sample_valid_r <= 1'b0;
        end else begin
            sample_data_l  <= mixed_s;
            sample_data_r  <= mixed_s;
            sample_valid_l <= ready;
            sample_valid_r <= ready;
        end
    end
endmodule
```

- [ ] **Step 2: Smoke-check compilation.**

  ```bash
  cd /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine/hw
  vlib work && vlog bgm_rom.sv sfx_rom.sv mixer.sv voice_bgm.sv voice_sfx.sv pvz_audio.sv
  ```
  Expected: clean compile, no errors.

- [ ] **Step 3: Commit.**

  ```bash
  git add hw/pvz_audio.sv
  git commit -m "audio: pvz_audio top module"
  ```

---

## Chunk 2 — Qsys integration

### Task 2.1 — WM8731 pin assignments in `soc_system.qsf`

**Files:**
- Modify: `hw/soc_system.qsf` (append 7 pin assignments)

**Dependency:** none (can run before or after chunk 1).

Reference `doc/reference-design/bubble-bobble/code/vga_audio_integration/soc_system.qsf` and/or the DE1-SoC pin table in `doc/manual/DE1-SoC_User_manual.md`.

- [ ] **Step 1: Append WM8731 pin assignments** to `hw/soc_system.qsf`. Append these lines at the end of the file:

```tcl
# =====================================================================
# Wolfson WM8731 Audio Codec (Task 2.1)
# =====================================================================
set_location_assignment PIN_AC27 -to AUD_ADCDAT
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to AUD_ADCDAT
set_location_assignment PIN_AG30 -to AUD_ADCLRCK
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to AUD_ADCLRCK
set_location_assignment PIN_AF30 -to AUD_BCLK
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to AUD_BCLK
set_location_assignment PIN_AC28 -to AUD_DACDAT
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to AUD_DACDAT
set_location_assignment PIN_AH4  -to AUD_DACLRCK
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to AUD_DACLRCK
set_location_assignment PIN_AC9  -to AUD_XCK
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to AUD_XCK
set_location_assignment PIN_AH7  -to FPGA_I2C_SCLK
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to FPGA_I2C_SCLK
set_location_assignment PIN_AG7  -to FPGA_I2C_SDAT
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to FPGA_I2C_SDAT
```

**Note:** Verify these pin numbers against the DE1-SoC manual Chapter 3.6.4 before committing. If they differ, use the manual's values, not these.

- [ ] **Step 2: Commit.**

  ```bash
  git add hw/soc_system.qsf
  git commit -m "audio: WM8731 pin assignments"
  ```

---

### Task 2.2 — `pvz_audio_hw.tcl` Qsys component descriptor

**Files:**
- Create: `hw/pvz_audio_hw.tcl`

**Dependency:** Task 1.5 (needs `pvz_audio.sv`).

This file follows the same structure as `hw/pvz_top_hw.tcl` and
`doc/reference-design/bubble-bobble/code/vga_audio_integration/audio/fpga_audio_hw.tcl`.
Use the existing `pvz_top_hw.tcl` as the template for register layout.

- [ ] **Step 1: Write `hw/pvz_audio_hw.tcl`.**

```tcl
# pvz_audio Qsys component descriptor
package require -exact qsys 16.1

set_module_property DESCRIPTION "PvZ audio peripheral"
set_module_property NAME pvz_audio
set_module_property VERSION 1.0
set_module_property INTERNAL false
set_module_property OPAQUE_ADDRESS_MAP true
set_module_property DISPLAY_NAME pvz_audio
set_module_property INSTANTIATE_IN_SYSTEM_MODULE true
set_module_property EDITABLE true
set_module_property REPORT_TO_TALKBACK false
set_module_property ALLOW_GREYBOX_GENERATION false
set_module_property REPORT_HIERARCHY false

# File set
add_fileset QUARTUS_SYNTH QUARTUS_SYNTH "" ""
set_fileset_property QUARTUS_SYNTH TOP_LEVEL pvz_audio
set_fileset_property QUARTUS_SYNTH ENABLE_RELATIVE_INCLUDE_PATHS false
set_fileset_property QUARTUS_SYNTH ENABLE_FILE_OVERWRITE_MODE false
add_fileset_file pvz_audio.sv   SYSTEM_VERILOG PATH pvz_audio.sv TOP_LEVEL_FILE
add_fileset_file mixer.sv       SYSTEM_VERILOG PATH mixer.sv
add_fileset_file voice_bgm.sv   SYSTEM_VERILOG PATH voice_bgm.sv
add_fileset_file voice_sfx.sv   SYSTEM_VERILOG PATH voice_sfx.sv
add_fileset_file bgm_rom.sv     SYSTEM_VERILOG PATH bgm_rom.sv
add_fileset_file sfx_rom.sv     SYSTEM_VERILOG PATH sfx_rom.sv
add_fileset_file bgm_rom.mem    HEX            PATH bgm_rom.mem
add_fileset_file sfx_rom.mem    HEX            PATH sfx_rom.mem
add_fileset_file sfx_offsets.svh SYSTEM_VERILOG_INCLUDE PATH sfx_offsets.svh

# Three-way compatible-string tie: CLAUDE.md invariant.
set_module_assignment embeddedsw.dts.group    audio
set_module_assignment embeddedsw.dts.name     pvz_audio
set_module_assignment embeddedsw.dts.vendor   csee4840
set_module_assignment embeddedsw.dts.compatible "csee4840,pvz_audio-1.0"

# Clock
add_interface clock clock end
set_interface_property clock clockRate 0
set_interface_property clock ENABLED true
add_interface_port clock clk clk Input 1

# Reset
add_interface reset reset end
set_interface_property reset associatedClock clock
set_interface_property reset synchronousEdges DEASSERT
set_interface_property reset ENABLED true
add_interface_port reset reset reset Input 1

# Avalon-MM slave — addressUnits WORDS, 32-bit writedata
add_interface avalon_slave avalon end
set_interface_property avalon_slave addressUnits WORDS
set_interface_property avalon_slave associatedClock clock
set_interface_property avalon_slave associatedReset reset
set_interface_property avalon_slave bitsPerSymbol 8
set_interface_property avalon_slave burstOnBurstBoundariesOnly false
set_interface_property avalon_slave burstcountUnits WORDS
set_interface_property avalon_slave explicitAddressSpan 0
set_interface_property avalon_slave holdTime 0
set_interface_property avalon_slave linewrapBursts false
set_interface_property avalon_slave maximumPendingReadTransactions 0
set_interface_property avalon_slave maximumPendingWriteTransactions 0
set_interface_property avalon_slave readLatency 0
set_interface_property avalon_slave readWaitTime 1
set_interface_property avalon_slave setupTime 0
set_interface_property avalon_slave timingUnits Cycles
set_interface_property avalon_slave writeWaitTime 0
set_interface_property avalon_slave ENABLED true
add_interface_port avalon_slave address     address     Input   3
add_interface_port avalon_slave chipselect  chipselect  Input   1
add_interface_port avalon_slave write       write       Input   1
add_interface_port avalon_slave read        read        Input   1
add_interface_port avalon_slave writedata   writedata   Input  32
add_interface_port avalon_slave readdata    readdata    Output 32
set_interface_assignment avalon_slave embeddedsw.configuration.isFlash 0
set_interface_assignment avalon_slave embeddedsw.configuration.isMemoryDevice 0
set_interface_assignment avalon_slave embeddedsw.configuration.isNonVolatileStorage 0
set_interface_assignment avalon_slave embeddedsw.configuration.isPrintableDevice 0

# Avalon-ST source L
add_interface audio_out_l avalon_streaming start
set_interface_property audio_out_l associatedClock clock
set_interface_property audio_out_l associatedReset reset
set_interface_property audio_out_l dataBitsPerSymbol 16
set_interface_property audio_out_l errorDescriptor ""
set_interface_property audio_out_l firstSymbolInHighOrderBits true
set_interface_property audio_out_l maxChannel 0
set_interface_property audio_out_l readyLatency 0
set_interface_property audio_out_l ENABLED true
add_interface_port audio_out_l left_chan_ready ready Input  1
add_interface_port audio_out_l sample_data_l   data  Output 16
add_interface_port audio_out_l sample_valid_l  valid Output 1

# Avalon-ST source R
add_interface audio_out_r avalon_streaming start
set_interface_property audio_out_r associatedClock clock
set_interface_property audio_out_r associatedReset reset
set_interface_property audio_out_r dataBitsPerSymbol 16
set_interface_property audio_out_r firstSymbolInHighOrderBits true
set_interface_property audio_out_r maxChannel 0
set_interface_property audio_out_r readyLatency 0
set_interface_property audio_out_r ENABLED true
add_interface_port audio_out_r right_chan_ready ready Input  1
add_interface_port audio_out_r sample_data_r    data  Output 16
add_interface_port audio_out_r sample_valid_r   valid Output 1
```

- [ ] **Step 2: Commit.**

  ```bash
  git add hw/pvz_audio_hw.tcl
  git commit -m "audio: pvz_audio qsys component descriptor"
  ```

---

### Task 2.3 — Qsys system edits: add Intel audio IP and pvz_audio instance

**Files:**
- Modify: `hw/soc_system.qsys`

**Dependency:** Task 2.2.

This must be done in Platform Designer (Qsys) GUI, not by hand-editing XML. The actions to perform:

- [ ] **Step 1: Open Qsys.**

  ```bash
  cd /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine/hw
  qsys-edit soc_system.qsys &
  ```

- [ ] **Step 2: Add these four IP instances** (use the IP Catalog):
  1. `altera_up_avalon_audio_pll` → rename to `audio_pll_0`. Set reference clock to 50 MHz.
  2. `altera_up_avalon_audio_and_video_config` → rename to `audio_and_video_config_0`.
  3. `altera_up_avalon_audio` → rename to `audio_0`. Set: `audio_in=false`, `audio_out=true`, `avalon_bus_type=Streaming`, `dw=16`.
  4. `pvz_audio` (from **Project Components**, since we just added the `_hw.tcl`) → instance name `pvz_audio_0`.

- [ ] **Step 3: Wire the instances** exactly as bubble-bobble does (see `doc/reference-design/bubble-bobble/code/vga_audio_integration/soc_system.qsys` for reference):
  - `audio_pll_0.audio_clk` → `audio_0.sys_clk`, `audio_and_video_config_0.clk`, and `pvz_audio_0.clock`
  - Main 50 MHz system clock → `audio_pll_0.ref_clk`
  - Reset: system reset → `audio_0.reset`, `audio_and_video_config_0.reset`, `pvz_audio_0.reset`
  - I²C conduit: `audio_and_video_config_0.external_interface` → export as `audio_and_video_config_0_external_interface`
  - I²S conduit: `audio_0.external_interface` → export as `audio_0_external_interface`
  - Avalon-MM: HPS lightweight bridge master → `pvz_audio_0.avalon_slave`. Set base address to `0x0000_1000` inside the lwh2f region (absolute `0xff20_1000`, which maps to `0xff210000` after the bridge — or whichever unused slot Qsys assigns; record the actual base in Step 5).
  - Avalon-ST: `pvz_audio_0.audio_out_l` → `audio_0.avalon_left_channel_sink`
  - Avalon-ST: `pvz_audio_0.audio_out_r` → `audio_0.avalon_right_channel_sink`
  - HPS-to-audio_and_video_config Avalon-MM: HPS lightweight bridge master → `audio_and_video_config_0.avalon_av_config_slave` (required by the IP for one-time codec init from userspace, or skip if the IP auto-configures on reset with default 8 kHz)

- [ ] **Step 4: Generate HDL.** File → Save, then Generate → Generate HDL, then close Qsys.

- [ ] **Step 5: Record the pvz_audio base address.** Open `hw/soc_system.qsys` in a text editor, grep for `pvz_audio_0` and the `baseAddress` parameter on its connection. Record the byte-offset address it was assigned — this must match what the Linux driver expects. **If it's not `0xff210000`**, update the spec reference in `CLAUDE.md` (Task 6.1) to match the actual address rather than hard-coding `0xff210000` in the driver.

- [ ] **Step 6: Run qsys-generate in headless mode** to confirm the system builds:

  ```bash
  cd /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine/hw
  make qsys 2>&1 | tail -30
  ```
  Expected: `# Info: qsys-generate succeeded.` at the end.

- [ ] **Step 7: Commit.**

  ```bash
  git add hw/soc_system.qsys
  git commit -m "audio: qsys wiring for pvz_audio and intel audio IPs"
  ```

---

### Task 2.4 — `soc_system_top.sv` conduit routing to board

**Files:**
- Modify: `hw/soc_system_top.sv`

**Dependency:** Task 2.3.

- [ ] **Step 1: Read the file first** to find the existing port list and instance of the Qsys system:

  ```bash
  grep -n "AUD_\|FPGA_I2C_\|soc_system soc" /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine/hw/soc_system_top.sv | head -20
  ```

- [ ] **Step 2: Add these inouts/outputs to the module port list** (after the existing VGA / KEY / LED ports):

```systemverilog
    // WM8731 audio codec — added for pvz_audio
    inout  logic        FPGA_I2C_SCLK,
    inout  logic        FPGA_I2C_SDAT,
    output logic        AUD_XCK,
    inout  logic        AUD_ADCLRCK,
    input  logic        AUD_ADCDAT,
    inout  logic        AUD_DACLRCK,
    output logic        AUD_DACDAT,
    inout  logic        AUD_BCLK,
```

- [ ] **Step 3: Connect these ports to the Qsys system instance.** Find the existing `soc_system u0 (.*)` (or named-port) instantiation and add:

```systemverilog
        // Audio codec conduits (names match the exports from Task 2.3)
        .audio_and_video_config_0_external_interface_I2C_SDAT (FPGA_I2C_SDAT),
        .audio_and_video_config_0_external_interface_I2C_SCLK (FPGA_I2C_SCLK),
        .audio_0_external_interface_ADCDAT   (AUD_ADCDAT),
        .audio_0_external_interface_ADCLRCK  (AUD_ADCLRCK),
        .audio_0_external_interface_BCLK     (AUD_BCLK),
        .audio_0_external_interface_DACDAT   (AUD_DACDAT),
        .audio_0_external_interface_DACLRCK  (AUD_DACLRCK),
        .audio_pll_0_audio_clk_clk           (AUD_XCK),
```

  If `soc_system u0 (.*)` (wildcard port binding) is used, the named connection above must still be added — wildcard only covers ports whose names match; the audio exports are new conduits whose top-level names may differ from the signal names.

- [ ] **Step 4: Smoke-check compilation.**

  ```bash
  cd /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine/hw
  vlib work && vlog mixer.sv voice_bgm.sv voice_sfx.sv bgm_rom.sv sfx_rom.sv pvz_audio.sv soc_system_top.sv 2>&1 | grep -E "Error|Warning" | head -20
  ```
  Expected: no errors. Warnings for uninferred ports are OK at this stage (only Qsys assembles the full hierarchy).

- [ ] **Step 5: Commit.**

  ```bash
  git add hw/soc_system_top.sv
  git commit -m "audio: route WM8731 conduits in soc_system_top"
  ```

---

### Task 2.5 — End-to-end testbench `tb_pvz_audio.sv`

**Files:**
- Create: `hw/tb/tb_pvz_audio.sv`

**Dependency:** Task 1.5.

Functional end-to-end test — Avalon MM writes → streaming outputs. Catches the Avalon-WORDS-vs-bytes trap.

- [ ] **Step 1: Write the failing testbench** at `hw/tb/tb_pvz_audio.sv`.

```systemverilog
/*
 * End-to-end testbench for pvz_audio. Drives Avalon-MM writes to:
 *   - BGM_CTRL   at word address 0 (CPU byte offset 0x00)
 *   - SFX0_TRIG  at word address 1 (CPU byte offset 0x04)
 *
 * Uses the production sfx_offsets.svh — for the testbench we supply a minimal
 * version at hw/sfx_offsets.svh (empty/zero clips) so the DUT elaborates. The
 * real file is regenerated by the asset pipeline (Task 5.2).
 *
 * Uses tb/fixtures/bgm_test.mem (16-sample ramp) for BGM verification.
 */
`timescale 1ns/1ps

module tb_pvz_audio;
    logic clk, reset;
    logic [2:0]  address;
    logic        chipselect, write, read;
    logic [31:0] writedata, readdata;
    logic        left_chan_ready, right_chan_ready;
    logic signed [15:0] sample_data_l, sample_data_r;
    logic        sample_valid_l, sample_valid_r;

    // Instantiate DUT with test fixtures (override MEM_FILE + DEPTH on voices)
    pvz_audio dut(.*);

    initial clk = 0;
    always #10 clk = ~clk; // 50 MHz

    integer errors = 0;

    task av_write(input [2:0] a, input [31:0] d);
        @(posedge clk);
        address = a; writedata = d; chipselect = 1; write = 1;
        @(posedge clk);
        chipselect = 0; write = 0;
        @(posedge clk);
    endtask

    task sample_pulse;
        // Pulse both ready high for 1 cycle, then low.
        left_chan_ready = 1; right_chan_ready = 1;
        @(posedge clk);
        left_chan_ready = 0; right_chan_ready = 0;
        @(posedge clk);
    endtask

    initial begin
        reset = 1; chipselect = 0; write = 0; read = 0;
        address = 0; writedata = 0;
        left_chan_ready = 0; right_chan_ready = 0;
        repeat (5) @(posedge clk);
        reset = 0;
        @(posedge clk);

        // --- Start BGM ---
        av_write(3'd0, 32'd1);   // BGM_CTRL = 1

        // Pulse sample ticks, expect samples to appear on the output
        // (exact value depends on BGM_LEN and ramp content; simple check
        // is that sample_valid pulses and sample_data is non-zero for
        // at least one of the first 4 ticks)
        integer saw_data = 0;
        for (int i = 0; i < 8; i++) begin
            sample_pulse;
            if (sample_valid_l && sample_data_l !== 16'sd0) saw_data = 1;
        end
        if (saw_data == 0) begin
            $display("FAIL: after BGM_CTRL=1, no non-zero sample observed on L");
            errors++;
        end else begin
            $display("PASS: BGM produces samples");
        end

        // --- Stop BGM ---
        av_write(3'd0, 32'd0);   // BGM_CTRL = 0
        repeat (4) sample_pulse;
        if (sample_data_l !== 16'sd0) begin
            $display("FAIL: after BGM_CTRL=0, L still non-zero (%0d)", sample_data_l);
            errors++;
        end else begin
            $display("PASS: BGM stopped");
        end

        // --- Trigger SFX voice 0 with cue 0 (= stop). Should stay silent. ---
        av_write(3'd1, 32'h0000_0000);
        repeat (2) sample_pulse;
        if (sample_data_l !== 16'sd0) begin
            $display("FAIL: cue 0 trigger produced audio");
            errors++;
        end else begin
            $display("PASS: cue 0 trigger stays silent");
        end

        if (errors == 0) $display("TEST PASSED: tb_pvz_audio");
        else             $display("TEST FAILED: tb_pvz_audio (%0d errors)", errors);
        $finish;
    end
endmodule
```

- [ ] **Step 2: Create a minimal production `hw/sfx_offsets.svh`** stub (will be overwritten by asset pipeline in Task 5.2):

```systemverilog
/* Placeholder — replaced by hw/audio/build_audio_mif.py (Task 5.2). */
parameter logic [14:0] SFX_START [1:8] = '{15'd0, 15'd0, 15'd0, 15'd0, 15'd0, 15'd0, 15'd0, 15'd0};
parameter logic [14:0] SFX_END   [1:8] = '{15'd0, 15'd0, 15'd0, 15'd0, 15'd0, 15'd0, 15'd0, 15'd0};
```

- [ ] **Step 3: Create placeholder `hw/bgm_rom.mem` and `hw/sfx_rom.mem`** (single zero sample each, will be overwritten by Task 5.2):

```
# hw/bgm_rom.mem — one line:
0000
```

```
# hw/sfx_rom.mem — one line:
0000
```

- [ ] **Step 4: Run the testbench. Verify it fails first** (if pvz_audio or its includes aren't wired right the elaboration will error):

  ```bash
  cd /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine/hw
  vlib work && vlog mixer.sv bgm_rom.sv sfx_rom.sv voice_bgm.sv voice_sfx.sv pvz_audio.sv tb/tb_pvz_audio.sv
  ```
  Expected: clean compile. If errors, fix module port mismatches before proceeding.

- [ ] **Step 5: Run the testbench.**

  ```bash
  vsim -do "run -all; quit" work.tb_pvz_audio 2>&1 | tail -30
  ```
  Expected: `TEST PASSED: tb_pvz_audio`, zero errors.

  **If it fails with "no non-zero sample observed":** check that `bgm_rom.mem` contains the testbench ramp content — the testbench shares the production `bgm_rom.mem`, so the placeholder from Step 3 will emit zeros. Temporarily point `voice_bgm`'s parameter to `tb/fixtures/bgm_test.mem` via the DUT instantiation override in the testbench (`pvz_audio #(.BGM_MEM_FILE("tb/fixtures/bgm_test.mem")) dut(.*)`). Alternative: skip the "samples non-zero" subcheck and rely on `sample_valid` pulsing alone as the proof-of-wiring.

- [ ] **Step 6: Commit.**

  ```bash
  git add hw/tb/tb_pvz_audio.sv hw/sfx_offsets.svh hw/bgm_rom.mem hw/sfx_rom.mem
  git commit -m "audio: end-to-end pvz_audio testbench and rom placeholders"
  ```

---

### Task 2.6 — Full Quartus build sanity check

**Files:** none (verification step).

**Dependency:** Tasks 2.1–2.5.

- [ ] **Step 1: Run the full build chain.**

  ```bash
  cd /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine/hw
  make qsys && make quartus && make rbf && make dtb 2>&1 | tee /tmp/quartus_build.log | tail -40
  ```
  Expected: `quartus_sh --flow compile` completes, `.rbf` and `.dtb` files appear in `output_files/`.

- [ ] **Step 2: Verify the DTB contains `pvz_audio`.**

  ```bash
  strings hw/output_files/soc_system.dtb 2>/dev/null | grep pvz_audio
  ```
  Alternative if `output_files/` doesn't hold the dtb (location varies): `find . -name "*.dtb" -exec strings {} \; | grep pvz_audio`.
  Expected: `csee4840,pvz_audio-1.0` appears at least once.

- [ ] **Step 3: Record M10K utilization.** From the build log:

  ```bash
  grep -A 2 "Total block memory" /tmp/quartus_build.log
  ```
  Expected: utilization close to 80.2% (spec's Success Criteria 10 §1). **If above 85%, escalate:** drop BGM sample rate to 4 kHz (Chunk 5, future follow-up) per the spec's Known Risks.

- [ ] **Step 4: Commit generated artifacts if not gitignored** (check `.gitignore` — typically `output_files/` is ignored). If any committable file was updated (e.g. `quartus_build.log`), commit:

  ```bash
  git status --short
  git add -u hw/
  git diff --cached --stat
  # only commit if there are real source changes (not ignored build artifacts)
  git commit -m "audio: quartus build clean" 2>/dev/null || echo "nothing to commit"
  ```

---

## Chunk 3 — Linux driver

### Task 3.1 — `sw/pvz_audio_driver.h`

**Files:**
- Create: `sw/pvz_audio_driver.h`

**Dependency:** none (driver header is the contract surface; userspace includes it).

- [ ] **Step 1: Write `sw/pvz_audio_driver.h`.**

```c
#ifndef PVZ_AUDIO_DRIVER_H
#define PVZ_AUDIO_DRIVER_H

#include <linux/ioctl.h>

#define PVZ_AUDIO_MAGIC      'A'

/*
 * BGM_CTRL: arg is 0 (stop) or 1 (play).
 * PLAY_SFX: arg is a cue id in [1..8]; driver round-robins between the two
 *           voice registers. Argument 0 stops whichever voice last played.
 * STATUS:   readback of {sfx1_active, sfx0_active, bgm_active} as uint32_t.
 */
#define PVZ_AUDIO_BGM_CTRL   _IOW(PVZ_AUDIO_MAGIC, 1, uint32_t)
#define PVZ_AUDIO_PLAY_SFX   _IOW(PVZ_AUDIO_MAGIC, 2, uint32_t)
#define PVZ_AUDIO_STATUS     _IOR(PVZ_AUDIO_MAGIC, 3, uint32_t)

#endif /* PVZ_AUDIO_DRIVER_H */
```

- [ ] **Step 2: Commit.**

  ```bash
  git add sw/pvz_audio_driver.h
  git commit -m "audio: driver header"
  ```

---

### Task 3.2 — `sw/pvz_audio_driver.c`

**Files:**
- Create: `sw/pvz_audio_driver.c`

**Dependency:** Task 3.1.

Structural mirror of `sw/pvz_driver.c`. Voice round-robin lives here.

- [ ] **Step 1: Write `sw/pvz_audio_driver.c`.**

```c
/*
 * pvz_audio_driver — Linux misc-device driver for the pvz_audio peripheral.
 * Exposes /dev/pvz_audio with three ioctls defined in pvz_audio_driver.h.
 *
 * Voice round-robin: on each PLAY_SFX, the driver writes to whichever voice
 * did NOT last play, so two back-to-back cues never stomp each other.
 * Argument 0 to PLAY_SFX stops whichever voice last played.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include "pvz_audio_driver.h"

#define DRIVER_NAME "pvz_audio"

/* Register offsets in bytes (WORDS addressing → N * 4 on the CPU side). */
#define REG_BGM_CTRL        0x00
#define REG_SFX_VOICE0_TRIG 0x04
#define REG_SFX_VOICE1_TRIG 0x08
#define REG_STATUS          0x0C

struct pvz_audio_dev {
    struct resource res;
    void __iomem   *virtbase;
    unsigned int    last_voice;  /* 0 or 1 */
};
static struct pvz_audio_dev dev;

static long pvz_audio_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
    uint32_t val;
    switch (cmd) {
    case PVZ_AUDIO_BGM_CTRL:
        if (copy_from_user(&val, (uint32_t __user *)arg, sizeof(val)))
            return -EACCES;
        iowrite32(val & 0x1, dev.virtbase + REG_BGM_CTRL);
        break;

    case PVZ_AUDIO_PLAY_SFX:
        if (copy_from_user(&val, (uint32_t __user *)arg, sizeof(val)))
            return -EACCES;
        /* Round-robin: pick the voice that didn't last play. */
        if (dev.last_voice == 0) {
            iowrite32(val & 0xF, dev.virtbase + REG_SFX_VOICE1_TRIG);
            dev.last_voice = 1;
        } else {
            iowrite32(val & 0xF, dev.virtbase + REG_SFX_VOICE0_TRIG);
            dev.last_voice = 0;
        }
        break;

    case PVZ_AUDIO_STATUS:
        val = ioread32(dev.virtbase + REG_STATUS);
        if (copy_to_user((uint32_t __user *)arg, &val, sizeof(val)))
            return -EACCES;
        break;

    default:
        return -EINVAL;
    }
    return 0;
}

static const struct file_operations fops = {
    .owner          = THIS_MODULE,
    .unlocked_ioctl = pvz_audio_ioctl,
};

static struct miscdevice misc_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = DRIVER_NAME,
    .fops  = &fops,
};

static int __init pvz_audio_probe(struct platform_device *pdev)
{
    int ret;

    ret = misc_register(&misc_device);
    if (ret) return ret;

    ret = of_address_to_resource(pdev->dev.of_node, 0, &dev.res);
    if (ret) { ret = -ENOENT; goto out_deregister; }

    if (request_mem_region(dev.res.start, resource_size(&dev.res),
                           DRIVER_NAME) == NULL) {
        ret = -EBUSY; goto out_deregister;
    }

    dev.virtbase = of_iomap(pdev->dev.of_node, 0);
    if (dev.virtbase == NULL) { ret = -ENOMEM; goto out_release; }

    dev.last_voice = 1;  /* first PLAY_SFX → voice 0 */
    pr_info(DRIVER_NAME ": probed at %pa\n", &dev.res.start);
    return 0;

out_release:
    release_mem_region(dev.res.start, resource_size(&dev.res));
out_deregister:
    misc_deregister(&misc_device);
    return ret;
}

static int pvz_audio_remove(struct platform_device *pdev)
{
    iounmap(dev.virtbase);
    release_mem_region(dev.res.start, resource_size(&dev.res));
    misc_deregister(&misc_device);
    return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id pvz_audio_of_match[] = {
    { .compatible = "csee4840,pvz_audio-1.0" },
    {},
};
MODULE_DEVICE_TABLE(of, pvz_audio_of_match);
#endif

static struct platform_driver pvz_audio_driver = {
    .driver = {
        .name           = DRIVER_NAME,
        .owner          = THIS_MODULE,
        .of_match_table = of_match_ptr(pvz_audio_of_match),
    },
    .remove = __exit_p(pvz_audio_remove),
};

static int __init pvz_audio_init(void) {
    pr_info(DRIVER_NAME ": init\n");
    return platform_driver_probe(&pvz_audio_driver, pvz_audio_probe);
}

static void __exit pvz_audio_exit(void) {
    platform_driver_unregister(&pvz_audio_driver);
    pr_info(DRIVER_NAME ": exit\n");
}

module_init(pvz_audio_init);
module_exit(pvz_audio_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("CSEE4840 PvZ team");
MODULE_DESCRIPTION("pvz_audio driver");
```

- [ ] **Step 2: Commit.**

  ```bash
  git add sw/pvz_audio_driver.c
  git commit -m "audio: linux driver"
  ```

---

### Task 3.3 — `sw/Makefile` — build the new module

**Files:**
- Modify: `sw/Makefile`

**Dependency:** Task 3.2.

- [ ] **Step 1: Add `pvz_audio_driver.o` to the kernel build** by editing the `obj-m` line in `sw/Makefile`. Change:

```makefile
obj-m := pvz_driver.o
```

to:

```makefile
obj-m := pvz_driver.o pvz_audio_driver.o
```

- [ ] **Step 2: Cross-compile sanity check** (skip if no ARM toolchain; log gap in `doc/v4-known-issues.md`):

  ```bash
  cd /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine/sw
  make CC=arm-linux-gnueabihf-gcc ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- \
       KERNEL_SOURCE=/path/to/linux-kernel-headers module 2>&1 | tail -20
  ```
  Expected: `pvz_audio_driver.ko` appears in `sw/`.

  If the toolchain or kernel source isn't available, add an entry to `doc/v4-known-issues.md` with severity=minor, area=driver, proposed fix "cross-compile on Columbia workstation and verify `pvz_audio_driver.ko` builds".

- [ ] **Step 3: Commit.**

  ```bash
  git add sw/Makefile
  git commit -m "audio: makefile wires pvz_audio_driver"
  ```

---

## Chunk 4 — Userspace integration and event bus

### Task 4.1 — `sw/pvz.h` SFX cue enum

**Files:**
- Modify: `sw/pvz.h`

**Dependency:** none.

- [ ] **Step 1: Read the existing `sw/pvz.h`** to see what's already there:

  ```bash
  grep -n "typedef\|enum\|#define" /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine/sw/pvz.h
  ```

- [ ] **Step 2: Append the enum near the bottom of `sw/pvz.h`** (just before the closing `#endif`):

```c
/* SFX cue ids — shared by game.c, main.c, test_game.c, and driver. */
typedef enum {
    PVZ_SFX_NONE         = 0,
    PVZ_SFX_PEA_FIRE     = 1,
    PVZ_SFX_PEA_HIT      = 2,
    PVZ_SFX_ZOMBIE_BITE  = 3,
    PVZ_SFX_ZOMBIE_DEATH = 4,
    PVZ_SFX_PLANT_PLACE  = 5,
    PVZ_SFX_WAVE_START   = 6,
    PVZ_SFX_GAME_OVER    = 7,
    PVZ_SFX_VICTORY      = 8,
} pvz_sfx_cue_t;

#define PVZ_AUDIO_EVENT(cue) (1u << ((cue) - 1))
```

- [ ] **Step 3: Commit.**

  ```bash
  git add sw/pvz.h
  git commit -m "audio: pvz_sfx_cue_t enum"
  ```

---

### Task 4.2 — `sw/audio.h` + `sw/audio.c` userspace wrapper

**Files:**
- Create: `sw/audio.h`
- Create: `sw/audio.c`

**Dependency:** Tasks 3.1, 4.1.

- [ ] **Step 1: Write `sw/audio.h`.**

```c
#ifndef PVZ_AUDIO_H
#define PVZ_AUDIO_H

#include "pvz.h"   /* pvz_sfx_cue_t */

/* Opens /dev/pvz_audio; returns 0 on success, -1 on failure. */
int  audio_init(void);

/* Idempotent start/stop of background music. */
void audio_bgm_start(void);
void audio_bgm_stop(void);

/* Fire a one-shot SFX. Safe to call with PVZ_SFX_NONE (no-op). */
void audio_play_sfx(pvz_sfx_cue_t cue);

/* Close device handle. */
void audio_close(void);

#endif /* PVZ_AUDIO_H */
```

- [ ] **Step 2: Write `sw/audio.c`.**

```c
#include "audio.h"
#include "pvz_audio_driver.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int audio_fd = -1;

int audio_init(void) {
    audio_fd = open("/dev/pvz_audio", O_RDWR);
    if (audio_fd < 0) {
        perror("audio_init: open /dev/pvz_audio");
        return -1;
    }
    return 0;
}

void audio_close(void) {
    if (audio_fd >= 0) {
        close(audio_fd);
        audio_fd = -1;
    }
}

void audio_bgm_start(void) {
    uint32_t v = 1;
    if (audio_fd >= 0 && ioctl(audio_fd, PVZ_AUDIO_BGM_CTRL, &v) < 0)
        perror("audio_bgm_start");
}

void audio_bgm_stop(void) {
    uint32_t v = 0;
    if (audio_fd >= 0 && ioctl(audio_fd, PVZ_AUDIO_BGM_CTRL, &v) < 0)
        perror("audio_bgm_stop");
}

void audio_play_sfx(pvz_sfx_cue_t cue) {
    if (cue == PVZ_SFX_NONE) return;
    uint32_t v = (uint32_t)cue;
    if (audio_fd >= 0 && ioctl(audio_fd, PVZ_AUDIO_PLAY_SFX, &v) < 0)
        perror("audio_play_sfx");
}
```

- [ ] **Step 3: Verify compilation.**

  ```bash
  cd /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine/sw
  gcc -Wall -O2 -c audio.c -o /tmp/audio.o
  ```
  Expected: no warnings, no errors.

- [ ] **Step 4: Commit.**

  ```bash
  git add sw/audio.h sw/audio.c
  git commit -m "audio: userspace wrapper"
  ```

---

### Task 4.3 — `game.h` event bitmask struct + `game_step` signature change

**Files:**
- Modify: `sw/game.h`

**Dependency:** Task 4.1.

- [ ] **Step 1: Read `sw/game.h`** to find the current `game_step` declaration:

  ```bash
  grep -n "game_step" /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine/sw/game.h
  ```

- [ ] **Step 2: Add the struct and update `game_step` signature.** After the existing includes and before the first function declaration, insert:

```c
/* Audio events emitted by game_step(). Caller zeroes `flags` before the
 * call; set bits are cue ids (use PVZ_AUDIO_EVENT(cue) to build masks). */
typedef struct {
    uint32_t flags;
} audio_events_t;
```

Then change the `game_step` prototype from whatever it is (likely `void game_step(game_state_t *gs, input_action_t action);`) to:

```c
void game_step(game_state_t *gs, input_action_t action, audio_events_t *ev);
```

- [ ] **Step 3: Commit.**

  ```bash
  git add sw/game.h
  git commit -m "audio: game.h event bitmask and game_step signature"
  ```

---

### Task 4.4 — `game.c` event emission + test assertions

**Files:**
- Modify: `sw/game.c`
- Modify: `sw/test/test_game.c`

**Dependency:** Tasks 4.1, 4.3.

This task follows TDD: a single test is added, verified to fail, then the corresponding emission point is added, verified to pass. Repeat for each of the 6 in-game events + the "caller-clears" contract test. Seven iterations inside one task, shared subagent.

- [ ] **Step 1: Update `game_step`'s body in `sw/game.c`** to accept the new `ev` parameter and clear-on-entry semantics — the new line at the top of `game_step` is:

```c
void game_step(game_state_t *gs, input_action_t action, audio_events_t *ev) {
    /* Caller-owned bitmask: we overwrite wholesale each step. */
    if (ev) ev->flags = 0;

    /* ... existing body unchanged ... */
}
```

  Update every existing call site of `game_step` in the codebase (at least `sw/main.c` and `sw/test/test_game.c`) to pass a pointer — can be `NULL` at this stage; the next steps will pass a real struct.

  ```bash
  grep -n "game_step" /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine/sw/main.c \
                     /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine/sw/test/test_game.c
  ```
  Pass `NULL` at existing call sites (main.c will get a real struct in Task 4.6; test_game.c call sites will be updated as each new test below adds one):

  ```c
  game_step(&gs, INPUT_NONE, NULL);
  ```

- [ ] **Step 2: Compile & run the existing test suite** — it should still pass (no new tests yet, behavior unchanged).

  ```bash
  cd /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine/sw
  make test_game && ./test_game
  ```
  Expected: all existing tests pass.

- [ ] **Step 3: Commit the signature-only refactor.**

  ```bash
  git add sw/game.c sw/main.c sw/test/test_game.c
  git commit -m "audio: thread audio_events_t through game_step"
  ```

- [ ] **Step 4: TDD cycle — `PEA_FIRE` event.** Add to the bottom of `sw/test/test_game.c` (before the main() runner):

```c
static void test_pea_fire_emits_event(void) {
    game_state_t gs;
    game_init(&gs);
    /* Place a peashooter that is about to fire its first pea. */
    /* (Use the test helpers already in test_game.c for placement; names
     *  may vary — see the existing test_plant_placement test for the
     *  pattern.) */
    audio_events_t ev;
    /* Step forward enough ticks to cross the peashooter's fire interval. */
    for (int i = 0; i < PEASHOOTER_FIRE_INTERVAL_TICKS; i++) {
        game_step(&gs, INPUT_NONE, &ev);
    }
    assert(ev.flags & PVZ_AUDIO_EVENT(PVZ_SFX_PEA_FIRE));
    printf("PASS: PEA_FIRE emitted\n");
}
```

  **NOTE:** The exact placement API, fire-interval constant name, and
  helper signatures come from the existing `sw/game.h` / `sw/game.c` —
  the subagent should grep for them in that file before writing the test.
  (`PEASHOOTER_FIRE_INTERVAL_TICKS` is representative; the actual name may
  differ.) Add the test function's call to the `main()` runner at the
  bottom of `test_game.c` (next to the other `test_*()` calls).

  Then run:
  ```bash
  make test_game && ./test_game
  ```
  Expected: failure in `test_pea_fire_emits_event` because `game.c`
  doesn't emit the bit yet.

- [ ] **Step 5: Implement `PEA_FIRE` emission in `game.c`.** Find the block that spawns a pea (grep for `projectile` or `pea_spawn`) and add inside the success path:

```c
if (ev) ev->flags |= PVZ_AUDIO_EVENT(PVZ_SFX_PEA_FIRE);
```

  Re-run the test:
  ```bash
  make test_game && ./test_game
  ```
  Expected: PASS.

- [ ] **Step 6: Commit.**

  ```bash
  git add sw/game.c sw/test/test_game.c
  git commit -m "audio: PEA_FIRE event"
  ```

- [ ] **Step 7: Repeat Steps 4–6 for each remaining cue.** Follow the same
  pattern (test → fail → implement → pass → commit). Use the spec's
  Section 6 emission table as the source of truth for what each cue's
  trigger condition is:

  - `PEA_HIT` — set inside the pea×zombie collision handler, before the
    pea slot is freed. Test: place a pea and a zombie in overlap, step
    once, assert bit set.
  - `ZOMBIE_BITE` — set **once** per bite-interval tick in the
    zombie-eats-plant loop. Test: step past 3 bite intervals, assert
    exactly 3 events (need to accumulate across steps).
  - `ZOMBIE_DEATH` — set when a zombie's HP reaches ≤ 0. Test: zombie at
    1 HP, pea hits, assert both `PEA_HIT` and `ZOMBIE_DEATH` set.
  - `PLANT_PLACE` — set inside `place_plant()` success path. Test: call
    `place_plant()` on an empty grid cell, assert bit set.
  - `WAVE_START` — set in the wave-manager when the next wave is
    dispatched. Test: advance the wave timer past its threshold, assert
    bit set on the transition.

  One commit per cue: `audio: PEA_HIT event`, `audio: ZOMBIE_BITE event`,
  `audio: ZOMBIE_DEATH event`, `audio: PLANT_PLACE event`,
  `audio: WAVE_START event`.

- [ ] **Step 8: TDD cycle — caller-clears contract test.** Add:

```c
static void test_audio_events_cleared_by_caller(void) {
    game_state_t gs;
    game_init(&gs);
    audio_events_t ev;
    ev.flags = 0xDEADBEEF;     /* garbage the caller forgot to clear */
    game_step(&gs, INPUT_NONE, &ev);
    /* game_step must wipe flags to 0 before it starts emitting. */
    /* Idle step should produce no events. */
    assert((ev.flags & ~0u) == 0);
    printf("PASS: audio events cleared by game_step\n");
}
```

  Add to `main()` runner. Run test. If it fails, verify Step 1's
  `ev->flags = 0` line is present at the top of `game_step()` (it was
  added there; this test is the regression guard).

  Commit: `audio: caller-clears events contract test`.

---

### Task 4.5 — `sw/main.c` state-edge BGM tracker + SFX fan-out

**Files:**
- Modify: `sw/main.c`

**Dependency:** Tasks 4.2, 4.3, 4.4.

- [ ] **Step 1: Inspect `main.c`** to find the main loop:

  ```bash
  grep -n "game_step\|STATE_PLAYING\|main.c" /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine/sw/main.c | head -20
  ```

- [ ] **Step 2: Add `#include "audio.h"`** at the top of `main.c` (next to the existing `#include "render.h"` etc.).

- [ ] **Step 3: Call `audio_init()` at startup** next to the existing render/input init calls:

```c
    if (audio_init() < 0) {
        fprintf(stderr, "audio_init failed, continuing without audio\n");
        /* Don't hard-fail: the game is still playable silent. */
    }
```

- [ ] **Step 4: Add the state-edge tracker and SFX fan-out inside the main
  loop.** Around the existing `game_step()` call, wrap it like this (exact
  placement depends on the current code — the change is "declare `ev` and
  `last_state`, pass `&ev`, fan-out after the call"):

```c
    static pvz_state_t last_state = 0;   /* whatever the boot state is */
    audio_events_t ev = { .flags = 0 };

    game_step(&gs, action, &ev);

    /* BGM state-edge tracker */
    if (gs.state != last_state) {
        if (gs.state == STATE_PLAYING) {
            audio_bgm_start();
        } else if (last_state == STATE_PLAYING) {
            audio_bgm_stop();
        }
        if (last_state == STATE_PLAYING && gs.state == STATE_LOSE)
            audio_play_sfx(PVZ_SFX_GAME_OVER);
        if (last_state == STATE_PLAYING && gs.state == STATE_WIN)
            audio_play_sfx(PVZ_SFX_VICTORY);
        last_state = gs.state;
    }

    /* Fan-out in-game SFX events (cues 1..6). */
    for (pvz_sfx_cue_t c = PVZ_SFX_PEA_FIRE; c <= PVZ_SFX_WAVE_START; c++) {
        if (ev.flags & PVZ_AUDIO_EVENT(c)) audio_play_sfx(c);
    }
```

- [ ] **Step 5: Add `audio_close()` before the existing exit path.**

- [ ] **Step 6: Update the Makefile `pvz` target** to link `audio.c`:

```makefile
pvz: main.c game.c render.c input.c audio.c pvz.h game.h render.h input.h audio.h
	gcc -Wall -O2 -o pvz main.c game.c render.c input.c audio.c -lpthread
```

- [ ] **Step 7: Build and smoke-test compilation.**

  ```bash
  cd /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine/sw
  make pvz 2>&1 | tail -10
  ```
  Expected: clean build, no warnings about unused vars or undefined funcs.

- [ ] **Step 8: Commit.**

  ```bash
  git add sw/main.c sw/Makefile
  git commit -m "audio: main.c bgm state-edge tracker and sfx fan-out"
  ```

---

### Task 4.6 — `sw/test/test_audio.c` standalone smoke test

**Files:**
- Create: `sw/test/test_audio.c`
- Modify: `sw/Makefile` (add `test_audio` target)

**Dependency:** Tasks 4.1, 4.2.

- [ ] **Step 1: Write `sw/test/test_audio.c`.**

```c
/*
 * test_audio — standalone smoke test for /dev/pvz_audio.
 * Usage:
 *   ./test_audio            — play BGM for 3 s, stop, exit.
 *   ./test_audio --sfx N    — trigger SFX cue N (1..8), then exit.
 */
#include "../audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (audio_init() < 0) {
        fprintf(stderr, "audio_init failed\n");
        return 1;
    }

    if (argc >= 3 && strcmp(argv[1], "--sfx") == 0) {
        int cue = atoi(argv[2]);
        if (cue < 1 || cue > 8) {
            fprintf(stderr, "sfx cue must be 1..8\n");
            audio_close();
            return 1;
        }
        audio_play_sfx((pvz_sfx_cue_t)cue);
        sleep(1);
    } else {
        printf("Starting BGM for 3 seconds...\n");
        audio_bgm_start();
        sleep(3);
        audio_bgm_stop();
        printf("BGM stopped.\n");
    }

    audio_close();
    return 0;
}
```

- [ ] **Step 2: Add `test_audio` to the Makefile.** Add near the other `test_*` rules:

```makefile
test_audio: test/test_audio.c audio.c audio.h pvz.h pvz_audio_driver.h
	gcc -Wall -O2 -o test_audio test/test_audio.c audio.c
```

  And add `test_audio` to the `default:` target list:

```makefile
default: module pvz test_shapes test_input test_input_devices test_game test_audio
```

  And to the `clean:` target's `${RM}` list.

- [ ] **Step 3: Compile.**

  ```bash
  cd /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine/sw
  make test_audio 2>&1 | tail -5
  ```
  Expected: `test_audio` binary produced.

- [ ] **Step 4: Commit.**

  ```bash
  git add sw/test/test_audio.c sw/Makefile
  git commit -m "audio: standalone test_audio smoke program"
  ```

---

## Chunk 5 — Asset pipeline

### Task 5.1 — `hw/audio/manifest.json`

**Files:**
- Create: `hw/audio/manifest.json`

**Dependency:** none.

- [ ] **Step 1: Write `hw/audio/manifest.json`.**

```json
{
  "format": {
    "sample_rate_hz": 8000,
    "bits_per_sample": 16,
    "channels": 1
  },
  "bgm": {
    "file": "assets/bgm_theme.wav"
  },
  "sfx": [
    { "cue_id": 1, "name": "PEA_FIRE",     "file": "assets/sfx_pea_fire.wav" },
    { "cue_id": 2, "name": "PEA_HIT",      "file": "assets/sfx_pea_hit.wav" },
    { "cue_id": 3, "name": "ZOMBIE_BITE",  "file": "assets/sfx_zombie_bite.wav" },
    { "cue_id": 4, "name": "ZOMBIE_DEATH", "file": "assets/sfx_zombie_death.wav" },
    { "cue_id": 5, "name": "PLANT_PLACE",  "file": "assets/sfx_plant_place.wav" },
    { "cue_id": 6, "name": "WAVE_START",   "file": "assets/sfx_wave_start.wav" },
    { "cue_id": 7, "name": "GAME_OVER",    "file": "assets/sfx_game_over.wav" },
    { "cue_id": 8, "name": "VICTORY",      "file": "assets/sfx_victory.wav" }
  ],
  "silent_samples_per_missing_cue": 2400
}
```

- [ ] **Step 2: Commit.**

  ```bash
  git add hw/audio/manifest.json
  git commit -m "audio: asset manifest"
  ```

---

### Task 5.2 — `hw/audio/build_audio_mif.py` generator

**Files:**
- Create: `hw/audio/build_audio_mif.py`

**Dependency:** Task 5.1.

- [ ] **Step 1: Write the generator** at `hw/audio/build_audio_mif.py`.

```python
#!/usr/bin/env python3
"""
build_audio_mif.py — turn manifest.json + WAV assets into:
  - hw/bgm_rom.mem
  - hw/sfx_rom.mem
  - hw/sfx_offsets.svh

Stdlib-only (wave, struct, json, pathlib).

Contributors pre-process their source audio to 8 kHz 16-bit mono WAV before
putting files in assets/. This script refuses any WAV that doesn't match.

Missing SFX WAVs are filled with silence (manifest.silent_samples_per_missing_cue
zero samples) so the output files are always a consistent size — this is what
makes Scope A (no SFX assets yet) a purely asset-less build.
"""
import json
import struct
import sys
import wave
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent    # hw/audio
HW_DIR     = SCRIPT_DIR.parent                  # hw/

EXPECTED_RATE    = 8000
EXPECTED_WIDTH   = 2           # 16-bit
EXPECTED_CHANNELS = 1


def read_pcm16_mono(path: Path) -> list[int]:
    """Read a WAV file; return a list of signed int16 samples. Validates format."""
    with wave.open(str(path), "rb") as w:
        if w.getframerate() != EXPECTED_RATE:
            sys.exit(f"ERROR: {path}: sample rate {w.getframerate()} != {EXPECTED_RATE}")
        if w.getsampwidth() != EXPECTED_WIDTH:
            sys.exit(f"ERROR: {path}: sample width {w.getsampwidth()} != {EXPECTED_WIDTH}")
        if w.getnchannels() != EXPECTED_CHANNELS:
            sys.exit(f"ERROR: {path}: channels {w.getnchannels()} != {EXPECTED_CHANNELS}")
        raw = w.readframes(w.getnframes())
    n = len(raw) // EXPECTED_WIDTH
    return list(struct.unpack(f"<{n}h", raw))


def write_mem(path: Path, samples: list[int]) -> None:
    """Write one 16-bit hex per line, two's-complement."""
    with open(path, "w") as f:
        for s in samples:
            if s < 0:
                s = s + (1 << 16)
            f.write(f"{s:04x}\n")


def main() -> None:
    manifest_path = SCRIPT_DIR / "manifest.json"
    manifest = json.loads(manifest_path.read_text())

    silent_len = manifest.get("silent_samples_per_missing_cue", 2400)

    # --- BGM ---
    bgm_file = SCRIPT_DIR / manifest["bgm"]["file"]
    if bgm_file.exists():
        bgm_samples = read_pcm16_mono(bgm_file)
        print(f"BGM: {bgm_file.name} = {len(bgm_samples)} samples "
              f"({len(bgm_samples)/EXPECTED_RATE:.2f} s)")
    else:
        bgm_samples = [0] * silent_len
        print(f"BGM: {bgm_file.name} MISSING, using {silent_len} silent samples")
    write_mem(HW_DIR / "bgm_rom.mem", bgm_samples)

    # --- SFX ---
    sfx_samples: list[int] = []
    sfx_start: list[int] = [0] * 9    # 1-indexed, index 0 unused
    sfx_end:   list[int] = [0] * 9

    # Sort cues by cue_id to pack deterministically
    cues = sorted(manifest["sfx"], key=lambda c: c["cue_id"])
    for cue in cues:
        cid = cue["cue_id"]
        if not (1 <= cid <= 8):
            sys.exit(f"ERROR: manifest cue_id {cid} out of range 1..8")
        f = SCRIPT_DIR / cue["file"]
        if f.exists():
            samples = read_pcm16_mono(f)
        else:
            samples = [0] * silent_len
            print(f"SFX cue {cid} ({cue['name']}): MISSING, using {silent_len} silent samples")

        start = len(sfx_samples)
        end   = start + len(samples) - 1
        sfx_start[cid] = start
        sfx_end[cid]   = end
        sfx_samples.extend(samples)

    # Fill unused cue ids with start=end=0
    write_mem(HW_DIR / "sfx_rom.mem", sfx_samples if sfx_samples else [0])
    print(f"SFX total: {len(sfx_samples)} samples ({len(sfx_samples)/EXPECTED_RATE:.2f} s)")

    # --- Offsets include ---
    with open(HW_DIR / "sfx_offsets.svh", "w") as f:
        f.write("/* AUTOGENERATED by hw/audio/build_audio_mif.py. DO NOT EDIT. */\n")
        f.write("parameter logic [14:0] SFX_START [1:8] = '{")
        f.write(", ".join(f"15'd{sfx_start[i]}" for i in range(1, 9)))
        f.write("};\n")
        f.write("parameter logic [14:0] SFX_END   [1:8] = '{")
        f.write(", ".join(f"15'd{sfx_end[i]}" for i in range(1, 9)))
        f.write("};\n")

    print(f"Wrote {HW_DIR/'bgm_rom.mem'}, {HW_DIR/'sfx_rom.mem'}, {HW_DIR/'sfx_offsets.svh'}")


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Make it executable.**

  ```bash
  chmod +x /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine/hw/audio/build_audio_mif.py
  ```

- [ ] **Step 3: Commit.**

  ```bash
  git add hw/audio/build_audio_mif.py
  git commit -m "audio: asset generator script"
  ```

---

### Task 5.3 — Placeholder BGM WAV + first generator run (Scope A asset)

**Files:**
- Create: `hw/audio/assets/bgm_theme.wav`
- Overwrite: `hw/bgm_rom.mem`
- Overwrite: `hw/sfx_rom.mem`
- Overwrite: `hw/sfx_offsets.svh`

**Dependency:** Task 5.2.

Scope A ships with a placeholder tone-sweep BGM. Real arrangement can replace this file without any other changes.

- [ ] **Step 1: Generate a placeholder tone-sweep WAV** using Python's stdlib (no external audio tools needed).

  Create a tiny helper script `/tmp/make_placeholder_bgm.py`:
  ```python
  import wave, struct, math
  SR=8000; DUR=15.0
  n = int(SR*DUR)
  samples = []
  for i in range(n):
      t = i / SR
      # Frequency sweep 220 Hz → 880 Hz over 15 s, half-amplitude to stay well below clipping
      f = 220.0 + (880.0 - 220.0) * (t / DUR)
      samples.append(int(0.5 * 32767 * math.sin(2 * math.pi * f * t)))
  with wave.open("/homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine/hw/audio/assets/bgm_theme.wav", "wb") as w:
      w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
      w.writeframes(struct.pack(f"<{n}h", *samples))
  print("wrote bgm_theme.wav")
  ```

  Run:
  ```bash
  mkdir -p /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine/hw/audio/assets
  python3 /tmp/make_placeholder_bgm.py
  ```
  Expected: `wrote bgm_theme.wav`.

- [ ] **Step 2: Run the generator.**

  ```bash
  cd /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine/hw
  python3 audio/build_audio_mif.py
  ```
  Expected output includes:
  - `BGM: bgm_theme.wav = 120000 samples (15.00 s)`
  - `SFX cue 1 ... MISSING, using 2400 silent samples` × 8
  - `SFX total: 19200 samples (2.40 s)`
  - `Wrote hw/bgm_rom.mem, hw/sfx_rom.mem, hw/sfx_offsets.svh`

- [ ] **Step 3: Spot-check the generated files.**

  ```bash
  wc -l hw/bgm_rom.mem hw/sfx_rom.mem
  head -5 hw/sfx_offsets.svh
  ```
  Expected: `bgm_rom.mem` has 120000 lines, `sfx_rom.mem` has 19200 lines,
  `sfx_offsets.svh` contains the SFX_START / SFX_END parameter lines.

- [ ] **Step 4: Re-run the HW testbenches** to confirm the new `.mem`
  files don't break tb_pvz_audio (which reads the production
  `bgm_rom.mem`):

  ```bash
  vlib work && vlog mixer.sv bgm_rom.sv sfx_rom.sv voice_bgm.sv voice_sfx.sv pvz_audio.sv tb/tb_pvz_audio.sv
  vsim -do "run -all; quit" work.tb_pvz_audio 2>&1 | tail -10
  ```
  Expected: `TEST PASSED: tb_pvz_audio`.

- [ ] **Step 5: Commit.**

  ```bash
  git add hw/audio/assets/bgm_theme.wav hw/bgm_rom.mem hw/sfx_rom.mem hw/sfx_offsets.svh
  git commit -m "audio: placeholder bgm and regenerated rom files"
  ```

---

### Task 5.4 — `hw/audio/README.md`

**Files:**
- Create: `hw/audio/README.md`

**Dependency:** Tasks 5.1–5.3.

- [ ] **Step 1: Write `hw/audio/README.md`.**

```markdown
# Audio asset pipeline

Source audio lives in `assets/`. The generator `build_audio_mif.py` reads
`manifest.json`, validates format, and emits three files at the project root
(`hw/bgm_rom.mem`, `hw/sfx_rom.mem`, `hw/sfx_offsets.svh`) — all three are
committed to git.

## Required asset format

8 kHz, 16-bit, mono WAV. The generator refuses any other format. Pre-process
your source audio before dropping it here:

```
sox input.mp3 -r 8000 -c 1 -b 16 output.wav
```

Or in Audacity: Tracks → Resample → 8000 Hz; Tracks → Mix → Stereo to Mono;
File → Export → WAV (Microsoft signed 16-bit PCM).

## Regenerating after a change

```
make audio   # (from hw/)
# or directly:
python3 audio/build_audio_mif.py
```

Then `git add hw/bgm_rom.mem hw/sfx_rom.mem hw/sfx_offsets.svh hw/audio/assets/<changed>.wav`
and commit — the three generated files are committed alongside the asset.

## Missing assets

If a SFX WAV is absent, the generator fills its slot with
`silent_samples_per_missing_cue` zero samples (default 2400 = 0.3 s) rather
than failing. This is how Scope A ships: with none of the SFX WAVs present,
`sfx_rom.mem` is 19200 zero-valued samples, the voice modules play silence,
and the end-to-end hardware path is still fully exercised.

## BGM

Scope A ships with a placeholder tone-sweep `bgm_theme.wav` (220 Hz → 880 Hz
over 15 s). Replace it with a real arrangement of the PvZ theme — same
8 kHz / 16-bit / mono format — and re-run the generator.
```

- [ ] **Step 2: Commit.**

  ```bash
  git add hw/audio/README.md
  git commit -m "audio: asset pipeline readme"
  ```

---

### Task 5.5 — `hw/Makefile` `make audio` target

**Files:**
- Modify: `hw/Makefile`

**Dependency:** Task 5.2.

- [ ] **Step 1: Append to `hw/Makefile`** (end of file):

```makefile
# -----------------------------------------------------------
# audio: regenerate bgm_rom.mem + sfx_rom.mem + sfx_offsets.svh from assets
# -----------------------------------------------------------
.PHONY : audio
audio :
	python3 audio/build_audio_mif.py
```

- [ ] **Step 2: Verify.**

  ```bash
  cd /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine/hw
  make audio 2>&1 | tail -5
  ```
  Expected: `Wrote hw/bgm_rom.mem, hw/sfx_rom.mem, hw/sfx_offsets.svh`.

- [ ] **Step 3: Commit.**

  ```bash
  git add hw/Makefile
  git commit -m "audio: make audio target"
  ```

---

## Chunk 6 — Documentation

### Task 6.1 — `CLAUDE.md` updates

**Files:**
- Modify: `CLAUDE.md`

**Dependency:** Chunks 1–5 landed.

Per the spec, update the project-level guide with:
- The new second peripheral `pvz_audio` alongside `pvz_gpu`.
- The new three-way compatible-string tie for `csee4840,pvz_audio-1.0`.
- The new base address `0xff210000` (or whatever Task 2.3 Step 5 recorded).
- The `audio_events_t` event-bus contract (game.c emits → main.c fires ioctls).
- The Scope A convention: silent SFX ROM is intentional; adding SFX is asset-only.
- The `/dev/pvz_audio` ioctls: `PVZ_AUDIO_BGM_CTRL`, `PVZ_AUDIO_PLAY_SFX`, `PVZ_AUDIO_STATUS`.
- On-board debugging additions: `insmod pvz_audio_driver.ko`; `/dev/pvz_audio` should exist; `dmesg | grep pvz_audio`; the `ff21xxxx` region should appear in `/proc/iomem`.

- [ ] **Step 1: Edit `CLAUDE.md`.** Under the `## Architecture` section,
  near the existing HPS/FPGA split description, add a paragraph:

```markdown
**Audio (Scope A shipped via `v3-audio-engine`):** A second Avalon-slave
peripheral `pvz_audio` at `0xff210000` (separate from `pvz_gpu`'s
`0xff200000`) plays a looping PvZ theme song over the WM8731 codec
during `STATE_PLAYING`. Hardware path: `voice_bgm` + 2× `voice_sfx`
feed a 3-input clamp `mixer`, then the Intel `altera_up_avalon_audio`
IP streams to the codec over I²S. BGM samples live in `bgm_rom.mem`;
SFX samples in `sfx_rom.mem`; both are `$readmemh`-loaded BRAMs.
`sfx_rom.mem` currently contains zero samples (silent SFX) — adding
audible SFX is an asset-only change via `hw/audio/build_audio_mif.py`
(see `hw/audio/README.md`).
```

  Under `## Critical invariants`, add:

```markdown
- **Three-way compatible-string tie (second peripheral).** Like `pvz_gpu`:
  `hw/pvz_audio_hw.tcl`'s `embeddedsw.dts.compatible "csee4840,pvz_audio-1.0"`,
  the generated dtb, and `sw/pvz_audio_driver.c`'s `of_device_id` must
  agree character-for-character.
- **Audio event bus.** `game_step()` takes an `audio_events_t *out`. Caller
  zeroes `flags` before the call (`game_step` also clears defensively).
  game.c sets bits for in-game events (PEA_FIRE/PEA_HIT/ZOMBIE_BITE/
  ZOMBIE_DEATH/PLANT_PLACE/WAVE_START); main.c fans them out to
  `audio_play_sfx()` after each step. main.c emits GAME_OVER/VICTORY
  directly on state-edge transitions, not via game.c.
- **Scope A silent SFX ROM.** `sfx_rom.mem` is populated with zero samples
  on purpose. The full ioctl → driver → register → voice → mixer path is
  exercised; making SFX audible is a matter of running
  `make audio` with real WAVs in `hw/audio/assets/` and committing the
  regenerated `.mem` + `.svh` files.
```

  Under the existing on-board debugging block, add:

```bash
  dmesg | grep pvz_audio             # audio driver probe messages
  ls /dev/pvz_audio                  # should exist after insmod
  grep ff21 /proc/iomem              # audio peripheral region
```

- [ ] **Step 2: Commit.**

  ```bash
  git add CLAUDE.md
  git commit -m "audio: CLAUDE.md updates"
  ```

---

### Task 6.2 — `design-document.tex` audio section rewrite

**Files:**
- Modify: `doc/design-document/design-document.tex`

**Dependency:** Chunks 1–5 landed.

The existing `\subsection{Audio Playback (Planned)}` section becomes the real
spec, matching what actually shipped. The section currently reads "(Planned)"
in its title and describes a design where SFX are mixed with BGM by clamping
sums — which IS what we built, so the text is mostly correct; we're just
dropping the "(Planned)" label and firming up specifics.

- [ ] **Step 1: Open and edit the LaTeX file.** Change the subsection
  title and fill in what actually shipped:

  - Change `\subsection{Audio Playback (Planned)}` → `\subsection{Audio Playback}`
  - Under "Configuration": confirm the "8 kHz, 16-bit" numbers match what we picked.
  - Under "Playback": change "The wave ROM holds a looping background track
    and a handful of short sound-effect clips." to specify the two-BRAM
    layout (`bgm_rom` + `sfx_rom`), the three voices, and the 3-input
    clamp mixer.
  - Add a paragraph about the register map: reference `pvz_audio`'s
    four registers and the state-edge BGM lifecycle.
  - Add a paragraph explaining the scope A convention: SFX path is fully
    wired but `sfx_rom.mem` is silent until real assets ship.

  Exact text is left to the author's discretion — but it must match the spec
  at `docs/superpowers/specs/2026-04-23-audio-engine-design.md` Sections 3–6.

- [ ] **Step 2: Rebuild the PDF** (if a Makefile target exists; otherwise skip):

  ```bash
  cd /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine/doc/design-document
  ls Makefile 2>/dev/null && make || echo "no doc Makefile; skip PDF rebuild"
  ```

- [ ] **Step 3: Commit.**

  ```bash
  git add doc/design-document/design-document.tex doc/design-document/design-document.pdf 2>/dev/null
  git commit -m "audio: design document audio section rewrite"
  ```

---

### Task 6.3 — `doc/v4-changes.md` summary

**Files:**
- Create: `doc/v4-changes.md`

**Dependency:** Chunks 1–5 landed.

Mirror the style of `doc/v2-changes.md` and `doc/v3-changes.md`.

- [ ] **Step 1: Write `doc/v4-changes.md`.**

```markdown
# v4 — Audio Engine

This branch (`v3-audio-engine`) adds a PCM audio engine to the project. v3 ships
gameplay on the controller path; v4 adds a looping PvZ theme song over the
3.5 mm jack and an end-to-end SFX path that is wired through the hardware
but currently silent (the SFX ROM is zero-filled until real sound effects
are dropped in).

## What's new on the hardware

- Second Avalon-slave peripheral `pvz_audio` at `0xff210000`, independent
  from `pvz_gpu`.
- Three voice modules (`voice_bgm`, 2× `voice_sfx`) feeding a 3-input clamp
  `mixer`, behind a 4-register Avalon slave (`BGM_CTRL`, `SFX_VOICE0_TRIG`,
  `SFX_VOICE1_TRIG`, `STATUS`).
- Intel `altera_up_avalon_audio*` IP stack configured for 8 kHz / 16-bit /
  stereo; mono content is duplicated to both channels.
- BRAM-backed audio ROMs (`bgm_rom`, `sfx_rom`) loaded from `.mem` files via
  `$readmemh`, same pattern as `sprite_rom`.

## What's new on the software

- Kernel misc-device driver `pvz_audio_driver` exposes `/dev/pvz_audio` with
  three ioctls: `PVZ_AUDIO_BGM_CTRL`, `PVZ_AUDIO_PLAY_SFX`, `PVZ_AUDIO_STATUS`.
  Voice round-robin lives in the driver.
- `sw/audio.{c,h}` userspace wrapper parallels `sw/render.{c,h}`.
- `game_step()` grew an `audio_events_t *` output parameter. `game.c` emits
  six in-game SFX cues as bit flags; `main.c` fans them out to ioctls after
  each step.
- `main.c` edge-detects `STATE_PLAYING` transitions to drive BGM
  start/stop and to fire `GAME_OVER` / `VICTORY` cues.
- Seven new assertions in `test_game.c` covering each event emission point
  plus the caller-clears contract.

## What's new in tooling

- `hw/audio/` directory: `manifest.json`, `build_audio_mif.py` (stdlib-only
  Python generator), `README.md`, and `assets/`.
- `make audio` convenience target in `hw/Makefile`.
- Placeholder `bgm_theme.wav` (220 Hz → 880 Hz tone sweep, 15 s) ships
  until a real arrangement is produced.

## Scope A vs Scope C

This feature is Scope A of a two-step delivery:

- **Scope A (shipped):** BGM loops during `STATE_PLAYING`. SFX path wired
  end-to-end but `sfx_rom.mem` is zero-filled, so SFX ioctls execute
  silently.
- **Scope C (future, asset-only):** Populate 8 real SFX WAVs in
  `hw/audio/assets/`, run `make audio`, commit the regenerated `.mem` +
  `.svh` files. No hardware, driver, or game-logic change needed — every
  hook point is already exercised by Scope A.

## Board verification

Any on-board gaps are logged in `doc/v4-known-issues.md` per the v3
convention.
```

- [ ] **Step 2: Commit.**

  ```bash
  git add doc/v4-changes.md
  git commit -m "audio: v4 changes document"
  ```

---

## Chunk 7 — Test-before-ship gate + push

### Task 7.1 — Run the full relevant test suite

**Files:** none.

**Dependency:** Chunks 1–6 landed.

Per `feedback_test_before_ship.md`: full suite, not just the chunks you touched.

- [ ] **Step 1: Run every ModelSim testbench.**

  ```bash
  cd /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine/hw
  vlib work
  for tb in mixer voice_bgm voice_sfx pvz_audio bg_grid linebuffer shape_renderer vga_counters pvz_top; do
      vlog *.sv tb/tb_$tb.sv 2>&1 | grep -E "Error|Warning" || true
      vsim -do "run -all; quit" work.tb_$tb 2>&1 | tail -5
  done
  ```
  Expected: every `tb_*` reports `TEST PASSED`. No `Error` lines in vlog output.

  If any existing testbench (bg_grid / linebuffer / shape_renderer /
  vga_counters / pvz_top) fails, investigate immediately — that's an
  integration break caused by something in this branch, not test flake.

- [ ] **Step 2: Run host-side game tests.**

  ```bash
  cd /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine/sw
  make test_game && ./test_game
  ```
  Expected: all existing tests pass AND all 7 new assertions pass.

- [ ] **Step 3: Run host-side compile of the full game.**

  ```bash
  make pvz && file pvz
  ```
  Expected: `pvz` is a x86-64 ELF (host build). If the workstation has the
  ARM toolchain, also `make CC=arm-linux-gnueabihf-gcc ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- KERNEL_SOURCE=... module` and verify both `pvz_driver.ko` and `pvz_audio_driver.ko` build.

- [ ] **Step 4: Run the full HW build.**

  ```bash
  cd /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine/hw
  make qsys && make quartus && make rbf && make dtb 2>&1 | tail -10
  ```
  Expected: clean build. Confirm `.rbf`, `.dtb` produced.

- [ ] **Step 5: If any of the above fails** — fix the root cause, DO NOT
  mark the gate as passed. If hardware or ModelSim is unavailable (not
  installed on this workstation), create or append to
  `doc/v4-known-issues.md` per the v3 format:

```markdown
## <what was not verified>
- **Severity**: minor
- **Area**: <hw / driver / ...>
- **Found in phase**: Chunk 7 test-before-ship gate
- **Reproduction**: <what's needed to verify — e.g. "Columbia workstation with ModelSim">
- **Proposed fix**: <what to run>
- **Status**: open
```

  The gate is considered "passed with knowns" when all failures are
  environment-limited (not code-defects), each is logged, and no failures
  are severity=blocker.

- [ ] **Step 6: Commit any known-issues log updates.**

  ```bash
  git add doc/v4-known-issues.md 2>/dev/null
  git diff --cached --stat
  git commit -m "audio: v4 known issues ledger" 2>/dev/null || echo "nothing to log"
  ```

---

### Task 7.2 — Push the feature branch

**Files:** none.

**Dependency:** Task 7.1.

- [ ] **Step 1: Final log review.**

  ```bash
  git -C /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine log --oneline -40
  ```
  Expected: the v3-audio-engine branch has the full chain starting from
  `audio: design spec` through the Chunk 1..6 commits plus the known-issues
  commit (if created).

- [ ] **Step 2: Push.**

  ```bash
  git -C /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine push -u origin v3-audio-engine
  ```

- [ ] **Step 3: Report the branch URL to the user.** The remote is
  `nyavana/pvz-fpga`; surface
  `https://github.com/nyavana/pvz-fpga/tree/v3-audio-engine` (or whatever `gh
  browse` prints) so the user can review and merge.

---

### Task 7.3 — Offer merge to main and worktree cleanup

**Files:** none.

**Dependency:** Task 7.2.

- [ ] **Step 1: Prompt the user** whether to open a PR (recommended) or
  merge directly to main. Do not take the merge action without explicit
  confirmation — pushing to main is a shared-state change per the system
  instructions.

- [ ] **Step 2: If user approves a PR**, run:

  ```bash
  gh pr create --title "audio: PCM audio engine" --body "Implements Scope A of the audio engine spec at docs/superpowers/specs/2026-04-23-audio-engine-design.md. BGM plays during STATE_PLAYING; SFX path wired end-to-end but silent until Scope C assets land. Full spec and test plan in-branch."
  ```

- [ ] **Step 3: If user wants the worktree removed after merge:**

  ```bash
  git -C /homes/user/stud/spring26/cy2822/4840-final/pvz-fpga worktree remove /homes/user/stud/spring26/cy2822/4840-final/worktrees/v3-audio-engine
  ```
  **Do not run this without explicit user confirmation** — the worktree
  holds real work and removing it before merge would lose it.

---

## Self-review summary

- **Spec coverage:** Every requirement in Sections 3–12 of the spec maps to a
  numbered task here. Section 3 (architecture) → Tasks 1.1–1.5 + 2.1–2.5;
  Section 4 (module details) → Tasks 1.1–1.5; Section 5 (registers + driver)
  → Tasks 2.2 + 3.1–3.3; Section 6 (game integration) → Tasks 4.1–4.5;
  Section 7 (asset pipeline) → Tasks 5.1–5.5; Section 8 (testing) → testbenches
  are embedded in Tasks 1.1, 1.3, 1.4, 2.5; host tests in 4.4; on-board smoke
  in 4.6; full-suite gate in 7.1; Section 9 (ship list) → every "Files created
  / modified" block across chunks; Section 10 (success criteria) → 7.1;
  Section 11 (risks) → Task 2.6 utilization check, Task 5.3 placeholder BGM,
  Task 2.3 Step 5 address record; Section 12 (workflow) → header + Task 7.x.
- **Placeholder scan:** No `TBD` / `TODO` / `implement later` phrasing.
  Two places ask the subagent to grep existing code for the exact symbol
  name (Task 4.4 Step 4 `PEASHOOTER_FIRE_INTERVAL_TICKS` — representative
  constant; Task 4.5 Step 1 main loop locator) — these are navigation
  instructions, not missing content.
- **Type consistency:** `audio_events_t.flags` is `uint32_t` everywhere
  (game.h, main.c, test_game.c). `pvz_sfx_cue_t` is defined once in
  pvz.h and referenced consistently. The register names (`BGM_CTRL`,
  `SFX_VOICE0_TRIG`, `SFX_VOICE1_TRIG`, `STATUS`) match between the SV
  comment in `pvz_audio.sv`, the TCL in `pvz_audio_hw.tcl`, and the C
  driver. `PVZ_AUDIO_*` ioctl names match between header and wrapper.

## Execution handoff

**Not invoked this session — the user has opted to execute in a future
session.** When execution begins, use `superpowers:subagent-driven-development`
(recommended) to dispatch one subagent per task in isolated worktrees,
reviewing between tasks. The six chunks can be coordinated as follows:

- Session opens → dispatch Chunks 1, 3, 4, 5 **in parallel** (one message,
  four `Agent` tool calls with `isolation: "worktree"`).
- When Chunk 1 returns and is reviewed, dispatch Chunk 2.
- When Chunks 1–5 all return and are reviewed, dispatch Chunk 6.
- When Chunk 6 returns, run Chunk 7 in the parent session (test-before-ship
  gate + push).

Alternatively, `superpowers:executing-plans` executes inline in one session
with checkpoints — simpler but loses the parallelism benefit.
