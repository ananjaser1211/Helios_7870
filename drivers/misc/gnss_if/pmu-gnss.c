#include <linux/io.h>
#include <linux/cpumask.h>
#include <linux/suspend.h>
#include <linux/notifier.h>
#include <linux/bug.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/smc.h>
#include "pmu-gnss.h"

static void __init set_shdmem_size(struct gnss_ctl *gc, u32 memsz)
{
	u32 tmp;
	gif_err("[GNSS]Set shared mem size: %dB\n", memsz);

	memsz = (memsz >> 22);
#ifdef USE_IOREMAP_NOPMU
	{
		u32 memcfg_val;

		memcfg_val = __raw_readl(gc->pmu_reg + EXYNOS_PMU_GNSS2AP_MEM_CONFIG);
		memcfg_val &= ~(0x1ff << MEMSIZE_OFFSET);
		memcfg_val |= (memsz << MEMSIZE_OFFSET);
		__raw_writel(memcfg_val, gc->pmu_reg + EXYNOS_PMU_GNSS2AP_MEM_CONFIG);
		tmp = __raw_readl(gc->pmu_reg + EXYNOS_PMU_GNSS2AP_MEM_CONFIG);
	}
#else
	regmap_update_bits(gc->pmu_reg, EXYNOS_PMU_GNSS2AP_MEM_CONFIG,
			0x1ff << MEMSIZE_OFFSET, memsz << MEMSIZE_OFFSET);

	regmap_read(gc->pmu_reg, EXYNOS_PMU_GNSS2AP_MEM_CONFIG, &tmp);
#endif
}

static void __init set_shdmem_base(struct gnss_ctl *gc, u32 shmem_base)
{
	u32 tmp, base_addr;
	gif_err("[GNSS]Set shared mem baseaddr : 0x%x\n", shmem_base);

	base_addr = (shmem_base >> 22);

#ifdef USE_IOREMAP_NOPMU
	{
		u32 memcfg_val;
		printk(KERN_ERR "Access Reg : 0x%p", gc->pmu_reg + EXYNOS_PMU_GNSS2AP_MEM_CONFIG);

		memcfg_val = __raw_readl(gc->pmu_reg + EXYNOS_PMU_GNSS2AP_MEM_CONFIG);
		memcfg_val &= ~(0x3ff << MEMBASE_ADDR_OFFSET);
		memcfg_val |= (base_addr << MEMBASE_ADDR_OFFSET);
		__raw_writel(memcfg_val, gc->pmu_reg + EXYNOS_PMU_GNSS2AP_MEM_CONFIG);
		tmp = __raw_readl(gc->pmu_reg + EXYNOS_PMU_GNSS2AP_MEM_CONFIG);
	}
#else
	regmap_update_bits(gc->pmu_reg, EXYNOS_PMU_GNSS2AP_MEM_CONFIG,
			0x3fff << MEMBASE_ADDR_OFFSET,
			base_addr << MEMBASE_ADDR_OFFSET);
	regmap_read(gc->pmu_reg, EXYNOS_PMU_GNSS2AP_MEM_CONFIG, &tmp);
#endif
}

static void exynos_sys_powerdown_conf_gnss(struct gnss_ctl *gc)
{
#ifdef USE_IOREMAP_NOPMU
	__raw_writel(0, gc->pmu_reg + EXYNOS_PMU_CENTRAL_SEQ_GNSS_CONFIGURATION);
	__raw_writel(0, gc->pmu_reg + EXYNOS_PMU_RESET_AHEAD_GNSS_SYS_PWR_REG);
	__raw_writel(0, gc->pmu_reg + EXYNOS_PMU_CLEANY_BUS_SYS_PWR_REG);
	__raw_writel(0, gc->pmu_reg + EXYNOS_PMU_LOGIC_RESET_GNSS_SYS_PWR_REG);
	__raw_writel(0, gc->pmu_reg + EXYNOS_PMU_TCXO_GATE_GNSS_SYS_PWR_REG);
	__raw_writel(0, gc->pmu_reg + EXYNOS_PMU_RESET_ASB_GNSS_SYS_PWR_REG);
#else
	regmap_write(gc->pmu_reg, EXYNOS_PMU_CENTRAL_SEQ_GNSS_CONFIGURATION, 0);
	regmap_write(gc->pmu_reg, EXYNOS_PMU_RESET_AHEAD_GNSS_SYS_PWR_REG, 0);
	regmap_write(gc->pmu_reg, EXYNOS_PMU_CLEANY_BUS_SYS_PWR_REG, 0);
	regmap_write(gc->pmu_reg, EXYNOS_PMU_LOGIC_RESET_GNSS_SYS_PWR_REG, 0);
	regmap_write(gc->pmu_reg, EXYNOS_PMU_TCXO_GATE_GNSS_SYS_PWR_REG, 0);
	regmap_write(gc->pmu_reg, EXYNOS_PMU_RESET_ASB_GNSS_SYS_PWR_REG, 0);
#endif
}

