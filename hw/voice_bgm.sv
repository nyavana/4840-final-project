/*
 * voice_bgm — address counter + simple FSM for the background-music voice.
 *
 * On every sample_tick pulse while play=1, walks addr through [0, BGM_LEN-1]
 * and wraps. Output is the ROM sample at the CURRENT addr (not the one we
 * just advanced to), which means addr must be issued one cycle before the
 * sample becomes available at the mixer — BRAM read latency is 1 cycle.
 *
 * When play=0 the addr is held at 0 so the next enable starts from sample 0.
 * Output is forced to 0 whenever play=0 so the mixer doesn't hear stale data.
 */
module voice_bgm #(
    parameter int DEPTH     = 131072,
    parameter int AW        = 17,
    parameter int BGM_LEN   = 120000,
    parameter     MEM_FILE  = "bgm_rom.mem"
)(
    input  logic                clk,
    input  logic                rst,
    input  logic                play,
    input  logic                sample_tick,
    output logic signed [15:0]  sample
);
    logic [AW-1:0]       addr;
    logic signed [15:0]  rom_sample;

    bgm_rom #(.DEPTH(DEPTH), .AW(AW), .MEM_FILE(MEM_FILE)) rom_i(
        .clk(clk), .addr(addr), .sample(rom_sample)
    );

    always_ff @(posedge clk) begin
        if (rst) begin
            addr <= '0;
        end else if (!play) begin
            addr <= '0;
        end else if (sample_tick) begin
            if (addr == BGM_LEN - 1) addr <= '0;
            else                     addr <= addr + 1'b1;
        end
    end

    assign sample = play ? rom_sample : 16'sd0;
endmodule
