/*
 * 4-row x 8-column background grid
 *
 * Each cell stores an 8-bit color index. The grid maps to the game area
 * (y=60 to y=419), with each cell covering 80x90 pixels.
 *
 * Shadow/active double-buffering: CPU writes go to shadow registers.
 * Active registers latch at vsync (vsync_latch pulse).
 *
 * Output: given pixel coordinates (px, py), outputs the grid cell color
 * index for that position. Returns 0 (black) for pixels outside the
 * game area.
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

    // Shadow and active cell arrays: 4 rows x 8 columns
    logic [7:0] shadow [0:3][0:7];
    logic [7:0] active [0:3][0:7];

    // CPU writes to shadow registers
    always_ff @(posedge clk or posedge reset)
        if (reset) begin
            for (int r = 0; r < 4; r++)
                for (int c = 0; c < 8; c++)
                    shadow[r][c] <= 8'd0;
        end else if (wr_en) begin
            shadow[wr_row][wr_col] <= wr_color;
        end

    // Vsync latch: copy shadow -> active
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

    // Coordinate-to-cell lookup (combinational)
    // Game area: y in [60, 419], each cell is 80 wide x 90 tall
    logic in_game_area;
    logic [9:0] local_y;
    logic [2:0] col;
    logic [1:0] row;

    assign in_game_area = (py >= 10'd60) && (py < 10'd420);
    assign local_y = py - 10'd60;

    // Division by 80: px / 80 (approximate with shift for synthesis)
    // 640 / 8 = 80, so col = px / 80
    // Use a simple comparison chain for 8 columns
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

    // Division by 90: local_y / 90
    always_comb begin
        if (local_y < 10'd90)       row = 2'd0;
        else if (local_y < 10'd180) row = 2'd1;
        else if (local_y < 10'd270) row = 2'd2;
        else                        row = 2'd3;
    end

    assign color_out = in_game_area ? active[row][col] : 8'd0;

endmodule