int gnss_pmu_clear_interrupt(struct gnss_ctl *gc, enum gnss_int_clear gnss_int)
{
	int ret = 0;

	gif_debug("%s\n", __func__);
#ifdef USE_IOREMAP_NOPMU
	{
		u32 reg_val = 0;

		reg_val = __raw_readl(gc->pmu_reg + EXYNOS_PMU_GNSS_CTRL_NS);

		if (gnss_int == GNSS_INT_WAKEUP_CLEAR) {
			reg_val |= GNSS_WAKEUP_REQ_CLR;
		} else if (gnss_int == GNSS_INT_ACTIVE_CLEAR) {
			reg_val |= GNSS_ACTIVE_REQ_CLR;
		} else if (gnss_int == GNSS_INT_WDT_RESET_CLEAR) {
			reg_val |= GNSS_WAKEUP_REQ_CLR;
		} else {
			pr_err("Unexpected interrupt value!\n");
			return -EIO;
		}
		__raw_writel(reg_val, gc->pmu_reg + EXYNOS_PMU_GNSS_CTRL_NS);
	}
#else
	if (gnss_int == GNSS_INT_WAKEUP_CLEAR) {
		ret = regmap_update_bits(gc->pmu_reg, EXYNOS_PMU_GNSS_CTRL_NS,
				GNSS_WAKEUP_REQ_CLR, GNSS_WAKEUP_REQ_CLR);
	} else if (gnss_int == GNSS_INT_ACTIVE_CLEAR) {
		ret = regmap_update_bits(gc->pmu_reg, EXYNOS_PMU_GNSS_CTRL_NS,
				GNSS_ACTIVE_REQ_CLR, GNSS_ACTIVE_REQ_CLR);
	} else if (gnss_int == GNSS_INT_WDT_RESET_CLEAR) {
		ret = regmap_update_bits(gc->pmu_reg, EXYNOS_PMU_GNSS_CTRL_NS,
				GNSS_RESET_REQ_CLR, GNSS_RESET_REQ_CLR);
	} else {
		pr_err("Unexpected interrupt value!\n");
		return -EIO;
	}

	if (ret < 0) {
		pr_err("%s: ERR! GNSS Reset Fail: %d\n", __func__, ret);
		return -EIO;
	}
#endif

	return ret;
}

int gnss_pmu_release_reset(struct gnss_ctl *gc)
{
	u32 gnss_ctrl = 0;
	int ret = 0;

#ifdef USE_IOREMAP_NOPMU
	gnss_ctrl = __raw_readl(gc->pmu_reg + EXYNOS_PMU_GNSS_CTRL_NS);
	{
		u32 tmp_reg_val;
		if (!(gnss_ctrl & GNSS_PWRON)) {
			gnss_ctrl |= GNSS_PWRON;
			__raw_writel(gnss_ctrl, gc->pmu_reg + EXYNOS_PMU_GNSS_CTRL_NS);
		}

		tmp_reg_val = __raw_readl(gc->pmu_reg + EXYNOS_PMU_GNSS_CTRL_S);
		tmp_reg_val |= GNSS_START;
		__raw_writel(tmp_reg_val, gc->pmu_reg + EXYNOS_PMU_GNSS_CTRL_S);

		printk(KERN_ERR "PMU_GNSS_CTRL_S : 0x%x\n",
				__raw_readl(gc->pmu_reg + EXYNOS_PMU_GNSS_CTRL_S));
	}
#else
	regmap_read(gc->pmu_reg, EXYNOS_PMU_GNSS_CTRL_NS, &gnss_ctrl);
	if (!(gnss_ctrl & GNSS_PWRON)) {
		ret = regmap_update_bits(gc->pmu_reg,
			EXYNOS_PMU_GNSS_CTRL_NS, GNSS_PWRON, GNSS_PWRON);
		if (ret < 0) {
			pr_err("%s: ERR! write Fail: %d\n",
					__func__, ret);
			ret = -EIO;
		}
	}
	ret = regmap_update_bits(gc->pmu_reg, EXYNOS_PMU_GNSS_CTRL_S,
			GNSS_START, GNSS_START);
	if (ret < 0)
		pr_err("ERR! GNSS Release Fail: %d\n", ret);
	else {
		regmap_read(gc->pmu_reg, EXYNOS_PMU_GNSS_CTRL_NS, &gnss_ctrl);
		pr_info("%s, PMU_GNSS_CTRL_S[0x%08x]\n", __func__, gnss_ctrl);
		ret = -EIO;
	}
#endif

	return ret;
}

