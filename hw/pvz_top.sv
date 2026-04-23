/*
 * pvz_top — Avalon-MM peripheral that drives the VGA display for PvZ
 *
 * Hardware half of the game. Takes register writes from the HPS (the
 * ARM Cortex-A9 running Linux) and paints a 640x480 @60 Hz picture on
 * the DE1-SoC's VGA DAC, one scanline at a time.
 *
 * --- How it fits ---
 * See the design document's "System Block Diagram" (Figure 1) and
 * "Peripheral Internal Structure" (Figure 2, Table 2). This module is
 * the "pvz_top" box in those diagrams; the sub-modules below are the
 * inner boxes.
 *
 * --- Background for anyone new to this ---
 *
 * 1. Avalon-MM slave interface. Platform Designer's on-chip bus. A
 *    master drives 4 signals we care about here:
 *       chipselect  — 1 when this peripheral is being addressed
 *       write       — 1 on a write transaction
 *       writedata   — 32 bits of payload
 *       address     — which register within the peripheral
 *    No readdata port, so CPU reads from our offsets return the fabric
 *    default of 0.
 *
 * 2. Word vs byte addressing. The TCL descriptor (pvz_top_hw.tcl) sets
 *    `addressUnits = WORDS`, so the `address` port here is a word index
 *    (0..4). The CPU issues byte-addressed writes; the kernel driver
 *    uses `iowrite32(val, base + 4*N)`. The lightweight bridge divides
 *    by 4 on the way in. That is why offsets 0x00, 0x04, 0x08, 0x0C,
 *    0x10 land on `address` values 0, 1, 2, 3, 4 below.
 *
 * 3. Lightweight HPS-to-FPGA bridge. 32-bit memory-mapped window the
 *    CPU sees at physical address 0xFF200000. The kernel driver
 *    `ioremap`s this range and adds a per-peripheral base offset from
 *    Platform Designer. See "System Architecture" and "Register Map"
 *    in the design document.
 *
 * 4. Shadow/active vsync-latched double-buffering. CPU writes go into
 *    shadow copies inside `bg_grid` and `shape_table`. Once per frame,
 *    at the start of the vertical blanking interval (vcount=480,
 *    hcount=0), the `vsync_latch` pulse below copies every shadow into
 *    its active counterpart in one cycle. The renderer only reads the
 *    active copy, so a half-written frame is never displayed. Standard
 *    fix for mid-frame tearing.
 *
 * 5. Three-way binding. This Verilog, `pvz_top_hw.tcl`, and the kernel
 *    driver's `of_device_id` table (in `sw/pvz_driver.c`) must agree
 *    on port names and the compatible string "csee4840,pvz_gpu-1.0".
 *    If they diverge, the device node just does not show up at boot:
 *    no error, nothing in dmesg.
 *
 * --- Register map (byte offsets from CPU; word indices on this bus) ---
 *
 *   0x00  BG_CELL      bg_grid[row][col] <= color (one cell)
 *                      [2:0]   = col   (0..7)
 *                      [4:3]   = row   (0..3)
 *                      [15:8]  = color (8-bit palette index)
 *
 *   0x04  SHAPE_ADDR   cur_addr <= index
 *                      [5:0]   = shape-table slot (0..47)
 *
 *   0x08  SHAPE_DATA0  staged_data0 <= writedata
 *                      [1:0]   = type (0=rect, 1=circle, 2=digit, 3=sprite)
 *                      [2]     = visible
 *                      [12:3]  = x
 *                      [21:13] = y
 *
 *   0x0C  SHAPE_DATA1  staged_data1 <= writedata
 *                      [8:0]   = w
 *                      [17:9]  = h
 *                      [25:18] = color
 *
 *   0x10  SHAPE_COMMIT if writedata != 0: shadow[cur_addr] <=
 *                         {color, h, w, y, x, visible, type}  (48 bits)
 *
 * Only SHAPE_COMMIT has per-entry staging. BG_CELL writes through to
 * the shadow grid directly. See "Register Map" and "Shape Table Entry
 * Format" (Figure 5) in the design document.
 */

