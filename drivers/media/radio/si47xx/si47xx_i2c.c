/* drivers/misc/fm_si47xx/si47xx_i2c.c
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/miscdevice.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/wait.h>
#include <linux/stat.h>
#include <linux/ioctl.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/i2c/si47xx_common.h>

#include "si47xx_dev.h"
#include "si47xx_ioctl.h"

static int si47xx_open(struct inode *inode, struct file *filp)
{
	pr_debug("%s()\n", __func__);
	return nonseekable_open(inode, filp);
}

static int si47xx_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static long si47xx_ioctl(struct file *filp, unsigned int ioctl_cmd,
							unsigned long arg)
{
	long ret = 0;
	void __user *argp = (void __user *)arg;

	if ((_IOC_TYPE(ioctl_cmd) != SI47XX_IOC_MAGIC)
		|| (_IOC_NR(ioctl_cmd) > SI47XX_IOC_NR_MAX)) {
		pr_err("%s(): Inappropriate ioctl 0x%x\n",
			__func__, ioctl_cmd);
		return -ENOTTY;
	}

	pr_debug("%s(): cmd=0x%x\n", __func__, ioctl_cmd);

	switch (ioctl_cmd) {
	case SI47XX_IOC_POWERUP:
		ret = (long)si47xx_dev_powerup();
		if (ret < 0)
			pr_err("%s(): SI47XX_IOC_POWERUP failed\n", __func__);
		break;

	case SI47XX_IOC_POWERDOWN:
		ret = (long)si47xx_dev_powerdown();
		if (ret < 0)
			pr_err("%s(): SI47XX_IOC_POWERDOWN failed\n", __func__);
		break;

	case SI47XX_IOC_BAND_SET:
		{
			int band;

			if (copy_from_user((void *)&band, argp, sizeof(int)))
				ret = -EFAULT;
			else {
				ret = (long)si47xx_dev_band_set(band);
				if (ret < 0)
					pr_err("%s(): SI47XX_IOC_BAND_SET failed\n",
					__func__);
			}
		}
		break;

	case SI47XX_IOC_CHAN_SPACING_SET:
		{
			int ch_spacing;

			if (copy_from_user
			    ((void *)&ch_spacing, argp, sizeof(int)))
				ret = -EFAULT;
			else {
			ret = (long)si47xx_dev_ch_spacing_set(ch_spacing);
				if (ret < 0)
					pr_err("%s(): SI47XX_IOC_CHAN_SPACING_SET failed\n",
					__func__);
			}
		}
		break;

	case SI47XX_IOC_CHAN_SELECT:
		{
			u32 frequency;

			if (copy_from_user
			    ((void *)&frequency, argp, sizeof(u32)))
				ret = -EFAULT;
			else {
				ret = (long)si47xx_dev_chan_select(frequency);
				if (ret < 0)
					pr_err("%s(): SI47XX_IOC_CHAN_SELECT failed\n",
					__func__);
			}
		}
		break;

	case SI47XX_IOC_CHAN_GET:
		{
			u32 frequency = 0;

			ret = (long)si47xx_dev_chan_get(&frequency);
			if (ret < 0)
				pr_err("%s(): SI47XX_IOC_CHAN_GET failed\n",
				__func__);
			else if (copy_to_user
				 (argp, (void *)&frequency, sizeof(u32)))
				ret = -EFAULT;
		}
		break;

	case SI47XX_IOC_CHAN_CHECK_VALID:
		{
			bool valid = 0;

			ret = (long)si47xx_dev_chan_check_valid(&valid);
			if (ret < 0)
				pr_err("%s(): SI47XX_IOC_CHAN_GET failed\n",
				__func__);
			else if (copy_to_user
				 (argp, (void *)&valid, sizeof(bool)))
				ret = -EFAULT;
		}
		break;

	case SI47XX_IOC_SEEK_FULL:
		{
			u32 frequency = 0;

			ret = (long)si47xx_dev_seek_full(&frequency);
			if (ret < 0)
				pr_err("%s(): SI47XX_IOC_SEEK_FULL failed\n",
				__func__);
			else if (copy_to_user
				 (argp, (void *)&frequency, sizeof(u32)))
				ret = -EFAULT;
		}
		break;

	case SI47XX_IOC_SEEK_UP:
		{
			u32 frequency = 0;

			ret = (long)si47xx_dev_seek_up(&frequency);
			if (ret < 0)
				pr_err("%s(): SI47XX_IOC_SEEK_UP failed\n",
				__func__);
			else if (copy_to_user
				 (argp, (void *)&frequency, sizeof(u32)))
				ret = -EFAULT;
		}
		break;

	case SI47XX_IOC_SEEK_DOWN:
		{
			u32 frequency = 0;

			ret = (long)si47xx_dev_seek_down(&frequency);
			if (ret < 0)
				pr_err("%s(): SI47XX_IOC_SEEK_DOWN failed\n",
				__func__);
			else if (copy_to_user
				 (argp, (void *)&frequency, sizeof(u32)))
				ret = -EFAULT;
		}
		break;

	case SI47XX_IOC_RSSI_SEEK_TH_SET:
		{
			u8 RSSI_seek_th;

			if (copy_from_user
			    ((void *)&RSSI_seek_th, argp, sizeof(u8)))
				ret = -EFAULT;
			else {
			ret = (long)si47xx_dev_RSSI_seek_th_set(RSSI_seek_th);
				if (ret < 0)
					pr_err("%s(): SI47XX_IOC_RSSI_SEEK_TH_SET failed\n",
					__func__);
			}
		}
		break;

	case SI47XX_IOC_SEEK_SNR_SET:
		{
			u8 seek_SNR_th;

			if (copy_from_user
			    ((void *)&seek_SNR_th, argp, sizeof(u8)))
				ret = -EFAULT;
			else {
			ret = (long)si47xx_dev_seek_SNR_th_set(seek_SNR_th);
				if (ret < 0)
					pr_err("%s(): SI47XX_IOC_SEEK_SNR_SET failed\n",
					__func__);
			}
		}
		break;

	case SI47XX_IOC_SEEK_CNT_SET:
		{
			u8 seek_FM_ID_th;

			if (copy_from_user
			    ((void *)&seek_FM_ID_th, argp, sizeof(u8)))
				ret = -EFAULT;
			else {
				ret =
			(long)si47xx_dev_seek_FM_ID_th_set(seek_FM_ID_th);
				if (ret < 0)
					pr_err("%s(): SI47XX_IOC_SEEK_CNT_SET failed\n",
					__func__);
			}
		}
		break;

	case SI47XX_IOC_CUR_RSSI_GET:
		{
			struct rssi_snr_t data;

			ret = (long)si47xx_dev_cur_RSSI_get(&data);
			if (ret < 0)
				pr_err("%s(): SI47XX_IOC_CUR_RSSI_GET failed\n",
				__func__);
			else if (copy_to_user(argp, (void *)&data,
					      sizeof(data)))
				ret = -EFAULT;

			pr_debug("%s(): curr_rssi:%d\ncurr_rssi_th:%d\ncurr_snr:%d\n",
				__func__,
			      data.curr_rssi,
			      data.curr_rssi_th,
			      data.curr_snr);
		}
		break;

	case SI47XX_IOC_VOLUME_SET:
		{
			u8 volume;

			if (copy_from_user((void *)&volume, argp, sizeof(u8)))
				ret = -EFAULT;
			else {
				ret = (long)si47xx_dev_volume_set(volume);
				if (ret < 0)
					pr_err("%s(): SI47XX_IOC_VOLUME_SET failed\n",
					__func__);
			}
		}
		break;

	case SI47XX_IOC_VOLUME_GET:
		{
			u8 volume;

			ret = (long)si47xx_dev_volume_get(&volume);
			if (ret < 0)
				pr_err("%s(): SI47XX_IOC_VOLUME_GET failed\n",
				__func__);
			else if (copy_to_user
				 (argp, (void *)&volume, sizeof(u8)))
				ret = -EFAULT;
		}
		break;

	case SI47XX_IOC_DSMUTE_ON:
		ret = (long)si47xx_dev_DSMUTE_ON();
		if (ret < 0)
			pr_err("%s(): SI47XX_IOC_DSMUTE_ON failed\n", __func__);
		break;

	case SI47XX_IOC_DSMUTE_OFF:
		ret = (long)si47xx_dev_DSMUTE_OFF();
		if (ret < 0)
			pr_err("%s(): SI47XX_IOC_DSMUTE_OFF failed\n",
			__func__);
		break;

	case SI47XX_IOC_MUTE_ON:
		ret = (long)si47xx_dev_MUTE_ON();
		if (ret < 0)
			pr_err("%s(): SI47XX_IOC_MUTE_ON failed\n", __func__);
		break;

	case SI47XX_IOC_MUTE_OFF:
		ret = (long)si47xx_dev_MUTE_OFF();
		if (ret < 0)
			pr_err("%s(): SI47XX_IOC_MUTE_OFF failed\n", __func__);
		break;

	case SI47XX_IOC_MONO_SET:
		ret = (long)si47xx_dev_MONO_SET();
		if (ret < 0)
			pr_err("%s(): SI47XX_IOC_MONO_SET failed\n", __func__);
		break;

	case SI47XX_IOC_STEREO_SET:
		ret = (long)si47xx_dev_STEREO_SET();
		if (ret < 0)
			pr_err("%s(): SI47XX_IOC_STEREO_SET failed\n",
			__func__);
		break;

	case SI47XX_IOC_RSTATE_GET:
		{
			struct dev_state_t dev_state;

			ret = (long)si47xx_dev_rstate_get(&dev_state);
			if (ret < 0)
				pr_err("%s(): SI47XX_IOC_RSTATE_GET failed\n",
				__func__);
			else if (copy_to_user(argp, (void *)&dev_state,
					      sizeof(dev_state)))
				ret = -EFAULT;
		}
		break;

	case SI47XX_IOC_RDS_DATA_GET:
		{
			struct radio_data_t data;

			ret = (long)si47xx_dev_RDS_data_get(&data);
			if (ret < 0)
				pr_err("%s(): SI47XX_IOC_RDS_DATA_GET failed\n",
				__func__);
			else if (copy_to_user(argp, (void *)&data,
					      sizeof(data)))
				ret = -EFAULT;
		}
		break;

	case SI47XX_IOC_RDS_ENABLE:
		ret = (long)si47xx_dev_RDS_ENABLE();
		if (ret < 0)
			pr_err("%s(): SI47XX_IOC_RDS_ENABLE failed\n",
			__func__);
		break;

	case SI47XX_IOC_RDS_DISABLE:
		ret = (long)si47xx_dev_RDS_DISABLE();
		if (ret < 0)
			pr_err("%s(): SI47XX_IOC_RDS_DISABLE failed\n",
			__func__);
		break;

	case SI47XX_IOC_RDS_TIMEOUT_SET:
		{
			u32 time_out;

			if (copy_from_user
			    ((void *)&time_out, argp, sizeof(u32)))
				ret = -EFAULT;
			else {
			ret = (long)si47xx_dev_RDS_timeout_set(time_out);
				if (ret < 0)
					pr_err("%s(): SI47XX_IOC_RDS_TIMEOUT_SET failed\n",
					__func__);
			}
		}
		break;

	case SI47XX_IOC_SEEK_CANCEL:
		if (si47xx_dev_wait_flag == SEEK_WAITING) {
			si47xx_dev_wait_flag = SEEK_CANCEL;
			wake_up_interruptible(&si47xx_waitq);
		}
		break;

	case SI47XX_IOC_CHIP_ID_GET:
		{
			struct chip_id chp_id;

			ret = (long)si47xx_dev_chip_id(&chp_id);
			if (ret < 0)
				pr_err("%s(): SI47XX_IOC_CHIP_ID_GET failed\n",
				__func__);
			else if (copy_to_user(argp, (void *)&chp_id,
					      sizeof(chp_id)))
				ret = -EFAULT;
		}
		break;

	case SI47XX_IOC_DEVICE_ID_GET:
		{
			struct device_id dev_id;

			ret = (long)si47xx_dev_device_id(&dev_id);
			if (ret < 0)
				pr_err("%s(): SI47XX_IOC_DEVICE_ID_GET failed\n",
				__func__);
			else if (copy_to_user(argp, (void *)&dev_id,
					      sizeof(dev_id)))
				ret = -EFAULT;
		}
		break;

	case SI47XX_IOC_SYS_CONFIG2_GET:
		{
			struct sys_config2 sys_conf2;

			ret = (long)si47xx_dev_sys_config2(&sys_conf2);
			if (ret < 0)
				pr_err("%s(): SI47XX_IOC_SYS_CONFIG2_GET failed\n",
				__func__);
			else if (copy_to_user(argp, (void *)&sys_conf2,
					      sizeof(sys_conf2)))
				ret = -EFAULT;
		}
		break;

	case SI47XX_IOC_SYS_CONFIG3_GET:
		{
			struct sys_config3 sys_conf3;

			ret = (long)si47xx_dev_sys_config3(&sys_conf3);
			if (ret < 0)
				pr_err("%s(): SI47XX_IOC_SYS_CONFIG3_GET failed\n",
				__func__);
			else if (copy_to_user(argp, (void *)&sys_conf3,
					      sizeof(sys_conf3)))
				ret = -EFAULT;
		}
		break;

	case SI47XX_IOC_POWER_CONFIG_GET:
		ret = -EFAULT;
		break;

	case SI47XX_IOC_AFCRL_GET:
		{
			u8 afc;

			ret = (long)si47xx_dev_AFCRL_get(&afc);
			if (ret < 0)
				pr_err("%s(): SI47XX_IOC_AFCRL_GET failed\n",
				__func__);
			else if (copy_to_user(argp, (void *)&afc, sizeof(u8)))
				ret = -EFAULT;
		}
		break;

	case SI47XX_IOC_DE_SET:
		{
			u8 de_tc;

			if (copy_from_user((void *)&de_tc, argp, sizeof(u8)))
				ret = -EFAULT;
			else {
				ret = (long)si47xx_dev_DE_set(de_tc);
				if (ret < 0)
					pr_err("%s(): SI47XX_IOC_DE_SET failed\n",
					__func__);
			}
		}
		break;

	case SI47XX_IOC_STATUS_RSSI_GET:
		{
			struct status_rssi status;

			ret = (long)si47xx_dev_status_rssi(&status);
			if (ret < 0)
				pr_err("%s(): SI47XX_IOC_STATUS_RSSI_GET failed\n",
				__func__);
			else if (copy_to_user(argp, (void *)&status,
					      sizeof(status)))
				ret = -EFAULT;
		}
		break;

	case SI47XX_IOC_SYS_CONFIG2_SET:
		{
			struct sys_config2 sys_conf2;
			unsigned long n;

			n = copy_from_user((void *)&sys_conf2, argp,
					   sizeof(sys_conf2));
			if (n) {
				pr_err("%s(): copy_from_user failed. Failed to read [%lu] byes\n",
					__func__, n);
				ret = -EFAULT;
			} else {
			ret = (long)si47xx_dev_sys_config2_set(&sys_conf2);
				if (ret < 0)
					pr_err("%s(): SI47XX_IOC_SYS_CONFIG2_SET failed\n",
					__func__);
			}
		}
		break;

	case SI47XX_IOC_SYS_CONFIG3_SET:
		{
			struct sys_config3 sys_conf3;
			unsigned long n;

			n = copy_from_user((void *)&sys_conf3, argp,
					   sizeof(sys_conf3));
			if (n) {
				pr_err("%s(): copy_from_user failed. Failed to read [%lu] byes\n",
					__func__, n);
				ret = -EFAULT;
			} else {
			ret = (long)si47xx_dev_sys_config3_set(&sys_conf3);
				if (ret < 0)
					pr_err("%s(): SI47XX_IOC_SYS_CONFIG3_SET failed\n",
					__func__);
			}
		}
		break;

	case SI47XX_IOC_RESET_RDS_DATA:
		{
			ret = (long)si47xx_dev_reset_rds_data();
			if (ret < 0)
				pr_err("%s(): SI47XX_IOC_RESET_RDS_DATA failed\n",
				__func__);
		}
		break;

	default:
		pr_debug("%s(): default\n", __func__);
		ret = -ENOTTY;
		break;
	}

	return ret;
}

static int si47xx_set_dt_pdata(struct si47xx_platform_data *pdata,
	struct device *dev)
{
	int ret = 0;
	u32 data[SI47XX_VOLUME_NUM];
	int i;

	if (!pdata) {
		dev_err(dev, "%s : could not allocate memory for platform data\n",
			__func__);
		return -ENOMEM;
	}

	pdata->rst_gpio = of_get_named_gpio(dev->of_node, "si47xx,reset", 0);
	if (pdata->rst_gpio < 0) {
		dev_err(dev, "%s : can not find the si47xx reset in the dt\n",
			__func__);
		return -EINVAL;
	} else
		dev_info(dev, "%s : si47xx reset =%d\n", __func__,
		pdata->rst_gpio);

	pdata->int_gpio = of_get_named_gpio(dev->of_node,
		"si47xx,interrupt", 0);
	if (pdata->int_gpio < 0) {
		dev_err(dev, "%s : can not find the si47xx inturrpt in the dt\n",
			__func__);
		return -EINVAL;
	} else
		dev_info(dev, "%s : si47xx interrupt =%d\n", __func__,
		pdata->int_gpio);

	/* 0:Analog, 1:Digital(I2S) */
	ret = of_property_read_u32(dev->of_node, "si47xx,mode",
		&pdata->mode);
	if (ret) {
		pr_err("%s : can not find the si47xx mode in the dt\n",
			__func__);
		pdata->mode = 0;
	} else
		pr_info("%s : si47xx mode =%d\n", __func__,
		pdata->mode);

	if (!of_property_read_u32_array(dev->of_node, "si47xx,rx-vol", data, SI47XX_VOLUME_NUM)) {
		for (i = 0; i < SI47XX_VOLUME_NUM; i++) {
			pdata->rx_vol[i] = data[i];
			pr_info("%s: rx_vol = 0x%x\n", __func__,
				pdata->rx_vol[i]);
		}
	} else {
		pr_err("%s: can not find the si47xx volume in the dt\n", __func__);
	}

	return ret;
}

