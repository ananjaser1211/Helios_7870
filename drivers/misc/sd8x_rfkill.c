/*
 * rfkill power contorl for Marvell sd8xxx wlan/bt
 *
 * Copyright (C) 2009 Marvell, Inc.
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
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/debugfs.h>
#include <linux/device.h>

#include <linux/err.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/mmc/host.h>
#include <linux/sd8x_rfkill.h>
#include <linux/platform_data/pxa_sdhci.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/mmc/dw_mmc.h>
#include <linux/clk.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/regulator/consumer.h>
#include <linux/completion.h>

//#include "../mmc/host/dw_mmc.h"
#define SD8X_DEV_NAME "sd8x-rfkill"

/*
 * Define optimum max current to 200mA while min current to 0.
 * They will be handled in regulator_set_optimum_mode and
 * enable or disable the regulator sleep mode accordingly.
 */
#define OPTIMUM_MAX_CURRENT 200000
#define OPTIMUM_MIN_CURRENT 0

static DEFINE_MUTEX(sd8x_pwr_mutex);

static struct wakeup_source wlan_dat1_wakeup;
static struct work_struct wlan_wk;

static void wlan_edge_wakeup(struct work_struct *work)
{
	/*
	 * it is handled in SDIO driver instead, so code not need now
	 * but temparally keep the code here,it may be used for debug
	 */
#if 0
	unsigned int sec = 3;

	/*
	 * Why use a workqueue to call this function?
	 *
	 * As now dat1_edge_wakeup is called just after CPU exist LPM,
	 * and if __pm_wakeup_event is called before syscore_resume,
	 * WARN_ON(timekeeping_suspended) will happen in ktime_get in
	 * /kernel/time/timekeeping.c
	 *
	 * So we create a workqueue to fix this issue
	 */
	__pm_wakeup_event(&wlan_dat1_wakeup, 1000 * sec);
	pr_info("SDIO pade edge wake up+++\n");
#endif
}

static void wlan_wakeup_init(void)
{
	 INIT_WORK(&wlan_wk, wlan_edge_wakeup);
	 wakeup_source_init(&wlan_dat1_wakeup,
		"wifi_hs_wakeups");
}

static void sd8x_rfkill_platform_data_init(
	struct sd8x_rfkill_platform_data *pdata)
{
	/* all intems are invalid just after alloc */
	pdata->gpio_power_down = -1;
	pdata->gpio_reset = -1;
	pdata->gpio_edge_wakeup = -1;
	pdata->gpio_3v3_en = -1;
	pdata->gpio_1v8_en = -1;

	pdata->wib_3v3 = NULL;
	pdata->wib_1v8 = NULL;
	pdata->wib_sdio_1v8 = NULL;

	/*for issue mmc card_detection interrupt */
	pdata->mmc = NULL;

	/* power status, unknown status at first */
	pdata->is_on = -1;
}

static struct sd8x_rfkill_platform_data
	*sd8x_rfkill_platform_data_alloc(struct platform_device *pdev)
{
	struct sd8x_rfkill_platform_data *pdata;

	/* create a new one and init it */
	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (pdata) {
		sd8x_rfkill_platform_data_init(pdata);
		return pdata;
	}

	return NULL;
}

static int sd8x_regulator_set_low_current(struct regulator *wib_ldo, int enable)
{
	int uA_load;
	int mode;

	uA_load = enable ? OPTIMUM_MIN_CURRENT : OPTIMUM_MAX_CURRENT;
	mode = enable ? REGULATOR_MODE_IDLE : REGULATOR_MODE_NORMAL;

	if (regulator_set_optimum_mode(wib_ldo, uA_load) < 0) {
#if 0 //mrvl       
		if (regulator_set_suspend_mode(wib_ldo, mode) < 0)
			return -1;
#else
        

		    pr_info("%s: set optimum mode(uA_load=%d) failed\n", __func__, uA_load );
            return -1;

#endif
	}

	return 0;
}

/*
 * For SD8xxx device, there are two power: supply 1v8 and 3v3
 *
 * If 18v and 3v3 can't be both disabled during power off stage,
 * it is recommand to keep them both on and disable pd-gpio only
 * TODO: add flags to check gpio active level
 */
static int sd8x_1v8_3v3_control(
	struct sd8x_rfkill_platform_data *pdata, int on)
{
	int gpio_3v3_en = pdata->gpio_3v3_en;
	int gpio_1v8_en = pdata->gpio_1v8_en;

