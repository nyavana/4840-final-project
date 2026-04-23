## Context

The v2Dino baseline delivers a tearing-free 640×480 display pipeline, a 48-entry shape table with shadow/active latching at vsync, a Linux misc-device driver, and a 60 Hz HPS game loop covering one plant type, one zombie type, and keyboard input. Everything above the Avalon register interface is straightforward C. The FPGA side is small enough that this design document focuses on where v3 touches it and how each touch stays inside v2's invariants rather than rewriting architecture.

Key v2 invariants that v3 must preserve (see `CLAUDE.md`):
- The three-way compatible-string tie between `pvz_top_hw.tcl`, the dtb, and `pvz_driver.c`.
- Word-addressed Avalon writes on the FPGA, byte-offset writes on the CPU.
- Shadow/active latching of every display-affecting register at `vcount == 480 && hcount == 0`.
- Painter's-algorithm z-order: shape slot `i` draws on top of slot `j` when `i > j`. HUD must stay above entities.
- BRAM read latency absorption in `shape_renderer.sv`'s `sprite_pending` path; the `0xFF` transparency sentinel suppresses the commit.
- 2× sprite downscale via `{sprite_local_y[5:1], sprite_local_x[5:1]}`.

v3 does not change any of these.

## Goals / Non-Goals

**Goals:**
- Three plants (Peashooter, Sunflower, Wall-nut) selectable from a top-HUD plant bar.
- Three zombies (Basic, Conehead, Buckethead) spawned from a precomputed wave table with per-entry jitter.
- Xbox 360–compatible USB gamepad support via evdev, auto-detected at startup with keyboard fallback.
- Test coverage: new unit tests in `test_game.c`, extended ModelSim TBs covering indices ≥ 48, a new on-board `test_input_devices` utility, explicit gate criteria per implementation phase.
- Known-issues discipline: anything broken and not fixed inside the branch is explicitly logged in `doc/v3-known-issues.md` before push.

**Non-Goals:**
- Sprite art for Sunflower, Wall-nut, Conehead, Buckethead. These use color-coded primitive shapes; new sprite ROMs come in a follow-up.
- Multi-wave level progression or a level-select screen. v3 ships one expanded wave.
- Audio. The Wolfson codec integration is still out of scope.
- 5×9 grid expansion. The proposal mentions 5×9 but v2 ships 4×8 and the `BG_CELL` register only has 2 row bits and 3 column bits; widening the grid requires a separate change.
- Cost labels on plant cards. Three extra digit slots per card inflates the HUD budget by 9 entries; cost is documented instead.
- Simultaneous use of both controller and keyboard. Priority auto-detect at startup keeps the input layer simple; multi-device fusion can be a follow-up.

## Decisions

### Shape-table growth (48 → 64)

**Decision.** Enlarge `shape_table.sv` to 64 entries and update `shape_renderer.sv`'s iteration bound to 64. Rebudget `render.c` shape-slot allocation.

