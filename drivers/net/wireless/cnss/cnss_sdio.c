/* Copyright (c) 2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/export.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/pm.h>
#include <linux/pm_wakeup.h>
#include <linux/sched.h>
#include <linux/pm_qos.h>
#include <linux/pinctrl/consumer.h>
#include <linux/firmware.h>
#include <linux/dma-mapping.h>
#include <linux/spinlock.h>
#include <linux/suspend.h>
#include <linux/rwsem.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <linux/log2.h>
#include <linux/irqreturn.h>
#include <linux/interrupt.h>
#include <linux/decompress/mm.h>
#include <linux/spinlock.h>
#include <net/cnss.h>
#include <linux/kthread.h>
#include <net/cnss.h>

#define VREG_ON			1
#define VREG_OFF		0
#define WLAN_EN_HIGH		1
#define WLAN_EN_LOW		0
#define WLAN_BOOTSTRAP_HIGH	1
#define WLAN_BOOTSTRAP_LOW	0

#define WLAN_VREG_NAME		"vdd-wlan"
#define WLAN_VREG_IO_NAME	"vdd-wlan-io"
#define WLAN_VREG_XTAL_NAME	"vdd-wlan-xtal"
#define WLAN_VREG_SP2T_NAME	"vdd-wlan-sp2t"
#define WLAN_SWREG_NAME		"wlan-soc-swreg"
#define WLAN_EN_GPIO_NAME	"wlan-en-gpio"
#define WLAN_HOST_WAKE_NAME	"wlan-host-wake"
#define WLAN_BOOTSTRAP_GPIO_NAME "wlan-bootstrap-gpio"
#define NUM_OF_BOOTSTRAP 0

#define SOC_SWREG_VOLT_MAX	1200000
#define SOC_SWREG_VOLT_MIN	1200000
#define WLAN_VREG_IO_MAX	1800000
#define WLAN_VREG_IO_MIN	1800000
#define WLAN_VREG_XTAL_MAX	1800000
#define WLAN_VREG_XTAL_MIN	1800000
#define WLAN_VREG_SP2T_MAX	2700000
#define WLAN_VREG_SP2T_MIN	2700000

#define POWER_ON_DELAY		2000
#define WLAN_ENABLE_DELAY	10000
#define WLAN_RECOVERY_DELAY	1000
#define PCIE_ENABLE_DELAY	100000
#define WLAN_BOOTSTRAP_DELAY	10
#define CNSS_PINCTRL_STATE_ACTIVE "default"
#define BUS_ACTIVITY_TIMEOUT	1000

#define PRINT(format, ...)   printk("cnss: " format, ## __VA_ARGS__)

typedef struct _cnss_boarddata_section {
    struct list_head list;
    unsigned int offset;
    unsigned int len;
    unsigned char * sec;
} CNSS_BOARDDATA_SECTION;

enum sec_type {
    CLEAN = 0,
    DIRTY
};

#define CNSS_BOARDDATA_MAX_SECTION          16
#define CNSS_BOARDDATA_BUFFER_SIZE          (CNSS_BOARDDATA_MAX_SECTION * 0x400)  //16K
typedef struct _cnss_section_lsit {
    struct list_head  head;
    unsigned int      len;
} CNSS_SECTION_LIST;

typedef struct _cnss_boarddata {
    unsigned int len;
    unsigned int section_num;
    CNSS_BOARDDATA_SECTION sections[CNSS_BOARDDATA_MAX_SECTION];
    CNSS_SECTION_LIST clean_sections;
    CNSS_SECTION_LIST dirty_sections;
    unsigned char buf[CNSS_BOARDDATA_BUFFER_SIZE];
} CNSS_BOARDDATA, * PCNSS_BOARDDATA;


struct cnss_wlan_gpio_info {
	char *name;
	u32 num;
	bool state;
	bool init;
	bool prop;
	struct pinctrl *pinctrl;
	struct pinctrl_state *gpio_state_default;
};

struct cnss_wlan_vreg_info {
	struct regulator *wlan_reg;
	struct regulator *soc_swreg;
	struct regulator *wlan_reg_io;
	struct regulator *wlan_reg_xtal;
	struct regulator *wlan_reg_sp2t;
	bool state;
};

struct cnss_oob_ts {
	uint64_t timestamp_start;
	uint64_t timestamp_end;
	uint8_t oob_state;
};

static struct cnss_sdio_data {
	struct platform_device *pldev;
	struct cnss_wlan_vreg_info vreg_info;
	struct cnss_wlan_gpio_info gpio_info;
	struct cnss_wlan_gpio_info wake_info;
	struct cnss_platform_cap cap;
	int wlan_bootstrap_gpio[NUM_OF_BOOTSTRAP];
	enum cnss_driver_status driver_status;
	oob_irq_handler_t cnss_wlan_oob_irq_handler;
	void *cnss_wlan_oob_pm;
	int cnss_wlan_oob_irq_num;
	bool cnss_wlan_oob_irq_prop;
	bool cnss_wlan_oob_irq_wake_enabled;
	struct task_struct *oob_task;              /* task to handle oob interrupts */
	struct semaphore sem_oob;                  /* wake up for oob task */
	int oob_shutdown;                          /* stop the oob task */
	int force_hung;
	struct completion oob_completion;          /* oob thread completion */
	CNSS_BOARDDATA board_data;
	struct cnss_oob_ts oob_ts;
} *penv;


static CNSS_BOARDDATA_SECTION *
    cnss_boarddata_section_dequeue(CNSS_BOARDDATA * boarddata,
                                    enum sec_type type)
{
    CNSS_BOARDDATA_SECTION * sec = NULL;
    CNSS_SECTION_LIST * section_list = NULL;

    section_list = (CLEAN == type) ? &boarddata->clean_sections :
                                     &boarddata->dirty_sections;

    if (!list_empty(&section_list->head)){
        sec = list_first_entry(&section_list->head,
                              CNSS_BOARDDATA_SECTION, list);
        list_del_init(&sec->list);
        section_list->len--;
    } else {
        /* TODO empty */
        PRINT("The %d list is empty.\n", type);
    }
    return sec;
}

static unsigned int cnss_boarddata_get_section_depth(CNSS_BOARDDATA * boarddata,
                                             enum sec_type type)
{

