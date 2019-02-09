#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/spi/spi.h>
#include <linux/of_device.h>

/* Display resolution */
#define EPD_WIDTH 176
#define EPD_HEIGHT 264
#define EPD_NUM_PIXELS (EPD_WIDTH * EPD_WIDTH)

/* SPI commands */
#define PANEL_SETTING 0x00
#define POWER_SETTING 0x01
#define POWER_OFF 0x02
#define POWER_OFF_SEQUENCE_SETTING 0x03
#define POWER_ON 0x04
#define POWER_ON_MEASURE 0x05
#define BOOSTER_SOFT_START 0x06
#define DEEP_SLEEP 0x07
#define DATA_START_TRANSMISSION_1 0x10
#define DATA_STOP 0x11
#define DISPLAY_REFRESH 0x12
#define DATA_START_TRANSMISSION_2 0x13
#define PARTIAL_DATA_START_TRANSMISSION_1 0x14
#define PARTIAL_DATA_START_TRANSMISSION_2 0x15
#define PARTIAL_DISPLAY_REFRESH 0x16
#define LUT_FOR_VCOM 0x20
#define LUT_WHITE_TO_WHITE 0x21
#define LUT_BLACK_TO_WHITE 0x22
#define LUT_WHITE_TO_BLACK 0x23
#define LUT_BLACK_TO_BLACK 0x24
#define PLL_CONTROL 0x30
#define TEMPERATURE_SENSOR_COMMAND 0x40
#define TEMPERATURE_SENSOR_CALIBRATION 0x41
#define TEMPERATURE_SENSOR_WRITE 0x42
#define TEMPERATURE_SENSOR_READ 0x43
#define VCOM_AND_DATA_INTERVAL_SETTING 0x50
#define LOW_POWER_DETECTION 0x51
#define TCON_SETTING 0x60
#define TCON_RESOLUTION 0x61
#define SOURCE_AND_GATE_START_SETTING 0x62
#define GET_STATUS 0x71
#define AUTO_MEASURE_VCOM 0x80
#define VCOM_VALUE 0x81
#define VCM_DC_SETTING_REGISTER 0x82
#define PROGRAM_MODE 0xA0
#define ACTIVE_PROGRAM 0xA1
#define READ_OTP_DATA 0xA2

#define RST_Pin 17
#define DC_PIN 25
#define CS_PIN 8
#define BUSY_PIN 24

/*

TODO:
- Proper init
- Proper shutdown
- Map physical memory for the mmapping
- override the mmap operation
- clear op
- suspend
- refresh timer

*/

struct epaper_27inch_spi_private {
	int gpio_rst;
	int gpio_busy;
	int gpio_dc;
	int gpio_cs;
};


int epaper_27inch_spi_send_command(struct spi_device *spi, char command)
{
	/* TODO: pull the GPIO low */

	char tx = command;

	struct spi_transfer xfers[] = {
		{
			.len = 1,
			.tx_buf = &tx,
		},
	};

	return spi_sync_transfer(spi, xfers, ARRAY_SIZE(xfers));
}

static int epaper_27inch_spi_probe(struct spi_device *spi) {
	struct epaper_27inch_spi_private *prv;
	int err;
	struct device_node *of_node = spi->dev.of_node;

	printk(KERN_INFO "Probing the epaper SPI\n");

	prv = devm_kzalloc(&spi->dev, sizeof(*prv), GFP_KERNEL);
	if (!prv) {
		err = -ENOMEM;
		goto exit_err;
	}

//	prv->gpio_rst = of_get_named_gpio(of_node, "gpio-rst", 0);
	prv->gpio_rst = 17;
	if (!gpio_is_valid(prv->gpio_rst)) {
		dev_err(&spi->dev, "Unable to request reset GPIO %d\n", prv->gpio_rst);
		err = -ENOMEM; // TODO: actual error
		goto exit_free_mem;
	}

//	prv->gpio_busy = of_get_named_gpio(of_node, "gpio-busy", 0);
	prv->gpio_busy = 24;
	if (!gpio_is_valid(prv->gpio_busy)) {
		dev_err(&spi->dev, "Unable to request busy GPIO %d\n", prv->gpio_busy);
		err = -ENOMEM; // TODO: actual error
		goto exit_free_mem;
	}

	// prv->gpio_cs = of_get_named_gpio(of_node, "gpio-cs", 0);
	// if (!gpio_is_valid(prv->gpio_cs)) {
	// 	dev_err(&spi->dev, "Unable to request GPIO %d\n", prv->gpio_cs);
	// 	err = -ENOMEM; // TODO: actual error
	// 	goto exit_free_mem;
	// }

//	prv->gpio_dc = of_get_named_gpio(of_node, "gpio-dc", 0);
	prv->gpio_dc = 25;
	if (!gpio_is_valid(prv->gpio_dc)) {
		dev_err(&spi->dev, "Unable to request DC GPIO %d\n", prv->gpio_dc);
		err = -ENOMEM; // TODO: actual error
		goto exit_free_mem;
	}

	dev_warn(&spi->dev, "Got GPIOs: %d, %d, %d\n", 
		prv->gpio_rst, prv->gpio_busy, prv->gpio_dc);

	/*
	TODO: command/data GPIO

	err = gpio_request_one(prv->gpio_int, GPIOF_OUT, dev_name(prv->dev));
	if (err) {
		dev_err(prv->dev, "Unable to request GPIO %d\n", prv->gpio_int);
		goto exit_free_mem;
	}
	*/

	spi_set_drvdata(spi, prv);
	return 0;

exit_free_mem:
	kfree(prv);
	return err;

exit_err:
	return err;
}

