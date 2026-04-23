/*
 * sprite_rom.sv — 32x32 palette-indexed sprite ROM (M10K)
 *
 * Role
 *   Returns one 8-bit palette index per address. Holds the 32x32
 *   peashooter sprite that the type-3 branch of shape_renderer uses.
 *
 * How it fits
 *   Instantiated inside pvz_top. shape_renderer's S_SHAPE_DRAW sprite
 *   branch drives addr and reads pixel one cycle later. See
 *   design-document §Sprite System (§3.4) and Figure 2 arrow 5.
 *
 * Background concepts
 *   - Inferred M10K. This is the Quartus-recognised pattern for a single-
 *     port ROM: an unpacked byte array plus a $readmemh initial block,
 *     read through a registered always_ff. Quartus maps it into one M10K
 *     block and pre-loads the contents from peas_idx.mem during
 *     configuration, so software does not need to initialise it at
 *     runtime.
 *   - 1-cycle read latency. The read is registered, so the byte for
 *     address A appears on pixel on the clock edge after A is driven.
 *     The renderer has to issue sprite addresses one cycle before it
 *     needs the data and keep a 1-entry pending queue to commit the
 *     returned byte. Missing this pipeline stage was a recurring bug
 *     during bring-up.
 *   - 0xFF as transparent sentinel. peas_idx.mem uses 0xFF wherever the
 *     sprite should not overwrite the background. Consumers have to
 *     suppress the line-buffer write when the returned pixel equals 0xFF;
 *     color_palette is never asked to render this value.
 *   - Address is row-major: addr = y*32 + x for 0 <= x,y < 32, giving
 *     1024 entries total.
 *
 * Key state
 *   rom[0..1023] : 8-bit palette index per sprite pixel, loaded from
 *                  peas_idx.mem at synthesis (or at $readmemh time in
 *                  simulation).
 *   pixel        : registered output, valid one clock after addr.
 */
module sprite_rom(
    input  logic        clk,
    input  logic [9:0]  addr,   // 0..1023 = y*32 + x
    output logic [7:0]  pixel
);

    logic [7:0] rom [0:1023];

    initial begin
        $readmemh("peas_idx.mem", rom);
    end

    always_ff @(posedge clk)
        pixel <= rom[addr];

endmodule