    CNSS_SECTION_LIST * section_list = NULL;

    section_list = (CLEAN == type) ? &boarddata->clean_sections :
                                     &boarddata->dirty_sections;

    return section_list->len;
}

static void cnss_boarddata_section_inqueue(CNSS_BOARDDATA * boarddata,
                                           CNSS_BOARDDATA_SECTION * sec,
                                           enum sec_type type)
{
    CNSS_SECTION_LIST * section_list = NULL;

    section_list = (CLEAN == type) ? &boarddata->clean_sections :
                                     &boarddata->dirty_sections;

    list_add_tail(&sec->list, &section_list->head);
    section_list->len++;

}

static int cnss_init_boarddata(CNSS_BOARDDATA * board_data)
{
    int i;
    int ret = 0;
    CNSS_BOARDDATA * boarddata = NULL;

    boarddata = board_data;

    if (!boarddata) {
        PRINT("alloc buffer for boarddata failed.\n");
        ret = -1;
    } else {
        memset(boarddata, 0, sizeof(CNSS_BOARDDATA));
        boarddata->len = CNSS_BOARDDATA_BUFFER_SIZE;
        INIT_LIST_HEAD(&boarddata->clean_sections.head);
        INIT_LIST_HEAD(&boarddata->dirty_sections.head);

        for (i = 0; i < CNSS_BOARDDATA_MAX_SECTION; i++) {
            /* insert all the sections to the free list */
            cnss_boarddata_section_inqueue(boarddata,
                                           &boarddata->sections[i],
                                           CLEAN);
        }
        PRINT("%s: success. clean queue depth is %d.\n",
                __func__, boarddata->clean_sections.len);
    }

    return ret;
}

/**
 * cnss_cache_boardda() - cache some board data
 * @buf: pointer to cached data:w
 *
 * @len: length of the cached data
 * @offset: dest offset in the boarddata buffer
 *
 * Function invokes sme api to find the operating class
 *
 * Return: operating class
 */
int cnss_cache_boarddata(const unsigned char * buf,
                         unsigned int len,
                         unsigned int offset)
{
    int ret = 0;
    unsigned int depth = 0;
    CNSS_BOARDDATA  * boarddata = NULL;
    CNSS_BOARDDATA_SECTION * sec = NULL;

    if (!penv) {
	    pr_err("%s: penv is NULL which is unexpected\n", __func__);
        ret = -1;
        goto error;
    }

    boarddata = &penv->board_data;
    /* input check */
    if (!boarddata) {
        printk("cnss: ""There is no boarddata needed in this platform.\n");
        ret = -1;
        goto error;
    }

    depth = cnss_boarddata_get_section_depth(boarddata, DIRTY);

    while (depth) {
        sec = cnss_boarddata_section_dequeue(boarddata, DIRTY);
        if (sec->offset == offset) {
            sec->len    = len;
            memcpy(sec->sec, buf, len);
            cnss_boarddata_section_inqueue(boarddata, sec, DIRTY);
            goto error;
        }
        cnss_boarddata_section_inqueue(boarddata, sec, DIRTY);
        depth--;
    }

    if (likely(boarddata->len >= len + offset)) {
        PRINT("Cache board data. offset 0x%8x, length %d.\n", offset, len);
        sec = cnss_boarddata_section_dequeue(boarddata, CLEAN);

        /* TODO: the offset should be checked.  */
        sec->offset = offset;
        sec->len    = len;
        sec->sec    = boarddata->buf + offset;
        memcpy(sec->sec, buf, len);

        cnss_boarddata_section_inqueue(boarddata, sec, DIRTY);
    }
error:
    return ret;
}
EXPORT_SYMBOL(cnss_cache_boarddata);

#if 0
static int dump_boarddata(unsigned char *buf, int size)
{
	int ret = 0;
	struct file *fp;
	mm_segment_t old_fs;
	loff_t pos = 0;

	/* change to KERNEL_DS address limit */
	old_fs = get_fs();
	set_fs(KERNEL_DS);

	/* open file to write */
	fp = filp_open("/data/boarddata", O_WRONLY|O_CREAT, 0640);
	if (IS_ERR(fp)) {
		printk("%s: open file error\n", __FUNCTION__);
		ret = -1;
		goto exit;
	}

	/* Write buf to file */
	fp->f_op->write(fp, buf, size, &pos);

exit:
	/* close file before return */
	if (fp)
		filp_close(fp, current->files);
	/* restore previous address limit */
	set_fs(old_fs);

	return ret;
}
#endif

int cnss_update_boarddata(unsigned char * buf, unsigned int len)
{
    int ret = 0;
    int depth = 0;
    CNSS_BOARDDATA_SECTION * sec;
    CNSS_BOARDDATA * boarddata = NULL;

    if (!penv) {
	    pr_err("%s: penv is NULL which is unexpected\n", __func__);
        ret = -1;
        goto error;
    }

    boarddata = &penv->board_data;

    if (!boarddata) {
        printk("There is no boarddata needed in this platform.\n");
        ret = -1;
        goto error;
    }

   	printk("Update boarddata using the cached data. There are %d sections.\n", boarddata->dirty_sections.len);
    depth = boarddata->dirty_sections.len;
    while (depth--) {
        sec = cnss_boarddata_section_dequeue(boarddata, DIRTY);

        if (sec) {
            if (unlikely(len < sec->offset + sec->len)) {
                PRINT("Section (0x%8x length %d) will exceed BD buffer.\
                       Stop updating.\n", sec->offset, sec->len);
                ret = -1;
                goto error;
            }
            PRINT("Patch board data. offset 0x%08x, length %d, address 0x%p.\n", sec->offset, sec->len, (void *)sec->sec);
            memcpy(buf + sec->offset, sec->sec, sec->len);
            cnss_boarddata_section_inqueue(boarddata, sec, DIRTY);
        } else {
            /* FIXME: Bug!! This branch should never be reached. */
            PRINT("Fixme: Dirty list is broken. No update for BD.\n");
            ret = -1;
            goto error;
        }
    }

    PRINT("CleanQ depth %d, DirtyQ depth %d\n", boarddata->clean_sections.len, boarddata->dirty_sections.len);
error:
	return ret;
}
EXPORT_SYMBOL(cnss_update_boarddata);

