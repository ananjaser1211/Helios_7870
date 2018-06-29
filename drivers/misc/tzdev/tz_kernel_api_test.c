/*
 * Copyright (C) 2012-2016 Samsung Electronics, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include <asm/sizes.h>

#include <tzdev/kernel_api.h>
#include <tzdev/tzdev.h>

#include "sysdep.h"
#include "tzdev.h"
#include "tz_cdev.h"
#include "tz_kernel_api_test.h"

MODULE_AUTHOR("Oleksandr Targunakov <o.targunakov@samsung.com>");
MODULE_DESCRIPTION("Trustzone Kernel API test driver");
MODULE_LICENSE("GPL");

#define TZ_KAPI_DEVICE_NAME "tz_kapi_test"

#define IWSHMEM_PATTERN_SEND	0xaa
#define IWSHMEM_PATTERN_RECV	0x55

/* Timeout in seconds */
#define IWNOTIFY_TIMEOUT	5

enum {
	KAPI_SIMPLE_SEND_RECV_TEST,
	KAPI_IWSHMEM_TEST,
	KAPI_MULTIPLE_SEND_RECV_TEST,
	KAPI_MULTIPLE_SEND_RECV_IWSHMEM_TEST,
	KAPI_IWSHMEM_STRESS_TEST,
	KAPI_BIG_CONTIGUOUS_IWSHMEM_TEST,
	KAPI_MULTIPLE_BIG_CONTIGUOUS_IWSHMEM_TEST,
	KAPI_BIG_IWSHMEM_TEST,
	KAPI_MULTIPLE_BIG_IWSHMEM_TEST,
	KAPI_IWSHMEM_SWD_RESTART_TEST,
};

struct test_msg {
	uint32_t test_num;
	char str[128];
	uint32_t iwshmem_id;
	uint32_t result;
	uint32_t iwshmem_size;
} __packed;

struct test_private_data {
	void (*release_func)(struct test_private_data *);
	int shmem_id;
	struct page *page;
	unsigned int size;
};

static struct tz_uuid uuid = {
	.time_low = 0xc8c7fc92,
	.time_mid = 0x7496,
	.time_hi_and_version = 0x4d61,
	.clock_seq_and_node = {0x94, 0xad, 0x90, 0x9e, 0x49, 0x0d, 0xdc, 0x1f}
};

static DECLARE_COMPLETION(kcmd_event);

static int __kcmd_notifier(struct notifier_block *nb, unsigned long action, void *data)
{
	tzdev_print(0, "Complete kcmd_event\n");
	complete(&kcmd_event);
	return 0;
}

static struct notifier_block kcmd_notifier = {
	.notifier_call	= __kcmd_notifier,
};

static int memory_check_pattern(const void *ptr, size_t size, unsigned char pattern)
{
	size_t i = 0;
	const unsigned char *buf = ptr;

	for (i = 0; i < size; i++) {
		if (buf[i] != pattern) {
			tzdev_print(0, "memory_check_pattern(0x%p, 0x%lx, 0x%x) failed at pos %zu\n",
					ptr, (unsigned long)size, pattern, i);
			return -1;
		}
	}

	return 0;
}

static void iwshmem_free_pages(struct page **pages, unsigned int num_pages)
{
	unsigned int i;

	for (i = 0; i < num_pages; i++)
		__free_page(pages[i]);

	kfree(pages);
}

static struct page **iwshmem_alloc_pages(unsigned int num_pages)
{
	struct page **pages;
	unsigned int i;

	pages = kmalloc_array(num_pages, sizeof(struct page *), GFP_KERNEL);
	if (!pages) {
		tzdev_print(0, "Iwshmem buffer pages allocation failed.\n");
		return NULL;
	}

	for (i = 0; i < num_pages; i++) {
		pages[i] = alloc_page(GFP_KERNEL);
		if (!pages[i]) {
			tzdev_print(0, "alloc_page() failed.\n");
			goto out;
		}
	}

	return pages;

out:
	iwshmem_free_pages(pages, i);