static int si47xx_gpio_init(struct si47xx_platform_data *pdata)
{
	int ret = 0;

	if (pdata->rst_gpio > 0) {
		ret = gpio_request(pdata->rst_gpio, "si47xx_reset");
		if (ret) {
			pr_err("%s : gpio_request failed for %d\n",
				__func__, pdata->rst_gpio);
			return ret;
		}
	}

	if (pdata->int_gpio > 0) {
		ret = gpio_request(pdata->int_gpio, "si47xx_interrupt");
		if (ret) {
			pr_err("%s : gpio_request failed for %d\n",
				__func__, pdata->int_gpio);
			return ret;
		}
	}

	return ret;
}

static struct si47xx_platform_data si47xx_pdata = {
	.rx_vol = {
		0x0,
		0x10,
		0x12,
		0x14,
		0x16,
		0x18,
		0x1B,
		0x1E,
		0x21,
		0x24,
		0x27,
		0x2A,
		0x2D,
		0x30,
		0x33,
		0x36
	},
};

static int si47xx_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct si47xx_device_t *si47xx;
	struct si47xx_platform_data *pdata;
	int ret;

	dev_info(&client->dev, "Register si47xx FM driver\n");

	pdata = devm_kzalloc(&client->dev,
		sizeof(struct si47xx_platform_data), GFP_KERNEL);

	if (!pdata) {
		dev_err(&client->dev, "Failed to allocate memory\n");
		return -ENOMEM;
	}

	pdata = &si47xx_pdata;

	ret = si47xx_set_dt_pdata(pdata, &client->dev);
	if (ret < 0) {
		dev_err(&client->dev, "Failed get dt data\n");
		goto err;
	}

	ret = si47xx_gpio_init(pdata);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to init gpio\n");
		goto err;
	}

	si47xx = kzalloc(sizeof(struct si47xx_device_t), GFP_KERNEL);

	if (si47xx == NULL) {
		dev_err(&client->dev, "Failed to allocate memory\n");
		goto err;
	}

	si47xx->client = client;
	i2c_set_clientdata(client, si47xx);

	si47xx->dev = &client->dev;
	dev_set_drvdata(si47xx->dev, si47xx);

	mutex_init(&si47xx->lock);

	si47xx->pdata = pdata;

	ret = si47xx_dev_init(si47xx);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to dev init\n");
		goto err_dev_init;
	}

	return 0;

