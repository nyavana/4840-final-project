# v3 changes

Summary of what's new on `v3-controller-and-roster` relative to v2Dino
(`a6eb347`). Work was staged in seven phases (A–G) with a commit per
phase; see `git log --oneline main..HEAD` for the one-line history.

## 1. Shape table grown 48 → 64 (Phase A)

The HUD needs space for three plant cards, a selection highlight, and a
sun counter wide enough to hold the total income from a full Sunflower
run. The v2Dino shape-index budget had four HUD digits (indices 43–46)
and the cursor (47); there was no room for cards.

**Hardware**

- `hw/shape_table.sv` — shadow and active arrays grow from `[0:47]` to
  `[0:63]`. Per-entry pack format is unchanged (48 bits: type, visible,
  x, y, w, h, color). `SHAPE_ADDR` already carries 6 bits so no register
  layout or compatible-string change is needed.
- `hw/shape_renderer.sv` — the iteration bound flips from `6'd47` to
  `6'd63`; the `cur_shape` counter width is unchanged (already 6 bits).

**Testbenches**

- `hw/tb/tb_shape_renderer.sv` — adds a high-index case. Shape 40 at
  color 7 and shape 63 at color 9 overlap at `x ∈ [240, 260)` on
  scanline 120; the TB asserts the overlap pixel is color 9 (painter's
  algorithm z-order) while the non-overlap regions remain colors 7 and
  9 respectively.
- `hw/tb/tb_pvz_top.sv` — adds a second Avalon write to `SHAPE_ADDR = 50`
  and samples the pixel at (420, 220) to confirm the new high-index
  slot lands on the VGA output.

**Driver**

- `sw/pvz_driver.c::pvz_ioctl` — adds an explicit `index >= 64` check
  that returns `-EINVAL` on out-of-range writes. No register layout or
  ioctl number change.

**Shape-index rebudget** (in `sw/render.c`)

```
0–31   Plants (one slot per grid cell, r*8 + c)
32–36  Zombies (5 concurrent slots)
37–42  Peas   (6 slots, unchanged from v2)
43     Plant-card selection highlight (below cards, lower index)
44–46  Plant cards (Peashooter / Sunflower / Wall-nut)
47–50  Sun counter digits (up to 4 digits, max 9999)
51–62  Reserved — hidden every frame
63     Cursor (highest index; always drawn on top)
```

## 2. Plant and zombie roster (Phase B)

v2Dino shipped Peashooter (3 HP, fires every 2 s, 50 sun) and the red
rectangle zombie (3 HP). v3 expands both lists.

- `sw/game.h` — adds `plant_type_t` (PLANT_NONE / PEASHOOTER /
  SUNFLOWER / WALLNUT) and `zombie_type_t` (BASIC / CONEHEAD /
  BUCKETHEAD) enums. `zombie_t` gains a `type` field. `game_state_t`
  gains `selected_plant`, `wave_entry_t wave[10]`, and `wave_index`. A
  `SUNFLOWER_PRODUCE_COOLDOWN = 600` constant joins
  `PEASHOOTER_FIRE_COOLDOWN`. `TOTAL_ZOMBIES` bumps to 10.
- `sw/game.c` — adds per-type tables: `plant_stats[]` (max HP, cost for
  each plant type) and `zombie_hp_table[]` (max HP per zombie type).
  Accessors `plant_max_hp`, `plant_cost`, and `zombie_max_hp` front the
  tables.
- The old `update_firing` becomes `update_plants`, branching on plant
  type: peashooters fire when a zombie sits in the same row;
  sunflowers add 25 sun to `gs->sun` every 600 frames; wall-nuts idle.
- `game_place_plant` reads `gs->selected_plant` (0 / 1 / 2) and maps it
  to `PLANT_PEASHOOTER` / `_SUNFLOWER` / `_WALLNUT`, pulling the cost
  and initial HP from the tables.

### Wave-table spawn system

v2Dino used a random spawn interval in `[8 s, 15 s]`, which gave
inconsistent difficulty from run to run. v3 replaces it with a
hand-tuned 10-entry template jittered by ±60 frames at `game_init`:

```
 480  BASIC       780  BASIC      1080  BASIC
1440  CONEHEAD   1800  BASIC      2160  CONEHEAD
2580  BASIC      3000  CONEHEAD   3420  BUCKETHEAD
3900  BASIC
```

`update_spawning` walks the array by frame count instead of a timer.
If all zombie slots are busy, the spawn is held — `wave_index` stays
put and the spawn retries next frame. `check_win` now waits for
`wave_index == TOTAL_ZOMBIES` and every active zombie cleared before
flipping to `STATE_WIN`.

## 3. Type-aware rendering (Phase C)

- `render_plants` branches on `plant_t.type`:
  - `PLANT_PEASHOOTER` → existing sprite path, 64×64 at cell offset (8, 13).
  - `PLANT_SUNFLOWER` → yellow circle (palette 4), 60×60 at offset (10, 15).
  - `PLANT_WALLNUT` → brown circle (palette 3), 70×70 at offset (5, 10).
- `render_zombies` branches on `zombie_t.type` for the rectangle color:
  - `ZOMBIE_BASIC` → red (5).
  - `ZOMBIE_CONEHEAD` → orange (12).
  - `ZOMBIE_BUCKETHEAD` → gray (11).
  All three share the 30×70 bounding box and the uniform walk speed.
