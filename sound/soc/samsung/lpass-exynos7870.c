/*
 * Audio SubSystem driver for Samsung Exynos7870
 *
 * Copyright (c) 2015 Samsung Electronics Co. Ltd.
 *	Tushar Behera <tushar.b@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/proc_fs.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <soc/samsung/exynos-powermode.h>

#include <sound/exynos.h>
#include <sound/exynos-audmixer.h>

#include "lpass.h"

#ifdef CONFIG_SND_SOC_SAMSUNG_VERBOSE_DEBUG
#ifdef dev_dbg
#undef dev_dbg
#endif
#define dev_dbg dev_err
#endif

#ifdef USE_EXYNOS_AUD_SCHED
#define AUD_TASK_CPU_UHQ	(5)
#ifdef CONFIG_SOC_EXYNOS5422
#define USE_AUD_TASK_RT
#endif
#endif

#ifdef CONFIG_PM_DEVFREQ
#define USE_AUD_DEVFREQ
#define AUD_CPU_FREQ_UHQA	(0)
#define AUD_KFC_FREQ_UHQA	(1352000)
#define AUD_MIF_FREQ_UHQA	(0)
#define AUD_INT_FREQ_UHQA	(0)
#define AUD_CPU_FREQ_HIGH	(0)
#define AUD_KFC_FREQ_HIGH	(0)
#define AUD_MIF_FREQ_HIGH	(0)
#define AUD_INT_FREQ_HIGH	(0)
#define AUD_CPU_FREQ_NORM	(0)
#define AUD_KFC_FREQ_NORM	(0)
#define AUD_MIF_FREQ_NORM	(0)
#define AUD_INT_FREQ_NORM	(0)
#endif

#define AUD_PLL_FREQ			(98304000U)
#define AUD_MI2S_FREQ			(110000000U + 100)
#define AUD_MIXER_FREQ			(49152000U + 100)


#define EXYNOS7870_DISPAUD_CFG			0x1000

#define DISPAUD_CFG_ADMA_SWRST_BIT		3
#define DISPAUD_CFG_AMP_SWRST_BIT		2
#define DISPAUD_CFG_MI2S_SWRST_BIT		1
#define DISPAUD_CFG_MIXER_SWRST_BIT		0

#define EXYNOS7880_AUD_PATH_CFG			0x0080

#define AUD_PATH_CFG_BT_MUX_CON_MASK		0x2

#define EXYNOS_GPIO_MODE_AUD_SYS_PWR_REG_OFFSET	0x1340
#define EXYNOS_PAD_RETENTION_AUD_OPTION_OFFSET	0x3028

static struct lpass_cmu_info {
	struct clk *clk_fout_aud_pll;
	struct clk *clk_dout_sclk_mi2s;
	struct clk *clk_dout_sclk_mixer;
	struct clk *clk_mi2s_aud_bclk;
	struct clk *clk_aclk_aud;
	struct clk *clk_dispaud_decon;
} lpass_cmu;

void __iomem *lpass_cmu_save[] = {
	NULL, /* endmark */
};

/* Audio subsystem version */
enum {
	LPASS_VER_7870, /* Joshua */
	LPASS_VER_MAX
};

struct lpass_info lpass;

struct aud_reg {
	void __iomem		*reg;
	u32			val;
	struct list_head	node;
};

struct subip_info {
	struct device		*dev;
	const char		*name;
	void			(*cb)(struct device *dev);
	atomic_t		use_cnt;
	struct list_head	node;
};

static LIST_HEAD(reg_list);
static LIST_HEAD(subip_list);
static void lpass_update_qos(void);

void lpass_enable_pll(bool on)
{
	if (on) {
		clk_prepare_enable(lpass_cmu.clk_dispaud_decon);
		clk_prepare_enable(lpass_cmu.clk_fout_aud_pll);
		clk_prepare_enable(lpass_cmu.clk_dout_sclk_mi2s);
	} else {
		clk_disable_unprepare(lpass_cmu.clk_fout_aud_pll);
		clk_disable_unprepare(lpass_cmu.clk_dout_sclk_mi2s);
		clk_disable_unprepare(lpass_cmu.clk_dispaud_decon);
	}
}

void lpass_set_ip_idle(bool value)
{
	if (value)
		exynos_update_ip_idle_status(lpass.idle_ip_index, 1);
	else
		exynos_update_ip_idle_status(lpass.idle_ip_index, 0);
}
EXPORT_SYMBOL(lpass_set_ip_idle);

