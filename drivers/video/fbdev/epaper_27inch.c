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
#define COMMAND_PANEL_SETTING 0x00
#define COMMAND_POWER_SETTING 0x01
#define COMMAND_POWER_OFF 0x02
#define COMMAND_POWER_OFF_SEQUENCE_SETTING 0x03
#define COMMAND_POWER_ON 0x04
#define COMMAND_POWER_ON_MEASURE 0x05
#define COMMAND_BOOSTER_SOFT_START 0x06
#define COMMAND_DEEP_SLEEP 0x07
#define COMMAND_DATA_START_TRANSMISSION_1 0x10
#define COMMAND_DATA_STOP 0x11
#define COMMAND_DISPLAY_REFRESH 0x12
#define COMMAND_DATA_START_TRANSMISSION_2 0x13
#define COMMAND_PARTIAL_DATA_START_TRANSMISSION_1 0x14
#define COMMAND_PARTIAL_DATA_START_TRANSMISSION_2 0x15
#define COMMAND_PARTIAL_DISPLAY_REFRESH 0x16
#define COMMAND_LUT_FOR_VCOM 0x20
#define COMMAND_LUT_WHITE_TO_WHITE 0x21
#define COMMAND_LUT_BLACK_TO_WHITE 0x22
#define COMMAND_LUT_WHITE_TO_BLACK 0x23
#define COMMAND_LUT_BLACK_TO_BLACK 0x24
#define COMMAND_PLL_CONTROL 0x30
#define COMMAND_TEMPERATURE_SENSOR_COMMAND 0x40
#define COMMAND_TEMPERATURE_SENSOR_CALIBRATION 0x41
#define COMMAND_TEMPERATURE_SENSOR_WRITE 0x42
#define COMMAND_TEMPERATURE_SENSOR_READ 0x43
#define COMMAND_VCOM_AND_DATA_INTERVAL_SETTING 0x50
#define COMMAND_LOW_POWER_DETECTION 0x51
#define COMMAND_TCON_SETTING 0x60
#define COMMAND_TCON_RESOLUTION 0x61
#define COMMAND_SOURCE_AND_GATE_START_SETTING 0x62
#define COMMAND_GET_STATUS 0x71
#define COMMAND_AUTO_MEASURE_VCOM 0x80
#define COMMAND_VCOM_VALUE 0x81
#define COMMAND_VCM_DC_SETTING_REGISTER 0x82
#define COMMAND_PROGRAM_MODE 0xA0
#define COMMAND_ACTIVE_PROGRAM 0xA1
#define COMMAND_READ_OTP_DATA 0xA2

#define DEEP_SLEEP_MAGIC 0xA5

#define RST_Pin 17
#define DC_PIN 25
#define CS_PIN 8
#define BUSY_PIN 24

#define DC_COMMAND 0
#define DC_DATA 1

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
	int gpio_rst_n;
	int gpio_busy;
	int gpio_dc;
	int gpio_cs;
};

