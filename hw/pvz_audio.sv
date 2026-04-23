/*
 * pvz_audio — second Avalon-slave peripheral for PCM audio playback.
 *
 * Register map (4 × 32-bit words, addressUnits WORDS):
 *   0x00 BGM_CTRL        W  bit [0]    1=play, 0=stop
 *   0x04 SFX_VOICE0_TRIG W  bits [3:0] cue_id; write pulses trig_valid on voice 0
 *   0x08 SFX_VOICE1_TRIG W  bits [3:0] cue_id; write pulses trig_valid on voice 1
 *   0x0C STATUS          R  bits [2:0] {sfx1_active, sfx0_active, bgm_active}
 *
 * Avalon-streaming out: 16-bit signed L+R (mono duplicated), with
 * left_chan_ready / right_chan_ready inputs from altera_up_avalon_audio.
 */
module pvz_audio(
    input  logic         clk,
    input  logic         reset,

    // Avalon-MM slave
    input  logic [2:0]   address,
    input  logic         chipselect,
    input  logic         write,
    input  logic         read,
    input  logic [31:0]  writedata,
    output logic [31:0]  readdata,

    // Avalon-ST sink side (from audio IP)
    input  logic         left_chan_ready,
    input  logic         right_chan_ready,
    output logic signed [15:0] sample_data_l,
    output logic signed [15:0] sample_data_r,
    output logic         sample_valid_l,
    output logic         sample_valid_r
);
    // -- audio_regs: latched control, trigger pulses --
    logic        bgm_play;
    logic        trig0_valid, trig1_valid;
    logic [3:0]  cue0_id,     cue1_id;
    logic        bgm_active, sfx0_active, sfx1_active;

    always_ff @(posedge clk) begin
        if (reset) begin
            bgm_play    <= 1'b0;
            trig0_valid <= 1'b0;
            trig1_valid <= 1'b0;
            cue0_id     <= 4'd0;
            cue1_id     <= 4'd0;
        end else begin
            // trigger valids are 1-cycle pulses
            trig0_valid <= 1'b0;
            trig1_valid <= 1'b0;

            if (chipselect && write) begin
                case (address)
                    3'd0: bgm_play <= writedata[0];
                    3'd1: begin cue0_id <= writedata[3:0]; trig0_valid <= 1'b1; end
                    3'd2: begin cue1_id <= writedata[3:0]; trig1_valid <= 1'b1; end
                    default: ;
                endcase
            end
        end
    end

    // Readback: STATUS at word 3
    always_comb begin
        readdata = 32'd0;
        if (chipselect && read && address == 3'd3) begin
            readdata = {29'd0, sfx1_active, sfx0_active, bgm_active};
        end
    end

    // -- sample_tick: rising edge of combined channel ready --
    logic ready, ready_d;
    assign ready = left_chan_ready & right_chan_ready;
    always_ff @(posedge clk) ready_d <= ready;
    logic sample_tick;
    assign sample_tick = ready & ~ready_d;

    // -- voices --
    logic signed [15:0] bgm_s, sfx0_s, sfx1_s, mixed_s;

    voice_bgm bgm_i(
        .clk(clk), .rst(reset), .play(bgm_play), .sample_tick(sample_tick),
        .sample(bgm_s)
    );

    voice_sfx sfx0_i(
        .clk(clk), .rst(reset), .sample_tick(sample_tick),
        .trig_valid(trig0_valid), .cue_id(cue0_id), .sample(sfx0_s)
    );

    voice_sfx sfx1_i(
        .clk(clk), .rst(reset), .sample_tick(sample_tick),
        .trig_valid(trig1_valid), .cue_id(cue1_id), .sample(sfx1_s)
    );

    mixer mix_i(.bgm(bgm_s), .sfx0(sfx0_s), .sfx1(sfx1_s), .mixed(mixed_s));

    // -- activity flags for STATUS --
    assign bgm_active  = bgm_play;
    assign sfx0_active = (sfx0_s != 16'sd0);
    assign sfx1_active = (sfx1_s != 16'sd0);

    // -- streaming out: mono duplicated, valid while ready --
    always_ff @(posedge clk) begin
        if (reset) begin
            sample_data_l  <= 16'sd0;
            sample_data_r  <= 16'sd0;
            sample_valid_l <= 1'b0;
            sample_valid_r <= 1'b0;
        end else begin
            sample_data_l  <= mixed_s;
            sample_data_r  <= mixed_s;
            sample_valid_l <= ready;
            sample_valid_r <= ready;
        end
    end
endmodule