err_dev_init:
	free_irq(si47xx->pdata->si47xx_irq, NULL);
	mutex_destroy(&si47xx->lock);
	dev_set_drvdata(si47xx->dev, NULL);
	i2c_set_clientdata(client, NULL);
	kfree(si47xx);
err:
	if(si47xx->dev != NULL)
		devm_kfree(si47xx->dev, pdata);

	return ret;
}

static int si47xx_i2c_remove(struct i2c_client *client)
{
	struct si47xx_device_t *si47xx = i2c_get_clientdata(client);
	int ret = 0;

	if (si47xx->pdata->rst_gpio > 0)
		gpio_free(si47xx->pdata->rst_gpio);
	if (si47xx->pdata->int_gpio > 0)
		gpio_free(si47xx->pdata->int_gpio);

	free_irq(client->irq, NULL);
	ret = si47xx_dev_exit();
	dev_set_drvdata(si47xx->dev, NULL);
	i2c_set_clientdata(client, NULL);
	mutex_destroy(&si47xx->lock);
	kfree(si47xx);
	return ret;
}

static int si47xx_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct si47xx_device_t *si47xx = i2c_get_clientdata(client);
	int ret = 0;

	dev_dbg(&client->dev, "%s():\n", __func__);

	disable_irq(si47xx->pdata->si47xx_irq);

	return ret;
}

