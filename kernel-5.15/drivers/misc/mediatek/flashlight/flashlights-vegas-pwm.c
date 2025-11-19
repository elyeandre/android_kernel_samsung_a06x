/*
 * Copyright (C) 2022 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": %s: " fmt, __func__

#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#include "../include/mt-plat/mtk_pwm.h"
#include "flashlight-core.h"
#include "flashlight-dt.h"

/* define device tree */
#ifndef VEGAS_PWM_DTNAME
#define VEGAS_PWM_DTNAME "mediatek,flashlights_vegas_pwm"
#endif

/* define driver name */
#define VEGAS_FLASHLIGHT_NAME "flashlights-vegas-pwm"

/* define pwm parameters */
#define VEGAS_PWM_NUMBER                1
#define VEGAS_PWM_PERIOD                1000

/* define current, level */
#define FLASH_FIRE_HIGH_MAXCURRENT      1259
#define FLASH_FIRE_LOW_MAXCURRENT       118
#define VEGAS_LEVEL_NUM                 26
#define VEGAS_LEVEL_TORCH               7
#define VEGAS_HW_TIMEOUT                500 /* ms */

/* Platform device and driver */
static struct class *flashlight_class;
static struct device *flashlight_device;
static dev_t flashlight_devno;
static unsigned int flashlight_enable;
static unsigned int torch_mode_flag = 0;


/* TORCH_BRIGHTNESS_LEVEL_1 1001 55mA
   TORCH_BRIGHTNESS_LEVEL_2 1002 70mA
   TORCH_BRIGHTNESS_LEVEL_3 1003 85mA
   TORCH_BRIGHTNESS_LEVEL_5 1005 100mA
   TORCH_BRIGHTNESS_LEVEL_7 1007 115mA
*/
enum {
	TORCH_BRIGHTNESS_LEVEL_1 = 1001,
	TORCH_BRIGHTNESS_LEVEL_2 = 1002,
	TORCH_BRIGHTNESS_LEVEL_3 = 1003,
	TORCH_BRIGHTNESS_LEVEL_5 = 1005,
	TORCH_BRIGHTNESS_LEVEL_7 = 1007
};

enum {
	TORCH_CURRENT_LEVEL_1 = 55,
	TORCH_CURRENT_LEVEL_2 = 70,
	TORCH_CURRENT_LEVEL_3 = 85,
	TORCH_CURRENT_LEVEL_5 = 100,
	TORCH_CURRENT_LEVEL_7 = 115
};


enum vegas_flash_opcode {
	VEGAS_FLASH_OP_NULL,
	VEGAS_FLASH_OP_FIRELOW,
	VEGAS_FLASH_OP_FIREHIGH
};

enum vegas_flash_pwm {
	VEGAS_FLASH_PWM_OFF,
	VEGAS_FLASH_PWM_ON
};

static struct regmap *pwm_src_regmap;

/* define mutex and work queue */
static DEFINE_MUTEX(vegas_flashlight_mutex);
static struct work_struct vegas_work;

/* define pinctrl */
#define VEGAS_PINCTRL_PIN_GPIO         1
#define VEGAS_PINCTRL_PIN_PWM          0
#define VEGAS_PINCTRL_PIN_PWM_EN       2
#define VEGAS_PINCTRL_PINSTATE_LOW     0
#define VEGAS_PINCTRL_PINSTATE_HIGH    1
#define VEGAS_PINCTRL_STATE_GPIO_HIGH  "flashlight_gpio_high"
#define VEGAS_PINCTRL_STATE_GPIO_LOW   "flashlight_gpio_low"
#define VEGAS_PINCTRL_STATE_PWM_HIGH   "flashlight_pwm_high"
#define VEGAS_PINCTRL_STATE_PWM_LOW    "flashlight_pwm_low"
#define VEGAS_PINCTRL_STATE_PWM_EN     "flashlight_pwm_en"
static  struct pinctrl                  *vegas_pinctrl;
static  struct pinctrl_state            *vegas_gpio_high;
static  struct pinctrl_state            *vegas_gpio_low;
static  struct pinctrl_state            *vegas_pwm_high;
static  struct pinctrl_state            *vegas_pwm_low;
static  struct pinctrl_state            *vegas_pwm_en;

