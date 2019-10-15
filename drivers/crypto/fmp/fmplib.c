/*
 * Exynos FMP libary for FIPS
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
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/dma-mapping.h>
#include <linux/smc.h>

#include <asm/cacheflush.h>

#include "fmpdev_int.h"
#include "fmpdev.h"

#define BYPASS_MODE	0
#define CBC_MODE	1
#define XTS_MODE	2

#define INIT		0
#define UPDATE		1
#define FINAL		2

/*
 * FMP library to call FMP driver
 *
 * This file is part of linux fmpdev
 */

int fmpdev_cipher_init(struct fmp_info *info, struct cipher_data *out,
			const char *alg_name,
			uint8_t *enckey, uint8_t *twkey, size_t keylen)
{
	int ret;
	struct device *dev = info->dev;

	memset(out, 0, sizeof(*out));

	if (!strcmp(alg_name, "cbc(aes-fmp)"))
		out->mode = CBC_MODE;
	else if (!strcmp(alg_name, "xts(aes-fmp)"))
		out->mode = XTS_MODE;
	else {
		dev_err(dev, "Invalid mode\n");
		return -1;
	}

	out->blocksize = 16;
	out->ivsize = 16;
	ret = fips_fmp_cipher_init(dev, enckey, twkey, keylen, out->mode);
	if (ret) {
		dev_err(dev, "Fail to initialize fmp cipher\n");
		return -1;
	}

	out->init = 1;
	return 0;
}

void fmpdev_cipher_deinit(struct cipher_data *cdata)
{
	if (cdata->init)
		cdata->init = 0;
}

int fmpdev_cipher_set_iv(struct fmp_info *info, struct cipher_data *cdata,
			uint8_t *iv, size_t iv_size)
{
	int ret;
	struct device *dev = info->dev;

	ret = fips_fmp_cipher_set_iv(dev, iv, cdata->mode);
	if (ret) {
		dev_err(dev, "Fail to set fmp iv\n");
		return -1;
	}

	return 0;
}

int fmpdev_cipher_exit(struct fmp_info *info)
{
	int ret;
	struct device *dev = info->dev;

	ret = fips_fmp_cipher_exit(dev);
	if (ret) {
		dev_err(dev, "Fail to exit fmp\n");
		return -1;
	}

	return 0;
}

static int fmpdev_cipher_encrypt(struct fmp_info *info,
		struct cipher_data *cdata,
		struct scatterlist *src,
		struct scatterlist *dst, size_t len)
{
	int ret;
	struct device *dev = info->dev;

	ret = fips_fmp_cipher_run(dev, sg_virt(src), sg_virt(dst),
				len, cdata->mode, ENCRYPT);
	if (ret) {
		dev_err(dev, "Fail to encrypt using fmp\n");
		return -1;
	}

	return 0;
}

static int fmpdev_cipher_decrypt(struct fmp_info *info,
		struct cipher_data *cdata,
		struct scatterlist *src,
		struct scatterlist *dst, size_t len)
{
	int ret;
	struct device *dev = info->dev;

	ret = fips_fmp_cipher_run(dev, sg_virt(src), sg_virt(dst),
				len, cdata->mode, DECRYPT);
	if (ret) {
		dev_err(dev, "Fail to encrypt using fmp\n");
		return -1;
	}

	return 0;
}

int fmpdev_hash_init(struct fmp_info *info, struct hash_data *hdata,
			const char *alg_name,
			int hmac_mode, void *mackey, size_t mackeylen)
{
	int ret = -ENOMSG;
	struct device *dev = info->dev;

	hdata->init = 0;

	switch (hmac_mode) {
	case 0:
		hdata->sha = kzalloc(sizeof(*hdata->sha), GFP_KERNEL);
		if (!hdata->sha)
			return -ENOMEM;

		ret = sha256_init(hdata->sha);
		break;
	case 1:
		hdata->hmac = kzalloc(sizeof(*hdata->hmac), GFP_KERNEL);
		if (!hdata->hmac)
			return -ENOMEM;

		ret = hmac_sha256_init(hdata->hmac,
						mackey,
						mackeylen);
		break;
	default:
		dev_err(dev, "Wrong mode\n");
		return ret;
	}

	if (ret == 0)
		hdata->init = 1;

	return ret;
}

