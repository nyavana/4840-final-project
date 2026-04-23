/*
 * voice_sfx — one SFX voice. Two states: IDLE, PLAYING.
 *
 * On trig_valid with cue_id in 1..8 → load SFX_START[cue_id], SFX_END[cue_id],
 * go PLAYING. On trig_valid with cue_id == 0 → force IDLE. On trig_valid with
 * cue_id in 9..15 → no-op (reserved).
 *
 * Per-sample_tick in PLAYING: emit rom sample, advance addr; when addr reaches
 * stop_addr, set done flag and clear playing on the next tick so the last
 * sample is still visible for one cycle. IDLE output is 0.
 */
module voice_sfx #(
    parameter int DEPTH        = 32768,
    parameter int AW           = 15,
    parameter     MEM_FILE     = "sfx_rom.mem",
    parameter     OFFSETS_FILE = "sfx_offsets.svh"
)(
    input  logic                clk,
    input  logic                rst,
    input  logic                sample_tick,
    input  logic                trig_valid,
    input  logic [3:0]          cue_id,
    output logic signed [15:0]  sample
);
`ifdef TB_VOICE_SFX
    `include "tb/fixtures/sfx_offsets_test.svh"
`else
    `include "sfx_offsets.svh"    // defines SFX_START[1:8], SFX_END[1:8]
`endif

    logic [AW-1:0] addr, stop_addr;
    logic          playing, done;
    logic signed [15:0] rom_sample;

    sfx_rom #(.DEPTH(DEPTH), .AW(AW), .MEM_FILE(MEM_FILE)) rom_i(
        .clk(clk), .addr(addr), .sample(rom_sample)
    );

    always_ff @(posedge clk) begin
        if (rst) begin
            playing   <= 1'b0;
            done      <= 1'b0;
            addr      <= '0;
            stop_addr <= '0;
        end else if (trig_valid) begin
            if (cue_id == 4'd0) begin
                playing <= 1'b0;
                done    <= 1'b0;
            end else if (cue_id >= 4'd1 && cue_id <= 4'd8) begin
                addr      <= SFX_START[cue_id][AW-1:0];
                stop_addr <= SFX_END  [cue_id][AW-1:0];
                playing   <= 1'b1;
                done      <= 1'b0;
            end
            // cue_id in 9..15 → reserved, no-op (FSM unchanged)
        end else if (done) begin
            // Clear playing one cycle after the last sample is read
            playing <= 1'b0;
            done    <= 1'b0;
        end else if (playing && sample_tick) begin
            if (addr == stop_addr) begin
                // Last sample: stay on addr so ROM latches it; signal done
                done <= 1'b1;
            end else begin
                addr <= addr + 1'b1;
            end
        end
    end

    assign sample = playing ? rom_sample : 16'sd0;
endmodule