	return NULL;
}

static int kapi_send_recv(int client_id, const void *msg_in, size_t size_in,
		void *msg_out, size_t size_out)
{
	int ret;

	ret = tzdev_kapi_send(client_id, msg_in, size_in);
	if (ret < 0) {
		tzdev_print(0, "tzdev_kapi_send failed - %d\n", ret);
		return ret;
	}

	ret = wait_for_completion_interruptible_timeout(&kcmd_event,
			IWNOTIFY_TIMEOUT * HZ);
	if (ret <= 0) {
		tzdev_print(0, "Wait for message timed out or interrupted, ret = %d\n",
				ret);
		if (ret == 0)
			return -EBUSY;

		return ret;
	}

	ret = tzdev_kapi_recv(client_id, msg_out, size_out);
	if (ret < 0) {
		tzdev_print(0, "tzdev_kapi_recv failed - %d\n", ret);
		return ret;
	}

	return ret;
}

static int kapi_simple_send_recv_test(unsigned int iterations)
{
	int client_id;
	int ret, test_return = 0;
	struct test_msg msg_in, msg_out;
	const char *str = "NWd: Hello from tz_kernel_api_test";
	const char expected_str[] = "SWd: Hello from kapi_simple_send_recv_test";

	tzdev_print(0, "Running kapi_simple_send_recv_test\n");

	init_completion(&kcmd_event);

	tzdev_print(0, "tz_iwnotify_chain_register()\n");
	ret = tz_iwnotify_chain_register(TZ_IWNOTIFY_OEM_NOTIFICATION_FLAG_16, &kcmd_notifier);
	if (ret < 0) {
		tzdev_print(0, "tz_iwnotify_chain_register failed. ret = %d\n", ret);
		return ret;
	}

	tzdev_print(0, "tzdev_kapi_open()\n");
	client_id = tzdev_kapi_open(&uuid);
	if (client_id < 0) {
		test_return = client_id;
		tzdev_print(0, "  tzdev_kapi_open failed - %d\n", test_return);
		goto out_iwnotify;
	}
	tzdev_print(0, "  client_id = %d\n", client_id);

	tzdev_print(0, "Calling kapi_send_recv %u time(s)\n", iterations);
	msg_in.test_num = KAPI_SIMPLE_SEND_RECV_TEST;
	strcpy(msg_in.str, str);
	while (iterations--) {
		memset(&msg_out, 0, sizeof(msg_out));
		ret = kapi_send_recv(client_id, &msg_in, sizeof(msg_in),
				&msg_out, sizeof(msg_out));
		if (ret < 0) {
			test_return = ret;
			tzdev_print(0, "kapi_send_recv failed - %d\n", ret);
			break;
		}

		if (msg_out.result) {
			tzdev_print(0, "Bad msg.result from TA - %u\n", msg_out.result);
			test_return = -EPERM;
			break;
		}

		if (strncmp(expected_str, msg_out.str, sizeof(expected_str))) {
			msg_out.str[sizeof(expected_str)] = '\0';
			tzdev_print(0, "Received msg.str '%s' mismatch expected one '%s'\n",
					msg_out.str, expected_str);
			test_return = -EPERM;
			break;
		}
	}

	tzdev_print(0, "tzdev_kapi_close()\n");
	ret = tzdev_kapi_close(client_id);
	if (ret < 0) {
		test_return = ret;
		tzdev_print(0, "tzdev_kapi_close failed - %d\n", ret);
	}

out_iwnotify:
	tzdev_print(0, "tz_iwnotify_chain_unregister()\n");
	ret = tz_iwnotify_chain_unregister(TZ_IWNOTIFY_OEM_NOTIFICATION_FLAG_16, &kcmd_notifier);
	if (ret < 0) {
		test_return = ret;
		tzdev_print(0, "tz_iwnotify_chain_unregister failed - %d\n", ret);
	}

	return test_return;
}