void fmpdev_hash_deinit(struct hash_data *hdata)
{
	if (hdata->hmac != NULL) {
		hmac_sha256_ctx_cleanup(hdata->hmac);
		kfree(hdata->hmac);
		hdata->init = 0;
		return;
	}

	if (hdata->sha != NULL) {
		memset(hdata->sha, 0x00, sizeof(*hdata->sha));
		kfree(hdata->sha);
		hdata->init = 0;
		return;
	}
}

ssize_t fmpdev_hash_update(struct fmp_info *info, struct hash_data *hdata,
				struct scatterlist *sg, size_t len)
{
	int ret = -ENOMSG;
	int8_t *buf;
	struct device *dev = info->dev;

	buf = kmalloc(len, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto exit;
	}

	if (len != sg_copy_to_buffer(sg, 1, buf, len)) {
		dev_err(dev, "%s: sg_copy_buffer error copying\n", __func__);
		goto exit;
	}

	if (hdata->sha != NULL)
		ret = sha256_update(hdata->sha, buf, len);
	else
		ret = hmac_sha256_update(hdata->hmac, buf, len);

exit:
	kfree(buf);
	return ret;
}

int fmpdev_hash_final(struct fmp_info *info, struct hash_data *hdata, void *output)
{
	int ret_crypto = 0; /* OK if zero */

	if (hdata->sha != NULL)
		ret_crypto = sha256_final(hdata->sha, output);
	else
		ret_crypto = hmac_sha256_final(hdata->hmac, output);

	if (ret_crypto != 0)
		return -ENOMSG;

	hdata->digestsize = 32;
	return 0;
}

static int fmp_n_crypt(struct fmp_info *info, struct csession *ses_ptr,
		struct crypt_op *cop,
		struct scatterlist *src_sg, struct scatterlist *dst_sg,
		uint32_t len)
{
	int ret;
	struct device *dev = info->dev;

	if (cop->op == COP_ENCRYPT) {
		if (ses_ptr->hdata.init != 0) {
			ret = fmpdev_hash_update(info, &ses_ptr->hdata,
								src_sg, len);
			if (unlikely(ret))
				goto out_err;
		}

		if (ses_ptr->cdata.init != 0) {
			ret = fmpdev_cipher_encrypt(info, &ses_ptr->cdata,
						src_sg, dst_sg, len);
			if (unlikely(ret))
				goto out_err;
		}
	} else {
		if (ses_ptr->cdata.init != 0) {
			ret = fmpdev_cipher_decrypt(info, &ses_ptr->cdata,
						src_sg, dst_sg, len);
			if (unlikely(ret))
				goto out_err;
		}

		if (ses_ptr->hdata.init != 0) {
			ret = fmpdev_hash_update(info, &ses_ptr->hdata,
								dst_sg, len);
			if (unlikely(ret))
				goto out_err;
		}
	}

	return 0;
out_err:
	dev_err(dev, "FMP crypt failure: %d\n", ret);

	return ret;
}

static int __fmp_run_std(struct fmp_info *info,
		struct csession *ses_ptr, struct crypt_op *cop)
{
	char *data;
	struct device *dev = info->dev;
	char __user *src, *dst;
	size_t nbytes, bufsize;
	struct scatterlist sg;
	int ret = 0;

	nbytes = cop->len;
	data = (char *)__get_free_page(GFP_KERNEL);
	if (unlikely(!data)) {
		dev_err(dev, "Error getting free page.\n");
		return -ENOMEM;
	}

	bufsize = PAGE_SIZE < nbytes ? PAGE_SIZE : nbytes;

	src = cop->src;
	dst = cop->dst;

	while (nbytes > 0) {
		size_t current_len = nbytes > bufsize ? bufsize : nbytes;

		if (unlikely(copy_from_user(data, src, current_len))) {
			dev_err(dev, "Error copying %d bytes from user address %p\n",
						(int)current_len, src);
			ret = -EFAULT;
			break;
		}

		sg_init_one(&sg, data, current_len);
		ret = fmp_n_crypt(info, ses_ptr, cop, &sg, &sg, current_len);
		if (unlikely(ret)) {
			dev_err(dev, "fmp_n_crypt failed\n");
			break;
		}

		if (ses_ptr->cdata.init != 0) {
			if (unlikely(copy_to_user(dst, data, current_len))) {
				dev_err(dev, "could not copy to user\n");
				ret = -EFAULT;
				break;
			}
		}

		dst += current_len;
		nbytes -= current_len;
		src += current_len;
	}
	free_page((unsigned long)data);

