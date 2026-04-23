/*
 * tb_linebuffer.sv — unit testbench for the double-banked linebuffer
 *
 * Role
 *   Fills one bank through the write port, swaps roles, and reads back
 *   through the display port to confirm the draw/display split works.
 *   Simulation-only.
 *
 * How it fits
 *   Covers the linebuffer swap scenario in design-document §Testing and
 *   Verification / Hardware Testbenches: "confirm that buffer roles
 *   toggle correctly on the lb_swap pulse and that read and write ports
 *   never alias the same bank simultaneously". The real system drives
 *   swap from vga_counters at hcount=0; this TB pulses it directly. See
 *   §VGA Rendering Pipeline (§3.1) and Figure 3 for the per-scanline
 *   timing the linebuffer is built around.
 *
 * Background concepts
 *   - Double-banked line buffer. Two 640x8 banks. A sel flip-flop inside
 *     linebuffer picks one as "display" (read port) and the other as
 *     "draw" (write port). swap toggles sel, so on every hsync the roles
 *     flip and no pixel ever moves between banks.
 *   - 1-cycle read latency. Registered read-through. The TB drives
 *     rd_addr and waits two posedge clk before sampling rd_data: one to
 *     clock the address into the bank, one for the registered output.
 *   - Standard SV TB scaffolding. Free-running clock via `always #10 clk
 *     = ~clk;`, reset sequence in an initial block, stimulus-then-check,
 *     $display/$finish for reporting.
 *
 * Test phases
 *   1. Reset, then write the pattern pixel[i] = i[7:0] into the current
 *      draw bank across all 640 addresses.
 *   2. With no swap yet, read the display bank. It should still be the
 *      untouched all-zero bank, proving the write port did not leak into
 *      the read bank.
 *   3. Pulse swap once; the bank we just wrote becomes the display bank.
 *      Read pixels 0, 255, and 639 and compare to the pattern (255 is an
 *      exact match for the low byte, 639 wraps to 127).
 */

`timescale 1ns/1ps

module tb_linebuffer;

    logic       clk, reset;
    logic       wr_en;
    logic [9:0] wr_addr;
    logic [7:0] wr_data;
    logic [9:0] rd_addr;
    logic [7:0] rd_data;
    logic       swap;

    linebuffer dut(.*);

    initial clk = 0;
    always #10 clk = ~clk;

    integer errors = 0;

    initial begin
        reset = 1; wr_en = 0; swap = 0;
        wr_addr = 0; wr_data = 0; rd_addr = 0;
        #100;
        reset = 0;
        @(posedge clk);

        // Write pattern to draw buffer (buf1, since sel=0 -> display=buf0, draw=buf1)
        for (int i = 0; i < 640; i++) begin
            wr_en = 1;
            wr_addr = i;
            wr_data = i[7:0];
            @(posedge clk);
        end
        wr_en = 0;
        @(posedge clk);

        // Before swap, reading should come from buf0 (all zeros)
        rd_addr = 10'd100;
        @(posedge clk);
        @(posedge clk); // 1 cycle read latency
        if (rd_data != 8'd0) begin
            $display("FAIL: Before swap, expected 0 at addr 100, got %d", rd_data);
            errors++;
        end else begin
            $display("PASS: Before swap, display buffer reads 0");
        end

        // Swap: now buf1 becomes display, buf0 becomes draw
        swap = 1;
        @(posedge clk);
        swap = 0;
        @(posedge clk);

        // Read back the pattern we wrote (now in display buffer = buf1)
        rd_addr = 10'd0;
        @(posedge clk);
        @(posedge clk);
        if (rd_data != 8'd0) begin
            $display("FAIL: After swap, expected 0 at addr 0, got %d", rd_data);
            errors++;
        end else begin
            $display("PASS: After swap, pixel 0 correct");
        end

        rd_addr = 10'd255;
        @(posedge clk);
        @(posedge clk);
        if (rd_data != 8'd255) begin
            $display("FAIL: After swap, expected 255 at addr 255, got %d", rd_data);
            errors++;
        end else begin
            $display("PASS: After swap, pixel 255 correct");
        end

        // Boundary: pixel 639
        rd_addr = 10'd639;
        @(posedge clk);
        @(posedge clk);
        // 639 & 0xFF = 127
        if (rd_data != 8'd127) begin
            $display("FAIL: Boundary pixel 639, expected 127, got %d", rd_data);
            errors++;
        end else begin
            $display("PASS: Boundary pixel 639 correct");
        end

        if (errors == 0)
            $display("\n*** ALL TESTS PASSED ***");
        else
            $display("\n*** %0d TESTS FAILED ***", errors);

        $finish;
    end

endmodule
