/*
 * File Name : u_ccr.c
 *
 * utilities for ccr protocol implementation.
 * Copyright (C) 2017 Samsung Electronics
 * Author: Guneet Singh Khurana, <gs.khurana@samsung.com>
 *         Namhee Park, <namh.park@samsung.com>
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/miscdevice.h>
#define DEFAULT_VENDOR_CODE 0xF6
#define CCR_PROTOCOL_VERSION 0x01
#define CCR_DEFAULT_SW_VERSION "T230NZXXU0ARA1_B2BF"
#define CCR_INFO_STRING_SIZE 20

#define BATTERY_LEVEL_UNKNOWN 127
#define BATTERY_STATE_UNKNOWN 001
#define BATTERY_TEMPERATURE_UNKNOWN -128
#define SYSTEM_STATE_UNKNOWN 000
#define APP_STATE_UNKNOWN 000

struct ccr_dev {
	struct workqueue_struct *wq;
	struct ccr_function_config *config;
};
struct ccr_dev_work {
	struct work_struct work;
	int	event;
	int	param;
	u8	err;
	u16	bRequestType;
	u16	w_value;
	u16	w_index;
	u16	w_length;
};

static const char usb_ccr_shortname[] = "usb_ccr";
/* Create misc driver for custom cmd */
static struct miscdevice usb_ccr_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = usb_ccr_shortname,
};

struct ccrad_info_transfer_data {
	u8 ccrad_protocol_version;
	u8 ccrad_protocol_version_reserve;
	u8 hw_version_year;
	u8 hw_version_week;
	u8 hw_rev;
	u8 sw_version_year;
	u8 sw_version_week;
	u8 sw_rev;
}__attribute__((packed));
struct ccr_info_transfer_data {
	char sw_version[CCR_INFO_STRING_SIZE];
}__attribute__((packed));
struct battery_transfer_data {
	u8 batt_level;
	u8 batt_health;
	u8 charging_status;
	u8 batt_temp;
}__attribute__((packed));
struct ccr_function_config {
	u8 system_state;
	u16 ccr_protocol_version;
	struct battery_transfer_data batt_data;
	struct ccr_info_transfer_data c_info_data;
	struct ccrad_info_transfer_data c_ad_info_data;
	u8 app_state;
	u8 vendor_code;
	u8 lpm_bootup;
	u8 batt_state_updt;
};

char ccr_events[][512] = {
		{ "CMD=CCRAD INFO" },
		{ "CMD=CCR INFO" },
		{ "CMD=BOOTUP" },
		{ "CMD=SHUTDOWN" },
		{ "CMD=RESTART" },
		{ "CMD=LCD OFF" },
		{ "CMD=LCD ON" },
		{ "CMD=LCD TOGGLE" },
		{ "CMD=RESTART APP" },
		{ "CMD=CCR STATE" },
		{ "CMD=BATT STATE UPDATE" },
		{ "CMD=BATT STATE TRANSFER" },
		{ "CMD=APP STATE UPDATE" },
		{ "CMD=APP STATE TRANSFER"},
		{ "CMD=PROTOCOL VERSION TRANSFER" },
		{ "\0"}
};

enum {
		COMMAND_CCRADINFO = 97,
		COMMAND_CCRINFO = 98,
		COMMAND_BOOTUP = 99,
		COMMAND_SHUTDOWN = 100,
		COMMAND_RESTART = 101,
		COMMAND_LCDON = 102,
		COMMAND_LCDOFF = 103,
		COMMAND_LCDTOGGLE = 104,
		COMMAND_RESTARTAPP = 154,
		COMMAND_CCRSTATE = 106,
		COMMAND_BATTSTATEUPDATE = 150,
		COMMAND_BATTSTATETF = 151,
		COMMAND_APPSTATEUPDATE = 152,
		COMMAND_APPSTATETF = 153,
		COMMAND_PROTOCOL_VER_TF = 155
};

static int ccr_cmds[] = {
		COMMAND_CCRADINFO,
		COMMAND_CCRINFO,
		COMMAND_BOOTUP,
		COMMAND_SHUTDOWN,
		COMMAND_RESTART,
		COMMAND_LCDON,
		COMMAND_LCDOFF,
		COMMAND_LCDTOGGLE,
		COMMAND_RESTARTAPP,
		COMMAND_CCRSTATE,
		COMMAND_BATTSTATEUPDATE,
		COMMAND_BATTSTATETF,
		COMMAND_APPSTATEUPDATE,
		COMMAND_APPSTATETF,
		COMMAND_PROTOCOL_VER_TF
};