/*
 * lpass_set_clk_hierarchy(): Define clock settings for audio
 *
 * This configures the default state of the MUX/DIV clocks used in Audio
 * sub-system. Since this called from driver probe function only, it is safe to
 * use devm_clk_get() APIs and remove the goto statements.
 *
 * Arguments:
 * 1. dev: 'struct device *', pointer to LPASS device
 *
 * Return value:
 * 0 on success, error code on failure.
 */
static int lpass_set_clk_hierarchy(struct device *dev)
{

	lpass_cmu.clk_fout_aud_pll = devm_clk_get(dev, "fout_aud_pll");
	if (IS_ERR(lpass_cmu.clk_fout_aud_pll)) {
		dev_err(dev, "fout_aud_pll clk not found\n");
		return PTR_ERR(lpass_cmu.clk_fout_aud_pll);
	}

	lpass_cmu.clk_dout_sclk_mi2s = devm_clk_get(dev, "dout_sclk_mi2s");
	if (IS_ERR(lpass_cmu.clk_dout_sclk_mi2s)) {
		dev_err(dev, "dout_sclk_mi2s clk not found\n");
		return PTR_ERR(lpass_cmu.clk_dout_sclk_mi2s);
	}

	lpass_cmu.clk_dispaud_decon = devm_clk_get(dev, "dispaud_decon");
	if (IS_ERR(lpass_cmu.clk_dispaud_decon)) {
		dev_err(dev, "clk_dispaud_decon clk not found\n");
		return PTR_ERR(lpass_cmu.clk_dispaud_decon);
	}

	lpass_enable_pll(true);

	clk_set_rate(lpass_cmu.clk_fout_aud_pll, AUD_PLL_FREQ);
	dev_info(dev, "PLL rate = %lu\n",
			clk_get_rate(lpass_cmu.clk_fout_aud_pll));

	clk_set_rate(lpass_cmu.clk_dout_sclk_mi2s, AUD_MI2S_FREQ);

	dev_info(dev, "sclk_mi2s clock rate = %lu\n",
			clk_get_rate(lpass_cmu.clk_dout_sclk_mi2s));

	return 0;
}

/**
 * AUD_PLL_USER Mux is defined as USERMUX. Enabling the USERMUX selects
 * the underlying PLL as the parent of this MUX and disabling sets the
 * oscillator clock as the parent of this clock.
 */
void lpass_set_mux_pll(void)
{
	/* Already taken care in lpass_enable_pll() */
}

void lpass_set_mux_osc(void)
{
	/* Already taken care in lpass_enable_pll() */
}

void lpass_retention_pad_reg(void)
{
	regmap_update_bits(lpass.pmureg,
			EXYNOS_GPIO_MODE_AUD_SYS_PWR_REG_OFFSET,
			0x1, 1);
}

void lpass_release_pad_reg(void)
{
	regmap_update_bits(lpass.pmureg,
			EXYNOS_PAD_RETENTION_AUD_OPTION_OFFSET,
			0x10000000, 1);

	regmap_update_bits(lpass.pmureg,
			EXYNOS_GPIO_MODE_AUD_SYS_PWR_REG_OFFSET,
			0x1, 1);
}

static inline bool is_running_only(const char *name)
{
	struct subip_info *si;

	if (atomic_read(&lpass.use_cnt) != 1)
		return false;

	list_for_each_entry(si, &subip_list, node) {
		if (atomic_read(&si->use_cnt) > 0 &&
			!strncmp(name, si->name, strlen(si->name)))
			return true;
	}

	return false;
}

int exynos_check_aud_pwr(void)
{
	/* TODO: Implement later */
	return 0;
}

void lpass_set_dma_intr(bool on)
{
}

void lpass_dma_enable(bool on)
{
}

void lpass_reset(int ip, int op)
{
	u32 reg, val, bit;
	void __iomem *regs;

	spin_lock(&lpass.lock);
	regs = lpass.regs;
	reg = EXYNOS7870_DISPAUD_CFG;
	switch (ip) {
	case LPASS_IP_DMA:
		bit = DISPAUD_CFG_ADMA_SWRST_BIT;
		break;

	case LPASS_IP_AMP:
		bit = DISPAUD_CFG_AMP_SWRST_BIT;
		break;

	case LPASS_IP_I2S:
		bit = DISPAUD_CFG_MI2S_SWRST_BIT;
		break;

	case LPASS_IP_MIXER:
		bit = DISPAUD_CFG_MIXER_SWRST_BIT;
		break;

	default:
		spin_unlock(&lpass.lock);
		pr_err("%s: wrong ip type %d!\n", __func__, ip);
		return;
	}

	val = readl(regs + reg);
	switch (op) {

	case LPASS_RESET_BIT_UNSET:
		val &= ~BIT(bit);
		break;
	case LPASS_RESET_BIT_SET:
		val |= BIT(bit);
		break;
	default:
		spin_unlock(&lpass.lock);
		pr_err("%s: wrong op type %d!\n", __func__, op);
		return;
	}

	writel(val, regs + reg);
	spin_unlock(&lpass.lock);
}

