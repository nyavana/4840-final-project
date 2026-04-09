## Context

This is a greenfield CSEE4840 final project building a Plants vs Zombies game on the DE1-SoC (Cyclone V). The board provides an ARM HPS running Linux, an FPGA fabric, and a lightweight HPS-to-FPGA bridge at `0xFF200000`. The lab3 reference design demonstrates the pattern: SystemVerilog Avalon-MM peripheral + kernel driver + userspace program. The bubble-bobble reference project demonstrates a more complex sprite/linebuffer display engine.

**Current state:** Only documentation exists. No hardware or software source files have been written.

**Constraints:**
- Cyclone V FPGA on-chip SRAM is limited (~400 KB total M10K blocks) — a full 640x480x8bpp framebuffer (300 KB) is feasible but leaves little room. Linebuffer approach preferred.
- The Quartus/Platform Designer/dtb toolchain runs on Columbia `micro*.ee.columbia.edu` workstations.
- USB gamepad support is deferred to post-MVP; MVP uses keyboard via `/dev/input/eventX`.
- Sprites and audio are deferred to post-MVP; MVP uses primitive shapes (rectangles, circles, 7-seg digits).

## Goals / Non-Goals

**Goals:**
- Playable single-level PvZ demo: place peashooters, defend against 5 zombies, win/lose conditions
- Hardware-accelerated VGA display at 640x480@60Hz with no tearing (vsync-latched shadow registers)
- Clean HW/SW split: FPGA handles all rendering, HPS handles all game logic
- Reusable foundation for post-MVP sprite engine, audio, and USB gamepad extensions
- Follows lab3 build and driver patterns so the team can work from familiar ground

**Non-Goals:**
- Sprite-based graphics (post-MVP)
- Audio playback via Wolfson codec (post-MVP)
- USB gamepad input (post-MVP)
- Multiple plant types (post-MVP — MVP is peashooter only)
- Scrolling or camera movement
- Network multiplayer

## Decisions

### 1. Linebuffer rendering vs full framebuffer

**Decision:** Dual linebuffer (two 640x8-bit line RAMs, swap on hsync).

**Alternatives considered:**
- Full 640x480x8bpp framebuffer in on-chip SRAM: simpler software model but consumes ~300 KB of M10K, leaving almost nothing for shape tables, palette, and future sprite ROMs.
- Tile-based rendering: more complex, better for scrolling games — unnecessary here since PvZ grid is static.

**Rationale:** Linebuffer uses only ~1.3 KB of SRAM, leaves ample room for future sprite ROMs, and the bubble-bobble reference proves the pattern works. Each scanline has ~1600 pixel-clock cycles — enough to fill background (640 cycles) and render all 48 shapes (~960 cycles).

### 2. Shape table approach (48 entries) vs per-pixel software rendering

**Decision:** Hardware maintains a 48-entry shape table; software writes descriptors (type, position, size, color), hardware rasterizes per-scanline.

**Alternatives considered:**
- Software renders pixel-by-pixel into a framebuffer: flexible but too slow over the lightweight bridge (writing 307,200 bytes/frame at ~1 byte/cycle would take >5 ms, eating the 16.7 ms frame budget).
- Tile map only: insufficient for moving objects (zombies, projectiles).

**Rationale:** 48 entries is enough for MVP (4x8 grid plants + 5 zombies + 16 projectiles + HUD elements = ~45 worst case). Software writes only ~5 registers per shape (~240 writes/frame), taking <0.1 ms. Hardware does the heavy lifting per-scanline.

### 3. Shadow/active double-buffering at vsync

**Decision:** All shape table and background grid registers are shadow-latched — CPU writes go to shadow registers; active registers update atomically at `vcount == 480, hcount == 0`.

**Rationale:** Prevents mid-frame tearing. This is exactly the lab3 `vga_ball.sv` pattern (lines 36-63). Well-understood and proven.

### 4. Register interface layout

**Decision:** Five Avalon registers at word-aligned offsets (0x00-0x10) using 32-bit writedata:
- `BG_CELL` (0x00): background grid cell color write
- `SHAPE_ADDR` (0x04): select shape index
- `SHAPE_DATA0` (0x08): shape type, visibility, position
- `SHAPE_DATA1` (0x0C): shape dimensions and color
- `SHAPE_COMMIT` (0x10): commit selected shape to shadow table

**Alternatives considered:**
- Single flat register per shape field: too many registers (48 shapes x 7 fields = 336).
- Packed writes with auto-increment: simpler software but harder to debug with SignalTap.

**Rationale:** Address-then-data pattern is a standard Avalon idiom. 5 registers keeps the decode logic trivial. The commit register ensures atomic shape updates (write all fields, then commit).

### 5. Kernel driver as misc device with ioctls

**Decision:** Follow the lab3 `vga_ball.c` pattern exactly — misc device at `/dev/pvz`, three ioctl commands (`PVZ_WRITE_BG`, `PVZ_WRITE_SHAPE`, `PVZ_COMMIT_SHAPES`), driver does `iowrite32` to mapped Avalon registers.

**Alternatives considered:**
- mmap the registers to userspace: faster but bypasses kernel protection and complicates the three-way tie debugging.
- Character device with read/write: more complex for structured data.

**Rationale:** The ioctl pattern is what the course teaches and what the graders expect. It's simple, debuggable, and sufficient for MVP throughput.

### 6. Game loop timing via usleep

**Decision:** 60 Hz game loop using `usleep(16667)` with wall-clock drift correction.

**Alternatives considered:**
- vsync interrupt from FPGA: more precise but requires interrupt wiring through the bridge, which adds complexity for minimal benefit in an MVP.
- Busy-wait polling: wastes CPU.

**Rationale:** `usleep` is good enough — the game is not latency-sensitive at the sub-frame level, and the approach matches the bubble-bobble reference.

## Risks / Trade-offs

- **[Shape count limit]** 48 entries may be tight if future features add more entity types. **Mitigation:** MVP needs ~45 worst case; post-MVP can increase table size or switch to sprite engine.

- **[Timing budget per scanline]** 48 shapes at ~20 cycles each = 960 cycles. If shape rendering takes longer (e.g., circle distance calculation), lines could be missed. **Mitigation:** Circle rendering uses `(dx^2 + dy^2 <= r^2)` which is 2 multiplies + 1 add + 1 compare — fits in ~4 cycles pipelined. Budget has ~640 cycles of slack.

- **[Three-way tie breakage]** The `pvz_top.sv` ↔ `pvz_top_hw.tcl` ↔ `pvz_driver.c` compatible string must stay in sync. A mismatch silently prevents the driver from binding. **Mitigation:** Use a single canonical name `csee4840,pvz_gpu-1.0` everywhere; document in the Makefile header.

- **[Avalon address confusion]** 32-bit writedata means Avalon uses word offsets internally, but CPU uses byte offsets. **Mitigation:** Driver always uses `iowrite32(val, base + 4*N)` with byte offsets; document the mapping in `pvz.h`.

- **[Keyboard input latency]** Reading `/dev/input/eventX` in the game loop could block. **Mitigation:** Use non-blocking reads (`O_NONBLOCK`) or a separate input thread.

- **[BRAM read pipeline]** Linebuffer read has 1-2 cycle latency. **Mitigation:** Read address leads display address by 2 cycles (read `hcount+2`, output at `hcount`), following the bubble-bobble pattern.
