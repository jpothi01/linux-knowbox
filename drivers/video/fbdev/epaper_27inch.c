#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/of_device.h>
#include <drivers/video/fbdev/waveshare/epaper_2_7_inch.h>
 
static int epaper_27inch_probe(struct platform_device *op)
{
	return 0;
}

static int epaper_27inch_remove(struct platform_device *op)
{
	return 0;
}

static const struct of_device_id epaper_27inch_match[] = {
	{
		.name = "waveshare,epaper-2-7-inch",
	},
	{},
};
MODULE_DEVICE_TABLE(of, epaper_27inch_match);

static struct platform_driver epaper_27inch_driver = {
	.driver = {
		.name = "epaper_27inch",
		.of_match_table = epaper_27inch_match,
	},
	.probe		= epaper_27inch_probe,
	.remove		= epaper_27inch_remove,
};

static int __init epaper_27inch_init(void)
{
	if (fb_get_options("epaper_27inch", NULL))
		return -ENODEV;

	return platform_driver_register(&epaper_27inch_driver);
}

static void __exit epaper_27inch_exit(void)
{
	platform_driver_unregister(&epaper_27inch_driver);
}

module_init(epaper_27inch_init);
module_exit(epaper_27inch_exit);

MODULE_DESCRIPTION("Framebuffer driver for the Waveshare 2.7 inch ePaper display");
MODULE_AUTHOR("John Pothier <john.pothier@outlook.com>");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