static int __kapi_iwshmem_test(unsigned int shmem_id, void *shmem, unsigned int size,
		unsigned int iterations)
{
	int client_id;
	int ret, test_return = 0;
	struct test_msg msg_in, msg_out;

	init_completion(&kcmd_event);

	tzdev_print(0, "tz_iwnotify_chain_register()\n");
	ret = tz_iwnotify_chain_register(TZ_IWNOTIFY_OEM_NOTIFICATION_FLAG_16, &kcmd_notifier);
	if (ret < 0) {
		tzdev_print(0, "tz_iwnotify_chain_register failed. ret = %d\n", ret);
		return ret;
	}

	tzdev_print(0, "tzdev_kapi_open()\n");
	client_id = tzdev_kapi_open(&uuid);
	if (client_id < 0) {
		test_return = client_id;
		tzdev_print(0, "  tzdev_kapi_open failed - %d\n", test_return);
		goto out_iwnotify;
	}
	tzdev_print(0, "  client_id = %d\n", client_id);

	tzdev_print(0, "tzdev_kapi_mem_grant()\n");
	ret = tzdev_kapi_mem_grant(client_id, shmem_id);
	if (ret) {
		test_return = ret;
		tzdev_print(0, "  tzdev_kapi_mem_grant failed - %d\n", ret);
		goto out_open;
	}

	tzdev_print(0, "Calling kapi_send_recv %u time(s)\n", iterations);
	msg_in.test_num = KAPI_IWSHMEM_TEST;
	msg_in.iwshmem_id = shmem_id;
	msg_in.iwshmem_size = size;
	while (iterations--) {
		memset(shmem, IWSHMEM_PATTERN_SEND, size);
		memset(&msg_out, 0, sizeof(msg_out));

		ret = kapi_send_recv(client_id, &msg_in, sizeof(msg_in),
				&msg_out, sizeof(msg_out));
		if (ret < 0) {
			test_return = ret;
			tzdev_print(0, "kapi_send_recv failed - %d\n", ret);
			break;
		}

		if (msg_out.result) {
			tzdev_print(0, "Bad msg.result from TA - %d\n", msg_out.result);
			test_return = -EPERM;
			break;
		}

		ret = memory_check_pattern(shmem, size, IWSHMEM_PATTERN_RECV);
		if (ret) {
			tzdev_print(0, "Received shmem have incorrect data\n");
			test_return = -EPERM;
			break;
		}
	}

	tzdev_print(0, "tzdev_kapi_mem_revoke()\n");
	ret = tzdev_kapi_mem_revoke(client_id, shmem_id);
	if (ret) {
		test_return = ret;
		tzdev_print(0, "  tzdev_kapi_mem_revoke failed - %d\n", ret);
	}

out_open:
	tzdev_print(0, "tzdev_kapi_close()\n");
	ret = tzdev_kapi_close(client_id);
	if (ret < 0) {
		test_return = ret;
		tzdev_print(0, "tzdev_kapi_close failed - %d\n", ret);
	}

out_iwnotify:
	tzdev_print(0, "tz_iwnotify_chain_unregister()\n");
	ret = tz_iwnotify_chain_unregister(TZ_IWNOTIFY_OEM_NOTIFICATION_FLAG_16, &kcmd_notifier);
	if (ret < 0) {
		test_return = ret;
		tzdev_print(0, "tz_iwnotify_chain_unregister failed - %d\n", ret);
	}

	return test_return;
}

static int kapi_iwshmem_test(unsigned int iterations)
{
	void *shmem;
	int ret, test_return = 0;
	int shmem_id;
	struct page *shmem_page;

	tzdev_print(0, "Running kapi_iwshmem_test\n");

	shmem_page = alloc_page(GFP_KERNEL);
	if (!shmem_page) {
		tzdev_print(0, "alloc_page failed\n");
		return -ENOMEM;
	}

	shmem = page_address(shmem_page);

	tzdev_print(0, "tzdev_kapi_mem_register() shmem=0x%p\n", shmem);
	ret = tzdev_kapi_mem_register(shmem, PAGE_SIZE, 1);
	if (ret < 0) {
		__free_page(shmem_page);
		tzdev_print(0, "tzdev_kapi_mem_register failed - %d\n", ret);
		return ret;
	}

	shmem_id = ret;
	tzdev_print(0, "  shmem_id = %d\n", shmem_id);

	test_return = __kapi_iwshmem_test(shmem_id, shmem, PAGE_SIZE, iterations);

	tzdev_print(0, "tzdev_kapi_mem_release()\n");
	ret = tzdev_kapi_mem_release(shmem_id);
	if (ret < 0) {
		test_return = ret;
		tzdev_print(0, "tzdev_kapi_mem_release failed - %d\n", ret);
	}

	__free_page(shmem_page);

	return test_return;
}

