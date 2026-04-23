/*
 * PvZ GPU kernel driver.
 *
 * Misc device at /dev/pvz with three ioctls: background cell write, shape
 * entry write, shape commit. Same shape as lab3's vga_ball.c.
 *
 * Compatible: csee4840,pvz_gpu-1.0
 *
 * Where it sits in the stack (design-document §Kernel Driver Interface,
 * Figure 6). The game loop in sw/game.c calls render_frame, which issues
 * PVZ_WRITE_BG / PVZ_WRITE_SHAPE ioctls on /dev/pvz. Those arrive at
 * pvz_ioctl below. The handler packs each argument into the pvz_top
 * bitfield layout and does one or more iowrite32s to the lightweight
 * HPS-to-FPGA bridge at 0xFF200000. The Avalon slave inside pvz_top
 * decodes the incoming address as a word offset (pvz_top_hw.tcl sets
 * addressUnits = WORDS) and updates bg_grid or the shape-table shadow.
 * The change becomes visible on the next vsync.
 *
 * Notes for someone coming from userspace C. The file uses four kernel
 * idioms you rarely meet in userland:
 *
 *   1. Misc device. The driver exposes one character node, /dev/pvz,
 *      through miscdevice + file_operations. Userspace open()s it and
 *      issues ioctl()s; pvz_ioctl is the single dispatcher.
 *
 *   2. Device-tree binding. pvz_top is found through the device tree,
 *      not PCI or USB, so the module registers a platform_driver with
 *      an of_device_id table. The kernel walks the dtb at boot, finds
 *      a node whose "compatible" property is "csee4840,pvz_gpu-1.0"
 *      (the string emitted by pvz_top_hw.tcl), and calls pvz_probe
 *      with the matching platform_device.
 *
 *   3. MMIO mapping. pvz_probe pulls the peripheral's physical address
 *      range from the device tree (of_address_to_resource), reserves
 *      it (request_mem_region), and maps it into kernel virtual memory
 *      (of_iomap, which wraps ioremap). The result lives in
 *      dev.virtbase, a `void __iomem *`. The __iomem annotation is a
 *      sparse marker that says: only touch this through
 *      iowrite{8,16,32}/ioread* accessors. Direct dereference trips
 *      sparse and, on some arches, the compiler.
 *
 *   4. Avalon byte-vs-word addressing. The CPU issues byte addresses.
 *      pvz_top_hw.tcl sets addressUnits = WORDS on the slave, so the
 *      Avalon fabric forwards byte_addr / 4 as `address`. To reach
 *      register N (N = 0..4), write to virtbase + 4*N. That is why
 *      the PVZ_* offsets in pvz.h are 0x00, 0x04, 0x08, 0x0C, 0x10
 *      and not 0, 1, 2, 3, 4.
 *
 * Key definitions in this file:
 *   write_bg_cell / write_shape  register packers.
 *   pvz_ioctl                    dispatch for PVZ_WRITE_BG,
 *                                PVZ_WRITE_SHAPE, PVZ_COMMIT_SHAPES.
 *   pvz_fops, pvz_misc_device    character-device wiring.
 *   pvz_probe / pvz_remove       MMIO setup and teardown.
 *   pvz_of_match                 device-tree compatible-string match.
 *   pvz_driver                   platform_driver registration.
 *
 * Patterned after lab3's sw/vga_ball.c.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include "pvz.h"
#include "pvz_driver.h"

/* Only one pvz_top exists in the fabric, so a file-scope static holds
 * the single instance. probe fills res and virtbase; the ioctl handler
 * reads virtbase. */
static struct pvz_dev dev;

/*
 * Write one background cell color to the FPGA.
 *
 * BG_CELL register layout (design-document §BG_CELL):
 *   col   [2:0]   grid column 0..7
 *   row   [4:3]   grid row 0..3
 *   color [15:8]  palette index 0..255
 *
 * One 32-bit write stages the value in the shadow bg_grid. It flips to
 * active on the next vsync. Background cells need no commit pulse.
 */
static void write_bg_cell(pvz_bg_arg_t *bg)
{
    u32 val = (bg->col & 0x7) |          // col[2:0]
              ((bg->row & 0x3) << 3) |   // row[4:3]
              ((bg->color & 0xFF) << 8); // color[15:8]
    iowrite32(val, dev.virtbase + PVZ_BG_CELL);
}

