/*
 * Exynos FMP MMC driver for FIPS
 *
 * Copyright (C) 2014 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/syscalls.h>
#include <linux/file.h>
#include <linux/buffer_head.h>
#include <linux/genhd.h>
#include <linux/delay.h>
#include <linux/mtd/mtd.h>
#include <linux/kdev_t.h>
#include <linux/smc.h>
#include <linux/mmc/dw_mmc.h>

#include "fmpdev_int.h"

#if defined(CONFIG_MMC_DW_FMP_ECRYPT_FS)
#include "fmp_derive_iv.h"
#endif
#if defined(CONFIG_FIPS_FMP)
#include "fmpdev_info.h"
#endif

#define byte2word(b0, b1, b2, b3) 	\
		((unsigned int)(b0) << 24) | ((unsigned int)(b1) << 16) | ((unsigned int)(b2) << 8) | (b3)
#define get_word(x, c)	byte2word(((unsigned char *)(x) + 4 * (c))[0], ((unsigned char *)(x) + 4 * (c))[1], \
			((unsigned char *)(x) + 4 * (c))[2], ((unsigned char *)(x) + 4 * (c))[3])

#define SF_BLK_OFFSET	(5)
#define MAX_SCAN_PART	(50)
#define MAX_RETRY_COUNT	(0x100000)

struct mmc_fmp_work {
	struct dw_mci *host;
	struct block_device *bdev;
	sector_t sector;
	dev_t devt;
};

struct idmac_desc_64addr *desc;
struct idmac_desc_64addr *desc_st;

static int mmc_fmp_init(struct device *dev, uint32_t mode)
{
	struct mmc_fmp_work *work;
	struct dw_mci *host;

	work = dev_get_drvdata(dev);
	if (!work) {
		dev_err(dev, "Fail to get work from platform device\n");
		return -ENODEV;
	}
	host = work->host;

	desc_st = kmalloc(sizeof(struct idmac_desc_64addr), GFP_KERNEL);
	if (!desc_st) {
		dev_err(dev, "Fail to alloc descriptor for self test\n");
		return -ENOMEM;
	}
	host->desc_st = desc_st;

	return 0;
}

static int mmc_fmp_set_key(struct device *dev, uint32_t mode, uint8_t *key, uint32_t key_len)
{
	struct mmc_fmp_work *work;
	struct dw_mci *host;
	struct idmac_desc_64addr *desc;

	work = dev_get_drvdata(dev);
	if (!work) {
		dev_err(dev, "Fail to get work from platform device\n");
		return -ENODEV;
	}

	host = work->host;
	desc = host->desc_st;

	if (mode == CBC_MODE) {
		IDMAC_SET_FAS(desc, CBC_MODE);

		switch (key_len) {
		case 32:
			desc->des2 = 32;
			/* encrypt key */
			desc->des12 = get_word(key, 7);
			desc->des13 = get_word(key, 6);
			desc->des14 = get_word(key, 5);
			desc->des15 = get_word(key, 4);
			desc->des16 = get_word(key, 3);
			desc->des17 = get_word(key, 2);
			desc->des18 = get_word(key, 1);
			desc->des19 = get_word(key, 0);
			break;
		case 16:
			desc->des2 = 16;
			/* encrypt key */
			desc->des12 = get_word(key, 3);
			desc->des13 = get_word(key, 2);
			desc->des14 = get_word(key, 1);
			desc->des15 = get_word(key, 0);
			desc->des16 = 0;
			desc->des17 = 0;
			desc->des18 = 0;
			desc->des19 = 0;
			break;
		default:
			dev_err(dev, "Invalid key length : %d\n", key_len);
			return -EINVAL;
		}
	} else if (mode == XTS_MODE) {
		IDMAC_SET_FAS(desc, XTS_MODE);

		switch (key_len) {
		case 64:
			desc->des2 = 32;
			/* encrypt key */
			desc->des12 = get_word(key, 7);
			desc->des13 = get_word(key, 6);
			desc->des14 = get_word(key, 5);
			desc->des15 = get_word(key, 4);
			desc->des16 = get_word(key, 3);
			desc->des17 = get_word(key, 2);
			desc->des18 = get_word(key, 1);
			desc->des19 = get_word(key, 0);

			/* tweak key */
			desc->des20 = get_word(key, 15);
			desc->des21 = get_word(key, 14);
			desc->des22 = get_word(key, 13);
			desc->des23 = get_word(key, 12);
			desc->des24 = get_word(key, 11);
			desc->des25 = get_word(key, 10);
			desc->des26 = get_word(key, 9);
			desc->des27 = get_word(key, 8);

			break;
		case 32:
			desc->des2 = 16;
			/* encrypt key */
			desc->des12 = get_word(key, 3);
			desc->des13 = get_word(key, 2);
			desc->des14 = get_word(key, 1);
			desc->des15 = get_word(key, 0);
			desc->des16 = 0;
			desc->des17 = 0;
			desc->des18 = 0;
			desc->des19 = 0;

			/* tweak key */
			desc->des20 = get_word(key, 7);
			desc->des21 = get_word(key, 6);
			desc->des22 = get_word(key, 5);
			desc->des23 = get_word(key, 4);
			desc->des24 = 0;
			desc->des25 = 0;
			desc->des26 = 0;
			desc->des27 = 0;

			break;
		default:
			dev_err(dev, "Invalid key length : %d\n", key_len);
			return -EINVAL;
		}
	} else if (mode == BYPASS_MODE) {
		IDMAC_SET_FAS(desc, BYPASS_MODE);

		/* enc key */
		desc->des12 = 0;
		desc->des13 = 0;
		desc->des14 = 0;
		desc->des15 = 0;
		desc->des16 = 0;
		desc->des17 = 0;
		desc->des18 = 0;
		desc->des19 = 0;

		/* tweak key */
		desc->des20 = 0;
		desc->des21 = 0;
		desc->des22 = 0;
		desc->des23 = 0;
		desc->des24 = 0;
		desc->des25 = 0;
		desc->des26 = 0;
		desc->des27 = 0;
	} else {
		dev_err(dev, "Invalid mode : %d\n", mode);
		return -EINVAL;
	}

	return 0;
}

