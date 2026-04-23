/*
 * tb_bg_grid.sv — unit testbench for bg_grid
 *
 * Role
 *   Drives bg_grid with a handful of CPU writes and coordinate queries
 *   and prints PASS/FAIL. Simulation-only; never synthesised.
 *
 * How it fits
 *   Exercises the shadow/active double-buffer path from the bg_grid
 *   header and design-document §System Architecture, plus the game-area
 *   clipping from §Display Layout. Runs under Verilator or ModelSim as
 *   part of the hardware testbench suite in §Testing and Verification.
 *
 * Background concepts
 *   - Standard SystemVerilog TB idioms. A free-running clock comes from
 *     `always #10 clk = ~clk;` (50 ns period with the 1ns/1ps timescale
 *     below; the frequency is cosmetic for a unit TB like this). The
 *     initial block holds reset high, drops it, then walks a
 *     stimulus-then-check sequence using @(posedge clk) to step a cycle
 *     at a time. $display writes to the simulator console and $finish
 *     ends the simulation.
 *   - `dut(.*);` port auto-connect. Every signal here shares a name with
 *     a port on bg_grid, so SystemVerilog wires them up by name.
 *   - errors accumulator. Every failed check bumps the counter; the
 *     end-of-run summary prints either the count or "ALL TESTS PASSED".
 *
 * Test phases
 *   1. Reset, idle the CPU write port.
 *   2. Write two cells in the shadow copy (row 0 col 0 = 5, row 1 col 3 = 10).
 *   3. Query before vsync_latch to confirm active is still zero (no tear).
 *   4. Pulse vsync_latch, re-query the same two cells, expect the new
 *      colors.
 *   5. Query pixels above and below the game area to confirm both
 *      clipping branches (py < 60 and py >= 420) return 0.
 */

`timescale 1ns/1ps

module tb_bg_grid;

    logic       clk, reset;
    logic       wr_en;
    logic [2:0] wr_col;
    logic [1:0] wr_row;
    logic [7:0] wr_color;
    logic       vsync_latch;
    logic [9:0] px, py;
    logic [7:0] color_out;

    bg_grid dut(.*);

    initial clk = 0;
    always #10 clk = ~clk;

    integer errors = 0;

    initial begin
        reset = 1; wr_en = 0; vsync_latch = 0;
        wr_col = 0; wr_row = 0; wr_color = 0;
        px = 0; py = 0;
        #100;
        reset = 0;
        @(posedge clk);

        // Phase 2: two shadow writes. Only shadow[] gets updated here;
        // active[] is still all zeros because no vsync_latch has fired.
        // Write color 5 to cell (row=0, col=0)
        wr_en = 1; wr_row = 2'd0; wr_col = 3'd0; wr_color = 8'd5;
        @(posedge clk);
        // Write color 10 to cell (row=1, col=3)
        wr_row = 2'd1; wr_col = 3'd3; wr_color = 8'd10;
        @(posedge clk);
        wr_en = 0;
        @(posedge clk);

        // Phase 3: pre-latch read. active[] is the render-visible copy
        // and must still show 0 at the cell we just wrote, to prove the
        // CPU write did not leak through mid-frame.
        // Before vsync latch, active should still be 0
        px = 10'd40; py = 10'd100; // row=0, col=0 -> cell (0,0)
        @(posedge clk);
        if (color_out != 8'd0) begin
            $display("FAIL: Before latch, expected 0, got %d", color_out);
            errors++;
        end else begin
            $display("PASS: Before latch, active is 0");
        end

        // Phase 4: pulse vsync_latch for one cycle so active[] picks up
        // the shadow writes, then re-query.
        // Latch
        vsync_latch = 1;
        @(posedge clk);
        vsync_latch = 0;
        @(posedge clk);

        // After latch, check cell (0,0) at pixel (40, 100)
        px = 10'd40; py = 10'd100;
        @(posedge clk);
        if (color_out != 8'd5) begin
            $display("FAIL: Cell (0,0) expected 5, got %d", color_out);
            errors++;
        end else begin
            $display("PASS: Cell (0,0) color correct");
        end

        // Check cell (1,3) at pixel (280, 160) -> row=(160-60)/90=1, col=280/80=3
        px = 10'd280; py = 10'd160;
        @(posedge clk);
        if (color_out != 8'd10) begin
            $display("FAIL: Cell (1,3) expected 10, got %d", color_out);
            errors++;
        end else begin
            $display("PASS: Cell (1,3) color correct");
        end

        // Phase 5: clipping checks. Both py < 60 (above the game area)
        // and py >= 420 (below it) must return 0 regardless of what
        // active[] holds.
        // Check outside game area (y < 60)
        px = 10'd100; py = 10'd30;
        @(posedge clk);
        if (color_out != 8'd0) begin
            $display("FAIL: Outside game area, expected 0, got %d", color_out);
            errors++;
        end else begin
            $display("PASS: Outside game area returns 0");
        end

        // Check outside game area (y >= 420)
        px = 10'd100; py = 10'd430;
        @(posedge clk);
        if (color_out != 8'd0) begin
            $display("FAIL: Below game area, expected 0, got %d", color_out);
            errors++;
        end else begin
            $display("PASS: Below game area returns 0");
        end

        if (errors == 0)
            $display("\n*** ALL TESTS PASSED ***");
        else
            $display("\n*** %0d TESTS FAILED ***", errors);

        $finish;
    end

endmodule
