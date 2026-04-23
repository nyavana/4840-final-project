# v4 Known Issues

Deviations from the plan and things that need human follow-up.

## Plan pin assignment values were incorrect (Task 2.1)
- **Severity**: minor (caught at review)
- **Area**: hw / qsf
- **Found in phase**: Chunk 2
- **Detail**: The audio engine plan proposed `PIN_AC27/AG30/AF30/AC28/AH4/AC9/AH7/AG7` for the WM8731 codec pins. Those values do not match the DE1-SoC. The correct pins (already present in `hw/soc_system.qsf` from the pre-audio baseline) are `PIN_K7/K8/H7/J7/H8/G7/J12/K12`. No action needed — qsf is already correct; logging so future readers of the plan don't get confused.
- **Status**: resolved in-place (no commit)

## Task 2.3 headless approach required XML editing for pvz_audio_0
- **Severity**: minor (resolved)
- **Area**: hw / Qsys
- **Found in phase**: Chunk 2
- **Detail**: `qsys-script` couldn't register the project component's interfaces (`add_interface`/`add_interface_port` in `pvz_audio_hw.tcl` don't execute in qsys-script's context). Workaround: `add_instance pvz_audio_0 pvz_audio` via qsys-script, then the Avalon-MM slave, clock, reset, and two Avalon-ST sources were added as XML edits to `hw/soc_system.qsys`.
- **Resolution**: commit `25eccba` succeeds: `make qsys` produces a clean `Info: qsys-generate succeeded.` after later fixes (8-bit symbol fix in commit `ecee098`, ROM stubs in `1bd157f`). If future work needs to re-edit the system (add more interfaces, change address map), doing it in the Platform Designer GUI on a workstation is still the most reliable path.
- **Status**: resolved

## pvz_audio base address differs from plan (Task 2.3)
- **Severity**: minor (documentation)
- **Area**: hw / address map
- **Detail**: The plan assumed `0xff210000` for pvz_audio. The actual assigned address within the lwh2f bridge (base `0xff200000`) is `0x1000`, giving CPU byte address `0xff201000`. The Linux driver (Task 3.2) reads the address from the dtb via `of_address_to_resource`, so it does not need to hard-code this value. Documentation in `CLAUDE.md` (Task 6.1) must say `0xff201000` not `0xff210000`, and `/proc/iomem` debug hints should look for `ff20` not `ff21`.
- **Status**: resolved pending Task 6.1

## ARM cross-toolchain not available on the workstation used for this session
- **Severity**: minor
- **Area**: sw / kernel module build
- **Detail**: `arm-linux-gnueabihf-gcc` is not on PATH in this environment. The kernel module `pvz_audio_driver.ko` can be built later on a Columbia workstation with the cross-toolchain and kernel headers 4.19.0 installed.
- **Proposed fix**: On a workstation with the cross-toolchain, run `cd sw && make CC=arm-linux-gnueabihf-gcc ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- KERNEL_SOURCE=/path/to/linux-kernel-headers module`. Verify `pvz_audio_driver.ko` and `pvz_driver.ko` both build.
- **Status**: open

## Board deployment / on-board smoke test not performed
- **Severity**: minor
- **Area**: hw+sw integration
- **Detail**: The DE1-SoC board is not accessible from this session. The audio path has been verified in simulation (ModelSim testbenches pass) and Qsys generation succeeds, but the full `.rbf` has not been run on the board and `test_audio` has not been exercised against a live WM8731 codec.
- **Proposed fix**: After full Quartus build, deploy to board via `deploy.sh install && deploy.sh run`. Listen for BGM during `STATE_PLAYING`. Run `deploy.sh test test_audio` for the standalone audio smoke test, and `deploy.sh test test_audio --sfx N` for each cue.
- **Status**: open

## Task 7.1 test-before-ship gate results (2026-04-23)

All simulation-side tests pass on this workstation:
- **ModelSim testbenches (9/9 pass):** tb_mixer, tb_voice_bgm, tb_voice_sfx, tb_pvz_audio (audio) + tb_bg_grid, tb_linebuffer, tb_shape_renderer, tb_vga_counters, tb_pvz_top (existing).
- **Host test suite:** test_game reports `Results: 145 passed, 0 failed`.
- **Host build of pvz:** x86-64 ELF produced cleanly.
- **Full Quartus compilation:** 0 errors, 421 warnings. `.sof`, `.rbf` generated; `.dtb` contains `csee4840,pvz_audio-1.0` at address `0x100001000` (CPU byte `0xff201000`). Block memory utilisation 78% (target was ≤85%).

Gaps requiring a workstation / board:
- ARM cross-compile of `pvz_audio_driver.ko` (see above).
- On-board deployment smoke test (see above).
