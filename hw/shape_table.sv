/*
 * 48-entry shape table with shadow/active double-buffering
 *
 * Each entry stores:
 *   type    [1:0]  - 0=rectangle, 1=circle, 2=seven-segment digit
 *   visible [0:0]  - 1=draw, 0=skip
 *   x       [9:0]  - x position (pixel)
 *   y       [8:0]  - y position (pixel)
 *   w       [8:0]  - width (pixels), or digit value in w[3:0] for type=2
 *   h       [8:0]  - height (pixels)
 *   color   [7:0]  - palette color index
 *
 * CPU interface (address-then-data pattern):
 *   1. Write SHAPE_ADDR to select entry index [5:0]
 *   2. Write SHAPE_DATA0: {y[8:0], x[9:0], visible, type[1:0]} = bits [21:0]
 *   3. Write SHAPE_DATA1: {color[7:0], h[8:0], w[8:0]} = bits [25:0]
 *   4. Write SHAPE_COMMIT (any nonzero) to commit data0/data1 into shadow table
 *
 * At vsync_latch, shadow -> active.
 */

module shape_table(
    input  logic        clk,
    input  logic        reset,

    // CPU write interface
    input  logic        addr_wr,     // write to SHAPE_ADDR register
    input  logic [5:0]  addr_data,   // shape index 0-47

    input  logic        data0_wr,    // write to SHAPE_DATA0 register
    input  logic [31:0] data0,

    input  logic        data1_wr,    // write to SHAPE_DATA1 register
    input  logic [31:0] data1,

    input  logic        commit_wr,   // write to SHAPE_COMMIT register

    // Vsync latch
    input  logic        vsync_latch,

    // Read port for renderer (from active table)
    input  logic [5:0]  rd_index,
    output logic [1:0]  rd_type,
    output logic        rd_visible,
    output logic [9:0]  rd_x,
    output logic [8:0]  rd_y,
    output logic [8:0]  rd_w,
    output logic [8:0]  rd_h,
    output logic [7:0]  rd_color
);

    // Staging registers (hold data0/data1 until commit)
    logic [5:0]  cur_addr;
    logic [31:0] staged_data0;
    logic [31:0] staged_data1;

    // Shadow and active tables
    // Pack each entry as {color[7:0], h[8:0], w[8:0], y[8:0], x[9:0], visible, type[1:0]}
    // Total = 8+9+9+9+10+1+2 = 48 bits
    logic [47:0] shadow [0:47];
    logic [47:0] active [0:47];

    // CPU writes: stage address, data0, data1, then commit to shadow
    always_ff @(posedge clk or posedge reset)
        if (reset) begin
            cur_addr <= 6'd0;
            staged_data0 <= 32'd0;
            staged_data1 <= 32'd0;
        end else begin
            if (addr_wr)
                cur_addr <= addr_data;
            if (data0_wr)
                staged_data0 <= data0;
            if (data1_wr)
                staged_data1 <= data1;
        end

    // Commit: pack staged data into shadow table
    always_ff @(posedge clk or posedge reset)
        if (reset) begin
            for (int i = 0; i < 48; i++)
                shadow[i] <= 48'd0;
        end else if (commit_wr) begin
            // DATA0 bit fields: [1:0]=type, [2]=visible, [12:3]=x, [21:13]=y
            // DATA1 bit fields: [8:0]=w, [17:9]=h, [25:18]=color
            shadow[cur_addr] <= {
                staged_data1[25:18],  // color [7:0]  -> bits [47:40]
                staged_data1[17:9],   // h     [8:0]  -> bits [39:31]
                staged_data1[8:0],    // w     [8:0]  -> bits [30:22]
                staged_data0[21:13],  // y     [8:0]  -> bits [21:13]
                staged_data0[12:3],   // x     [9:0]  -> bits [12:3]
                staged_data0[2],      // visible       -> bit  [2]
                staged_data0[1:0]     // type  [1:0]  -> bits [1:0]
            };
        end

    // Vsync latch: shadow -> active
    always_ff @(posedge clk or posedge reset)
        if (reset) begin
            for (int i = 0; i < 48; i++)
                active[i] <= 48'd0;
        end else if (vsync_latch) begin
            for (int i = 0; i < 48; i++)
                active[i] <= shadow[i];
        end

    // Read port: unpack active table entry
    wire [47:0] entry = active[rd_index];
    assign rd_type    = entry[1:0];
    assign rd_visible = entry[2];
    assign rd_x       = entry[12:3];
    assign rd_y       = entry[21:13];
    assign rd_w       = entry[30:22];
    assign rd_h       = entry[39:31];
    assign rd_color   = entry[47:40];

endmodule
