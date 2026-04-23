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

## ModelSim verification of shape-table growth (Phase A gate 2.5)

- **Severity**: minor
- **Area**: hw
- **Found in phase**: A
- **Reproduction**: The Phase A gate requires `vsim -do "run -all; quit" work.tb_shape_renderer` and `work.tb_pvz_top` to run green. The v3 worktree ran in an environment without ModelSim on `PATH`, so the ModelSim run could not be executed before the Phase A commit. TB source changes are in place (high-index overlap check in `tb_shape_renderer.sv`, `SHAPE_ADDR = 50` write in `tb_pvz_top.sv`).
- **Proposed fix**: Run the two TBs on a Columbia workstation. Expected passes: overlap pixel at [240, 260) reports color 9 (shape 63 wins over shape 40); non-overlap pixels at [200, 240) report color 7; the `SHAPE_ADDR = 50` end-to-end write is decoded and the pixel at (420, 220) shows bright-green palette output.
- **Status**: open