/* Look-up tables. */
static char lut_vcom_dc[44] = {
    0x00, 0x00,
    0x00, 0x08, 0x00, 0x00, 0x00, 0x02,
    0x60, 0x28, 0x28, 0x00, 0x00, 0x01,
    0x00, 0x14, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x12, 0x12, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static char lut_ww[42] = {
    0x40, 0x08, 0x00, 0x00, 0x00, 0x02,
    0x90, 0x28, 0x28, 0x00, 0x00, 0x01,
    0x40, 0x14, 0x00, 0x00, 0x00, 0x01,
    0xA0, 0x12, 0x12, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static char lut_bw[42] = {
    0x40, 0x08, 0x00, 0x00, 0x00, 0x02,
    0x90, 0x28, 0x28, 0x00, 0x00, 0x01,
    0x40, 0x14, 0x00, 0x00, 0x00, 0x01,
    0xA0, 0x12, 0x12, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static char lut_bb[42] = {
    0x80, 0x08, 0x00, 0x00, 0x00, 0x02,
    0x90, 0x28, 0x28, 0x00, 0x00, 0x01,
    0x80, 0x14, 0x00, 0x00, 0x00, 0x01,
    0x50, 0x12, 0x12, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static char lut_wb[42] = {
    0x80, 0x08, 0x00, 0x00, 0x00, 0x02,
    0x90, 0x28, 0x28, 0x00, 0x00, 0x01,
    0x80, 0x14, 0x00, 0x00, 0x00, 0x01,
    0x50, 0x12, 0x12, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static int epaper_27inch_spi_send_command(struct spi_device *spi, char command)
{
	struct epaper_27inch_spi_private *prv;
	struct spi_transfer xfers[] = {
		{
			.len = 1,
			.tx_buf = &command,
		},
	};

	prv = spi_get_drvdata(spi);
	gpio_set_value(prv->gpio_dc, DC_COMMAND);

	return spi_sync_transfer(spi, xfers, ARRAY_SIZE(xfers));
}

static int epaper_27inch_spi_send_data(struct spi_device *spi, char* data, int data_size)
{
	struct epaper_27inch_spi_private *prv;
	struct spi_transfer xfers[] = {
		{
			.len = data_size,
			.tx_buf = data,
		},
	};

	prv = spi_get_drvdata(spi);
	gpio_set_value(prv->gpio_dc, DC_DATA);


	return spi_sync_transfer(spi, xfers, ARRAY_SIZE(xfers));
}

static int epaper_27inch_spi_send_data_single(struct spi_device *spi, char data) {
	return epaper_27inch_spi_send_data(spi, &data, 1);
}

static int epaper_27inch_spi_send_command_with_data(struct spi_device *spi, char command, char* data, int data_size)
{
	int err;
	err = epaper_27inch_spi_send_command(spi, command);
	if (err) {
		return err;
	}

	return epaper_27inch_spi_send_data(spi, data, data_size);
}

static void epaper_27inch_wait_until_idle(struct spi_device *spi) {
	struct epaper_27inch_spi_private *prv;
	prv = spi_get_drvdata(spi);

	/* The reference implementation sends the GET_STATUS command while spinning
	on the idle GPIO. Though it is unclear why, we will do that as well. */
	while (gpio_get_value(prv->gpio_busy) == 0) {
		epaper_27inch_spi_send_command(spi, COMMAND_GET_STATUS);
		usleep_range(500, 1000);
	}
}

static int epaper_27inch_clear(struct spi_device *spi) {
	int i;
	char pixel;
	int err;

	pixel = 0xff;

	/* temp code */
	err = epaper_27inch_spi_send_command(spi, COMMAND_DATA_START_TRANSMISSION_1);
	if (err) {
		return err;
	}

	for (i = 0; i < (EPD_WIDTH * EPD_HEIGHT) / 8; ++i) {
		err = epaper_27inch_spi_send_data(spi, &pixel, 1);
		if (err) {
			return err;
		}
	}

	err = epaper_27inch_spi_send_command(spi, COMMAND_DATA_START_TRANSMISSION_2);
	if (err) {
		return err;
	}

	for (i = 0; i < (EPD_WIDTH * EPD_HEIGHT) / 8; ++i) {
		err = epaper_27inch_spi_send_data(spi, &pixel, 1);
		if (err) {
			return err;
		}
	}

	return 0;
}

static int epaper_27inch_sleep(struct spi_device *spi) {
	struct epaper_27inch_spi_private *prv;

	prv = spi_get_drvdata(spi);

	epaper_27inch_spi_send_command(spi, COMMAND_VCOM_AND_DATA_INTERVAL_SETTING);
	/* VBD = 0b11, DDX = 0b11, CDI = 0b0111 */
	epaper_27inch_spi_send_data_single(spi, 0xf7);
	epaper_27inch_spi_send_command(spi, COMMAND_POWER_OFF);
	epaper_27inch_spi_send_command(spi, COMMAND_DEEP_SLEEP);
	epaper_27inch_spi_send_data_single(spi, DEEP_SLEEP_MAGIC);

	return 0;
}

static void epaper_27inch_reset(struct spi_device *spi) {
	int reset_delay;
	struct epaper_27inch_spi_private *prv;

	prv = spi_get_drvdata(spi);

	/* This is the value used in the reference implementation */
	reset_delay = 200;

	gpio_set_value(prv->gpio_rst_n, 1);
	msleep(reset_delay);
	gpio_set_value(prv->gpio_rst_n, 0);
	msleep(reset_delay);
	gpio_set_value(prv->gpio_rst_n, 1);
	msleep(reset_delay);
}

static int epaper_27inch_poweron(struct spi_device *spi) {
	struct epaper_27inch_spi_private *prv;
	prv = spi_get_drvdata(spi);

	epaper_27inch_spi_send_command(spi, COMMAND_POWER_ON);
	epaper_27inch_wait_until_idle(spi);

	return 0;
}

static int epaper_27inch_configure_power(struct spi_device *spi) {
	struct epaper_27inch_spi_private *prv;
	prv = spi_get_drvdata(spi);

	/* This sequence of black magic incantations is taken from the reference
	implementation, which implements the flow chart found in section 8-2 of the
	data sheet */

	epaper_27inch_spi_send_command(spi, COMMAND_POWER_SETTING);

	/* VDS_EN, VDG_EN */
    epaper_27inch_spi_send_data_single(spi, 0x03);
    /* VCOM_HV, VGHL_LV[1], VGHL_LV[0] */
    epaper_27inch_spi_send_data_single(spi, 0x00);
    /* VDH */
    epaper_27inch_spi_send_data_single(spi, 0x2b);
    /* VDL */
    epaper_27inch_spi_send_data_single(spi, 0x2b);
    /* VDHR */
    epaper_27inch_spi_send_data_single(spi, 0x09);
    epaper_27inch_spi_send_command(spi, COMMAND_BOOSTER_SOFT_START);
    epaper_27inch_spi_send_data_single(spi, 0x07);
    epaper_27inch_spi_send_data_single(spi, 0x07);
    epaper_27inch_spi_send_data_single(spi, 0x17);
    /* Power optimization */
    epaper_27inch_spi_send_command(spi, 0xF8);
    epaper_27inch_spi_send_data_single(spi, 0x60);
    epaper_27inch_spi_send_data_single(spi, 0xA5);
    /* Power optimization */
    epaper_27inch_spi_send_command(spi, 0xF8);
    epaper_27inch_spi_send_data_single(spi, 0x89);
    epaper_27inch_spi_send_data_single(spi, 0xA5);
    /* Power optimization */
    epaper_27inch_spi_send_command(spi, 0xF8);
    epaper_27inch_spi_send_data_single(spi, 0x90);
    epaper_27inch_spi_send_data_single(spi, 0x00);
    /* Power optimization */
    epaper_27inch_spi_send_command(spi, 0xF8);
    epaper_27inch_spi_send_data_single(spi, 0x93);
    epaper_27inch_spi_send_data_single(spi, 0x2A);
    /* Power optimization */
    epaper_27inch_spi_send_command(spi, 0xF8);
    epaper_27inch_spi_send_data_single(spi, 0xA0);
    epaper_27inch_spi_send_data_single(spi, 0xA5);
    /* Power optimization */
    epaper_27inch_spi_send_command(spi, 0xF8);
    epaper_27inch_spi_send_data_single(spi, 0xA1);
    epaper_27inch_spi_send_data_single(spi, 0x00);
    /* Power optimization */
    epaper_27inch_spi_send_command(spi, 0xF8);
    epaper_27inch_spi_send_data_single(spi, 0x73);
    epaper_27inch_spi_send_data_single(spi, 0x41);
    epaper_27inch_spi_send_command(spi, COMMAND_PARTIAL_DISPLAY_REFRESH);
    epaper_27inch_spi_send_data_single(spi, 0x00);

	return 0;
}

static int epaper_27inch_configure_panel(struct spi_device *spi) {
	struct epaper_27inch_spi_private *prv;
	prv = spi_get_drvdata(spi);

	epaper_27inch_spi_send_command(spi, COMMAND_PANEL_SETTING);
	/* # KW-BF   KWR-AF    BWROTP 0f */
	epaper_27inch_spi_send_data_single(spi, 0xAF);
	epaper_27inch_spi_send_command(spi, COMMAND_PLL_CONTROL);
	/* 3A 100HZ   29 150Hz 39 200HZ    31 171HZ */
	epaper_27inch_spi_send_data_single(spi, 0x3A);
	epaper_27inch_spi_send_command(spi, COMMAND_VCM_DC_SETTING_REGISTER);
	epaper_27inch_spi_send_data_single(spi, 0x12);

	return 0;
}

static int epaper_27inch_set_lut(struct spi_device *spi) {
	struct epaper_27inch_spi_private *prv;
	prv = spi_get_drvdata(spi);

	/* TODO */

	return 0;
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

	prv->gpio_rst_n = of_get_named_gpio(of_node, "gpio-rst", 0);
	if (!gpio_is_valid(prv->gpio_rst_n)) {
		dev_err(&spi->dev, "Unable to request reset GPIO %d\n", prv->gpio_rst_n);
		err = -ENOMEM; // TODO: actual error
		goto exit_free_mem;
	}

	prv->gpio_busy = of_get_named_gpio(of_node, "gpio-busy", 0);
	if (!gpio_is_valid(prv->gpio_busy)) {
		dev_err(&spi->dev, "Unable to request busy GPIO %d\n", prv->gpio_busy);
		err = -ENOMEM; // TODO: actual error
		goto exit_free_mem;
	}

	prv->gpio_dc = of_get_named_gpio(of_node, "gpio-dc", 0);
	if (!gpio_is_valid(prv->gpio_dc)) {
		dev_err(&spi->dev, "Unable to request DC GPIO %d\n", prv->gpio_dc);
		err = -ENOMEM; // TODO: actual error
		goto exit_free_mem;
	}

	spi_set_drvdata(spi, prv);

	epaper_27inch_reset(spi);

	err = epaper_27inch_configure_power(spi);
	if (err) {
		goto exit_free_mem;
	}

	err = epaper_27inch_poweron(spi);
	if (err) {
		goto exit_free_mem;
	}

	err = epaper_27inch_configure_panel(spi);
	if (err) {
		goto exit_free_mem;
	}

	err = epaper_27inch_set_lut(spi);
	if (err) {
		goto exit_free_mem;
	}

	err = epaper_27inch_clear(spi);
	if (err) {
		goto exit_free_mem;
	}

	return 0;

exit_free_mem:
	kfree(prv);
	return err;

exit_err:
	return err;
}

static int epaper_27inch_spi_remove(struct spi_device *spi) {
	struct epaper_27inch_spi_private *prv = spi_get_drvdata(spi);

	epaper_27inch_sleep(spi);
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
