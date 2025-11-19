/*****************************************************************************
  * Longcheer Touchscreen Driver
  * Copyright (C) 2020 - 2024 Longcheer, Inc.
  *
  * This program is free software; you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation; either version 2 of the License, or
  * (at your option) any later version.
  *
  * This program is distributed in the hope that it will be a reference
  * to you, when you are integrating any CTP IC into your system,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  * General Public License for more details.
*****************************************************************************/


#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/sysfs.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/string.h>
#include <asm/unistd.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include <linux/proc_fs.h>

#include "lct_tp_driver.h"

/*add lct info start*/
#include <linux/module.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/miscdevice.h>

#define TP_INFO_NAME          "tp_info"
#define LCT_STRING_SIZE       128

typedef struct lct_tp{
	struct kobject *tp_device;
	char tp_info_buf[LCT_STRING_SIZE];
	struct proc_dir_entry *proc_entry_tp_info;
	int (*pfun_info_cb)(const char *);
}lct_tp_t;
static lct_tp_t *lct_tp_p = NULL;
/*add lct info end*/

struct touch_panel tp_mode;
EXPORT_SYMBOL(tp_mode);


/*
 * GLOBAL VARIABLE DEFINITIONS
 ****************************************************************************************
 */
static struct proc_dir_entry *tp_selftest_proc;

static char ft_tp_selftest_status[15] = {0};

static bool is_in_self_test;
static int lct_tp_selftest_cmd;

/*
 * FUNCTION DEFINITIONS
 ****************************************************************************************
 */

/*add lct tp-selftest start*/
static int tp_chip_self_test(void);
static void tp_selftest_work_func(void);

static ssize_t tp_selftest_proc_write(struct file *file, const char __user *buf, size_t size, loff_t *ppos)
{
	char tmp_data[64] = {0};

	if (copy_from_user(tmp_data, buf, size)) {
		TP_LOGE("copy_from_user() fail.\n");
		return -EFAULT;
	}

	if ((strncmp(tmp_data, TP_SELF_TEST_LONGCHEER_MMI_CMD, strlen(TP_SELF_TEST_LONGCHEER_MMI_CMD)) == 0) && (!is_in_self_test)) { //mmi
		TP_LOGW("Longcheer MMI TP self-test ...\n");
		lct_tp_selftest_cmd = TP_SELFTEST_CMD_LONGCHEER_MMI;
	} else {
		TP_LOGW("Unknown command\n");
		is_in_self_test = false;
		return size;
	}

	is_in_self_test = true;

	return size;
}

static ssize_t tp_selftest_proc_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	int cnt = 0;
	char *page = NULL;

	if (*ppos)
		return 0;

	if (is_in_self_test)
		tp_selftest_work_func();

	page = kzalloc(128, GFP_KERNEL);
	cnt = snprintf(page, PAGE_SIZE, "%s", ft_tp_selftest_status);
	cnt = simple_read_from_buffer(buf, size, ppos, page, cnt);
	if (*ppos != cnt)
		*ppos = cnt;
	TP_LOGW("page=%s, cnt=%d\n", page, cnt);
	kfree(page);

	return cnt;
}

static int tp_chip_self_test(void)
{
	unsigned char cmd = lct_tp_selftest_cmd;
	if (tp_selftest_callback_func == NULL) {
		TP_LOGW("The TP is not support self test func\n");
		return 0;
	} else {
		TP_LOGW("Testing ...\n");
		return tp_selftest_callback_func(cmd);
	}
}

static void tp_selftest_work_func(void)
{
	int i = 0;
	int val = 0;

	for (i = 0; i < TP_SELF_TEST_RETROY_COUNT; i++) {
		TP_LOGW("tp self test count = %d\n", i);
		val = tp_chip_self_test();
		if (val == 2) {
			strlcpy(ft_tp_selftest_status, TP_SELF_TEST_RESULT_PASS,
					sizeof(ft_tp_selftest_status));
			TP_LOGW("self test success\n");
			break;
		} else if (val == 1) {
			strlcpy(ft_tp_selftest_status, TP_SELF_TEST_RESULT_FAIL,
					sizeof(ft_tp_selftest_status));
			TP_LOGW("self test failed\n");
		} else {
			strlcpy(ft_tp_selftest_status,
					TP_SELF_TEST_RESULT_UNKNOWN,
					sizeof(ft_tp_selftest_status));
			TP_LOGW("self test result Unknown\n");
			break;
		}
	}
	is_in_self_test = false;
}

void lct_tp_selftest_init(tp_selftest_callback_t callback)
{
	tp_selftest_callback_func = callback;
}
EXPORT_SYMBOL(lct_tp_selftest_init);

static const struct proc_ops tp_selftest_proc_fops = {
	.proc_read = tp_selftest_proc_read,
	.proc_write = tp_selftest_proc_write,
};