module pvz_top(
    input  logic        clk,
    input  logic        reset,

    // Avalon-MM slave interface
    input  logic [2:0]  address,      // word offset (0-4)
    input  logic [31:0] writedata,
    input  logic        write,
    input  logic        chipselect,

    // VGA output
    output logic [7:0]  VGA_R, VGA_G, VGA_B,
    output logic        VGA_CLK, VGA_HS, VGA_VS,
    output logic        VGA_BLANK_n,
    output logic        VGA_SYNC_n
);

    // ---------------------------------------------------------------
    // VGA timing
    //
    // `vga_counters` divides the 50 MHz board clock by 2 for a 25 MHz
    // pixel clock and runs hcount/vcount through the 640x480 @ 60 Hz
    // back-porch/front-porch envelope (hcount 0..1599 counts at 50 MHz,
    // i.e. two ticks per pixel; vcount 0..524 counts lines). See the
    // "Timing" section of the design document for Table 4.
    // ---------------------------------------------------------------
    logic [10:0] hcount;
    logic [9:0]  vcount;

    vga_counters counters(
        .clk50(clk),
        .reset(reset),
        .hcount(hcount),
        .vcount(vcount),
        .VGA_CLK(VGA_CLK),
        .VGA_HS(VGA_HS),
        .VGA_VS(VGA_VS),
        .VGA_BLANK_n(VGA_BLANK_n),
        .VGA_SYNC_n(VGA_SYNC_n)
    );

    // hcount counts half-pixels (50 MHz ticks on a 25 MHz pixel clock),
    // so pixel_x is hcount >> 1. pixel_y is just vcount.
    wire [9:0] pixel_x = hcount[10:1];
    wire [9:0] pixel_y = vcount[9:0];

    // ---------------------------------------------------------------
    // vsync_latch: one-cycle pulse the moment the active 640x480
    // region ends. Fires when vcount first steps onto line 480 (first
    // blank line after the last visible scanline) at hcount=0. Every
    // shadow register in this peripheral copies into its active on
    // this pulse, which is what keeps frames from tearing. See
    // "System Architecture" in the design document.
    // ---------------------------------------------------------------
    logic vsync_latch;
    assign vsync_latch = (vcount == 10'd480) && (hcount == 11'd0);

    // ---------------------------------------------------------------
    // lb_swap: one pulse per line at hcount=0 during the active 480
    // lines. Tells `linebuffer` to swap draw/display banks so the next
    // scanline gets built while the current one drains to the DAC.
    // See Figure 3 ("Per-scanline timing") in the design doc.
    // ---------------------------------------------------------------
    logic lb_swap;
    assign lb_swap = (hcount == 11'd0) && (vcount < 10'd480);

    // ---------------------------------------------------------------
    // render_start: kicks off the scanline FSM in `shape_renderer`.
    // Offset 2 hcount ticks after `lb_swap` so the swap has settled
    // and the renderer gets the full ~1598-tick window to fill the
    // draw buffer before the next swap.
    // ---------------------------------------------------------------
    logic render_start;
    assign render_start = (hcount == 11'd2) && (vcount < 10'd480);

    // ---------------------------------------------------------------
    // Linebuffer: two 640x8 banks. One is read by the DAC path (at
    // `pixel_x`) while the other takes writes from `shape_renderer`.
    // Roles flip on each `lb_swap`. Read port returns 8-bit palette
    // indices; color_palette turns them into RGB.
    // ---------------------------------------------------------------
    logic        lb_wr_en;
    logic [9:0]  lb_wr_addr;
    logic [7:0]  lb_wr_data;
    logic [7:0]  lb_rd_data;

    linebuffer lb_inst(
        .clk(clk),
        .reset(reset),
        .wr_en(lb_wr_en),
        .wr_addr(lb_wr_addr),
        .wr_data(lb_wr_data),
        .rd_addr(pixel_x),
        .rd_data(lb_rd_data),
        .swap(lb_swap)
    );

    // ---------------------------------------------------------------
    // Background grid: 4 rows x 8 cols of palette indices. CPU writes
    // one cell per BG_CELL transaction. Reads come from the renderer
    // asking "what color is the cell at (px,py)?" via a coord-to-cell
    // lookup. Shadow/active double-buffered so CPU updates mid-frame
    // don't tear.
    // ---------------------------------------------------------------
    logic        bg_wr_en;
    logic [2:0]  bg_wr_col;
    logic [1:0]  bg_wr_row;
    logic [7:0]  bg_wr_color;

    logic [9:0]  bg_query_px, bg_query_py;
    logic [7:0]  bg_query_color;

    bg_grid bg_inst(
        .clk(clk),
        .reset(reset),
        .wr_en(bg_wr_en),
        .wr_col(bg_wr_col),
        .wr_row(bg_wr_row),
        .wr_color(bg_wr_color),
        .vsync_latch(vsync_latch),
        .px(bg_query_px),
        .py(bg_query_py),
        .color_out(bg_query_color)
    );

    // ---------------------------------------------------------------
    // Shape table: 48 slots, 48 bits each. Writing a new shape is a
    // four-write sequence from the CPU:
    //   SHAPE_ADDR  -> cur_addr
    //   SHAPE_DATA0 -> staged_data0
    //   SHAPE_DATA1 -> staged_data1
    //   SHAPE_COMMIT (nonzero) -> shadow[cur_addr] <= packed entry
    // The staged regs let the CPU assemble one 48-bit entry from two
    // 32-bit writes before it becomes visible. The whole shadow table
    // copies into active on `vsync_latch`.
    // ---------------------------------------------------------------
    logic        st_addr_wr, st_data0_wr, st_data1_wr, st_commit_wr;
    logic [5:0]  st_addr_data;
    logic [31:0] st_data0, st_data1;

    logic [5:0]  st_rd_index;
    logic [1:0]  st_rd_type;
    logic        st_rd_visible;
    logic [9:0]  st_rd_x;
    logic [8:0]  st_rd_y, st_rd_w, st_rd_h;
    logic [7:0]  st_rd_color;

    shape_table st_inst(
        .clk(clk),
        .reset(reset),
        .addr_wr(st_addr_wr),
        .addr_data(st_addr_data),
        .data0_wr(st_data0_wr),
        .data0(st_data0),
        .data1_wr(st_data1_wr),
        .data1(st_data1),
        .commit_wr(st_commit_wr),
        .vsync_latch(vsync_latch),
        .rd_index(st_rd_index),
        .rd_type(st_rd_type),
        .rd_visible(st_rd_visible),
        .rd_x(st_rd_x),
        .rd_y(st_rd_y),
        .rd_w(st_rd_w),
        .rd_h(st_rd_h),
        .rd_color(st_rd_color)
    );

    // ---------------------------------------------------------------
    // Sprite ROM: 1024x8-bit M10K block initialised from
    // `peas_idx.mem` (ASCII hex, 1024 bytes, row-major). Today it holds
    // one 32x32 peashooter sprite. Bytes are palette indices, and the
    // value 0xFF is the transparent sentinel: the renderer skips the
    // line-buffer write for those bytes so irregular sprite outlines
    // composite over the background without leaving a square outline.
    // Reads have a 1-cycle registered latency; the renderer handles
    // that by issuing the address one cycle before it needs the pixel.
    // ---------------------------------------------------------------
    logic [9:0] sprite_rd_addr;
    logic [7:0] sprite_rd_pixel;

    sprite_rom sprite_inst(
        .clk(clk),
        .addr(sprite_rd_addr),
        .pixel(sprite_rd_pixel)
    );

    // ---------------------------------------------------------------
    // Shape renderer: scanline FSM. For the current scanline it first
    // paints background cells into the draw line buffer, then walks
    // the 48-entry shape table in order (painter's algorithm: later
    // entries overlay earlier ones). Queries `bg_grid`, `shape_table`,
    // and `sprite_rom`, and writes bursts into `linebuffer` via
    // lb_wr_en/lb_wr_addr/lb_wr_data.
    // ---------------------------------------------------------------
    // render_done fires from the FSM at end of scanline. Nothing
    // consumes it: the linebuffer swap is hcount-driven, not data-
    // driven. Kept in the port list for testbench visibility, and
    // silenced for Verilator's unused-signal lint.
    /* verilator lint_off UNUSED */
    logic render_done;
    /* verilator lint_on UNUSED */

    shape_renderer renderer(
        .clk(clk),
        .reset(reset),
        .render_start(render_start),
        .scanline(pixel_y),
        .render_done(render_done),
        .bg_px(bg_query_px),
        .bg_py(bg_query_py),
        .bg_color(bg_query_color),
        .shape_index(st_rd_index),
        .shape_type(st_rd_type),
        .shape_visible(st_rd_visible),
        .shape_x(st_rd_x),
        .shape_y(st_rd_y),
        .shape_w(st_rd_w),
        .shape_h(st_rd_h),
        .shape_color(st_rd_color),
        .sprite_rd_addr(sprite_rd_addr),
        .sprite_rd_pixel(sprite_rd_pixel),
        .lb_wr_en(lb_wr_en),
        .lb_wr_addr(lb_wr_addr),
        .lb_wr_data(lb_wr_data)
    );

    // ---------------------------------------------------------------
    // Color palette: combinational 256->24 LUT. MVP uses 13 live
    // entries (see "Color Palette" section, Table 13). The renderer
    // and sprite ROM both speak 8-bit palette indices; only this last
    // stage converts to the VGA DAC's R/G/B.
    // ---------------------------------------------------------------
    logic [7:0] pal_r, pal_g, pal_b;

    color_palette pal_inst(
        .index(lb_rd_data),
        .r(pal_r),
        .g(pal_g),
        .b(pal_b)
    );

    // VGA output mux. VGA_BLANK_n must be low during the hsync/vsync
    // porches, otherwise the monitor sees out-of-range video and may
    // refuse to sync. Forcing RGB to 0 during blanking is belt-and-
    // braces.
    always_comb begin
        if (VGA_BLANK_n) begin
            VGA_R = pal_r;
            VGA_G = pal_g;
            VGA_B = pal_b;
        end else begin
            VGA_R = 8'h00;
            VGA_G = 8'h00;
            VGA_B = 8'h00;
        end
    end

    // ---------------------------------------------------------------
    // Avalon-MM register decode.
    //
    // The five CPU byte offsets (0x00/0x04/0x08/0x0C/0x10) map to
    // `address` values 0/1/2/3/4 here because the TCL descriptor sets
    // `addressUnits = WORDS`: Platform Designer divides the CPU's
    // byte address by 4 before presenting it on this port. The decode
    // is combinational. A matching chipselect+write raises exactly
    // one of the *_wr enables for one cycle, and the downstream
    // module registers the data on the next edge.
    // ---------------------------------------------------------------
    always_comb begin
        bg_wr_en    = 1'b0;
        bg_wr_col   = 3'd0;
        bg_wr_row   = 2'd0;
        bg_wr_color = 8'd0;
        st_addr_wr  = 1'b0;
        st_addr_data = 6'd0;
        st_data0_wr = 1'b0;
        st_data0    = 32'd0;
        st_data1_wr = 1'b0;
        st_data1    = 32'd0;
        st_commit_wr = 1'b0;

        if (chipselect && write) begin
            case (address)
                3'd0: begin // BG_CELL (byte offset 0x00)
                    bg_wr_en    = 1'b1;
                    bg_wr_col   = writedata[2:0];
                    bg_wr_row   = writedata[4:3];
                    bg_wr_color = writedata[15:8];
                end
                3'd1: begin // SHAPE_ADDR (byte offset 0x04)
                    st_addr_wr   = 1'b1;
                    st_addr_data = writedata[5:0];
                end
                3'd2: begin // SHAPE_DATA0 (byte offset 0x08)
                    st_data0_wr = 1'b1;
                    st_data0    = writedata;
                end
                3'd3: begin // SHAPE_DATA1 (byte offset 0x0C)
                    st_data1_wr = 1'b1;
                    st_data1    = writedata;
                end
                3'd4: begin // SHAPE_COMMIT (byte offset 0x10)
                    st_commit_wr = (writedata != 32'd0);
                end
                default: ;
            endcase
        end
    end

endmodule
