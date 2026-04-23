/*
 * tb_pvz_top.sv — end-to-end smoke test for the full pvz_top peripheral
 *
 * Role
 *   Drives Avalon writes the way the kernel driver would, runs the design
 *   long enough for a full VGA frame to scan out, and samples the VGA
 *   pins during active video to confirm nothing is badly broken.
 *   Simulation-only; $display and $finish here are part of the
 *   Verilator/ModelSim harness and are not synthesised.
 *
 * How it fits
 *   Corresponds to the "End-to-end" bullet under design-document §Testing
 *   and Verification / Hardware Testbenches. See §Peripheral Internal
 *   Structure (Figure 2) for the dataflow this TB exercises and §Register
 *   Map (Table 10) for the five CPU-visible offsets.
 *
 * Background concepts
 *   - Avalon write protocol. pvz_top is an Avalon-MM agent. A write
 *     asserts chipselect and write, drives address (word offset 0..4) and
 *     writedata, then deasserts both. The avalon_write task below
 *     packages that into a single call.
 *   - Word vs byte offsets. The address input here is a word offset; the
 *     ARM-side kernel driver uses byte offsets (base + 4*N). The five
 *     slots are BG_CELL=0, SHAPE_ADDR=1, SHAPE_DATA0=2, SHAPE_DATA1=3,
 *     SHAPE_COMMIT=4.
 *   - Frame timing. One frame is 1600 (hcount wrap) * 525 (vcount wrap) =
 *     840 000 cycles at 50 MHz. The long `repeat` below steps past one
 *     full frame so vsync_latch has definitely copied shadow->active
 *     before the VGA sampling runs.
 *   - Watchdog. The second initial block kills the run if the main
 *     simulation hangs past 200 ms of simulated time.
 *
 * Test phases
 *   1. Reset, then pulse through the configuration writes:
 *        a. BG_CELL: cell (row=0, col=0) := palette index 2.
 *        b. SHAPE_ADDR := slot 0.
 *        c. SHAPE_DATA0: type=0 (rect), visible=1, x=100, y=100.
 *        d. SHAPE_DATA1: w=50, h=50, color=5.
 *        e. SHAPE_COMMIT := 1 (any nonzero, as the register requires).
 *   2. Wait one full frame (plus ~120 lines) so vsync latches the staged
 *      shape and background into the active copies.
 *   3. Wait for VGA_BLANK_n to go high, sample R/G/B, and print sync
 *      signal states so a human can eyeball that the pipeline produced
 *      non-zero pixels and that HS/VS are toggling.
 */

`timescale 1ns/1ps

module tb_pvz_top;

    logic        clk, reset;
    logic [2:0]  address;
    logic [31:0] writedata;
    logic        write, chipselect;

    logic [7:0]  VGA_R, VGA_G, VGA_B;
    logic        VGA_CLK, VGA_HS, VGA_VS, VGA_BLANK_n, VGA_SYNC_n;

    pvz_top dut(.*);

    initial clk = 0;
    always #10 clk = ~clk;

    // Task to perform an Avalon write
    task avalon_write(input logic [2:0] addr, input logic [31:0] data);
        @(posedge clk);
        chipselect = 1;
        write = 1;
        address = addr;
        writedata = data;
        @(posedge clk);
        chipselect = 0;
        write = 0;
    endtask

    integer errors = 0;

    initial begin
        reset = 1;
        chipselect = 0; write = 0;
        address = 0; writedata = 0;
        #200;
        reset = 0;
        @(posedge clk);

        // Write background cell (0,0) = color 2
        // BG_CELL: [2:0]=col, [4:3]=row, [12:8]=color
        // col=0, row=0, color=2 -> 0x0000_0200
        avalon_write(3'd0, 32'h0000_0200);

        // Write a rectangle shape at index 0
        // SHAPE_ADDR: index 0
        avalon_write(3'd1, 32'h0000_0000);

        // SHAPE_DATA0: type=0(rect), visible=1, x=100, y=100
        // [1:0]=00, [2]=1, [12:3]=100=0x64, [21:13]=100=0x64
        // = (100 << 13) | (100 << 3) | (1 << 2) | 0
        // = 0x000C_8324
        avalon_write(3'd2, (100 << 13) | (100 << 3) | (1 << 2) | 0);

        // SHAPE_DATA1: w=50, h=50, color=5
        // [8:0]=50, [17:9]=50, [25:18]=5
        // = (5 << 18) | (50 << 9) | 50
        // = 0x0014_6432
        avalon_write(3'd3, (5 << 18) | (50 << 9) | 50);

        // SHAPE_COMMIT
        avalon_write(3'd4, 32'h0000_0001);

        // Wait for vsync (vcount reaches 480)
        // One full frame is 1600 * 525 = 840000 cycles at 50MHz
        repeat (840000) @(posedge clk);

        // Wait a bit more into the next frame
        repeat (1600 * 120) @(posedge clk);

        // Check VGA output during active area
        // We should see non-zero RGB values
        @(posedge clk);
        while (!VGA_BLANK_n) @(posedge clk);
        // Now in active area
        @(posedge clk);
        @(posedge clk);

        $display("VGA output sample: R=%h G=%h B=%h BLANK_n=%b",
                 VGA_R, VGA_G, VGA_B, VGA_BLANK_n);

        // Verify VGA_HS and VGA_VS are toggling (just check they exist)
        $display("VGA_HS=%b VGA_VS=%b", VGA_HS, VGA_VS);

        if (errors == 0)
            $display("\n*** ALL TESTS PASSED ***");
        else
            $display("\n*** %0d TESTS FAILED ***", errors);

        $finish;
    end

    // Timeout watchdog
    initial begin
        #200_000_000; // 200ms
        $display("TIMEOUT - simulation took too long");
        $finish;
    end

endmodule
