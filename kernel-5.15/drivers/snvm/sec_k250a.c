/*
 * Copyright (C) 2020 Samsung Electronics. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program;
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/i2c.h>
#include <linux/regulator/consumer.h>
#include <linux/ioctl.h>
#include <linux/gpio.h>
#include <linux/version.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/string.h>
#if defined(CONFIG_SEC_SNVM_PLATFORM_DRV)
#include <linux/platform_device.h>
#endif

#include "sec_star.h"
#include "hal/ese_i2c.h"

#undef USE_INTERNAL_PULLUP //if use external pull-up, not necessary

#define ERR(msg...)	pr_err("[star-k250a] : " msg)
#define INFO(msg...)	pr_info("[star-k250a] : " msg)

struct k250a_dev {
	struct i2c_client *client;
	struct regulator *vdd;
	unsigned int reset_gpio;
	struct pinctrl *pinctrl;
	struct pinctrl_state *nvm_on_pin;
	struct pinctrl_state *nvm_off_pin;
#if defined(USE_INTERNAL_PULLUP)
#define SCL_GPIO_NUM	335
#define SDA_GPIO_NUM	320
	struct pinctrl *pinctrl;
	struct pinctrl_state *pin_i2c;
	struct pinctrl_state *pin_gpio;
#endif
	sec_star_t *star;
#ifdef CONFIG_SEC_SNVM_I2C_CLOCK_CONTROL
	struct clk *i2c_main_clk;
	struct clk *i2c_dma_clk;
	bool i2c_main_clk_enabled;
	bool i2c_dma_clk_enabled;
#endif
};

static struct k250a_dev g_k250a;

#define SELFTEST_CMD_TEST_CHIP_SN	(0x01)
#define	SELFTEST_CMD_TIMER			(0x02)
#define SELFTEST_CMD_PROVISION_AK	(0x04)

#define	SELFTEST_CMD_NVM_CHECKSUM	(0x08)
#define SELFTEST_CMD_NVM_ACCESS		(0x10)
#define	SELFTEST_CMD_RAM_ACCESS		(0x20)
#define SELFTEST_CMD_ENC_CHECK		(0x40)
#define	SELFTEST_CMD_TEST_ALL		(0x7F)
#define SELFTEST_CMD_COUNT		(10)

#define MAX_FILE_DATA_SIZE 256
#define CHIP_INFO_SIZE				(44)
#define KEY_FLAG_INFO_SIZE			(4)
#define PARTITION_INFO_SIZE			(4)
#define IMG_VERSION_SIZE			(4)
#define IMG_DATE_SIZE				(12)
#define VERSION_INFO_SIZE			(IMG_VERSION_SIZE + IMG_DATE_SIZE)
#define ADD_SEQUENCE(x) \
do {\
	(x) = (((x) + 1) % 2); \
} while(0);

typedef struct img_ver_s {
	uint8_t version[IMG_VERSION_SIZE];
	uint8_t build_date[IMG_DATE_SIZE];
} img_ver_t;

typedef struct {
	uint8_t chip[CHIP_INFO_SIZE];
	uint8_t key_flag[KEY_FLAG_INFO_SIZE];
	uint8_t partition[PARTITION_INFO_SIZE];
	img_ver_t bootloader;
	img_ver_t core;
	img_ver_t crypto;
} __attribute__((packed)) ese_info_t;

#ifdef CONFIG_SEC_SNVM_I2C_CLOCK_CONTROL
static void k250a_parse_i2c_clock(struct k250a_dev *k250a, struct device_node *np)
{
	struct device_node *i2c_np;

	i2c_np = of_parse_phandle(np, "i2c_node", 0);
	if (!i2c_np) {
		INFO("i2c_node not found\n");
		return;
	}

	INFO("i2c_node found\n");
	k250a->i2c_main_clk = of_clk_get_by_name(i2c_np, "main");

	if (IS_ERR(k250a->i2c_main_clk))
		INFO("failed to get i2c main clock\n");
	else
		INFO("i2c main clock found");

	k250a->i2c_dma_clk = of_clk_get_by_name(i2c_np, "dma");
	if (IS_ERR(k250a->i2c_dma_clk))
		INFO("failed to get i2c dma clock\n");
	else
		INFO("i2c dma clock found");
}

static void k250a_i2c_clock_enable(void)
{
	int ret = 0;

	if (!g_k250a.i2c_main_clk_enabled && !IS_ERR_OR_NULL(g_k250a.i2c_main_clk)) {
		ret = clk_prepare_enable(g_k250a.i2c_main_clk);
		if (ret)
			ERR("failed to enable i2c main clock\n");
		else
			g_k250a.i2c_main_clk_enabled = true;
	}

	if (!g_k250a.i2c_dma_clk_enabled && !IS_ERR_OR_NULL(g_k250a.i2c_dma_clk)) {
		ret = clk_prepare_enable(g_k250a.i2c_dma_clk);
		if (ret)
			ERR("failed to enable i2c dma clock\n");
		else
			g_k250a.i2c_dma_clk_enabled = true;
	}
}

static void k250a_i2c_clock_disable(void)
{
	if (g_k250a.i2c_main_clk_enabled && !IS_ERR_OR_NULL(g_k250a.i2c_main_clk)) {
		clk_disable_unprepare(g_k250a.i2c_main_clk);
		g_k250a.i2c_main_clk_enabled = false;
	}

	if (g_k250a.i2c_dma_clk_enabled && !IS_ERR_OR_NULL(g_k250a.i2c_dma_clk)) {
		clk_disable_unprepare(g_k250a.i2c_dma_clk);
		g_k250a.i2c_dma_clk_enabled = false;
	}
}
#endif

static int k250a_poweron(void)
{
	int ret = 0;
#ifdef CONFIG_SEC_SNVM_I2C_CLOCK_CONTROL
	bool i2c_main_clk = !IS_ERR_OR_NULL(g_k250a.i2c_main_clk);
	bool i2c_dma_clk = !IS_ERR_OR_NULL(g_k250a.i2c_dma_clk);

	INFO("k250a_poweron (i2c clk:%s%s)\n",
		i2c_main_clk ? " main" : "", i2c_dma_clk ? " dma" : "");
#else
	INFO("k250a_poweron\n");
#endif
	if (g_k250a.vdd == NULL) {
		if (g_k250a.reset_gpio == 0) {
			return 0;
		}

		INFO("rest pin control instead of vdd\n");

		gpio_set_value(g_k250a.reset_gpio, 0);
		usleep_range(1000, 2000);
		gpio_set_value(g_k250a.reset_gpio, 1);

		usleep_range(15000, 20000);
		return 0;
	}

	ret = regulator_enable(g_k250a.vdd);
	if (ret) {
		ERR("%s - enable vdd failed, ret=%d\n", __func__, ret);
		return ret;
	}

	usleep_range(1000, 2000);
	if (g_k250a.nvm_on_pin) {
		if (pinctrl_select_state(g_k250a.pinctrl, g_k250a.nvm_on_pin))
			ERR("nvm on pinctrl set error\n");
		else
			INFO("nvm on pinctrl set\n");
	}

#if defined(USE_INTERNAL_PULLUP)
	if (pinctrl_select_state(g_k250a.pinctrl, g_k250a.pin_i2c) < 0) {
		ERR("failed to pinctrl_select_state for gpio");
	}
#endif

	usleep_range(14000, 15000);

	return 0;
}

static int k250a_poweroff(void)
{
	int ret = 0;

	INFO("k250a_poweroff\n");

	if (g_k250a.vdd == NULL) {
		return 0;
	}
#if 0
	if (g_k250a.nvm_off_pin) {
		if (pinctrl_select_state(g_k250a.pinctrl, g_k250a.nvm_off_pin))
			ERR("nvm off pinctrl set error\n");
		else
			INFO("nvm off pinctrl set\n");
	}

#if defined(USE_INTERNAL_PULLUP)
	if (pinctrl_select_state(g_k250a.pinctrl, g_k250a.pin_gpio) < 0) {
		ERR("failed to pinctrl_select_state for gpio");
	}
#endif
#endif
	ret = regulator_disable(g_k250a.vdd);
	if (ret) {
		ERR("%s - disable vdd failed, ret=%d\n", __func__, ret);
		return ret;
	}

	usleep_range(15000, 20000);
	return 0;
}

static int k250a_reset(void)
{
	if (g_k250a.reset_gpio == 0) {
		return -ENODEV;
	}

	gpio_set_value(g_k250a.reset_gpio, 0);
	usleep_range(1000, 2000);
	gpio_set_value(g_k250a.reset_gpio, 1);
	usleep_range(15000, 20000);
	return 0;
}

static star_dev_t star_dev = {
	.name = "k250a",
	.hal_type = SEC_HAL_I2C,
	.client = NULL,
	.power_on = k250a_poweron,
	.power_off = k250a_poweroff,
	.reset = k250a_reset
};

unsigned char __cal_lrc(unsigned char *data, unsigned char data_size)
{
	int i = 0;
	unsigned char lrc = 0x0;
	for (i=0; i < (data_size - 1); i++)
	{
		lrc ^= data[i];
	}
	return lrc;
}

char __hex2ascii(char hex)
{
	if (hex < 0x0A) hex += 0x30;
	else hex += 0x37;

	return (hex);
}

void __getHWInfo(uint8_t *chip_info, uint8_t *hw_info)
{
	uint32_t i = 0;

	for (i = 0; i < 0x05; i++) {
		hw_info[i + 2] = __hex2ascii(chip_info[0x04 + i]);
	}
	hw_info[11] = __hex2ascii(chip_info[0x2A]);
}

static void print_buffer(const char *buf_tag, uint8_t *buffer, uint32_t buffer_size)
{
	char tmp_buffer[49] = { 0 };
	char *tmp_p = tmp_buffer;
	uint32_t i = 0;

	for (i = 0; i < buffer_size; i ++) {
		if (i != 0 && (i % 16) == 0) {
			INFO("%s, %d %s", __func__, i, tmp_buffer);
			tmp_p = tmp_buffer;
		}
		INFO("%s,buffer printf++: %dth, %02x",__func__, i, buffer[i]);
		tmp_p += 3;
	}
	INFO("%s tmp_buffer %s",__func__, tmp_buffer);
}

int receive_data_with_polling(void *ctx, unsigned char *data, unsigned int data_size)
{
#define SLAVE_ADDRESS 0x21
#define MAX_POLLING_COUNT 100
	struct i2c_client *client = ctx;
	unsigned int data_len =0, rec_len =0;
	unsigned int poll_cnt = MAX_POLLING_COUNT;

	do {
		msleep(2);
		data_len = ese_i2c_receive(client, data, 1);
		if(data_len != 1) {
			ERR("%s, mismatch receive size+1 %u, %u\n",__func__, 1, data_len);
			return -1;
		}
	} while (data[0] == 0x0 && poll_cnt > 0);

	if(poll_cnt ==0){
		ERR("%s, respone timeout\n", __func__);
	}

	if(data[0] != SLAVE_ADDRESS) {
		ERR("%s, invalid slave address: 0x%02x\n", __func__, data[0]);
	}

	data_len += ese_i2c_receive(client, data + 1, 2);
	if (data_len != 3) {
		ERR(" %s,mismatch receive size %u, %u",__func__, 3, data_len);
		return -1;
	}

	rec_len = data[2] + 1;
	data_len += ese_i2c_receive(client,data + 3, rec_len);
	if (data_len != 3 + rec_len) {
		ERR(" %s, mismatch receive size+2 %u, %u",__func__, 3 + rec_len, data_len);
		return -1;
	}

	return data_len;
}

int boot_firmware(void)
{
	unsigned char txdata[] = {0x12, 0x00, 0x04, 0x00, 0xfc, 0x00, 0x00, 0xea};
	unsigned char rxdata[256] = {0x0,};
	int ret;

	ret = regulator_enable(g_k250a.vdd);
	INFO("%s, enable mt6377-vtp!\n", __func__);
	msleep(200); //200ms

	ese_i2c_send(g_k250a.client, txdata, 8);
        ret = receive_data_with_polling(g_k250a.client, rxdata, 6);
	print_buffer("sec_k250a", rxdata, 6);

	if (rxdata[3] != 0x90 || rxdata[4] != 0x00)
        {
                INFO("%s, error star boot firmware!\n",__func__);
        } else {
                ERR("%s, success  star boot firmware!\n",__func__);
        }

	return  ret;
}

int get_info_origin(void)
{
	unsigned char txdata[] = {0x12, 0x00, 0x05, 0x00, 0xb1, 0x3b, 0x00, 0x84, 0x19};
	unsigned char rxdata[256] = {0x0,};
	int ret;

	ese_i2c_send(g_k250a.client, txdata, 9);
        ret = receive_data_with_polling(g_k250a.client, rxdata, 0x8A);
      	print_buffer("sec_k250a", rxdata, 0x8A);

        if (rxdata[0x87] != 0x90 && rxdata[0x88] != 0x00)
        {
                INFO("%s, error get info!\n",__func__);
        } else {
                ERR("%s, success get info!\n",__func__);
        }

	return ret;
}

static int get_info(unsigned int *seq, unsigned char *info, unsigned int *info_size)
{
	unsigned char txdata[] = {0x12, 0x00, 0x05, 0x00, 0xb1, 0x3b, 0x00, 0x84, 0x19};
	unsigned int txdata_size = 9;
	unsigned char rxdata[MAX_FILE_DATA_SIZE] = {0x0, };
	unsigned int rxdata_size = 0x8A;
	int ret_size = 0;

	txdata[1] = ((*seq) << 6) & 0xFF;
	txdata[sizeof(txdata)-1] = __cal_lrc(txdata, sizeof(txdata));

	ret_size = ese_i2c_send(g_k250a.client, txdata, txdata_size);
	if (ret_size != txdata_size) {
		ERR("mismatch send size : %u, %d", txdata_size, ret_size);
	}

	ret_size = receive_data_with_polling(g_k250a.client,rxdata, rxdata_size);
	if (ret_size != rxdata_size) {
		ERR("failed to get_info (receive data fail)");
		return -1;
	}

	if (rxdata[0x87] != 0x90 || rxdata[0x88] != 0x00) {
		ERR("failed to get_info (FW get information fail)");
		ERR("return value : %x%x", rxdata[0x87], rxdata[0x88]);
		return -1;
	}

	if (memcpy(info, rxdata + 3, rxdata_size - 6) < 0) {
		ERR("failed to copy rx data");
		return -1;
	}
	*info_size = rxdata_size - 6;
	ERR("get_info over");

	return 0;
}

static int selfTest(unsigned int *seq, unsigned char test_cmd, unsigned char *selfTestRes, unsigned int *selfTestRes_size)
{
	unsigned char txdata[] = {0x12, 0xFF, 0x04, 0x00, 0xf1, 0xFF, 0x00, 0xFF};	// 0xFF is dynamic data
	unsigned int txdata_size = 8;
	unsigned char rxdata[MAX_FILE_DATA_SIZE] = {0x0, };
	unsigned int rxdata_size = 0x2E;
	unsigned int wtxResponse_size = 8;
	unsigned int wtxResponse_cnt = 0;
	unsigned int wtxTime = 0;
	int ret_size = 0;
	ERR("selfTest command begin test_cmd: 0x%x \n",test_cmd);
	if (test_cmd > 0x7F || test_cmd < 0x01) {
		ERR("Wrong selfTest command");
		return -1;
	}

	if ((*seq) != 0 && (*seq) != 1) {
		ERR("Wrong sequnce format");
		return -1;
	}
	txdata[5] = (test_cmd) & 0xFF;
	txdata[1] = ((*seq) << 6) & 0xFF;
	txdata[sizeof(txdata)-1] = __cal_lrc(txdata, sizeof(txdata));


	wtxResponse_cnt += (test_cmd & SELFTEST_CMD_NVM_CHECKSUM) != 0 ? 1 : 0;
	wtxResponse_cnt += (test_cmd & SELFTEST_CMD_NVM_ACCESS) != 0 ? 1 : 0;
	wtxResponse_cnt += (test_cmd & SELFTEST_CMD_RAM_ACCESS) != 0 ? 1 : 0;
	wtxResponse_cnt += (test_cmd & SELFTEST_CMD_ENC_CHECK) != 0 ? 1 : 0;

 	msleep(2); //2ms

	ret_size = ese_i2c_send(g_k250a.client, txdata, txdata_size);
	if (ret_size != txdata_size) {
		ERR("mismatch send size : %u, %d", txdata_size, ret_size);
	}

	while(wtxResponse_cnt > 0) {
		ret_size = receive_data_with_polling(g_k250a.client, rxdata, wtxResponse_size);
		if (ret_size != wtxResponse_size) {
		    ERR("failed to get wtxReqeust data %d\n", wtxResponse_size);
		    goto out;
	    }
        if (rxdata[1] != 0xc3) {
            ERR("failed to wtxReqeust");
            goto out;
        }

		wtxTime = (rxdata[5] << 8) | rxdata[6];
		udelay(wtxTime * 1000);
		wtxResponse_cnt--;
	}

	ret_size = receive_data_with_polling(g_k250a.client, rxdata, rxdata_size);
	if (ret_size != rxdata_size) {
		ERR("receive data fail ret_size:%d rxdata_size %d\n", ret_size, rxdata_size);
		goto out;
	}
	if (rxdata[0x2b] != 0x90 || rxdata[0x2C] != 0x00) {
		ERR("return value : %x%x", rxdata[0x2b], rxdata[0x2c]);
		goto out;
	}

	if (memcpy(selfTestRes, rxdata + 3, rxdata_size - 6) < 0) {
		ERR("failed to copy rx data");
		goto out;
	}

	*selfTestRes_size = rxdata_size - 6;
	ADD_SEQUENCE(*seq);
	ERR("success seq %d, test_cmd 0x%x\n", *seq, test_cmd);
	return 0;
out:
	ADD_SEQUENCE(*seq);
	return -1;
}

static int k250a_selftest_work_func(void)
{
	unsigned char selfTestRes[MAX_FILE_DATA_SIZE] = {0x00, };
	unsigned int selfTestRes_size = MAX_FILE_DATA_SIZE;
	unsigned int seq = 1;
	int i = 0;
	int ret = 0;
	boot_firmware();
	get_info_origin();
	msleep(200);
	INFO("%s, enable mt6377-vtp!\n", __func__);
	ERR("k250a_selftest begin\n");
	// TEST_CHIP_SN
	if (selfTest(&seq, SELFTEST_CMD_TEST_CHIP_SN, selfTestRes, &selfTestRes_size) < 0) {
		ERR("failed to SelfTest - TEST_CHIP_SN");
		goto out;
	}
	print_buffer("TEST_CHIP_SN", selfTestRes + 4, 12);
	// end of TEST_CHIP_SN

	// TIMER
	if (selfTest(&seq, SELFTEST_CMD_TIMER, selfTestRes, &selfTestRes_size) < 0) {
		ERR("failed to SelfTest - TIMER");
		goto out;
	}
	print_buffer("TIMER", selfTestRes + 16, 4);
	// end of TIMER

	// PROVISION_AK
	if (selfTest(&seq, SELFTEST_CMD_PROVISION_AK, selfTestRes, &selfTestRes_size) < 0) {
		ERR("failed to SelfTest - PROVISION_AK");
		goto out;
	}
	print_buffer("PROVISION_AK", selfTestRes + 20, 4);
	// end of PROVISION_AK

	// NVM_CHECKSUM
	if (selfTest(&seq, SELFTEST_CMD_NVM_CHECKSUM, selfTestRes, &selfTestRes_size) < 0) {
		ERR("failed to SelfTest - NVM_CHECKSUM");
		goto out;
	}
	// end of NVM_CHECKSUM

	// NVM_ACCESS
	if (selfTest(&seq, SELFTEST_CMD_NVM_ACCESS, selfTestRes, &selfTestRes_size) < 0) {
		ERR("failed to SelfTest - NVM_ACCESS");
		goto out;
	}
	// end of NVM_ACCESS

	for (i = 0; i < SELFTEST_CMD_COUNT; i++) {
		ERR("RAM_ACCESS ENC_CHECK selftest count=%d\n", i);
		// RAM_ACCESS
		if (selfTest(&seq, SELFTEST_CMD_RAM_ACCESS, selfTestRes, &selfTestRes_size) < 0) {
			ERR("failed to SelfTest - RAM_ACCESS");
			goto out;
		}

		// ENC_CHECK
		if (selfTest(&seq, SELFTEST_CMD_ENC_CHECK, selfTestRes, &selfTestRes_size) < 0) {
			ERR("failed to SelfTest - ENC_CHECK");
			goto out;
		}
	}
	// TEST_ALL
	if (selfTest(&seq, SELFTEST_CMD_TEST_ALL, selfTestRes, &selfTestRes_size) < 0) {
		ERR("failed to SelfTest - TEST ALL");
		goto out;
	}
	print_buffer("TEST_CHIP_SN", selfTestRes + 4, 12);
	print_buffer("TIMER", selfTestRes + 16, 4);
	print_buffer("PROVISION_AK", selfTestRes + 20, 4);
	// end of TEST_ALL
	ret = regulator_disable(g_k250a.vdd);
	INFO("%s, SelfTest disable mt6377-vtp!\n", __func__);
	msleep(200);
	return 0;
out:
	ret = regulator_disable(g_k250a.vdd);
	INFO("%s, SelfTest disable mt6377-vtp!\n", __func__);
	msleep(200);
	return -1;
}

static ssize_t selftest_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int ret = 0;
	ERR("selftest_show\n");

	ret = k250a_selftest_work_func();
	if (ret < 0) {
		ERR("selftest cmd fail\n");
		return sprintf(buf, "FAIL");
	}
	ERR("SelfTest Pass - TEST ALL\n");
	return sprintf(buf, "Pass");
}

static ssize_t selftest_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	ERR("do selftest nothing\n");
	return count;
}

static ssize_t gethwinfo_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	ese_info_t *version = NULL;
	char date[13] = { 0 };
	uint8_t hw_info[12] = {'S', '3', 0x00, 0x00, 0x00, 0x00, 0x00, '_', 'R', 'E', 'V', 0x00};
	unsigned char info[MAX_FILE_DATA_SIZE] = {0x00, };
	unsigned int info_size = MAX_FILE_DATA_SIZE;
	unsigned int seq = 0;
	char chipno[128];
	char ChipSetName[30];
	char Crypto_verion[30];
	char Core_verion[30];
	const char *staticString = "S3K250A_REV"; 
	uint8_t chipValue;
	uint8_t byte_value;
	char tmp[3];
	int i;
	int ret;

	if (boot_firmware() < 0) {
		return -1;
	} else {
		seq = 0;
	}
	ERR("Boot Firmware Success");

	if (get_info(&seq, info, &info_size) < 0) {
		ERR("get_info fail");
		goto out;
	}
	version = (ese_info_t *)info;
	__getHWInfo(version->chip , hw_info);

	// HW Info :: hw_info
	// FW_ver :: version->core.version
	// CRYPTO_ver :: version->crypto.version

	print_buffer("chip", version->chip, CHIP_INFO_SIZE);
	for (i = 0; i < 12; i++) {
		byte_value = version->chip[16 + i];
		sprintf(tmp, "%02x", byte_value);
		strcat(chipno, tmp);
	}
	ERR("chipno: %s", chipno);

	chipValue = version->chip[42];
	sprintf(ChipSetName, "%s%x", staticString, chipValue);
	ERR("ChipSetName: %s", ChipSetName);
	
	print_buffer("keyflag", version->key_flag, KEY_FLAG_INFO_SIZE);
	print_buffer("partition", version->partition, PARTITION_INFO_SIZE);
	print_buffer("BL", version->bootloader.version, IMG_VERSION_SIZE);
	date[IMG_DATE_SIZE] = '\0';
	memcpy(date, version->bootloader.build_date, IMG_DATE_SIZE);

	print_buffer("CORE", version->core.version, IMG_VERSION_SIZE);
	for (i = 0; i < IMG_VERSION_SIZE; i++) {
		byte_value = version->core.version[i];
		sprintf(tmp, "%02x", byte_value);
		strcat(Core_verion, tmp);
	}
	ERR("Core_verion %s",Core_verion);
	memcpy(date, version->core.build_date, IMG_DATE_SIZE);
	print_buffer("CRYPTO", version->crypto.version, IMG_VERSION_SIZE);
	for (i = 0; i < IMG_VERSION_SIZE; i++) {
		byte_value = version->crypto.version[i]; 
		sprintf(tmp, "%02x", byte_value);
		strcat(Crypto_verion, tmp);
	}
	ERR("crypto_verion %s",Crypto_verion);
	memcpy(date, version->crypto.build_date, IMG_DATE_SIZE);
	print_buffer("HW INFO", hw_info, sizeof(hw_info));
	ERR("get HW_INFO pass");
out:
	ret = regulator_disable(g_k250a.vdd);
	INFO("%s, disable mt6377-vtp!\n", __func__);
	msleep(200);
	return sprintf(buf, "%s,%s,%s,%s\n", chipno, ChipSetName, Crypto_verion, Core_verion);
}

static ssize_t gethwinfo_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	ERR("gethwinfo_attr_store do nothing\n");
	return count;
}

static struct kobj_attribute selftest_attribute = __ATTR(selftest, 0644, selftest_show, selftest_store);
static struct kobj_attribute gethwinfo_attribute = __ATTR(gethwinfo, 0644, gethwinfo_show, gethwinfo_store);

static struct kobject *k250a_kobject;

static int k250a_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct device_node *np = client->dev.of_node;
	int ret = -1;
	INFO("Entry : %s\n", __func__);

	if (np) {
		g_k250a.vdd = devm_regulator_get_optional(&(client->dev), "1p8_pvdd");
		if (IS_ERR(g_k250a.vdd)) {
			ERR("%s - 1p8_pvdd can not be used\n", __func__);
			g_k250a.vdd = NULL;
		} else {
			ERR("%s - get 1p8_pvdd success \n", __func__);
		}

	} else {
		return -ENODEV;
	}

	g_k250a.client = client;
	star_dev.client = client;
	g_k250a.star = star_open(&star_dev);
	if (g_k250a.star == NULL) {
		return -ENODEV;
	}

	k250a_kobject = kobject_create_and_add("k250a_selftest", kernel_kobj);
	if (!k250a_kobject) {
		ERR("kobject_create_and_add fail\n");
		return -ENOMEM;
	}

	ret = sysfs_create_file(k250a_kobject, &selftest_attribute.attr);
	if (ret) {
		ERR("selftest_attribute fail\n");
		kobject_put(k250a_kobject);
		return ret;
	}

	ret = sysfs_create_file(k250a_kobject, &gethwinfo_attribute.attr);
	if (ret) {
		ERR("getinfo_attribute fail\n");
		sysfs_remove_file(k250a_kobject, &selftest_attribute.attr); 
		kobject_put(k250a_kobject);
		return ret;
	}

	INFO("Exit : %s\n", __func__);
	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
static int k250a_remove(struct i2c_client *client)
#else
static void k250a_remove(struct i2c_client *client)
#endif
{
	INFO("Entry : %s\n", __func__);
#if defined(USE_INTERNAL_PULLUP)
	devm_pinctrl_put(g_k250a.pinctrl);
	gpio_free(SCL_GPIO_NUM);
	gpio_free(SDA_GPIO_NUM);
#endif
	if (g_k250a.reset_gpio != 0) {
		gpio_free(g_k250a.reset_gpio);
	}
	sysfs_remove_file(k250a_kobject, &selftest_attribute.attr);
	sysfs_remove_file(k250a_kobject, &gethwinfo_attribute.attr);
	kobject_put(k250a_kobject);
	star_close(g_k250a.star);
	INFO("Exit : %s\n", __func__);
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
	return 0;
#endif
}

#if defined(CONFIG_SEC_SNVM_PLATFORM_DRV)
static int k250a_dev_open(struct inode *inode, struct file *filp)
{
	k250a_poweron();
#ifdef CONFIG_SEC_SNVM_I2C_CLOCK_CONTROL
	k250a_i2c_clock_enable();
#endif

	return 0;
}

static int ese_dev_release(struct inode *inode, struct file *filp)
{
#ifdef CONFIG_SEC_SNVM_I2C_CLOCK_CONTROL
	k250a_i2c_clock_disable();
#endif
	k250a_poweroff();

	return 0;
}

static const struct file_operations k250a_dev_fops = {
	.owner = THIS_MODULE,
	.open = k250a_dev_open,
	.release = ese_dev_release,
};

static struct miscdevice k250a_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "k250a",
	.fops = &k250a_dev_fops,
};

void k250a_parse_dt_for_platform_device(struct device *dev)
{
	g_k250a.vdd = devm_regulator_get_optional(dev, "1p8_pvdd");
	if (IS_ERR(g_k250a.vdd)) {
		ERR("%s - 1p8_pvdd can not be used\n", __func__);
		g_k250a.vdd = NULL;
	} else {
		INFO("%s: regulator_get success\n", __func__);
	}

#ifdef CONFIG_SEC_SNVM_I2C_CLOCK_CONTROL
	k250a_parse_i2c_clock(&g_k250a, dev->of_node);
#endif
}

static int k250a_platform_probe(struct platform_device *pdev)
{
	int ret = -1;

	k250a_parse_dt_for_platform_device(&pdev->dev);
	ret = misc_register(&k250a_misc_device);
	if (ret < 0)
		ERR("misc_register failed! %d\n", ret);

	INFO("%s: finished...\n", __func__);
	return 0;
}

static int k250a_platform_remove(struct platform_device *pdev)
{
	INFO("Entry : %s\n", __func__);
	return 0;
}

static const struct of_device_id k250a_secure_match_table[] = {
	{ .compatible = "sec_k250a_platform",},
	{},
};

static struct platform_driver k250a_platform_driver = {
	.driver = {
		.name = "k250a_platform",
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = k250a_secure_match_table,
#endif
	},
	.probe =  k250a_platform_probe,
	.remove = k250a_platform_remove,
};
#endif

static const struct i2c_device_id k250a_id[] = {
	{"k250a", 0},
	{}
};

static const struct of_device_id k250a_match_table[] = {
	{ .compatible = "sec_k250a",},
	{},
};

static struct i2c_driver k250a_driver = {
	.id_table = k250a_id,
	.probe = k250a_probe,
	.remove = k250a_remove,
	.driver = {
		.name = "k250a",
		.owner = THIS_MODULE,
		.of_match_table = k250a_match_table,
	},
};

static int __init k250a_init(void)
{
#if defined(CONFIG_SEC_SNVM_PLATFORM_DRV)
	int ret;

	ret = platform_driver_register(&k250a_platform_driver);
	if (!ret) {
		INFO("platform_driver_register success : %s\n", __func__);
		return ret;
	} else {
		ERR("platform_driver_register fail : %s\n", __func__);
		return ret;
	}
#endif
	INFO("Entry : %s\n", __func__);

	return i2c_add_driver(&k250a_driver);
}
module_init(k250a_init);

static void __exit k250a_exit(void)
{
	INFO("Entry : %s\n", __func__);
#if defined(CONFIG_SEC_SNVM_PLATFORM_DRV)
	platform_driver_unregister(&k250a_platform_driver);
	return;
#endif
	i2c_del_driver(&k250a_driver);
}

module_exit(k250a_exit);

MODULE_AUTHOR("Sec");
MODULE_DESCRIPTION("K250A driver");
MODULE_LICENSE("GPL");