/* define usage count */
static int use_count;

/* platform data */
struct vegas_flashlight_platform_data {
	int channel_num;
	struct flashlight_device_id *dev_id;
};

/* flash operation data */
struct vegas_flash_operation_data {
	enum vegas_flash_opcode opcode;
	enum vegas_flash_pwm pwm_state;
	u32 flash_current;
};

/* define flash operation data */
static struct vegas_flash_operation_data flash_opdata = {
	.opcode        = VEGAS_FLASH_OP_NULL,
	.pwm_state     = VEGAS_FLASH_PWM_OFF,
	.flash_current = 0,
};

/******************************************************************************
 * Pinctrl configuration
 *****************************************************************************/
static int vegas_flashlight_pinctrl_init(struct platform_device *pdev)
{
	int ret = 0;
	pr_err("%s: E.\n",__func__);

	/* get pinctrl */
	vegas_pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(vegas_pinctrl)) {
		pr_err("Failed to get flashlight pinctrl.\n");
		ret = PTR_ERR(vegas_pinctrl);
		return ret;
	}

	/* Flashlight ENM pin initialization */
	vegas_gpio_high = pinctrl_lookup_state(
			vegas_pinctrl, VEGAS_PINCTRL_STATE_GPIO_HIGH);
	if (IS_ERR(vegas_gpio_high)) {
		pr_err("Failed to init (%s)\n", VEGAS_PINCTRL_STATE_GPIO_HIGH);
		ret = PTR_ERR(vegas_gpio_high);
	}
	vegas_gpio_low = pinctrl_lookup_state(
			vegas_pinctrl, VEGAS_PINCTRL_STATE_GPIO_LOW);
	if (IS_ERR(vegas_gpio_low)) {
		pr_err("Failed to init (%s)\n", VEGAS_PINCTRL_STATE_GPIO_LOW);
		ret = PTR_ERR(vegas_gpio_low);
	}

	/* Flashlight ENF pin initialization */
	vegas_pwm_high = pinctrl_lookup_state(
			vegas_pinctrl, VEGAS_PINCTRL_STATE_PWM_HIGH);
	if (IS_ERR(vegas_pwm_high)) {
		pr_err("Failed to init (%s)\n", VEGAS_PINCTRL_STATE_PWM_HIGH);
		ret = PTR_ERR(vegas_pwm_high);
	}
	vegas_pwm_low = pinctrl_lookup_state(
			vegas_pinctrl, VEGAS_PINCTRL_STATE_PWM_LOW);
	if (IS_ERR(vegas_pwm_low)) {
		pr_err("Failed to init (%s)\n", VEGAS_PINCTRL_STATE_PWM_LOW);
		ret = PTR_ERR(vegas_pwm_low);
	}

	vegas_pwm_en = pinctrl_lookup_state(
			vegas_pinctrl, VEGAS_PINCTRL_STATE_PWM_EN);
	if (IS_ERR(vegas_pwm_low)) {
		pr_err("Failed to init (%s)\n", VEGAS_PINCTRL_STATE_PWM_EN);
		ret = PTR_ERR(vegas_pwm_en);
	}

	pr_err("%s: X.\n",__func__);

	return ret;
}

