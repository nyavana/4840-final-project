/*
 * shape_table.sv — 48-entry shape table with staging and vsync latching
 *
 * Role
 *   Holds up to 48 drawable entries that the renderer walks each
 *   scanline. Each entry is a packed 48-bit record describing one shape
 *   (rectangle, circle, seven-segment digit, or sprite).
 *
 * How it fits
 *   Instantiated inside pvz_top. pvz_top's Avalon decode routes four
 *   CPU-side registers (SHAPE_ADDR / SHAPE_DATA0 / SHAPE_DATA1 /
 *   SHAPE_COMMIT) into the write strobes on this module. shape_renderer's
 *   S_SHAPE_SETUP state drives rd_index and latches the unpacked geometry
 *   on the next cycle. See design-document §Register Map (Table 10) and
 *   §Shape Table Entry Format (§5.3, Figure 5).
 *
 * Background concepts
 *   - Staging with an explicit commit. A shape entry is 48 bits but the
 *     Avalon bus moves 32 bits per write, so the CPU splits each update
 *     into four writes: SHAPE_ADDR picks the slot, SHAPE_DATA0 and
 *     SHAPE_DATA1 buffer the two halves of the record, and SHAPE_COMMIT
 *     (any nonzero value) snaps the staged halves into shadow[cur_addr].
 *     Writing the two halves without committing has no visible effect.
 *   - Shadow/active double-buffering. Commits land in shadow[]. The
 *     vsync_latch pulse at the start of vertical blanking copies the whole
 *     shadow[] array into active[]. The renderer only reads active[], so a
 *     half-finished CPU update never shows up mid-frame. Worst-case
 *     latency from a commit to on-screen pixels is one frame.
 *
 * Entry packing (48 bits, layout matches design-document Figure 5)
 *   [47:40] color    (8)   palette index, ignored for type=3
 *   [39:31] h        (9)   height in pixels
 *   [30:22] w        (9)   width (for type=2 the low 4 bits carry the digit)
 *   [21:13] y        (9)   on-screen top-left y
 *   [12:3]  x        (10)  on-screen top-left x
 *   [2]     visible  (1)   renderer skips the slot when 0
 *   [1:0]   type     (2)   0=rect, 1=circle, 2=7-seg digit, 3=sprite
 *
 * Key state
 *   cur_addr       : which shadow slot the next SHAPE_COMMIT targets.
 *   staged_data0/1 : buffered copies of the last SHAPE_DATA0/1 writes.
 *   shadow[0..47]  : CPU-visible packed entries, updated on commit_wr.
 *   active[0..47]  : render-visible packed entries, latched on vsync.
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

    // Staging registers. cur_addr picks which shadow slot the next commit
    // targets and is captured from SHAPE_ADDR writes. staged_data0 and
    // staged_data1 hold the last DATA0/DATA1 values so the CPU can write
    // them in either order before pulsing SHAPE_COMMIT. The verilator lint
    // waivers below exist because the pack logic only reads sub-ranges of
    // the 32-bit words.
    logic [5:0]  cur_addr;
    /* verilator lint_off UNUSED */
    logic [31:0] staged_data0;
    logic [31:0] staged_data1;
    /* verilator lint_on UNUSED */

    // Shadow and active tables. 48 entries x 48 bits = 2 304 bits each.
    // Small enough that Quartus usually keeps them in MLABs / registers
    // instead of M10K, which avoids the 1-cycle BRAM read latency the
    // sprite ROM has. Field order matches design-document §Shape Table
    // Entry Format (§5.3):
    // {color[7:0], h[8:0], w[8:0], y[8:0], x[9:0], visible, type[1:0]}
    // Width check: 8+9+9+9+10+1+2 = 48.
    logic [47:0] shadow [0:47];
    logic [47:0] active [0:47];

    // Staging path. Each cycle this block latches whichever of
    // addr_wr / data0_wr / data1_wr the Avalon decode asserts. Multiple
    // strobes can fire in the same cycle if the CPU ever manages
    // back-to-back writes; each targets its own register independently.
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

    // Commit: pack the two staged 32-bit halves into one 48-bit shadow
    // entry at cur_addr. commit_wr is the decoded SHAPE_COMMIT strobe from
    // pvz_top, already filtered for nonzero writedata there, so any pulse
    // seen here is a real commit. See §Register Map SHAPE_COMMIT for the
    // Verilog snippet reproduced below.
    always_ff @(posedge clk or posedge reset)
        if (reset) begin
            for (int i = 0; i < 48; i++)
                shadow[i] <= 48'd0;
        end else if (commit_wr) begin
            // Source layout inside the staged 32-bit words:
            //   DATA0: [1:0]=type, [2]=visible, [12:3]=x, [21:13]=y
            //   DATA1: [8:0]=w,    [17:9]=h,   [25:18]=color
            // Destination is the 48-bit packed entry from the header.
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

    // Vsync latch: snap the full shadow array into active in one cycle.
    // vga_counters raises vsync_latch at the start of vertical blanking
    // (vcount=480, hcount=0), so the renderer always sees a consistent
    // set of 48 entries even if the CPU was mid-update.
    always_ff @(posedge clk or posedge reset)
        if (reset) begin
            for (int i = 0; i < 48; i++)
                active[i] <= 48'd0;
        end else if (vsync_latch) begin
            for (int i = 0; i < 48; i++)
                active[i] <= shadow[i];
        end

    // Read port: unpack a single active entry combinationally. The
    // renderer drives rd_index in S_SHAPE_SETUP; these assigns decode the
    // 48-bit packed record back into the individual rd_* fields in the
    // same order the commit block packed them. No read latency because
    // active[] lives in registers, not M10K BRAM.
    wire [47:0] entry = active[rd_index];
    assign rd_type    = entry[1:0];
    assign rd_visible = entry[2];
    assign rd_x       = entry[12:3];
    assign rd_y       = entry[21:13];
    assign rd_w       = entry[30:22];
    assign rd_h       = entry[39:31];
    assign rd_color   = entry[47:40];

endmodule
