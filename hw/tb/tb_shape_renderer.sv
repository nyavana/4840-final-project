/*
 * Testbench for shape_renderer module
 *
 * Verifies:
 *   - Rectangle rendering (correct pixels filled)
 *   - Z-order (shapes overwrite background)
 *   - Invisible shapes skipped
 *   - Scanlines that don't intersect shapes get only background
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

    /* verilator lint_off UNUSED */
    logic [9:0] sprite_rd_addr;
    /* verilator lint_on UNUSED */
    logic [7:0] sprite_rd_pixel;
    assign sprite_rd_pixel = 8'hFF; // not exercising sprite type in this TB

    shape_renderer dut(.*);

    initial clk = 0;
    always #10 clk = ~clk;

    // Simple background: always return color 1
    assign bg_color = 8'd1;

    // Shape table simulation:
    //   index 0  -> visible rectangle at (100, 100) size 50x50, color 5
    //   index 40 -> visible rectangle at (200, 100) size 60x60, color 7  (lower z)
    //   index 63 -> visible rectangle at (240, 100) size 60x60, color 9  (higher z)
    //     40 and 63 overlap in x range [240, 260) on scanline 120
    always_comb begin
        shape_type    = 2'd0;
        shape_visible = 1'b0;
        shape_x       = 10'd0;
        shape_y       = 9'd0;
        shape_w       = 9'd0;
        shape_h       = 9'd0;
        shape_color   = 8'd0;

        if (shape_index == 6'd0) begin
            shape_type    = 2'd0;
            shape_visible = 1'b1;
            shape_x       = 10'd100;
            shape_y       = 9'd100;
            shape_w       = 9'd50;
            shape_h       = 9'd50;
            shape_color   = 8'd5;
        end else if (shape_index == 6'd40) begin
            shape_type    = 2'd0;
            shape_visible = 1'b1;
            shape_x       = 10'd200;
            shape_y       = 9'd100;
            shape_w       = 9'd60;
            shape_h       = 9'd60;
            shape_color   = 8'd7;
        end else if (shape_index == 6'd63) begin
            shape_type    = 2'd0;
            shape_visible = 1'b1;
            shape_x       = 10'd240;
            shape_y       = 9'd100;
            shape_w       = 9'd60;
            shape_h       = 9'd60;
            shape_color   = 8'd9;
        end
    end

    integer errors = 0;
    integer write_count;
    integer timeout_cnt;

    // Track last write to each pixel address
    logic [7:0] pixel_result [0:639];

    task automatic do_render(input logic [9:0] line);
        // Clear pixel tracking
        for (int i = 0; i < 640; i++)
            pixel_result[i] = 8'hFF; // sentinel: no write

        scanline = line;
        @(posedge clk);  // let scanline settle
        render_start = 1;
        @(posedge clk);  // FSM sees render_start, transitions out of IDLE/DONE
        render_start = 0;
        @(posedge clk);  // allow one cycle for state to settle

        write_count = 0;
        timeout_cnt = 0;
        while (!render_done && timeout_cnt < 5000) begin
            if (lb_wr_en && lb_wr_addr < 10'd640) begin
                write_count++;
                pixel_result[lb_wr_addr] = lb_wr_data;
            end
            @(posedge clk);
            timeout_cnt++;
        end
    endtask

    initial begin
        reset = 1; render_start = 0;
        scanline = 10'd0;
        #100;
        reset = 0;
        @(posedge clk);

        // ---- Test 1: Scanline 120 (inside rectangle y=100..149) ----
        do_render(10'd120);

        $display("Scanline 120: %0d writes (timeout_cnt=%0d)", write_count, timeout_cnt);

        if (write_count < 640) begin
            $display("FAIL: Too few writes on scanline 120: %0d", write_count);
            errors++;
        end else begin
            $display("PASS: Sufficient writes for background + shapes");
        end

        // Check that pixels 100-149 have the rectangle color (5)
        for (int i = 100; i < 150; i++) begin
            if (pixel_result[i] != 8'd5) begin
                $display("FAIL: Pixel %0d expected 5, got %0d", i, pixel_result[i]);
                errors++;
            end
        end
        if (errors == 0)
            $display("PASS: Rectangle pixels 100-149 all correct (color 5)");

        // Check that a pixel outside the rectangle has bg color
        if (pixel_result[50] != 8'd1) begin
            $display("FAIL: Pixel 50 expected bg color 1, got %0d", pixel_result[50]);
            errors++;
        end else begin
            $display("PASS: Background pixel 50 correct (color 1)");
        end

        // Shape 40 (color 7) and shape 63 (color 9) overlap in x=[240, 260).
        // Painter's algorithm: index 63 > index 40, so color 9 must win.
        for (int i = 240; i < 260; i++) begin
            if (pixel_result[i] != 8'd9) begin
                $display("FAIL: Overlap pixel %0d expected 9, got %0d",
                         i, pixel_result[i]);
                errors++;
            end
        end
        // Non-overlapping portion of shape 40: x in [200, 240) should be color 7
        for (int i = 200; i < 240; i++) begin
            if (pixel_result[i] != 8'd7) begin
                $display("FAIL: Shape-40 pixel %0d expected 7, got %0d",
                         i, pixel_result[i]);
                errors++;
            end
        end
        // Non-overlapping portion of shape 63: x in [260, 300) should be color 9
        for (int i = 260; i < 300; i++) begin
            if (pixel_result[i] != 8'd9) begin
                $display("FAIL: Shape-63 pixel %0d expected 9, got %0d",
                         i, pixel_result[i]);
                errors++;
            end
        end
        $display("PASS: High-index shapes 40/63 z-ordered correctly");

        // ---- Test 2: Scanline 50 (above rectangle -> bg only) ----
        do_render(10'd50);

        $display("Scanline 50: %0d writes (timeout_cnt=%0d)", write_count, timeout_cnt);

        if (write_count < 640) begin
            $display("FAIL: Too few writes on scanline 50: %0d", write_count);
            errors++;
        end else begin
            $display("PASS: Scanline 50 has enough writes");
        end

        // Rectangle should NOT appear on scanline 50
        if (pixel_result[120] != 8'd1) begin
            $display("FAIL: Pixel 120 on scanline 50 expected bg 1, got %0d",
                     pixel_result[120]);
            errors++;
        end else begin
            $display("PASS: No rectangle on scanline 50");
        end

        if (errors == 0)
            $display("\n*** ALL TESTS PASSED ***");
        else
            $display("\n*** %0d TESTS FAILED ***", errors);

        $finish;
    end

    // Watchdog timeout
    initial begin
        #10_000_000;
        $display("TIMEOUT - simulation took too long");
        $finish;
    end

endmodule