void lpass_reset_toggle(int ip)
{
	lpass_reset(ip, LPASS_RESET_BIT_SET);
	udelay(100);
	lpass_reset(ip, LPASS_RESET_BIT_UNSET);
}

int lpass_register_subip(struct device *ip_dev, const char *ip_name)
{
	struct device *dev = &lpass.pdev->dev;
	struct subip_info *si;

	si = devm_kzalloc(dev, sizeof(struct subip_info), GFP_KERNEL);
	if (!si)
		return -1;

	si->dev = ip_dev;
	si->name = ip_name;
	si->cb = NULL;
	atomic_set(&si->use_cnt, 0);
	list_add(&si->node, &subip_list);

	pr_info("%s: %s(%p) registered\n", __func__, ip_name, ip_dev);

	return 0;
}

int lpass_set_gpio_cb(struct device *ip_dev, void (*ip_cb)(struct device *dev))
{
	struct subip_info *si;

	list_for_each_entry(si, &subip_list, node) {
		if (si->dev == ip_dev) {
			si->cb = ip_cb;
			pr_info("%s: %s(cb: %p)\n", __func__,
				si->name, si->cb);
			return 0;
		}
	}

	return -EINVAL;
}

void lpass_get_sync(struct device *ip_dev)
{
	struct subip_info *si;

	list_for_each_entry(si, &subip_list, node) {
		if (si->dev == ip_dev) {
			atomic_inc(&si->use_cnt);
			atomic_inc(&lpass.use_cnt);
			dev_dbg(ip_dev, "%s: %s (use:%d)\n", __func__,
				si->name, atomic_read(&si->use_cnt));
			pm_runtime_get_sync(&lpass.pdev->dev);
		}
	}

	lpass_update_qos();
}

void lpass_put_sync(struct device *ip_dev)
{
	struct subip_info *si;

	list_for_each_entry(si, &subip_list, node) {
		if (si->dev == ip_dev) {
			atomic_dec(&si->use_cnt);
			atomic_dec(&lpass.use_cnt);
			dev_dbg(ip_dev, "%s: %s (use:%d)\n", __func__,
				si->name, atomic_read(&si->use_cnt));
			pm_runtime_put_sync(&lpass.pdev->dev);
		}
	}

	lpass_update_qos();
}

#ifdef USE_EXYNOS_AUD_SCHED
void lpass_set_sched(pid_t pid, int mode)
{
#ifdef USE_AUD_TASK_RT
	struct sched_param param_fifo = {.sched_priority = MAX_RT_PRIO >> 1};
	struct task_struct *task = find_task_by_vpid(pid);
#endif

	switch (mode) {
	case AUD_MODE_UHQA:
		lpass.uhqa_on = true;
		break;
	case AUD_MODE_NORM:
		lpass.uhqa_on = false;
		break;
	default:
		break;
	}

	lpass_update_qos();

#ifdef USE_AUD_TASK_RT
	if (task) {
		sched_setscheduler_nocheck(task,
				SCHED_RR | SCHED_RESET_ON_FORK, &param_fifo);
		pr_info("%s: [%s] pid = %d, prio = %d\n",
				__func__, task->comm, pid, task->prio);
	} else {
		pr_err("%s: task not found (pid = %d)\n",
				__func__, pid);
	}
#endif
}
#endif

void lpass_add_stream(void)
{
	atomic_inc(&lpass.stream_cnt);
	lpass_update_qos();
}

void lpass_remove_stream(void)
{
	atomic_dec(&lpass.stream_cnt);
	lpass_update_qos();
}

void lpass_set_fm_bt_mux(int is_fm)
{
	regmap_update_bits(lpass.pmureg,
			EXYNOS7880_AUD_PATH_CFG,
			AUD_PATH_CFG_BT_MUX_CON_MASK,
			is_fm ? AUD_PATH_CFG_BT_MUX_CON_MASK : 0);
}

