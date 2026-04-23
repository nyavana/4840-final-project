/*
 * SFX PCM ROM — 16-bit signed samples, all 8 SFX clips packed back-to-back.
 * Depth parameterized; production build uses 32768 (2^15).
 *
 * Read latency: 1 clock (inferred M10K block RAM).
 */
module sfx_rom #(
    parameter int DEPTH    = 32768,
    parameter int AW       = 15,
    parameter     MEM_FILE = "sfx_rom.mem"
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
