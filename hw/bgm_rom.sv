/*
 * BGM PCM ROM — 16-bit signed samples.
 *
 * Depth is parameterized; production build uses 131072 (2^17, enough for
 * 15 s @ 8 kHz = 120000 samples plus some headroom). Testbenches instantiate
 * with a small depth and a fixture .mem file.
 *
 * Read latency: 1 clock (inferred M10K block RAM), matching sprite_rom.sv.
 */
module bgm_rom #(
    parameter int DEPTH    = 131072,
    parameter int AW       = 17,
    parameter     MEM_FILE = "bgm_rom.mem"
)(
    input  logic                clk,
    input  logic [AW-1:0]       addr,
    output logic signed [15:0]  sample
);
    logic [15:0] rom [0:DEPTH-1];

    initial begin
        $readmemh(MEM_FILE, rom);
    end

    always_ff @(posedge clk)
        sample <= $signed(rom[addr]);
endmodule
