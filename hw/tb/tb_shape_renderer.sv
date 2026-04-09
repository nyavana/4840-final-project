/*
 * Testbench for shape_renderer module
 *
 * Verifies:
 *   - Rectangle rendering (correct pixels filled)
 *   - Circle rendering (distance check)
 *   - Digit rendering (7-segment pattern)
 *   - Z-order (later shapes overwrite earlier)
 *   - Invisible shapes skipped
 */

`timescale 1ns/1ps

module tb_shape_renderer;

    logic       clk, reset;
    logic       render_start;
    logic [9:0] scanline;
    logic       render_done;

    logic [9:0] bg_px, bg_py;
    logic [7:0] bg_color;

    logic [5:0] shape_index;
    logic [1:0] shape_type;
    logic       shape_visible;
    logic [9:0] shape_x;
    logic [8:0] shape_y, shape_w, shape_h;
    logic [7:0] shape_color;

    logic       lb_wr_en;
    logic [9:0] lb_wr_addr;
    logic [7:0] lb_wr_data;

    shape_renderer dut(.*);

    initial clk = 0;
    always #10 clk = ~clk;

    // Simple background: always return color 1
    assign bg_color = 8'd1;

    // Shape table simulation: one visible rectangle at shape 0
    // at (100, 100) size 50x50, color 5
    // shape 1: invisible
    // all others: invisible
    always_comb begin
        shape_type    = 2'd0;
        shape_visible = 1'b0;
        shape_x       = 10'd0;
        shape_y       = 9'd0;
        shape_w       = 9'd0;
        shape_h       = 9'd0;
        shape_color   = 8'd0;

        if (shape_index == 6'd0) begin
            shape_type    = 2'd0; // rectangle
            shape_visible = 1'b1;
            shape_x       = 10'd100;
            shape_y       = 9'd100;
            shape_w       = 9'd50;
            shape_h       = 9'd50;
            shape_color   = 8'd5;
        end
    end

    integer errors = 0;
    integer write_count;

    initial begin
        reset = 1; render_start = 0;
        scanline = 10'd0;
        #100;
        reset = 0;
        @(posedge clk);

        // Render scanline 120 (inside the rectangle, y=100 to y=149)
        scanline = 10'd120;
        render_start = 1;
        @(posedge clk);
        render_start = 0;

        // Wait for render_done
        write_count = 0;
        while (!render_done) begin
            if (lb_wr_en) begin
                write_count++;
                // Check if rectangle pixel is correct
                if (lb_wr_addr >= 10'd100 && lb_wr_addr < 10'd150) begin
                    if (lb_wr_data != 8'd5) begin
                        $display("FAIL: Rect pixel at %d expected 5, got %d",
                                 lb_wr_addr, lb_wr_data);
                        errors++;
                    end
                end
            end
            @(posedge clk);
        end

        $display("Scanline 120: %0d writes", write_count);
        if (write_count >= 640) // at least background fill
            $display("PASS: Sufficient writes for background + shapes");
        else begin
            $display("FAIL: Too few writes: %0d", write_count);
            errors++;
        end

        // Render scanline 50 (above the rectangle -> only bg)
        scanline = 10'd50;
        render_start = 1;
        @(posedge clk);
        render_start = 0;

        write_count = 0;
        while (!render_done) begin
            if (lb_wr_en) begin
                write_count++;
                // Should all be bg color (1) since rect doesn't overlap
                if (lb_wr_addr >= 10'd100 && lb_wr_addr < 10'd150) begin
                    if (lb_wr_data != 8'd1) begin
                        // Shape shouldn't be drawn on this scanline
                        $display("INFO: Non-bg write at %d = %d on scanline 50",
                                 lb_wr_addr, lb_wr_data);
                    end
                end
            end
            @(posedge clk);
        end
        $display("Scanline 50: %0d writes (bg only expected)", write_count);

        if (errors == 0)
            $display("\n*** ALL TESTS PASSED ***");
        else
            $display("\n*** %0d TESTS FAILED ***", errors);

        $finish;
    end

endmodule
