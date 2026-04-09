/*
 * VGA 640x480@60Hz timing generator
 *
 * Extracted from lab3 vga_ball.sv (Stephen A. Edwards, Columbia University).
 * Uses a 50 MHz input clock; one pixel every other cycle (25 MHz pixel clock).
 *
 * hcount[10:1] = pixel column (0-639 active)
 * vcount[9:0]  = pixel row   (0-479 active)
 */

module vga_counters(
    input  logic        clk50,
    input  logic        reset,
    output logic [10:0] hcount,
    output logic [9:0]  vcount,
    output logic        VGA_CLK,
    output logic        VGA_HS,
    output logic        VGA_VS,
    output logic        VGA_BLANK_n,
    output logic        VGA_SYNC_n
);

    parameter HACTIVE      = 11'd1280,
              HFRONT_PORCH = 11'd32,
              HSYNC        = 11'd192,
              HBACK_PORCH  = 11'd96,
              HTOTAL       = HACTIVE + HFRONT_PORCH + HSYNC + HBACK_PORCH; // 1600

    parameter VACTIVE      = 10'd480,
              VFRONT_PORCH = 10'd10,
              VSYNC        = 10'd2,
              VBACK_PORCH  = 10'd33,
              VTOTAL       = VACTIVE + VFRONT_PORCH + VSYNC + VBACK_PORCH; // 525

    logic endOfLine;
    assign endOfLine = hcount == HTOTAL - 1;

    logic endOfField;
    assign endOfField = vcount == VTOTAL - 1;

    always_ff @(posedge clk50 or posedge reset)
        if (reset)          hcount <= 0;
        else if (endOfLine) hcount <= 0;
        else                hcount <= hcount + 11'd1;

    always_ff @(posedge clk50 or posedge reset)
        if (reset)          vcount <= 0;
        else if (endOfLine)
            if (endOfField) vcount <= 0;
            else            vcount <= vcount + 10'd1;

    // Horizontal sync: active low during sync pulse
    assign VGA_HS = !( (hcount[10:8] == 3'b101) &
                       !(hcount[7:5] == 3'b111) );

    // Vertical sync: active low during sync pulse
    assign VGA_VS = !( vcount[9:1] == (VACTIVE + VFRONT_PORCH) / 2 );

    assign VGA_SYNC_n = 1'b0; // Unused — composite sync on green

    // Blanking: active high when in visible area
    assign VGA_BLANK_n = !( hcount[10] & (hcount[9] | hcount[8]) ) &
                         !( vcount[9] | (vcount[8:5] == 4'b1111) );

    // 25 MHz pixel clock from 50 MHz input
    assign VGA_CLK = hcount[0];

endmodule