static int vegas_pinctrl_set(int pin, int state)
{
	int ret = 0;

	pr_err("%s: E.\n",__func__);

	if (IS_ERR(vegas_pinctrl)) {
		pr_err("pinctrl is not available\n");
		return -1;
	}

	switch (pin) {
	case VEGAS_PINCTRL_PIN_GPIO:
		if (state == VEGAS_PINCTRL_PINSTATE_LOW &&
				!IS_ERR(vegas_gpio_low))
			pinctrl_select_state(vegas_pinctrl, vegas_gpio_low);
		else if (state == VEGAS_PINCTRL_PINSTATE_HIGH &&
				!IS_ERR(vegas_gpio_high))
			pinctrl_select_state(vegas_pinctrl, vegas_gpio_high);
		else
			pr_err("set err, pin(%d) state(%d)\n", pin, state);
		break;
	case VEGAS_PINCTRL_PIN_PWM:
		if (state == VEGAS_PINCTRL_PINSTATE_LOW &&
				!IS_ERR(vegas_pwm_low))
			pinctrl_select_state(vegas_pinctrl, vegas_pwm_low);
		else if (state == VEGAS_PINCTRL_PINSTATE_HIGH &&
				!IS_ERR(vegas_pwm_high))
			pinctrl_select_state(vegas_pinctrl, vegas_pwm_high);
		else
			pr_err("set err, pin(%d) state(%d)\n", pin, state);
		break;
	case VEGAS_PINCTRL_PIN_PWM_EN:
		if (state == VEGAS_PINCTRL_PINSTATE_LOW &&
				!IS_ERR(vegas_pwm_low))
			pinctrl_select_state(vegas_pinctrl, vegas_pwm_low);
		else if (state == VEGAS_PINCTRL_PINSTATE_HIGH &&
				!IS_ERR(vegas_pwm_en))
			pinctrl_select_state(vegas_pinctrl, vegas_pwm_en);
		else
			pr_err("set err, pin(%d) state(%d)\n", pin, state);
		break;
	default:
		pr_err("set err, pin(%d) state(%d)\n", pin, state);
		break;
	}
	pr_err("pin(%d) state(%d)\n", pin, state);

	pr_err("%s: X.\n",__func__);

	return ret;
}

void mt_pwm_26M_clk_sel(void)
{
	regmap_update_bits(pwm_src_regmap, 0x24, 0x3 << 18 | 0x3 << 12, 0x1 << 18 | 0x1 << 12);
	return;
}

static int vegas_flashlight_set_pwm(int pwm_num, u32 flash_current, u32 flash_maxcurrent)
{
	struct pwm_spec_config pwm_setting;
	memset(&pwm_setting, 0, sizeof(struct pwm_spec_config));
	pr_err("%s: E.\n",__func__);
	flash_opdata.pwm_state                    = VEGAS_FLASH_PWM_ON;

	pwm_setting.pwm_no                        = pwm_num;
	pwm_setting.mode                          = PWM_MODE_OLD;
	pwm_setting.pmic_pad                      = 0;
	pwm_setting.clk_div                       = CLK_DIV1;
	pwm_setting.clk_src                       = PWM_CLK_OLD_MODE_BLOCK;
	pwm_setting.PWM_MODE_OLD_REGS.IDLE_VALUE  = 0;
	pwm_setting.PWM_MODE_OLD_REGS.GUARD_VALUE = 0;
	pwm_setting.PWM_MODE_OLD_REGS.GDURATION   = 0;
	pwm_setting.PWM_MODE_OLD_REGS.WAVE_NUM    = 0;
	pwm_setting.PWM_MODE_OLD_REGS.DATA_WIDTH  = VEGAS_PWM_PERIOD;
	pwm_setting.PWM_MODE_OLD_REGS.THRESH      = VEGAS_PWM_PERIOD * flash_current / flash_maxcurrent ;

	// mt_pwm_26M_clk_sel();
	pwm_set_spec_config(&pwm_setting);

	pr_err("Set pwm_no = %d,PWM thresh = %u, period = %u, flash_current = %u, flash_maxcurrent = %u",
		pwm_setting.pwm_no,pwm_setting.PWM_MODE_OLD_REGS.THRESH, pwm_setting.PWM_MODE_OLD_REGS.DATA_WIDTH,
		flash_current, flash_maxcurrent);
	pr_err("%s: X.\n",__func__);
	return 0;
}

/******************************************************************************
 * vegas pwm-flashlight operations
 *****************************************************************************/