static int si47xx_resume(struct i2c_client *client)
{
	struct si47xx_device_t *si47xx = i2c_get_clientdata(client);
	int ret = 0;

	dev_dbg(&client->dev, "%s():\n", __func__);

	enable_irq(si47xx->pdata->si47xx_irq);

	return ret;
}

static const struct file_operations si47xx_fops = {
	.owner = THIS_MODULE,
	.open = si47xx_open,
	.unlocked_ioctl = si47xx_ioctl,
	.compat_ioctl = si47xx_ioctl,
	.release = si47xx_release,
};

static struct miscdevice si47xx_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "radio0",
	.fops = &si47xx_fops,
};

static const struct of_device_id si4705_dt_match[] = {
	{ .compatible = "si47xx,fmradio" },
	{ }
};
MODULE_DEVICE_TABLE(of, si4705_dt_match);

static const struct i2c_device_id si47xx_id[] = {
	{"si47xx", 0},
	{}
};

static struct i2c_driver si47xx_i2c_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "si47xx",
		.of_match_table = si4705_dt_match,
	},
	.id_table = si47xx_id,
	.probe = si47xx_i2c_probe,
	.remove = si47xx_i2c_remove,
	.suspend = si47xx_suspend,
	.resume = si47xx_resume,
};

static __init int si47xx_i2c_drv_init(void)
{
	int ret = 0;

	ret = misc_register(&si47xx_misc_device);
	if (ret < 0) {
		pr_err("%s(): misc_register failed\n", __func__);
		goto err_misc_register;
	}

	ret = i2c_add_driver(&si47xx_i2c_driver);
	if (ret < 0) {
		pr_err("%s(): i2c_add_driver failed", __func__);
		goto err_i2c_add_driver;
	}

	init_waitqueue_head(&si47xx_waitq);

	return 0;

err_i2c_add_driver:
	misc_deregister(&si47xx_misc_device);
err_misc_register:
	return ret;
}

void __exit si47xx_i2c_drv_exit(void)
{
	i2c_del_driver(&si47xx_i2c_driver);
	misc_deregister(&si47xx_misc_device);
}

module_init(si47xx_i2c_drv_init);
module_exit(si47xx_i2c_drv_exit);

MODULE_DESCRIPTION("si47xx FM tuner driver");
MODULE_LICENSE("GPL");
