# hw/Makefile — Quartus/Platform Designer build flow for PvZ GPU
#
# Adapted from lab3 hw/Makefile.
# Compatible string: csee4840,pvz_gpu-1.0
# Run on Columbia micro*.ee.columbia.edu workstations.

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

# -----------------------------------------------------------
# audio: regenerate bgm_rom.mem + sfx_rom.mem + sfx_offsets.svh from assets
# -----------------------------------------------------------
.PHONY : audio
audio :
	python3 audio/build_audio_mif.py