/*
 * Write one shape entry to the FPGA.
 *
 * Issues the four-write burst pvz_top expects. ADDR first to pick which
 * of the 48 shape-table slots to update; DATA0 (type/visible/x/y) and
 * DATA1 (w/h/color) to stage the 48-bit entry; a nonzero COMMIT to pack
 * the staged words into shadow[cur_addr]. The shadow entry goes live on
 * the next vsync. See design-document §Write Sequence.
 *
 * Bit layouts per design-document §SHAPE_DATA0 / §SHAPE_DATA1:
 *   DATA0: type[1:0] | visible[2] | x[12:3] | y[21:13]
 *   DATA1: w[8:0]    | h[17:9]    | color[25:18]
 */
static void write_shape(pvz_shape_arg_t *s)
{
    u32 addr_val = s->index & 0x3F; // SHAPE_ADDR.index is 6 bits (0..47)
    u32 data0 = (s->type & 0x3) |              // type[1:0]
                ((s->visible & 0x1) << 2) |    // visible[2]
                ((s->x & 0x3FF) << 3) |        // x[12:3], 10 bits
                ((s->y & 0x1FF) << 13);        // y[21:13], 9 bits
    u32 data1 = (s->w & 0x1FF) |               // w[8:0]
                ((s->h & 0x1FF) << 9) |        // h[17:9]
                ((s->color & 0xFF) << 18);     // color[25:18]

    // Four byte-offset writes. pvz_top_hw.tcl sets addressUnits=WORDS,
    // so the fabric forwards these as word offsets 1, 2, 3, 4. The
    // `base + 4*N` form is already baked into the PVZ_SHAPE_* constants
    // in pvz.h.
    iowrite32(addr_val, dev.virtbase + PVZ_SHAPE_ADDR);
    iowrite32(data0,    dev.virtbase + PVZ_SHAPE_DATA0);
    iowrite32(data1,    dev.virtbase + PVZ_SHAPE_DATA1);
    iowrite32(1,        dev.virtbase + PVZ_SHAPE_COMMIT); // any nonzero triggers commit
}

/*
 * ioctl dispatch. `arg` is the userspace pointer passed to ioctl(2). The
 * kernel can't dereference a userspace pointer directly, so each case
 * uses copy_from_user to pull the argument struct across the privilege
 * boundary into a kernel-stack copy before reading its fields. -EACCES
 * on a short or faulting copy matches the lab3 convention.
 *
 * PVZ_WRITE_BG       one iowrite32 to BG_CELL, no commit needed.
 * PVZ_WRITE_SHAPE    ADDR -> DATA0 -> DATA1 -> COMMIT four-write burst.
 * PVZ_COMMIT_SHAPES  reserved for a future batch-commit optimization.
 *                    Today it just re-pulses SHAPE_COMMIT against the
 *                    last-staged entry. Harmless, but not part of the
 *                    normal write path.
 */
static long pvz_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
    pvz_bg_arg_t bg;
    pvz_shape_arg_t shape;

    switch (cmd) {
    case PVZ_WRITE_BG:
        // `arg` points into userspace. Copy the pvz_bg_arg_t into the
        // kernel-side `bg` before reading it.
        if (copy_from_user(&bg, (pvz_bg_arg_t *)arg, sizeof(bg)))
            return -EACCES;
        write_bg_cell(&bg);
        break;

    case PVZ_WRITE_SHAPE:
        // Same userspace-to-kernel copy for the larger shape struct.
        if (copy_from_user(&shape, (pvz_shape_arg_t *)arg, sizeof(shape)))
            return -EACCES;
        write_shape(&shape);
        break;

    case PVZ_COMMIT_SHAPES:
        iowrite32(1, dev.virtbase + PVZ_SHAPE_COMMIT);
        break;

    default:
        return -EINVAL;
    }

    return 0;
}

/* Only ioctl is wired up; no read/write/mmap path. Clients have to
 * open(/dev/pvz) and use ioctl. .unlocked_ioctl runs without the big
 * kernel lock. */