static int tp_selftest_init(void)
{
	tp_selftest_proc = proc_create_data(TP_SELF_TEST_PROC_FILE, 0666, NULL, &tp_selftest_proc_fops, NULL);
	if (IS_ERR_OR_NULL(tp_selftest_proc)) {
		TP_LOGW("create /proc/%s failed\n", TP_SELF_TEST_PROC_FILE);
		return -EPERM;
	} else {
		TP_LOGW("create /proc/%s success\n", TP_SELF_TEST_PROC_FILE);
	}
	return 0;
}

static void tp_selftest_exit(void)
{
	if (tp_selftest_proc != NULL) {
		remove_proc_entry(TP_SELF_TEST_PROC_FILE, NULL);
		tp_selftest_proc = NULL;
		TP_LOGW("remove /proc/%s\n", TP_SELF_TEST_PROC_FILE);
	}
	return;
}
/*add lct tp-selftest end*/

/*add lct info start*/
static ssize_t lct_proc_tp_info_read(struct file *file, char __user *buf, size_t size, loff_t *ppos);
static const struct proc_ops lct_proc_tp_info_fops = {
	.proc_read		= lct_proc_tp_info_read,
};



void update_lct_tp_info(char *tp_info_buf)
{
	if (NULL != tp_info_buf) {
		memset(lct_tp_p->tp_info_buf, 0, sizeof(lct_tp_p->tp_info_buf));
		strcpy(lct_tp_p->tp_info_buf, tp_info_buf);
	}
	return;
}
EXPORT_SYMBOL(update_lct_tp_info);


static ssize_t lct_proc_tp_info_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	int cnt=0;
	char *page = NULL;

	//TP_INFO("size = %lu, pos = %lld\n", size, *ppos);
	if (*ppos)
		return 0;

	page = kzalloc(128, GFP_KERNEL);

	//if(NULL == lct_tp_p->tp_info_buf)
	if(0 == strlen(lct_tp_p->tp_info_buf))
		cnt = sprintf(page, "No touchpad\n");
	else
		cnt = sprintf(page, "%s", (strlen(lct_tp_p->tp_info_buf) ? lct_tp_p->tp_info_buf : "Unknown touchpad"));

	cnt = simple_read_from_buffer(buf, size, ppos, page, cnt);
	if (*ppos != cnt)
		*ppos = cnt;
	TP_INFO("page=%s", page);

	kfree(page);
	return cnt;
}

int init_lct_tp_info(char *tp_info_buf)
{
	TP_INFO("init /proc/%s ...\n", TP_INFO_NAME);
	lct_tp_p = kzalloc(sizeof(lct_tp_t), GFP_KERNEL);
	if (IS_ERR_OR_NULL(lct_tp_p)){
		TP_ERR("kzalloc() request memory failed!\n");
		return -ENOMEM;
	}

	if (NULL != tp_info_buf)
		strcpy(lct_tp_p->tp_info_buf, tp_info_buf);

	lct_tp_p->proc_entry_tp_info = proc_create_data(TP_INFO_NAME, 0444, NULL, &lct_proc_tp_info_fops, NULL);
	if (IS_ERR_OR_NULL(lct_tp_p->proc_entry_tp_info)) {
		TP_ERR("add /proc/%s error\n", TP_INFO_NAME);
		goto err_tp_info;
	}

	TP_INFO("done\n");
	return 0;

err_tp_info:
	kfree(lct_tp_p);
	return -1;
}

void uninit_lct_tp_info(void)
{
	TP_INFO("uninit /proc/%s ...\n", TP_INFO_NAME);

	if (IS_ERR_OR_NULL(lct_tp_p))
		return;

	if (lct_tp_p->proc_entry_tp_info != NULL) {
		remove_proc_entry(TP_INFO_NAME, NULL);
		lct_tp_p->proc_entry_tp_info = NULL;
		TP_INFO("remove /proc/%s\n", TP_INFO_NAME);
	}

	kfree(lct_tp_p);
	TP_INFO("done\n");
}

static int tp_info_init(void)
{
	int ret;
	TP_INFO("init");
	//create longcheer procfs node
	ret = init_lct_tp_info("[Vendor]unkown,[FW]unkown,[IC]unkown\n");
	if (ret < 0) {
		TP_ERR("init_lct_tp_info Failed!\n");
		return ret;
	}
	TP_INFO("init_lct_tp_info Succeeded!\n");
	return 0;
}

static void tp_info_exit(void)
{
	TP_INFO("exit");
	uninit_lct_tp_info();
	return;
}
/*add lct info end*/

static int __init lct_tp_driver_init(void)
{
	tp_info_init();
	tp_selftest_init();
	return 0;
}

static void __exit lct_tp_driver_exit(void)
{
	tp_selftest_exit();
	tp_info_exit();
	return;
}

void tp_charge_status_switch(int status)
{
    if (tp_mode.charger_mode_switch_status) {
        TP_INFO("tp charge status switch status = %d \n",status);
        tp_mode.charger_mode_switch_status(status);
    } else {
        TP_INFO("tp charge status switch not func\n");
    }
}
EXPORT_SYMBOL(tp_charge_status_switch);

module_init(lct_tp_driver_init);
module_exit(lct_tp_driver_exit);

MODULE_DESCRIPTION("TP selftest driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
