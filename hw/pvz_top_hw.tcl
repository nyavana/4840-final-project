# pvz_top_hw.tcl — Platform Designer component descriptor for pvz_top
#
# Tells Platform Designer (Altera/Intel's on-chip bus integrator) how
# to treat the Verilog module `pvz_top` as a reusable IP block.
# Without this file, `pvz_top` is just a SystemVerilog file that
# Quartus can compile. With it, `pvz_top` shows up in the Platform
# Designer IP catalog, can be dropped onto the system bus, and gets a
# matching node in the generated device tree so the Linux kernel
# driver can bind to it at boot.
#
# --- How it fits ---
# See the design document's "Platform Designer Integration" section.
# This is artifact (2) of the three-way binding listed there. The
# other two are:
#   (1) hw/pvz_top.sv         — the Verilog module itself
#   (3) sw/pvz_driver.c       — kernel driver `of_device_id` table
# All three must agree on the compatible string
# "csee4840,pvz_gpu-1.0". If they diverge, the /dev/pvz node never
# appears at boot and nothing logs an error.
#
# --- What a _hw.tcl does ---
# A Platform Designer component descriptor declares:
#   * module metadata (name, version, display name)
#   * embedded-software hints (`embeddedsw.dts.*`) that end up in the
#     generated .dts / .dtb node for this peripheral
#   * the list of HDL source files that make up this IP
#   * Avalon/conduit/clock/reset interfaces and their parameters
#
# `qsys-generate` reads this file plus soc_system.qsys, emits the
# SystemVerilog wrapper that instantiates pvz_top on the fabric, and
# `sopc2dts` later turns the same description into a device-tree
# fragment (the `make dtb` target in hw/Makefile).
#
# --- Address units (read this if you touch the interface) ---
# `set_interface_property s1 addressUnits WORDS` below is the reason
# pvz_top's `address` port is a word index (0..4) even though the CPU
# writes byte-addressed. Platform Designer divides the byte address
# by 4 on the way through the bridge. The kernel driver uses
# `iowrite32(val, base + 4*N)` with byte offsets, and the Verilog
# sees word indices. Every register offset in the design document
# (0x00, 0x04, 0x08, 0x0C, 0x10) is a CPU-side byte offset. Flipping
# addressUnits to SYMBOLS without also rewriting the driver will
# silently mis-address every register.

package require -exact qsys 16.0

# Module properties
set_module_property DESCRIPTION "PvZ GPU — VGA shape-based display engine"
set_module_property NAME pvz_top
set_module_property VERSION 1.0
set_module_property INTERNAL false
set_module_property OPAQUE_ADDRESS_MAP true
set_module_property AUTHOR "CSEE4840 Team"
set_module_property DISPLAY_NAME "PvZ GPU"
set_module_property INSTANTIATE_IN_SYSTEM_MODULE true
set_module_property EDITABLE true
set_module_property REPORT_TO_TALKBACK false
set_module_property ALLOW_GREYBOX_GENERATION false
set_module_property REPORT_HIERARCHY false

# Device-tree assignments. These three lines are what makes the
# peripheral findable by the kernel. `sopc2dts` copies them verbatim
# into the generated device tree; the kernel's driver core matches
# the `compatible` string against each driver's `of_device_id` table.
#
# The compatible string MUST match `pvz_driver.c:pvz_of_match`. If
# you rename it here, rename it there too. There is no build-time
# check that links them.
set_module_assignment embeddedsw.dts.compatible "csee4840,pvz_gpu-1.0"
set_module_assignment embeddedsw.dts.group "pvz_gpu"
set_module_assignment embeddedsw.dts.vendor "csee4840"