static int mmc_fmp_set_iv(struct device *dev, uint32_t mode, uint8_t *iv, uint32_t iv_len)
{
	struct mmc_fmp_work *work;
	struct dw_mci *host;
	struct idmac_desc_64addr *desc;

	work = dev_get_drvdata(dev);
	if (!work) {
		dev_err(dev, "Fail to get work from platform device\n");
		return -ENODEV;
	}

	host = work->host;
	desc = host->desc_st;

	if (mode == CBC_MODE || mode == XTS_MODE) {
		desc->des8 = get_word(iv, 3);
		desc->des9 = get_word(iv, 2);
		desc->des10 = get_word(iv, 1);
		desc->des11 = get_word(iv, 0);
	} else if (mode == BYPASS_MODE) {
		desc->des8 = 0;
		desc->des9 = 0;
		desc->des10 = 0;
		desc->des11 = 0;
	} else {
		dev_err(dev, "Invalid mode : %d\n", mode);
		return -EINVAL;
	}

	return 0;
}

static dev_t find_devt_for_selftest(struct device *dev)
{
	int i, idx = 0;
	uint32_t count = 0;
	uint64_t size;
	uint64_t size_list[MAX_SCAN_PART];
	dev_t devt_list[MAX_SCAN_PART];
	dev_t devt_scan, devt;
	struct block_device *bdev;
	fmode_t fmode = FMODE_WRITE | FMODE_READ;

	memset(size_list, 0, sizeof(size_list));
	memset(devt_list, 0, sizeof(devt_list));

	do {
		for (i = 1; i < MAX_SCAN_PART; i++) {
			devt_scan = blk_lookup_devt("mmcblk0", i);
			bdev = blkdev_get_by_dev(devt_scan, fmode, NULL);
			if (IS_ERR(bdev))
				continue;
			else {
				size_list[idx] = (uint64_t)i_size_read(bdev->bd_inode);
				devt_list[idx++] = devt_scan;
				blkdev_put(bdev, fmode);
			}
		}

		if (!idx) {
			mdelay(100);
			count++;
			continue;
		}

		devt = (dev_t)0;
		for (i = 0; i < idx; i++) {
			if (i == 0) {
				size = size_list[i];
				devt = devt_list[i];
			} else {
				if (size < size_list[i])
					devt = devt_list[i];
			}
		}

		bdev = blkdev_get_by_dev(devt, fmode, NULL);
		dev_info(dev, "FMP fips driver found mmcblk0p%d for self-test\n", bdev->bd_part->partno);
		blkdev_put(bdev, fmode);

		return devt;
	} while (count < MAX_RETRY_COUNT);

	dev_err(dev, "Block device isn't initialized yet. It makes to fail FMP selftest\n");
	return (dev_t)0;
}

