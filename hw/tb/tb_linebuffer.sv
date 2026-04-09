/*
 * Testbench for linebuffer module
 *
 * Verifies:
 *   - Write to draw buffer and read from display buffer
 *   - Swap toggles roles correctly
 *   - Boundary pixels (0 and 639)
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