int gnss_pmu_hold_reset(struct gnss_ctl *gc)
{
	int ret = 0;
	u32 __maybe_unused gnss_ctrl;

	/* set sys_pwr_cfg registers */
	exynos_sys_powerdown_conf_gnss(gc);

#ifdef USE_IOREMAP_NOPMU
	{
		u32 reg_val;
		reg_val = __raw_readl(gc->pmu_reg + EXYNOS_PMU_GNSS_CTRL_NS);
		reg_val |= GNSS_RESET_SET;
		__raw_writel(reg_val, gc->pmu_reg + EXYNOS_PMU_GNSS_CTRL_NS);
	}
#else
	ret = regmap_update_bits(gc->pmu_reg, EXYNOS_PMU_GNSS_CTRL_NS,
			GNSS_RESET_SET, GNSS_RESET_SET);
	if (ret < 0) {
		pr_err("%s: ERR! GNSS Reset Fail: %d\n", __func__, ret);
		return -1;
	}
#endif

	/* some delay */
	cpu_relax();
	usleep_range(80, 100);

	return ret;
}

int gnss_pmu_power_on(struct gnss_ctl *gc, enum gnss_mode mode)
{
	u32 gnss_ctrl;
	int ret = 0;

	gif_err("[GNSS] %s: mode[%d]\n", __func__, mode);

#ifdef USE_IOREMAP_NOPMU
	gnss_ctrl = __raw_readl(gc->pmu_reg + EXYNOS_PMU_GNSS_CTRL_NS);
	if (mode == GNSS_POWER_ON) {
		u32 tmp_reg_val;
		if (!(gnss_ctrl & GNSS_PWRON)) {
			gnss_ctrl |= GNSS_PWRON;
			__raw_writel(gnss_ctrl, gc->pmu_reg + EXYNOS_PMU_GNSS_CTRL_NS);
		}

		tmp_reg_val = __raw_readl(gc->pmu_reg + EXYNOS_PMU_GNSS_CTRL_S);
		tmp_reg_val |= GNSS_START;
		__raw_writel(tmp_reg_val, gc->pmu_reg + EXYNOS_PMU_GNSS_CTRL_S);
	} else {
		printk(KERN_ERR "%s : Not supported!!!(%d)\n", __func__, mode);
		return -1;
	}
#else
	regmap_read(gc->pmu_reg, EXYNOS_PMU_GNSS_CTRL_NS, &gnss_ctrl);
	if (mode == GNSS_POWER_ON) {
		if (!(gnss_ctrl & GNSS_PWRON)) {
			ret = regmap_update_bits(gc->pmu_reg,
				EXYNOS_PMU_GNSS_CTRL_NS, GNSS_PWRON, GNSS_PWRON);
			if (ret < 0)
				pr_err("%s: ERR! write Fail: %d\n",
						__func__, ret);
		}

		ret = regmap_update_bits(gc->pmu_reg,
			EXYNOS_PMU_GNSS_CTRL_S, GNSS_START, GNSS_START);
		if (ret < 0)
			pr_err("%s: ERR! write Fail: %d\n", __func__, ret);
	} else {
		ret = regmap_update_bits(gc->pmu_reg,
			EXYNOS_PMU_GNSS_CTRL_NS, GNSS_PWRON, 0);
		if (ret < 0) {
			pr_err("ERR! write Fail: %d\n", ret);
			return ret;
		}
		/* set sys_pwr_cfg registers */
		exynos_sys_powerdown_conf_gnss(gc);
	}
#endif

	return ret;
}

int gnss_change_tcxo_mode(struct gnss_ctl *gc, enum gnss_tcxo_mode mode)
{
	int ret = 0;

	if (mode == TCXO_SHARED_MODE) {
		gif_err("Change TCXO mode to Shared Mode(%d)\n", mode);
		ret = regmap_update_bits(gc->pmu_reg, EXYNOS_PMU_GNSS_CTRL_NS,
				TCXO_26M_40M_SEL, 0);
	} else if (mode == TCXO_NON_SHARED_MODE) {
		gif_err("Change TCXO mode to NON-sared Mode(%d)\n", mode);
		ret = regmap_update_bits(gc->pmu_reg, EXYNOS_PMU_GNSS_CTRL_NS,
				TCXO_26M_40M_SEL, TCXO_26M_40M_SEL);
	} else
		gif_err("Unexpected modem(Mode:%d)\n", mode);

	if (ret < 0) {
		pr_err("%s: ERR! GNSS change tcxo: %d\n", __func__, ret);
		return -1;
	}

	return 0;
}

int gnss_pmu_init_conf(struct gnss_ctl *gc)
{
	u32 shmem_size = gc->gnss_data->shmem_size;
	u32 shmem_base = gc->gnss_data->shmem_base;

	set_shdmem_size(gc, shmem_size);
	set_shdmem_base(gc, shmem_base);

#ifndef USE_IOREMAP_NOPMU
	/* set access window for GNSS */
	regmap_write(gc->pmu_reg, EXYNOS_PMU_GNSS2AP_MIF0_PERI_ACCESS_CON,
			0x0);
	regmap_write(gc->pmu_reg, EXYNOS_PMU_GNSS2AP_MIF1_PERI_ACCESS_CON,
			0x0);
	regmap_write(gc->pmu_reg, EXYNOS_PMU_GNSS2AP_PERI_ACCESS_WIN,
			0x0);
#endif

	return 0;
}

