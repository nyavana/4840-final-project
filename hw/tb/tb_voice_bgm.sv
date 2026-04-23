/*
 * Testbench for voice_bgm.
 * Drives sample_tick pulses (1-cycle wide, matching production
 * pvz_audio's sample_tick = ready & ~ready_d) and verifies:
 *  - While play=1, addr walks 0..N-1 then wraps to 0.
 *  - While play=0, output is 0 and addr stays reset to 0.
 *  - play 0->1 restarts from addr 0 regardless of prior state.
 */
`timescale 1ns/1ps

module tb_voice_bgm;
    logic clk, rst, play, sample_tick;
    logic signed [15:0] sample;

    voice_bgm #(.DEPTH(16), .AW(4), .BGM_LEN(16), .MEM_FILE("tb/fixtures/bgm_test.mem")) dut(.*);

    initial clk = 0;
    always #5 clk = ~clk;

    integer errors = 0;

    task pulse_tick;
        sample_tick = 1;
        @(posedge clk);
        sample_tick = 0;
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
        rst = 1; play = 0; sample_tick = 0;
        repeat (4) @(posedge clk);
        rst = 0;
        @(posedge clk);

        pulse_tick;
        expect_sample(16'sd0, "play=0 tick emits 0");

        play = 1;
        @(posedge clk);
        for (int i = 0; i < 16; i++) begin
            pulse_tick;
            expect_sample(i[15:0], $sformatf("play tick %0d", i));
        end
        pulse_tick;
        expect_sample(16'sd0, "wrap to 0");

        play = 0;
        pulse_tick;
        expect_sample(16'sd0, "stop restores silence");

        play = 1;
        @(posedge clk);
        pulse_tick;
        expect_sample(16'sd0, "restart from 0");
        pulse_tick;
        expect_sample(16'sd1, "play tick 1 after restart");

        if (errors == 0) $display("TEST PASSED: tb_voice_bgm");
        else             $display("TEST FAILED: tb_voice_bgm (%0d errors)", errors);
        $finish;
    end
endmodule
