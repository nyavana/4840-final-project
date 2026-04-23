/*
 * tb_vga_counters.sv — unit testbench for the VGA timing generator
 *
 * Role
 *   Runs vga_counters for two full frames off a 50 MHz clock and spot-
 *   checks that hcount/vcount wrap at the right values, that VGA_BLANK_n
 *   tracks the active-video window, and that VGA_CLK is hcount[0] (the
 *   divide-by-two pixel clock). Simulation-only.
 *
 * How it fits
 *   Covers the "VGA timing" testbench goal in design-document §Testing
 *   and Verification. Reference values come from the 640x480@60 Hz
 *   timing in §Timing, Table 4: H Total = 1600 pixel-clock ticks, V
 *   Total = 525 lines, frame rate ~59.5 Hz. The DUT produces hcount
 *   (0..1599) and vcount (0..524), plus the VGA_HS / VGA_VS /
 *   VGA_BLANK_n / VGA_SYNC_n / VGA_CLK pins the VGA DAC expects.
 *
 * Background concepts
 *   - hcount runs at the 50 MHz board clock; every two ticks produces one
 *     VGA pixel clock (so VGA_CLK = hcount[0]). 1600 ticks cover 800
 *     visible+blank pixels: 1280 active, then front porch, sync, back
 *     porch until wrap.
 *   - hcount = 1280 sits inside the horizontal blank window, so
 *     VGA_BLANK_n must be low there. Inside the active window
 *     VGA_BLANK_n is high and the DAC should be driving the RGB lines.
 *   - Standard SV TB scaffolding. Free-running clk50 via
 *     `always #10 clk50 = ~clk50;` (20 ns period = 50 MHz with the
 *     1ns/1ps timescale). Reset held high briefly, then dropped.
 *     Assertions are inline if/else blocks that bump an `errors`
 *     accumulator and print PASS/FAIL through $display; the run ends
 *     with $finish.
 *
 * Test phases
 *   1. Run two full frames (2 * 1600 * 525 cycles) to let the counters
 *      settle and hit every state.
 *   2. Wait until hcount reaches 1599, confirm the next cycle wraps to 0.
 *   3. Wait until vcount reaches 524 (last line before frame wrap).
 *   4. Re-align on (hcount=0, vcount=0) and sample VGA_BLANK_n one cycle
 *      later to confirm it is high in active video.
 *   5. Step to hcount = 1280 (inside horizontal blanking) and confirm
 *      VGA_BLANK_n is low there.
 *   6. Confirm VGA_CLK == hcount[0] for the current cycle (the
 *      divide-by-two relationship documented in §Timing).
 */

`timescale 1ns/1ps

module tb_vga_counters;

    logic        clk50;
    logic        reset;
    logic [10:0] hcount;
    logic [9:0]  vcount;
    logic        VGA_CLK, VGA_HS, VGA_VS, VGA_BLANK_n, VGA_SYNC_n;

    vga_counters dut(
        .clk50(clk50),
        .reset(reset),
        .hcount(hcount),
        .vcount(vcount),
        .VGA_CLK(VGA_CLK),
        .VGA_HS(VGA_HS),
        .VGA_VS(VGA_VS),
        .VGA_BLANK_n(VGA_BLANK_n),
        .VGA_SYNC_n(VGA_SYNC_n)
    );

    // 50 MHz clock: 20 ns period
    initial clk50 = 0;
    always #10 clk50 = ~clk50;

    integer errors = 0;

    initial begin
        reset = 1;
        #100;
        reset = 0;

        // Wait for first full frame (1600 * 525 = 840000 cycles)
        // Each cycle = 20ns -> 16.8ms
        // Run for 2 full frames
        repeat (2 * 1600 * 525) @(posedge clk50);

        // Check that hcount reaches 1599
        @(posedge clk50);
        while (hcount != 11'd1599) @(posedge clk50);
        $display("PASS: hcount reached 1599");

        // Check hcount wraps to 0
        @(posedge clk50);
        if (hcount != 11'd0) begin
            $display("FAIL: hcount did not wrap to 0, got %d", hcount);
            errors++;
        end else begin
            $display("PASS: hcount wrapped to 0");
        end

        // Wait for vcount to reach 524
        while (vcount != 10'd524) @(posedge clk50);
        $display("PASS: vcount reached 524");

        // Check blanking during active area
        // Go to hcount=0, vcount=0
        while (!(hcount == 11'd0 && vcount == 10'd0)) @(posedge clk50);
        // At (0,0), should be in active area on even hcount
        @(posedge clk50); // hcount=1
        if (!VGA_BLANK_n) begin
            $display("FAIL: VGA_BLANK_n should be high at (1,0)");
            errors++;
        end else begin
            $display("PASS: VGA_BLANK_n is high in active area");
        end

        // Check blanking during horizontal blank
        while (hcount != 11'd1280) @(posedge clk50);
        if (VGA_BLANK_n) begin
            $display("FAIL: VGA_BLANK_n should be low at hcount=1280");
            errors++;
        end else begin
            $display("PASS: VGA_BLANK_n is low during h-blank");
        end

        // Check VGA_CLK is hcount[0]
        @(posedge clk50);
        if (VGA_CLK != hcount[0]) begin
            $display("FAIL: VGA_CLK != hcount[0]");
            errors++;
        end else begin
            $display("PASS: VGA_CLK == hcount[0]");
        end

        if (errors == 0)
            $display("\n*** ALL TESTS PASSED ***");
        else
            $display("\n*** %0d TESTS FAILED ***", errors);

        $finish;
    end

endmodule
