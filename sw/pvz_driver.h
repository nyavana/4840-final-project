#ifndef _PVZ_DRIVER_H
#define _PVZ_DRIVER_H

#include <linux/miscdevice.h>
#include <linux/platform_device.h>

#define DRIVER_NAME "pvz"

struct pvz_dev {
    struct resource res;
    void __iomem *virtbase;
};

#endif /* _PVZ_DRIVER_H */