	if (gpio_3v3_en >= 0) {
		if (gpio_request(gpio_3v3_en, "sd8xxx 3v3 on")) {
				pr_info("gpio %d request failed\n", gpio_3v3_en);
				return -1;
		}
	}

	if (gpio_1v8_en >= 0) {
		if (gpio_request(gpio_3v3_en, "sd8xxx 1v8 on")) {
				pr_info("gpio %d request failed\n", gpio_1v8_en);
				if (gpio_3v3_en >= 0)
					gpio_free(gpio_3v3_en);
				return -1;
		}
	}

	if (on) {
		if (gpio_1v8_en >= 0)
			gpio_direction_output(gpio_1v8_en, 1);
		if (gpio_3v3_en >= 0)
			gpio_direction_output(gpio_3v3_en, 1);
		if (pdata->wib_3v3) {
			if (regulator_set_voltage(pdata->wib_3v3, 3300000, 3300000))
				pr_err("fail to set regulator wib_3v3 to 3.3v\n");
			if (sd8x_regulator_set_low_current(pdata->wib_3v3, 0) < 0)
				pr_err("fail to set optimum_mode for regulator wib_3v3\n");
			if (!regulator_is_enabled(pdata->wib_3v3) &&
					regulator_enable(pdata->wib_3v3))
				pr_err("fail to enable regulator wib_3v3\n");
		}
		if (pdata->wib_1v8) {
			if (regulator_set_voltage(pdata->wib_1v8, 1800000, 1800000))
				pr_err("fail to set regulator wib_1v8 to 1.8v\n");
			if (sd8x_regulator_set_low_current(pdata->wib_1v8, 0) < 0)
				pr_err("fail to set optimum_mode for regulator wib_1v8\n");
			if (!regulator_is_enabled(pdata->wib_1v8) &&
					regulator_enable(pdata->wib_1v8))
				pr_err("fail to enable regulator wib_1v8\n");
		}
		if (pdata->wib_sdio_1v8) {
			if (regulator_set_voltage(pdata->wib_sdio_1v8, 1800000, 1800000))
				pr_err("fail to set regulator wib_sdio_1v8 to 1.8v\n");
			if (sd8x_regulator_set_low_current(pdata->wib_sdio_1v8, 0) < 0)
				pr_err("fail to set optimum_mode for regulator wib_sdio_1v8\n");
			if (!regulator_is_enabled(pdata->wib_sdio_1v8) &&
					regulator_enable(pdata->wib_sdio_1v8))
				pr_err("fail to enable regulator wib_sdio_1v8\n");
		}
	} else {
		if ((gpio_1v8_en >= 0) && (gpio_3v3_en >= 0)) {
			gpio_direction_output(gpio_3v3_en, 0);
			gpio_direction_output(gpio_1v8_en, 0);
		}
		if (pdata->wib_3v3) {
			if (sd8x_regulator_set_low_current(pdata->wib_3v3, 1) < 0)
				pr_err("fail to set optimum_mode for regulator wib_3v3\n");
			if (regulator_is_enabled(pdata->wib_3v3) &&
					regulator_disable(pdata->wib_3v3))
				pr_err("fail to disable regulator wib_3v3\n");

		}
		if (pdata->wib_1v8) {
			if (sd8x_regulator_set_low_current(pdata->wib_1v8, 1) < 0)
				pr_err("fail to set optimum_mode for regulator wib_1v8\n");
			if (regulator_is_enabled(pdata->wib_1v8) &&
					regulator_disable(pdata->wib_1v8))
				pr_err("fail to disable regulator wib_1v8\n");
		}
		if (pdata->wib_sdio_1v8) {
			if (sd8x_regulator_set_low_current(pdata->wib_sdio_1v8, 1) < 0)
				pr_err("fail to set optimum_mode for regulator wib_sdio_1v8\n");
			if (regulator_is_enabled(pdata->wib_sdio_1v8) &&
					regulator_disable(pdata->wib_sdio_1v8))
				pr_err("fail to disable regulator wib_sdio_1v8\n");
		}
	}

	if (gpio_3v3_en >= 0)
		gpio_free(gpio_3v3_en);
	if (gpio_1v8_en >= 0)
		gpio_free(gpio_1v8_en);

	return 0;
}
/*
 * Power on/off sd8xxx by control GPIO or regulator
 * TODO: fine tune the function if need, like adding regulator
 * support
 *
 */
