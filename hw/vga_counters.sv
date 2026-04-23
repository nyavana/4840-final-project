/*
 * VGA 640x480@60Hz timing generator
 *
 * Lifted from lab3 vga_ball.sv (Stephen A. Edwards, Columbia University).
 * 50 MHz clock in, one pixel every other cycle (so 25 MHz pixel clock).
 *
 * hcount[10:1] = pixel column (0-639 active)
 * vcount[9:0]  = pixel row   (0-479 active)
 *
 * -------------------------------------------------------------------------
 * How it fits
 * -------------------------------------------------------------------------
 * The whole display pipeline hangs off this counter. See design-document
 * §VGA Rendering Pipeline (Hardware). linebuffer, shape_renderer, pvz_top's
 * scanline controller, and the external DAC all read hcount/vcount from
 * here. Numbers match Table 4 in the design document.
 *
 * -------------------------------------------------------------------------
 * Background a reader needs
 * -------------------------------------------------------------------------
 * A VGA frame is a fixed grid of "pixel clocks." For 640x480@60 Hz the
 * standard pixel clock is 25.175 MHz; we round to 25 MHz. Each line is
 * 800 pixel columns: 640 visible, then front porch, sync pulse, and back
 * porch (the "blanking" region, which is where the CRT gun retraced).
 * Each frame is 525 lines: 480 visible plus vertical blanking. HS and VS
 * are active-low pulses that fall during the sync window. BLANK_n is high
 * only inside the 640x480 visible rectangle; outside it the DAC has to
 * emit black.
 *
 * The 50 MHz board clock is twice the pixel clock, so this counter runs
 * at 50 MHz and treats every *pair* of cycles as one pixel. So HTOTAL is
 * 1600 (800 * 2) instead of 800, and VGA_CLK is just hcount[0]: it toggles
 * every input clock, giving a 25 MHz square wave with 50% duty.
 *
 * -------------------------------------------------------------------------
 * Key signals defined here
 * -------------------------------------------------------------------------
 *   hcount[10:0]  : 0..1599, wraps each line. hcount[10:1] is the column,
 *                   hcount[0] is the 25 MHz pixel-clock phase.
 *   vcount[9:0]   : 0..524, wraps each frame. vcount counts lines directly.
 *   endOfLine     : hcount == 1599 (last 50 MHz tick of the line).
 *   endOfField    : vcount == 524  (last line of the frame).
 *   VGA_HS/VGA_VS : active-low sync pulses.
 *   VGA_BLANK_n   : low outside the 640x480 active rectangle.
 *   VGA_SYNC_n    : tied low. We don't use composite-sync-on-green.
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

    // Horizontal timing in 50 MHz ticks (2 ticks = 1 pixel). Doubled vs.
    // Table 4's "Pixels/Lines" column, matching the "Clocks (50 MHz)"
    // column: 640 active pixels = 1280 ticks, and so on.
    parameter HACTIVE      = 11'd1280,
              HFRONT_PORCH = 11'd32,
              HSYNC        = 11'd192,
              HBACK_PORCH  = 11'd96,
              HTOTAL       = HACTIVE + HFRONT_PORCH + HSYNC + HBACK_PORCH; // 1600

    // Vertical timing counts lines, so no doubling.
    parameter VACTIVE      = 10'd480,
              VFRONT_PORCH = 10'd10,
              VSYNC        = 10'd2,
              VBACK_PORCH  = 10'd33,
              VTOTAL       = VACTIVE + VFRONT_PORCH + VSYNC + VBACK_PORCH; // 525

    // endOfLine fires on the last 50 MHz tick of the line (hcount == 1599),
    // the cycle before hcount wraps to 0. Downstream blocks tie line-level
    // events (linebuffer swap, render_start) to this wrap.
    logic endOfLine;
    assign endOfLine = hcount == HTOTAL - 1;

    // endOfField fires on the last line of the frame (vcount == 524), so
    // the next endOfLine wraps vcount back to 0 as well.
    logic endOfField;
    assign endOfField = vcount == VTOTAL - 1;

    // hcount: free-running 0..1599 counter at 50 MHz. Wraps on endOfLine.
    // Low bit is the pixel-clock phase; bits [10:1] are the column.
    always_ff @(posedge clk50 or posedge reset)
        if (reset)          hcount <= 0;
        else if (endOfLine) hcount <= 0;
        else                hcount <= hcount + 11'd1;

    // vcount: bumps once per finished line (on endOfLine), wraps to 0
    // after line 524 (endOfField). Never resets mid-line.
    always_ff @(posedge clk50 or posedge reset)
        if (reset)          vcount <= 0;
        else if (endOfLine)
            if (endOfField) vcount <= 0;
            else            vcount <= vcount + 10'd1;

    // --- Sync and blanking generation --------------------------------------
    // VGA sync pulses are active low. The encodings below pull upper bits
    // out of hcount/vcount to identify the sync window cheaply. Order per
    // line is active -> front porch -> sync pulse -> back porch -> (wrap).
    // Exact boundaries are in design-document Table 4.

    // Horizontal sync: active low during the sync region of each line.
    // hcount[10:8] == 3'b101 selects ticks 1280..1535, and we then mask
    // off ticks 1504..1535 where hcount[7:5] == 3'b111. Net pulse is
    // hcount in [1280, 1503]. Encoding is inherited from lab3. The width
    // is an approximation of the nominal 192-tick HSync in Table 4 and
    // monitors lock on it reliably.
    assign VGA_HS = !( (hcount[10:8] == 3'b101) &
                       !(hcount[7:5] == 3'b111) );

    // Vertical sync: active low for the 2-line sync pulse. vcount[9:1]
    // divides by two, and 9'd245 covers the two lines starting at
    // vcount = 490 (= VACTIVE + VFRONT_PORCH = 480 + 10).
    assign VGA_VS = !( vcount[9:1] == 9'd245 ); // (VACTIVE+VFRONT_PORCH)/2

    assign VGA_SYNC_n = 1'b0; // Unused. Composite sync on green is off.

    // Blanking: high in the visible area, low everywhere else, so the DAC
    // emits black during porches and sync regardless of RGB inputs. The
    // expression factors into "h is past column 639" AND "v is past line 479".
    assign VGA_BLANK_n = !( hcount[10] & (hcount[9] | hcount[8]) ) &
                         !( vcount[9] | (vcount[8:5] == 4'b1111) );

    // 25 MHz pixel clock from the 50 MHz input. hcount[0] toggles every
    // tick, so it's a clean 25 MHz square wave the DAC and downstream
    // logic can treat as the pixel clock.
    assign VGA_CLK = hcount[0];

endmodule