void cnss_get_monotonic_boottime(struct timespec *ts) 
{ 
	get_monotonic_boottime(ts); 
} 
EXPORT_SYMBOL(cnss_get_monotonic_boottime); 

int cnss_wlan_get_pending_irq(void)
{
	int ret = 0;

	if (!penv) {
	    pr_err("%s: penv is NULL which is unexpected\n", __func__);
	    ret = -ENODEV;
	} else {
	    ret = gpio_get_value(penv->wake_info.num);
	}

	return ret;
}
EXPORT_SYMBOL(cnss_wlan_get_pending_irq);

int cnss_wlan_query_oob_status(void)
{
	return 0;
}
EXPORT_SYMBOL(cnss_wlan_query_oob_status);

uint64_t cnss_get_timestamp(void)
{
	int cpu = raw_smp_processor_id();

	return cpu_clock(cpu);
}

/* thread to handle all the oob interrupts */
#define CNSS_OOB_MAX_IRQ_PENDING_COUNT 100
#define CNSS_OOB_PANIC_IRQ_PENDING_COUNT 500000
//#define CNSS_SDIO_OOB_STATS 1
/* refer to athdefs.h */
#define CNSS_MAX_ERR      3
#define CNSS_FAKE_STATUS  31
#define CNSS_STATUS_COUNT 33
#define CNSS_FORCE_POLL_COUNT 10
unsigned long oob_loop_count = 0, irq_pending_count = 0, irq_statistics[CNSS_OOB_MAX_IRQ_PENDING_COUNT+1] = {0};
unsigned long irq_handle_status[CNSS_STATUS_COUNT] = {0};
static int oob_task(void *pm_oob)
{
	struct sched_param param = { .sched_priority = 1 };
	int status, err_cnt;

	init_completion(&penv->oob_completion);
	sched_setscheduler(current, SCHED_FIFO, &param);

	while (!penv->oob_shutdown) {
		oob_loop_count++;
		irq_pending_count = 0;
		err_cnt = 0;
		if (down_interruptible(&penv->sem_oob) != 0)
			continue;

		penv->oob_ts.timestamp_start = cnss_get_timestamp();
		penv->oob_ts.oob_state = cnss_wlan_get_pending_irq();
		if (penv->cnss_wlan_oob_pm && penv->cnss_wlan_oob_irq_handler) {
			while (!cnss_wlan_get_pending_irq() && !penv->oob_shutdown) {
				irq_pending_count++;
				if (irq_pending_count > CNSS_OOB_PANIC_IRQ_PENDING_COUNT && oob_loop_count != 1)
					panic("%s: irq pending count %lu\n", __func__, irq_pending_count);

				status = penv->cnss_wlan_oob_irq_handler(penv->cnss_wlan_oob_pm);
				if (status) {
					(status < 0) ? irq_handle_status[0]++ :
						((status < CNSS_STATUS_COUNT) ?
						 irq_handle_status[status]++ :
						 irq_handle_status[CNSS_STATUS_COUNT-1]++);
					break;
				}
			}
			if (irq_pending_count) {
				if (irq_pending_count < CNSS_OOB_MAX_IRQ_PENDING_COUNT)
					irq_statistics[irq_pending_count]++;
				else
					irq_statistics[CNSS_OOB_MAX_IRQ_PENDING_COUNT]++;
			} else
				irq_statistics[0]++;
#ifdef CNSS_SDIO_OOB_STATS
			if (oob_loop_count % 10000 == 0) {
				int count;
				for (count = 0; count <= CNSS_OOB_MAX_IRQ_PENDING_COUNT; count++) {
				if (irq_statistics[count] > 0)
					pr_info("%s: irq_statistics[%d]=%lu\n", __func__, count, irq_statistics[count]);
				}
			}
#endif
		} else {
			pr_err("%s: irq callback isn't registerred or paramter is NULL, irq_handler=%p, para=%p\n",
				__func__, penv->cnss_wlan_oob_irq_handler, penv->cnss_wlan_oob_pm);
		}
		penv->oob_ts.timestamp_end = cnss_get_timestamp();
	}

	complete_and_exit(&penv->oob_completion, 0);
	return 0;
}

#if 1 /* 20160327 AI 4 2 */
int cnss_wlan_oob_shutdown(void)
{
	if (!penv) {
		pr_err("%s: penv is NULL which is unexpected\n", __func__);
	} else {
		if (!penv->oob_task) {
			pr_err("%s: oob_task is NULL which is unexpected\n", __func__);
		} else {
			pr_info("%s: wait for oob task finished itself\n", __func__);
			penv->oob_shutdown = 1;
			penv->force_hung = 1;
			up(&penv->sem_oob);
		}
	}

	return 0;
}
EXPORT_SYMBOL(cnss_wlan_oob_shutdown);

int cnss_wlan_check_hang(void)
{
	if (!penv) {
		pr_err("%s: penv is NULL which is unexpected\n", __func__);
		return 0;
	} else {
		return penv->force_hung;
	}
}
EXPORT_SYMBOL(cnss_wlan_check_hang);
#endif /* 20160327 AI 4 2 */

static int wlan_oob_irq_put(void)
{
	if (!penv) {
		pr_err("%s: penv is NULL which is unexpected\n", __func__);
	} else {
		if (!penv->oob_task) {
			pr_err("%s: oob_task is NULL which is unexpected\n", __func__);
		} else {
			pr_info("%s: try to kill the oob task\n", __func__);
			penv->oob_shutdown = 1;
			up(&penv->sem_oob);
			wait_for_completion(&penv->oob_completion);
			penv->oob_task = NULL;
		}
	}

	return 0;
}

static int wlan_oob_irq_get(void)
{
	if (!penv) {
		pr_err("%s: penv is NULL which is unexpected\n", __func__);
	} else {
		if (penv->oob_task) {
			pr_err("%s: oob_task is already running\n", __func__);
		} else {
			pr_info("%s: try to create the oob task\n", __func__);
			penv->oob_shutdown = 0;
			penv->oob_task = kthread_create(oob_task, NULL, "koobirqd");
			if (IS_ERR(penv->oob_task)) {
				pr_err("%s: fail to create oob task\n", __func__);
				penv->oob_task = NULL;
			} else {
				wake_up_process(penv->oob_task);
				up(&penv->sem_oob);
				pr_info("%s: successfully start oob task\n", __func__);
			}
		}
	}

	return 0;
}