static int sd8x_pwr_ctrl(struct sd8x_rfkill_platform_data *pdata, int on)
{
 	int gpio_power_down = pdata->gpio_power_down;
 	int gpio_reset = pdata->gpio_reset;
 
	pr_info("%s: on=%d\n", __func__, on);

	if (gpio_power_down >= 0) {
		if (gpio_request(gpio_power_down, "sd8xxx power down")) {
			pr_info("gpio %d request failed\n", gpio_power_down);
			return -1;
		}
	}

	if (gpio_reset >= 0) {
		if (gpio_request(gpio_reset, "sd8xxx reset")) {
			pr_info("gpio %d request failed\n", gpio_reset);
			if (gpio_power_down >= 0)
				gpio_free(gpio_power_down);
			return -1;
		}
	}

	if (on) {
		sd8x_1v8_3v3_control(pdata, 1);

		if (gpio_power_down >= 0) {
			msleep(1);
			gpio_direction_output(gpio_power_down, 1);
		}

		if (gpio_reset >= 0) {
			gpio_direction_output(gpio_reset, 0);
			msleep(1);
			gpio_direction_output(gpio_reset, 1);
		}
	} else {
		if (gpio_power_down >= 0)
			gpio_direction_output(gpio_power_down, 0);

		sd8x_1v8_3v3_control(pdata, 0);
	}
    printk("### power_down=%d ####\n", gpio_get_value(gpio_power_down));
	if (gpio_power_down >= 0)
		gpio_free(gpio_power_down);
	if (gpio_reset >= 0)
		gpio_free(gpio_reset);

	return 0;
}

static int sd8x_pwr_on(struct sd8x_rfkill_platform_data *pdata)
{
	int ret = 0;
	int retry;
	unsigned long timeout_secs;
	unsigned long timeout;
	struct dw_mci_slot *slot = mmc_priv(pdata->mmc);
	struct dw_mci *host = slot->host;
    unsigned long flags;
    DECLARE_COMPLETION_ONSTACK(complete);
    
	if (pdata->is_on)
		return 0;

	if (!pdata->mmc)
		return -EINVAL;

	pr_info("sd8x set_power on start\n");

	

	if (pdata->set_power)
		pdata->set_power(1);

	/*
	 * As we known, if we want to support Card Interrupt well, SDIO Host's
	 * Main Source CLK should be free running.
	 *
	 * But if PM Runtime is enabled, the host's RPM callback may dynamic
	 * on/off the SDIO Host's SRC CL
	 *
	 * Here we call "pm_runtime_get" to make sure Source Clock will not
	 * be disabled untill pm_runtime_put is called again
	 */


 
	/* First: power up sd8x device */

	sd8x_pwr_ctrl(pdata, 1);


	/* Second: check whether sdio device can be detected */

	if (pdata->mmc) {
#if 1        
   		retry = 2;
		timeout_secs = 60;
		timeout = msecs_to_jiffies(timeout_secs * 1000);
        host->pdata->quirks |= DW_MCI_QUIRK_BROKEN_CARD_DETECTION;

		while (retry) {
         
			pdata->mmc->detect_complete = &complete;
            spin_lock_irqsave(&host->lock, flags);
            queue_work(host->card_workqueue, &host->card_work);  
            spin_unlock_irqrestore(&host->lock, flags);
   	        wait_for_completion_timeout(&complete, timeout);
	        pdata->mmc->detect_complete= NULL;
            

           
			if (pdata->mmc->card)
				break;

          
			printk(KERN_INFO "Retry mmc detection\n");
			retry--;
		}
#else
        host->pdata->quirks |= DW_MCI_QUIRK_BROKEN_CARD_DETECTION;
        spin_lock_irqsave(&host->lock, flags);
        queue_work(host->card_workqueue, &host->card_work);  
        spin_unlock_irqrestore(&host->lock, flags);
       
#endif 


        timeout = 5000; /* 5 sec */
        
        while (timeout > 0) {
                if (pdata->mmc->card)
                        break;
                msleep(100);
                timeout -= 100;
        }

		if (pdata->mmc->card) {
       
			ret = 0;
			pr_info("sucess to detect SDIO device\n");
		} else {
       
			ret = -EBUSY;
			pr_err("fail to detect SDIO device\n");
		}
       
     
	}


	/* Last: save power on status after detection */
	if (!ret) {
     
		pdata->is_on = 1;


		if (pdata->pinctrl && pdata->pin_on)
			pinctrl_select_state(pdata->pinctrl, pdata->pin_on);

	} else {
  
		pdata->is_on = 0;
        host->pdata->quirks &= ~DW_MCI_QUIRK_BROKEN_CARD_DETECTION;

		/* roll back if fail */
		sd8x_pwr_ctrl(pdata, 0);


		if (pdata->set_power)
			pdata->set_power(0);


		/* here only put_sync when open SD8x device fail */

    

		if (pdata->pinctrl && pdata->pin_off)
			pinctrl_select_state(pdata->pinctrl, pdata->pin_off);
   
	}



	pr_info("sd8x set_power on end\n");
	return ret;
}