#define BIG_CONTIGUOUS_IWSHMEM_TEST_PAGES_MAX_ORDER	8

static int kapi_big_contiguous_iwshmem_test(unsigned int iterations)
{
	int ret, test_return = 0;
	void *shmem;
	int shmem_id;
	struct page *page_start = NULL;
	unsigned int order = BIG_CONTIGUOUS_IWSHMEM_TEST_PAGES_MAX_ORDER;
	unsigned int num_pages;

	tzdev_print(0, "Running kapi_big_contiguous_iwshmem_test\n");

	while (order > 0) {
		page_start = alloc_pages(GFP_KERNEL, order);
		if (page_start) {
			num_pages = 1 << order;
			tzdev_print(0, "Allocated %u pages\n", num_pages);
			break;
		}
		order--;
	}

	if (!page_start) {
		tzdev_print(0, "alloc_pages failed\n");
		return -ENOMEM;
	}

	shmem = page_address(page_start);
	if (!shmem) {
		tzdev_print(0, "Can't get page address\n");
		test_return = -ENOMEM;
		goto out_pages;
	}

	tzdev_print(0, "tzdev_kapi_mem_register() shmem = 0x%p, size = %lu\n",
			shmem, num_pages * PAGE_SIZE);
	ret = tzdev_kapi_mem_register(shmem, num_pages * PAGE_SIZE, 1);
	if (ret < 0) {
		tzdev_print(0, "tzdev_kapi_mem_register failed - %d\n", ret);
		test_return = ret;
		goto out_pages;
	}

	shmem_id = ret;
	tzdev_print(0, "  shmem_id = %d\n", shmem_id);

	test_return = __kapi_iwshmem_test(shmem_id, shmem, num_pages * PAGE_SIZE, iterations);

	tzdev_print(0, "tzdev_kapi_mem_release()\n");
	ret = tzdev_kapi_mem_release(shmem_id);
	if (ret < 0) {
		test_return = ret;
		tzdev_print(0, "tzdev_kapi_mem_release failed - %d\n", ret);
	}

out_pages:
	__free_pages(page_start, order);

	return test_return;
}

#define BIG_IWSHMEM_TEST_NUM_PAGES	(10 * SZ_1M / PAGE_SIZE)

static int kapi_big_iwshmem_test(unsigned int iterations)
{
	int ret, test_return = 0;
	void *shmem;
	int shmem_id;
	struct page **pages;
	unsigned int num_pages = BIG_IWSHMEM_TEST_NUM_PAGES;

	tzdev_print(0, "Running kapi_big_iwshmem_test\n");

	pages = iwshmem_alloc_pages(num_pages);
	if (!pages) {
		tzdev_print(0, "iwshmem_alloc_pages failed\n");
		return -ENOMEM;
	}

	shmem = vmap(pages, num_pages, VM_MAP, PAGE_KERNEL);
	if (!shmem) {
		tzdev_print(0, "vmap failed\n");
		test_return = -ENOMEM;
		goto out_pages;
	}

	tzdev_print(0, "tzdev_kapi_mem_pages_register() num_pages = %u\n", num_pages);
	ret = tzdev_kapi_mem_pages_register(pages, num_pages, 1);
	if (ret < 0) {
		tzdev_print(0, "tzdev_kapi_mem_pages_register failed - %d\n", ret);
		test_return = ret;
		goto out_vmap;
	}

	shmem_id = ret;
	tzdev_print(0, "  shmem_id = %d\n", shmem_id);

	test_return = __kapi_iwshmem_test(shmem_id, shmem, num_pages * PAGE_SIZE, iterations);

	tzdev_print(0, "tzdev_kapi_mem_release()\n");
	ret = tzdev_kapi_mem_release(shmem_id);
	if (ret < 0) {
		test_return = ret;
		tzdev_print(0, "tzdev_kapi_mem_release failed - %d\n", ret);
	}

out_vmap:
	vunmap(shmem);

out_pages:
	iwshmem_free_pages(pages, num_pages);

	return test_return;
}

