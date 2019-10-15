/*
 * Exynos FMP MMC driver
 *
 * Copyright (C) 2015 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/mmc/dw_mmc.h>
#include <linux/smc.h>

#include "dw_mmc-exynos.h"

#if defined(CONFIG_FIPS_FMP)
#include "fmpdev_info.h"
#endif

#include "fmpdev_int.h" //For FIPS_FMP_FUNC_TEST macro

extern bool in_fmp_fips_err(void);
extern volatile unsigned int disk_key_flag;
extern spinlock_t disk_key_lock;

#define byte2word(b0, b1, b2, b3) 	\
		((unsigned int)(b0) << 24) | ((unsigned int)(b1) << 16) | ((unsigned int)(b2) << 8) | (b3)
#define word_in(x, c)	byte2word(((unsigned char *)(x) + 4 * (c))[0], ((unsigned char *)(x) + 4 * (c))[1], \
			((unsigned char *)(x) + 4 * (c))[2], ((unsigned char *)(x) + 4 * (c))[3])

#define FMP_KEY_SIZE    32
#define SHA256_HASH_SIZE 32

#if defined(CONFIG_FIPS_FMP)
static int fmp_xts_check_key(uint8_t *enckey, uint8_t *twkey, uint32_t len)
{
	if (!enckey | !twkey | !len) {
		printk(KERN_ERR "FMP key buffer is NULL or length is 0.\n");
		return -1;
	}

	if (!memcmp(enckey, twkey, len))
		return -1;      /* enckey and twkey are same */
	else
		return 0;       /* enckey and twkey are different */
}
#endif

int fmp_mmc_map_sg(struct dw_mci *host, struct idmac_desc_64addr *desc,
			uint32_t idx, uint32_t enc_mode, uint32_t sector,
			struct mmc_data *data, struct bio *bio)
{
	int ret;
	uint32_t size;

	if (!host || !desc || !data || !bio)
		return 0;

	size = desc->des2;
#if defined(CONFIG_FIPS_FMP)
	/* The length of XTS AES must be smaller than 4KB */
	if (size > 0x1000) {
		printk(KERN_ERR "Fail to FMP XTS due to invalid size(%x)\n",
				size);
		return -EINVAL;
	}
#endif

#if defined(CONFIG_MMC_DW_FMP_DM_CRYPT)
	/* Disk Encryption */
	if (!(enc_mode & MMC_FMP_DISK_ENC_MODE) ||
			!(host->pdata->quirks & DW_MCI_QUIRK_USE_SMU))
		goto file_enc;

	/* disk algorithm selector  */
	IDMAC_SET_DAS(desc, AES_XTS);
	desc->des2 |= IDMAC_DES2_DKL;

	/* Disk IV */
	desc->des28 = 0;
	desc->des29 = 0;
	desc->des30 = 0;
	desc->des31 = htonl(sector);

	if (disk_key_flag) {
		unsigned long flags;

#if defined(CONFIG_FIPS_FMP)
		if (!bio) {
			printk(KERN_ERR "Fail to check xts key due to bio\n");
			return -EINVAL;
		}

		if (fmp_xts_check_key(bio->key,
					(uint8_t *)((uint64_t)bio->key + FMP_KEY_SIZE), FMP_KEY_SIZE)) {
			printk(KERN_ERR "Fail to FMP XTS because enckey and twkey is the same\n");
			return -EINVAL;
		}
#endif
		if (disk_key_flag == 1)
			printk(KERN_INFO "FMP disk encryption key is set\n");
		else if (disk_key_flag == 2)
			printk(KERN_INFO "FMP disk encryption key is set after clear\n");
		ret = exynos_smc(SMC_CMD_FMP, FMP_KEY_SET, EMMC0_FMP, 0);
		if (ret < 0)
			panic("Fail to load FMP loadable firmware\n");
		else if (ret) {
			printk(KERN_ERR "Fail to smc call for FMP key setting(%x)\n", ret);
			return ret;
		}

		spin_lock_irqsave(&disk_key_lock, flags);
		disk_key_flag = 0;
		spin_unlock_irqrestore(&disk_key_lock, flags);
	}
#endif

file_enc:
        return 0;
}
EXPORT_SYMBOL_GPL(fmp_mmc_map_sg);