static irqreturn_t wlan_oob_irq(int irq, void *dev_id)
{
	if (!penv) {
		pr_err("%s: penv is NULL which is unexpected\n", __func__);
	} else {
		up(&penv->sem_oob);
	}
	return IRQ_HANDLED;
}

int cnss_wlan_register_oob_irq_handler(oob_irq_handler_t handler, void* pm_oob)
{
	int ret = 0;

	if (!penv) {
		pr_err("%s: penv is NULL which is unexpected\n", __func__);
		ret = -ENODEV;
	} else {
		pr_info("%s: handler=%p, pm_oob=%p\n", __func__, handler, pm_oob);
		if (penv->cnss_wlan_oob_irq_prop == false) {
			pr_err("%s: OOB IRQ hasn't been requested\n", __func__);
			ret = -ENOSYS;
		} else if (penv->oob_task) {
			pr_err("%s: OOB task is still running\n", __func__);
			ret = -EBUSY;
		}
		else {
			penv->cnss_wlan_oob_irq_handler = handler;
			penv->cnss_wlan_oob_pm = pm_oob;
			wlan_oob_irq_get();
		}
	}

	return ret;
}
EXPORT_SYMBOL(cnss_wlan_register_oob_irq_handler);

int cnss_wlan_unregister_oob_irq_handler(void *pm_oob)
{
	int ret = 0;

	if (!penv) {
		pr_err("%s: pdev is NULL which is unexpected\n", __func__);
		ret = -ENODEV;
	} else {
		if (penv->cnss_wlan_oob_irq_prop == false) {
			pr_err("%s: OOB IRQ hasn't been requested\n", __func__);
			ret = -ENOSYS;
		} else if (!penv->oob_task) {
			pr_err("%s: no OOB task is running\n", __func__);
//			ret = -ESRCH;
		} else if (pm_oob != penv->cnss_wlan_oob_pm) {
			pr_err("%s: wrong parameter\n", __func__);
			ret = -EINVAL;
		} else if (penv->oob_shutdown == 1) {
			pr_err("%s: oob task is already under shutdown\n", __func__);
			ret = -EBUSY;
			wait_for_completion(&penv->oob_completion);
			penv->oob_task = NULL;
		} else {
			wlan_oob_irq_put();
			penv->cnss_wlan_oob_irq_handler = NULL;
			penv->cnss_wlan_oob_pm = NULL;
		}
	}

	return ret;
}
EXPORT_SYMBOL(cnss_wlan_unregister_oob_irq_handler);

int cnss_wlan_vreg_on(struct cnss_wlan_vreg_info *vreg_info)
{
	int ret = 0;

	if (vreg_info->wlan_reg) {
		ret = regulator_enable(vreg_info->wlan_reg);
		if (ret) {
			pr_err("%s: regulator enable failed for WLAN power\n",
					__func__);
			goto error_enable;
		}
	}

	if (vreg_info->wlan_reg_io) {
		ret = regulator_enable(vreg_info->wlan_reg_io);
		if (ret) {
			pr_err("%s: regulator enable failed for wlan_reg_io\n",
				__func__);
			goto error_enable_reg_io;
		}
	}

	if (vreg_info->wlan_reg_xtal) {
		ret = regulator_enable(vreg_info->wlan_reg_xtal);
		if (ret) {
			pr_err("%s: regulator enable failed for wlan_reg_xtal\n",
				__func__);
			goto error_enable_reg_xtal;
		}
	}

	if (vreg_info->wlan_reg_sp2t) {
		ret = regulator_enable(vreg_info->wlan_reg_sp2t);
		if (ret) {
			pr_err("%s: regulator enable failed for wlan_reg_sp2t\n",
				__func__);
			goto error_enable_reg_sp2t;
		}
	}

	if (vreg_info->soc_swreg) {
		ret = regulator_enable(vreg_info->soc_swreg);
		if (ret) {
			pr_err("%s: regulator enable failed for external soc-swreg\n",
					__func__);
			goto error_enable_soc_swreg;
		}
	}

	return ret;

error_enable_soc_swreg:
	if (vreg_info->wlan_reg_sp2t)
		regulator_disable(vreg_info->wlan_reg_sp2t);
error_enable_reg_sp2t:
	if (vreg_info->wlan_reg_xtal)
		regulator_disable(vreg_info->wlan_reg_xtal);
error_enable_reg_xtal:
	if (vreg_info->wlan_reg_io)
		regulator_disable(vreg_info->wlan_reg_io);
error_enable_reg_io:
	if (vreg_info->wlan_reg)
		regulator_disable(vreg_info->wlan_reg);
error_enable:
	return ret;
}

int cnss_wlan_vreg_off(struct cnss_wlan_vreg_info *vreg_info)
{
	int ret = 0;

	if (vreg_info->soc_swreg) {
		ret = regulator_disable(vreg_info->soc_swreg);
		if (ret) {
			pr_err("%s: regulator disable failed for external soc-swreg\n",
					__func__);
			goto error_disable;
		}
	}

	if (vreg_info->wlan_reg_sp2t) {
		ret = regulator_disable(vreg_info->wlan_reg_sp2t);
		if (ret) {
			pr_err("%s: regulator disable failed for wlan_reg_sp2t\n",
				__func__);
			goto error_disable;
		}
	}

	if (vreg_info->wlan_reg_xtal) {
		ret = regulator_disable(vreg_info->wlan_reg_xtal);
		if (ret) {
			pr_err("%s: regulator disable failed for wlan_reg_xtal\n",
				__func__);
			goto error_disable;
		}
	}

	if (vreg_info->wlan_reg_io) {
		ret = regulator_disable(vreg_info->wlan_reg_io);
		if (ret) {
			pr_err("%s: regulator disable failed for wlan_reg_io\n",
				__func__);
			goto error_disable;
		}
	}

	if (vreg_info->wlan_reg) {
		ret = regulator_disable(vreg_info->wlan_reg);
		if (ret) {
			pr_err("%s: regulator disable failed for WLAN power\n",
					__func__);
			goto error_disable;
		}
 	}

error_disable:
	return ret;
}