#define IWSHMEM_STRESS_TEST_PAGES_NUMBER	256

static int kapi_iwshmem_stress_test(unsigned int iterations)
{
	int client_id;
	int ret, test_return = 0;
	struct test_msg msg_in, msg_out;
	unsigned int shmem_msg_size = 0x100;
	int *mem_id = NULL;
	struct page **pages = NULL;
	unsigned int i;

	tzdev_print(0, "Running kapi_iwshmem_stress_test\n");

	init_completion(&kcmd_event);

	mem_id = kcalloc(IWSHMEM_STRESS_TEST_PAGES_NUMBER, sizeof(int), GFP_KERNEL);
	if (!mem_id) {
		tzdev_print(0, "kcalloc ids failed\n");
		return -ENOMEM;
	}

	pages = kcalloc(IWSHMEM_STRESS_TEST_PAGES_NUMBER, sizeof(struct page *), GFP_KERNEL);
	if (!pages) {
		test_return = -ENOMEM;
		tzdev_print(0, "kcalloc pages failed\n");
		goto out_mem_id;
	}

	tzdev_print(0, "Allocating %d pages\n", IWSHMEM_STRESS_TEST_PAGES_NUMBER);
	for (i = 0; i < IWSHMEM_STRESS_TEST_PAGES_NUMBER; i++) {
		pages[i] = alloc_page(GFP_KERNEL);
		if (!pages[i]) {
			test_return = -ENOMEM;
			tzdev_print(0, "alloc_page failed\n");
			goto out_pages;
		}

		mem_id[i] = tzdev_kapi_mem_register(page_address(pages[i]), PAGE_SIZE, 1);
		if (mem_id[i] < 0) {
			test_return = mem_id[i];
			tzdev_print(0, "tzdev_kapi_mem_register failed - %d\n", mem_id[i]);
			goto out_pages;
		}
	}

	tzdev_print(0, "tz_iwnotify_chain_register()\n");
	ret = tz_iwnotify_chain_register(TZ_IWNOTIFY_OEM_NOTIFICATION_FLAG_16, &kcmd_notifier);
	if (ret < 0) {
		test_return = ret;
		tzdev_print(0, "tz_iwnotify_chain_register failed. ret = %d\n", ret);
		goto out_pages;
	}

	tzdev_print(0, "tzdev_kapi_open()\n");
	client_id = tzdev_kapi_open(&uuid);
	if (client_id < 0) {
		test_return = client_id;
		tzdev_print(0, "  tzdev_kapi_open failed - %d\n", test_return);
		goto out_iwnotify;
	}
	tzdev_print(0, "  client_id = %d\n", client_id);


	tzdev_print(0, "Calling iwshmem stress test %u time(s)\n", iterations);
	while (iterations--) {
		tzdev_print(0, "Grant access for each iwshmem\n");
		for (i = 0; i < IWSHMEM_STRESS_TEST_PAGES_NUMBER; i++) {
			ret = tzdev_kapi_mem_grant(client_id, mem_id[i]);
			if (ret) {
				test_return = ret;
				tzdev_print(0, "  tzdev_kapi_mem_grant failed - %d\n", ret);
				goto out_mem_grant;
			}
		}

		tzdev_print(0, "Run simple test for each iwshmem\n");
		for (i = 0; i < IWSHMEM_STRESS_TEST_PAGES_NUMBER; i++) {
			msg_in.test_num = KAPI_IWSHMEM_TEST;
			msg_in.iwshmem_id = mem_id[i];
			msg_in.iwshmem_size = shmem_msg_size;
			memset(page_address(pages[i]), IWSHMEM_PATTERN_SEND, shmem_msg_size);
			memset(&msg_out, 0, sizeof(msg_out));

			ret = kapi_send_recv(client_id, &msg_in, sizeof(msg_in),
					&msg_out, sizeof(msg_out));
			if (ret < 0) {
				test_return = ret;
				tzdev_print(0, "kapi_send_recv failed - %d\n", ret);
				goto out_mem_grant;
			}

			if (msg_out.result) {
				tzdev_print(0, "Bad msg.result from TA - %d\n", msg_out.result);
				test_return = -EPERM;
				goto out_mem_grant;
			}

			ret = memory_check_pattern(page_address(pages[i]), shmem_msg_size, IWSHMEM_PATTERN_RECV);
			if (ret) {
				tzdev_print(0, "Received shmem have incorrect data\n");
				test_return = -EPERM;
				goto out_mem_grant;
			}
		}

		tzdev_print(0, "Revoke access for each iwshmem\n");
		for (i = 0; i < IWSHMEM_STRESS_TEST_PAGES_NUMBER; i++)
			tzdev_kapi_mem_revoke(client_id, mem_id[i]);
	}


out_mem_grant:
	for (i = 0; i < IWSHMEM_STRESS_TEST_PAGES_NUMBER; i++)
		tzdev_kapi_mem_revoke(client_id, mem_id[i]);

	tzdev_print(0, "tzdev_kapi_close()\n");
	ret = tzdev_kapi_close(client_id);
	if (ret < 0) {
		test_return = ret;
		tzdev_print(0, "tzdev_kapi_close failed - %d\n", ret);
	}

out_iwnotify:
	tzdev_print(0, "tz_iwnotify_chain_unregister()\n");
	ret = tz_iwnotify_chain_unregister(TZ_IWNOTIFY_OEM_NOTIFICATION_FLAG_16, &kcmd_notifier);
	if (ret < 0) {
		test_return = ret;
		tzdev_print(0, "tz_iwnotify_chain_unregister failed - %d\n", ret);
	}

out_pages:
	for (i = 0; i < IWSHMEM_STRESS_TEST_PAGES_NUMBER; i++) {
		if (!pages[i])
			break;
		__free_page(pages[i]);

		if (mem_id[i] < 0)
			break;
		tzdev_kapi_mem_release(mem_id[i]);
	}

	kfree(pages);

out_mem_id:
	kfree(mem_id);

	return test_return;
}

