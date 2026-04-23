/*
 * Testbench for mixer — 3-input signed 16-bit adder with saturation clamp.
 */
`timescale 1ns/1ps

module tb_mixer;
    logic signed [15:0] bgm, sfx0, sfx1;
    logic signed [15:0] mixed;

    mixer dut(.*);

    integer errors = 0;

    task check(input signed [15:0] a, b, c, expected);
        bgm = a; sfx0 = b; sfx1 = c;
        #1;
        if (mixed !== expected) begin
            $display("FAIL: mix(%0d,%0d,%0d) = %0d, expected %0d", a, b, c, mixed, expected);
            errors++;
        end else begin
            $display("PASS: mix(%0d,%0d,%0d) = %0d", a, b, c, mixed);
        end
    endtask

    initial begin
        check(16'sd0,      16'sd0,      16'sd0,      16'sd0);           // all zero
        check(16'sd100,    16'sd200,    16'sd300,    16'sd600);         // small positive
        check(-16'sd500,   -16'sd500,   -16'sd500,   -16'sd1500);       // small negative
        check(16'sd30000,  16'sd20000,  16'sd0,      16'sd32767);       // positive clip
        check(-16'sd30000, -16'sd20000, 16'sd0,      -16'sd32768);      // negative clip
        check(16'sd32767,  16'sd32767,  16'sd32767,  16'sd32767);       // max clip
        check(-16'sd32768, -16'sd32768, -16'sd32768, -16'sd32768);      // min clip
        check(16'sd32767,  -16'sd100,   -16'sd100,   16'sd32567);       // near-max cancel

        if (errors == 0) $display("TEST PASSED: tb_mixer");
        else             $display("TEST FAILED: tb_mixer (%0d errors)", errors);
        $finish;
    end
endmodule