int cnss_wlan_vreg_set(struct cnss_wlan_vreg_info *vreg_info, bool state)
{
	int ret = 0;

	if (vreg_info->state == state) {
		pr_debug("Already wlan vreg state is %s\n",
				state ? "enabled" : "disabled");
		goto out;
	}

	if (state)
		ret = cnss_wlan_vreg_on(vreg_info);
	else
		ret = cnss_wlan_vreg_off(vreg_info);

	if (ret)
		goto out;

	pr_debug("%s: wlan vreg is now %s\n", __func__,
			state ? "enabled" : "disabled");
	vreg_info->state = state;

out:
	return ret;
}

int cnss_wlan_gpio_init(struct cnss_wlan_gpio_info *info)
{
	int ret = 0;

	ret = gpio_request(info->num, info->name);

	if (ret) {
		pr_err("%s:can't get gpio %s ret %d\n",__func__, info->name, ret);
		goto err_gpio_req;
	}

	ret = gpio_direction_output(info->num, info->init);

	if (ret) {
		pr_err("can't set gpio direction %s ret %d\n", info->name, ret);
		goto err_gpio_dir;
	}
	info->state = info->init;

	return ret;

err_gpio_dir:
	gpio_free(info->num);

err_gpio_req:

	return ret;
}

int cnss_wlan_wakeup_init(struct cnss_wlan_gpio_info *info)
{
	int ret = 0;

	if (!penv) {
		pr_err("%s: penv is NULL which is unexpected\n", __func__);
		return -ENODEV;
	}

	ret = gpio_request(info->num, info->name);

	if (ret) {
		pr_err("%s:can't get wakeup gpio %s ret %d\n",__func__, info->name, ret);
		goto err_wakeup_req;
	}

	ret = gpio_direction_input(info->num);

	if (ret) {
		pr_err("%s:can't set gpio direction %s ret %d\n", __func__, info->name, ret);
		goto err_wakeup_dir;
	}
	info->state = info->init;

	penv->cnss_wlan_oob_irq_num = gpio_to_irq(penv->wake_info.num);
	ret = request_irq(penv->cnss_wlan_oob_irq_num, wlan_oob_irq,
			  IORESOURCE_IRQ | IORESOURCE_IRQ_LOWEDGE,
			  penv->wake_info.name, NULL);
	if (ret) {
		penv->cnss_wlan_oob_irq_prop = false;
		pr_err("%s: request oob irq %d failed with %d\n", __func__, penv->cnss_wlan_oob_irq_num, ret);
		goto err_wakeup_dir;
	} else {
		penv->cnss_wlan_oob_irq_prop = true;
		pr_info("%s: succeed to request oob irq %d\n", __func__, penv->cnss_wlan_oob_irq_num);
		if (!enable_irq_wake(penv->cnss_wlan_oob_irq_num))
			penv->cnss_wlan_oob_irq_wake_enabled = true;
	}

	return ret;

err_wakeup_dir:
	gpio_free(info->num);

err_wakeup_req:
	return ret;
}

int cnss_wlan_bootstrap_gpio_init(int index)
{
	int ret = 0;
	int gpio = penv->wlan_bootstrap_gpio[index];

	ret = gpio_request(gpio, WLAN_BOOTSTRAP_GPIO_NAME);
	if (ret) {
		pr_err("%s: Can't get GPIO %d, ret = %d\n",
			__func__, gpio, ret);
		goto out;
	}

	ret = gpio_direction_output(gpio, WLAN_BOOTSTRAP_HIGH);
	if (ret) {
		pr_err("%s: Can't set GPIO %d direction, ret = %d\n",
			__func__, gpio, ret);
		gpio_free(gpio);
		goto out;
	}

	msleep(WLAN_BOOTSTRAP_DELAY);
out:
	return ret;
}

void cnss_wlan_gpio_set(struct cnss_wlan_gpio_info *info, bool state)
{
	if (!info->prop)
		return;

	if (info->state == state) {
		pr_debug("Already %s gpio is %s\n",
			 info->name, state ? "high" : "low");
		return;
	}

	gpio_set_value(info->num, state);
	info->state = state;

	pr_debug("%s: %s gpio is now %s\n, num is %d", __func__,
		 info->name, info->state ? "enabled" : "disabled", info->num);
}

int cnss_pinctrl_init(struct cnss_wlan_gpio_info *gpio_info,
	struct platform_device *pdev)
{
	int ret;
	gpio_info->pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR_OR_NULL(gpio_info->pinctrl)) {
		pr_err("%s: Failed to get pinctrl!\n", __func__);
		return PTR_ERR(gpio_info->pinctrl);
	}

	gpio_info->gpio_state_default = pinctrl_lookup_state(gpio_info->pinctrl,
		CNSS_PINCTRL_STATE_ACTIVE);
	if (IS_ERR_OR_NULL(gpio_info->gpio_state_default)) {
		pr_err("%s: Can not get active pin state!\n", __func__);
		return PTR_ERR(gpio_info->gpio_state_default);
	}

	ret = pinctrl_select_state(gpio_info->pinctrl,
		gpio_info->gpio_state_default);

	return ret;
}

