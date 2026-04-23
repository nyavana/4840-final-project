## 1. Worktree setup (Phase 0)

- [x] 1.1 Create a git worktree at a workspace-local path, checking out branch `v3-controller-and-roster` from `main`
- [x] 1.2 Verify the worktree compiles the v2 baseline before any edits (`cd sw && make test_game && ./test_game` green, `hw/` synth not required at this stage)
- [x] 1.3 Record in `doc/v3-known-issues.md` an empty ledger with the format "severity / area / description / proposed fix" so later phases can append to it

## 2. Shape-table growth (Phase A, hardware)

- [ ] 2.1 Update `hw/shape_table.sv` to 64 entries: bump the entry-count parameter and any hard-coded `48` constants
- [ ] 2.2 Update `hw/shape_renderer.sv`'s `cur_shape` counter and loop bound to iterate 0..63
- [ ] 2.3 Extend `hw/tb/tb_shape_renderer.sv` with a case that draws two overlapping shapes at indices 40 and 63 and asserts the pixel at the overlap matches index 63
- [ ] 2.4 Extend `hw/tb/tb_pvz_top.sv` with an Avalon-side write to `SHAPE_ADDR = 50` and assert the expected RGB at the pixel location
- [ ] 2.5 **Gate**: run `vsim -do "run -all; quit" work.tb_shape_renderer` and `work.tb_pvz_top` — both must pass. Do not proceed until green.
- [ ] 2.6 Widen the range-check in `sw/pvz_driver.c` from `index < 48` to `index < 64` and rebuild
- [ ] 2.7 **Commit**: `v3: grow shape table 48->64 for HUD cards`

## 3. Data model and game logic (Phase B, software)

- [ ] 3.1 Add `plant_type` and `zombie_type` enums to `sw/game.h`; add `type` field to `zombie_t`; add `selected_plant`, `wave_entry_t wave[10]`, and `wave_index` to `game_state_t`; add `SUNFLOWER_PRODUCE_COOLDOWN 600` constant; bump `TOTAL_ZOMBIES` to 10
- [ ] 3.2 In `sw/game.c` add a static const per-plant table (cost, max HP) and per-zombie-type max HP table
- [ ] 3.3 Rename `update_firing` to `update_plants` and branch on plant type (peashooter → fire, sunflower → +25 sun, wall-nut → no-op)
- [ ] 3.4 In `game_place_plant`, read `gs->selected_plant` and look up cost + HP from the table rather than hardcoding
- [ ] 3.5 Replace `update_spawning` random loop with a wave-table walker; add a `WAVE_TEMPLATE[10]` constant and a `game_init`-time jitter pass using `rand()`
- [ ] 3.6 Extend `sw/test/test_game.c` with these cases:
  - sunflower produces +25 sun exactly every 600 frames
  - sunflower does not fire peas; wall-nut does not fire peas
  - conehead dies after exactly 6 peas; buckethead dies after exactly 12
  - wave-table determinism under `srand(42)` — both wave arrays match
  - `selected_plant` cycling wraps 0↔2 via the helpers used in `main.c`
  - `game_place_plant` respects `selected_plant` for each of the three plant types
- [ ] 3.7 **Gate**: `cd sw && make test_game && ./test_game` — all tests green. No on-board work in this phase.
- [ ] 3.8 **Commit**: `v3: plant & zombie type data model + wave table`

## 4. Rendering updates (Phase C, software + on-board)