static int sd8x_pwr_off(struct sd8x_rfkill_platform_data *pdata)
{
	int ret = 0;
	struct dw_mci_slot *slot = mmc_priv(pdata->mmc);
	struct dw_mci *host = slot->host;
    unsigned long flags;
    DECLARE_COMPLETION_ONSTACK(complete);

	if (!pdata->is_on)
		return 0;

	if (!pdata->mmc)
		return -EINVAL;

	pr_info("sd8x set_power off start\n");



	/* First: tell SDIO bus the device will be power off */
//	if (host->mmc  && host->mmc->card)
//		mmc_disable_sdio(host->mmc);

	/* Second: power off sd8x device */
	sd8x_pwr_ctrl(pdata, 0);

	if (pdata->pinctrl && pdata->pin_off)
		pinctrl_select_state(pdata->pinctrl, pdata->pin_off);

	/* TODO: check sdio device can't be detected indeed */

	if (pdata->mmc) {
        
		unsigned long timeout_secs = 1;
        unsigned long timeout =0;


		timeout = msecs_to_jiffies(timeout_secs * 1000);


        host->pdata->quirks &= ~DW_MCI_QUIRK_BROKEN_CARD_DETECTION;
#if 0        
        spin_lock_irqsave(&host->lock, flags);
        queue_work(host->card_workqueue, &host->card_work);  
        spin_unlock_irqrestore(&host->lock, flags);
#else        


         
            pdata->mmc->detect_complete = &complete;
            spin_lock_irqsave(&host->lock, flags);
            queue_work(host->card_workqueue, &host->card_work);  
            spin_unlock_irqrestore(&host->lock, flags);
            wait_for_completion_timeout(&complete, timeout);
            pdata->mmc->detect_complete= NULL;
            

  



#endif
#if 0        
        while (timeout > 0) {
                if (pdata->mmc->card)
                        break;
                msleep(100);
                timeout -= 100;
        }
#endif        
     
     

	}
   
	/* Last: update the status */


	

	if (pdata->set_power)
		pdata->set_power(0);
	pdata->is_on = 0;

 
	pr_info("sd8x set_power off end\n");
	return ret;
}


static ssize_t sd8x_pwr_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int len = 0;
	struct sd8x_rfkill_platform_data *pdata = dev->platform_data;

	mutex_lock(&sd8x_pwr_mutex);

	len = sprintf(buf, "SD8x Device is power %s\n",
		pdata->is_on ? "on" : "off");

	mutex_unlock(&sd8x_pwr_mutex);

	return (ssize_t)len;
}

static ssize_t sd8x_pwr_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int pwr_ctrl;
	int valid_ctrl = 0;
	struct sd8x_rfkill_platform_data *pdata = dev->platform_data;

	mutex_lock(&sd8x_pwr_mutex);

	if (sscanf(buf, "%d", &pwr_ctrl) == 1) {
		if ((pwr_ctrl == 1) || (pwr_ctrl == 0))
			valid_ctrl = 1;
 	}

	if (valid_ctrl != 1) {
		pr_err("Please input valid ctrl: 0: Close, 1: Open\n");
		mutex_unlock(&sd8x_pwr_mutex);
		return size;
	}

	if (pwr_ctrl)
		sd8x_pwr_on(pdata);
	else
		sd8x_pwr_off(pdata);

	pr_info("Now SD8x Device is power %s\n",
		pdata->is_on ? "on" : "off");

	mutex_unlock(&sd8x_pwr_mutex);

	return size;
}

static DEVICE_ATTR(pwr_ctrl, 0660, sd8x_pwr_show, sd8x_pwr_store);


#ifdef CONFIG_OF
static const struct of_device_id sd8x_rfkill_of_match[] = {
	{
		.compatible = "mrvl,sd8x-rfkill",
	},
	{},
};