int cnss_wlan_get_resources(struct platform_device *pdev)
{
	int ret = 0;
	int i;
	struct cnss_wlan_gpio_info *gpio_info = &penv->gpio_info;
	struct cnss_wlan_gpio_info *wake_info = &penv->wake_info;
	struct cnss_wlan_vreg_info *vreg_info = &penv->vreg_info;

	vreg_info->wlan_reg = regulator_get(&pdev->dev, WLAN_VREG_NAME);

	if (IS_ERR(vreg_info->wlan_reg)) {
		if (PTR_ERR(vreg_info->wlan_reg) == -EPROBE_DEFER)
			pr_err("%s: vreg probe defer\n", __func__);
		else
			pr_err("%s: vreg regulator get failed\n", __func__);
		ret = PTR_ERR(vreg_info->wlan_reg);
		goto err_reg_get;
	}

	ret = regulator_enable(vreg_info->wlan_reg);

	if (ret) {
		pr_err("%s: vreg initial vote failed\n", __func__);
		goto err_reg_enable;
	}

	if (of_get_property(pdev->dev.of_node,
		WLAN_VREG_IO_NAME"-supply", NULL)) {
		vreg_info->wlan_reg_io = regulator_get(&pdev->dev,
			WLAN_VREG_IO_NAME);
		if (!IS_ERR(vreg_info->wlan_reg_io)) {
			ret = regulator_set_voltage(vreg_info->wlan_reg_io,
				WLAN_VREG_IO_MIN, WLAN_VREG_IO_MAX);
			if (ret) {
				pr_err("%s: Set wlan_vreg_io failed!\n",
					__func__);
				goto err_reg_io_set;
			}

			ret = regulator_enable(vreg_info->wlan_reg_io);
			if (ret) {
				pr_err("%s: Enable wlan_vreg_io failed!\n",
					__func__);
				goto err_reg_io_enable;
			}
		}
	}

	if (of_get_property(pdev->dev.of_node,
		WLAN_VREG_XTAL_NAME"-supply", NULL)) {
		vreg_info->wlan_reg_xtal =
			regulator_get(&pdev->dev, WLAN_VREG_XTAL_NAME);
		if (!IS_ERR(vreg_info->wlan_reg_xtal)) {
			ret = regulator_set_voltage(vreg_info->wlan_reg_xtal,
				WLAN_VREG_XTAL_MIN, WLAN_VREG_XTAL_MAX);
			if (ret) {
				pr_err("%s: Set wlan_vreg_xtal failed!\n",
					__func__);
				goto err_reg_xtal_set;
			}

			ret = regulator_enable(vreg_info->wlan_reg_xtal);
			if (ret) {
				pr_err("%s: Enable wlan_vreg_xtal failed!\n",
					__func__);
				goto err_reg_xtal_enable;
			}
		}
	}

	if (of_get_property(pdev->dev.of_node,
		WLAN_VREG_SP2T_NAME"-supply", NULL)) {
		vreg_info->wlan_reg_sp2t =
			regulator_get(&pdev->dev, WLAN_VREG_SP2T_NAME);
		if (!IS_ERR(vreg_info->wlan_reg_sp2t)) {
			ret = regulator_set_voltage(vreg_info->wlan_reg_sp2t,
				WLAN_VREG_SP2T_MIN, WLAN_VREG_SP2T_MAX);
			if (ret) {
				pr_err("%s: Set wlan_vreg_sp2t failed!\n",
					__func__);
				goto err_reg_sp2t_set;
			}

			ret = regulator_enable(vreg_info->wlan_reg_sp2t);
			if (ret) {
				pr_err("%s: Enable wlan_vreg_sp2t failed!\n",
					__func__);
				goto err_reg_sp2t_enable;
			}
		}
	}

	if (of_find_property((&pdev->dev)->of_node,
				"qcom,wlan-uart-access", NULL))
		penv->cap.cap_flag |= CNSS_HAS_UART_ACCESS;

	if (of_get_property(pdev->dev.of_node,
			WLAN_SWREG_NAME"-supply", NULL)) {

		vreg_info->soc_swreg = regulator_get(&pdev->dev,
			WLAN_SWREG_NAME);
		if (IS_ERR(vreg_info->soc_swreg)) {
			pr_err("%s: soc-swreg node not found\n",
				__func__);
			goto err_reg_get2;
		}
		ret = regulator_set_voltage(vreg_info->soc_swreg,
				SOC_SWREG_VOLT_MIN, SOC_SWREG_VOLT_MAX);
		if (ret) {
			pr_err("%s: vreg initial voltage set failed on soc-swreg\n",
				__func__);
			goto err_reg_set;
		}
		ret = regulator_enable(vreg_info->soc_swreg);
		if (ret) {
			pr_err("%s: vreg initial vote failed\n", __func__);
			goto err_reg_enable2;
		}
		penv->cap.cap_flag |= CNSS_HAS_EXTERNAL_SWREG;
	}

	vreg_info->state = VREG_ON;

	if (!of_find_property((&pdev->dev)->of_node, gpio_info->name, NULL)) {
		gpio_info->prop = false;
		goto err_get_gpio;
	}

	gpio_info->prop = true;
	ret = of_get_named_gpio((&pdev->dev)->of_node,
				gpio_info->name, 0);

	if (ret >= 0) {
		gpio_info->num = ret;
		ret = 0;
	} else {
		if (ret == -EPROBE_DEFER)
			pr_debug("get WLAN_EN GPIO probe defer\n");
		else
			pr_err("%s: can't get gpio %s ret %d",
				__func__, gpio_info->name, ret);
		goto err_get_gpio;
	}

	ret = cnss_pinctrl_init(gpio_info, pdev);
	if (ret) {
		pr_err("%s: pinctrl init failed!\n", __func__);
		goto err_pinctrl_init;
	}

	ret = cnss_wlan_gpio_init(gpio_info);
	if (ret) {
		pr_err("gpio init failed\n");
		goto err_gpio_init;
	}

	if (!of_find_property((&pdev->dev)->of_node, wake_info->name, NULL)) {
		wake_info->prop = false;
		pr_err("%s: Fail to get wakeup gpio\n", __func__);
		goto err_get_wakeup_gpio;
	}

	wake_info->prop = true;
	ret = of_get_named_gpio((&pdev->dev)->of_node, wake_info->name, 0);

	if (ret >= 0) {
		wake_info->num = ret;
			pr_debug("%s: wake-host gpio num is %d\n", __func__, wake_info->num);
		ret = 0;
	} else {
		if (ret == -EPROBE_DEFER)
			pr_err("get WLAN_WAKE GPIO probe defer\n");
		else
			pr_err("%s: can't get gpio %s ret %d",
				__func__, wake_info->name, ret);
		goto err_get_wakeup_gpio;
	}

	ret = cnss_wlan_wakeup_init(wake_info);
	if (ret) {
		pr_err("%s: gpio wake init failed\n", __func__);
		goto err_wakeup_gpio_init;
	}

	if (of_find_property((&pdev->dev)->of_node,
		WLAN_BOOTSTRAP_GPIO_NAME, NULL)) {
		for (i = 0; i < NUM_OF_BOOTSTRAP; i++) {
			penv->wlan_bootstrap_gpio[i] =
			of_get_named_gpio((&pdev->dev)->of_node,
					WLAN_BOOTSTRAP_GPIO_NAME, i);
			if (penv->wlan_bootstrap_gpio[i] > 0) {
				ret = cnss_wlan_bootstrap_gpio_init(i);
				if (ret)
					goto err_gpio_init;
			} else {
				pr_err("%s: Can't get GPIO-%d %d, ret = %d",
					__func__, i,
					penv->wlan_bootstrap_gpio[i],
					ret);
			}
		}
	}

	return ret;

err_get_wakeup_gpio:
err_wakeup_gpio_init:
	gpio_free(gpio_info->num);
	gpio_info->state = WLAN_EN_LOW;
	gpio_info->prop = false;
err_gpio_init:
err_pinctrl_init:
err_get_gpio:
	if (vreg_info->soc_swreg)
		regulator_disable(vreg_info->soc_swreg);
	vreg_info->state = VREG_OFF;

err_reg_enable2:
err_reg_set:
	if (vreg_info->soc_swreg)
		regulator_put(vreg_info->soc_swreg);

err_reg_get2:
	if (vreg_info->wlan_reg_sp2t)
		regulator_disable(vreg_info->wlan_reg_sp2t);

err_reg_sp2t_enable:
	if (vreg_info->wlan_reg_sp2t)
		regulator_put(vreg_info->wlan_reg_sp2t);

err_reg_sp2t_set:
	if (vreg_info->wlan_reg_xtal)
		regulator_disable(vreg_info->wlan_reg_xtal);

err_reg_xtal_enable:
	if (vreg_info->wlan_reg_xtal)
		regulator_put(vreg_info->wlan_reg_xtal);

err_reg_xtal_set:
	if (vreg_info->wlan_reg_io)
		regulator_disable(vreg_info->wlan_reg_io);

err_reg_io_enable:
	if (vreg_info->wlan_reg_io)
		regulator_put(vreg_info->wlan_reg_io);

err_reg_io_set:
	regulator_disable(vreg_info->wlan_reg);
err_reg_enable:
	regulator_put(vreg_info->wlan_reg);

err_reg_get:
	return ret;
}

