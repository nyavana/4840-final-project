/*
 * Testbench for voice_sfx.
 * Uses tb/fixtures/sfx_offsets_test.svh (3 cues in the offset table) and
 * tb/fixtures/sfx_test.mem (12 samples packed as 3 × 4-sample clips).
 * pulse_tick is 1-cycle to match production pvz_audio.sv sample_tick.
 */
`timescale 1ns/1ps

module tb_voice_sfx;
    logic clk, rst, sample_tick, trig_valid;
    logic [3:0] cue_id;
    logic signed [15:0] sample;

    voice_sfx #(
        .DEPTH(16), .AW(4),
        .MEM_FILE("tb/fixtures/sfx_test.mem"),
        .OFFSETS_FILE("tb/fixtures/sfx_offsets_test.svh")
    ) dut(.*);

    initial clk = 0;
    always #5 clk = ~clk;

    integer errors = 0;

    task pulse_tick;
        sample_tick = 1;
        @(posedge clk);
        sample_tick = 0;
    endtask

    task trigger(input [3:0] id);
        cue_id = id;
        trig_valid = 1;
        @(posedge clk);
        trig_valid = 0;
        @(posedge clk);
    endtask

    task expect_sample(input signed [15:0] want, input string label);
        @(posedge clk);
        if (sample !== want) begin
            $display("FAIL (%s): got %0d, want %0d", label, sample, want);
            errors++;
        end else begin
            $display("PASS (%s): got %0d", label, sample);
        end
    endtask

    initial begin
        rst = 1; trig_valid = 0; cue_id = 0; sample_tick = 0;
        repeat (4) @(posedge clk);
        rst = 0;
        @(posedge clk);

        // Idle: sample is 0
        pulse_tick;
        expect_sample(16'sd0, "idle");

        // Trigger cue 1 (offsets 0..3 = ramp 0..3)
        trigger(4'd1);
        pulse_tick;
        expect_sample(16'sd0, "cue1 sample 0");
        pulse_tick;
        expect_sample(16'sd1, "cue1 sample 1");
        pulse_tick;
        expect_sample(16'sd2, "cue1 sample 2");
        pulse_tick;
        expect_sample(16'sd3, "cue1 sample 3");
        pulse_tick;
        expect_sample(16'sd0, "cue1 done -> idle");

        // Trigger cue 2 (offsets 4..7 = reverse ramp 3..0)
        trigger(4'd2);
        pulse_tick;
        expect_sample(16'sd3, "cue2 sample 0");
        pulse_tick;
        expect_sample(16'sd2, "cue2 sample 1");
        // Retrigger mid-playback: cue 3 (constant 10)
        trigger(4'd3);
        pulse_tick;
        expect_sample(16'sd10, "cue3 after retrigger");
        pulse_tick;
        expect_sample(16'sd10, "cue3 sample 1");

        // cue_id == 0 forces IDLE
        trigger(4'd0);
        pulse_tick;
        expect_sample(16'sd0, "cue 0 stops");

        // Out-of-range cue (5..8 have zero offsets; 9..15 reserved no-op)
        trigger(4'd9);
        pulse_tick;
        expect_sample(16'sd0, "cue 9 is no-op");

        if (errors == 0) $display("TEST PASSED: tb_voice_sfx");
        else             $display("TEST FAILED: tb_voice_sfx (%0d errors)", errors);
        $finish;
    end
endmodule