MODULE_DEVICE_TABLE(of, sd8x_rfkill_of_match);







static int sd8x_rfkill_probe_dt(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node, *sdh_np;
	struct sd8x_rfkill_platform_data *pdata = pdev->dev.platform_data;
	struct platform_device *sdh_pdev;
	struct dw_mci *host;
	int sdh_phandle, gpio;
	struct regulator *wib_3v3, *wib_1v8, *wib_sdio_1v8;

	/* Get PD/RST pins status */
	pdata->pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(pdata->pinctrl)) {
		pdata->pinctrl = NULL;
		dev_warn(&pdev->dev, "could not get PD/RST pinctrl.\n");
	} else {
		pdata->pin_off = pinctrl_lookup_state(pdata->pinctrl, "off");
		if (IS_ERR(pdata->pin_off)) {
			pdata->pin_off = NULL;
			dev_err(&pdev->dev, "could not get off pinstate.\n");
		}

		pdata->pin_on = pinctrl_lookup_state(pdata->pinctrl, "on");
		if (IS_ERR(pdata->pin_on)) {
			pdata->pin_on = NULL;
			dev_err(&pdev->dev, "could not get on pinstate.\n");
		}
	}

	if (pdata->pinctrl && pdata->pin_off)
			pinctrl_select_state(pdata->pinctrl, pdata->pin_off);

	if (of_property_read_u32(np, "sd-host", &sdh_phandle)) {
		dev_err(&pdev->dev, "failed to find sd-host in dt\n");
		return -1;
	}

	/* we've got the phandle for sdh */
	sdh_np = of_find_node_by_phandle(sdh_phandle);
	if (unlikely(IS_ERR(sdh_np))) {
		dev_err(&pdev->dev, "failed to find device_node for sdh\n");
		return -1;
	}

	sdh_pdev = of_find_device_by_node(sdh_np);
	if (unlikely(IS_ERR(sdh_pdev))) {
		dev_err(&pdev->dev, "failed to find platform_device for sdh\n");
		return -1;
	}

	/* sdh_pdev->dev->driver_data was set as sdhci_host in sdhci driver */
	host = platform_get_drvdata(sdh_pdev);

	/*
	 * If we cannot find host, it's because sdh device is not registered
	 * yet. Probe again later.
	 */
	if (!host)
		return -EPROBE_DEFER;


	pdata->mmc = host->slot[0]->mmc;

    	

    pr_info("### sd8x_rfkill_probe_dt, host=%p \n", host );
	/* get gpios from dt */
	gpio = of_get_named_gpio(np, "edge-wakeup-gpio", 0);
	if (unlikely(gpio < 0)) {
		dev_warn(&pdev->dev, "edge-wakeup-gpio undefined\n");
		pdata->gpio_edge_wakeup = -1;
	} else {
		pdata->gpio_edge_wakeup = gpio;
	}
 

	gpio = of_get_named_gpio(np, "pd-gpio", 0);
	if (unlikely(gpio < 0)) {
		dev_err(&pdev->dev, "pd-gpio undefined\n");
		pdata->gpio_power_down = -1;
	} else {
		pdata->gpio_power_down = gpio;
	}


	gpio = of_get_named_gpio(np, "rst-gpio", 0);
	if (unlikely(gpio < 0)) {
		dev_err(&pdev->dev, "rst-gpio undefined\n");
		pdata->gpio_reset = -1;
	} else {
		pdata->gpio_reset = gpio;
	}
 

	gpio = of_get_named_gpio(np, "3v3-ldo-gpio", 0);
	if (unlikely(gpio < 0)) {
		dev_err(&pdev->dev, "3v3-ldo-gpio undefined\n");
		pdata->gpio_3v3_en = -1;
	} else {
		pdata->gpio_3v3_en = gpio;
	}


	gpio = of_get_named_gpio(np, "1v8-ldo-gpio", 0);
	if (unlikely(gpio < 0)) {
		dev_err(&pdev->dev, "1v8-ldo-gpio undefined\n");
		pdata->gpio_1v8_en = -1;
	} else {
		pdata->gpio_1v8_en = gpio;
	}
 

	wib_3v3 = regulator_get(&pdev->dev, "wib_3v3");
	if (IS_ERR_OR_NULL(wib_3v3)) {
		if (PTR_ERR(wib_3v3) < 0) {
			pr_info("%s: the regulator for wib_3v3 not found\n",
				__func__);
		}
	} else {
		pdata->wib_3v3 = wib_3v3;
	}
  

	wib_1v8 = regulator_get(&pdev->dev, "wib_1v8");
	if (IS_ERR_OR_NULL(wib_1v8)) {
		if (PTR_ERR(wib_1v8) < 0) {
			pr_info("%s: the regulator for wib_1v8 not found\n",
				__func__);
		}
	} else {
		pdata->wib_1v8 = wib_1v8;
	}


	wib_sdio_1v8 = regulator_get(&pdev->dev, "wib_sdio_1v8");
	if (IS_ERR_OR_NULL(wib_sdio_1v8)) {
		if (PTR_ERR(wib_sdio_1v8) < 0) {
			pr_info("%s: the regulator for wib_sdio_1v8 not found\n",
				__func__);
		}
	} else {
		pdata->wib_sdio_1v8 = wib_sdio_1v8;
	}

	return 0;
}
#else
static int sd8x_rfkill_probe_dt(struct platform_device *pdev)
{
	return 0;
}
#endif