static int kapi_iwshmem_swd_restart_test(struct file *file)
{
	struct test_private_data *test_data = file->private_data;

	tzdev_print(0, "Running kapi_iwshmem_swd_restart_test\n");

	if (!test_data) {
		tzdev_print(0, "Error: test data is not initialized\n");
		return -EBUSY;
	}

	return __kapi_iwshmem_test(test_data->shmem_id, page_address(test_data->page),
			test_data->size, 1);
}

static void kapi_release_mem(struct test_private_data *data)
{
	tzdev_print(0, "Release test data: release iwshmem %d\n", data->shmem_id);

	tzdev_kapi_mem_release(data->shmem_id);
	__free_page(data->page);

	kfree(data);
}

static int kapi_register_mem(struct file *file)
{
	int ret;
	int shmem_id;
	struct page *shmem_page;
	struct test_private_data *test_data;

	tzdev_print(0, "Initialize test data: register iwshmem\n");

	if (file->private_data) {
		tzdev_print(0, "Error: test data is already initialized\n");
		return -EBUSY;
	}

	test_data = kmalloc(sizeof(struct test_private_data), GFP_KERNEL);
	if (!test_data) {
		tzdev_print(0, "kmalloc failed\n");
		return -ENOMEM;
	}

	shmem_page = alloc_page(GFP_KERNEL);
	if (!shmem_page) {
		tzdev_print(0, "alloc_page failed\n");
		ret = -ENOMEM;
		goto out_test_data;
	}

	ret = tzdev_kapi_mem_register(page_address(shmem_page), PAGE_SIZE, 1);
	if (ret < 0) {
		tzdev_print(0, "tzdev_kapi_mem_register failed - %d\n", ret);
		goto out_shmem_page;
	}

	shmem_id = ret;

	tzdev_print(0, "Page 0x%p iwshmem id is %d\n", page_address(shmem_page), shmem_id);

	memset(test_data, 0, sizeof(struct test_private_data));
	test_data->page = shmem_page;
	test_data->size = PAGE_SIZE;
	test_data->shmem_id = shmem_id;
	test_data->release_func = kapi_release_mem;

	file->private_data = test_data;

	return 0;

out_shmem_page:
	__free_page(shmem_page);

out_test_data:
	kfree(test_data);

	return ret;
}

