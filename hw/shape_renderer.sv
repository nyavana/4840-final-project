/*
 * Scanline-based shape renderer
 *
 * For each scanline, walks all 48 shape-table entries and writes pixels
 * to the linebuffer draw port. Later shapes overwrite earlier ones
 * (painter's algorithm, z-order by index).
 *
 * Four shape types:
 *   0 = filled rectangle
 *   1 = filled circle (bounding-box + distance check)
 *   2 = 7-segment digit (digit value in w[3:0])
 *   3 = sprite (32x32 ROM, rendered at 2x scale -> 64x64 on screen;
 *               pixel value 0xFF is transparent)
 *
 * Operation:
 *   1. On render_start pulse, begin background fill from bg_grid
 *   2. After background, iterate shapes 0-47
 *   3. Assert render_done when complete
 *
 * The renderer has to finish within ~1600 pixel clocks per scanline.
 *
 * -------------------------------------------------------------------------
 * How it fits
 * -------------------------------------------------------------------------
 * This is the main block in the display pipeline. pvz_top pulses
 * `render_start` once per visible scanline (at hcount = 2, see
 * design-document §Scanline Rendering Sequence). This FSM then has the
 * rest of the line, roughly 1598 pixel-clock cycles, to paint 640 pixels
 * into whichever bank of `linebuffer` is currently the draw buffer. The
 * `lb_swap` pulse at hcount = 0 hands that bank to the DAC on the next
 * line. See also design-document §Renderer FSM and the figure in
 * §Peripheral Internal Structure (arrow 5 is the sprite ROM read this
 * block issues).
 *
 * -------------------------------------------------------------------------
 * Background a reader needs
 * -------------------------------------------------------------------------
 *   Scanline renderer : we never materialise a full 640x480 frame buffer.
 *                       Each visible line is recomputed from scratch into
 *                       the linebuffer draw bank while the companion bank
 *                       is streamed to the DAC.
 *   Painter's algorithm : shapes are drawn in shape-table index order,
 *                       with no depth buffer. Later writes clobber earlier
 *                       ones at the same pixel, so z-order is exactly
 *                       "higher index = in front." Software depends on
 *                       this when it allocates index ranges (background
 *                       grid, entities, projectiles, HUD).
 *   Transparent sentinel: sprite-ROM bytes equal to 0xFF mean "don't write
 *                       this pixel." The sentinel is per-pixel, not per-
 *                       sprite, so irregular silhouettes compose over
 *                       whatever's already in the line buffer. See
 *                       design-document §Sprite System.
 *   Shadow/active doubling : shape_table and bg_grid are latched at vsync
 *                       by pvz_top, so the values we see here stay stable
 *                       across the whole frame even if software is still
 *                       writing the shadow copies.
 *
 * -------------------------------------------------------------------------
 * Key state defined here
 * -------------------------------------------------------------------------
 *   state_t / state       : six-state FSM. Per-state comments below cover
 *                           entry/exit conditions.
 *   bg_x                  : column counter for the background-fill phase.
 *   cur_shape, shape_index: which of the 48 shape-table entries we're on.
 *                           shape_index drives the read port.
 *   s_type, s_visible,
 *   s_x, s_y, s_w, s_h,
 *   s_color               : local snapshot of the current shape's row,
 *                           grabbed in S_SHAPE_SETUP so the rest of the
 *                           processing doesn't stall on the shape-table
 *                           read port.
 *   draw_x                : column cursor walking across the shape's span
 *                           in S_SHAPE_DRAW.
 *   sprite_pending,
 *   sprite_pending_addr   : one-entry queue that soaks up sprite_rom's
 *                           1-cycle read latency. Issue address now,
 *                           commit pixel next cycle.
 */