static const u32 vegas_torch_current[VEGAS_LEVEL_NUM] = {
	10, 30, 55, 70, 85, 100, 115, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const u32 vegas_flash_current[VEGAS_LEVEL_NUM] = {
	10,  30,  55,  70,  85,  100, 115, 150, 200, 250,
	300, 350, 400, 450, 500, 550, 600, 650, 700, 750,
	800, 850, 900, 950, 1000, 1100
};

static void os_mdelay(unsigned long ms)
{
	unsigned long us = ms*1000;
	usleep_range(us, us+2000);
}

static int vegas_is_torch(int level)
{
	if (level >= VEGAS_LEVEL_TORCH)
		return -1;

	return 0;
}

static int vegas_verify_level(int level)
{
	if (level < 0)
		level = 0;
	else if (level >= VEGAS_LEVEL_NUM)
		level = VEGAS_LEVEL_NUM - 1;

	return level;
}

/* set flashlight level */
static int vegas_set_level(int level, struct vegas_flash_operation_data *fl_opdata)
{
	pr_err("%s: E.\n",__func__);
	/* wrap set level function */
	level = vegas_verify_level(level);
	if(!vegas_is_torch(level)) {
		fl_opdata->opcode        = VEGAS_FLASH_OP_FIRELOW;
		fl_opdata->flash_current = vegas_torch_current[level];
		pr_err("set level %d current = %u, opcode = %d will enter movie mode",
			level, fl_opdata->flash_current, fl_opdata->opcode);
	} else {
		fl_opdata->opcode        = VEGAS_FLASH_OP_FIREHIGH;
		fl_opdata->flash_current = vegas_flash_current[level];
		pr_err("set level %d current = %u, opcode = %d will enter flash mode",
			level, fl_opdata->flash_current, fl_opdata->opcode);
	}
	pr_err("%s: X.\n",__func__);
	return 0;
}

/* flashlight enable function */
static int vegas_enable(struct vegas_flash_operation_data *fl_opdata)
{
	enum vegas_flash_opcode opcode = fl_opdata->opcode;
	u32 flash_current = fl_opdata->flash_current;

	pr_err("%s: E.\n",__func__);

	/* wrap enable function */
	switch (opcode)
	{
	case VEGAS_FLASH_OP_FIRELOW:
		vegas_pinctrl_set(VEGAS_PINCTRL_PIN_GPIO, VEGAS_PINCTRL_PINSTATE_LOW);
		vegas_pinctrl_set(VEGAS_PINCTRL_PIN_PWM, VEGAS_PINCTRL_PINSTATE_HIGH);
		os_mdelay(6);
		vegas_flashlight_set_pwm(VEGAS_PWM_NUMBER, flash_current, FLASH_FIRE_LOW_MAXCURRENT);
		vegas_pinctrl_set(VEGAS_PINCTRL_PIN_PWM_EN, VEGAS_PINCTRL_PINSTATE_HIGH);
		break;
	case VEGAS_FLASH_OP_FIREHIGH:
		vegas_pinctrl_set(VEGAS_PINCTRL_PIN_GPIO, VEGAS_PINCTRL_PINSTATE_HIGH);
		vegas_flashlight_set_pwm(VEGAS_PWM_NUMBER, flash_current, FLASH_FIRE_HIGH_MAXCURRENT);
		vegas_pinctrl_set(VEGAS_PINCTRL_PIN_PWM_EN, VEGAS_PINCTRL_PINSTATE_HIGH);
		break;
	default:
		pr_err("Flash opcode is error,failed to enable flashlight.\n");
		return -1;
	}
	pr_err("%s: X.\n",__func__);
	return 0;
}

/* flashlight disable function */
static int vegas_disable(struct vegas_flash_operation_data *fl_opdata)
{
	pr_err("%s: E.\n",__func__);
	/* wrap disable function */
	if (fl_opdata->pwm_state == VEGAS_FLASH_PWM_ON) {
		mt_pwm_disable(VEGAS_PWM_NUMBER, true);
		fl_opdata->pwm_state = VEGAS_FLASH_PWM_OFF;
		vegas_pinctrl_set(VEGAS_PINCTRL_PIN_PWM_EN, VEGAS_PINCTRL_PINSTATE_LOW);
	}
	vegas_pinctrl_set(VEGAS_PINCTRL_PIN_PWM, VEGAS_PINCTRL_PINSTATE_LOW);
	vegas_pinctrl_set(VEGAS_PINCTRL_PIN_GPIO, VEGAS_PINCTRL_PINSTATE_LOW);

	/* clear torch_mode_flag */
	torch_mode_flag = 0;
	pr_err("%s: X.\n",__func__);

	return 0;
}

/* flashlight init */
static int vegas_init(void)
{
	pr_err("%s: E.\n",__func__);
	/* wrap init function */
	vegas_pinctrl_set(VEGAS_PINCTRL_PIN_PWM, 0);
	vegas_pinctrl_set(VEGAS_PINCTRL_PIN_GPIO, 0);
	pr_err("%s: X.\n",__func__);
	return 0;
}

/* flashlight uninit */
static int vegas_uninit(void)
{
	pr_err("%s: E.\n",__func__);
	/* wrap uninit function */
	vegas_pinctrl_set(VEGAS_PINCTRL_PIN_PWM, 0);
	vegas_pinctrl_set(VEGAS_PINCTRL_PIN_GPIO, 0);
	pr_err("%s: X.\n",__func__);
	return 0;
}

/******************************************************************************
 * Timer and work queue
 *****************************************************************************/
static struct hrtimer vegas_timer;
static unsigned int vegas_timeout_ms;

static void vegas_flashlight_work_disable(struct work_struct *data)
{
	pr_err("%s: E.\n",__func__);
	pr_err("work queue callback\n");
	vegas_disable(&flash_opdata);
}

static enum hrtimer_restart vegas_timer_func(struct hrtimer *timer)
{
	pr_err("%s: E.\n",__func__);
	schedule_work(&vegas_work);
	return HRTIMER_NORESTART;
}

/******************************************************************************
 * Flashlight operations
 *****************************************************************************/
static int vegas_ioctl(unsigned int cmd, unsigned long arg)
{
	struct flashlight_dev_arg *fl_arg;
	int channel;
	ktime_t ktime;
	unsigned int s;
	unsigned int ns;

	pr_err("%s: E.\n",__func__);

	fl_arg = (struct flashlight_dev_arg *)arg;
	channel = fl_arg->channel;

	switch (cmd) {
	case FLASH_IOC_SET_TIME_OUT_TIME_MS:
		pr_err("FLASH_IOC_SET_TIME_OUT_TIME_MS(%d): %d\n",
				channel, (int)fl_arg->arg);
		vegas_timeout_ms = fl_arg->arg;
		break;

	case FLASH_IOC_SET_DUTY:
		pr_err("FLASH_IOC_SET_DUTY(%d): %d\n",
				channel, (int)fl_arg->arg);
		vegas_set_level(fl_arg->arg, &flash_opdata);
		break;

	case FLASH_IOC_SET_ONOFF:
		pr_err("FLASH_IOC_SET_ONOFF(%d): %d\n",
				channel, (int)fl_arg->arg);
		if (fl_arg->arg == 1) {
			if (vegas_timeout_ms) {
				if(vegas_timeout_ms > VEGAS_HW_TIMEOUT)
					vegas_timeout_ms = VEGAS_HW_TIMEOUT;
				s = vegas_timeout_ms / 1000;
				ns = vegas_timeout_ms % 1000 * 1000000;
				ktime = ktime_set(s, ns);
				hrtimer_start(&vegas_timer, ktime,
						HRTIMER_MODE_REL);
			}
			vegas_enable(&flash_opdata);
		} else {
			vegas_disable(&flash_opdata);
			hrtimer_cancel(&vegas_timer);
		}
		break;

	case FLASH_IOC_GET_DUTY_NUMBER:
		pr_err("FLASH_IOC_GET_DUTY_NUMBER(%d)\n", channel);
		fl_arg->arg = VEGAS_LEVEL_NUM;
		break;

	case FLASH_IOC_GET_MAX_TORCH_DUTY:
		pr_err("FLASH_IOC_GET_MAX_TORCH_DUTY(%d)\n", channel);
		fl_arg->arg = VEGAS_LEVEL_TORCH - 1;
		break;

	case FLASH_IOC_GET_DUTY_CURRENT:
		fl_arg->arg = vegas_verify_level(fl_arg->arg);
		pr_err("FLASH_IOC_GET_DUTY_CURRENT(%d): %d\n",
				channel, (int)fl_arg->arg);
		fl_arg->arg = vegas_torch_current[fl_arg->arg];
		break;

	case FLASH_IOC_GET_HW_TIMEOUT:
		pr_err("FLASH_IOC_GET_HW_TIMEOUT(%d)\n", channel);
		fl_arg->arg = VEGAS_HW_TIMEOUT;
		break;

	default:
		pr_err("No such command and arg(%d): (%d, %d)\n",
				channel, _IOC_NR(cmd), (int)fl_arg->arg);
		return -ENOTTY;
	}

	pr_err("%s: X.\n",__func__);

	return 0;
}

static int vegas_open(void)
{
	/* Move to set driver for saving power */
	return 0;
}

static int vegas_release(void)
{
	/* Move to set driver for saving power */
	return 0;
}

static int vegas_flashlight_set_driver(int set)
{
	int ret = 0;

	pr_err("%s: E.\n",__func__);

	/* set chip and usage count */
	mutex_lock(&vegas_flashlight_mutex);
	if (set) {
		if (!use_count)
			ret = vegas_init();
		use_count++;
		pr_err("Set driver: %d\n", use_count);
	} else {
		use_count--;
		if (!use_count)
			ret = vegas_uninit();
		if (use_count < 0)
			use_count = 0;
		pr_err("Unset driver: %d\n", use_count);
	}
	mutex_unlock(&vegas_flashlight_mutex);

	pr_err("%s: X.\n",__func__);

	return ret;
}

static ssize_t vegas_strobe_store(struct flashlight_arg arg)
{
	pr_err("%s: E.\n",__func__);
	vegas_flashlight_set_driver(1);
	vegas_set_level(arg.level, &flash_opdata);
	vegas_timeout_ms = 0;
	vegas_enable(&flash_opdata);
	msleep(arg.dur);
	vegas_disable(&flash_opdata);
	vegas_flashlight_set_driver(0);
	pr_err("%s: X.\n",__func__);

	return 0;
}

static struct flashlight_operations vegas_flashlight_ops = {
	vegas_open,
	vegas_release,
	vegas_ioctl,
	vegas_strobe_store,
	vegas_flashlight_set_driver
};


/******************************************************************************
 * Platform device and driver
 *****************************************************************************/
static int vegas_chip_init(void)
{
	/* NOTE: Chip initialication move to "set driver" for power saving.
	 * vegas_init();
	 */

	return 0;
}

static int vegas_parse_dt(struct device *dev,
		struct vegas_flashlight_platform_data *pdata)
{
	struct device_node *np, *cnp;
	u32 decouple = 0;
	int i = 0;

	pr_err("%s: E.\n",__func__);

	if (!dev || !dev->of_node || !pdata)
		return -ENODEV;

	np = dev->of_node;

	pdata->channel_num = of_get_child_count(np);
	if (!pdata->channel_num) {
		pr_err("Parse no dt, node.\n");
		return 0;
	}
	pr_err("Channel number(%d).\n", pdata->channel_num);

	if (of_property_read_u32(np, "decouple", &decouple))
		pr_err("Parse no dt, decouple.\n");

	pdata->dev_id = devm_kzalloc(dev,
			pdata->channel_num *
			sizeof(struct flashlight_device_id),
			GFP_KERNEL);
	if (!pdata->dev_id)
		return -ENOMEM;

	for_each_child_of_node(np, cnp) {
		if (of_property_read_u32(cnp, "type", &pdata->dev_id[i].type))
			goto err_node_put;
		if (of_property_read_u32(cnp, "ct", &pdata->dev_id[i].ct))
			goto err_node_put;
		if (of_property_read_u32(cnp, "part", &pdata->dev_id[i].part))
			goto err_node_put;
		snprintf(pdata->dev_id[i].name, FLASHLIGHT_NAME_SIZE,
				VEGAS_FLASHLIGHT_NAME);
		pdata->dev_id[i].channel = i;
		pdata->dev_id[i].decouple = decouple;

		pr_err("Parse dt (type,ct,part,name,channel,decouple)=(%d,%d,%d,%s,%d,%d).\n",
				pdata->dev_id[i].type, pdata->dev_id[i].ct,
				pdata->dev_id[i].part, pdata->dev_id[i].name,
				pdata->dev_id[i].channel,
				pdata->dev_id[i].decouple);
		i++;
	}

	pr_err("%s: X.\n",__func__);

	return 0;

err_node_put:
	of_node_put(cnp);
	return -EINVAL;
}

static ssize_t vegas_flashlight_show(struct device* dev, struct device_attribute* attr, char* buf)
{
	pr_err("flashlight_enable = %u.\n", flashlight_enable);
	return sprintf(buf, "%u\n", flashlight_enable);
}

static ssize_t vegas_flashlight_store(struct device* dev, struct device_attribute* attr, const char* buf, size_t count)
{
	unsigned int value;
	int ret;
	struct vegas_flash_operation_data test;

	ret = kstrtou32(buf, 10, &value);
	if (ret) {
		return ret;
	}

	flashlight_enable = value;
	pr_err("111 flashlight_enable = %u.\n", flashlight_enable);

	if (flashlight_enable) {
		if (torch_mode_flag == 0) {
			vegas_pinctrl_set(VEGAS_PINCTRL_PIN_GPIO, VEGAS_PINCTRL_PINSTATE_LOW);
			vegas_pinctrl_set(VEGAS_PINCTRL_PIN_PWM, VEGAS_PINCTRL_PINSTATE_HIGH);
			os_mdelay(6);
		}
		switch (flashlight_enable) {
			case TORCH_BRIGHTNESS_LEVEL_1:
				vegas_flashlight_set_pwm(VEGAS_PWM_NUMBER, TORCH_CURRENT_LEVEL_1, FLASH_FIRE_LOW_MAXCURRENT);
				torch_mode_flag = 1;
				break;
			case TORCH_BRIGHTNESS_LEVEL_2:
				vegas_flashlight_set_pwm(VEGAS_PWM_NUMBER, TORCH_CURRENT_LEVEL_2, FLASH_FIRE_LOW_MAXCURRENT);
				torch_mode_flag = 1;
				break;
			case TORCH_BRIGHTNESS_LEVEL_3:
				vegas_flashlight_set_pwm(VEGAS_PWM_NUMBER, TORCH_CURRENT_LEVEL_3, FLASH_FIRE_LOW_MAXCURRENT);
				torch_mode_flag = 1;
				break;
			case TORCH_BRIGHTNESS_LEVEL_5:
				vegas_flashlight_set_pwm(VEGAS_PWM_NUMBER, TORCH_CURRENT_LEVEL_5, FLASH_FIRE_LOW_MAXCURRENT);
				torch_mode_flag = 1;
				break;
			case TORCH_BRIGHTNESS_LEVEL_7:
				vegas_flashlight_set_pwm(VEGAS_PWM_NUMBER, TORCH_CURRENT_LEVEL_7, FLASH_FIRE_LOW_MAXCURRENT);
				torch_mode_flag = 1;
				break;
			default:
				vegas_flashlight_set_pwm(VEGAS_PWM_NUMBER, TORCH_CURRENT_LEVEL_3, FLASH_FIRE_LOW_MAXCURRENT);
				torch_mode_flag = 1;
				break;
		}
		vegas_pinctrl_set(VEGAS_PINCTRL_PIN_PWM_EN, VEGAS_PINCTRL_PINSTATE_HIGH);
	} else {
		test.opcode = VEGAS_FLASH_OP_FIRELOW;
		test.flash_current = 0;
		vegas_disable(&test);
	}
	pr_err("222 flashlight_enable = %u.\n", flashlight_enable);

	return count;
}

static DEVICE_ATTR(rear_flash, S_IRUGO|S_IWUSR, vegas_flashlight_show, vegas_flashlight_store);

static int vegas_flashlight_create_device(void)
{
	/* allocate char device number */
	pr_err("Create device node E.\n");
	if (alloc_chrdev_region(&flashlight_devno, 0, 1, "flash")) {
		pr_err("Failed to allocate char device region\n");
		return -1;
	}
	pr_err("Allocate major number and minor number: (%d, %d)\n", MAJOR(flashlight_devno), MINOR(flashlight_devno));

	/* create class */
	flashlight_class = class_create(THIS_MODULE, "camera");
	if (IS_ERR(flashlight_class)) {
		pr_err("Failed to create class (%d)\n",
			   (int)PTR_ERR(flashlight_class));
		return -1;
	}

	/* create device */
	flashlight_device = device_create(flashlight_class, NULL, flashlight_devno, NULL, "flash");
	if (!flashlight_device) {
		pr_err("Failed to create device\n");
		return -1;
	}

	/* create device file */
	if (device_create_file(flashlight_device, &dev_attr_rear_flash)) {
		pr_err("Failed to create device file(strobe)\n");
		return -1;
	}
	pr_err("Create device node X.\n");

	return 0;
}

static int vegas_flashlight_probe(struct platform_device *pdev)
{
	struct vegas_flashlight_platform_data *pdata = dev_get_platdata(&pdev->dev);
	int err;
	int i;

	pr_err("%s:Probe start E.\n",__func__);

	/* init pinctrl */
	if (vegas_flashlight_pinctrl_init(pdev)) {
		pr_err("Failed to init pinctrl.\n");
		err = -EFAULT;
		goto err;
	}

	pwm_src_regmap = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
		"srcclk");
	if (IS_ERR(pwm_src_regmap)) {
		dev_err(&pdev->dev, "Cannot find pwm src controller: %ld\n",
		PTR_ERR(pwm_src_regmap));
	}

	/* init platform data */
	if (!pdata) {
		pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata) {
			err = -ENOMEM;
			goto err;
		}
		pdev->dev.platform_data = pdata;
		err = vegas_parse_dt(&pdev->dev, pdata);
		if (err)
			goto err;
	}

	/* init work queue */
	INIT_WORK(&vegas_work, vegas_flashlight_work_disable);

	/* init timer */
	hrtimer_init(&vegas_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	vegas_timer.function = vegas_timer_func;
	vegas_timeout_ms = 100;

	/* init chip hw */
	vegas_chip_init();

	/* clear usage count */
	use_count = 0;

	/* register flashlight device */
	if (pdata->channel_num) {
		for (i = 0; i < pdata->channel_num; i++)
			if (flashlight_dev_register_by_device_id(
						&pdata->dev_id[i],
						&vegas_flashlight_ops)) {
				err = -EFAULT;
				goto err;
			}
	} else {
		if (flashlight_dev_register(VEGAS_FLASHLIGHT_NAME, &vegas_flashlight_ops)) {
			err = -EFAULT;
			goto err;
		}
	}

	/* create flash device node */
	vegas_flashlight_create_device();

	pr_err("%s:Probe done X.\n",__func__);

	return 0;

err:
	return err;
}