	return ret;
}


int fmp_run(struct fmp_info *info, struct fcrypt *fcr, struct kernel_crypt_op *kcop)
{
	struct device *dev = info->dev;
	struct csession *ses_ptr;
	struct crypt_op *cop = &kcop->cop;
	int ret = -EINVAL;

	if (unlikely(cop->op != COP_ENCRYPT && cop->op != COP_DECRYPT)) {
		dev_err(dev, "invalid operation op=%u\n", cop->op);
		return -EINVAL;
	}

	/* this also enters ses_ptr->sem */
	ses_ptr = fmp_get_session_by_sid(fcr, cop->ses);
	if (unlikely(!ses_ptr)) {
		dev_err(dev, "invalid session ID=0x%08X\n", cop->ses);
		return -EINVAL;
	}

	if ((ses_ptr->cdata.init != 0) && (cop->len > PAGE_SIZE)) {
		dev_err(dev, "Invalid input length. len = %d\n", cop->len);
		return -EINVAL;
	}

	if (ses_ptr->cdata.init != 0) {
		int blocksize = ses_ptr->cdata.blocksize;

		if (unlikely(cop->len % blocksize)) {
			dev_err(dev,
				"data size (%u) isn't a multiple "
				"of block size (%u)\n",
				cop->len, blocksize);
			ret = -EINVAL;
			goto out_unlock;
		}

		if (cop->flags == COP_FLAG_AES_CBC)
			fmpdev_cipher_set_iv(info, &ses_ptr->cdata, kcop->iv, 16);
		else if (cop->flags == COP_FLAG_AES_XTS)
			fmpdev_cipher_set_iv(info, &ses_ptr->cdata, (uint8_t *)&cop->data_unit_seqnumber, 16);
		else {
			ret = -EINVAL;
			goto out_unlock;
		}
	}

	if (likely(cop->len)) {
		ret = __fmp_run_std(info, ses_ptr, &kcop->cop);
		if (unlikely(ret))
			goto out_unlock;
	}

	if (ses_ptr->hdata.init != 0 &&
		((cop->flags & COP_FLAG_FINAL) ||
		   (!(cop->flags & COP_FLAG_UPDATE) || cop->len == 0))) {
		ret = fmpdev_hash_final(info, &ses_ptr->hdata, kcop->hash_output);
		if (unlikely(ret)) {
			dev_err(dev, "CryptoAPI failure: %d\n", ret);
			goto out_unlock;
		}
		kcop->digestsize = ses_ptr->hdata.digestsize;
	}

out_unlock:
	fmp_put_session(ses_ptr);
	return ret;
}

int fmp_run_AES_CBC_MCT(struct fmp_info *info, struct fcrypt *fcr,
			struct kernel_crypt_op *kcop)
{
	struct device *dev = info->dev;
	struct csession *ses_ptr;
	struct crypt_op *cop = &kcop->cop;
	char **Ct = 0;
	char **Pt = 0;
	int ret = 0, k = 0;
	char *data = NULL;

	if (unlikely(cop->op != COP_ENCRYPT && cop->op != COP_DECRYPT)) {
		dev_err(dev, "invalid operation op=%u\n", cop->op);
		return -EINVAL;
	}

	/* this also enters ses_ptr->sem */
	ses_ptr = fmp_get_session_by_sid(fcr, cop->ses);
	if (unlikely(!ses_ptr)) {
		dev_err(dev, "invalid session ID=0x%08X\n", cop->ses);
		return -EINVAL;
	}

	if (cop->len > PAGE_SIZE) {
		dev_err(dev, "Invalid input length. len = %d\n", cop->len);
		return -EINVAL;
	}

