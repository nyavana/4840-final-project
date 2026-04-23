# v3 Known Issues

Any defect surfaced during v3 integration that is not fixed in-branch lives here. A **blocker** holds the push; **major** and **minor** may ship with v3 and are scheduled as follow-ups.

Format per entry:

```
## <short title>
- **Severity**: blocker / major / minor
- **Area**: hw / driver / game / render / input / docs / build
- **Found in phase**: A / B / C / D / E / F / G
- **Reproduction**: <steps>
- **Proposed fix**: <description>
- **Status**: open / deferred / fixed-in-<sha>
```

---

## Board verification of plant-selector HUD (Phase D gate 5.6)

- **Severity**: minor
- **Area**: render / game
- **Found in phase**: D
- **Reproduction**: Phase D gate requires visual confirmation on the DE1-SoC that the yellow highlight border moves between cards on cycle inputs, that place-plant respects the current selection, and that overlay screens hide cards as expected. Keyboard `[` / `]` bindings are not wired until Phase E, so today the Phase D cycle path can be exercised only once Phase E is on the board. Board not available in this worktree environment.
- **Proposed fix**: After Phase E lands on the board, exercise `[` / `]` on the keyboard and LB / RB on the gamepad; confirm the yellow border moves and placement matches the active card.
- **Status**: open

## Board verification of type-aware rendering (Phase C gate 4.6)

- **Severity**: minor
- **Area**: render
- **Found in phase**: C
- **Reproduction**: The Phase C gate requires a visual check on the DE1-SoC that each plant type renders with the right shape/colour (peashooter sprite, yellow circle sunflower, brown circle wall-nut) and each zombie type renders with the right rectangle colour (red basic, orange conehead, gray buckethead). Board not available in this worktree environment, so the visual check has not run.
- **Proposed fix**: Cross-compile, deploy, hand-place one of each plant via debug path, spawn one of each zombie via debug path, confirm colours and sizes match `specs/game-rendering/spec.md`. The wave template already spawns all three zombie types during a natural playthrough, so confirming via the wave is also acceptable.
- **Status**: open

## ModelSim verification of shape-table growth (Phase A gate 2.5)

- **Severity**: minor
- **Area**: hw
- **Found in phase**: A
- **Reproduction**: The Phase A gate requires `vsim -do "run -all; quit" work.tb_shape_renderer` and `work.tb_pvz_top` to run green. The v3 worktree ran in an environment without ModelSim on `PATH`, so the ModelSim run could not be executed before the Phase A commit. TB source changes are in place (high-index overlap check in `tb_shape_renderer.sv`, `SHAPE_ADDR = 50` write in `tb_pvz_top.sv`).
- **Proposed fix**: Run the two TBs on a Columbia workstation. Expected passes: overlap pixel at [240, 260) reports color 9 (shape 63 wins over shape 40); non-overlap pixels at [200, 240) report color 7; the `SHAPE_ADDR = 50` end-to-end write is decoded and the pixel at (420, 220) shows bright-green palette output.
- **Status**: open
