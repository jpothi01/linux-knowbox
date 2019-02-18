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
#include <linux/uaccess.h>
#include <linux/spi/spi.h>
#include <linux/of_device.h>

/* These values are swapped from the reference implementation so
that the longer side is "width". This means we need to rotate when
drawing, but it's worth it to have the right screen orientation */
#define EPD_HEIGHT 264
#define EPD_WIDTH 176
#define EPD_NUM_PIXELS (EPD_WIDTH * EPD_HEIGHT)

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

struct epaper_27inch {
	int gpio_rst_n;
	int gpio_busy;
	int gpio_dc;
	struct fb_info *fb_info;
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

static char lut_wb[42] = {
    0x80, 0x08, 0x00, 0x00, 0x00, 0x02,
    0x90, 0x28, 0x28, 0x00, 0x00, 0x01,
    0x80, 0x14, 0x00, 0x00, 0x00, 0x01,
    0x50, 0x12, 0x12, 0x00, 0x00, 0x01,
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

static int epaper_27inch_spi_send_command(struct spi_device *spi, char command)
{
	struct epaper_27inch *epaper;
	struct spi_transfer xfers[] = {
		{
			.len = 1,
			.tx_buf = &command,
		},
	};

	epaper = spi_get_drvdata(spi);
	gpio_set_value(epaper->gpio_dc, DC_COMMAND);

	return spi_sync_transfer(spi, xfers, ARRAY_SIZE(xfers));
}

static int epaper_27inch_spi_send_data(struct spi_device *spi, char* data, int data_size)
{
	struct epaper_27inch *epaper;
	int i;
	int err;

	epaper = spi_get_drvdata(spi);
	gpio_set_value(epaper->gpio_dc, DC_DATA);

	for (i = 0; i < data_size; ++i) {
		struct spi_transfer xfers[] = {
			{
				.len = 1,
				.tx_buf = &data[i],
			},
		};

		err = spi_sync_transfer(spi, xfers, ARRAY_SIZE(xfers));
		if (err) {
			return err;
		}
	}	

	return 0;
}

static int epaper_27inch_spi_send_data_single(struct spi_device *spi, char data) {
	return epaper_27inch_spi_send_data(spi, &data, 1);
}

// static int epaper_27inch_spi_send_command_with_data(struct spi_device *spi, char command, char* data, int data_size)
// {
// 	int err;
// 	err = epaper_27inch_spi_send_command(spi, command);
// 	if (err) {
// 		return err;
// 	}

// 	return epaper_27inch_spi_send_data(spi, data, data_size);
// }

static int epaper_27inch_get_status(struct spi_device *spi) {
	int err;
	struct epaper_27inch *epaper;
	char status;

	epaper = spi_get_drvdata(spi);

	err = epaper_27inch_spi_send_command(spi, COMMAND_GET_STATUS);
	if (err) {
		return err;
	}

	gpio_set_value(epaper->gpio_dc, DC_DATA);
	err = spi_read(spi, &status, 1);
	if (err) {
		return err;
	}

	return 0;
}

static void epaper_27inch_wait_until_idle(struct spi_device *spi) {
	struct epaper_27inch *epaper;
	epaper = spi_get_drvdata(spi);

	dev_warn(&spi->dev, "Entering epaper_27inch_wait_until_idle\n");

	/* The reference implementation sends the GET_STATUS command while spinning
	on the idle GPIO. Though it is unclear why, we will do that as well. */
	epaper_27inch_get_status(spi);
	while (gpio_get_value(epaper->gpio_busy) == 0) {
		epaper_27inch_get_status(spi);
		usleep_range(500, 1000);
	}

	msleep(100);
}

static int epaper_27inch_clear(struct spi_device *spi) {
	int i;
	char pixel;
	int err;

	dev_warn(&spi->dev, "Entering epaper_27inch_clear\n");

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

	err = epaper_27inch_spi_send_command(spi, COMMAND_DISPLAY_REFRESH);
	if (err) {
		return err;
	}

	epaper_27inch_wait_until_idle(spi);

	return 0;
}

static int epaper_27inch_sleep(struct spi_device *spi) {
	struct epaper_27inch *epaper;
	int err;

	dev_warn(&spi->dev, "Entering epaper_27inch_sleep\n");

	epaper = spi_get_drvdata(spi);

	err = epaper_27inch_spi_send_command(spi, COMMAND_VCOM_AND_DATA_INTERVAL_SETTING);
	if (err) {
		return err;
	}

	/* VBD = 0b11, DDX = 0b11, CDI = 0b0111 */
	err = epaper_27inch_spi_send_data_single(spi, 0xf7);
	if (err) {
		return err;
	}

	err = epaper_27inch_spi_send_command(spi, COMMAND_POWER_OFF);
	if (err) {
		return err;
	}

	err = epaper_27inch_spi_send_command(spi, COMMAND_DEEP_SLEEP);
	if (err) {
		return err;
	}

	err = epaper_27inch_spi_send_data_single(spi, DEEP_SLEEP_MAGIC);
	if (err) {
		return err;
	}

	return 0;
}

static void epaper_27inch_reset(struct spi_device *spi) {
	int reset_delay;
	struct epaper_27inch *epaper;

	dev_warn(&spi->dev, "Entering epaper_27inch_reset\n");

	epaper = spi_get_drvdata(spi);

	/* This is the value used in the reference implementation */
	reset_delay = 200;

	gpio_set_value(epaper->gpio_rst_n, 1);
	msleep(reset_delay);
	gpio_set_value(epaper->gpio_rst_n, 0);
	msleep(reset_delay);
	gpio_set_value(epaper->gpio_rst_n, 1);
	msleep(reset_delay);
}

static int epaper_27inch_poweron(struct spi_device *spi) {
	struct epaper_27inch *epaper;
	epaper = spi_get_drvdata(spi);

	dev_warn(&spi->dev, "Entering epaper_27inch_poweron\n");

	epaper_27inch_spi_send_command(spi, COMMAND_POWER_ON);
	epaper_27inch_wait_until_idle(spi);

	return 0;
}

static int epaper_27inch_configure_power(struct spi_device *spi) {
	struct epaper_27inch *epaper;
	int err;

	dev_warn(&spi->dev, "Entering epaper_27inch_configure_power\n");

	epaper = spi_get_drvdata(spi);

	/* This sequence of black magic incantations is taken from the reference
	implementation, which implements the flow chart found in section 8-2 of the
	data sheet */

	err = epaper_27inch_spi_send_command(spi, COMMAND_POWER_SETTING);
	if (err) {
		return err;
	}

	/* VDS_EN, VDG_EN */
    err = epaper_27inch_spi_send_data_single(spi, 0x03);
	if (err) {
		return err;
	}

    /* VCOM_HV, VGHL_LV[1], VGHL_LV[0] */
    err = epaper_27inch_spi_send_data_single(spi, 0x00);
	if (err) {
		return err;
	}

    /* VDH */
    err = epaper_27inch_spi_send_data_single(spi, 0x2b);
	if (err) {
		return err;
	}

    /* VDL */
    err = epaper_27inch_spi_send_data_single(spi, 0x2b);
	if (err) {
		return err;
	}

    /* VDHR */
    err = epaper_27inch_spi_send_data_single(spi, 0x09);
	if (err) {
		return err;
	}

    err = epaper_27inch_spi_send_command(spi, COMMAND_BOOSTER_SOFT_START);
	if (err) {
		return err;
	}

    err = epaper_27inch_spi_send_data_single(spi, 0x07);
	if (err) {
		return err;
	}

    err = epaper_27inch_spi_send_data_single(spi, 0x07);
	if (err) {
		return err;
	}

    err = epaper_27inch_spi_send_data_single(spi, 0x17);
	if (err) {
		return err;
	}

    /* Power optimization */
    err = epaper_27inch_spi_send_command(spi, 0xF8);
	if (err) {
		return err;
	}

    err = epaper_27inch_spi_send_data_single(spi, 0x60);
	if (err) {
		return err;
	}

    err = epaper_27inch_spi_send_data_single(spi, 0xA5);
	if (err) {
		return err;
	}

    /* Power optimization */
    err = epaper_27inch_spi_send_command(spi, 0xF8);
	if (err) {
		return err;
	}

    err = epaper_27inch_spi_send_data_single(spi, 0x89);
	if (err) {
		return err;
	}

    err = epaper_27inch_spi_send_data_single(spi, 0xA5);
	if (err) {
		return err;
	}

    /* Power optimization */
    err = epaper_27inch_spi_send_command(spi, 0xF8);
	if (err) {
		return err;
	}

    err = epaper_27inch_spi_send_data_single(spi, 0x90);
	if (err) {
		return err;
	}

    err = epaper_27inch_spi_send_data_single(spi, 0x00);
	if (err) {
		return err;
	}

    /* Power optimization */
    err = epaper_27inch_spi_send_command(spi, 0xF8);
	if (err) {
		return err;
	}

    err = epaper_27inch_spi_send_data_single(spi, 0x93);
	if (err) {
		return err;
	}

    err = epaper_27inch_spi_send_data_single(spi, 0x2A);
	if (err) {
		return err;
	}

    /* Power optimization */
    err = epaper_27inch_spi_send_command(spi, 0xF8);
	if (err) {
		return err;
	}

    err = epaper_27inch_spi_send_data_single(spi, 0xA0);
	if (err) {
		return err;
	}

    err = epaper_27inch_spi_send_data_single(spi, 0xA5);
	if (err) {
		return err;
	}

    /* Power optimization */
    err = epaper_27inch_spi_send_command(spi, 0xF8);
	if (err) {
		return err;
	}

    err = epaper_27inch_spi_send_data_single(spi, 0xA1);
	if (err) {
		return err;
	}

    err = epaper_27inch_spi_send_data_single(spi, 0x00);
	if (err) {
		return err;
	}

    /* Power optimization */
    err = epaper_27inch_spi_send_command(spi, 0xF8);
	if (err) {
		return err;
	}

    err = epaper_27inch_spi_send_data_single(spi, 0x73);
	if (err) {
		return err;
	}

    err = epaper_27inch_spi_send_data_single(spi, 0x41);
	if (err) {
		return err;
	}

    err = epaper_27inch_spi_send_command(spi, COMMAND_PARTIAL_DISPLAY_REFRESH);
	if (err) {
		return err;
	}

    err = epaper_27inch_spi_send_data_single(spi, 0x00);
	if (err) {
		return err;
	}


	return 0;
}

static int epaper_27inch_configure_panel(struct spi_device *spi) {
	struct epaper_27inch *epaper;
	int err;

	dev_warn(&spi->dev, "Entering epaper_27inch_configure_panel\n");

	epaper = spi_get_drvdata(spi);

	err = epaper_27inch_spi_send_command(spi, COMMAND_PANEL_SETTING);
	if (err) {
		return err;
	}

	/* # KW-BF   KWR-AF    BWROTP 0f */
	err = epaper_27inch_spi_send_data_single(spi, 0xAF);
	if (err) {
		return err;
	}

	err = epaper_27inch_spi_send_command(spi, COMMAND_PLL_CONTROL);
	if (err) {
		return err;
	}

	/* 3A 100HZ   29 150Hz 39 200HZ    31 171HZ */
	err = epaper_27inch_spi_send_data_single(spi, 0x3A);
	if (err) {
		return err;
	}

	err = epaper_27inch_spi_send_command(spi, COMMAND_VCM_DC_SETTING_REGISTER);
	if (err) {
		return err;
	}

	err = epaper_27inch_spi_send_data_single(spi, 0x12);
	if (err) {
		return err;
	}

	return 0;
}

static int epaper_27inch_set_lut(struct spi_device *spi) {
	struct epaper_27inch *epaper;
	int err;

	dev_warn(&spi->dev, "Entering epaper_27inch_set_lut\n");

	epaper = spi_get_drvdata(spi);

	err = epaper_27inch_spi_send_command(spi, COMMAND_LUT_FOR_VCOM);
	if (err) {
		return err;
	}

    err = epaper_27inch_spi_send_data(spi, lut_vcom_dc, 44);
    if (err) {
		return err;
	}

    err = epaper_27inch_spi_send_command(spi, COMMAND_LUT_WHITE_TO_WHITE);
    if (err) {
		return err;
	}

    err = epaper_27inch_spi_send_data(spi, lut_ww, 42);
    if (err) {
		return err;
	}

    err = epaper_27inch_spi_send_command(spi, COMMAND_LUT_BLACK_TO_WHITE);
	if (err) {
		return err;
	}

    err = epaper_27inch_spi_send_data(spi, lut_bw, 42);
    if (err) {
		return err;
	}

    err = epaper_27inch_spi_send_command(spi, COMMAND_LUT_WHITE_TO_BLACK);
    if (err) {
		return err;
	}

    err = epaper_27inch_spi_send_data(spi, lut_wb, 42);
    if (err) {
		return err;
	}

    err = epaper_27inch_spi_send_command(spi, COMMAND_LUT_BLACK_TO_BLACK);
    if (err) {
		return err;
	}

    err = epaper_27inch_spi_send_data(spi, lut_bb, 42);
    if (err) {
		return err;
	}

	return 0;
}

static int epaper_27inch_fb_probe(struct spi_device *spi);
static int epaper_27inch_fb_remove(struct spi_device *spi);

static int epaper_27inch_spi_probe(struct spi_device *spi) {
	struct epaper_27inch *epaper;
	int err;
	struct device_node *of_node = spi->dev.of_node;

	dev_warn(&spi->dev, "Entering epaper_spi_probe\n");

	epaper = devm_kzalloc(&spi->dev, sizeof(*epaper), GFP_KERNEL);
	if (!epaper) {
		err = -ENOMEM;
		goto exit_err;
	}

	epaper->gpio_rst_n = of_get_named_gpio(of_node, "gpio-rst", 0);
	if (!gpio_is_valid(epaper->gpio_rst_n)) {
		dev_err(&spi->dev, "Unable to parse reset GPIO %d\n", epaper->gpio_rst_n);
		err = epaper->gpio_rst_n;
		goto exit_err;
	}

	err = devm_gpio_request_one(&spi->dev, epaper->gpio_rst_n, GPIOF_OUT_INIT_LOW, "rst_n");
	if (err) {
		dev_err(&spi->dev, "Unable to request reset GPIO %d\n", epaper->gpio_rst_n);
		goto exit_err;
	}

	epaper->gpio_busy = of_get_named_gpio(of_node, "gpio-busy", 0);
	if (!gpio_is_valid(epaper->gpio_busy)) {
		dev_err(&spi->dev, "Unable to parse busy GPIO %d\n", epaper->gpio_busy);
		err = epaper->gpio_busy;
		goto exit_err;
	}

	err = devm_gpio_request_one(&spi->dev, epaper->gpio_busy, GPIOF_IN, "busy");
	if (err) {
		dev_err(&spi->dev, "Unable to request budy GPIO %d\n", epaper->gpio_busy);
		goto exit_err;
	}

	epaper->gpio_dc = of_get_named_gpio(of_node, "gpio-dc", 0);
	if (!gpio_is_valid(epaper->gpio_dc)) {
		dev_err(&spi->dev, "Unable to parse DC GPIO %d\n", epaper->gpio_dc);
		err = epaper->gpio_dc;
		goto exit_err;
	}

	err = devm_gpio_request_one(&spi->dev, epaper->gpio_dc, GPIOF_OUT_INIT_LOW, "dc");
	if (err) {
		dev_err(&spi->dev, "Unable to request DC GPIO %d\n", epaper->gpio_dc);
		goto exit_err;
	}

	spi_set_drvdata(spi, epaper);

	dev_warn(&spi->dev, "GPIOs OK. Initializating device.\n");

	epaper_27inch_reset(spi);

	err = epaper_27inch_configure_power(spi);
	if (err) {
		dev_err(&spi->dev, "Error in epaper_27inch_configure_power: %d\n", err);
		goto exit_err;
	}

	err = epaper_27inch_poweron(spi);
	if (err) {
		dev_err(&spi->dev, "Error in epaper_27inch_poweron: %d\n", err);
		goto exit_err;
	}

	err = epaper_27inch_configure_panel(spi);
	if (err) {
		dev_err(&spi->dev, "Error in epaper_27inch_configure_panel: %d\n", err);
		goto exit_err;
	}

	err = epaper_27inch_set_lut(spi);
	if (err) {
		dev_err(&spi->dev, "Error in epaper_27inch_set_lut: %d\n", err);
		goto exit_err;
	}

	err = epaper_27inch_clear(spi);
	if (err) {
		dev_err(&spi->dev, "Error in epaper_27inch_clear: %d\n", err);
		goto exit_err;
	}

	dev_warn(&spi->dev, "SPI device initialized!\n");

	err = epaper_27inch_fb_probe(spi);
	if (err) {
		dev_err(&spi->dev, "Error in epaper_27inch_fb_probe: %d\n", err);
		goto exit_err;
	}

	return 0;

exit_err:
	return err;
}

static int epaper_27inch_spi_remove(struct spi_device *spi) {
	struct epaper_27inch *epaper;

	epaper = spi_get_drvdata(spi);

	if (!epaper) {
		dev_warn(&spi->dev, "epaper_27inch_spi_remove: epaper was null?\n");
		return 0;
	}

	epaper_27inch_sleep(spi);

	if (gpio_is_valid(epaper->gpio_dc)) {
		gpio_free(epaper->gpio_dc);
	}

	if (gpio_is_valid(epaper->gpio_rst_n)) {
		gpio_free(epaper->gpio_rst_n);
	}

	if (gpio_is_valid(epaper->gpio_busy)) {
		gpio_free(epaper->gpio_busy);
	}

	epaper_27inch_fb_remove(spi);

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

/* Framebuffer driver. Most of this implementation is lifted from hecubafb.c, which is
a framebuffer driver for a different class of ePaper displays that have a similar API. */

struct epaper_27inch_par {
	struct spi_device *spi;
	struct fb_info *info;
	unsigned char *shadow_video_memory;
	bool is_updating;
	struct mutex is_updating_lock;
};

static const struct fb_fix_screeninfo epaper_27inch_fix_screeninfo = {
	.id = "2.7 inch EPD",
	.visual = FB_VISUAL_MONO10,
	.type = FB_TYPE_PACKED_PIXELS,
	.line_length = EPD_WIDTH / 8,
	.xpanstep =	0,
	.ypanstep =	0,
	.ywrapstep = 0,
	.accel =	FB_ACCEL_NONE
};

static const struct fb_var_screeninfo epaper_27inch_var_screeninfo = {
	.xres		= EPD_WIDTH,
	.yres		= EPD_HEIGHT,
	.xres_virtual	= EPD_WIDTH,
	.yres_virtual	= EPD_HEIGHT,
	.bits_per_pixel	= 1,
	.nonstd		= 1
};

static void epaper_27inch_dpy_update(struct epaper_27inch_par *par)
{
	int i;
	int err;
	struct spi_device *spi;
	unsigned char *buf;
	unsigned char *old_frame;
	bool need_update;

	spi = par->spi;

	buf = (unsigned char __force *)par->info->screen_base;
	old_frame = (unsigned char __force *)par->shadow_video_memory;

	need_update = false;
	for (i = 0; i < EPD_NUM_PIXELS / 8; ++i) {
		if (buf[i] != old_frame[i]) {
			need_update = true;
			break;
		}
	}

	if (!need_update) {
		dev_warn(&spi->dev, "Skipping updating ePaper display--no update.\n");
		goto exit_unlock;
	}

	dev_warn(&spi->dev, "Updating ePaper display\n");

	/* "before" data -- TODO: use actual diffing */
	err = epaper_27inch_spi_send_command(spi, COMMAND_DATA_START_TRANSMISSION_1);
	if (err) {
		goto exit_err;
	}

	err = epaper_27inch_spi_send_data(spi, old_frame, EPD_NUM_PIXELS / 8);
	if (err) {
		goto exit_err;
	}

	/* "after" data */
	err = epaper_27inch_spi_send_command(spi, COMMAND_DATA_START_TRANSMISSION_2);
	if (err) {
		goto exit_err;
	}

	err = epaper_27inch_spi_send_data(spi, buf, EPD_NUM_PIXELS / 8);
	if (err) {
		goto exit_err;
	}

	err = epaper_27inch_spi_send_command(spi, COMMAND_DISPLAY_REFRESH);
	if (err) {
		goto exit_err;
	}

	memcpy(old_frame, buf, EPD_NUM_PIXELS / 8);

	epaper_27inch_wait_until_idle(spi);

	goto exit_unlock;

exit_err:
	dev_err(&spi->dev, "Failed to update display. Error: %d\n", err);

exit_unlock:
	mutex_lock(&par->is_updating_lock);
	par->is_updating = false;
	mutex_unlock(&par->is_updating_lock);

}

/* this is called back from the deferred io workqueue */
static void epaper_27inch_dpy_deferred_io(struct fb_info *info, struct list_head *pagelist)
{
	struct spi_device *spi;
	struct epaper_27inch_par *par = info->par;
	spi = par->spi;

	dev_warn(&spi->dev, "epaper_27inch_dpy_deferred_io\n");
	mutex_lock(&par->is_updating_lock);
	par->is_updating = true;
	mutex_unlock(&par->is_updating_lock);

	epaper_27inch_dpy_update(par);
}

static void epaper_27inch_update_delayed(struct fb_info *info) {
	struct epaper_27inch_par *par = info->par;

	mutex_lock(&par->is_updating_lock);
	if (!par->is_updating) {
		par->is_updating = true;
		schedule_delayed_work(&info->deferred_work, 3 * HZ);
	}
	mutex_unlock(&par->is_updating_lock);
}

static void epaper_27inch_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	struct spi_device *spi;
	struct epaper_27inch_par *par = info->par;
	spi = par->spi;

	dev_warn(&spi->dev, "epaper_27inch_fillrect\n");
	sys_fillrect(info, rect);
	
	epaper_27inch_update_delayed(info);
}

static void epaper_27inch_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
	struct spi_device *spi;
	struct epaper_27inch_par *par = info->par;
	spi = par->spi;

	dev_warn(&spi->dev, "epaper_27inch_copyarea\n");

	sys_copyarea(info, area);
	epaper_27inch_update_delayed(info);
}

static void epaper_27inch_imageblit(struct fb_info *info, const struct fb_image *image)
{
	struct spi_device *spi;
	struct epaper_27inch_par *par = info->par;
	int i;
	u32 start_offset;
	u32 cursor;
	u32 line_cursor;
	u32 image_line_cursor;
	u32 num_end_partial_bits;
	u8 end_partial_bit_mask;
	u32 offset_for_end_partial_bits;
	u32 num_begin_partial_bits;	
	u8 begin_partial_bit_mask;
	u8 *frame_buffer;
	spi = par->spi;

	dev_warn(&spi->dev, "epaper_27inch_imageblit\n");

	printk(KERN_INFO "dx: %d, dy: %d, width: %d, height: %d, fg_color: %x, bg_color: %x, depth: %d",
		image->dx, image->dy, image->width, image->height, image->fg_color, image->bg_color, image->depth);

	if (image->width == 8 && image->height == 8) {
		printk(KERN_INFO "Image: %02x %02x %02x %02x %02x %02x %02x %02x", 
		image->data[0], image->data[1], image->data[2], image->data[3], image->data[4], image->data[5], image->data[6], image->data[7]);
	}

	
	frame_buffer = info->screen_base;
	num_begin_partial_bits = (8 - (image->dx - (image->dx % 8))) % 8;
	begin_partial_bit_mask = (1 << num_begin_partial_bits) - 1;
	
	num_end_partial_bits = (image->dx + image->width) % 8;
	end_partial_bit_mask = ((1 << 8 ) >> num_end_partial_bits);

	offset_for_end_partial_bits = 
		(image->width - num_begin_partial_bits - num_end_partial_bits) / 8 + 
		(num_begin_partial_bits ? 1 : 0);

	start_offset = ((image->dy * info->fix.line_length * 8) + image->dx) / 8;
	cursor = start_offset;

	printk(KERN_INFO "num_begin_partial_bits: %d", num_begin_partial_bits);
	printk(KERN_INFO "begin_partial_bit_mask: %02x", begin_partial_bit_mask);
	printk(KERN_INFO "num_end_partial_bits: %d", num_end_partial_bits);
	printk(KERN_INFO "end_partial_bit_mask: %02x", end_partial_bit_mask);
	printk(KERN_INFO "offset_for_end_partial_bits: %d", offset_for_end_partial_bits);
	printk(KERN_INFO "start_offset: %d", start_offset);
	
	// Draw horizontal line by horizontal line, one byte at a time
	for (i = 0; i < image->height; ++i) {
		line_cursor = cursor;

		image_line_cursor = 0;
		if (num_begin_partial_bits > 0) {
			frame_buffer[line_cursor] = 
				(frame_buffer[line_cursor] & ~begin_partial_bit_mask) |
				(~image->data[i * image->width + image_line_cursor] & begin_partial_bit_mask);
			line_cursor++;
			image_line_cursor++;
		}

		// Full bytes
		for (; image_line_cursor < offset_for_end_partial_bits; ++image_line_cursor, ++line_cursor) {
			frame_buffer[line_cursor] = ~image->data[(i * image->width) / 8 + image_line_cursor];
		}

		if (num_end_partial_bits > 0) {
			frame_buffer[line_cursor] = 
				(frame_buffer[line_cursor] & ~end_partial_bit_mask) |
				(~image->data[i * image->width + image_line_cursor] & end_partial_bit_mask);
		}

		cursor += info->fix.line_length;
	}

	epaper_27inch_update_delayed(info);
}

/*
 * this is the slow path from userspace. they can seek and write to
 * the fb. it's inefficient to do anything less than a full screen draw
 */
static ssize_t epaper_27inch_write(struct fb_info *info, const char __user *buf,
				size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	void *dst;
	int err = 0;
	unsigned long total_size;

	if (info->state != FBINFO_STATE_RUNNING)
		return -EPERM;

	total_size = info->fix.smem_len;

	if (p > total_size)
		return -EFBIG;

	if (count > total_size) {
		err = -EFBIG;
		count = total_size;
	}

	if (count + p > total_size) {
		if (!err)
			err = -ENOSPC;

		count = total_size - p;
	}

	printk(KERN_INFO "epaper_27inch_write: Writing %d bytes at offset %lu", count, p);

	dst = (void __force *) (info->screen_base + p);

	if (copy_from_user(dst, buf, count))
		err = -EFAULT;

	if  (!err)
		*ppos += count;

	epaper_27inch_update_delayed(info);

	return (err) ? err : count;
}

static struct fb_ops epaper_27inch_ops = {
	.owner		= THIS_MODULE,
	.fb_read        = fb_sys_read,
	.fb_write	= epaper_27inch_write,
	.fb_fillrect	= epaper_27inch_fillrect,
	.fb_copyarea	= epaper_27inch_copyarea,
	.fb_imageblit	= epaper_27inch_imageblit,
};

static struct fb_deferred_io epaper_27inch_defio = {
	.delay		= 3 * HZ,
	.deferred_io	= epaper_27inch_dpy_deferred_io,
};

static int epaper_27inch_fb_probe(struct spi_device *spi)
{
	struct epaper_27inch *epaper;
	struct epaper_27inch_par *par;
	struct fb_info *info;
	int err;
	int video_memory_size;
	unsigned char *video_memory;

	epaper = spi_get_drvdata(spi);

	dev_warn(&spi->dev, "In epaper_27inch_fb_probe\n");

	video_memory_size = EPD_NUM_PIXELS / 8;
	video_memory = vzalloc(video_memory_size);
	if (!video_memory) {
		dev_err(&spi->dev, "Could not allocate video memory\n");
		goto out_err;
	}

	epaper->fb_info = framebuffer_alloc(sizeof(struct epaper_27inch_par), &spi->dev);
	if (!epaper->fb_info) {
		err = -ENOMEM;
		goto out_dealloc_vmem;
	}

	info = epaper->fb_info;
	info->screen_base = (u8 __force __iomem *)video_memory;
	info->fbops = &epaper_27inch_ops;

	info->fix = epaper_27inch_fix_screeninfo;
	info->var = epaper_27inch_var_screeninfo;

	info->var.red.length = 1;
	info->var.red.offset = 0;
	info->var.green.length = 1;
	info->var.green.offset = 0;
	info->var.blue.length = 1;
	info->var.blue.offset = 0;

	info->fix.smem_start = __pa(video_memory);
	info->fix.smem_len = video_memory_size;
	info->flags = FBINFO_FLAG_DEFAULT | FBINFO_VIRTFB;
	info->fbdefio = &epaper_27inch_defio;
	fb_deferred_io_init(info);

	par = info->par;
	par->info = info;
	par->spi = spi;
	par->shadow_video_memory = vzalloc(video_memory_size);
	par->is_updating = false;
	mutex_init(&par->is_updating_lock);
	if (!par->shadow_video_memory) {
		err = -ENOMEM;
		goto out_dealloc_shadow_vmem;
	}

	memset(par->shadow_video_memory, 0xFF, video_memory_size);

	err = register_framebuffer(info);
	if (err < 0)
		goto out_dealloc;

	fb_info(info, "epaper_27inch frame buffer device, using %dK of video memory\n",
		video_memory_size >> 10);

	return 0;

out_dealloc:
	framebuffer_release(info);
out_dealloc_shadow_vmem:
	vfree(par->shadow_video_memory);
out_dealloc_vmem:
	vfree(video_memory);

out_err:
	return err;
}

static int epaper_27inch_fb_remove(struct spi_device *spi)
{
	struct epaper_27inch *epaper = spi_get_drvdata(spi);
	struct epaper_27inch_par *par;
	struct fb_info *info = epaper->fb_info;

	if (info) {
		par = info->par;
		fb_deferred_io_cleanup(info);
		unregister_framebuffer(info);
		vfree((void __force *)info->screen_base);
		vfree((void __force *)par->shadow_video_memory);
		framebuffer_release(info);
		mutex_destroy(&par->is_updating_lock);
	}

	return 0;
}

module_spi_driver(epaper_27inch_spi_driver);

MODULE_DESCRIPTION("Framebuffer driver for the Waveshare 2.7 inch ePaper display");
MODULE_AUTHOR("John Pothier <john.pothier@outlook.com>");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