void cnss_wlan_release_resources(void)
{
	struct cnss_wlan_gpio_info *gpio_info = &penv->gpio_info;
	struct cnss_wlan_gpio_info *wake_info = &penv->wake_info;
	struct cnss_wlan_vreg_info *vreg_info = &penv->vreg_info;
	int i;

	for (i = 0; i < NUM_OF_BOOTSTRAP; i++) {
		if (penv->wlan_bootstrap_gpio[i] > 0)
			gpio_free(penv->wlan_bootstrap_gpio[i]);
	}
	gpio_free(gpio_info->num);
	gpio_info->state = WLAN_EN_LOW;
	gpio_info->prop = false;

	if (penv->cnss_wlan_oob_irq_prop == true) {
		wlan_oob_irq_put();
		if (penv->cnss_wlan_oob_irq_wake_enabled == true) {
			if(!disable_irq_wake(penv->cnss_wlan_oob_irq_num))
				penv->cnss_wlan_oob_irq_wake_enabled = false;
		}
		disable_irq(penv->cnss_wlan_oob_irq_num);
		free_irq(penv->cnss_wlan_oob_irq_num, NULL);
		penv->cnss_wlan_oob_irq_prop = false;
	}
	gpio_free(wake_info->num);
	wake_info->state = WLAN_EN_LOW;
	wake_info->prop = false;
	cnss_wlan_vreg_set(vreg_info, VREG_OFF);
	if (vreg_info->soc_swreg) {
		regulator_put(vreg_info->soc_swreg);
	}
	if (vreg_info->wlan_reg_sp2t) {
		regulator_put(vreg_info->wlan_reg_sp2t);
	}
	if (vreg_info->wlan_reg_xtal) {
		regulator_put(vreg_info->wlan_reg_xtal);
	}
	if (vreg_info->wlan_reg_io) {
		regulator_put(vreg_info->wlan_reg_io);
	}
	if (vreg_info->wlan_reg) {
		/* Fix me, if insmod failure, will kernel panic here*/
		regulator_put(vreg_info->wlan_reg);
	}
	vreg_info->state = VREG_OFF;
}

extern void (*notify_func_callback)(void *dev_id, int state);
extern void *mmc_host_dev;

int wlan_platform_sdio_enumerate(bool device_present)
{
	pr_err("%s: notify_func=%p, mmc_host_dev=%p, device_present=%d\n",
		__FUNCTION__, notify_func_callback, mmc_host_dev, device_present);

	if (notify_func_callback)
		notify_func_callback(mmc_host_dev, device_present);
	else
		pr_warning("%s: Nobody to notify\n", __FUNCTION__);

	return 0;
}

void cnss_set_driver_status(enum cnss_driver_status driver_status)
{
	penv->driver_status = driver_status;
}
EXPORT_SYMBOL(cnss_set_driver_status);

int cnss_wlan_register_driver(void)
{
	int ret = 0;
	int i;
	struct cnss_wlan_vreg_info *vreg_info;
	struct cnss_wlan_gpio_info *wake_info;
	struct cnss_wlan_gpio_info *gpio_info;

	if (!penv)
		return -ENODEV;

	vreg_info = &penv->vreg_info;
	wake_info = &penv->wake_info;
	gpio_info = &penv->gpio_info;

	ret = cnss_wlan_vreg_set(vreg_info, VREG_ON);
	if (ret) {
		pr_err("wlan vreg ON failed\n");
		goto err_wlan_vreg_on;
	}

	usleep_range(POWER_ON_DELAY,POWER_ON_DELAY);

	for (i = 0; i < NUM_OF_BOOTSTRAP; i++) {
		if (penv->wlan_bootstrap_gpio[i] <= 0)
			continue;
		if (!gpio_get_value(penv->wlan_bootstrap_gpio[i])) {
			gpio_set_value(penv->wlan_bootstrap_gpio[i],
				       WLAN_BOOTSTRAP_HIGH);
			msleep(WLAN_BOOTSTRAP_DELAY);
		}
	}
	cnss_wlan_gpio_set(wake_info, WLAN_EN_HIGH);

	cnss_wlan_gpio_set(gpio_info, WLAN_EN_HIGH);
	usleep_range(WLAN_ENABLE_DELAY,WLAN_ENABLE_DELAY);

	ret = wlan_platform_sdio_enumerate(true);
	if (ret) {
		pr_err("%s: Failed to enable SDIO!\n", __func__);
		goto err_wlan_sdio_enumerate;
	}
	pr_info("cnss_sdio: driver successfully registered\n");
	return ret;

err_wlan_sdio_enumerate:
	cnss_wlan_gpio_set(&penv->gpio_info, WLAN_EN_LOW);
	cnss_wlan_release_resources();
	wlan_platform_sdio_enumerate(false);
err_wlan_vreg_on:
	return ret;
}
EXPORT_SYMBOL(cnss_wlan_register_driver);