static int mmc_fmp_run(struct device *dev, uint32_t mode, uint8_t *data,
			uint32_t len, uint32_t write)
{
	int ret = 0;
	struct mmc_fmp_work *work;
	struct dw_mci *host;
	static struct buffer_head *bh;

	work = dev_get_drvdata(dev);
	if (!work) {
		dev_err(dev, "Fail to get work from platform device\n");
		return -ENODEV;
	}
	host = work->host;
	host->self_test_mode = mode;

	bh = __getblk(work->bdev, work->sector, FMP_BLK_SIZE);
	if (!bh) {
		dev_err(dev, "Fail to get block from bdev\n");
		return -ENODEV;
	}
	host->self_test_bh = bh;

	get_bh(bh);
	if (write == WRITE_MODE) {
		memcpy(bh->b_data, data, len);
		set_buffer_dirty(bh);
		sync_dirty_buffer(bh);
		if (buffer_req(bh) && !buffer_uptodate(bh)) {
			dev_err(dev, "IO error syncing for FMP fips write\n");
			ret = -EIO;
			goto out;
		}
		memset(bh->b_data, 0, FMP_BLK_SIZE);
	} else {
		lock_buffer(bh);
		bh->b_end_io = end_buffer_read_sync;
		submit_bh(READ_SYNC, bh);
		wait_on_buffer(bh);
		if (unlikely(!buffer_uptodate(bh))) {
			ret = -EIO;
			goto out;
		}
		memcpy(data, bh->b_data, len);
	}
out:
	host->self_test_mode = 0;
	host->self_test_bh = NULL;
	put_bh(bh);

	return ret;
}

static int mmc_fmp_exit(void)
{
	if (desc_st)
		kfree(desc_st);

	return 0;
}

struct fips_fmp_ops fips_fmp_fops = {
	.init = mmc_fmp_init,
	.set_key = mmc_fmp_set_key,
	.set_iv = mmc_fmp_set_iv,
	.run = mmc_fmp_run,
	.exit = mmc_fmp_exit,
};

int fips_fmp_init(struct device *dev)
{
	struct mmc_fmp_work *work;
	struct device_node *dev_node;
	struct platform_device *pdev_mmc;
	struct device *dev_mmc;
	struct dw_mci *host;
	struct inode *inode;
	struct super_block *sb;
	unsigned long blocksize;
	unsigned char blocksize_bits;

	sector_t self_test_block;
	fmode_t fmode = FMODE_WRITE | FMODE_READ;

	work = kmalloc(sizeof(*work), GFP_KERNEL);
	if (!work) {
		dev_err(dev, "Fail to alloc fmp work buffer\n");
		return -ENOMEM;
	}

	dev_node = of_find_compatible_node(NULL, NULL, "samsung,exynos-dw-mshc");
	if (!dev_node) {
		dev_err(dev, "Fail to find exynos mmc device node\n");
		goto out;
	}

	pdev_mmc = of_find_device_by_node(dev_node);
	if (!pdev_mmc) {
		dev_err(dev, "Fail to find exynos mmc pdev\n");
		goto out;
	}

	dev_mmc = &pdev_mmc->dev;
	host = dev_get_drvdata(dev_mmc);
	if (!host) {
		dev_err(dev, "Fail to find host from dev\n");
		goto out;
	}

	work->host = host;
	work->devt = find_devt_for_selftest(dev);
	if (!work->devt) {
		dev_err(dev, "Fail to find devt for self test\n");
		return -ENODEV;
	}

	work->bdev = blkdev_get_by_dev(work->devt, fmode, NULL);
	if (IS_ERR(work->bdev)) {
		dev_err(dev, "Fail to open block device\n");
		return -ENODEV;
	}
	inode = work->bdev->bd_inode;
	sb = inode->i_sb;
	blocksize = sb->s_blocksize;
	blocksize_bits = sb->s_blocksize_bits;
	self_test_block = (i_size_read(inode) - (blocksize * SF_BLK_OFFSET)) >> blocksize_bits;
	work->sector = self_test_block;

	dev_set_drvdata(dev, work);

	return 0;

out:
	if (work)
		kfree(work);

	return -ENODEV;
}
EXPORT_SYMBOL_GPL(fips_fmp_init);

void fips_fmp_exit(struct device *dev)
{
	struct mmc_fmp_work *work = dev_get_drvdata(dev);
	fmode_t fmode = FMODE_WRITE | FMODE_READ;

	if (!work)
		return;

	if (work->bdev)
		blkdev_put(work->bdev, fmode);
	kfree(work);

	return;
}
