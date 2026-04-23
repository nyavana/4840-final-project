/*
 * pvz_driver.h  kernel-private state and constants for the PvZ GPU driver.
 *
 * Only pvz_driver.c includes this header. It holds the per-instance
 * device state the kernel module carries while loaded. Userspace talks
 * to the driver through /dev/pvz and the ioctls declared in pvz.h; see
 * design-document §Kernel Driver Interface and Figure 6 for the full
 * path from render_frame down to an iowrite32 on the lightweight
 * HPS-to-FPGA bridge.
 *
 * Notes for someone coming from userspace C.
 *   - The driver is a Linux "misc device": one character node (/dev/pvz)
 *     with a file_operations table that dispatches ioctl commands. The
 *     node name comes from DRIVER_NAME below. misc_register() in
 *     pvz_driver.c creates /dev/<DRIVER_NAME> on its own.
 *   - It is also a "platform driver" because the peripheral is found
 *     through the device tree instead of a bus probe. The kernel walks
 *     the dtb, finds a node whose "compatible" property is
 *     "csee4840,pvz_gpu-1.0" (emitted by pvz_top_hw.tcl, matched in
 *     pvz_driver.c's of_match table), and calls the probe function.
 *   - struct resource res holds the physical address range that probe
 *     pulled from the device tree via of_address_to_resource(). On our
 *     board that's the 20-byte window at 0xFF200000, the base of the
 *     lightweight HPS-to-FPGA bridge for pvz_top.
 *   - void __iomem *virtbase is the same window after ioremap(), ready
 *     to hand to iowrite32(). The __iomem annotation is a sparse
 *     marker: don't dereference it as a plain pointer, only pass it to
 *     the {read,write}{b,w,l} accessors.
 *   - Userspace never sees this struct. pvz_probe stashes a pointer to
 *     it with platform_set_drvdata and the ioctl handler fetches it back.
 *
 * Defines and types in this file:
 *   DRIVER_NAME     also the misc-device node name (/dev/pvz).
 *   struct pvz_dev  per-instance state: MMIO resource + ioremap base.
 */

#ifndef _PVZ_DRIVER_H
#define _PVZ_DRIVER_H

#include <linux/miscdevice.h>
#include <linux/platform_device.h>

/* Doubles as the filename under /dev: misc_register(&pvz_misc_device)
 * creates /dev/pvz because pvz_misc_device.name = DRIVER_NAME. */
#define DRIVER_NAME "pvz"

/*
 * Per-instance driver state. There's only one pvz_top in the fabric, so
 * pvz_driver.c allocates this as a file-scope static. res describes the
 * physical MMIO window from the device tree; virtbase is that window
 * after ioremap, used as the base for iowrite32.
 */
struct pvz_dev {
    struct resource res;
    void __iomem *virtbase;
};

#endif /* _PVZ_DRIVER_H */