static int sd8x_rfkill_probe(struct platform_device *pdev)
{
	struct sd8x_rfkill_platform_data *pdata = NULL;
	/* flag: whether pdata is passed from platfrom_data */
	int pdata_passed = 1;
	const struct of_device_id *match = NULL;
	int ret = -1;

	/* make sure sd8x_rfkill_platform_data is valid */
	pdata = pdev->dev.platform_data;
	if (!pdata) {
		/* if platfrom data do not pass the struct to us */
		pdata_passed = 0;

		pdata = sd8x_rfkill_platform_data_alloc(pdev);
		if (!pdata) {
			pr_err("can't get sd8x_rfkill_platform_data struct during probe\n");
			goto err_pdata;
		}

		pdev->dev.platform_data = pdata;
	}

	/* set value to sd8x_rfkill_platform_data if DT pass them to us */
#ifdef CONFIG_OF
	match = of_match_device(of_match_ptr(sd8x_rfkill_of_match),
				&pdev->dev);
#endif
	if (match) {
      
		ret = sd8x_rfkill_probe_dt(pdev);
     
		if (ret)
			goto err_dt;
	}


	/*
	 * Init GPIO/Regulater just system boot up & power off sd8x
	 * devices by default
	 * TODO: enhance it if there is different choose in future
	 */
	sd8x_pwr_ctrl(pdata, 0);
	pdata->is_on = 0;

	/*
	 * Create a proc interface, allowing user space to control
	 * sd8x devices' power
	 */
	device_create_file(&pdev->dev, &dev_attr_pwr_ctrl);

	wlan_wakeup_init();

	return 0;

err_dt:
	if (!pdata_passed)
		pdev->dev.platform_data = NULL;
err_pdata:

	return ret;
}

static int sd8x_rfkill_remove(struct platform_device *pdev)
{
	struct sd8x_rfkill_platform_data *pdata;
	pdata = pdev->dev.platform_data;

	if (pdata->wib_3v3)
		regulator_put(pdata->wib_3v3);

	if (pdata->wib_1v8)
		regulator_put(pdata->wib_1v8);

	if (pdata->wib_sdio_1v8)
		regulator_put(pdata->wib_sdio_1v8);
	return 0;
}

static int sd8x_rfkill_suspend(struct platform_device *pdev,
			       pm_message_t pm_state)
{
	return 0;
}

static int sd8x_rfkill_resume(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver sd8x_rfkill_platform_driver = {
	.probe = sd8x_rfkill_probe,
	.remove = sd8x_rfkill_remove,
	.driver = {
		   .name = SD8X_DEV_NAME,
		   .owner = THIS_MODULE,
#ifdef CONFIG_OF
		   .of_match_table = sd8x_rfkill_of_match,
#endif

		   },
	.suspend = sd8x_rfkill_suspend,
	.resume = sd8x_rfkill_resume,
};

static int __init sd8x_rfkill_init(void)
{
	return platform_driver_register(&sd8x_rfkill_platform_driver);
}

static void __exit sd8x_rfkill_exit(void)
{
	platform_driver_unregister(&sd8x_rfkill_platform_driver);
}

module_init(sd8x_rfkill_init);
module_exit(sd8x_rfkill_exit);

MODULE_ALIAS("platform:sd8x_rfkill");
MODULE_DESCRIPTION("sd8x_rfkill");
MODULE_AUTHOR("Marvell");
MODULE_LICENSE("GPL");