static void lpass_reg_save(void)
{
	struct aud_reg *ar;

	list_for_each_entry(ar, &reg_list, node)
		ar->val = readl(ar->reg);

	return;
}

static void lpass_reg_restore(void)
{
	struct aud_reg *ar;

	list_for_each_entry(ar, &reg_list, node)
		writel(ar->val, ar->reg);

	return;
}

static void lpass_retention_pad(void)
{
	struct subip_info *si;

	/* Powerdown mode for gpio */
	list_for_each_entry(si, &subip_list, node) {
		if (si->cb != NULL)
			(*si->cb)(si->dev);
	}

	/* Set PAD retention */
	lpass_retention_pad_reg();
}

static void lpass_release_pad(void)
{
	struct subip_info *si;

	/* Restore gpio */
	list_for_each_entry(si, &subip_list, node) {
		if (si->cb != NULL)
			(*si->cb)(si->dev);
	}

	/* Release PAD retention */
	lpass_release_pad_reg();
}

static void lpass_enable(struct device *dev)
{
	static unsigned int count;

	dev_dbg(dev, "%s (count = %d)\n", __func__, ++count);

	if (!lpass.valid) {
		dev_err(dev, "%s: LPASS is not available", __func__);
		return;
	}

	/* Enable PLL */
	lpass_enable_pll(true);

	lpass_reg_restore();

	/* PLL path */
	lpass_set_mux_pll();

	/* Reset blocks inside audio sub-system as they are just powered-on */
	lpass_reset_toggle(LPASS_IP_MIXER);
	lpass_reset_toggle(LPASS_IP_AMP);
	lpass_reset_toggle(LPASS_IP_I2S);
	lpass_reset_toggle(LPASS_IP_DMA);

	/* PAD */
	lpass_release_pad();

	lpass.enabled = true;
}

static void lpass_disable(struct device *dev)
{
	static unsigned int count;

	dev_dbg(dev, "%s (count = %d)\n", __func__, ++count);

	if (!lpass.valid) {
		dev_err(dev, "%s: LPASS is not available", __func__);
		return;
	}

	lpass.enabled = false;

	/* PAD */
	lpass_retention_pad();

	lpass_reg_save();

	/* OSC path */
	lpass_set_mux_osc();

	/* Disable PLL */
	lpass_enable_pll(false);
}

static void lpass_add_suspend_reg(void __iomem *reg)
{
	struct device *dev = &lpass.pdev->dev;
	struct aud_reg *ar;

	ar = devm_kzalloc(dev, sizeof(struct aud_reg), GFP_KERNEL);
	if (!ar)
		return;

	ar->reg = reg;
	list_add(&ar->node, &reg_list);
}

static void lpass_init_reg_list(void)
{
	int n = 0;

	do {
		if (lpass_cmu_save[n] == NULL)
			break;

		lpass_add_suspend_reg(lpass_cmu_save[n]);
	} while (++n);
}

static int lpass_proc_show(struct seq_file *m, void *v)
{
	struct subip_info *si;
	int pmode = exynos_check_aud_pwr();

	seq_printf(m, "power: %s\n", lpass.enabled ? "on" : "off");
	seq_printf(m, "canbe: %s\n",
			(pmode == AUD_PWR_SLEEP) ? "sleep" :
			(pmode == AUD_PWR_LPA) ? "lpa" :
			(pmode == AUD_PWR_ALPA) ? "alpa" :
			(pmode == AUD_PWR_AFTR) ? "aftr" : "unknown");

	list_for_each_entry(si, &subip_list, node) {
		seq_printf(m, "subip: %s (%d)\n",
				si->name, atomic_read(&si->use_cnt));
	}

	seq_printf(m, "strm: %d\n", atomic_read(&lpass.stream_cnt));
	seq_printf(m, "uhqa: %s\n", lpass.uhqa_on ? "on" : "off");
	return 0;
}

static int lpass_proc_open(struct inode *inode, struct  file *file)
{
	return single_open(file, lpass_proc_show, NULL);
}