# Source fileset for Quartus synthesis. Every Verilog file the
# peripheral needs is registered here; Quartus copies them into the
# generated IP directory when soc_system is rebuilt. TOP_LEVEL_FILE
# marks which one is the module entry point.
#
# `peas_idx.mem` is listed as OTHER because `sprite_rom.sv` loads it
# via `$readmemh` at synthesis/power-up time. It has to be packaged
# with the IP even though it is not a Verilog source. See the design
# doc's "peas_idx.mem — sprite ROM initialiser" subsection under
# "File Formats" for the expected format (ASCII hex, 1024 bytes,
# row-major, 0xFF = transparent).
add_fileset QUARTUS_SYNTH QUARTUS_SYNTH "" ""
set_fileset_property QUARTUS_SYNTH TOP_LEVEL pvz_top
add_fileset_file pvz_top.sv SYSTEM_VERILOG PATH pvz_top.sv TOP_LEVEL_FILE
add_fileset_file vga_counters.sv SYSTEM_VERILOG PATH vga_counters.sv
add_fileset_file linebuffer.sv SYSTEM_VERILOG PATH linebuffer.sv
add_fileset_file bg_grid.sv SYSTEM_VERILOG PATH bg_grid.sv
add_fileset_file shape_table.sv SYSTEM_VERILOG PATH shape_table.sv
add_fileset_file shape_renderer.sv SYSTEM_VERILOG PATH shape_renderer.sv
add_fileset_file color_palette.sv SYSTEM_VERILOG PATH color_palette.sv
add_fileset_file sprite_rom.sv SYSTEM_VERILOG PATH sprite_rom.sv
add_fileset_file peas_idx.mem OTHER PATH peas_idx.mem

# Clock interface
add_interface clock clock end
set_interface_property clock clockRate 0
add_interface_port clock clk clk Input 1

# Reset interface
add_interface reset reset end
set_interface_property reset associatedClock clock
set_interface_property reset synchronousEdges DEASSERT
add_interface_port reset reset reset Input 1

# Avalon-MM slave interface ("s1"). The window the CPU writes
# through. addressUnits=WORDS means the `address` port of pvz_top
# carries word indices (0..4) rather than byte addresses. See the
# header block at the top of this file for the full explanation.
add_interface s1 avalon end
set_interface_property s1 addressUnits WORDS
set_interface_property s1 associatedClock clock
set_interface_property s1 associatedReset reset
set_interface_property s1 bitsPerSymbol 8
set_interface_property s1 burstOnBurstBoundariesOnly false
set_interface_property s1 burstcountUnits WORDS
set_interface_property s1 explicitAddressSpan 0
set_interface_property s1 holdTime 0
set_interface_property s1 linewrapBursts false
set_interface_property s1 maximumPendingReadTransactions 0
set_interface_property s1 maximumPendingWriteTransactions 0
set_interface_property s1 readLatency 0
set_interface_property s1 readWaitTime 1
set_interface_property s1 setupTime 0
set_interface_property s1 timingUnits Cycles
set_interface_property s1 writeWaitTime 0

add_interface_port s1 address address Input 3
add_interface_port s1 writedata writedata Input 32
add_interface_port s1 write write Input 1
add_interface_port s1 chipselect chipselect Input 1

# VGA conduit interface. "Conduit" is Platform Designer's term for
# signals that speak no bus protocol. They just pass through the
# fabric and surface as ports on the generated top-level. In
# soc_system_top.sv these show up as `pvz_top_0_vga_r`, etc., and we
# wire them to the VGA_* board pins.
add_interface vga conduit end
set_interface_property vga associatedClock clock
set_interface_property vga associatedReset reset

add_interface_port vga VGA_R r Output 8
add_interface_port vga VGA_G g Output 8
add_interface_port vga VGA_B b Output 8
add_interface_port vga VGA_CLK clk Output 1
add_interface_port vga VGA_HS hs Output 1
add_interface_port vga VGA_VS vs Output 1
add_interface_port vga VGA_BLANK_n blank_n Output 1
add_interface_port vga VGA_SYNC_n sync_n Output 1