static long tz_kapi_test_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg)
{
	switch (cmd) {
	case TZ_KAPI_TEST_RUN: {
		int res;
		int test_num = arg;

		switch (test_num) {
		case KAPI_SIMPLE_SEND_RECV_TEST:
			res = kapi_simple_send_recv_test(1);
			break;
		case KAPI_IWSHMEM_TEST:
			res = kapi_iwshmem_test(1);
			break;
		case KAPI_MULTIPLE_SEND_RECV_TEST:
			res = kapi_simple_send_recv_test(1000);
			break;
		case KAPI_MULTIPLE_SEND_RECV_IWSHMEM_TEST:
			res = kapi_iwshmem_test(1000);
			break;
		case KAPI_IWSHMEM_STRESS_TEST:
			res = kapi_iwshmem_stress_test(300);
			break;
		case KAPI_BIG_CONTIGUOUS_IWSHMEM_TEST:
			res = kapi_big_contiguous_iwshmem_test(1);
			break;
		case KAPI_MULTIPLE_BIG_CONTIGUOUS_IWSHMEM_TEST:
			res = kapi_big_contiguous_iwshmem_test(1000);
			break;
		case KAPI_BIG_IWSHMEM_TEST:
			res = kapi_big_iwshmem_test(1);
			break;
		case KAPI_MULTIPLE_BIG_IWSHMEM_TEST:
			res = kapi_big_iwshmem_test(200);
			break;
		case KAPI_IWSHMEM_SWD_RESTART_TEST:
			res = kapi_iwshmem_swd_restart_test(file);
			break;
		default:
			tzdev_print(0, "Invalid test number: %d\n", test_num);
			return -EINVAL;
		}

		return res;
	}
	case TZ_KAPI_REGISTER_MEM:
		return kapi_register_mem(file);
	default:
		return -ENOTTY;
	}
}

static int tz_kapi_test_open(struct inode *inode, struct file *filp)
{
	if (!tzdev_is_up())
		return -EPERM;

	return 0;
}

static int tz_kapi_test_release(struct inode *inode, struct file *file)
{
	struct test_private_data *test_data = file->private_data;

	(void) inode;

	if (test_data)
		test_data->release_func(test_data);

	return 0;
}

static const struct file_operations tz_kapi_test_fops = {
	.owner = THIS_MODULE,
	.open = tz_kapi_test_open,
	.unlocked_ioctl = tz_kapi_test_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = tz_kapi_test_ioctl,
#endif /* CONFIG_COMPAT */
	.release = tz_kapi_test_release,
};

static struct tz_cdev tz_kapi_test_cdev = {
	.name = TZ_KAPI_DEVICE_NAME,
	.fops = &tz_kapi_test_fops,
	.owner = THIS_MODULE,
};

static int __init tz_kapi_test_init(void)
{
	return tz_cdev_register(&tz_kapi_test_cdev);
}

static void __exit tz_kapi_test_exit(void)
{
	tz_cdev_unregister(&tz_kapi_test_cdev);
}

module_init(tz_kapi_test_init);
module_exit(tz_kapi_test_exit);
