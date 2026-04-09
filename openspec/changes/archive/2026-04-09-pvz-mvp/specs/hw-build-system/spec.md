## ADDED Requirements

### Requirement: Hardware Makefile with Quartus targets
The `hw/Makefile` SHALL provide targets: `qsys` (regenerate Verilog from .qsys), `quartus` (full synthesis), `rbf` (convert .sof to .rbf), and `dtb` (generate device tree blob). The Makefile SHALL follow the lab3 hw/Makefile pattern.

#### Scenario: make qsys
- **WHEN** `make qsys` is run
- **THEN** Platform Designer SHALL regenerate Verilog from `soc_system.qsys`

#### Scenario: make quartus
- **WHEN** `make quartus` is run after qsys
- **THEN** Quartus SHALL synthesize, fit, and generate `output_files/soc_system.sof`

#### Scenario: make rbf
- **WHEN** `make rbf` is run after quartus
- **THEN** the .sof SHALL be converted to `output_files/soc_system.rbf` for SD card deployment

#### Scenario: make dtb
- **WHEN** `make dtb` is run
- **THEN** `sopc2dts` and `dtc` SHALL produce `soc_system.dtb` with the pvz_gpu device tree entry

### Requirement: Platform Designer component descriptor
`pvz_top_hw.tcl` SHALL define a Platform Designer component for `pvz_top` with Avalon-MM slave interface, clock and reset connections, and `set_module_assignment embeddedsw.dts.compatible "csee4840,pvz_gpu-1.0"` so the device tree entry is auto-generated.

#### Scenario: Component appears in Platform Designer
- **WHEN** `pvz_top_hw.tcl` is loaded in Platform Designer
- **THEN** `pvz_top` SHALL appear as an available component with an Avalon-MM slave interface

#### Scenario: Device tree compatible string
- **WHEN** the dtb is generated from the Platform Designer system
- **THEN** the pvz_gpu node SHALL have `compatible = "csee4840,pvz_gpu-1.0"`

### Requirement: Software Makefile with kbuild and userspace targets
The `sw/Makefile` SHALL build the kernel module (`pvz_driver.ko`) using the standard kbuild two-pass pattern and compile the userspace game program and test programs using gcc.

#### Scenario: make builds driver and userspace
- **WHEN** `make` is run in the `sw/` directory
- **THEN** `pvz_driver.ko` and the userspace executables SHALL be built

#### Scenario: Kbuild two-pass pattern
- **WHEN** the Makefile is invoked by kbuild (KERNELRELEASE defined)
- **THEN** it SHALL specify `obj-m := pvz_driver.o`
- **WHEN** invoked directly (KERNELRELEASE not defined)
- **THEN** it SHALL invoke `make -C /usr/src/linux-headers-$(uname -r) M=$(pwd) modules`

### Requirement: Board top-level wiring
`soc_system_top.sv` SHALL instantiate the Platform Designer-generated `soc_system` and wire VGA output pins (R, G, B, hsync, vsync, blank_n, sync_n, clk) to the DE1-SoC board's VGA connector, following the lab3 `soc_system_top.sv` pattern.

#### Scenario: VGA pins connected
- **WHEN** the design is synthesized and loaded
- **THEN** the VGA connector pins SHALL carry the display output from pvz_top through soc_system