static const struct file_operations lpass_proc_fops = {
	.owner = THIS_MODULE,
	.open = lpass_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

#ifdef CONFIG_PM_SLEEP
static int lpass_suspend(struct device *dev)
{
	/*
	 * LPASS should be on during call, need to supply clocks to
	 * codec and mixer, so if call is in progress exit from suspend.
	 */
	if (is_cp_aud_enabled()) {
		dev_info(dev, "Audio block active, don't suspend\n");
		return 0;
	}

#ifdef CONFIG_PM_RUNTIME
	if (atomic_read(&lpass.use_cnt) > 0)
		lpass_disable(dev);
#else
	lpass_disable(dev);
#endif
	exynos_update_ip_idle_status(lpass.idle_ip_index, 1);
	return 0;
}

static int lpass_resume(struct device *dev)
{
	/*
	 * LPASS was left enabled during CP call. If it is already on, no need
	 * to do anything here.
	 */
	if (lpass.enabled)
		return 0;

	exynos_update_ip_idle_status(lpass.idle_ip_index, 0);
#ifdef CONFIG_PM_RUNTIME
	if (atomic_read(&lpass.use_cnt) > 0)
		lpass_enable(dev);
#else
	lpass_enable(dev);
#endif
	return 0;
}
#else
#define lpass_suspend NULL
#define lpass_resume  NULL
#endif

static void lpass_update_qos(void)
{
#ifdef USE_AUD_DEVFREQ
	int cpu_qos_new, kfc_qos_new, mif_qos_new, int_qos_new;

	if (!lpass.enabled) {
		cpu_qos_new = 0;
		kfc_qos_new = 0;
		mif_qos_new = 0;
		int_qos_new = 0;
	} else if (lpass.uhqa_on) {
		cpu_qos_new = AUD_CPU_FREQ_UHQA;
		kfc_qos_new = AUD_KFC_FREQ_UHQA;
		mif_qos_new = AUD_MIF_FREQ_UHQA;
		int_qos_new = AUD_INT_FREQ_UHQA;
	} else if (atomic_read(&lpass.stream_cnt) > 1) {
		cpu_qos_new = AUD_CPU_FREQ_HIGH;
		kfc_qos_new = AUD_KFC_FREQ_HIGH;
		mif_qos_new = AUD_MIF_FREQ_HIGH;
		int_qos_new = AUD_INT_FREQ_HIGH;
	} else {
		cpu_qos_new = AUD_CPU_FREQ_NORM;
		kfc_qos_new = AUD_KFC_FREQ_NORM;
		mif_qos_new = AUD_MIF_FREQ_NORM;
		int_qos_new = AUD_INT_FREQ_NORM;
	}

	if (lpass.cpu_qos != cpu_qos_new) {
		lpass.cpu_qos = cpu_qos_new;
		pm_qos_update_request(&lpass.aud_cluster1_qos, lpass.cpu_qos);
		pr_debug("%s: cpu_qos = %d\n", __func__, lpass.cpu_qos);
	}

	if (lpass.kfc_qos != kfc_qos_new) {
		lpass.kfc_qos = kfc_qos_new;
		pm_qos_update_request(&lpass.aud_cluster0_qos, lpass.kfc_qos);
		pr_debug("%s: kfc_qos = %d\n", __func__, lpass.kfc_qos);
	}

	if (lpass.mif_qos != mif_qos_new) {
		lpass.mif_qos = mif_qos_new;
		pm_qos_update_request(&lpass.aud_mif_qos, lpass.mif_qos);
		pr_debug("%s: mif_qos = %d\n", __func__, lpass.mif_qos);
	}

	if (lpass.int_qos != int_qos_new) {
		lpass.int_qos = int_qos_new;
		pm_qos_update_request(&lpass.aud_int_qos, lpass.int_qos);
		pr_debug("%s: int_qos = %d\n", __func__, lpass.int_qos);
	}
#endif
}

static char banner[] = KERN_INFO "Samsung Audio Subsystem driver\n";

static int lpass_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct device *dev = &pdev->dev;
	int ret = 0;

	printk(banner);

	lpass.pdev = pdev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "Unable to get LPASS SFRs\n");
		return -ENXIO;
	}

	lpass.regs = ioremap(res->start, res->end);
	if (!lpass.regs) {
		dev_err(dev, "SFR ioremap failed\n");
		return -ENOMEM;
	}

	ret = lpass_set_clk_hierarchy(&pdev->dev);
	if (ret) {
		dev_err(dev, "failed to set clock hierachy\n");
		return -ENXIO;
	}

	lpass.proc_file = proc_create("driver/lpass", 0,
					NULL, &lpass_proc_fops);
	if (!lpass.proc_file)
		pr_info("Failed to register /proc/driver/lpadd\n");

	spin_lock_init(&lpass.lock);
	atomic_set(&lpass.use_cnt, 0);
	atomic_set(&lpass.stream_cnt, 0);

	lpass_init_reg_list();

	lpass.idle_ip_index = exynos_get_idle_ip_index(dev_name(&pdev->dev));
	if (lpass.idle_ip_index < 0)
		dev_err(dev, "Idle ip index is not provided for Audio.\n");
