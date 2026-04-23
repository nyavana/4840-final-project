/*
 * pvz.h  shared kernel/userspace contract for the PvZ GPU peripheral.
 *
 * Single source of truth for the register byte offsets, shape-type
 * enum, ioctl argument structs, and ioctl command numbers. Included by
 * the kernel driver (sw/pvz_driver.c) and by every userspace program
 * that talks to /dev/pvz (sw/render.c, sw/test/*).
 *
 * How it fits. pvz_top exposes five 32-bit write-only registers on the
 * lightweight HPS-to-FPGA bridge at 0xFF200000 (design-document
 * §Register Map, Table 10). The kernel driver wraps that MMIO behind
 * three ioctls. This header is the contract both sides agree on. Figure
 * 6 shows the full stack from a render_frame() call down to an
 * iowrite32.
 *
 * Notes for someone coming from userspace C.
 *   - Avalon byte-vs-word addressing. Byte offset on the CPU side;
 *     Platform Designer sets addressUnits = WORDS on the slave, so the
 *     Avalon fabric forwards byte_addr / 4 as `address`.
 *     iowrite32(val, base + 4*N) therefore targets word N on the
 *     peripheral. Userspace never touches these offsets; the ioctls
 *     hide the bus layout.
 *   - _IOW / _IO are kernel macros that pack a direction bit, a magic
 *     number ('p' here; arbitrary but has to be unique per subsystem),
 *     an ordinal, and sizeof(argument) into one 32-bit command code.
 *     Userspace calls ioctl(fd, PVZ_WRITE_BG, &arg); the kernel switch
 *     in pvz_ioctl matches on that code and copy_from_user's the
 *     argument.
 *
 * Defines in this file:
 *   PVZ_BG_CELL, PVZ_SHAPE_ADDR, PVZ_SHAPE_DATA0, PVZ_SHAPE_DATA1,
 *   PVZ_SHAPE_COMMIT  Avalon byte offsets from the peripheral base.
 *   SHAPE_RECT / CIRCLE / DIGIT / SPRITE  values for the type[1:0]
 *     field in SHAPE_DATA0. Have to match the encoding in
 *     hw/shape_renderer.sv.
 *   pvz_bg_arg_t, pvz_shape_arg_t  ioctl argument structs.
 *   PVZ_WRITE_BG, PVZ_WRITE_SHAPE, PVZ_COMMIT_SHAPES  ioctl command
 *     codes.
 */

#ifndef _PVZ_H
#define _PVZ_H

#include <linux/ioctl.h>

/*
 * Shared header for PvZ GPU kernel driver and userspace programs.
 *
 * Register byte offsets (from driver base):
 *   0x00  BG_CELL      - background grid cell color
 *   0x04  SHAPE_ADDR   - shape table entry select
 *   0x08  SHAPE_DATA0  - shape type/visible/x/y
 *   0x0C  SHAPE_DATA1  - shape w/h/color
 *   0x10  SHAPE_COMMIT - commit shape entry
 */

/* Avalon register byte offsets.
 *
 * CPU-side byte offsets from the pvz_top base (0xFF200000 on the
 * DE1-SoC). The fabric divides by 4 to get the word offset pvz_top
 * actually decodes. See design-document Table 10. */
#define PVZ_BG_CELL       0x00
#define PVZ_SHAPE_ADDR    0x04
#define PVZ_SHAPE_DATA0   0x08
#define PVZ_SHAPE_DATA1   0x0C
#define PVZ_SHAPE_COMMIT  0x10

/* Shape types  value of the type[1:0] field in SHAPE_DATA0. Has to
 * stay in sync with the case split in hw/shape_renderer.sv. Adding a
 * type here without teaching the renderer about it produces a silent
 * no-op on screen. */
#define SHAPE_RECT    0
#define SHAPE_CIRCLE  1
#define SHAPE_DIGIT   2
#define SHAPE_SPRITE  3  /* 32x32 sprite ROM, rendered at 2x -> 64x64 on screen */

/* Background cell write argument.
 *
 * The driver packs this into BG_CELL as
 *   col[2:0] | (row << 3) | (color << 8)
 * to match the bitfield layout in design-document §BG_CELL. */
typedef struct {
    unsigned char row;    /* 0-3 */
    unsigned char col;    /* 0-7 */
    unsigned char color;  /* palette index 0-255 */
} pvz_bg_arg_t;

/* Shape write argument.
 *
 * Fields pack into SHAPE_DATA0 (type, visible, x, y) and SHAPE_DATA1
 * (w, h, color) per design-document §SHAPE_DATA0 / §SHAPE_DATA1. The
 * driver latches index via SHAPE_ADDR, stages both data words, then
 * pulses SHAPE_COMMIT. See §Write Sequence. */
typedef struct {
    unsigned char index;   /* shape table index 0-47 */
    unsigned char type;    /* SHAPE_RECT, SHAPE_CIRCLE, SHAPE_DIGIT */
    unsigned char visible; /* 1=visible, 0=hidden */
    unsigned short x;      /* x position 0-639 */
    unsigned short y;      /* y position 0-479 */
    unsigned short w;      /* width (or digit value in low 4 bits for SHAPE_DIGIT) */
    unsigned short h;      /* height */
    unsigned char color;   /* palette color index */
} pvz_shape_arg_t;

/* ioctl command codes. _IOW(magic, nr, type) packs direction=W,
 * magic='p', ordinal, and sizeof(type) into one 32-bit code that the
 * kernel switch in pvz_ioctl matches on. _IO takes no argument struct. */
#define PVZ_MAGIC 'p'
#define PVZ_WRITE_BG       _IOW(PVZ_MAGIC, 1, pvz_bg_arg_t)
#define PVZ_WRITE_SHAPE    _IOW(PVZ_MAGIC, 2, pvz_shape_arg_t)
#define PVZ_COMMIT_SHAPES  _IO(PVZ_MAGIC, 3)

#endif /* _PVZ_H */
