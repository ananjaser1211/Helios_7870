#include <plat/gpio-cfg.h>

#include <linux/init.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <plat/gpio-cfg.h>
#include <linux/sec_sysfs.h>


static struct device *gps_dev;
static unsigned int gps_pwr_on = 0;
static unsigned int gps_reset = 0;


static int __init gps_s5n6420_init(void)
{
	int ret = 0;
	const char *gps_node = "samsung,lsi_s5n6420";

	struct device_node *root_node = NULL;

	gps_dev = sec_device_create(NULL, "gps");
	BUG_ON(!gps_dev);

	root_node = of_find_compatible_node(NULL, NULL, gps_node);

	if (!root_node) {
		WARN(1, "failed to get device node of ske\n");
		ret = -ENODEV;
		goto err_sec_device_create;
	}

	gps_pwr_on = of_get_gpio(root_node, 0);
	if (!gpio_is_valid(gps_pwr_on)) {
		WARN(1, "----Invalied gpio pin : %d\n", gps_pwr_on);
		ret = -ENODEV;
		goto err_sec_device_create;
	}
	gps_reset = of_get_gpio(root_node, 1);
	if (!gpio_is_valid(gps_reset)) {
		WARN(1, "-----Invalied gpio pin : %d\n", gps_reset);
		ret = -ENODEV;
		goto err_sec_device_create;
	}

	if (gpio_request(gps_pwr_on, "GPS_PWR_EN")) {
		WARN(1, "fail to request gpio(GPS_PWR_EN)\n");
		ret = -ENODEV;
		goto err_sec_device_create;
	}
	if (gpio_request(gps_reset, "GPS_RESET")) {
		WARN(1, "fail to request gpio(GPS_RESET)\n");
		ret = -ENODEV;
		goto err_sec_device_create;
	}

	gpio_direction_output(gps_pwr_on, 0);
	gpio_export(gps_pwr_on, 1);
	gpio_export_link(gps_dev, "GPS_PWR_EN", gps_pwr_on);

	gpio_direction_output(gps_reset, 1);
	gpio_export(gps_reset, 1);
	gpio_export_link(gps_dev, "GPS_RESET", gps_reset);

	return 0;

err_sec_device_create:
    WARN(1, "err_sec_device_create");
	sec_device_destroy(gps_dev->devt);
	return ret;

}

device_initcall(gps_s5n6420_init);