#ifdef CONFIG_PM_RUNTIME
	pm_runtime_enable(&lpass.pdev->dev);

	pm_runtime_get_sync(&lpass.pdev->dev);
#else
	exynos_update_ip_idle_status(lpass.idle_ip_index, 0);
	lpass_enable(&lpass.pdev->dev);
#endif

	lpass_reg_save();
	lpass.valid = true;

	lpass.display_on = true;
	lpass.pmureg = syscon_regmap_lookup_by_phandle(dev->of_node,
			"samsung,syscon-phandle");
	if (IS_ERR(lpass.pmureg)) {
		dev_err(&pdev->dev, "syscon regmap lookup failed.\n");
		return PTR_ERR(lpass.pmureg);
	}

#ifdef USE_AUD_DEVFREQ
	lpass.cpu_qos = 0;
	lpass.kfc_qos = 0;
	lpass.mif_qos = 0;
	lpass.int_qos = 0;
	pm_qos_add_request(&lpass.aud_cluster1_qos, PM_QOS_CLUSTER1_FREQ_MIN, 0);
	pm_qos_add_request(&lpass.aud_cluster0_qos, PM_QOS_CLUSTER0_FREQ_MIN, 0);
	pm_qos_add_request(&lpass.aud_mif_qos, PM_QOS_BUS_THROUGHPUT, 0);
	pm_qos_add_request(&lpass.aud_int_qos, PM_QOS_DEVICE_THROUGHPUT, 0);
#endif
	dev_dbg(dev, "%s Completed\n", __func__);
	return 0;
}

static int lpass_remove(struct platform_device *pdev)
{
#ifdef CONFIG_PM_RUNTIME
	pm_runtime_disable(&pdev->dev);
#else
	lpass_disable(&pdev->dev);
	exynos_update_ip_idle_status(lpass.idle_ip_index, 1);
#endif
	iounmap(lpass.regs);
	return 0;
}

#ifdef CONFIG_PM_RUNTIME
static int lpass_runtime_suspend(struct device *dev)
{
	lpass_disable(dev);

	exynos_update_ip_idle_status(lpass.idle_ip_index, 1);
	return 0;
}

static int lpass_runtime_resume(struct device *dev)
{
	exynos_update_ip_idle_status(lpass.idle_ip_index, 0);
	lpass_enable(dev);

	return 0;
}
#endif

static const int lpass_ver_data[] = {
	[LPASS_VER_7870] = LPASS_VER_7870,
};

static struct platform_device_id lpass_driver_ids[] = {
	{
		.name	= "samsung-lpass",
	}, {},
};
MODULE_DEVICE_TABLE(platform, lpass_driver_ids);

#ifdef CONFIG_OF
static const struct of_device_id exynos_lpass_match[] = {
	{
		.compatible	= "samsung,exynos7870-lpass",
		.data		= &lpass_ver_data[LPASS_VER_7870],
	}, {},
};
MODULE_DEVICE_TABLE(of, exynos_lpass_match);
#endif

static const struct dev_pm_ops lpass_pmops = {
	SET_SYSTEM_SLEEP_PM_OPS(
		lpass_suspend,
		lpass_resume
	)
	SET_RUNTIME_PM_OPS(
		lpass_runtime_suspend,
		lpass_runtime_resume,
		NULL
	)
};

static struct platform_driver lpass_driver = {
	.probe		= lpass_probe,
	.remove		= lpass_remove,
	.id_table	= lpass_driver_ids,
	.driver		= {
		.name	= "samsung-lpass",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(exynos_lpass_match),
		.pm	= &lpass_pmops,
	},
};

static int __init lpass_driver_init(void)
{
	return platform_driver_register(&lpass_driver);
}
subsys_initcall(lpass_driver_init);

#ifdef CONFIG_PM_RUNTIME
static int lpass_driver_rpm_begin(void)
{
	pr_debug("%s entered\n", __func__);

	pm_runtime_put_sync(&lpass.pdev->dev);

	return 0;
}
late_initcall(lpass_driver_rpm_begin);
#endif

/* Module information */
MODULE_AUTHOR("Divya Jaiswal <divya.jswl@samsung.com>");
MODULE_AUTHOR("Chandrasekar R <rcsekar@samsung.com>");
MODULE_LICENSE("GPL");