module shape_renderer(
    input  logic        clk,
    input  logic        reset,

    // Control
    input  logic        render_start,   // pulse: begin rendering this scanline
    input  logic [9:0]  scanline,       // current scanline number (0-479)
    output logic        render_done,    // asserted when rendering is complete

    // Background grid query
    output logic [9:0]  bg_px,
    output logic [9:0]  bg_py,
    input  logic [7:0]  bg_color,

    // Shape table read port
    output logic [5:0]  shape_index,
    input  logic [1:0]  shape_type,
    input  logic        shape_visible,
    input  logic [9:0]  shape_x,
    input  logic [8:0]  shape_y,
    input  logic [8:0]  shape_w,
    input  logic [8:0]  shape_h,
    input  logic [7:0]  shape_color,

    // Sprite ROM read port (combinational addr, 1-cycle registered pixel)
    output logic [9:0]  sprite_rd_addr,
    input  logic [7:0]  sprite_rd_pixel,

    // Linebuffer write port
    output logic        lb_wr_en,
    output logic [9:0]  lb_wr_addr,
    output logic [7:0]  lb_wr_data
);

    // FSM states. Per-state semantics:
    //   S_IDLE        - Waiting for render_start. No writes.
    //   S_BG_FILL     - Streaming bg_grid colors into columns 0..639 of
    //                   the draw buffer, one per cycle.
    //   S_SHAPE_SETUP - shape_index was driven last cycle, so the table's
    //                   outputs are valid now. Latch them into s_*.
    //   S_SHAPE_CHECK - Reject invisible shapes and shapes whose vertical
    //                   span doesn't cover `scanline`. Otherwise fall into
    //                   S_SHAPE_DRAW.
    //   S_SHAPE_DRAW  - Walk draw_x across [s_x, s_x + s_w) and emit
    //                   pixels per s_type.
    //   S_DONE        - Draw buffer is complete for this line. Idle until
    //                   the next render_start.
    typedef enum logic [2:0] {
        S_IDLE,
        S_BG_FILL,
        S_SHAPE_SETUP,
        S_SHAPE_CHECK,
        S_SHAPE_DRAW,
        S_DONE
    } state_t;

    state_t state;

    // Background fill counter: the column being written during S_BG_FILL.
    logic [9:0] bg_x;

    // Shape iteration cursors.
    //   cur_shape - shape index we're currently on (0..47). Bumped at the
    //               end of each shape.
    //   draw_x    - column cursor inside S_SHAPE_DRAW.
    logic [5:0] cur_shape;
    logic [9:0] draw_x;

    // Local snapshot of the current shape's fields. Grabbed in
    // S_SHAPE_SETUP so later states don't have to re-read the shape_table
    // port.
    logic [1:0]  s_type;
    logic        s_visible;
    logic [9:0]  s_x;
    logic [8:0]  s_y;
    logic [8:0]  s_w;
    logic [8:0]  s_h;
    logic [7:0]  s_color;

    // Sprite pipeline. Soaks up sprite_rom's 1-cycle registered output.
    //   sprite_pending       - a read was issued last cycle; this cycle
    //                          sprite_rd_pixel has the returned byte.
    //   sprite_pending_addr  - line-buffer column that byte belongs to.
    logic        sprite_pending;
    logic [9:0]  sprite_pending_addr;

    // Combinational sprite ROM address. 2x downscale from screen to ROM
    // (32x32). Sprite's top-left is (s_x, s_y). Local coords inside the
    // on-screen footprint are (draw_x - s_x, scanline - s_y). Bits [5:1]
    // of each is a /2, so every on-screen 2x2 block reads the same source
    // pixel and a 32x32 ROM entry renders as 64x64 on screen. The
    // concatenation packs row in the high bits and column in the low bits
    // of the 10-bit ROM address (row-major, matching peas_idx.mem per
    // design-document §Sprite System).
    /* verilator lint_off UNUSED */
    wire [9:0] sprite_local_x = draw_x - s_x;
    wire [9:0] sprite_local_y = scanline - {1'b0, s_y};
    /* verilator lint_on UNUSED */
    assign sprite_rd_addr = {sprite_local_y[5:1], sprite_local_x[5:1]};

    // (Circle and 7-seg helpers are computed inline in S_SHAPE_DRAW)

    // 7-segment decode table: digit 0..9 -> 7-bit mask.
    // Bit order is {g,f,e,d,c,b,a}, so segs[0] = a, segs[6] = g. Same
    // labelling as an old calculator. The default case returns all-off,
    // so an out-of-range digit code renders blank.
    function logic [6:0] decode_7seg(input logic [3:0] val);
        case (val)
            4'd0: decode_7seg = 7'b0111111; // a,b,c,d,e,f
            4'd1: decode_7seg = 7'b0000110; // b,c
            4'd2: decode_7seg = 7'b1011011; // a,b,d,e,g
            4'd3: decode_7seg = 7'b1001111; // a,b,c,d,g
            4'd4: decode_7seg = 7'b1100110; // b,c,f,g
            4'd5: decode_7seg = 7'b1101101; // a,c,d,f,g
            4'd6: decode_7seg = 7'b1111101; // a,c,d,e,f,g
            4'd7: decode_7seg = 7'b0000111; // a,b,c
            4'd8: decode_7seg = 7'b1111111; // all
            4'd9: decode_7seg = 7'b1101111; // a,b,c,d,f,g
            default: decode_7seg = 7'b0000000;
        endcase
    endfunction

    // Check if a pixel is in a 7-segment digit.
    // Digit bounding box: 20 wide x 30 tall (local coords).
    // Segment thickness: 3 pixels.
    // Each branch asks whether (lx, ly) lies in one segment's rectangle
    // AND whether that segment is lit in `segs`. Segment rectangles
    // overlap at the corners; the loose `<= 2`-style tests keep the
    // corner pixels so segments connect visually.
    function logic check_7seg(
        input logic [6:0] segs,
        input logic [9:0] lx,  // local x within digit (0-19)
        input logic [9:0] ly   // local y within digit (0-29)
    );
        logic hit;
        hit = 1'b0;
        // Segment a: top horizontal (x:3-16, y:0-2)
        if (segs[0] && lx >= 3 && lx <= 16 && ly <= 2) hit = 1'b1;
        // Segment b: top-right vertical (x:17-19, y:3-13)
        if (segs[1] && lx >= 17 && lx <= 19 && ly >= 3 && ly <= 13) hit = 1'b1;
        // Segment c: bottom-right vertical (x:17-19, y:16-26)
        if (segs[2] && lx >= 17 && lx <= 19 && ly >= 16 && ly <= 26) hit = 1'b1;
        // Segment d: bottom horizontal (x:3-16, y:27-29)
        if (segs[3] && lx >= 3 && lx <= 16 && ly >= 27 && ly <= 29) hit = 1'b1;
        // Segment e: bottom-left vertical (x:0-2, y:16-26)
        if (segs[4] && lx <= 2 && ly >= 16 && ly <= 26) hit = 1'b1;
        // Segment f: top-left vertical (x:0-2, y:3-13)
        if (segs[5] && lx <= 2 && ly >= 3 && ly <= 13) hit = 1'b1;
        // Segment g: middle horizontal (x:3-16, y:13-15)
        if (segs[6] && lx >= 3 && lx <= 16 && ly >= 13 && ly <= 15) hit = 1'b1;
        check_7seg = hit;
    endfunction

    // bg_py is just an alias for scanline; every bg_grid read is for the
    // current line. render_done is a level signal held high in S_DONE, so
    // pvz_top can check completion any time before the next render_start.
    assign bg_py = scanline;
    assign render_done = (state == S_DONE);

    always_ff @(posedge clk or posedge reset) begin
        if (reset) begin
            state      <= S_IDLE;
            lb_wr_en   <= 1'b0;
            lb_wr_addr <= 10'd0;
            lb_wr_data <= 8'd0;
            bg_x       <= 10'd0;
            cur_shape  <= 6'd0;
            draw_x     <= 10'd0;
            shape_index <= 6'd0;
            sprite_pending      <= 1'b0;
            sprite_pending_addr <= 10'd0;
        end else begin
            // Write port defaults to off. States raise it for the single
            // cycle they want to emit a pixel.
            lb_wr_en <= 1'b0; // default: no write

            case (state)
                // S_IDLE: park here until the top level pulses render_start
                // for this scanline. Kick the line off with bg_x = 0 and
                // jump to S_BG_FILL.
                S_IDLE: begin
                    if (render_start) begin
                        bg_x  <= 10'd0;
                        state <= S_BG_FILL;
                    end
                end

                // S_BG_FILL: stream 640 background pixels into the draw
                // buffer. Each cycle presents bg_px to bg_grid (combinational
                // lookup), writes the returned bg_color at column bg_x, and
                // bumps bg_x. After column 639 we move on to shapes.
                S_BG_FILL: begin
                    bg_px      <= bg_x;
                    lb_wr_en   <= 1'b1;
                    lb_wr_addr <= bg_x;
                    lb_wr_data <= bg_color;
                    if (bg_x == 10'd639) begin
                        cur_shape   <= 6'd0;
                        shape_index <= 6'd0;
                        state       <= S_SHAPE_SETUP;
                    end else begin
                        bg_x <= bg_x + 10'd1;
                    end
                end

                // S_SHAPE_SETUP: shape_index was driven on the previous
                // cycle (either from S_BG_FILL or from the advance at the
                // end of the last shape), so the shape_table read port
                // now has valid outputs. Latch them into s_* so the rest
                // of the states can work off stable values.
                S_SHAPE_SETUP: begin
                    // shape_index is already set; data available next cycle
                    s_type    <= shape_type;
                    s_visible <= shape_visible;
                    s_x       <= shape_x;
                    s_y       <= shape_y;
                    s_w       <= shape_w;
                    s_h       <= shape_h;
                    s_color   <= shape_color;
                    state     <= S_SHAPE_CHECK;
                end

                // S_SHAPE_CHECK: drop shapes whose visible bit is clear,
                // or whose vertical span [s_y, s_y + s_h) doesn't cover
                // the current scanline. Otherwise seed draw_x at the
                // shape's left edge and go to the draw phase.
                S_SHAPE_CHECK: begin
                    if (!s_visible ||
                        scanline < {1'b0, s_y} ||
                        scanline >= ({1'b0, s_y} + {1'b0, s_h})) begin
                        // Skip this shape. Bump cur_shape and loop back to
                        // S_SHAPE_SETUP (or go to S_DONE after entry 47).
                        if (cur_shape == 6'd47) begin
                            state <= S_DONE;
                        end else begin
                            cur_shape   <= cur_shape + 6'd1;
                            shape_index <= cur_shape + 6'd1;
                            state       <= S_SHAPE_SETUP;
                        end
                    end else begin
                        // Shape overlaps scanline. Start drawing at its
                        // left edge. Clear sprite_pending so the sprite
                        // pipeline below starts from a known empty state.
                        draw_x         <= s_x;
                        sprite_pending <= 1'b0;
                        state          <= S_SHAPE_DRAW;
                    end
                end

                // S_SHAPE_DRAW: walk draw_x across the shape's horizontal
                // span, one pixel per cycle. Behaviour splits between the
                // sprite path (type 3) and the geometric types (0/1/2).
                // Every non-skip shape writes the line buffer with
                // lb_wr_addr = draw_x (or the pending sprite address), so
                // later shapes naturally overwrite earlier ones at the
                // same pixel. That's the painter's-algorithm z-ordering
                // noted above.
                S_SHAPE_DRAW: begin
                    if (s_type == 2'd3) begin
                        // -----------------------------------------------
                        // Sprite path
                        // -----------------------------------------------
                        // sprite_rom has a 1-cycle registered read. On
                        // this cycle we (a) commit the byte requested
                        // last cycle if any, and (b) issue a fresh
                        // request for the next column. draw_x bumps with
                        // each issue, so after N issues draw_x == s_x + N
                        // and there's still one pending pixel to flush
                        // next cycle.
                        if (sprite_pending && sprite_rd_pixel != 8'hFF) begin
                            // 0xFF is the sprite-ROM transparent sentinel
                            // (design-document §Sprite System). Suppress
                            // the line-buffer write so background shows through.
                            lb_wr_en   <= 1'b1;
                            lb_wr_addr <= sprite_pending_addr;
                            lb_wr_data <= sprite_rd_pixel;
                        end

                        // Default: clear pending (overridden below if issuing)
                        sprite_pending <= 1'b0;

                        if (draw_x < s_x + {1'b0, s_w} && draw_x < 10'd640) begin
                            // Still inside the sprite's span and on-screen:
                            // issue the next fetch. sprite_rd_addr is
                            // driven combinationally from draw_x/s_x/s_y,
                            // so the ROM sees the address this cycle and
                            // returns the pixel next cycle.
                            sprite_pending      <= 1'b1;
                            sprite_pending_addr <= draw_x;
                            draw_x              <= draw_x + 10'd1;
                        end else if (!sprite_pending) begin
                            // Every column has been issued and flushed,
                            // no pending reads left. Move to the next
                            // shape or finish the line.
                            if (cur_shape == 6'd47) begin
                                state <= S_DONE;
                            end else begin
                                cur_shape   <= cur_shape + 6'd1;
                                shape_index <= cur_shape + 6'd1;
                                state       <= S_SHAPE_SETUP;
                            end
                        end
                        // else: the last issued pixel is being committed
                        // this very cycle (sprite_pending was still set).
                        // Hold in S_SHAPE_DRAW one more cycle so the
                        // pending commit above can happen. Next cycle
                        // sprite_pending is 0 and we advance.
                    end else if (draw_x >= s_x + {1'b0, s_w} || draw_x >= 10'd640) begin
                        // Geometric-shape exit. draw_x walked past the
                        // shape's right edge (or off-screen). Move to the
                        // next shape, or finish the line if this was the last.
                        if (cur_shape == 6'd47) begin
                            state <= S_DONE;
                        end else begin
                            cur_shape   <= cur_shape + 6'd1;
                            shape_index <= cur_shape + 6'd1;
                            state       <= S_SHAPE_SETUP;
                        end
                    end else if (draw_x < 10'd640) begin
                        // -----------------------------------------------
                        // Geometric-shape pixel. Compute a hit predicate
                        // for (draw_x, scanline) against the current
                        // shape, then write s_color into the line buffer
                        // on a hit.
                        // -----------------------------------------------
                        logic pixel_hit;
                        logic [9:0] local_x, local_y_full;
                        logic signed [10:0] ddx, ddy;
                        logic [21:0] dist2;
                        logic [21:0] r2;

                        // Local coords inside the shape's bounding box.
                        local_x = draw_x - s_x;
                        local_y_full = scanline - {1'b0, s_y};

                        case (s_type)
                            2'd0: begin // Rectangle: always hit within bounds
                                // The S_SHAPE_CHECK vertical test plus
                                // the draw_x <= s_x + s_w horizontal test
                                // above already fence the bounding box,
                                // so every pixel reached here is inside.
                                pixel_hit = 1'b1;
                            end
                            2'd1: begin // Circle: distance check
                                // Standard (x-cx)^2 + (y-cy)^2 <= r^2
                                // test, with the circle inscribed in the
                                // shape's bounding box. Center at
                                // (s_x + s_w/2, s_y + s_h/2); radius is
                                // s_w/2. s_w[8:1] is a cheap /2 that
                                // rounds down. Both multiplies produce
                                // 22-bit results. Worst case 640^2 =
                                // 409,600 fits in 19 bits; the extra
                                // headroom covers the signed squaring.
                                ddx = $signed({1'b0, draw_x}) -
                                      $signed({1'b0, s_x} + {2'b0, s_w[8:1]});
                                ddy = $signed({1'b0, scanline}) -
                                      $signed({2'b0, s_y} + {2'b0, s_h[8:1]});
                                dist2 = ddx * ddx + ddy * ddy;
                                r2 = {13'd0, s_w[8:1]} * {13'd0, s_w[8:1]};
                                pixel_hit = (dist2 <= r2);
                            end
                            2'd2: begin // 7-segment digit
                                // Digit value is packed into the low 4
                                // bits of the width field (see design-
                                // document §Shape Types / Shape Table
                                // Entry Format). decode_7seg maps 0..9
                                // to a 7-segment mask; check_7seg tests
                                // whether (local_x, local_y) lies inside
                                // any lit segment of the 20x30 glyph grid.
                                pixel_hit = check_7seg(
                                    decode_7seg(s_w[3:0]),
                                    local_x,
                                    local_y_full
                                );
                            end
                            default: pixel_hit = 1'b0;
                        endcase

                        if (pixel_hit) begin
                            lb_wr_en   <= 1'b1;
                            lb_wr_addr <= draw_x;
                            lb_wr_data <= s_color;
                        end

                        draw_x <= draw_x + 10'd1;
                    end
                end

                // S_DONE: the draw buffer has a full scanline.
                // render_done is high (from the continuous assign above).
                // Wait here for the next render_start. pvz_top will pulse
                // it again at hcount = 2 of the next line.
                S_DONE: begin
                    // Stay in done until next render_start
                    if (render_start) begin
                        bg_x  <= 10'd0;
                        state <= S_BG_FILL;
                    end
                end

                default: state <= S_IDLE;
            endcase
        end
    end

endmodule
