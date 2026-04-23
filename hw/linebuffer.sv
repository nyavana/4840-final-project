/*
 * Dual 640x8-bit line buffer with hsync swap
 *
 * Two 640-byte line RAMs. One is read for VGA display while the other is
 * written by the shape renderer. Roles swap on hsync.
 *
 * Write port: wr_en, wr_addr[9:0], wr_data[7:0] -> draw buffer
 * Read port:  rd_addr[9:0] -> rd_data[7:0] from display buffer
 * Swap:       swap pulse (driven from hsync) toggles which is which
 *
 * -------------------------------------------------------------------------
 * How it fits
 * -------------------------------------------------------------------------
 * This is the hand-off between shape_renderer (write side) and the VGA DAC
 * path in pvz_top (read side). See design-document §VGA Rendering Pipeline
 * (Hardware) and the scanline-dataflow figure in §Scanline Rendering
 * Sequence for the surrounding pipeline.
 *
 * -------------------------------------------------------------------------
 * Why two banks?
 * -------------------------------------------------------------------------
 * A full 640x480x8bpp frame buffer would need 307 KB of on-chip RAM, past
 * the Cyclone V's M10K budget once sprite ROMs are added. The scanline
 * approach cuts that down to 2 * 640 = 1280 bytes: during scanline N the
 * DAC reads pixels out of bank A while the renderer paints line N+1 into
 * bank B, then the roles swap at the start of line N+1. The renderer then
 * gets roughly a whole line to produce 640 pixels (~1598 pixel-clock
 * cycles once you account for render_start latency).
 *
 * -------------------------------------------------------------------------
 * Key state defined here
 * -------------------------------------------------------------------------
 *   buf0, buf1 : the two 640-entry x 8-bit line RAMs. Quartus should
 *                infer these as M10K blocks.
 *   sel        : 1-bit flop picking which bank the DAC is currently
 *                reading. sel == 0 means display from buf0 and draw to
 *                buf1; sel == 1 swaps the roles. Toggles on every `swap`
 *                pulse.
 *   wr_addr/rd_addr : 10-bit pixel columns. Visible range is 0..639. The
 *                extra bit lets 640..1023 exist without wrap-around
 *                glitches, but the renderer never writes past 639 and the
 *                DAC never reads past 639.
 *
 * -------------------------------------------------------------------------
 * Timing notes
 * -------------------------------------------------------------------------
 * The read port is *registered*: rd_data lags rd_addr by one clock. The
 * consumer (color_palette / VGA output pipeline in pvz_top) has to
 * pipeline for that one-cycle latency. The write is a plain synchronous
 * write gated by wr_en.
 */

module linebuffer(
    input  logic        clk,
    input  logic        reset,

    // Write port (shape renderer -> draw buffer)
    input  logic        wr_en,
    input  logic [9:0]  wr_addr,
    input  logic [7:0]  wr_data,

    // Read port (VGA output <- display buffer)
    input  logic [9:0]  rd_addr,
    output logic [7:0]  rd_data,

    // Swap control
    input  logic        swap
);

    // Two 640-entry x 8-bit line RAMs. Contents are palette indices, not RGB.
    logic [7:0] buf0 [0:639];
    logic [7:0] buf1 [0:639];

    // Which buffer is currently on the display side (0 or 1). We invert it
    // with ~sel below to pick the draw buffer on each access.
    logic sel;

    // `swap` pulses on hcount == 0 at the start of each visible line (see
    // pvz_top's lb_swap generation). Reset pins sel to a known state so
    // the first frame after power-up is deterministic.
    always_ff @(posedge clk or posedge reset)
        if (reset)
            sel <= 1'b0;
        else if (swap)
            sel <= ~sel;

    // Write to the draw buffer (the bank NOT currently being displayed).
    // `if (sel == 1'b0)` is the same as addressing buf[~sel]; writing out
    // the two branches explicitly keeps Quartus's dual-port M10K inference
    // simple.
    always_ff @(posedge clk)
        if (wr_en) begin
            if (sel == 1'b0)
                buf1[wr_addr] <= wr_data;  // sel=0: displaying buf0, draw to buf1
            else
                buf0[wr_addr] <= wr_data;  // sel=1: displaying buf1, draw to buf0
        end

    // Read from the display buffer. Registered read: rd_data is valid on
    // the cycle AFTER rd_addr is presented. Callers have to pipeline by
    // one clock.
    always_ff @(posedge clk)
        if (sel == 1'b0)
            rd_data <= buf0[rd_addr];
        else
            rd_data <= buf1[rd_addr];

endmodule