static struct ccr_dev *_ccr_dev;

#define CMD_PARAMETERS_NONE -1
#define CMD_NOT_FOUND	-1
#define CMD_ERROR	1

static int ccr_function_init(struct android_usb_function *f,
		struct usb_composite_dev *cdev)
{
	struct ccr_dev *dev;
	struct ccr_function_config *config;
	int ret=0;
	printk(KERN_ERR "usb: ccr_function init called -->\n");
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	f->config = kzalloc(sizeof(struct ccr_function_config), GFP_KERNEL);

	if (!f->config)
	{
		kfree(dev);
		return -ENOMEM;
	}
	dev->wq = create_singlethread_workqueue("ccr_cmd_event");
	dev->config = f->config;
	_ccr_dev = dev;

	/*	Initialize Default Setting	*/
	dev->config->vendor_code = DEFAULT_VENDOR_CODE;
	dev->config->ccr_protocol_version = CCR_PROTOCOL_VERSION;

	dev->config->batt_data.batt_level =  BATTERY_LEVEL_UNKNOWN;
	dev->config->batt_data.batt_health = BATTERY_STATE_UNKNOWN;
	dev->config->batt_data.charging_status = BATTERY_STATE_UNKNOWN;
	dev->config->batt_data.batt_temp =  BATTERY_TEMPERATURE_UNKNOWN;

	dev->config->system_state = SYSTEM_STATE_UNKNOWN;
	dev->config->app_state = APP_STATE_UNKNOWN;

	strncpy(dev->config->c_info_data.sw_version, CCR_DEFAULT_SW_VERSION, sizeof(dev->config->c_info_data.sw_version)-1);

	config = f->config;

	config->vendor_code = DEFAULT_VENDOR_CODE;
	config->ccr_protocol_version = CCR_PROTOCOL_VERSION;

	config->batt_data.batt_level =  BATTERY_LEVEL_UNKNOWN;
	config->batt_data.batt_health = BATTERY_STATE_UNKNOWN;
	config->batt_data.charging_status = BATTERY_STATE_UNKNOWN;
	config->batt_data.batt_temp = BATTERY_TEMPERATURE_UNKNOWN;

	config->system_state = SYSTEM_STATE_UNKNOWN;
	config->app_state = APP_STATE_UNKNOWN;

	strncpy(config->c_info_data.sw_version, CCR_DEFAULT_SW_VERSION, sizeof(config->c_info_data.sw_version)-1);
	ret = misc_register(&usb_ccr_device);
	if (ret)
		printk("usb: %s - usb_ccr misc driver fail \n",__func__);

	printk(KERN_ERR "usb: ccr_function init <--\n");
	return 0;
}

static void ccr_function_cleanup(struct android_usb_function *f)
{
	printk(KERN_ERR "usb: ccr_function cleanup called -->\n");
	misc_deregister(&usb_ccr_device);
	kfree(_ccr_dev);
	kfree(f->config);
	f->config = NULL;
	_ccr_dev = NULL;
	printk(KERN_ERR "usb: ccr_function cleanup <--\n");
}

// CCR_AD INFO
static ssize_t ccr_ccrad_state_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct android_usb_function *f = dev_get_drvdata(dev);
	struct ccr_function_config *config = f->config;

	return sprintf(buf, "%02X %02X %02X %02X %02X %02X %02X %02X\n",
		config->c_ad_info_data.ccrad_protocol_version, config->c_ad_info_data.ccrad_protocol_version_reserve,
		config->c_ad_info_data.hw_version_year, config->c_ad_info_data.hw_version_week,
		config->c_ad_info_data.hw_rev, config->c_ad_info_data.sw_version_year,
		config->c_ad_info_data.sw_version_week, config->c_ad_info_data.sw_rev);
}

static DEVICE_ATTR(ccrad_state, S_IRUGO, ccr_ccrad_state_show,
						 NULL);


// CCR INFO, CCR's sw version
static ssize_t ccr_sw_version_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct android_usb_function *f = dev_get_drvdata(dev);
	struct ccr_function_config *config = f->config;

	return sprintf(buf, "%s\n", config->c_info_data.sw_version);
}

static ssize_t ccr_sw_version_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct android_usb_function *f = dev_get_drvdata(dev);
	struct ccr_function_config *config = f->config;
	char *value;

	if (size > 20) {
		printk(KERN_ERR "usb: (%s) size is wrong\n", __func__);
		return -EINVAL;
	}

	value = kzalloc(size+1, GFP_KERNEL);

	if (!value)
		return -ENOMEM;

	sscanf(buf, "%s", value);

	strncpy(config->c_info_data.sw_version, value, sizeof(config->c_info_data.sw_version)-1);
	kfree(value);
	return size;
}