**Why.** The new HUD needs plant cards and a selection highlight overlay on top of the existing sun digits and cursor. Slots 0–31 are per-cell plants (deterministic z-order we don't want to touch). Slots 32–36 remain zombies. Peas stay at 6 slots (37–42) matching v2; that is enough for the observed peak (roughly one pea in flight per firing row). Slot 43 is the plant-card selection highlight (lower index so the cards paint on top). Slots 44–46 are the three plant cards. Slots 47–50 are sun digits (4 digits → up to 9999 sun, safely above the 150-sun income a full Sunflower run produces per level). Slots 51–62 stay reserved for a later change that might add per-zombie accessories (cone, bucket), a zombies-remaining counter, or a spawn-rate indicator. Slot 63 is the cursor — highest index so it always draws on top.

**Cost of the change.** `shape_table.sv` stores N=48 shapes as 48×(2+1+10+9+9+9+8) ≈ 48×48 bits of registers; bumping to 64 costs roughly 768 additional flip-flops plus the decode logic for the new addresses. `shape_renderer.sv` gets a wider `cur_shape` counter; everything else is unchanged because `SHAPE_ADDR` already carries 6 bits.

**Alternative considered.** Keep 48 entries and compress HUD (3 plant cards + 1 highlight + 2 sun digits = 6 HUD slots, capping the displayed sun at 99). Rejected because 99-sun caps are visibly broken once a single Sunflower generates 250 sun over the level, and 2-digit displays shrink the HUD glance information.

### Plant-type dispatch via a shared action timer

**Decision.** Keep `plant_t` at the same shape, repurpose `fire_cooldown` as a generic `action_timer`, and branch on `type` inside a renamed `update_plants` function:
- `PLANT_PEASHOOTER`: when timer hits 0 and a zombie exists in the row, spawn a pea and reset timer to `PEASHOOTER_FIRE_COOLDOWN`.
- `PLANT_SUNFLOWER`: when timer hits 0, `gs->sun += 25`; reset to `SUNFLOWER_PRODUCE_COOLDOWN` (600 frames).
- `PLANT_WALLNUT`: no-op; timer stays at 0.

**Why.** No struct size change means the existing grid-sized array keeps its memory layout and the on-board native build doesn't need fresh testing for layout bugs. A single update pass keeps ordering predictable for the unit tests (sunflower income applies in the same frame it would have fired).

**Alternative considered.** Add a union inside `plant_t` for per-type state (e.g., wallnut HP counter). Rejected — HP lives in the shared field already, and the extra struct complexity buys nothing for v3's three plant types.

### Zombie-type dispatch via a lookup table

**Decision.** Add `int type` to `zombie_t`. Use a compile-time lookup table for per-type max HP. Color and visual size are selected in `render.c` via the same `type` field. Walk speed stays uniform across types for v3.

**Why.** Minimal game.c churn. Type-specific damage math is a no-op because each pea still does 1 HP; the only change is where the initial HP comes from on spawn.

**Alternative considered.** Per-type speed. Deferred — the original PvZ has Flag Zombies walking faster, but none of the three v3 types justify the extra tuning surface.

### Wave table with jitter

**Decision.** Ship a compile-time `WAVE_TEMPLATE[10]` in `game.c` (spawn frame + type). At `game_init`, copy the template into `gs->wave[]` and add per-entry uniform jitter of ±60 frames (±1 s at 60 fps). Spawn walks the array in order by `frame_count` comparison.

**Why.** Deterministic difficulty pacing without feeling scripted. Unit-testable under a fixed `srand` seed. Adding new levels later is one new template array.

**Alternative considered.** Weighted-random type picker with a difficulty ramp. Rejected — a single 12-HP Buckethead arriving at 15 s would be unplayable, and the random version can only guarantee "usually fair" difficulty. A tunable template puts that guarantee in the hands of a designer reading the array.

### Input auto-detect with priority (controller > keyboard)

**Decision.** `input_init` enumerates `/dev/input/event0..31`, queries each with `EVIOCGBIT(EV_KEY)`, and classifies:
- **Gamepad** if the device advertises `BTN_SOUTH` (the xpad driver sets this on Xbox 360 controllers).
- **Keyboard** if the device advertises `KEY_SPACE`.

Controller wins if both are found. The chosen device path is logged to stdout and stderr so on-board debugging is trivial. `input_poll` returns a `input_action_t` enum that maps both keyboard and gamepad inputs into a single action space (`INPUT_UP`, `INPUT_DOWN`, `INPUT_LEFT`, `INPUT_RIGHT`, `INPUT_PLACE`, `INPUT_DIG`, `INPUT_CYCLE_PREV`, `INPUT_CYCLE_NEXT`, `INPUT_QUIT`, `INPUT_NONE`).

**Why.** The user picked "priority with silent fallback" over simultaneous use, so this collapses the input layer to one fd and one translation table per device family. Game code never branches on device.

**Alternative considered.** Parallel polls that merge both devices' events every frame. Usable but the action enum is already a lossy merge point — there is no value in reaching the game loop from two devices at once in practice, because the user only has one set of hands. Deferred.

### Plant-selector UI

**Decision.** Three `SHAPE_RECT` cards along the top HUD at y=5, 55 × 45 px each, 10 px apart. Colors: Peashooter green (palette 7), Sunflower yellow (palette 4), Wall-nut brown (palette 3). A fourth shape (the selection highlight) is a yellow rectangle 59 × 49 at y=3, x positioned one pixel to the left of the selected card; `render_hud` updates its x each frame based on `gs->selected_plant`. The highlight has a **lower** shape index than the cards so that each card paints on top of the highlight where they overlap — the parts of the highlight that remain visible form a 2-pixel yellow border on the edges of the selected card only.

**Why.** One rectangle per card plus one highlight overlay. Small HUD real estate. No new palette entries. The painter's-algorithm z-order handles the border effect automatically: the larger yellow rectangle at slot 43 paints first, the smaller green / yellow / brown card at slot 44 / 45 / 46 paints over it. The highlight only exists at one position per frame, so only the selected card sits above it; the other two cards paint over bare background.

```
43     plant-card highlight   (behind cards, lower index)
44-46  plant cards            (Peashooter / Sunflower / Wall-nut)
47-50  sun digits             (up to 4 digits)
51-62  reserved
63     cursor                 (always on top)
```

Details refined in `specs/game-rendering/spec.md`.

### Rendering new plant and zombie types

| Entity | Shape | Palette | Bounding box | In-cell offset |
|---|---|---|---|---|
| Peashooter | `SHAPE_SPRITE` | sprite ROM | 64 × 64 | (8, 13) – unchanged |
| Sunflower | `SHAPE_CIRCLE` | yellow (4) | 60 × 60 | (10, 15) |
| Wall-nut | `SHAPE_CIRCLE` | brown (3) | 70 × 70 | (5, 10) |
| Basic zombie | `SHAPE_RECT` | red (5) | 30 × 70 | (0, 10) from row top |
| Conehead | `SHAPE_RECT` | orange (12) | 30 × 70 | same |
| Buckethead | `SHAPE_RECT` | gray (11) | 30 × 70 | same |

The existing palette already has every color. No changes to `color_palette.sv`.

### Known-issues ledger

Any integration defect surfaced during Phase F (full playthrough) that is not fixed in-branch lands in `doc/v3-known-issues.md` with severity (blocker / major / minor), reproduction steps, and a proposed fix. A blocker stops the push; major or minor get a follow-up change and ship with v3.

## Risks / Trade-offs

- **[Shape-table growth breaks TBs]** → Update `tb_shape_renderer.sv` and `tb_pvz_top.sv` in the same commit that changes `shape_table.sv` and `shape_renderer.sv`. New TB cases write to index 50 and index 63 so both the new-range and top-index paths are covered.
- **[Untested on hardware before push]** → Phase C, D, E all require on-board smoke tests before their respective commits land in the final push. If board time is unavailable, the affected commits are held and the known-issues ledger records the blocker.
- **[xpad driver not loaded]** → evdev auto-detect will not find the controller and silently fall back to keyboard. `test_input_devices` reports the enumeration result to the console so the missing driver is obvious before launching the game. Loading xpad on the DE1-SoC image is a prerequisite and documented in `doc/v3-changes.md`.
- **[Wave table determinism vs. replay variety]** → per-entry jitter is ±60 frames, intentionally small. If the level feels identical across runs, the jitter range can widen; if it feels unfair, tighten. Tuned from playtest feedback during Phase F.
- **[HUD shape-budget misallocation]** → The highlight-under-cards z-order trick is subtle. The unit for this is a small visual test in Phase D that confirms the highlight's yellow border is visible around the selected card and only around the selected card.
- **[`selected_plant` out of range]** → `game_place_plant` reads `gs->selected_plant` as an index into `{PEASHOOTER, SUNFLOWER, WALLNUT}`. Cycle handlers clamp to `{0,1,2}` with modulo.

## Migration Plan

- Work in a new worktree off `main` on branch `v3-controller-and-roster`.
- Phases run in order A (HW), B (SW data model), C (SW rendering), D (UI), E (input), F (integration), G (docs). Each phase lists a gate that must pass before its commit lands.
- Commits are made in logical groups (not per file) with short imperative titles, no body: `v3: grow shape table 48->64 for HUD cards`, `v3: plant & zombie type data model + wave table`, and so on.
- Push the branch to `nyavana/pvz-fpga` at the end of Phase G. Do not push until Phase F (integration playthrough) passes on both control schemes.
- Rollback: the v3 branch is independent of `main`. If v3 needs to be abandoned, `main` remains on the v2Dino baseline. If a specific commit regresses, `git revert` on the v3 branch before push.

## Open Questions

- None of the test programs currently exercise the sprite pipeline. `tb_shape_renderer.sv` stubs `sprite_rd_pixel` to `0xFF`. Adding a sprite-aware test case is valuable but not strictly required for v3 because the sprite path is unchanged; decide during Phase A whether to add this coverage now or defer.
- The existing archived `openspec/specs/` is empty (v2's archived change wasn't promoted into live specs). Ask the user whether to promote v2's specs to `openspec/specs/` as part of v3 or treat every capability in this change as a fresh ADDED set. Current approach: fresh ADDED for the genuinely new capabilities; ADDED inside a `specs/<name>/spec.md` file for the modified ones, letting the archive stay the canonical v2 record.
