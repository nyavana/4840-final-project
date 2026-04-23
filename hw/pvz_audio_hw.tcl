# pvz_audio_hw.tcl — Platform Designer component descriptor for pvz_audio
#
# Defines the Avalon-MM slave and Avalon-ST source interfaces so Platform
# Designer can generate the bus fabric and device tree entries automatically.

package require -exact qsys 16.0

# Module properties
set_module_property DESCRIPTION "PvZ audio peripheral"
set_module_property NAME pvz_audio
set_module_property VERSION 1.0
set_module_property INTERNAL false
set_module_property OPAQUE_ADDRESS_MAP true
set_module_property AUTHOR "CSEE4840 Team"
set_module_property DISPLAY_NAME pvz_audio
set_module_property INSTANTIATE_IN_SYSTEM_MODULE true
set_module_property EDITABLE true
set_module_property REPORT_TO_TALKBACK false
set_module_property ALLOW_GREYBOX_GENERATION false
set_module_property REPORT_HIERARCHY false

# Device tree compatible string — must match kernel driver's of_device_id
set_module_assignment embeddedsw.dts.compatible "csee4840,pvz_audio-1.0"
set_module_assignment embeddedsw.dts.group      audio
set_module_assignment embeddedsw.dts.vendor     csee4840
set_module_assignment embeddedsw.dts.name       pvz_audio

# File set
add_fileset QUARTUS_SYNTH QUARTUS_SYNTH "" ""
set_fileset_property QUARTUS_SYNTH TOP_LEVEL pvz_audio
set_fileset_property QUARTUS_SYNTH ENABLE_RELATIVE_INCLUDE_PATHS false
set_fileset_property QUARTUS_SYNTH ENABLE_FILE_OVERWRITE_MODE false
add_fileset_file pvz_audio.sv        SYSTEM_VERILOG         PATH pvz_audio.sv TOP_LEVEL_FILE
add_fileset_file mixer.sv            SYSTEM_VERILOG         PATH mixer.sv
add_fileset_file voice_bgm.sv        SYSTEM_VERILOG         PATH voice_bgm.sv
add_fileset_file voice_sfx.sv        SYSTEM_VERILOG         PATH voice_sfx.sv
add_fileset_file bgm_rom.sv          SYSTEM_VERILOG         PATH bgm_rom.sv
add_fileset_file sfx_rom.sv          SYSTEM_VERILOG         PATH sfx_rom.sv
add_fileset_file bgm_rom.mem         HEX                    PATH bgm_rom.mem
add_fileset_file sfx_rom.mem         HEX                    PATH sfx_rom.mem
add_fileset_file sfx_offsets.svh     SYSTEM_VERILOG_INCLUDE PATH sfx_offsets.svh

# Clock interface
add_interface clock clock end
set_interface_property clock clockRate 0
set_interface_property clock ENABLED true
add_interface_port clock clk clk Input 1

# Reset interface
add_interface reset reset end
set_interface_property reset associatedClock clock
set_interface_property reset synchronousEdges DEASSERT
set_interface_property reset ENABLED true
add_interface_port reset reset reset Input 1

# Avalon-MM slave interface — addressUnits WORDS, 32-bit writedata
add_interface avalon_slave avalon end
set_interface_property avalon_slave addressUnits WORDS
set_interface_property avalon_slave associatedClock clock
set_interface_property avalon_slave associatedReset reset
set_interface_property avalon_slave bitsPerSymbol 8
set_interface_property avalon_slave burstOnBurstBoundariesOnly false
set_interface_property avalon_slave burstcountUnits WORDS
set_interface_property avalon_slave explicitAddressSpan 0
set_interface_property avalon_slave holdTime 0
set_interface_property avalon_slave linewrapBursts false
set_interface_property avalon_slave maximumPendingReadTransactions 0
set_interface_property avalon_slave maximumPendingWriteTransactions 0
set_interface_property avalon_slave readLatency 0
set_interface_property avalon_slave readWaitTime 1
set_interface_property avalon_slave setupTime 0
set_interface_property avalon_slave timingUnits Cycles
set_interface_property avalon_slave writeWaitTime 0
set_interface_property avalon_slave ENABLED true
add_interface_port avalon_slave address    address   Input   3
add_interface_port avalon_slave chipselect chipselect Input   1
add_interface_port avalon_slave write      write     Input   1
add_interface_port avalon_slave read       read      Input   1
add_interface_port avalon_slave writedata  writedata Input  32
add_interface_port avalon_slave readdata   readdata  Output 32
set_interface_assignment avalon_slave embeddedsw.configuration.isFlash 0
set_interface_assignment avalon_slave embeddedsw.configuration.isMemoryDevice 0
set_interface_assignment avalon_slave embeddedsw.configuration.isNonVolatileStorage 0
set_interface_assignment avalon_slave embeddedsw.configuration.isPrintableDevice 0

# Avalon-ST source — left channel
add_interface audio_out_l avalon_streaming start
set_interface_property audio_out_l associatedClock clock
set_interface_property audio_out_l associatedReset reset
set_interface_property audio_out_l dataBitsPerSymbol 16
set_interface_property audio_out_l errorDescriptor ""
set_interface_property audio_out_l firstSymbolInHighOrderBits true
set_interface_property audio_out_l maxChannel 0
set_interface_property audio_out_l readyLatency 0
set_interface_property audio_out_l ENABLED true
add_interface_port audio_out_l left_chan_ready ready Input  1
add_interface_port audio_out_l sample_data_l   data  Output 16
add_interface_port audio_out_l sample_valid_l  valid Output 1

# Avalon-ST source — right channel
add_interface audio_out_r avalon_streaming start
set_interface_property audio_out_r associatedClock clock
set_interface_property audio_out_r associatedReset reset
set_interface_property audio_out_r dataBitsPerSymbol 16
set_interface_property audio_out_r firstSymbolInHighOrderBits true
set_interface_property audio_out_r maxChannel 0
set_interface_property audio_out_r readyLatency 0
set_interface_property audio_out_r ENABLED true
add_interface_port audio_out_r right_chan_ready ready Input  1
add_interface_port audio_out_r sample_data_r    data  Output 16
add_interface_port audio_out_r sample_valid_r   valid Output 1