- Slots 51–62 are hidden every frame so reserved indices cannot carry
  stale data into the line renderer.

No palette entries change; all required colors already exist in
`hw/color_palette.sv`.

## 4. Plant-selector HUD (Phase D)

The top of the screen now carries three 55×45 plant cards starting at
(10, 5), 65 px apart: Peashooter (green 7), Sunflower (yellow 4),
Wall-nut (brown 3). A 59×49 yellow highlight lives at shape slot 43
(lower than the cards), positioned two pixels up and two pixels left
of the selected card so the card paints over the centre of the
highlight and only a 2-pixel yellow border remains visible around the
active selection.

`cycle_plant_prev` and `cycle_plant_next` in `sw/game.c` rotate
`selected_plant` modulo 3 and no-op while the game isn't in
`STATE_PLAYING`. `sw/main.c::process_input` calls them on
`INPUT_CYCLE_PREV` / `INPUT_CYCLE_NEXT`. When the game ends the cards
and highlight are hidden so the win/lose overlay paints over a clean
HUD row.

## 5. Input auto-detect (Phase E)

`sw/input.h` now exports an `input_action_t` enum and a zero-arg
`input_init(void)`. `sw/input.c` is a full rewrite:

- Walks `/dev/input/event0..31`.
- For each, queries `EVIOCGBIT(EV_KEY)`. A device advertising
  `BTN_SOUTH` is classified as a gamepad; one advertising `KEY_SPACE`
  is a keyboard. A device that advertises both (rare) is treated as a
  gamepad.
- Gamepad wins if both are present. The chosen device path is logged
  to both stdout and stderr so it's obvious from either the serial
  console or a redirected launch.
- Returns -1 if nothing matches, and `sw/main.c` exits with status 1.

Gamepad translation table (xpad / Xbox 360):

| evdev code | action |
|---|---|
| `BTN_DPAD_UP/DOWN/LEFT/RIGHT` or `ABS_HAT0X/Y` | `INPUT_UP/DOWN/LEFT/RIGHT` |
| `BTN_SOUTH` (A) | `INPUT_PLACE` |
| `BTN_EAST` (B) | `INPUT_DIG` |
| `BTN_TL` (LB) | `INPUT_CYCLE_PREV` |
| `BTN_TR` (RB) | `INPUT_CYCLE_NEXT` |
| `BTN_START` | `INPUT_QUIT` |

Keyboard table:

| evdev code | action |
|---|---|
| `KEY_UP/DOWN/LEFT/RIGHT` | `INPUT_UP/DOWN/LEFT/RIGHT` |
| `KEY_SPACE` | `INPUT_PLACE` |
| `KEY_D` | `INPUT_DIG` |
| `KEY_LEFTBRACE` (`[`) | `INPUT_CYCLE_PREV` |
| `KEY_RIGHTBRACE` (`]`) | `INPUT_CYCLE_NEXT` |
| `KEY_ESC` | `INPUT_QUIT` |

Only press events (`value == 1`) produce actions; release and
auto-repeat are dropped so cursor motion stays one-shot per physical
press.

A new on-board helper, `sw/test/test_input_devices.c`, enumerates all
event nodes, prints each device's name, BTN_SOUTH and KEY_SPACE
advertisement, and the auto-detect verdict. `sw/test/test_input.c` is
updated to use the new zero-arg `input_init` and print the unified
action names.

**Prerequisite on the board image:** `xpad` must be loaded for Xbox
360–compatible controllers. `modprobe xpad` is a one-liner; if the
module is not present, auto-detect silently falls back to keyboard.

## 6. Tests

`sw/test/test_game.c` expanded from 12 cases to 21; `./test_game`
reports 136 passing assertions. New coverage includes:

- Sunflower +25 sun on exactly the 600-frame cooldown.
- Sunflower and Wall-nut never fire peas even with zombies in-row.
- Peashooter fires on schedule when a zombie enters its row.
- Conehead dies after exactly 6 hits; Buckethead after 12.
- Wave-table determinism under `srand(42)` and jitter inside ±60
  frames.
- `selected_plant` cycle wrap (0↔2) and no-op while game-over.
- `game_place_plant` places the plant type the selection points at,
  with the correct per-type HP.
- First wave entry produces a zombie whose type and HP match the
  template.

## 7. Known issues

Four entries in `doc/v3-known-issues.md`, all at severity minor, each
tracking a verification step that needs ModelSim or board time: the
enlarged shape table under vsim (Phase A), type-aware plant and zombie
rendering on VGA (Phase C), the plant-selector HUD (Phase D), and the
three-configuration input auto-detect check (Phase E). None block the
push. Each entry lists the reproduction and a proposed fix for the
next board-access session.

## Build impact

- Hardware needs a re-synthesis (`make qsys && make quartus && make
  rbf`) to pick up the 64-entry shape table and renderer bound change.
- Software cross-compiles with the existing `sw/Makefile`; the Phase E
  rewrite adds a new `test_input_devices` target but doesn't change
  the `pvz` link line.
- No driver ABI change, so an existing `/dev/pvz` node keeps working
  with the v3 userspace.