static int vegas_flashlight_remove(struct platform_device *pdev)
{
	struct vegas_flashlight_platform_data *pdata = dev_get_platdata(&pdev->dev);
	int i;

	pr_err("%s:Remove start E.\n",__func__);

	pdev->dev.platform_data = NULL;

	/* unregister flashlight device */
	if (pdata && pdata->channel_num)
		for (i = 0; i < pdata->channel_num; i++)
			flashlight_dev_unregister_by_device_id(
					&pdata->dev_id[i]);
	else
		flashlight_dev_unregister(VEGAS_FLASHLIGHT_NAME);

	/* flush work queue */
	flush_work(&vegas_work);

	pr_err("%s:Remove done X.\n",__func__);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id vegas_pwm_of_match[] = {
	{.compatible = VEGAS_PWM_DTNAME},
	{},
};
MODULE_DEVICE_TABLE(of, vegas_pwm_of_match);
#else
static struct platform_device vegas_pwm_platform_device[] = {
	{
		.name = VEGAS_FLASHLIGHT_NAME,
		.id = 0,
		.dev = {}
	},
	{}
};
MODULE_DEVICE_TABLE(platform, vegas_pwm_platform_device);
#endif

static struct platform_driver vegas_pwm_platform_driver = {
	.probe = vegas_flashlight_probe,
	.remove = vegas_flashlight_remove,
	.driver = {
		.name = VEGAS_FLASHLIGHT_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = vegas_pwm_of_match,
#endif
	},
};

static int __init flashlight_vegas_init(void)
{
	int ret;

	pr_err("Init start.\n");

#ifndef CONFIG_OF
	ret = platform_device_register(&vegas_pwm_platform_device);
	if (ret) {
		pr_err("Failed to register platform device\n");
		return ret;
	}
#endif

	ret = platform_driver_register(&vegas_pwm_platform_driver);
	if (ret) {
		pr_err("Failed to register platform driver\n");
		return ret;
	}

	pr_err("Init done.\n");

	return 0;
}

static void __exit flashlight_vegas_exit(void)
{
	pr_err("Exit start.\n");

	platform_driver_unregister(&vegas_pwm_platform_driver);

	pr_err("Exit done.\n");
}

module_init(flashlight_vegas_init);
module_exit(flashlight_vegas_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LIU fulin <liufulin2@longcheer.com>");
MODULE_DESCRIPTION("VEGAS MTK PWM Flashlight Drive");
