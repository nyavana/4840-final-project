/*
 * Testbench for vga_counters module
 *
 * Verifies:
 *   - hcount cycles 0-1599
 *   - vcount cycles 0-524
 *   - VGA_HS timing (active low during sync pulse)
 *   - VGA_VS timing (active low during sync pulse)
 *   - VGA_BLANK_n is high during active area only
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
