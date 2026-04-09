/*
 * Dual 640x8-bit line buffer with hsync swap
 *
 * Two 640-byte line RAMs. While one is being read for VGA display,
 * the other is written by the shape renderer. Roles swap on hsync.
 *
 * Write port: wr_en, wr_addr[9:0], wr_data[7:0] -> draw buffer
 * Read port:  rd_addr[9:0] -> rd_data[7:0] from display buffer
 * Swap:       swap pulse (driven from hsync) toggles which is which
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

    // Two 640-entry x 8-bit line RAMs
    logic [7:0] buf0 [0:639];
    logic [7:0] buf1 [0:639];

    // Which buffer is currently the display buffer (0 or 1)
    logic sel;

    always_ff @(posedge clk or posedge reset)
        if (reset)
            sel <= 1'b0;
        else if (swap)
            sel <= ~sel;

    // Write to draw buffer (the one NOT being displayed)
    always_ff @(posedge clk)
        if (wr_en) begin
            if (sel == 1'b0)
                buf1[wr_addr] <= wr_data;  // sel=0: displaying buf0, draw to buf1
            else
                buf0[wr_addr] <= wr_data;  // sel=1: displaying buf1, draw to buf0
        end

    // Read from display buffer
    always_ff @(posedge clk)
        if (sel == 1'b0)
            rd_data <= buf0[rd_addr];
        else
            rd_data <= buf1[rd_addr];

endmodule
