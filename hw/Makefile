# hw/Makefile — Quartus/Platform Designer build flow for the PvZ GPU
#
# Drives the FPGA half of the project, from .qsys/.sv sources to the
# two files the board actually consumes:
#   * output_files/soc_system.rbf — the FPGA bitstream
#   * soc_system.dtb              — the Linux device-tree blob
#
# --- Targets ---
#   qsys     — run Platform Designer (`qsys-generate`) on
#              soc_system.qsys to emit SystemVerilog for the whole
#              system (including the wrapper that instantiates
#              pvz_top). Produces soc_system.sopcinfo and the
#              soc_system/ source tree.
#   quartus  — full Quartus flow: map, fit, assemble, timing. Needs
#              the qsys output. Produces output_files/soc_system.sof.
#   rbf      — convert .sof to .rbf so U-Boot can load it from the
#              SD card at boot.
#   dtb      — sopc2dts + dtc: turn the sopcinfo into a .dts, then
#              compile to a .dtb. The `csee4840,pvz_gpu-1.0` node in
#              this .dtb is what lets the kernel driver bind to our
#              peripheral at boot.
#
# --- Where to run it ---
# On the Columbia micro*.ee.columbia.edu workstations with the Intel
# FPGA tools in PATH. `dtb` in particular needs the embedded-command-
# shell environment (`embedded_command_shell.sh`) so `dtc` and
# `sopc2dts` resolve.
#
# --- Cross-references ---
# Three-way binding: soc_system.qsys instantiates pvz_top via the
# descriptor in pvz_top_hw.tcl, which publishes compatible string
# "csee4840,pvz_gpu-1.0". The kernel driver (sw/pvz_driver.c) must
# match that exact string. See the "Platform Designer Integration"
# section of the design document.
#
# Adapted from lab3 hw/Makefile.

SYSTEM = soc_system

TCL = $(SYSTEM).tcl
QSYS = $(SYSTEM).qsys
SOPCINFO = $(SYSTEM).sopcinfo
QIP = $(SYSTEM)/synthesis/$(SYSTEM).qip
HPS_PIN_TCL = $(SYSTEM)/synthesis/submodules/hps_sdram_p0_pin_assignments.tcl

QPF = $(SYSTEM).qpf
QSF = $(SYSTEM).qsf
SDC = $(SYSTEM).sdc
SRF = $(SYSTEM).srf

BOARD_INFO = $(SYSTEM)_board_info.xml
DTS = $(SYSTEM).dts
DTB = $(SYSTEM).dtb

SOF = output_files/$(SYSTEM).sof
RBF = output_files/$(SYSTEM).rbf

# -----------------------------------------------------------
# project: generate initial project files from tcl
# -----------------------------------------------------------
.PHONY : project
project : $(QPF) $(QSF) $(SDC)

$(QPF) $(QSF) $(SDC) : $(TCL) $(HPS_PIN_TCL)
	quartus_sh -t $(TCL)
	quartus_map $(SYSTEM)
	quartus_sta -t $(HPS_PIN_TCL) $(SYSTEM)

# -----------------------------------------------------------
# qsys: generate Verilog from Platform Designer .qsys file
# -----------------------------------------------------------
.PHONY : qsys
qsys : $(SOPCINFO)

$(SOPCINFO) $(QIP) $(HPS_PIN_TCL) $(SYSTEM)/ : $(QSYS)
	rm -rf $(SOPCINFO) $(SYSTEM)/
	qsys-generate $(QSYS) --synthesis=VERILOG

# -----------------------------------------------------------
# quartus: full synthesis, fitting, and bitstream generation
# -----------------------------------------------------------
.PHONY : quartus
quartus : $(SOF)

$(SOF) : $(QIP) $(QPF) $(QSF) $(HPS_PIN_TCL)
	quartus_sh --flow compile $(QPF)

# -----------------------------------------------------------
# rbf: convert .sof to .rbf for SD card deployment
# -----------------------------------------------------------
.PHONY : rbf
rbf : $(RBF)

$(RBF) : $(SOF)
	quartus_cpf -c $(SOF) $(RBF)

# -----------------------------------------------------------
# dtb: generate device tree blob from sopcinfo
# -----------------------------------------------------------
.PHONY : dtb
dtb : $(DTB)

$(DTB) : $(DTS)
	@which dtc || (echo "dtc not found. Did you run embedded_command_shell.sh?"; exit 1)
	dtc -I dts -O dtb -o $(DTB) $(DTS)

# Generate the .dts from the Platform Designer sopcinfo.
# --board pulls in board-level info (clocks, off-chip connections)
# that Platform Designer alone does not know about.
$(DTS) : $(SOPCINFO) $(BOARD_INFO)
	@which sopc2dts || (echo "sopc2dts not found. Did you run embedded_command_shell.sh?"; exit 1)
	sopc2dts --input $(SOPCINFO) \
		--output $(DTS) \
		--type dts \
		--board $(BOARD_INFO) \
		--clocks

# -----------------------------------------------------------
# clean
# -----------------------------------------------------------
.PHONY : clean quartus-clean qsys-clean project-clean dtb-clean

clean : quartus-clean qsys-clean project-clean dtb-clean

project-clean :
	rm -rf $(QPF) $(QSF) $(SDC)

qsys-clean :
	rm -rf $(SOPCINFO) $(QIP) $(SYSTEM)/ .qsys_edit \
	hps_isw_handoff/ hps_sdram_p0_summary.csv

quartus-clean :
	rm -rf $(SOF) output_files db incremental_db $(SYSTEM).qdf \
	c5_pin_model_dump.txt

dtb-clean :
	rm -rf $(DTS) $(DTB)