void cnss_wlan_unregister_driver(void)
{
	struct cnss_wlan_vreg_info *vreg_info;
	struct cnss_wlan_gpio_info *wake_info;
	struct cnss_wlan_gpio_info *gpio_info;
	if (!penv)
		return;

	vreg_info = &penv->vreg_info;
	wake_info = &penv->wake_info;
	gpio_info = &penv->gpio_info;

	wlan_platform_sdio_enumerate(false);
	penv->driver_status = CNSS_UNINITIALIZED;

	cnss_wlan_gpio_set(gpio_info, WLAN_EN_LOW);
	cnss_wlan_gpio_set(wake_info, WLAN_EN_LOW);

	if (cnss_wlan_vreg_set(vreg_info, VREG_OFF))
		pr_err("wlan vreg OFF failed\n");
}
EXPORT_SYMBOL(cnss_wlan_unregister_driver);

void cnss_wlan_force_ldo_reset(void)
{
#ifdef CONFIG_QCA9377_LDO_RESET
	gpio_set_value(15, 0);
	msleep(100);
	gpio_set_value(15, 1);
#else
	printk("cnss: ""CONFIG_QCA9377_LDO_RESET is not set.\n");
#endif
}
EXPORT_SYMBOL(cnss_wlan_force_ldo_reset);

int cnss_sdio_probe(struct platform_device *pdev)
{
	int ret = 0;
	if (penv)
		return -ENODEV;

	penv = devm_kzalloc(&pdev->dev, sizeof(*penv), GFP_KERNEL);
	if (!penv)
		return -ENOMEM;

	penv->pldev = pdev;

	penv->gpio_info.name = WLAN_EN_GPIO_NAME;
	penv->gpio_info.num = 0;
	penv->gpio_info.state = WLAN_EN_LOW;
	penv->gpio_info.init = WLAN_EN_LOW;
	penv->gpio_info.prop = false;

	penv->wake_info.name = WLAN_HOST_WAKE_NAME;
	penv->wake_info.num = 0;
	penv->wake_info.state = WLAN_EN_LOW;
	penv->wake_info.init = WLAN_EN_LOW;
	penv->wake_info.prop = false;
	penv->oob_task = NULL;
	penv->oob_shutdown = 1;
	penv->force_hung = 0;
	sema_init(&penv->sem_oob, 0);

	penv->vreg_info.wlan_reg = NULL;
	penv->vreg_info.soc_swreg = NULL;
	penv->vreg_info.wlan_reg_io = NULL;
	penv->vreg_info.wlan_reg_xtal = NULL;
	penv->vreg_info.wlan_reg_sp2t = NULL;
	penv->vreg_info.state = VREG_OFF;

	penv->cnss_wlan_oob_irq_handler = NULL;
	penv->cnss_wlan_oob_pm = NULL;
	penv->cnss_wlan_oob_irq_num = 0;
	penv->cnss_wlan_oob_irq_prop = false;
	penv->cnss_wlan_oob_irq_wake_enabled = false;

	ret = cnss_wlan_get_resources(pdev);
	if (ret)
		goto err_get_wlan_res;

	cnss_wlan_gpio_set(&penv->gpio_info, WLAN_EN_HIGH);
	usleep_range(WLAN_ENABLE_DELAY,WLAN_ENABLE_DELAY);

	ret = wlan_platform_sdio_enumerate(true);
	if (ret) {
		pr_err("%s: Failed to enable SDIO!\n", __func__);
		goto err_wlan_sdio_enumerate;
	}

	cnss_init_boarddata(&penv->board_data);
	pr_info("cnss_sdio: Platform driver probed successfully.\n");
	return ret;

err_wlan_sdio_enumerate:
	cnss_wlan_gpio_set(&penv->gpio_info, WLAN_EN_LOW);
	cnss_wlan_release_resources();
	wlan_platform_sdio_enumerate(false);

err_get_wlan_res:
	penv = NULL;

	return ret;
}

int cnss_sdio_remove(struct platform_device *pdev)
{
	struct cnss_wlan_gpio_info *gpio_info = &penv->gpio_info;
	int i;

	wlan_platform_sdio_enumerate(false);

	cnss_wlan_gpio_set(gpio_info, WLAN_EN_LOW);
	for (i = 0; i < NUM_OF_BOOTSTRAP; i++) {
		if (penv->wlan_bootstrap_gpio[i] > 0)
			gpio_set_value(penv->wlan_bootstrap_gpio[i],
				       WLAN_BOOTSTRAP_LOW);
	}

	cnss_wlan_release_resources();

	return 0;
}

#include <net/cnss_prealloc.h>

static const struct of_device_id cnss_sdio_dt_match[] = {
	{.compatible = "qcom,cnss_sdio"},
	{}
};

MODULE_DEVICE_TABLE(of, cnss_sdio_dt_match);

static struct platform_driver cnss_sdio_driver = {
	.probe  = cnss_sdio_probe,
	.remove = cnss_sdio_remove,
	.driver = {
		.name = "cnss_sdio",
		.owner = THIS_MODULE,
		.of_match_table = cnss_sdio_dt_match,
	},
};

static int __init cnss_sdio_initialize(void)
{
	wcnss_prealloc_init();
	return platform_driver_register(&cnss_sdio_driver);
}

static void __exit cnss_sdio_exit(void)
{
	platform_driver_unregister(&cnss_sdio_driver);
	wcnss_prealloc_deinit();
}

late_initcall_sync(cnss_sdio_initialize);
module_exit(cnss_sdio_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("CNSS SDIO Driver");
