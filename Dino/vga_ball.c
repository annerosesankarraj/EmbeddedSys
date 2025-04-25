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
#include "vga_ball.h"

#define DRIVER_NAME "vga_ball"

/* Device register offsets */
#define BALL_XCOOR(x)   ((x) + 0)   // X coordinate register offset
#define BALL_YCOOR(x)   ((x) + 4)   // Y coordinate register offset
#define BG_RED(x)       ((x) + 8)   // Background red component register
#define BG_GREEN(x)     ((x) + 12)  // Background green component register
#define BG_BLUE(x)      ((x) + 16)  // Background blue component register

/* Device information structure */
struct vga_ball_dev {
    struct resource res;     /* resource for our registers */
    void __iomem *virtbase;  /* virtual base address for registers */
    vga_ball_color_t background;
    vga_ball_pos_t   position;
} dev;

/* Write the background color to hardware and store it */
static void write_background(vga_ball_color_t *background) {
    iowrite32(background->red,   BG_RED(dev.virtbase));
    iowrite32(background->green, BG_GREEN(dev.virtbase));
    iowrite32(background->blue,  BG_BLUE(dev.virtbase));
    dev.background = *background;
}

/* Write the ball position to hardware (does not update dev.position here) */
static void write_pos(vga_ball_pos_t *pos) {
    iowrite32(pos->xcoor, BALL_XCOOR(dev.virtbase));
    iowrite32(pos->ycoor, BALL_YCOOR(dev.virtbase));
    // Note: dev.position is updated in the ioctl handler after calling this.
}

/* ioctl handler to service user requests */
static long vga_ball_ioctl(struct file *f, unsigned int cmd, unsigned long arg) {
    vga_ball_arg_t vla;
    vga_ball_pos_t bpos;
    long status = 0;

    switch (cmd) {
    case VGA_BALL_WRITE_BACKGROUND:
        if (copy_from_user(&vla, (vga_ball_arg_t __user *)arg, sizeof(vga_ball_arg_t))) {
            return -EACCES;
        }
        write_background(&vla.background);
        break;

    case VGA_BALL_READ_BACKGROUND:
        vla.background = dev.background;
        if (copy_to_user((vga_ball_arg_t __user *)arg, &vla, sizeof(vga_ball_arg_t))) {
            return -EACCES;
        }
        break;

    case VGA_BALL_WRITE_POS:
        if (copy_from_user(&bpos, (vga_ball_pos_t __user *)arg, sizeof(vga_ball_pos_t))) {
            return -EACCES;
        }
        write_pos(&bpos);
        dev.position = bpos;  // update stored position
        break;

    case VGA_BALL_READ_POS:
        bpos = dev.position;
        if (copy_to_user((vga_ball_pos_t __user *)arg, &bpos, sizeof(vga_ball_pos_t))) {
            return -EACCES;
        }
        break;

    default:
        return -EINVAL;
    }

    return status;
}

/* File operations structure for the misc device */
static const struct file_operations vga_ball_fops = {
    .owner          = THIS_MODULE,
    .unlocked_ioctl = vga_ball_ioctl,
};

/* Misc device structure */
static struct miscdevice vga_ball_misc_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = DRIVER_NAME,
    .fops  = &vga_ball_fops,
};

/* Probe function: called when the device is initialized */
static int __init vga_ball_probe(struct platform_device *pdev) {
    vga_ball_color_t beige = {0xf9, 0xe4, 0xb7};  // default background color
    int ret;

    // Register the misc device (creates /dev/vga_ball)
    ret = misc_register(&vga_ball_misc_device);
    if (ret) {
        pr_err(DRIVER_NAME ": misc device registration failed\n");
        return ret;
    }

    // Get register resource from device tree
    ret = of_address_to_resource(pdev->dev.of_node, 0, &dev.res);
    if (ret) {
        ret = -ENOENT;
        goto fail_register;
    }

    // Request memory region for our device
    if (request_mem_region(dev.res.start, resource_size(&dev.res), DRIVER_NAME) == NULL) {
        ret = -EBUSY;
        goto fail_register;
    }

    // I/O map the memory region for register access
    dev.virtbase = of_iomap(pdev->dev.of_node, 0);
    if (dev.virtbase == NULL) {
        ret = -ENOMEM;
        goto fail_mem_region;
    }

    // Initialize background color and ball position
    write_background(&beige);      // set initial background color
    dev.position.xcoor = 320;      
    dev.position.ycoor = 240;      // set initial ball position to center (matches hardware reset)

    pr_info(DRIVER_NAME ": device initialized\n");
    return 0;

    // Error handling and cleanup:
fail_mem_region:
    release_mem_region(dev.res.start, resource_size(&dev.res));
fail_register:
    misc_deregister(&vga_ball_misc_device);
    return ret;
}

/* Remove function: called when the device is removed/unloaded */
static int vga_ball_remove(struct platform_device *pdev) {
    iounmap(dev.virtbase);
    release_mem_region(dev.res.start, resource_size(&dev.res));
    misc_deregister(&vga_ball_misc_device);
    return 0;
}

/* Device Tree match table */
#ifdef CONFIG_OF
static const struct of_device_id vga_ball_of_match[] = {
    { .compatible = "csee4840,vga_ball-1.0" },
    {},
};
MODULE_DEVICE_TABLE(of, vga_ball_of_match);
#endif

/* Platform driver definition */
static struct platform_driver vga_ball_driver = {
    .driver = {
        .name           = DRIVER_NAME,
        .owner          = THIS_MODULE,
        .of_match_table = of_match_ptr(vga_ball_of_match),
    },
    .probe  = vga_ball_probe,
    .remove = __exit_p(vga_ball_remove),
};

/* Module initialization and exit */
static int __init vga_ball_init(void) {
    pr_info(DRIVER_NAME ": init\n");
    return platform_driver_probe(&vga_ball_driver, vga_ball_probe);
}

static void __exit vga_ball_exit(void) {
    platform_driver_unregister(&vga_ball_driver);
    pr_info(DRIVER_NAME ": exit\n");
}

module_init(vga_ball_init);
module_exit(vga_ball_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Stephen A. Edwards, Columbia University");
MODULE_DESCRIPTION("VGA Ball Driver");