	if (ses_ptr->cdata.init != 0) {
		int blocksize = ses_ptr->cdata.blocksize;

		if (unlikely(cop->len % blocksize)) {
			dev_err(dev,
				"data size (%u) isn't a multiple "
				"of block size (%u)\n",
				cop->len, blocksize);
			ret = -EINVAL;
			goto out_unlock;
		}

		fmpdev_cipher_set_iv(info, &ses_ptr->cdata, kcop->iv, 16);
	}

	if (likely(cop->len)) {
		if (cop->flags & COP_FLAG_AES_CBC_MCT) {
		// do MCT here
	        char __user *src, *dst, *secondLast;
	        struct scatterlist sg;
	        size_t nbytes, bufsize;
	        int ret = 0;
	        int y = 0;

	        nbytes = cop->len;
	        data = (char *)__get_free_page(GFP_KERNEL);
		if (unlikely(!data)) {
			dev_err(dev, "Error getting free page.\n");
			return -ENOMEM;
		}

		Pt = (char**)kmalloc(1000 * sizeof(char*), GFP_KERNEL);
		if (!Pt) {
			ret = -ENOMEM;
			goto out_err_mem_data;
		}
		for (k=0; k<1000; k++) {
		       Pt[k]= (char*)kmalloc(nbytes, GFP_KERNEL);
		       if (!Pt[k]) {
				ret = -ENOMEM;
				goto out_err_mem_pt_k;
			}
		}

		Ct = (char**)kmalloc(1000 * sizeof(char*), GFP_KERNEL);
		if (!Ct) {
			ret = -ENOMEM;
			goto out_err_mem_ct;
		}
		for (k=0; k<1000; k++) {
			Ct[k]= (char*)kmalloc(nbytes, GFP_KERNEL);
			if (!Ct[k]) {
				ret = -ENOMEM;
				goto out_err_mem_ct_k;
			}
		}

	        bufsize = PAGE_SIZE < nbytes ? PAGE_SIZE : nbytes;

	        src = cop->src;
	        dst = cop->dst;
	        secondLast = cop->secondLastEncodedData;

		if (unlikely(copy_from_user(data, src, nbytes))) {
			printk(KERN_ERR "Error copying %d bytes from user address %p.\n", (int)nbytes, src);
		        ret = -EFAULT;
		        goto out_err_fail;
	        }

	        sg_init_one(&sg, data, nbytes);
		for (y = 0; y < 1000; y++) {
			memcpy(Pt[y], data, nbytes);
			ret = fmp_n_crypt(info, ses_ptr, cop, &sg, &sg, nbytes);
			memcpy(Ct[y], data, nbytes);

			if (y == 998) {
				if (unlikely(copy_to_user(secondLast, data, nbytes)))
					printk(KERN_ERR "unable to copy second last data for AES_CBC_MCT\n");
				else
					printk(KERN_ERR "KAMAL copied secondlast data\n");
			}

			if( y == 0) {
				memcpy(data, kcop->iv, kcop->ivlen);
			} else {
				if(y != 999)
					memcpy(data, Ct[y-1], nbytes);
			}

			if (unlikely(ret)) {
				printk(KERN_ERR "fmp_n_crypt failed.\n");
				ret = -EFAULT;
				goto out_err_fail;
			}

			if (cop->op == COP_ENCRYPT)
				fmpdev_cipher_set_iv(info, &ses_ptr->cdata, Ct[y], 16);
			else if (cop->op == COP_DECRYPT)
				fmpdev_cipher_set_iv(info, &ses_ptr->cdata, Pt[y], 16);
		} // for loop

		if (ses_ptr->cdata.init != 0) {
			if (unlikely(copy_to_user(dst, data, nbytes))) {
				printk(KERN_ERR "could not copy to user.\n");
				ret = -EFAULT;
				goto out_err_fail;
			}
		}

out_err_fail:
		for (k=0; k<1000; k++)
			kfree(Ct[k]);
out_err_mem_ct_k:
		kfree(Ct);
out_err_mem_ct:
		for (k=0; k<1000; k++)
			kfree(Pt[k]);
out_err_mem_pt_k:
		kfree(Pt);
out_err_mem_data:
		free_page((unsigned long)data);
		} else
			goto out_unlock;
	}
out_unlock:
	fmp_put_session(ses_ptr);
	return ret;
}