static int epaper_27inch_spi_remove(struct spi_device *spi) {
	struct epaper_27inch_spi_private *prv = spi_get_drvdata(spi);
	kfree(prv);
	return 0;
}

/* SPI driver */

static const struct of_device_id epaper_27inch_spi_of_match[] = {
	{ .compatible = "waveshare,epaper_27inch", },
	{ }
};
MODULE_DEVICE_TABLE(of, epaper_27inch_spi_of_match);

static const struct spi_device_id epaper_27inch_spi_id_table[] = {
	{ "epaper_27inch", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, epaper_27inch_spi_id_table);

static struct spi_driver epaper_27inch_spi_driver = {
	.driver  = {
		.name   = "epaper_27inch",
		.owner  = THIS_MODULE,
		.of_match_table = epaper_27inch_spi_of_match
	},
	.id_table = epaper_27inch_spi_id_table,
	.probe   = epaper_27inch_spi_probe,
	.remove  = epaper_27inch_spi_remove,
};

/* Framebuffer driver */

// struct epaper_27inch_par {
// 	spinlock_t lock;
// };

// static const struct fb_fix_screeninfo epaper_27inch_fix_screeninfo = {
// 	.visual = FB_VISUAL_MONO01,
// 	.type = FB_TYPE_PACKED_PIXELS,
// 	.id = "2.7 inch EPD",
// 	.line_length = EPD_WIDTH,
// 	.xpanstep =	0,
// 	.ypanstep =	0,
// 	.ywrapstep =	0,
// 	.accel =	FB_ACCEL_NONE,
// };

// static const struct fb_var_screeninfo epaper_27inch_var_screeninfo = {
// 	.xres		= EPD_WIDTH,
// 	.yres		= EPD_HEIGHT,
// 	.xres_virtual	= EPD_WIDTH,
// 	.yres_virtual	= EPD_HEIGHT,
// 	.bits_per_pixel	= 1,
// 	.nonstd		= 1,
// };

// static int epaper_27inch_probe(struct platform_device *op)
// {
// 	struct fb_info *info;
// 	struct epaper_27inch_par *par;
// 	int err;

// 	printk(KERN_INFO "In epaper_27inch_probe\n");

// 	info = framebuffer_alloc(sizeof(struct epaper_27inch_par), &op->dev);
// 	err = -ENOMEM;
// 	if (!info)
// 		goto out_err;

// 	info->fix = epaper_27inch_fix_screeninfo;
// 	info->var = epaper_27inch_var_screeninfo;

// 	par = info->par;
// 	spin_lock_init(&par->lock);

// 	err = register_framebuffer(info);
// 	if (err < 0)
// 		goto out_dealloc_cmap;

// 	dev_set_drvdata(&op->dev, info);

// 	return 0;

// out_dealloc_cmap:
// 	fb_dealloc_cmap(&info->cmap);
// 	framebuffer_release(info);

// out_err:
// 	return err;
// }

// static int epaper_27inch_remove(struct platform_device *op)
// {
// 	struct fb_info *info = dev_get_drvdata(&op->dev);

// 	unregister_framebuffer(info);
// 	fb_dealloc_cmap(&info->cmap);

// 	framebuffer_release(info);
// 	return 0;
// }

// static const struct of_device_id epaper_27inch_match[] = {
// 	{
// 		.name = "waveshare,epaper-2-7-inch",
// 	},
// 	{},
// };
// MODULE_DEVICE_TABLE(of, epaper_27inch_match);

// static struct platform_driver epaper_27inch_driver = {
// 	.driver = {
// 		.name = "epaper_27inch",
// 		.of_match_table = epaper_27inch_match,
// 	},
// 	.probe		= epaper_27inch_probe,
// 	.remove		= epaper_27inch_remove,
// };

// static int __init epaper_27inch_init(void)
// {
// 	int err;

// 	printk(KERN_INFO "In epaper_27inch_init\n");
// 	if (fb_get_options("epaper_27inch", NULL)) {
// 		err = -ENODEV;
// 		printk(KERN_INFO "fb_get_options failed\n");
// 		goto exit_err;
// 	}

// 	err = spi_register_driver(&epaper_27inch_spi_driver);
// 	if (err) {
// 		printk(KERN_INFO "spi_register_driver failed\n");
// 		goto exit_err;
// 	}

// 	err = platform_driver_register(&epaper_27inch_driver);
// 	if (err) {
// 		printk(KERN_INFO "platform_driver_register failed\n");
// 		goto exit_err;
// 	}

// 	return 0;
// exit_err:
// 	return err;
// }

// static void __exit epaper_27inch_exit(void)
// {
// 	printk(KERN_INFO "In epaper_27inch_exit\n");
// 	spi_unregister_driver(&epaper_27inch_spi_driver);
// 	platform_driver_unregister(&epaper_27inch_driver);
// }

module_spi_driver(epaper_27inch_spi_driver);

MODULE_DESCRIPTION("Framebuffer driver for the Waveshare 2.7 inch ePaper display");
MODULE_AUTHOR("John Pothier <john.pothier@outlook.com>");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
