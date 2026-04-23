/*
 * pvz_audio_driver — Linux misc-device driver for the pvz_audio peripheral.
 * Exposes /dev/pvz_audio with three ioctls defined in pvz_audio_driver.h.
 *
 * Voice round-robin: on each PLAY_SFX, the driver writes to whichever voice
 * did NOT last play, so two back-to-back cues never stomp each other.
 * Argument 0 to PLAY_SFX stops whichever voice last played.
 *
 * Compatible: csee4840,pvz_audio-1.0
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include "pvz_audio_driver.h"

#define DRIVER_NAME "pvz_audio"

/* Register offsets in bytes (word addressing → N * 4 on the CPU side). */
#define REG_BGM_CTRL        0x00
#define REG_SFX_VOICE0_TRIG 0x04
#define REG_SFX_VOICE1_TRIG 0x08
#define REG_STATUS          0x0C

struct pvz_audio_dev {
    struct resource res;
    void __iomem   *virtbase;
    unsigned int    last_voice;  /* 0 or 1 */
};

static struct pvz_audio_dev dev;

static long pvz_audio_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
    uint32_t val;

    switch (cmd) {
    case PVZ_AUDIO_BGM_CTRL:
        if (copy_from_user(&val, (uint32_t __user *)arg, sizeof(val)))
            return -EACCES;
        iowrite32(val & 0x1, dev.virtbase + REG_BGM_CTRL);
        break;

    case PVZ_AUDIO_PLAY_SFX:
        if (copy_from_user(&val, (uint32_t __user *)arg, sizeof(val)))
            return -EACCES;
        /* Round-robin: pick the voice that didn't last play. */
        if (dev.last_voice == 0) {
            iowrite32(val & 0xF, dev.virtbase + REG_SFX_VOICE1_TRIG);
            dev.last_voice = 1;
        } else {
            iowrite32(val & 0xF, dev.virtbase + REG_SFX_VOICE0_TRIG);
            dev.last_voice = 0;
        }
        break;

    case PVZ_AUDIO_STATUS:
        val = ioread32(dev.virtbase + REG_STATUS);
        if (copy_to_user((uint32_t __user *)arg, &val, sizeof(val)))
            return -EACCES;
        break;

    default:
        return -EINVAL;
    }

    return 0;
}

static const struct file_operations pvz_audio_fops = {
    .owner          = THIS_MODULE,
    .unlocked_ioctl = pvz_audio_ioctl,
};

static struct miscdevice pvz_audio_misc_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = DRIVER_NAME,
    .fops  = &pvz_audio_fops,
};

static int __init pvz_audio_probe(struct platform_device *pdev)
{
    int ret;

    ret = misc_register(&pvz_audio_misc_device);
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

    dev.last_voice = 1;  /* first PLAY_SFX will go to voice 0 */
    pr_info(DRIVER_NAME ": initialized at 0x%08lx\n",
            (unsigned long)dev.res.start);
    return 0;

out_release_mem_region:
    release_mem_region(dev.res.start, resource_size(&dev.res));
out_deregister:
    misc_deregister(&pvz_audio_misc_device);
    return ret;
}

static int pvz_audio_remove(struct platform_device *pdev)
{
    iounmap(dev.virtbase);
    release_mem_region(dev.res.start, resource_size(&dev.res));
    misc_deregister(&pvz_audio_misc_device);
    pr_info(DRIVER_NAME ": removed\n");
    return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id pvz_audio_of_match[] = {
    { .compatible = "csee4840,pvz_audio-1.0" },
    {},
};
MODULE_DEVICE_TABLE(of, pvz_audio_of_match);
#endif

static struct platform_driver pvz_audio_driver = {
    .driver = {
        .name           = DRIVER_NAME,
        .owner          = THIS_MODULE,
        .of_match_table = of_match_ptr(pvz_audio_of_match),
    },
    .remove = __exit_p(pvz_audio_remove),
};

static int __init pvz_audio_init(void)
{
    pr_info(DRIVER_NAME ": init\n");
    return platform_driver_probe(&pvz_audio_driver, pvz_audio_probe);
}

static void __exit pvz_audio_exit(void)
{
    platform_driver_unregister(&pvz_audio_driver);
    pr_info(DRIVER_NAME ": exit\n");
}

module_init(pvz_audio_init);
module_exit(pvz_audio_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("CSEE4840 PvZ team");
MODULE_DESCRIPTION("pvz_audio peripheral driver");