static const struct file_operations pvz_fops = {
    .owner          = THIS_MODULE,
    .unlocked_ioctl = pvz_ioctl,
};

/* misc_register(&pvz_misc_device) creates /dev/pvz (DRIVER_NAME) with a
 * dynamically assigned minor number. */
static struct miscdevice pvz_misc_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = DRIVER_NAME,
    .fops  = &pvz_fops,
};

/*
 * Probe path. The kernel calls this once a device-tree node with
 * compatible = "csee4840,pvz_gpu-1.0" matches pvz_of_match. Four steps:
 *
 *   1. misc_register           creates /dev/pvz.
 *   2. of_address_to_resource  pulls the physical MMIO range from the
 *                              dtb reg property into dev.res. On our
 *                              board that's 0xFF200000, 20 bytes.
 *   3. request_mem_region      records the reservation in /proc/iomem
 *                              so no other driver maps the same range.
 *   4. of_iomap                ioremap()s the physical range into
 *                              dev.virtbase, the kernel-virtual base
 *                              every subsequent iowrite32 uses.
 *
 * Error paths unwind in reverse through the out_* labels (standard
 * kernel goto cleanup). On success the module logs the physical base
 * so you can cross-check with /proc/iomem.
 */
static int __init pvz_probe(struct platform_device *pdev)
{
    int ret;

    ret = misc_register(&pvz_misc_device);
    if (ret) {
        pr_err(DRIVER_NAME ": misc_register failed\n");
        return ret;
    }

    ret = of_address_to_resource(pdev->dev.of_node, 0, &dev.res);
    if (ret) {
        ret = -ENOENT;
        goto out_deregister;
    }

    if (request_mem_region(dev.res.start, resource_size(&dev.res),
                           DRIVER_NAME) == NULL) {
        ret = -EBUSY;
        goto out_deregister;
    }

    dev.virtbase = of_iomap(pdev->dev.of_node, 0);
    if (dev.virtbase == NULL) {
        ret = -ENOMEM;
        goto out_release_mem_region;
    }

    pr_info(DRIVER_NAME ": initialized at 0x%08lx\n",
            (unsigned long)dev.res.start);

    return 0;

out_release_mem_region:
    release_mem_region(dev.res.start, resource_size(&dev.res));
out_deregister:
    misc_deregister(&pvz_misc_device);
    return ret;
}

static int pvz_remove(struct platform_device *pdev)
{
    iounmap(dev.virtbase);
    release_mem_region(dev.res.start, resource_size(&dev.res));
    misc_deregister(&pvz_misc_device);
    pr_info(DRIVER_NAME ": removed\n");
    return 0;
}

#ifdef CONFIG_OF
/*
 * Device-tree binding table. At boot the kernel walks every dtb node
 * and, for each compatible string, searches for a driver whose
 * of_match_table holds that exact string. A match triggers
 * platform_driver_probe() below, which calls pvz_probe.
 *
 * The string "csee4840,pvz_gpu-1.0" has to appear verbatim in three
 * places or the driver silently fails to probe:
 *   - hw/pvz_top_hw.tcl       (producer; sopc2dts emits it into the dtb)
 *   - the generated dtb node  (consumer; this table matches on it)
 *   - here, in pvz_of_match   (binds the match to this module)
 * See design-document §Platform Designer Integration.
 */
static const struct of_device_id pvz_of_match[] = {
    { .compatible = "csee4840,pvz_gpu-1.0" },
    {},
};
MODULE_DEVICE_TABLE(of, pvz_of_match);
#endif

static struct platform_driver pvz_driver = {
    .driver = {
        .name           = DRIVER_NAME,
        .owner          = THIS_MODULE,
        .of_match_table = of_match_ptr(pvz_of_match),
    },
    .remove = __exit_p(pvz_remove),
};

static int __init pvz_init(void)
{
    pr_info(DRIVER_NAME ": init\n");
    return platform_driver_probe(&pvz_driver, pvz_probe);
}

static void __exit pvz_exit(void)
{
    platform_driver_unregister(&pvz_driver);
    pr_info(DRIVER_NAME ": exit\n");
}

module_init(pvz_init);
module_exit(pvz_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("CSEE4840 Team");
MODULE_DESCRIPTION("PvZ GPU display engine driver");