- [ ] 4.1 Update `sw/render.c` shape-index allocation to the v3 budget (see `specs/game-rendering/spec.md`) and update the `IDX_*` macros accordingly
- [ ] 4.2 In `render_plants`, branch on `p->type`: peashooter keeps the existing sprite call; sunflower emits a yellow circle at offset (10, 15) size 60 × 60; wall-nut emits a brown circle at offset (5, 10) size 70 × 70
- [ ] 4.3 In `render_zombies`, branch on `z->type` to pick palette index 5 / 12 / 11 for basic / conehead / buckethead; keep the 30 × 70 rectangle shape
- [ ] 4.4 Explicitly hide slots 51–62 each frame so stale data never reaches the FPGA
- [ ] 4.5 **Gate (unit)**: `make test_game` still green (should be unaffected but verifies the header changes didn't break compile)
- [ ] 4.6 **Gate (on-board)**: cross-compile, deploy, hand-place one of each plant and spawn one of each zombie type via a temp debug path; visually confirm each renders with the expected color and size. If the board is unavailable, do not claim this phase complete — log `doc/v3-known-issues.md` with "needs board verification" and hold the commit.
- [ ] 4.7 **Commit**: `v3: type-aware rendering for plants and zombies`

## 5. Plant-selector UI (Phase D, software + on-board)

- [ ] 5.1 Add `render_plant_cards(gs)` to `sw/render.c`: three rectangles at the positions in the spec, colored per-plant; hide when `state != STATE_PLAYING`
- [ ] 5.2 Add a selection-highlight rectangle at slot 43 whose x derives from `gs->selected_plant`
- [ ] 5.3 Wire `INPUT_CYCLE_PREV` / `INPUT_CYCLE_NEXT` in `sw/main.c` to decrement / increment `gs->selected_plant` modulo 3
- [ ] 5.4 Block cycling while `state != STATE_PLAYING`
- [ ] 5.5 **Gate (unit)**: add test cases in `test_game.c` for the cycle wrap logic (assumes a small helper `cycle_plant_prev`/`cycle_plant_next` in `game.c` that `main.c` calls)
- [ ] 5.6 **Gate (on-board)**: cycling visibly moves the yellow highlight; placing plants respects the current selection; overlay screens hide cards as expected. Log any gap in `doc/v3-known-issues.md` before holding the commit.
- [ ] 5.7 **Commit**: `v3: plant-selector HUD and selection logic`

## 6. Input auto-detect (Phase E, software + on-board)

- [ ] 6.1 Rewrite `sw/input.h` to export `input_action_t` enum (`INPUT_UP`/`INPUT_DOWN`/`INPUT_LEFT`/`INPUT_RIGHT`/`INPUT_PLACE`/`INPUT_DIG`/`INPUT_CYCLE_PREV`/`INPUT_CYCLE_NEXT`/`INPUT_QUIT`/`INPUT_NONE`) and a zero-arg `input_init(void)`
- [ ] 6.2 Rewrite `sw/input.c`: enumerate `/dev/input/event0..31`, `EVIOCGBIT(EV_KEY)` each, classify as gamepad (`BTN_SOUTH`) or keyboard (`KEY_SPACE`), prefer gamepad, log the chosen path
- [ ] 6.3 Add device-specific key-code translation tables for keyboard and for the xpad gamepad
- [ ] 6.4 Update `sw/main.c` to use the new `input_init` / `input_poll` signatures and the new action enum
- [ ] 6.5 Write `sw/test/test_input_devices.c`: enumerate, print capability summary per device, print the auto-detect choice; add a Makefile target for it
- [ ] 6.6 Bind `[` / `]` on the keyboard to `INPUT_CYCLE_PREV` / `INPUT_CYCLE_NEXT`
- [ ] 6.7 **Gate (on-board)**: run `test_input_devices` three times — keyboard only, controller only, both plugged in — and verify the chosen device matches the spec each time. Full playthrough with controller. Full playthrough with keyboard. Both must be smooth end-to-end.
- [ ] 6.8 **Commit**: `v3: input auto-detect between controller and keyboard`

## 7. Integration and known-issues ledger (Phase F, on-board)

- [ ] 7.1 Boot the v3-built board image with both a controller and a keyboard connected; confirm driver probe succeeds, `/dev/pvz` exists, the game launches
- [ ] 7.2 Play a full round using the controller; record any defects in `doc/v3-known-issues.md`
- [ ] 7.3 Unplug the controller, power-cycle, boot with keyboard only, play a full round; record defects
- [ ] 7.4 For each defect: decide "fix in-branch" or "defer". Fix any blocker; defer major / minor entries with a clear proposed fix in the ledger
- [ ] 7.5 **Gate**: no open blocker in `doc/v3-known-issues.md`
- [ ] 7.6 **Commit** (only if changes were made in 7.4): `v3: integration fixes`

## 8. Documentation (Phase G)

- [ ] 8.1 Write `doc/v3-changes.md` summarising the diff from v2Dino: new shape-table size, new plant / zombie types, wave system, plant-selector UI, input rewrite. Keep the style matching `doc/v2-changes.md`.
- [ ] 8.2 Update `CLAUDE.md`: bump the shape-table-size invariant (48 → 64), note the new input API, note the new action enum values, note the wave-table spawn discipline, note that `selected_plant` drives placement.
- [ ] 8.3 Run `/humanizer` on `doc/v3-changes.md` and any new prose in `CLAUDE.md`; apply its edits
- [ ] 8.4 **Commit**: `v3: documentation`

## 9. Push

- [ ] 9.1 Verify `doc/v3-known-issues.md` has no open blocker entries
- [ ] 9.2 `git push -u origin v3-controller-and-roster`
- [ ] 9.3 Report the branch URL on `nyavana/pvz-fpga` back to the user