static DEVICE_ATTR(sw_version, S_IRUGO | S_IWUSR, ccr_sw_version_show,
						 ccr_sw_version_store);


static ssize_t ccr_system_state_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct android_usb_function *f = dev_get_drvdata(dev);
	struct ccr_function_config *config = f->config;
	return sprintf(buf, "%d\n", config->system_state);
}

static ssize_t ccr_system_state_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct android_usb_function *f = dev_get_drvdata(dev);
	struct ccr_function_config *config = f->config;
	int value;
	sscanf(buf, "%d", &value);
	printk(KERN_ERR "usb: (%s) (%d)\n",__func__,value);
	config->system_state = value;
	return size;
}

static DEVICE_ATTR(system_state, S_IRUGO | S_IWUSR, ccr_system_state_show,
						 ccr_system_state_store);

static ssize_t ccr_app_state_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct android_usb_function *f = dev_get_drvdata(dev);
	struct ccr_function_config *config = f->config;
	return sprintf(buf, "%d\n", config->app_state);
}

static ssize_t ccr_app_state_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct android_usb_function *f = dev_get_drvdata(dev);
	struct ccr_function_config *config = f->config;
	int value;
	sscanf(buf, "%d", &value);
	printk(KERN_ERR "usb: (%s) (%d)\n",__func__,value);
	config->app_state = value;
	return size;
}

static DEVICE_ATTR(app_state, S_IRUGO | S_IWUSR, ccr_app_state_show,
						 ccr_app_state_store);
						 
						 
static ssize_t ccr_battery_state_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct android_usb_function *f = dev_get_drvdata(dev);
	struct ccr_function_config *config = f->config;
	return sprintf(buf, "%02X %02X %02X %02X\n",
		config->batt_data.batt_level, config->batt_data.batt_health, config->batt_data.charging_status, config->batt_data.batt_temp);
}

static ssize_t ccr_battery_state_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct android_usb_function *f = dev_get_drvdata(dev);
	struct ccr_function_config *config = f->config;
	int value[4];
	sscanf(buf, "%d %d %d %d",&value[0], &value[1],
			&value[2], &value[3]);
			
	config->batt_data.batt_level =  value[0];
	config->batt_data.batt_health = value[1];
	config->batt_data.charging_status = value[2];
	config->batt_data.batt_temp =  value[3];
	printk(KERN_ERR "usb: (%s) (%d) (%d) (%d) (%d) \n",__func__,
							config->batt_data.batt_level, config->batt_data.batt_health, config->batt_data.charging_status, config->batt_data.batt_temp);	
	return size;
}

static DEVICE_ATTR(battery_state, S_IRUGO | S_IWUSR, ccr_battery_state_show,
						 ccr_battery_state_store);

static ssize_t ccr_vendor_code_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct android_usb_function *f = dev_get_drvdata(dev);
	struct ccr_function_config *config = f->config;
	return sprintf(buf, "%d\n", config->vendor_code);
}

static ssize_t ccr_vendor_code_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct android_usb_function *f = dev_get_drvdata(dev);
	struct ccr_function_config *config = f->config;
	int value;
	sscanf(buf, "%d", &value);
	printk(KERN_ERR "usb: (%s) (%d)\n",__func__,value);
	config->vendor_code = value;
	return size;
}

static DEVICE_ATTR(vendor_code, S_IRUGO | S_IWUSR, ccr_vendor_code_show,
						 ccr_vendor_code_store);						 


static ssize_t lpm_bootup_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct android_usb_function *f = dev_get_drvdata(dev);
	struct ccr_function_config *config = f->config;
	int ret;

	ret = sprintf(buf, "%d\n", config->lpm_bootup);
	config->lpm_bootup = 0;
	return ret;
}

static DEVICE_ATTR(lpm_bootup, S_IRUGO, lpm_bootup_show,
						 NULL);			

static ssize_t batt_state_updt_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct android_usb_function *f = dev_get_drvdata(dev);
	struct ccr_function_config *config = f->config;
	int ret;

	ret = sprintf(buf, "%d\n", config->batt_state_updt);
	config->batt_state_updt = 0;
	return ret;
}

static DEVICE_ATTR(batt_state_updt, S_IRUGO, batt_state_updt_show,
						 NULL);			
						 