#if defined(CONFIG_FIPS_FMP)
int fmp_mmc_map_sg_st(struct dw_mci *host, struct idmac_desc_64addr *desc)
{
	struct idmac_desc_64addr *desc_st = host->desc_st;

	/* algorithm */
	if (host->self_test_mode == XTS_MODE) {
		if (desc->des2 > 0x1000) {
			printk(KERN_ERR "Fail to invalid size for FMP. size(%d)\n",
				desc->des2);
			return -EINVAL;
		}

		if (fmp_xts_check_key((uint8_t *)&desc_st->des12,
				(uint8_t *)&desc_st->des20, desc_st->des2)) {
			printk(KERN_ERR "Fail to FMP XTS because enckey and twkey is the same\n");
			return -EINVAL;
                }

		IDMAC_SET_FAS(desc, XTS_MODE);
	} else if (host->self_test_mode == CBC_MODE)
		IDMAC_SET_FAS(desc, CBC_MODE);
	else {
		IDMAC_SET_FAS(desc, 0);
		IDMAC_SET_DAS(desc, 0);
		return 0;
	}

	if (desc_st->des2 == 32)
		desc->des2 |= IDMAC_DES2_FKL;

	/* File IV */
	desc->des8 = desc_st->des8;
	desc->des9 = desc_st->des9;
	desc->des10 = desc_st->des10;
	desc->des11 = desc_st->des11;

	/* enc key */
	desc->des12 = desc_st->des12;
	desc->des13 = desc_st->des13;
	desc->des14 = desc_st->des14;
	desc->des15 = desc_st->des15;
	desc->des16 = desc_st->des16;
	desc->des17 = desc_st->des17;
	desc->des18 = desc_st->des18;
	desc->des19 = desc_st->des19;

	/* tweak key */
	desc->des20 = desc_st->des20;
	desc->des21 = desc_st->des21;
	desc->des22 = desc_st->des22;
	desc->des23 = desc_st->des23;
	desc->des24 = desc_st->des24;
	desc->des25 = desc_st->des25;
	desc->des26 = desc_st->des26;
	desc->des27 = desc_st->des27;

	return 0;
}
EXPORT_SYMBOL_GPL(fmp_mmc_map_sg_st);
#endif

#if defined(CONFIG_FIPS_FMP)
void fmp_mmc_clear_sg(struct idmac_desc_64addr *desc)
{
	if (!desc)
		return;

	if (!IDMAC_GET_FAS(desc))
		return;

#if FIPS_FMP_FUNC_TEST == 6 /* Key Zeroization */
	print_hex_dump(KERN_ERR, "FIPS FMP descriptor before zeroize: ",
			DUMP_PREFIX_NONE, 16, 1, &desc->des8,
			sizeof(__le32) * 20, false);
#endif
	memset(&desc->des8, 0, sizeof(__le32) * 20);
#if FIPS_FMP_FUNC_TEST == 6 /* Key Zeroization */
	print_hex_dump(KERN_ERR, "FIPS FMP descriptor after zeroize: ",
			DUMP_PREFIX_NONE, 16, 1, &desc->des8,
			sizeof(__le32) * 20, false);
#endif
	return;
}
EXPORT_SYMBOL_GPL(fmp_mmc_clear_sg);

int fmp_clear_disk_key(void)
{
	int ret;

	ret = exynos_smc(SMC_CMD_FMP, FMP_KEY_CLEAR, EMMC0_FMP, 0);
	if (ret) {
		printk(KERN_ERR "Fail to smc call for FMP key clear(%x)\n", ret);
		return ret;
	}

	return 0;
}
#endif
