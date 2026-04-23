/*
 * soc_system_top — DE1-SoC board-level wrapper for the PvZ project
 *
 * Thin pin adapter between the DE1-SoC's physical FPGA pins (named
 * by Terasic's reference constraints) and `soc_system`, the Platform
 * Designer-generated system that holds the HPS, the lightweight
 * HPS-to-FPGA bridge, and our `pvz_top` peripheral.
 *
 * --- How it fits ---
 * See the design document's "System Block Diagram" (Figure 1). The
 * "FPGA fabric" box in that diagram is `soc_system` (instance
 * `soc_system0` below); this file wires its conduit exports to the
 * physical header pins outside the chip.
 *
 * --- Background ---
 *
 * 1. `soc_system` is generated, not hand-written. It lives in
 *    `hw/soc_system.qsys` as a Platform Designer XML description, and
 *    `make qsys` runs `qsys-generate` to emit SystemVerilog into
 *    `hw/soc_system/`. That generated file is the actual module body
 *    for the instantiation below. The QSF tells Quartus to compile
 *    both this file and the generated files together.
 *
 * 2. Most of the port list is stubbed because we do not use those
 *    peripherals. The board manual defines every pin, but the MVP
 *    only lights up CLOCK_50, the VGA DAC, and the HPS DDR3 / SDIO /
 *    USB / Ethernet / UART needed to boot Linux. Everything else is
 *    tied off to a constant or tri-stated through SW[0]/SW[1] near
 *    the bottom of the file to keep Quartus from emitting "no driver"
 *    warnings.
 *
 * 3. Audio is not wired. AUD_* pins are stubbed for now. The design
 *    document lists a future `audio_controller` module that will
 *    drive the Wolfson codec through AUD_DACDAT/AUD_XCK/AUD_BCLK/
 *    AUD_DACLRCK, plus the FPGA_I2C_* pins for codec register setup.
 *
 * 4. DDR3 stubbing. The `DRAM_*` signals are the FPGA-side SDRAM.
 *    Not to be confused with `HPS_DDR3_*`, which is the HPS's own
 *    DDR3 controller and IS wired. The design runs out of on-chip
 *    M10K plus HPS DRAM; the FPGA SDRAM is unused for the MVP.
 *
 * 5. Pin naming matches the DE1-SoC User Manual and the project's
 *    `soc_system.qsf` pin assignments. Renaming any port here without
 *    updating the QSF will produce a fitter error.
 *
 * Adapted from lab3 soc_system_top.sv.
 */