static struct device_attribute *ccr_function_attributes[] = {
	&dev_attr_system_state,
	&dev_attr_battery_state,
	&dev_attr_app_state,
	&dev_attr_vendor_code,
	&dev_attr_sw_version,
	&dev_attr_ccrad_state,
	&dev_attr_lpm_bootup,
	&dev_attr_batt_state_updt,
	NULL
};


static struct android_usb_function ccr_function = {
	.name		= "ccr",
	.init		= ccr_function_init,
	.cleanup	= ccr_function_cleanup,
	.attributes	= ccr_function_attributes,
};

static void ccr_work_notifier_func(struct work_struct *data)
{
	char *uevent_envp[2] = {NULL,NULL};
	char uevent_arr[1024];
	
	
	struct ccr_dev_work *event_work =
		container_of(data, struct ccr_dev_work, work);

	memset(uevent_arr,0x00,sizeof(uevent_arr));
	printk(KERN_DEBUG "usb: %s command recived is =%d param(%d) err(%d) \n", __func__, event_work->event,event_work->param, event_work->err);
	
	
	if(event_work->err == CMD_ERROR) {
		sprintf(uevent_arr,"CMD=Error breqType(%d) val(%d) index(%d) len(%d)", event_work->bRequestType,
		event_work->w_value, event_work->w_index, event_work->w_length);

	} else if ( event_work->param != CMD_PARAMETERS_NONE) {
		sprintf(uevent_arr,"%s PARAM(%d)",ccr_events[event_work->event], event_work->param);
		
	} else {
		strcpy(uevent_arr,ccr_events[event_work->event]);
	}

	uevent_envp[0] = uevent_arr;

	pr_err("usb: uevent sent to framework (%s) \n",uevent_envp[0]);
	kobject_uevent_env(&usb_ccr_device.this_device->kobj, KOBJ_CHANGE, uevent_envp);
	kfree(event_work);
	
}

void ccr_event_work(int cmd, int cmd_param, u8 err, u16 w_value, u16 w_index ,u16 w_length, u16 bRequestType)
{
	struct ccr_dev_work * event_work;

	pr_info("usb: %s\n", __func__);
	event_work = kmalloc(sizeof(struct ccr_dev_work), GFP_ATOMIC);
	INIT_WORK(&event_work->work, ccr_work_notifier_func);
	
	event_work->event = cmd;
	event_work->param = cmd_param;
	event_work->err = err;
	event_work->w_value = w_value;
	event_work->w_index = w_index;
	event_work->w_length = w_length;
	event_work->bRequestType =  bRequestType;
	queue_work(_ccr_dev->wq, &event_work->work);
}

static void ccr_ccrad_data_complete(struct usb_ep *ep,
		struct usb_request *req)
{
	struct ccr_dev *custom = ep->driver_data;

	if (req->status != 0) {
		printk("usb: %s err\n", __func__);
		return;
	}

	if (req->actual != sizeof(struct ccrad_info_transfer_data)) {
		usb_ep_set_halt(ep);
	} else {
		struct ccrad_info_transfer_data *value = req->buf;
		custom->config->c_ad_info_data = *value;
	}
}
	
