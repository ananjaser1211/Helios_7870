/*
 * Perform FIPS Integrity test on Kernel Crypto API
 *
 * At build time, hmac(sha256) of crypto code, avaiable in different ELF sections
 * of vmlinux file, is generated. vmlinux file is updated with built-time hmac
 * in a read-only data variable, so that it is available at run-time
 *
 * At run time, hmac(sha256) is again calculated using crypto bytes of a running
 * At run time, hmac-fmp(sha256-fmp) is again calculated using crypto bytes of a running
 * kernel.
 * Run time hmac is compared to built time hmac to verify the integrity.
 *
 *
 * Author : Rohit Kothari (r.kothari@samsung.com)
 * Date	  : 11 Feb 2014
 *
 * Copyright (c) 2014 Samsung Electronics
 *
 */

#include <linux/kallsyms.h>
#include <linux/err.h>
#include <linux/scatterlist.h>
#include <linux/smc.h>

#include "hmac-sha256.h"
#include "fmpdev_int.h" //for FIPS_FMP_FUNC_TEST macro

#undef FIPS_DEBUG

/* Same as build time */
static const unsigned char *integrity_check_key = "The quick brown fox jumps over the lazy dog";

static const char *symtab[][3] = {
		{".text",	"first_fmp_text",	"last_fmp_text"  },
		{".rodata",	"first_fmp_rodata",	"last_fmp_rodata"},
		{".init.text",	"first_fmp_init",	"last_fmp_init"  } };

extern const char *get_builtime_fmp_hmac(void);

#ifdef FIPS_DEBUG
static int
dump_bytes(const char *section_name, const char *first_symbol, const char *last_symbol)
{
	u8 *start_addr = (u8 *)kallsyms_lookup_name(first_symbol);
	u8 *end_addr   = (u8 *)kallsyms_lookup_name(last_symbol);

	if (!start_addr || !end_addr || start_addr >= end_addr) {
		printk(KERN_ERR "FIPS(%s): Error Invalid Addresses in Section : %s, Start_Addr : %p , End_Addr : %p",
                       __FUNCTION__, section_name, start_addr, end_addr);
		return -1;
	}

	printk(KERN_INFO "FIPS CRYPTO RUNTIME : Section - %s, %s : %p, %s : %p \n", section_name, first_symbol, start_addr, last_symbol, end_addr);
	print_hex_dump_bytes("FIPS CRYPTO RUNTIME : ", DUMP_PREFIX_NONE, start_addr, end_addr - start_addr);

	return 0;
}
#endif

static int query_symbol_addresses(const char *first_symbol, const char *last_symbol,
				unsigned long *start_addr,unsigned long *end_addr)
{
	unsigned long start = kallsyms_lookup_name(first_symbol);
	unsigned long end = kallsyms_lookup_name(last_symbol);

#ifdef FIPS_DEBUG
	printk(KERN_INFO "FIPS CRYPTO RUNTIME : %s : %p, %s : %p\n", first_symbol, (u8*)start, last_symbol, (u8*)end);
#endif

	if (!start || !end || start >= end) {
		printk(KERN_ERR "FIPS(%s): Error Invalid Addresses.", __FUNCTION__);
		return -1;
	}

	*start_addr = start;
	*end_addr = end;

	return 0;
}

int do_fips_fmp_integrity_check(void)
{
	int i, rows, err;
	unsigned long start_addr = 0;
	unsigned long end_addr = 0;
	unsigned char runtime_hmac[32];
	struct hmac_sha256_ctx ctx;
	const char *builtime_hmac = 0;
	unsigned int size = 0;

	memset(runtime_hmac, 0x00, 32);

	err = hmac_sha256_init(&ctx, integrity_check_key, strlen(integrity_check_key));
	if (err) {
		printk(KERN_ERR "FIPS(%s): init_hash failed", __FUNCTION__);
		return -1;
	}

	rows = (unsigned int)sizeof(symtab) / sizeof(symtab[0]);

	for (i = 0; i < rows; i++) {
		err = query_symbol_addresses(symtab[i][1], symtab[i][2], &start_addr, &end_addr);
		if (err) {
			printk (KERN_ERR "FIPS(%s): Error to get start / end addresses", __FUNCTION__);
			return -1;
		}

#ifdef FIPS_DEBUG
		dump_bytes(symtab[i][0], symtab[i][1], symtab[i][2]);
#endif
		size = end_addr - start_addr;

		err = hmac_sha256_update(&ctx, (unsigned char *)start_addr, size);
		if (err) {
			printk(KERN_ERR "FIPS(%s): Error to update hash", __FUNCTION__);
			return -1;
		}
	}

#if FIPS_FMP_FUNC_TEST == 5
	pr_info("FIPS(%s): Failing Integrity Test\n", __func__);
	err = hmac_sha256_update(&ctx, (unsigned char *)start_addr, 1);
#endif

	err = hmac_sha256_final(&ctx, runtime_hmac);
	if (err) {
		printk(KERN_ERR "FIPS(%s): Error in finalize", __FUNCTION__);
		hmac_sha256_ctx_cleanup(&ctx);
		return -1;
	}

	hmac_sha256_ctx_cleanup(&ctx);
	builtime_hmac = get_builtime_fmp_hmac();
	if (!builtime_hmac) {
		printk(KERN_ERR "FIPS(%s): Unable to retrieve builtime_hmac", __FUNCTION__);
		return -1;
	}

#ifdef FIPS_DEBUG
	print_hex_dump_bytes("FIPS CRYPTO RUNTIME : runtime hmac  = ", DUMP_PREFIX_NONE, runtime_hmac, sizeof(runtime_hmac));
	print_hex_dump_bytes("FIPS CRYPTO RUNTIME : builtime_hmac = ", DUMP_PREFIX_NONE, builtime_hmac , sizeof(runtime_hmac));
#endif

	if (!memcmp(builtime_hmac, runtime_hmac, sizeof(runtime_hmac))) {
		printk(KERN_INFO "FIPS: Integrity Check Passed");
		return 0;
	} else {
		printk(KERN_ERR "FIPS(%s): Integrity Check Failed", __FUNCTION__);
		return -1;
	}

	return -1;
}

EXPORT_SYMBOL_GPL(do_fips_fmp_integrity_check);