module soc_system_top(

    ///////// ADC /////////
    inout        ADC_CS_N,
    output       ADC_DIN,
    input        ADC_DOUT,
    output       ADC_SCLK,

    ///////// AUD /////////
    input        AUD_ADCDAT,
    inout        AUD_ADCLRCK,
    inout        AUD_BCLK,
    output       AUD_DACDAT,
    inout        AUD_DACLRCK,
    output       AUD_XCK,

    ///////// CLOCK2 /////////
    input        CLOCK2_50,

    ///////// CLOCK3 /////////
    input        CLOCK3_50,

    ///////// CLOCK4 /////////
    input        CLOCK4_50,

    ///////// CLOCK /////////
    input        CLOCK_50,

    ///////// DRAM /////////
    output [12:0] DRAM_ADDR,
    output [1:0]  DRAM_BA,
    output        DRAM_CAS_N,
    output        DRAM_CKE,
    output        DRAM_CLK,
    output        DRAM_CS_N,
    inout  [15:0] DRAM_DQ,
    output        DRAM_LDQM,
    output        DRAM_RAS_N,
    output        DRAM_UDQM,
    output        DRAM_WE_N,

    ///////// FAN /////////
    output       FAN_CTRL,

    ///////// FPGA /////////
    output       FPGA_I2C_SCLK,
    inout        FPGA_I2C_SDAT,

    ///////// GPIO /////////
    inout [35:0] GPIO_0,
    inout [35:0] GPIO_1,

    ///////// HEX /////////
    output [6:0] HEX0,
    output [6:0] HEX1,
    output [6:0] HEX2,
    output [6:0] HEX3,
    output [6:0] HEX4,
    output [6:0] HEX5,

    ///////// HPS /////////
    inout        HPS_CONV_USB_N,
    output [14:0] HPS_DDR3_ADDR,
    output [2:0]  HPS_DDR3_BA,
    output        HPS_DDR3_CAS_N,
    output        HPS_DDR3_CKE,
    output        HPS_DDR3_CK_N,
    output        HPS_DDR3_CK_P,
    output        HPS_DDR3_CS_N,
    output [3:0]  HPS_DDR3_DM,
    inout  [31:0] HPS_DDR3_DQ,
    inout  [3:0]  HPS_DDR3_DQS_N,
    inout  [3:0]  HPS_DDR3_DQS_P,
    output        HPS_DDR3_ODT,
    output        HPS_DDR3_RAS_N,
    output        HPS_DDR3_RESET_N,
    input         HPS_DDR3_RZQ,
    output        HPS_DDR3_WE_N,
    output        HPS_ENET_GTX_CLK,
    inout         HPS_ENET_INT_N,
    output        HPS_ENET_MDC,
    inout         HPS_ENET_MDIO,
    input         HPS_ENET_RX_CLK,
    input  [3:0]  HPS_ENET_RX_DATA,
    input         HPS_ENET_RX_DV,
    output [3:0]  HPS_ENET_TX_DATA,
    output        HPS_ENET_TX_EN,
    inout         HPS_GSENSOR_INT,
    inout         HPS_I2C1_SCLK,
    inout         HPS_I2C1_SDAT,
    inout         HPS_I2C2_SCLK,
    inout         HPS_I2C2_SDAT,
    inout         HPS_I2C_CONTROL,
    inout         HPS_KEY,
    inout         HPS_LED,
    inout         HPS_LTC_GPIO,
    output        HPS_SD_CLK,
    inout         HPS_SD_CMD,
    inout  [3:0]  HPS_SD_DATA,
    output        HPS_SPIM_CLK,
    input         HPS_SPIM_MISO,
    output        HPS_SPIM_MOSI,
    inout         HPS_SPIM_SS,
    input         HPS_UART_RX,
    output        HPS_UART_TX,
    input         HPS_USB_CLKOUT,
    inout  [7:0]  HPS_USB_DATA,
    input         HPS_USB_DIR,
    input         HPS_USB_NXT,
    output        HPS_USB_STP,

    ///////// IRDA /////////
    input        IRDA_RXD,
    output       IRDA_TXD,

    ///////// KEY /////////
    input  [3:0] KEY,

    ///////// LEDR /////////
    output [9:0] LEDR,

    ///////// PS2 /////////
    inout        PS2_CLK,
    inout        PS2_CLK2,
    inout        PS2_DAT,
    inout        PS2_DAT2,

    ///////// SW /////////
    input  [9:0] SW,

    ///////// TD /////////
    input        TD_CLK27,
    input  [7:0] TD_DATA,
    input        TD_HS,
    output       TD_RESET_N,
    input        TD_VS,

    ///////// VGA /////////
    output [7:0] VGA_B,
    output       VGA_BLANK_N,
    output       VGA_CLK,
    output [7:0] VGA_G,
    output       VGA_HS,
    output [7:0] VGA_R,
    output       VGA_SYNC_N,
    output       VGA_VS
);

    // Platform Designer-generated system. The port names here are
    // auto-generated from the interface names in soc_system.qsys: the
    // `pvz_top_0` instance's `vga` conduit, for example, becomes the
    // `pvz_top_0_vga_*` ports, one per signal declared in the `vga`
    // conduit interface of `pvz_top_hw.tcl`.
    soc_system soc_system0(
        .clk_clk                      ( CLOCK_50 ),
        // Reset held high (deasserted). A real reset network would
        // wire this to KEY[0] or the HPS-generated reset.
        .reset_reset_n                ( 1'b1 ),

        // HPS DDR3
        .hps_ddr3_mem_a               ( HPS_DDR3_ADDR ),
        .hps_ddr3_mem_ba              ( HPS_DDR3_BA ),
        .hps_ddr3_mem_ck              ( HPS_DDR3_CK_P ),
        .hps_ddr3_mem_ck_n            ( HPS_DDR3_CK_N ),
        .hps_ddr3_mem_cke             ( HPS_DDR3_CKE ),
        .hps_ddr3_mem_cs_n            ( HPS_DDR3_CS_N ),
        .hps_ddr3_mem_ras_n           ( HPS_DDR3_RAS_N ),
        .hps_ddr3_mem_cas_n           ( HPS_DDR3_CAS_N ),
        .hps_ddr3_mem_we_n            ( HPS_DDR3_WE_N ),
        .hps_ddr3_mem_reset_n         ( HPS_DDR3_RESET_N ),
        .hps_ddr3_mem_dq              ( HPS_DDR3_DQ ),
        .hps_ddr3_mem_dqs             ( HPS_DDR3_DQS_P ),
        .hps_ddr3_mem_dqs_n           ( HPS_DDR3_DQS_N ),
        .hps_ddr3_mem_odt             ( HPS_DDR3_ODT ),
        .hps_ddr3_mem_dm              ( HPS_DDR3_DM ),
        .hps_ddr3_oct_rzqin           ( HPS_DDR3_RZQ ),

        // HPS Ethernet
        .hps_hps_io_emac1_inst_TX_CLK ( HPS_ENET_GTX_CLK ),
        .hps_hps_io_emac1_inst_TXD0   ( HPS_ENET_TX_DATA[0] ),
        .hps_hps_io_emac1_inst_TXD1   ( HPS_ENET_TX_DATA[1] ),
        .hps_hps_io_emac1_inst_TXD2   ( HPS_ENET_TX_DATA[2] ),
        .hps_hps_io_emac1_inst_TXD3   ( HPS_ENET_TX_DATA[3] ),
        .hps_hps_io_emac1_inst_RXD0   ( HPS_ENET_RX_DATA[0] ),
        .hps_hps_io_emac1_inst_MDIO   ( HPS_ENET_MDIO ),
        .hps_hps_io_emac1_inst_MDC    ( HPS_ENET_MDC ),
        .hps_hps_io_emac1_inst_RX_CTL ( HPS_ENET_RX_DV ),
        .hps_hps_io_emac1_inst_TX_CTL ( HPS_ENET_TX_EN ),
        .hps_hps_io_emac1_inst_RX_CLK ( HPS_ENET_RX_CLK ),
        .hps_hps_io_emac1_inst_RXD1   ( HPS_ENET_RX_DATA[1] ),
        .hps_hps_io_emac1_inst_RXD2   ( HPS_ENET_RX_DATA[2] ),
        .hps_hps_io_emac1_inst_RXD3   ( HPS_ENET_RX_DATA[3] ),

        // HPS SD Card
        .hps_hps_io_sdio_inst_CMD     ( HPS_SD_CMD ),
        .hps_hps_io_sdio_inst_D0      ( HPS_SD_DATA[0] ),
        .hps_hps_io_sdio_inst_D1      ( HPS_SD_DATA[1] ),
        .hps_hps_io_sdio_inst_CLK     ( HPS_SD_CLK ),
        .hps_hps_io_sdio_inst_D2      ( HPS_SD_DATA[2] ),
        .hps_hps_io_sdio_inst_D3      ( HPS_SD_DATA[3] ),

        // HPS USB
        .hps_hps_io_usb1_inst_D0      ( HPS_USB_DATA[0] ),
        .hps_hps_io_usb1_inst_D1      ( HPS_USB_DATA[1] ),
        .hps_hps_io_usb1_inst_D2      ( HPS_USB_DATA[2] ),
        .hps_hps_io_usb1_inst_D3      ( HPS_USB_DATA[3] ),
        .hps_hps_io_usb1_inst_D4      ( HPS_USB_DATA[4] ),
        .hps_hps_io_usb1_inst_D5      ( HPS_USB_DATA[5] ),
        .hps_hps_io_usb1_inst_D6      ( HPS_USB_DATA[6] ),
        .hps_hps_io_usb1_inst_D7      ( HPS_USB_DATA[7] ),
        .hps_hps_io_usb1_inst_CLK     ( HPS_USB_CLKOUT ),
        .hps_hps_io_usb1_inst_STP     ( HPS_USB_STP ),
        .hps_hps_io_usb1_inst_DIR     ( HPS_USB_DIR ),
        .hps_hps_io_usb1_inst_NXT     ( HPS_USB_NXT ),

        // HPS SPI
        .hps_hps_io_spim1_inst_CLK    ( HPS_SPIM_CLK ),
        .hps_hps_io_spim1_inst_MOSI   ( HPS_SPIM_MOSI ),
        .hps_hps_io_spim1_inst_MISO   ( HPS_SPIM_MISO ),
        .hps_hps_io_spim1_inst_SS0    ( HPS_SPIM_SS ),

        // HPS UART
        .hps_hps_io_uart0_inst_RX     ( HPS_UART_RX ),
        .hps_hps_io_uart0_inst_TX     ( HPS_UART_TX ),

        // HPS I2C
        .hps_hps_io_i2c0_inst_SDA     ( HPS_I2C1_SDAT ),
        .hps_hps_io_i2c0_inst_SCL     ( HPS_I2C1_SCLK ),
        .hps_hps_io_i2c1_inst_SDA     ( HPS_I2C2_SDAT ),
        .hps_hps_io_i2c1_inst_SCL     ( HPS_I2C2_SCLK ),

        // HPS GPIO
        .hps_hps_io_gpio_inst_GPIO09  ( HPS_CONV_USB_N ),
        .hps_hps_io_gpio_inst_GPIO35  ( HPS_ENET_INT_N ),
        .hps_hps_io_gpio_inst_GPIO40  ( HPS_LTC_GPIO ),
        .hps_hps_io_gpio_inst_GPIO48  ( HPS_I2C_CONTROL ),
        .hps_hps_io_gpio_inst_GPIO53  ( HPS_LED ),
        .hps_hps_io_gpio_inst_GPIO54  ( HPS_KEY ),
        .hps_hps_io_gpio_inst_GPIO61  ( HPS_GSENSOR_INT ),

        // PvZ GPU VGA conduit (exported from Platform Designer)
        .pvz_top_0_vga_r              ( VGA_R ),
        .pvz_top_0_vga_g              ( VGA_G ),
        .pvz_top_0_vga_b              ( VGA_B ),
        .pvz_top_0_vga_clk            ( VGA_CLK ),
        .pvz_top_0_vga_hs             ( VGA_HS ),
        .pvz_top_0_vga_vs             ( VGA_VS ),
        .pvz_top_0_vga_blank_n        ( VGA_BLANK_N ),
        .pvz_top_0_vga_sync_n         ( VGA_SYNC_N )
    );

    // ---------------------------------------------------------------
    // Stubs for unused board peripherals. Every top-level output has
    // to drive something or Quartus emits "no driver" warnings that
    // can mask real problems. We tie these to switches (SW[0]/SW[1])
    // so the compiler keeps them in the design, with values driven by
    // a human-reachable input instead of floating.
    //
    // AUD_*: stubbed until the planned `audio_controller` module
    //        lands (see design document, "Audio Playback (Planned)").
    // DRAM_*: the FPGA-side SDRAM is unused for the MVP.
    // Other assignments: legacy board peripherals (PS/2, GPIO, HEX
    //        displays, LEDs, IR, ADC, etc.) that the current design
    //        does not need.
    // ---------------------------------------------------------------
    assign ADC_CS_N = SW[1] ? SW[0] : 1'bZ;
    assign ADC_DIN = SW[0];
    assign ADC_SCLK = SW[0];

    assign AUD_ADCLRCK = SW[1] ? SW[0] : 1'bZ;
    assign AUD_BCLK = SW[1] ? SW[0] : 1'bZ;
    assign AUD_DACDAT = SW[0];
    assign AUD_DACLRCK = SW[1] ? SW[0] : 1'bZ;
    assign AUD_XCK = SW[0];

    assign DRAM_ADDR = {13{SW[0]}};
    assign DRAM_BA = {2{SW[0]}};
    assign DRAM_DQ = SW[1] ? {16{SW[0]}} : {16{1'bZ}};
    assign {DRAM_CAS_N, DRAM_CKE, DRAM_CLK, DRAM_CS_N,
            DRAM_LDQM, DRAM_RAS_N, DRAM_UDQM, DRAM_WE_N} = {8{SW[0]}};

    assign FAN_CTRL = SW[0];

    assign FPGA_I2C_SCLK = SW[0];
    assign FPGA_I2C_SDAT = SW[1] ? SW[0] : 1'bZ;

    assign GPIO_0 = SW[1] ? {36{SW[0]}} : {36{1'bZ}};
    assign GPIO_1 = SW[1] ? {36{SW[0]}} : {36{1'bZ}};

    assign HEX0 = {7{SW[1]}};
    assign HEX1 = {7{SW[2]}};
    assign HEX2 = {7{SW[3]}};
    assign HEX3 = {7{SW[4]}};
    assign HEX4 = {7{SW[5]}};
    assign HEX5 = {7{SW[6]}};

    assign IRDA_TXD = SW[0];

    assign LEDR = {10{SW[7]}};

    assign PS2_CLK = SW[1] ? SW[0] : 1'bZ;
    assign PS2_CLK2 = SW[1] ? SW[0] : 1'bZ;
    assign PS2_DAT = SW[1] ? SW[0] : 1'bZ;
    assign PS2_DAT2 = SW[1] ? SW[0] : 1'bZ;

    assign TD_RESET_N = SW[0];

endmodule
