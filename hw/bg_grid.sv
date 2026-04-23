/*
 * bg_grid.sv — 4-row x 8-column background color grid
 *
 * Role
 *   Returns the background palette index for any on-screen pixel inside the
 *   4x8 game board. Pixels outside the board read back as color 0 (black),
 *   so the output plugs straight into the bottom layer of the painter's
 *   pass in shape_renderer.
 *
 * How it fits
 *   Instantiated inside pvz_top. The CPU writes cells through the BG_CELL
 *   Avalon register (see design-document §Register Map, Table 10 and
 *   §Display Layout, Figure 7). shape_renderer drives px/py with the pixel
 *   it is filling on the current scanline and reads color_out as the
 *   S_BG_FILL payload.
 *
 * Background concepts
 *   - Shadow/active double-buffering. CPU writes land in shadow[]. Once per
 *     frame, at vcount=480 hcount=0, vga_counters pulses vsync_latch and
 *     the whole shadow array copies into active[]. Rendering only ever
 *     reads active[], so a CPU write in the middle of a frame cannot tear
 *     the picture. See design-document §System Architecture.
 *   - Grid geometry. The 4x8 board sits in the middle of the 640x480
 *     screen: y in [60, 420), x in [0, 640). Each cell is 80 wide by 90
 *     tall, matching CELL_WIDTH/CELL_HEIGHT in sw/game.h. The top 60 pixels
 *     are the HUD; the bottom 60 fall through to black. See
 *     design-document §Display Layout, Figure 7.
 *
 * Key state
 *   shadow[r][c] : CPU-visible copy, one byte palette index per cell.
 *   active[r][c] : render-visible copy, latched from shadow on vsync.
 *   color_out    : combinational lookup on (px, py). 0 outside the game
 *                  area, else active[row][col].
 */

module bg_grid(
    input  logic        clk,
    input  logic        reset,

    // CPU write port (to shadow registers)
    input  logic        wr_en,
    input  logic [2:0]  wr_col,    // 0-7
    input  logic [1:0]  wr_row,    // 0-3
    input  logic [7:0]  wr_color,

    // Vsync latch signal
    input  logic        vsync_latch,

    // Pixel coordinate query
    input  logic [9:0]  px,
    input  logic [9:0]  py,
    output logic [7:0]  color_out
);

    // Shadow and active cell arrays: 4 rows x 8 columns of 8-bit palette
    // indices. 32 bytes per copy (256 bits) — small enough that Quartus
    // keeps them in distributed logic instead of M10K blocks. BG_CELL
    // writes land in the shadow copy; shape_renderer reads the active
    // copy during S_BG_FILL.
    logic [7:0] shadow [0:3][0:7];
    logic [7:0] active [0:3][0:7];

    // CPU write port -> shadow. pvz_top's Avalon decode already pulls
    // wr_row/wr_col/wr_color out of BG_CELL bits [4:3]/[2:0]/[15:8], so
    // this block just writes the cell when wr_en is asserted.
    always_ff @(posedge clk or posedge reset)
        if (reset) begin
            for (int r = 0; r < 4; r++)
                for (int c = 0; c < 8; c++)
                    shadow[r][c] <= 8'd0;
        end else if (wr_en) begin
            shadow[wr_row][wr_col] <= wr_color;
        end

    // Vsync latch: bulk-copy shadow -> active. Runs on the single-cycle
    // vsync_latch pulse vga_counters raises at the start of vertical
    // blanking, so any partial CPU updates from the previous frame become
    // visible atomically.
    always_ff @(posedge clk or posedge reset)
        if (reset) begin
            for (int r = 0; r < 4; r++)
                for (int c = 0; c < 8; c++)
                    active[r][c] <= 8'd0;
        end else if (vsync_latch) begin
            for (int r = 0; r < 4; r++)
                for (int c = 0; c < 8; c++)
                    active[r][c] <= shadow[r][c];
        end

    // Coordinate-to-cell lookup (pure combinational).
    // px, py are the 10-bit pixel coordinates being drawn. The game area
    // is the rectangle y in [60, 420), x in [0, 640). Anything above (HUD)
    // or below (padding) returns 0 and the caller deals with it.
    // See design-document §Display Layout, Figure 7.
    logic in_game_area;
    logic [9:0] local_y;
    logic [2:0] col;
    logic [1:0] row;

    assign in_game_area = (py >= 10'd60) && (py < 10'd420); // GAME_AREA_Y=60, 60 + 4*CELL_HEIGHT=420
    assign local_y = py - 10'd60;                           // y relative to top of game area

    // Pick column. 640 px / 8 columns = 80 px per cell (CELL_WIDTH). The
    // comparison chain synthesises to a priority encoder; for eight
    // branches it is cheaper and clearer than a divider.
    always_comb begin
        if (px < 10'd80)       col = 3'd0;
        else if (px < 10'd160) col = 3'd1;
        else if (px < 10'd240) col = 3'd2;
        else if (px < 10'd320) col = 3'd3;
        else if (px < 10'd400) col = 3'd4;
        else if (px < 10'd480) col = 3'd5;
        else if (px < 10'd560) col = 3'd6;
        else                   col = 3'd7;
    end

    // Pick row. CELL_HEIGHT = 90, four rows cover local_y in [0, 360).
    // The last bucket is an else so local_y values just under 360 land in
    // row 3 without a fifth compare.
    always_comb begin
        if (local_y < 10'd90)       row = 2'd0;
        else if (local_y < 10'd180) row = 2'd1;
        else if (local_y < 10'd270) row = 2'd2;
        else                        row = 2'd3;
    end

    // Return the cell's active color inside the board, 0 elsewhere.
    assign color_out = in_game_area ? active[row][col] : 8'd0;

endmodule