static int ccr_ctrl_request(struct usb_composite_dev *cdev,
				const struct usb_ctrlrequest *ctrl)
{
	struct ccr_dev *custom = _ccr_dev;
	int	value = -EOPNOTSUPP;
	u16	w_index = le16_to_cpu(ctrl->wIndex);
	u16	w_value = le16_to_cpu(ctrl->wValue);
	u16 w_length = le16_to_cpu(ctrl->wLength);
	int i;
	int cmd = CMD_NOT_FOUND;
	int cmd_param = CMD_PARAMETERS_NONE;
	int err = 0;

	if ((ctrl->bRequestType & USB_TYPE_MASK) == USB_TYPE_VENDOR) {
		
		if (ctrl->bRequest == _ccr_dev->config->vendor_code) {
			printk(KERN_DEBUG "usb: %s MsgType=0x%x \n",
				__func__, w_value);
			value = 0;
			for(i = 0; i < sizeof(ccr_cmds)/sizeof(int); i++) {
				if(ccr_cmds[i] == w_value) cmd = i;
			}
			if(cmd == CMD_NOT_FOUND) {
				value = -EOPNOTSUPP;
				pr_info("usb: %s cmd not found in the matching table entry \n", __func__);
				err = CMD_ERROR;
				ccr_event_work(cmd, cmd_param, err, w_value , w_index, w_length, ctrl->bRequestType);
				return value;
			}

			switch (w_value) {
				case COMMAND_CCRADINFO:
					if ((w_index == 0 && w_length == 8 && (ctrl->bRequestType & USB_DIR_IN) == USB_DIR_OUT)) {
						value = w_length;
						cdev->gadget->ep0->driver_data = custom;
						cdev->req->complete = ccr_ccrad_data_complete;
					} else {
						value = -EOPNOTSUPP;
						err = CMD_ERROR;
					}
					break;
				case COMMAND_CCRINFO:
					if ((w_index == 0 && w_length == 19 && (ctrl->bRequestType & USB_DIR_IN) == USB_DIR_IN)) {
						value = min_t(unsigned, w_length, sizeof(u8)*(CCR_INFO_STRING_SIZE-1));
						memcpy(cdev->req->buf, &(_ccr_dev->config->c_info_data.sw_version), value);
					} else {
						value = -EOPNOTSUPP;
						err = CMD_ERROR;
					}
					break;
				case COMMAND_PROTOCOL_VER_TF:
					if ((w_index == 0 && w_length == 2 && (ctrl->bRequestType & USB_DIR_IN) == USB_DIR_IN)) {
						value = min_t(unsigned, w_length, sizeof(u16));
						memcpy(cdev->req->buf, &(_ccr_dev->config->ccr_protocol_version), value);
					} else {
						value = -EOPNOTSUPP;
						err = CMD_ERROR;
					}
					break;
				case COMMAND_BOOTUP:
				case COMMAND_RESTART:
				case COMMAND_LCDON:
				case COMMAND_LCDOFF:
				case COMMAND_LCDTOGGLE:
				case COMMAND_RESTARTAPP:
				case COMMAND_BATTSTATEUPDATE:
				case COMMAND_APPSTATEUPDATE:
					if (!(w_index == 0 && w_length == 0 && (ctrl->bRequestType & USB_DIR_IN) == USB_DIR_OUT)) {
						value = -EOPNOTSUPP;
						err = CMD_ERROR;
						break;
					}
					if (w_value == COMMAND_BOOTUP) {
						_ccr_dev->config->lpm_bootup = 1;
					} else if (w_value == COMMAND_BATTSTATEUPDATE) {
						_ccr_dev->config->batt_state_updt = 1;
					} else
						;
					break;
				case COMMAND_SHUTDOWN:
					if (!(w_index <= 255 && w_length == 0 && (ctrl->bRequestType & USB_DIR_IN) == USB_DIR_OUT)) {
						value = -EOPNOTSUPP;
						err = CMD_ERROR;
					}
					cmd_param = w_index;
					break;
				case COMMAND_CCRSTATE:
					if ((w_index == 0 && w_length == 1 && (ctrl->bRequestType & USB_DIR_IN) == USB_DIR_IN)) {
						value = min_t(unsigned, w_length, sizeof(u8));
						memcpy(cdev->req->buf, &(_ccr_dev->config->system_state), value);
					} else {
						value = -EOPNOTSUPP;
						err = CMD_ERROR;
					}
					break;
				case COMMAND_BATTSTATETF:
					if ((w_index == 0 && w_length == 4 && (ctrl->bRequestType & USB_DIR_IN) == USB_DIR_IN)) {
						value = min_t(unsigned, w_length, sizeof(struct battery_transfer_data));
						memcpy(cdev->req->buf, &(_ccr_dev->config->batt_data), value);
					} else {
						value = -EOPNOTSUPP;
						err = CMD_ERROR;
					}
					break;
				case COMMAND_APPSTATETF:
					if ((w_index == 0 && w_length == 1 && (ctrl->bRequestType & USB_DIR_IN) == USB_DIR_IN)) {
						value = min_t(unsigned, w_length, sizeof(u8));
						memcpy(cdev->req->buf, &(_ccr_dev->config->app_state), value);
					} else {
						value = -EOPNOTSUPP;
						err = CMD_ERROR;
					}
					break;
				default:
					value = -EOPNOTSUPP;
					err = CMD_ERROR;
					pr_err("usb: %s Invalid command code received \n", __func__);
					break;
			}
			ccr_event_work(cmd, cmd_param, err, w_value , w_index, w_length, ctrl->bRequestType);
	
		}
	}

	/* respond ZLP */
	if (value >= 0) {
		int rc;
		cdev->req->zero = 0;
		cdev->req->length = value;
		rc = usb_ep_queue(cdev->gadget->ep0, cdev->req, GFP_ATOMIC);
		if (rc < 0)
			printk(KERN_DEBUG "usb: %s failed usb_ep_queue\n",
					__func__);
	}
	return value;
}
