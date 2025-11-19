/// SPDX-License-Identifier: GPL-2.0
/*
 * Chrager driver for Sgm41542S
 *
 * Copyright (c) 2024 sg-micro.com.
 *
 * Author: hongqiang_qin <hongqiang_qin@sg-micro.com>
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/types.h>
#include <linux/phy/phy.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <dt-bindings/iio/qti_power_supply_iio.h>
#include <linux/iio/consumer.h>
#include <linux/math64.h>


#include "charger_class.h"
#include "mtk_charger.h"
#include "mtk_battery.h"
#include "sgm41542s_reg.h"
#include "sgm41542s_iio.h"
#include "tcpm.h"
#include "nopmi/qcom-pmic-voter.h"
#include "nopmi_vote.h"

#define PHY_MODE_BC11_SET 1
#define PHY_MODE_BC11_CLR 2


struct mutex i2c_rw_lock;
static bool sgm_init_flag;


static int dbg_enable;

module_param_named(dbg_level, dbg_enable, int, 0644);

//#define pr_fmt(fmt) "[sgm41542s-charger]:%s:" fmt, __func__
#define DBG(args...) \
	do { \
			pr_info(args); \
	} while (0)

#define SGM41542S_MANUFACTURER	"SGMICRO"
#define SGM41542S_NAME   "SGM41542S"

static struct charger_device *s_chg_dev;
static const struct charger_properties sgm41542S_chg_props = {
		.alias_name = "41542S",	};

enum {    
    ADC_VBAT,
    ADC_VSYS,
    ADC_TS,
    ADC_VBUS,    
    ADC_IBAT,    
    ADC_IBUS,
    ADC_MAX_NUM,
}SGM41542S_ADC_CH;

static const char *const sgm41542s_adc_name[ADC_MAX_NUM] = {
	"ADC_VBAT",
	"ADC_VSYS",
    "ADC_TS",
	"ADC_VBUS",
    "ADC_IBAT",
	"ADC_IBUS",
};

static const char *const sgm41542s_chrg_types[] = {
	"No input",
	"SDP",
    "CDP",
	"DCP", 
	"resered",
	"UNKNOW",	
	"Non-standard",	
	"OTG",
};

static const char *const sgm41542s_chrg_stat[] = {
	"Charge disable",
	"Pre-charge",
	"Fast charging",
    "Charging terminated",
};

static const char *const sgm41542s_chrg_fault[] = {
	"Normal",
	"Input fault (VBUS OVP or VBAT < VVBUS < 3.8V)",
    "Thermal shutdown",
	"Charge safety timer expired",

};

static const char *const sgm41542s_ntc_fault[] = {
	"Normal",
	"Reserved",
    "warm",
	"cold",  
	"hot",
	"resered",
	"resered2",
	"resered3",
};

/* SGM41542S REG06 BOOST_LIM[5:4], uV */
static const unsigned int SYS_MIN[] = {
	2600000, 2800000, 3000000, 3200000,3400000,3500000,3600000,3700000
};

static const unsigned int BOOST_CURRENT_LIMIT[] = {
	500000, 1000000, 1200000, 1500000, 2000000, 2500000, 3000000, 3200000,
};

static const unsigned int WATCHDOG_STABLE[] = {
	0, 40, 80, 160, 
};

static const unsigned int BOOSTV_STABLE[] = {
	4850000, 5000000, 5150000, 5300000, 5800000, 6400000, 6900000, 7500000,	
};

static const unsigned int IBATOCP_STABLE[] = {
	6000000, 8000000, 10000000, 12000000	
};

static const unsigned int RECHRG_STABLE[] = {
	100, 200, 300, 600	
};


enum SGM41542S_VINDPM_OS {
	VINDPM_OS_3900mV,
	VINDPM_OS_5900mV,
	VINDPM_OS_7500mV,
	VINDPM_OS_10500mV,
};


enum sgm41542_usbsw {
	USBSW_CHG,
	USBSW_USB,
};

bool sgm41542S_get_pdo_active(struct sgm41542S_device *sgm);

static int sgm41542S_adc_enable(struct charger_device *chg_dev, bool en);
static int sgm41542S_dump_register(struct charger_device *chg_dev);
static int sgm41542xx_write_reg(struct sgm41542S_device *sgm , u8 reg, u8 val)
{
    s32 ret;

    ret = i2c_smbus_write_byte_data(sgm->client, reg, val);
    if (ret < 0) {
        pr_err("%s i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n",
            __func__, val, reg, ret);
        return ret;
    }

	// pr_err("write reg:%02x, val=%x ret:%d\n", reg, val, ret);
	return 0;
}

static int sgm41542xx_read_reg(struct sgm41542S_device *sgm, u8 reg, int *data)
{
    s32 ret;

    ret = i2c_smbus_read_byte_data(sgm->client, reg);
    if (ret < 0) {
        pr_err("%s i2c read fail: can't read from reg 0x%02X\n", __func__, reg);
        return ret;
    }

    *data = (u8) ret;

	// pr_err("read reg:%02x, val=%x\n", reg, *data);

    return 0;
}

static int sgm41542xx_update_bits(struct sgm41542S_device *sgm, u8 reg, u8 mask, u8 data)
{
    int ret;
    int tmp;

    mutex_lock(&i2c_rw_lock);
    ret = sgm41542xx_read_reg(sgm, reg, &tmp);
    if (ret) {
        pr_err("%s Failed: read1 reg=%02X, ret=%d\n", __func__, reg, ret);
        goto out;
    }

    tmp &= ~mask;
    tmp |= data & mask;

    ret = sgm41542xx_write_reg(sgm, reg, tmp);
    if (ret) {
        pr_err("%s Failed: write reg=%02X, ret=%d\n", __func__, reg, ret);
    }

out:
    mutex_unlock(&i2c_rw_lock);
    return ret;
}


static int sgm41542_set_usbsw(struct sgm41542S_device *sgm,
                enum sgm41542_usbsw usbsw)
{
    struct phy *phy = NULL;
    int ret = 0;
    int mode = (usbsw == USBSW_CHG) ? PHY_MODE_BC11_SET :
                           PHY_MODE_BC11_CLR;

    pr_err("usbsw=%d\n", usbsw);
    phy = phy_get(sgm->dev, "usb2-phy");
    if (IS_ERR_OR_NULL(phy)) {
        pr_err("failed to get usb2-phy\n");
        return -ENODEV;
    }
    ret = phy_set_mode_ext(phy, PHY_MODE_USB_DEVICE, mode);
    if (ret){
        pr_err("failed to set phy ext mode\n");
		return -1;
	}
    phy_put(sgm->dev, phy);

    return ret;
}

static int sgm41542S_set_dp(struct sgm41542S_device *sgm, int dp_stat)
{
	pr_info("set dp:%d\n",dp_stat);

	return sgm41542xx_update_bits(sgm, SGM41542S_TFCP_CTRL_1, SGM41542S_DP_VSEL_MASK, dp_stat);
}

static int sgm41542S_set_dm(struct sgm41542S_device *sgm, int dm_stat)
{
	pr_info("set dm:%d\n",dm_stat);

	return sgm41542xx_update_bits(sgm, SGM41542S_TFCP_CTRL_1, SGM41542S_DM_VSEL_MASK, dm_stat);
}

static int sgm41542S_set_term_curr(struct charger_device *chg_dev, u32 uA)
{
	int reg_val;
	int ret;
	
	struct sgm41542S_device *sgm = charger_get_data(chg_dev);
	if (uA < SGM41542S_TERMCHRG_I_MIN_uA)
		uA = SGM41542S_TERMCHRG_I_MIN_uA;
	else if (uA > SGM41542S_TERMCHRG_I_MAX_uA)
		uA = SGM41542S_TERMCHRG_I_MAX_uA;

	reg_val = (uA - SGM41542S_TERMCHRG_I_MIN_uA) / SGM41542S_TERMCHRG_CURR_STEP_uA;
		

	ret = sgm41542xx_update_bits(sgm,
				 SGM41542S_CHRG_PRE_TERM,
				 SGM41542S_TERMCHRG_CUR_MASK,
				 reg_val);
	if (ret){
		dev_err(sgm->dev, "set term current error!\n");
		return ret;
	}

	pr_err("%s ret:%d\n", __func__, ret);
	sgm->init_data.iterm = uA;

	return ret;
}


static int sgm41542S_get_term_curr(struct charger_device *chg_dev, int* uA)
{
	int reg_val;
	int ret;
	
	struct sgm41542S_device *sgm = charger_get_data(chg_dev);

	ret = sgm41542xx_read_reg(sgm,
				 SGM41542S_CHRG_PRE_TERM, &reg_val);
	if (ret){
		dev_err(sgm->dev, "set term current error!\n");
		return ret;
	}

	reg_val &= SGM41542S_TERMCHRG_CUR_MASK;
	reg_val *= SGM41542S_TERMCHRG_CURR_STEP_uA;
	reg_val	+= SGM41542S_TERMCHRG_I_MIN_uA / 1000;
	*uA = reg_val;
	pr_err("%s term cur:%d\n", __func__, reg_val);

	return ret;
}

static int sgm41542S_set_prechrg_curr(struct charger_device *chg_dev, u32 uA)
{
	int reg_val;
	int ret;
	
	struct sgm41542S_device *sgm = charger_get_data(chg_dev);
	if (uA < SGM41542S_PRECHRG_I_MIN_uA)
		uA = SGM41542S_PRECHRG_I_MIN_uA;
	else if (uA > SGM41542S_PRECHRG_I_MAX_uA)
		uA = SGM41542S_PRECHRG_I_MAX_uA;

	reg_val = (uA - SGM41542S_PRECHRG_I_MIN_uA) / SGM41542S_PRECHRG_CURR_STEP_uA;

	reg_val = reg_val << 4;	
	
	ret = sgm41542xx_update_bits(sgm,
				 SGM41542S_CHRG_PRE_TERM,
				 SGM41542S_PRECHRG_CUR_MASK,
				 reg_val);
	if (ret)
		dev_err(sgm->dev, "set precharge current error!\n");
	
	pr_err("%s set ipre success ret:%d \n", __func__, ret);

	return ret;
}

static int sgm41542S_get_ichrg_curr(struct charger_device *chg_dev, u32 *uA)
{
	struct sgm41542S_device *sgm = charger_get_data(chg_dev);	
	*uA = sgm->init_data.ichg;
	return 0;
}

static int sgm41542S_get_minichg_curr(struct charger_device *chg_dev, u32 *uA)
{
	//struct sgm41542S_device *sgm = charger_get_data(chg_dev);
	*uA = SGM41542S_ICHRG_I_MIN_uA;
	return 0;	
}

static int sgm41542S_set_ichrg_curr(struct charger_device *chg_dev, u32 uA)
{
	int reg_val;
	int ret;

	struct sgm41542S_device *sgm = charger_get_data(chg_dev);
	if (uA < SGM41542S_ICHRG_I_MIN_uA)
		uA = SGM41542S_ICHRG_I_MIN_uA;
	else if ( uA > SGM41542S_ICHRG_I_MAX_uA)
		uA = SGM41542S_ICHRG_I_MAX_uA;
	
	reg_val = uA / SGM41542S_ICHRG_I_STEP_uA;

	ret = sgm41542xx_update_bits(sgm,
				 SGM41542S_CHRG_CURR_LIMIT,
				 SGM41542S_ICHRG_CUR_MASK,
				 reg_val);
	if (ret){
		pr_err("set icharge current error! ret=%d\n", ret);
		return ret;	
	}
	sgm->init_data.ichg = uA;

	pr_err("%s set ipre success ichg:%d ret :%d\n", __func__, uA, ret);
	return 0;
}

static int sgm41542S_get_chrg_volt(struct charger_device *chg_dev, u32 *chrg_volt)
{
	struct sgm41542S_device *sgm = charger_get_data(chg_dev);
	*chrg_volt = sgm->init_data.vreg;
	return 0;
}

static int sgm41542S_set_chrg_volt(struct charger_device *chg_dev, u32 chrg_volt)
{
	int reg_val;
	int ret;

	struct sgm41542S_device *sgm = charger_get_data(chg_dev);
	if (chrg_volt < SGM41542S_VREG_V_MIN_uV)
		chrg_volt = SGM41542S_VREG_V_MIN_uV;
	else if ( chrg_volt > SGM41542S_VREG_V_MAX_uV)
		chrg_volt = SGM41542S_VREG_V_MAX_uV;

	reg_val = (chrg_volt - SGM41542S_VREG_V_MIN_uV) / SGM41542S_VREG_V_STEP_uV;
	
	reg_val = reg_val << SGM41542S_VREG_V_SHIFT;
	ret = sgm41542xx_update_bits(sgm,
				 SGM41542S_BAT_VOLT_LIMIT,
				 SGM41542S_VREG_V_MASK,
				 reg_val);
	if (ret)
	{
		dev_err(sgm->dev, "set charge voltage error!\n");
		return ret;
	}

	pr_err("%s set cv %d\n", __func__, reg_val);
	sgm->init_data.vreg = chrg_volt;
	return 0;
}

static int sgm41542S_get_vindpm_offset_os(struct sgm41542S_device *sgm)
{
	int ret;
	u32 reg_val;

	ret = sgm41542xx_read_reg(sgm, SGM41542S_CHRG_CTRL_7, &reg_val);
	if (ret)
		return ret;	

	reg_val = reg_val & SGM41542S_VINDPM_OS_MASK;	

	return reg_val;
}

static int sgm41542S_set_vindpm_offset_os(struct sgm41542S_device *sgm,
					 enum SGM41542S_VINDPM_OS offset_os)
{
	int ret;

	ret = sgm41542xx_update_bits(sgm,
				 SGM41542S_CHRG_CTRL_7,
				 SGM41542S_VINDPM_OS_MASK,
				 offset_os);

	if (ret)
		dev_err(sgm->dev, "set vindpm offset os error!\n");
	
	pr_err("%s set vindpm :ret%d\n", __func__, ret);

	return ret;
}

static int sgm41542S_set_input_volt_lim(struct charger_device *chg_dev,
				       u32 vindpm)
{
	enum SGM41542S_VINDPM_OS os_val;
	unsigned int offset;
	u8 reg_val;
	int ret;

	struct sgm41542S_device *sgm = charger_get_data(chg_dev);
	if (vindpm < SGM41542S_VINDPM_V_MIN_uV ||
	    vindpm > SGM41542S_VINDPM_V_MAX_uV)
		return -EINVAL;

	if (vindpm < 5900000) {
		os_val = VINDPM_OS_3900mV;
		offset = 3900000;
	} else if (vindpm >= 5900000 && vindpm < 7500000) {
		os_val = VINDPM_OS_5900mV;
		offset = 5900000;
	} else if (vindpm >= 7500000 && vindpm < 10500000) {
		os_val = VINDPM_OS_7500mV;
		offset = 7500000;
	} else {
		os_val = VINDPM_OS_10500mV;
		offset = 10500000;
	}

	ret = sgm41542S_set_vindpm_offset_os(sgm, os_val);
	if (ret) {
		pr_err("set vin dpm error!\n");
		return ret;
	}

	reg_val = (vindpm - offset) / SGM41542S_VINDPM_STEP_uV;

	ret = sgm41542xx_update_bits(sgm, SGM41542S_CHRG_CTRL_3,
				 SGM41542S_VINDPM_V_MASK, reg_val);
	if (ret) {
		pr_err("input voltage error!\n");
		return ret;
	}
	sgm->init_data.vlim = vindpm;

	pr_err("%s set :ret%d\n", __func__, ret);
	return 0;
}

static int sgm41542S_get_input_volt_lim(struct charger_device *chg_dev, u32 *uV)
{
	int ret;
	int offset;
	u32 vlim;
	int temp;
	
	struct sgm41542S_device *sgm = charger_get_data(chg_dev);
	ret = sgm41542xx_read_reg(sgm, SGM41542S_CHRG_CTRL_3, &vlim);
	if (ret)
		return ret;
	
	temp = sgm41542S_get_vindpm_offset_os(sgm);
	if (0 == temp)
		offset = 3900000; //uv
	else if (1 == temp)
		offset = 5900000;
	else if (2 == temp)	
		offset = 7500000;
	else if (3 == temp)
		offset = 10500000;
	else
		return temp;
	
	*uV = offset + (vlim & 0x0F) * SGM41542S_VINDPM_STEP_uV;
	sgm->init_data.vlim = *uV;
	return 0;
}


static int sgm41542S_set_input_curr_lim(struct charger_device *chg_dev, u32 iindpm)
{
	int reg_val;
	int ret;

	struct sgm41542S_device *sgm = charger_get_data(chg_dev);
	if (iindpm < SGM41542S_IINDPM_I_MIN_uA)
		iindpm =  SGM41542S_IINDPM_I_MIN_uA;
	else if (iindpm > SGM41542S_IINDPM_I_MAX_uA)
		iindpm =  SGM41542S_IINDPM_I_MAX_uA;
	
	reg_val = (iindpm-SGM41542S_IINDPM_I_MIN_uA) / SGM41542S_IINDPM_STEP_uA;	

	ret = sgm41542xx_update_bits(sgm,
				 SGM41542S_INPUT_CURR_LIMIT,
				 SGM41542S_IINDPM_I_MASK,
				 reg_val);
	if (ret){
		pr_err("set input current limit error!\n");
		return ret;	
	}

	pr_err("%s set :ret%d\n", __func__, ret);
	return 0;
}

static int sgm41542S_get_input_mincurr_lim(struct charger_device *chg_dev, u32 *iindpm)
{
	//struct sgm41542S_device *sgm = charger_get_data(chg_dev);
	*iindpm = SGM41542S_IINDPM_I_MIN_uA;
	return 0;
}

static int sgm41542S_get_input_curr_lim(struct charger_device *chg_dev, u32 *iindpm)
{
	int ret;
	int val;

	struct sgm41542S_device *sgm = charger_get_data(chg_dev);
	ret = sgm41542xx_read_reg(sgm, SGM41542S_INPUT_CURR_LIMIT, &val);
	if (ret) {
		dev_err(sgm->dev, "get input current limit error!\n");
		return ret;
	}		
	*iindpm = (val & SGM41542S_IINDPM_I_MASK) * SGM41542S_IINDPM_STEP_uA + SGM41542S_IINDPM_I_MIN_uA;

	return ret;
}

#define SGM41542S_VINDPM_MAX  12000000
__maybe_unused static int sgm41542S_enable_power_path(struct charger_device *chg_dev, bool en)
{
	int ret =0;
	u32 mivr ;	
	u32 temp;
	static bool old_state = 0;	
	
	struct sgm41542S_device *sgm = charger_get_data(chg_dev);
	if(IS_ERR_OR_NULL(sgm))
		return -ENOMEM;
	if (old_state == en) 
	{
		dev_info(sgm->dev,"%s,already is set",__func__);	
		return 0;
	}
		
	if (en)
	{
		mivr = 	sgm->init_data.vlim;
	}
	else{
		ret = sgm41542S_get_input_volt_lim(chg_dev,&temp);
		if (!ret)
			sgm->init_data.vlim = temp;
		mivr = 	SGM41542S_VINDPM_MAX;
	}	
	
	ret = sgm41542S_set_input_volt_lim(chg_dev,mivr);
	if (ret) {
		dev_err(sgm->dev, "%s: power path set failed\n", __func__);
		return ret;
	}
	old_state = en;
	return 0;
}

static int sgm41542S_watchdog_timer_reset(struct charger_device *chg_dev)
{
	int ret;
	struct sgm41542S_device *sgm = charger_get_data(chg_dev);
	ret = sgm41542xx_update_bits(sgm,
				 SGM41542S_CHRG_CTRL_1,
				 SGM41542S_WD_RST,
				 SGM41542S_WD_RST);

	if (ret)
		dev_err(sgm->dev, "set watchdog timer reset error!\n");

	return ret;
}

static int sgm41542S_set_watchdog_timer(struct sgm41542S_device *sgm, int time)
{
	u8 reg_val;
	int ret;


	for(reg_val = 1; reg_val < 4 && time >= WATCHDOG_STABLE[reg_val]; reg_val++) {
		pr_debug("%d\n", reg_val);
	}
		reg_val--;

	pr_err("sgm41542S_set_watchdog_timer 1\n");

	ret = sgm41542xx_update_bits(sgm,
				 SGM41542S_CHRG_CTRL_2,
				 SGM41542S_WDT_TIMER_MASK,
				 reg_val);

	pr_err("sgm41542S_set_watchdog_timer 2\n");

	if (ret) {
		dev_err(sgm->dev, "set watchdog timsgm41542S_set_watchdog_timerer error i2c:%02x!\n", sgm->client->addr);
	}

	ret =  sgm41542xx_update_bits(sgm, SGM41542S_CHRG_CTRL_2, SGM41542S_WDT_TIMER_MASK, reg_val);
	if (ret) {
		dev_err(sgm->dev, "set watchdog timsgm41542S_set_watchdog_timerer error! i2c addr :%02X\n", sgm->client->addr);
	}
	
	pr_err("sgm41542S_set_watchdog_timer 3\n");

	if (time) {
		DBG("sgm41542: enable watchdog\n");
		if (!sgm->watchdog_enable)
			queue_delayed_work(sgm->sgm_monitor_wq,
					   &sgm->sgm_delay_work,
					   msecs_to_jiffies(1000 * 5));
		sgm->watchdog_enable = true;
	} else {
		DBG("sgm41542: disable watchdog\n");
		sgm->watchdog_enable = false;
		sgm41542S_watchdog_timer_reset(s_chg_dev);
	}

	pr_err("sgm41542S_set_watchdog_timer 4\n");

	return ret;
}

static int sgm41542S_enable_charger(struct sgm41542S_device *sgm)
{
	int ret;

	ret = sgm41542xx_update_bits(sgm,
				 SGM41542S_CHRG_CTRL_1,
				 SGM41542S_CHRG_EN,
				 SGM41542S_CHRG_EN);
	pr_err("%s ret=%d\n", __func__, ret);
	if (ret)
		dev_err(sgm->dev, "enable charger error!\n");

	return ret;
}

static int sgm41542S_disable_charger(struct sgm41542S_device *sgm)
{
	int ret;

	ret = sgm41542xx_update_bits(sgm,
				 SGM41542S_CHRG_CTRL_1,
				 SGM41542S_CHRG_EN,
				 0);
	if (ret)
		dev_err(sgm->dev, "disable charger error!\n");

	return ret;
}


static int sgm41542S_adc_enable(struct charger_device *chg_dev, bool en)
{
	int ret;
	struct sgm41542S_device *sgm = charger_get_data(chg_dev);
	if(IS_ERR_OR_NULL(sgm))
		return -ENOMEM;
	if (en) {
		ret = sgm41542xx_update_bits(sgm,
				 SGM41542S_CHRG_CTRL_9,
				 SGM41542S_CONV_START,
				 SGM41542S_CONV_START);

		ret += sgm41542xx_update_bits(sgm,
				SGM41542S_CHRG_CTRL_9,
				SGM41542S_CONV_RATE,
				SGM41542S_CONV_RATE);
	if (ret)
		dev_err(sgm->dev, "disable charger error!\n");
	} else {
			ret = sgm41542xx_update_bits(sgm,
				SGM41542S_CHRG_CTRL_9,
				SGM41542S_CONV_START,
				0);
			ret += sgm41542xx_update_bits(sgm,
				SGM41542S_CHRG_CTRL_9,
				SGM41542S_CONV_RATE,
				0);

	if (ret)
		dev_err(sgm->dev, "disable charger error!\n");

	}
		
	sgm->chg_en = en;
	return 0;
}

static int sgm41542S_charging_switch(struct charger_device *chg_dev, bool en)
{
	struct sgm41542S_device *sgm = charger_get_data(chg_dev);
	if(IS_ERR_OR_NULL(sgm))
		return -ENOMEM;
	if (en)
		sgm41542S_enable_charger(sgm);
	else
		sgm41542S_disable_charger(sgm);
	sgm->chg_en = en;
	return 0;
}

static int sgm41542S_is_charging(struct charger_device *chg_dev, bool *en)
{
	struct sgm41542S_device *sgm = charger_get_data(chg_dev);
	*en = sgm->chg_en;
	return 0;
}

static int sgm41542S_set_vac_ovp(struct sgm41542S_device *sgm)
{
	int reg_val;
	int ret;

	reg_val = 0xFF & SGM41542S_VAC_OVP_MASK;

	ret = sgm41542xx_update_bits(sgm,
				 SGM41542S_CHRG_CTRL_8,
				 SGM41542S_VAC_OVP_MASK,
				 reg_val);

	pr_err("%s ret=%d\n", __func__, ret);
	if (ret)
		dev_err(sgm->dev, "set vac ovp error!\n");

	return ret;
}

static __maybe_unused int sgm41542S_set_recharge_volt(struct sgm41542S_device *sgm, u32 recharge_volt)
{
	int reg_val;
	int ret;
	pr_err("%s \n", __func__);
	for(reg_val = 1; reg_val < 4 && recharge_volt >= RECHRG_STABLE[reg_val]; reg_val++) {
		pr_debug("%d\n", reg_val);
	}
		reg_val--;

	pr_err("%s val %d\n", __func__, reg_val);

	ret = sgm41542xx_update_bits(sgm,
				 SGM41542S_CHRG_CTRL_9,
				 SGM41542S_VRECHARGE_MASK,
				 reg_val);

	pr_err("%s ret=%d\n", __func__, ret);

	if (ret)
		dev_err(sgm->dev, "set recharger error!\n");

	return ret;
}

static int sgm41542S_get_adc_data(struct sgm41542S_device *sgm, 
            int channel, int *result)
{
    uint32_t val = 0;
    int ret;
        
    if(channel >= ADC_MAX_NUM) 
        return -EINVAL;	   
	
    ret = sgm41542xx_read_reg(sgm, SGM41542S_CHRG_VBAT_ADC + channel, &val);
    if (ret < 0) {
        return ret;
    }	
   
	switch(channel)
	{
		case ADC_VBAT:
		case ADC_VSYS:		
			*result = val * 10980 + 2304000;
			break;	
			
		case ADC_TS:			
			*result = val * 543 + 21000;
			break;
		case ADC_VBUS:			
			*result = val * 50000 + 2600000;
			break;
		case ADC_IBAT:
		case ADC_IBUS:		
			*result = val * 25100;
			break;
				
		default:
			break;
	}    

    dev_info(sgm->dev,"%s %s = %d", __func__, sgm41542s_adc_name[channel], *result);    

    return ret;
}

static int __maybe_unused sgm41542S_get_charging_status(struct sgm41542S_device *sgm )
{
	int rval = 0;

	sgm41542xx_read_reg(sgm, SGM41542S_CHRG_STAT_1, &rval);
	rval = rval & SGM41542S_CHG_STAT_MASK >> SGM41542S_CHG_STAT_SHIFT;

	sgm->state.chrg_stat = rval;

	pr_err("%s %s", __func__, sgm41542s_chrg_stat[rval]);

	return rval;
}

static int sgm41542S_get_ischarging_done(struct charger_device *chg_dev, bool *done)
{
	struct sgm41542S_device *sgm = charger_get_data(chg_dev);
	if(IS_ERR_OR_NULL(sgm))
		return -ENOMEM;	
	*done = sgm->charge_done;
	return 0;	
}

static int sgm41542S_enable_safetytimer(struct charger_device *chg_dev, bool en)
{
	int ret;
	int val;
	struct sgm41542S_device *sgm = charger_get_data(chg_dev);
	if(IS_ERR_OR_NULL(sgm))
		return -ENOMEM;	
	if (en)
		val = SGM41542S_EN_TIMER;
	else
		val = 0;
	ret = sgm41542xx_update_bits(sgm,
				 SGM41542S_CHRG_CTRL_2,
				 SGM41542S_EN_TIMER,
				 val);
	if (ret)
	{
		dev_err(sgm->dev, "enable safetytimer error!\n");	
		return ret;	
	}
		
	sgm->safetytimer_en = en;
	return 0;
}

static int sgm41542S_get_is_safetytimer_enable(struct charger_device *chg_dev, bool *en)
{
	struct sgm41542S_device *sgm = charger_get_data(chg_dev);
	if(IS_ERR_OR_NULL(sgm))
		return -ENOMEM;	
	*en = sgm->safetytimer_en;
	return 0;
}

static int sgm41542S_en_pe_current_partern(struct charger_device *chg_dev, bool is_increase)
{
	int val;
	int ret;

	struct sgm41542S_device *sgm = charger_get_data(chg_dev);
	if(IS_ERR_OR_NULL(sgm))
		return -ENOMEM;	

	if (is_increase)
		val = SGM41542S_EN_PUMPX | SGM41542S_PUMPX_UP;
	else
		val = SGM41542S_EN_PUMPX | SGM41542S_PUMPX_DN;
	
	ret = sgm41542xx_write_reg(sgm,
				 SGM41542S_CHRG_CTRL_6,				 
				 val);
	if (ret) {
		pr_err("increase or reduce voltage fail\n");
		return ret;
	}
	return 0;
}

static int sgm41542S_get_state(struct sgm41542S_device *sgm,
			      struct sgm41542S_state *state)
{
	int adc[ADC_MAX_NUM];
	int chrg_stat_1,chrg_stat_2;
	int fault;
	int ret;
	int k;
	ret = sgm41542xx_read_reg(sgm, SGM41542S_CHRG_STAT_1, &chrg_stat_1);
	if (ret) {
		pr_err("read SGM41542S_CHRG_STAT fail\n");
		return ret;
	}

	pr_err("SGM41542S_CHRG_STAT_1: 0x%x\n", chrg_stat_1);
	state->status_1 = chrg_stat_1;
	state->chrg_type = (chrg_stat_1 & SGM41542S_VBUS_STAT_MASK);
	state->chrg_stat = (chrg_stat_1 & SGM41542S_CHG_STAT_MASK);
	state->online = !!(chrg_stat_1 & SGM41542S_PG_STAT);
	state->therm_stat = !!(chrg_stat_1 & SGM41542S_THERM_STAT);
	state->vsys_stat = !!(chrg_stat_1 & SGM41542S_VSYS_STAT);
	if (state->chrg_stat == SGM41542S_TERM_CHRG)
		sgm->charge_done = true;
	else
		sgm->charge_done = false;

	pr_err("1 read SGM41542S_CHRG_STAT_1 status_1:%x  chrg_type:%x chrg_stat=%x online=%d therm_stat=%d  vsys_stat=%d\n",
					state->status_1, state->chrg_type, state->chrg_stat, state->online,state->therm_stat, state->vsys_stat );

	ret = sgm41542xx_read_reg(sgm, SGM41542S_CHRG_FAULT, &fault);
	if (ret) {
		pr_err("read SGM41542S_CHRG_FAULT fail\n");
		return ret;
	}
	DBG("SGM41542S_CHRG_FAULT[0x%x]: 0x%x\n", SGM41542S_CHRG_FAULT, fault);
	
	state->fault_status = fault;
	state->wdt_fault = !!(fault & SGM41542S_WDT_FAULT_MASK);
	state->boost_fault = !!(fault & SGM41542S_BOOST_FAULT_MASK);
	state->chrg_fault = (fault & SGM41542S_CHRG_FAULT_MASK) >> SGM41542S_CHRG_FAULT_SHIFT;
	state->ntc_fault = (fault & SGM41542S_TEMP_MASK) >> SGM41542S_TEMP_SHIFT;
	state->health = state->ntc_fault;
	
	pr_err("2 read SGM41542S_CHRG_FAULT fault_status:%x  wdt_fault:%x boost_fault=%x chrg_fault=%x ntc_fault=%x  health=%x\n",
					state->fault_status, state->wdt_fault, state->boost_fault, state->chrg_fault,state->ntc_fault, state->health);

	ret = sgm41542xx_read_reg(sgm, SGM41542S_CHRG_STAT_2, &chrg_stat_2);
	if (ret) {
		pr_err("read SGM41542S_CHRG_CTRL_0 fail\n");
		return ret;
	}
	
	DBG("SGM41542S_CHRG_STAT_2: 0x%x\n", chrg_stat_2);
	state->status_2 = chrg_stat_2;
	state->vbus_gd = !!(chrg_stat_2 & SGM41542S_VBUS_GD);
    state->vindpm_stat = !!(chrg_stat_2 & SGM41542S_VINDPM_STAT);
	state->iindpm_stat = !!(chrg_stat_2 & SGM41542S_IINDPM_STAT);
	state->cv_stat = !!(chrg_stat_2 & SGM41542S_CV_STAT);
	state->topoff_active = !!(chrg_stat_2 & SGM41542S_TOPOFF_ACTIVE);
	state->acov_stat = !!(chrg_stat_2 & SGM41542S_ACOV_STAT);
	state->vindpm_int_mask = !!(chrg_stat_2 & SGM41542S_VINDPM_INT_MASK);
	state->iindpm_int_mask = !!(chrg_stat_2 & SGM41542S_IINDPM_INT_MASK);
	

	pr_err("3 read SGM41542S_CHRG_FAULT status_2:%x  vbus_gd:%x vindpm_stat=%x iindpm_stat=%x cv_stat=%x  topoff_active=%x acov_stat=%x vindpm_int_mask=%x iindpm_int_mask=%x\n",
				state->status_2, state->vbus_gd, state->vindpm_stat, state->iindpm_stat,state->cv_stat, state->topoff_active,state->acov_stat,state->vindpm_int_mask,state->iindpm_int_mask);

	for (k = 0; k < ADC_MAX_NUM; k++)		
		sgm41542S_get_adc_data(sgm,k,&adc[k]);

	//DBG("chrg_type: %s ,chrg_stat: %s\n", sgm41542s_chrg_types[state->chrg_type >> SGM41542S_VBUS_STAT_SHIFT],sgm41542s_chrg_stat[state->chrg_stat >> SGM41542S_CHG_STAT_SHIFT]);	
	//DBG("online: 0x%x ,therm_stat: 0x%x,vsys_stat: 0x%x\n", state->online,state->therm_stat,state->vsys_stat);
	
//	DBG("wdt_fault: 0x%x,boost_fault: 0x%x\n", state->wdt_fault,state->boost_fault);	
//	DBG("chrg_fault: %s,ntc_fault: %s\n", sgm41542s_chrg_fault[state->chrg_fault],sgm41542s_ntc_fault[state->ntc_fault]);	
	
//	DBG("vbus_gd: 0x%x,vindpm_stat: 0x%x,iindpm_stat: 0x%x,cv_stat: 0x%x,topoff_active: 0x%x,acov_stat: 0x%x,\n", 
//		state->vbus_gd,state->vindpm_stat,state->iindpm_stat, state->cv_stat, state->topoff_active,state->acov_stat);	
//    DBG("vindpm_int_mask: 0x%x, iindpm_int_mask: 0x%x\n", state->vindpm_int_mask,state->iindpm_int_mask);
	
	return ret;
}

static int sgm41542S_property_is_writeable(struct power_supply *psy,
					  enum power_supply_property prop)
{
	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
	case POWER_SUPPLY_PROP_ONLINE:
		return true;
	default:
		return false;
	}
}

static int sgm41542S_charger_set_property(struct power_supply *psy,
					 enum power_supply_property prop,
					 const union power_supply_propval *val)
{
	struct sgm41542S_device *sgm = power_supply_get_drvdata(psy);
	int ret = -EINVAL;	

	switch (prop) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (val->intval) {
			ret = sgm41542S_enable_charger(sgm);
			sgm41542S_set_watchdog_timer(sgm, SGM41542S_WDT_TIMER_40S);
		} else {
			sgm41542S_set_watchdog_timer(sgm, 0);
			ret = sgm41542S_disable_charger(sgm);
		}
		break;

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = sgm41542S_set_input_curr_lim(s_chg_dev, (u32)val->intval);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = sgm41542S_set_ichrg_curr(s_chg_dev, (u32)val->intval);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		ret = sgm41542S_set_chrg_volt(s_chg_dev, (u32)val->intval);
		break;

	default:
		return -EINVAL;
	}

	return ret;
}

static int sgm41542S_charger_get_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 union power_supply_propval *val)
{
	struct sgm41542S_device *sgm = power_supply_get_drvdata(psy);
	int ret = 0;
	u32 value = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (!sgm->state.chrg_type || (sgm->state.chrg_type == SGM41542S_OTG_MODE))
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		else if (!sgm->state.chrg_stat)
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		else if (sgm->state.chrg_stat == SGM41542S_TERM_CHRG)
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		switch (sgm->state.chrg_stat) {
		case SGM41542S_PRECHRG:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
			break;
		case SGM41542S_FAST_CHRG:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_FAST;
			break;
		case SGM41542S_TERM_CHRG:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
			break;
		case SGM41542S_NOT_CHRGING:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_NONE;
			break;
		default:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
		}
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = SGM41542S_MANUFACTURER;
		break;

	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = SGM41542S_NAME;
		break;

	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = sgm->state.online;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = sgm->state.vbus_gd;
		break;
	case POWER_SUPPLY_PROP_TYPE:
		val->intval = sgm->chg_type;
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
	val->intval = sgm->usb_type;
	break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		val->intval = sgm->init_data.max_vreg;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval = SGM41542S_ICHRG_I_MAX_uA;
		break;
		
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
		ret = sgm41542S_get_input_volt_lim(s_chg_dev,&value);
		if (!ret)
			val->intval = value;
		break;

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = sgm41542S_get_input_curr_lim(s_chg_dev,&value);
		if (!ret)
			val->intval = value;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		ret = sgm41542S_get_term_curr(s_chg_dev,&value);
		if (!ret)
			val->intval = value;
		break;

	default:
		return -EINVAL;
	}

	return ret;
}

static ssize_t registers_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct sgm41542S_device *sgm41542S = dev_get_drvdata(dev);
	u8 tmpbuf[30];
	int idx = 0;
	u8 addr;
	int val;
	int len;
	int ret;

	for (addr = 0x0; addr <= SGM41542S_TFCP_CTRL_2; addr++) {
		ret = sgm41542xx_read_reg(sgm41542S, addr, &val);
		if (ret == 0) {
			len = snprintf(tmpbuf, 30,
				       "Reg[%.2X] = 0x%.2x\n",
				       addr,
				       val);
			memcpy(&buf[idx], tmpbuf, len);
			idx += len;
		}
	}

	return idx;
}

static ssize_t registers_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct sgm41542S_device *sgm41542S = dev_get_drvdata(dev);
	unsigned int reg;
	int ret;
	int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg <= SGM41542S_TFCP_CTRL_2)
		sgm41542xx_write_reg(sgm41542S, (unsigned char)reg, val);

	return count;
}
static DEVICE_ATTR_RW(registers);

static void sgm41542S_create_device_node(struct device *dev)
{
	device_create_file(dev, &dev_attr_registers);
}

static bool sgm41542S_dpdm_detect_is_done(struct sgm41542S_device *sgm)
{
	int chrg_flag = 0, ret;

	ret = sgm41542xx_read_reg(sgm, SGM41542S_CHRG_FLAG, &chrg_flag);
	if(ret) {
		dev_err(sgm->dev, "Check DPDM detecte error\n");
	}

	return (chrg_flag & SGM41542S_INPUT_DET_DONE) ? true:false;
}

/*
static bool sgm41542S_state_changed(struct sgm41542S_device *sgm,
				  struct sgm41542S_state *new_state)
{
	struct sgm41542S_state old_state;

	mutex_lock(&sgm->lock);
	old_state = sgm->state;
	mutex_unlock(&sgm->lock);

	return (old_state.status_1 != new_state->status_1 ||
		old_state.fault_status != new_state->fault_status	||
		old_state.status_2 != new_state->status_2);
}
*/
static void __maybe_unused sgm41542_afc_detect_dwork_cancel(struct sgm41542S_device *sgm)
{
    afc_cancel_voltage_workq();
    cancel_delayed_work(&sgm->afc_detect_dwork);
}

static void sgm41542_get_charger_type(struct sgm41542S_device *sgm)
{
	bool pd_active = false;

	pd_active = sgm41542S_get_pdo_active(sgm);

	pr_err("%s chg_type:%d\n", __func__, sgm->state.chrg_type);
	switch(sgm->state.chrg_type) {
		case SGM41542S_USB_SDP:
			pr_err("SGM4154x charger type: SDP\n");
			sgm->curr_in_limit = 500000;
			sgm->icl_limit = 500000;
			sgm->usb_type = POWER_SUPPLY_USB_TYPE_SDP;
			sgm->chg_type = POWER_SUPPLY_TYPE_USB;
			sgm41542_set_usbsw(sgm, USBSW_USB);
			break;

		case SGM41542S_USB_CDP:
			pr_err("SGM4154x charger type: CDP\n");
			sgm->curr_in_limit = 1000000;
			sgm->icl_limit = 1500000;
			sgm->usb_type = POWER_SUPPLY_USB_TYPE_CDP;
			sgm->chg_type = POWER_SUPPLY_TYPE_USB;
			sgm41542_set_usbsw(sgm, USBSW_USB);
			break;

		case SGM41542S_USB_DCP:
			pr_err("SGM4154x charger type: DCP\n");
			sgm->curr_in_limit = 2000000;
			sgm->icl_limit = 2000000;
            sgm->usb_type = POWER_SUPPLY_USB_TYPE_DCP;
            sgm->chg_type = POWER_SUPPLY_TYPE_USB_DCP;
			sgm41542S_set_ichrg_curr(s_chg_dev, 1500000);
			break;
			
		case SGM41542S_UNKNOWN:
			pr_err("SGM4154x charger type: UNKNOWN\n");
			sgm->usb_type = POWER_SUPPLY_USB_TYPE_SDP;
            sgm->chg_type = POWER_SUPPLY_TYPE_USB;
			sgm->curr_in_limit = 500000;
			sgm->icl_limit = 500000;
			break;
			
		case SGM41542S_NON_STANDARD:
			pr_err("SGM4154x charger type: NON_STANDARD\n");
			sgm->chg_type = POWER_SUPPLY_TYPE_USB_DCP;
			sgm->usb_type = POWER_SUPPLY_USB_TYPE_DCP;
			sgm->curr_in_limit = 900000;
			sgm->icl_limit = 1000000;
			break;	
			
		case SGM41542S_OTG_MODE:
			pr_err("SGM4154x charger type: OTG\n");
			sgm->usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
            sgm->chg_type = POWER_SUPPLY_TYPE_USB;
			sgm->curr_in_limit = 500000;
			sgm->icl_limit = 500000;
			sgm41542_set_usbsw(sgm, USBSW_USB);		
			break;
			
		default:
			pr_err("SGM4154x charger type: default\n");
			sgm->usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
            sgm->chg_type = POWER_SUPPLY_TYPE_USB;
			sgm->curr_in_limit = 500000;
			sgm->icl_limit = 500000;
			break;			
	}

	if (pd_active) {
			pr_err("SGM4154x charger type: PD apater\n");
			sgm->usb_type = POWER_SUPPLY_USB_TYPE_PD;
            sgm->chg_type = POWER_SUPPLY_TYPE_USB_PD;
			sgm->curr_in_limit = 2000000;
			sgm->icl_limit = 3000000;
	}

	if (sgm->usb_type == POWER_SUPPLY_USB_TYPE_DCP && !pd_active) {
		pr_err("dcp deteced, start afc process\n");
		sgm41542S_set_dp(sgm, REG0C_DPDM_OUT_0P6V);
		sgm41542S_set_dm(sgm, REG0C_DPDM_OUT_HIZ);
        if (sgm->is_disble_afc)
	        vote(sgm->usb_icl_votable, DISABLE_AFC_VOTER, true, PD_ICL_CURR_MAX);
		else
            schedule_delayed_work(&sgm->afc_detect_dwork, msecs_to_jiffies(1400));
    }

	sgm41542S_set_ichrg_curr(s_chg_dev, sgm->curr_in_limit);
	sgm41542S_set_input_curr_lim(s_chg_dev, sgm->icl_limit);
	sgm41542S_enable_charger(sgm);
}

static int sgm41542_set_usb(struct sgm41542S_device *sgm, bool online_val)
{
	int ret = 0;
	union power_supply_propval propval;
	if(sgm->usb_psy == NULL) {
		sgm->usb_psy = power_supply_get_by_name("usb");
		if (sgm->usb_psy == NULL) {
			pr_err("%s : fail to get psy usb\n", __func__);
			return -ENODEV;
		}
	}
	propval.intval = online_val;
    if(propval.intval) {
        ret = power_supply_set_property(sgm->usb_psy, POWER_SUPPLY_PROP_PRESENT, &propval);
        if (ret < 0)
            pr_err("set usb present fail ret:%d\n", ret);
        ret = power_supply_set_property(sgm->usb_psy, POWER_SUPPLY_PROP_ONLINE, &propval);
        if (ret < 0)
            pr_err("%s : set upm6922_set_usb fail ret:%d\n", __func__, ret);
    } else {
        ret = power_supply_set_property(sgm->usb_psy, POWER_SUPPLY_PROP_ONLINE, &propval);
        if (ret < 0)
            pr_err("%s : set upm6922_set_usb fail ret:%d\n", __func__, ret);
        ret = power_supply_set_property(sgm->usb_psy, POWER_SUPPLY_PROP_PRESENT, &propval);
        if (ret < 0)
            pr_err("set usb present fail ret:%d\n", ret);
    }
	return ret;
}


static irqreturn_t sgm41542S_irq_handler_thread(int irq, void *private)
{
	struct sgm41542S_device *sgm = private;
	struct sgm41542S_state state;
	int ret;
	int i = 0;

	state = sgm->state;
	pr_err("%s sgm bc12 trigger\n", __func__);
	sgm41542_set_usbsw(sgm, USBSW_CHG);
	if (!sgm->charger_wakelock->active)
		__pm_stay_awake(sgm->charger_wakelock);
	
	mutex_lock(&sgm->lock);
	ret = sgm41542S_get_state(sgm, &sgm->state);
	mutex_unlock(&sgm->lock);

	if (ret) {
		pr_err("get state error!\n");
		goto err;
	}

	for(; i < 3; i++) {
		if(!sgm41542S_dpdm_detect_is_done(sgm) && sgm->state.vbus_gd) {
			dev_err(sgm->dev, "DPDM detecte not done\n");
			msleep(100);
			pr_err("%s delay %d00ms for detect\n", __func__, i);
		} else 
			break;
	}

	pr_err("%s v_pre:%d, v_now", __func__, state.vbus_gd, sgm->state.vbus_gd);


    if (! state.vbus_gd && sgm->state.vbus_gd) {
        pr_notice("adapter/usb inserted\n");
        vote(sgm->usb_icl_votable, DISABLE_AFC_VOTER, false, 0);
        vote(sgm->fcc_votable, DISABLE_AFC_VOTER, false, 0);
        // sgm->force_dpdm = true;
        sgm->vbus_insert_flag = false;
    } else if ( state.vbus_gd && sgm->state.vbus_gd) {
        pr_notice("bc12 detect: adapter || usb removed\n");
        sgm->charge_afc = 0;
		sgm41542_set_usb(sgm, false);
		
        cancel_delayed_work_sync(&sgm->force_dpdm);
		sgm41542_afc_detect_dwork_cancel(sgm);
        // sgm->force_dpdm = false;
        sgm->retry_num = 0;
		// upm6922_set_iio_channel(sgm, NOPMI, NOPMI_CHG_INPUT_SUSPEND, 0);
		sgm41542S_charging_switch(s_chg_dev, true);

        vote(sgm->usb_icl_votable, DISABLE_AFC_VOTER, false, 0);
        vote(sgm->fcc_votable, DISABLE_AFC_VOTER, false, 0);
		sgm41542S_set_input_curr_lim(s_chg_dev, 500);
    } else if ( state.vbus_gd &&sgm->state.vbus_gd) {
        if(sgm->vbus_insert_flag) {
            pr_notice("adapter/usb quick unplug and plug\n");

            rerun_election(sgm->usb_icl_votable);
        }
        sgm->vbus_insert_flag = false;
    }
	    /* bc12 done, get charger type */
    if (!!sgm->state.online) {
        pr_notice("start get chgtype!\n");
		sgm41542_get_charger_type(sgm);
		sgm41542_set_usb(sgm, true);
        rerun_election(sgm->usb_icl_votable);
        // sgm->force_dpdm = false;
        if(sgm->usb_type != POWER_SUPPLY_USB_TYPE_DCP && sgm->usb_type != POWER_SUPPLY_USB_TYPE_UNKNOWN)
            sgm41542_set_usbsw(sgm, USBSW_USB);
		pr_err("get chgtype end!\n");
	}

err:
	power_supply_changed(sgm->charger);
	sgm41542S_dump_register(s_chg_dev);	
	__pm_relax(sgm->charger_wakelock);

	return IRQ_HANDLED;
}

static enum power_supply_property sgm41542S_power_supply_props[] = {
	POWER_SUPPLY_PROP_TYPE,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_PRESENT
};

static enum power_supply_usb_type sgm41542S_usb_types[] = {
    POWER_SUPPLY_USB_TYPE_UNKNOWN,
    POWER_SUPPLY_USB_TYPE_SDP,
    POWER_SUPPLY_USB_TYPE_CDP,
    POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_PD,
};

static char *sgm41542S_charger_supplied_to[] = {
	"usb",
    "mtk-master-charger",
    "primary_chg",
	"mtk-battery",
	"battery"
};

static struct power_supply_desc sgm41542S_power_supply_desc = {
	.name = "charger",
	.type = POWER_SUPPLY_TYPE_USB,
	.usb_types = sgm41542S_usb_types,
    .num_usb_types = ARRAY_SIZE(sgm41542S_usb_types),
	.properties = sgm41542S_power_supply_props,
	.num_properties = ARRAY_SIZE(sgm41542S_power_supply_props),
	.get_property = sgm41542S_charger_get_property,
	.set_property = sgm41542S_charger_set_property,
	.property_is_writeable = sgm41542S_property_is_writeable,
};

static bool sgm41542S_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SGM41542S_INPUT_CURR_LIMIT ... SGM41542S_TFCP_CTRL_2:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config sgm41542S_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = SGM41542S_TFCP_CTRL_2,

	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = sgm41542S_is_volatile_reg,
};

static int sgm41542S_power_supply_init(struct sgm41542S_device *sgm,
				      struct device *dev)
{
	struct power_supply_config psy_cfg = { .drv_data = sgm,
					       .of_node = dev->of_node, };

	psy_cfg.supplied_to = sgm41542S_charger_supplied_to;
	psy_cfg.num_supplicants = ARRAY_SIZE(sgm41542S_charger_supplied_to);
	psy_cfg.of_node = dev->of_node;
	sgm->charger = devm_power_supply_register(sgm->dev,
						  &sgm41542S_power_supply_desc,
						  &psy_cfg);
	if (IS_ERR(sgm->charger)) {
		pr_err("%s %d \n", __func__, PTR_ERR(sgm->charger));
		return -EINVAL;
	}
	return 0;
}

static int sgm41542S_hw_init(struct sgm41542S_device *sgm)
{
	struct power_supply_battery_info bat_info = { };
	int chrg_stat, ret = 0;

		//ret = power_supply_get_battery_info(sgm->charger, &bat_info);

	//if (ret) {
	//	pr_info("sgm41542S: no battery information is supplied\n");
		/*
		 * If no battery information is supplied, we should set
		 * default charge termination current to 120 mA, and default
		 * charge termination voltage to 4.35V.
		 
		bat_info.constant_charge_current_max_ua = 500000;		
		bat_info.constant_charge_voltage_max_uv = SGM41542S_VREG_V_DEF_uV;			
		bat_info.precharge_current_ua = SGM41542S_PRECHRG_I_DEF_uA;			
		bat_info.charge_term_current_ua = SGM41542S_TERMCHRG_I_DEF_uA;
			
	}*/
	if (!bat_info.constant_charge_current_max_ua)
		bat_info.constant_charge_current_max_ua = 500000;
	if (!bat_info.constant_charge_voltage_max_uv)
		bat_info.constant_charge_voltage_max_uv = SGM41542S_VREG_V_DEF_uV;
	if (!bat_info.precharge_current_ua)
		bat_info.precharge_current_ua = SGM41542S_PRECHRG_I_DEF_uA;
	if (!bat_info.charge_term_current_ua)
		bat_info.charge_term_current_ua = SGM41542S_TERMCHRG_I_DEF_uA;	

	ret = sgm41542S_set_watchdog_timer(sgm, 0);
	if (ret)
		goto err_out;

	ret = sgm41542S_set_prechrg_curr(s_chg_dev, bat_info.precharge_current_ua);
	if (ret)
		goto err_out;
pr_err("xpf1\n");
	ret = sgm41542S_set_chrg_volt(s_chg_dev,bat_info.constant_charge_voltage_max_uv);
	if (ret)
		goto err_out;
pr_err("xpf2\n");
	ret = sgm41542S_set_term_curr(s_chg_dev,bat_info.charge_term_current_ua);
	if (ret)
		goto err_out;
pr_err("xpf3\n");
	ret = sgm41542S_set_input_volt_lim(s_chg_dev, sgm->init_data.vlim);
	if (ret)
		goto err_out;
pr_err("xpf4\n");
	ret = sgm41542xx_read_reg(sgm, SGM41542S_CHRG_STAT_1, &chrg_stat);
	if (ret) {
		pr_err("read SGM41542S_CHRG_STAT fail\n");
		goto err_out;
	}

	pr_err("xpf5\n");

	if (!(chrg_stat & SGM41542S_PG_STAT)) {
		ret = sgm41542S_set_input_curr_lim(s_chg_dev, DEFAULT_INPUT_CURRENT);
		if (ret)
			goto err_out;
		ret = sgm41542S_set_ichrg_curr(s_chg_dev,bat_info.constant_charge_current_max_ua);
		if (ret)
			goto err_out;   
	}
	pr_err("xpf6\n");
	sgm41542S_adc_enable(s_chg_dev, true);
    sgm41542S_enable_charger(sgm);
	ret = sgm41542S_set_vac_ovp(sgm);
	if (ret)
		goto err_out;
	pr_err("xpf7\n");
/*
	sgm41542xx_update_bits(sgm,
			   SGM41542S_CHRG_CTRL_d,
			   0x01,
			   0x00);
*/
	sgm41542xx_update_bits(sgm,
			   SGM41542S_CHRG_STAT_2,
			   SGM41542S_IINDPM_INT_MASK | SGM41542S_VINDPM_INT_MASK,
			   SGM41542S_VINDPM_INT_MASK | SGM41542S_IINDPM_INT_MASK);

	pr_err("xpf8\n");
	// ret = sgm41542S_set_recharge_volt(sgm, 200);
	// if (ret)
		// goto err_out;

	pr_err("ichrg_curr:%d\n"
	    "prechrg_curr:%d\n"
	    "chrg_vol:%d\n"
	    "term_curr:%d\n"
	    "input_curr_lim:%d\n",
	    bat_info.constant_charge_current_max_ua,
	    bat_info.precharge_current_ua,
	    bat_info.constant_charge_voltage_max_uv,
	    bat_info.charge_term_current_ua,
	    sgm->init_data.ilim);

	pr_err("hw_init succuess\n");
	return 0;

err_out:
	pr_err("read SGM41542S_CHRG_reg fail%d\n", ret);
	return ret;
}

static int sgm41542S_parse_dt(struct sgm41542S_device *sgm)
{
	int ret;

	ret = device_property_read_u32(sgm->dev,
				       "input-voltage-limit-microvolt",
				       &sgm->init_data.vlim);
	if (ret)
		sgm->init_data.vlim = SGM41542S_VINDPM_DEF_uV;

	if (sgm->init_data.vlim > SGM41542S_VINDPM_V_MAX_uV ||
	    sgm->init_data.vlim < SGM41542S_VINDPM_V_MIN_uV)
		return -EINVAL;

	ret = device_property_read_u32(sgm->dev,
				       "input-current-limit-microamp",
				       &sgm->init_data.ilim);
	if (ret)
		sgm->init_data.ilim = SGM41542S_IINDPM_DEF_uA;

	if (sgm->init_data.ilim > SGM41542S_IINDPM_I_MAX_uA ||
	    sgm->init_data.ilim < SGM41542S_IINDPM_I_MIN_uA)
		return -EINVAL;

	sgm->intr_gpio = of_get_named_gpio(sgm->dev->of_node, "sgm,intr_gpio", 0);
    if (sgm->intr_gpio < 0) {
        pr_err("no up,intr_gpio, ret:%d\n", sgm->intr_gpio);
    }
    pr_err("intr_gpio = %d\n", sgm->intr_gpio);

	return 0;
}

static int sgm41542S_set_otg_voltage(struct charger_device *chg_dev, u32 uv)
{
	int ret = 0;
	int reg_val;

	struct sgm41542S_device *sgm = charger_get_data(chg_dev);
	for(reg_val = 1; reg_val < 8 && uv >= BOOSTV_STABLE[reg_val]; reg_val++)
	{
		pr_debug("%s\n",__func__);
	}
	reg_val--;
	
	reg_val = reg_val << SGM41542S_BOOSTV_SHIFT;
	ret = sgm41542xx_update_bits(sgm,
				 SGM41542S_CHRG_CTRL_3,
				 SGM41542S_BOOSTV,
				 reg_val);
	if (ret) {
		dev_err(sgm->dev, "set otg voltage error!\n");
		return ret;
	}

	pr_err("%s successfully", __func__);

	return ret;
}

static int sgm41542S_set_otg_current(struct charger_device *chg_dev, u32 ua)
{
	int ret = 0;
	int reg_val = 0;
	struct sgm41542S_device *sgm = charger_get_data(chg_dev);
	for(reg_val = 1; reg_val < 9 && ua >= BOOST_CURRENT_LIMIT[reg_val]; reg_val++)
		;
	reg_val--;
	
	ret = sgm41542xx_update_bits(sgm,
				 SGM41542S_CHRG_CTRL_8,
				 SGM41542S_BOOST_LIM,
				 reg_val);
	if (ret) {
		dev_err(sgm->dev, "set boost current limit error!\n");
		return ret;
	}
	return ret;
}
static int sgm415452S_enable_hiz(struct charger_device *chg_dev, bool en)
{
	struct sgm41542S_device *sgm = charger_get_data(chg_dev);
	int ret;

	if (en)
	{
		ret = sgm41542xx_update_bits(sgm,
					SGM41542S_CHRG_CTRL_10,
					SGM41542S_EN_HIZ,
					SGM41542S_EN_HIZ);
		if (ret) {
			dev_err(sgm->dev, "set hiz mode fail!\n");
			return ret;
		}
	} else {
		ret = sgm41542xx_update_bits(sgm,
					SGM41542S_CHRG_CTRL_10,
					SGM41542S_EN_HIZ,
					0);
		if (ret) {
			dev_err(sgm->dev, "set hiz mode fail!\n");
			return ret;
		}
	}
	pr_err("%s enable:%d\n", __func__, en);

	return ret;
}

static int sgm41542S_enable_vbus(struct charger_device *chg_dev, bool en)
{
	struct sgm41542S_device *sgm = charger_get_data(chg_dev);
	int ret = 0;
	int val;
	if (en) {
		val = SGM41542S_OTG_EN;
		sgm41542S_disable_charger(sgm);
	}
	else {
		val = 0;
		sgm41542S_enable_charger(sgm);
	}
	ret = sgm41542xx_update_bits(sgm,
				 SGM41542S_CHRG_CTRL_1,
				 SGM41542S_OTG_EN,
				 val);
	if (ret) {
		dev_err(sgm->dev, "set OTG enable error!\n");
		return ret;
	}

	sgm41542S_set_otg_voltage(s_chg_dev, 5000000);
	pr_err("%s enable otg vbus successfully\n", __func__);

	return 0;
}

static int sgm_enable_vbus(struct regulator_dev *rdev)
{
	int ret;

    ret = sgm41542S_enable_vbus(s_chg_dev, true);
    pr_err("ret = %d\n", ret);

	return 0;
}

static int sgm41542S_disable_vbus(struct regulator_dev *rdev)
{
	struct sgm41542S_device *sgm = rdev_get_drvdata(rdev);
	int ret = 0;

	ret = sgm41542xx_update_bits(sgm,
				 SGM41542S_CHRG_CTRL_1,
				 SGM41542S_OTG_EN,
				 0);
	if (ret) {
		dev_err(sgm->dev, "set OTG disable error!\n");
		return ret;
	}

	return ret;
}

static int sgm41542S_is_enabled_vbus(struct regulator_dev *rdev)
{
	struct sgm41542S_device *sgm = rdev_get_drvdata(rdev);
	int temp = 0;
	int ret = 0;

	ret = sgm41542xx_read_reg(sgm, SGM41542S_CHRG_CTRL_1, &temp);
	if (ret) {
		dev_err(sgm->dev, "get vbus status error!\n");
		return ret;
	}

	return (temp & SGM41542S_OTG_EN) ? 1 : 0;
}

static const struct regulator_ops sgm41542S_vbus_ops = {
	.enable = sgm_enable_vbus,
	.disable = sgm41542S_disable_vbus,
	.is_enabled = sgm41542S_is_enabled_vbus,
};

static struct regulator_desc sgm41542S_otg_rdesc = {
	.of_match = "sgm-usb-otg-vbus",
	.name = "usb-otg-vbus",
	.ops = &sgm41542S_vbus_ops,
	.owner = THIS_MODULE,
	.type = REGULATOR_VOLTAGE,
	.fixed_uV = 5000000,
	.n_voltages = 1,
};

static int sgm41542S_vbus_regulator_register(struct sgm41542S_device *sgm)
{	
	struct regulator_config config = {};
	int ret = 0;
/*
	np = of_get_child_by_name(sgm->dev->of_node, "regulators");
	if (!np) {
		dev_warn(sgm->dev, "cannot find regulators node\n");
		return -ENXIO;
	}*/

	/* otg regulator */
	config.dev = sgm->dev;
	config.driver_data = sgm;
	sgm->otg_rdev = devm_regulator_register(sgm->dev,
						&sgm41542S_otg_rdesc,
						&config);
	if (IS_ERR(sgm->otg_rdev)) {
		ret = PTR_ERR(sgm->otg_rdev);
		pr_err("register otg regulator failed (%d)\n", ret);
	}

	pr_err("register otg regulator successfully (%d)\n", ret);
	return ret;
}

static int sgm41542S_suspend_notifier(struct notifier_block *nb, unsigned long event, void *dummy);
static int sgm41542S_suspend_notifier(struct notifier_block *nb,
				     unsigned long event,
				     void *dummy)
{
	struct sgm41542S_device *sgm = container_of(nb, struct sgm41542S_device, pm_nb);

	switch (event) {

	case PM_SUSPEND_PREPARE:
		sgm->sgm41542S_suspend_flag = 1;
		return NOTIFY_OK;

	case PM_POST_SUSPEND:
		sgm->sgm41542S_suspend_flag = 0;
		return NOTIFY_OK;

	default:
		return NOTIFY_DONE;
	}
}

static int sgm41542S_dump_register(struct charger_device *chg_dev)
{	

	unsigned char i = 0;
	unsigned int ret = 0;
	unsigned int sgm41542S_reg[SGM41542S_TFCP_CTRL_2+1] = { 0 }; 
	char buf[512] = {0};
	int n = 0;
	struct sgm41542S_device *sgm = charger_get_data(chg_dev);

	sprintf(buf + n,"%s:  ", __func__);
	n = strlen(buf);

	for (i = 0; i <= SGM41542S_TFCP_CTRL_2; i++) {
		ret = sgm41542xx_read_reg(sgm,i, &sgm41542S_reg[i]);
		if (ret != 0) {
			pr_info("[sgm41542s] i2c transfor error\n");
			return 1;
		}
		sprintf(buf + n,"Reg[%.2x] = 0x%.2x ", i, sgm41542S_reg[i]);
		n = strlen(buf);
	}
	pr_err("%s\n",buf);
	
	return 0;
}

static int sgm41542S_hw_chipid_detect(struct sgm41542S_device *sgm)
{
	int ret = 0;
	int val = 0;	

	ret = sgm41542xx_read_reg(sgm, SGM41542S_PART_INFO, &val);
	if (ret)
	{
		dev_err(sgm->dev,"[%s] read fail\n", __func__);
		return false;
	}

	pr_info("[%s] Reg[0x0B]=0x%x\n", __func__,val);
	val = val & SGM41542S_PN_MASK;
	
	if (SGM41542S_PN == val)
	{		
		pr_info("[%s] \n", __func__);					
		return true;
	}			
	
	pr_info("[%s] don't found device ,Reg[0x0B]=0x%x\n", __func__,val);	
	return true;
}

static int sgm41542S_iio_read_raw(struct iio_dev *indio_dev,
		struct iio_chan_spec const *chan, int *val1,
		int *val2, long mask)
{
    struct mtk_battery *gm;
	struct power_supply *psy;
	u32 val = 0;
	struct sgm41542S_device *sgm = iio_priv(indio_dev);
	int rc = 0;
	*val1 = 0;


	if(!sgm){
		pr_err("sgm is NULL,Fail !\n");
		return -EINVAL;
	}

	switch (chan->channel) {
	case PSY_IIO_CHARGE_TYPE:
		rc = sgm41542S_get_charging_status(sgm);
		switch (rc) {
			case CHRG_STAT_NOT_CHARGING:
				*val1 = POWER_SUPPLY_CHARGE_TYPE_NONE;
				break;
			case CHRG_STAT_PRE_CHARGINT:
				*val1 = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
				break;
			case CHRG_STAT_FAST_CHARGING:
				*val1 = POWER_SUPPLY_CHARGE_TYPE_FAST;
				break;
			case CHRG_STAT_CHARGING_TERM:
				*val1 = POWER_SUPPLY_CHARGE_TYPE_NONE;
				break;
			default:
				*val1 = POWER_SUPPLY_CHARGE_TYPE_NONE;
				break;
		}
		break;
	case PSY_IIO_USB_MA:
		sgm41542S_get_input_curr_lim(s_chg_dev,&val);
		*val1 = val;
		break;
	case PSY_IIO_CHARGE_AFC:
		*val1 = sgm->charge_afc;
		break;
    case PSY_IIO_RESISTANCE_ID:
		psy = power_supply_get_by_name("mtk_battery");
		if (psy == NULL) {
            pr_err("can not get battery psy, get PSY_IIO_RESISTANCE_ID fail\n");
            return -EINVAL;
        }
		gm = (struct mtk_battery *)power_supply_get_drvdata(psy);
		if (gm == NULL) {
            pr_err("can not get battery drvdata, get PSY_IIO_RESISTANCE_ID fail\n");
			return -EINVAL;
        }
        *val1 = gm->battery_id;
        pr_err("PSY_IIO_RESISTANCE_ID %d\n", chan->channel);
        break;
	default:
		pr_err("Unsupported sgm41542S IIO chan %d\n", chan->channel);
		rc = -EINVAL;
		break;
	}

	//pr_info("read IIO channel %d, rc = %d, val1 = %d\n", chan->channel, rc, *val1);
	if (rc < 0) {
		pr_err("Couldn't read IIO channel %d, rc = %d\n",
			chan->channel, rc);
		return rc;
	}
	return IIO_VAL_INT;
}
void sgm41542S_disable_afc(struct sgm41542S_device *sgm)
{
	pr_err("%s\n", __func__);
	return;
}

bool sgm41542S_get_pdo_active(struct sgm41542S_device *sgm)
{
	int pdo_active = 0;
	int rc = 0;

    	if (!sgm->tcpc)
        	sgm->tcpc = tcpc_dev_get_by_name("type_c_port0");
	
	if (!sgm->tcpc)
		pr_err("tcpc_dev is null\n");
	else {
		rc = tcpm_get_pd_connect_type(sgm->tcpc);
		pr_err("%s: pdo is %d\n", __func__, rc);
		if ((rc == PD_CONNECT_PE_READY_SNK_PD30) ||
                    (rc == PD_CONNECT_PE_READY_SNK) || rc == PD_CONNECT_PE_READY_SNK_APDO) {
			pdo_active = 1;
		} else {
			pdo_active = 0;
	}
	}

	return pdo_active;
}

bool sgm41542S_disable_pdo(struct sgm41542S_device *sgm)
{
	pr_err("%s\n", __func__);
	return false;
}

static int sgm41542S_iio_write_raw(struct iio_dev *indio_dev,
		struct iio_chan_spec const *chan, int val1,
		int val2, long mask)
{
	struct sgm41542S_device *sgm = iio_priv(indio_dev);
	int rc = 0;

	if(!sgm){
		pr_err("sgm is NULL,Fail !\n");
		return -EINVAL;
	}

	pr_info("Write IIO channel %d, val = %d\n", chan->channel, val1);
	switch (chan->channel) {
	case PSY_IIO_CHARGE_TYPE:
		sgm->chg_type = val1;
		break;
	case PSY_IIO_CHARGE_ENABLED:
		pr_err("Write enable sgm charger , val = %d\n", val1);
		if (val1) {
			sgm41542S_enable_charger(sgm);
		} else {
			sgm41542S_disable_charger(sgm);
		}
		break;
	case PSY_IIO_CHARGE_DISABLE:
		sgm41542S_disable_charger(sgm);
		pr_err("Write PSY_IIO_CHARGE_DISABLE, val = %d\n", val1);
		break;
	case PSY_IIO_CHARGE_AFC_DISABLE:
		sgm->is_disble_afc = val1;
		pr_info("%s, bakup afc:%d disafc:%d\n",__func__, sgm->is_disble_afc_backup, sgm->is_disble_afc);

		if (sgm->is_disble_afc_backup != sgm->is_disble_afc) {
			if (sgm->charge_afc == 1) {
				pr_err("disable afc\n");
				sgm41542S_disable_afc(sgm);
			} else if ((sgm41542S_get_pdo_active(sgm) == 1)) {
				pr_err("disable pdo\n");
				sgm41542S_disable_pdo(sgm);
			}
			sgm->is_disble_afc_backup = sgm->is_disble_afc;
                  	pr_info("%s, 2 bakup afc:%d disafc:%d\n",__func__, sgm->is_disble_afc_backup, sgm->is_disble_afc);
		} else {
			pr_info("No changes, no need adjust vbus\n");
		}
		//pr_err("%s: is_disble_afc=%d\n", __func__, sgm->is_disble_afc);
		break;
	default:
		pr_err("Unsupported sgm41542S IIO chan %d\n", chan->channel);
		rc = -EINVAL;
		break;
	}

	if (rc < 0){
		pr_err("Couldn't write IIO channel %d, rc = %d\n",
			chan->channel, rc);
		return rc;
	}

	return IIO_VAL_INT;
}

static int sgm41542S_iio_of_xlate(struct iio_dev *indio_dev,
				const struct of_phandle_args *iiospec)
{
	struct sgm41542S_device *sgm= iio_priv(indio_dev);
	struct iio_chan_spec *iio_chan = sgm->iio_chan;
	int i = 0;

	if(!sgm){
		pr_err("sgm is NULL,Fail !\n");
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(SGM41542S_iio_psy_channels);
					i++, iio_chan++)
		if (iio_chan->channel == iiospec->args[0]){
			return i;
		}
	return -EINVAL;

}

static const struct iio_info sgm41542S_iio_info = {
	.read_raw	= sgm41542S_iio_read_raw,
	.write_raw	= sgm41542S_iio_write_raw,
	.of_xlate	= sgm41542S_iio_of_xlate,
};

static int sgm41542S_init_iio_psy(struct sgm41542S_device *sgm)
{
	struct iio_dev *indio_dev = sgm->indio_dev;
	struct iio_chan_spec *chan = NULL;
	int num_iio_channels = ARRAY_SIZE(SGM41542S_iio_psy_channels);
	int rc = 0, i = 0;

	pr_info("start !\n");

	if(!sgm){
		pr_err("sgm is NULL,Fail !\n");
		return -EINVAL;
	}

	sgm->iio_chan = devm_kcalloc(sgm->dev, num_iio_channels,
						sizeof(*sgm->iio_chan), GFP_KERNEL);
	if (!sgm->iio_chan) {
		pr_err("sgm->iio_chan is null!\n");
		return -ENOMEM;
	}

	sgm->int_iio_chans = devm_kcalloc(sgm->dev,
				num_iio_channels,
				sizeof(*sgm->int_iio_chans),
				GFP_KERNEL);
	if (!sgm->int_iio_chans) {
		pr_err("sgm->int_iio_chans is null!\n");
		return -ENOMEM;
	}

	indio_dev->info = &sgm41542S_iio_info;
	indio_dev->dev.parent = sgm->dev;
	indio_dev->dev.of_node = sgm->dev->of_node;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = sgm->iio_chan;
	indio_dev->num_channels = num_iio_channels;
	indio_dev->name = "main_chg";

	for (i = 0; i < num_iio_channels; i++) {
		sgm->int_iio_chans[i].indio_dev = indio_dev;
		chan = &sgm->iio_chan[i];
		sgm->int_iio_chans[i].channel = chan;
		chan->address = i;
		chan->channel = SGM41542S_iio_psy_channels[i].channel_num;
		chan->type = SGM41542S_iio_psy_channels[i].type;
		chan->datasheet_name =
			SGM41542S_iio_psy_channels[i].datasheet_name;
		chan->extend_name =
			SGM41542S_iio_psy_channels[i].datasheet_name;
		chan->info_mask_separate =
			SGM41542S_iio_psy_channels[i].info_mask;
	}

	rc = devm_iio_device_register(sgm->dev, indio_dev);
	if (rc)
		pr_err("Failed to register sgm41542S IIO device, rc=%d\n", rc);

	pr_info("sgm41542S IIO device, rc=%d\n", rc);
	return rc;
}

static int sgm41542S_ext_init_iio_psy(struct sgm41542S_device *sgm)
{
	if (!sgm){
		pr_err("sgm41542S_ext_init_iio_psy, sgm is NULL!\n");
		return -ENOMEM;
	}

	sgm->ds_ext_iio_chans = devm_kcalloc(sgm->dev,
				ARRAY_SIZE(ds_ext_iio_chan_name),
				sizeof(*sgm->ds_ext_iio_chans),
				GFP_KERNEL);
	if (!sgm->ds_ext_iio_chans) {
		pr_err("sgm->ds_ext_iio_chans is NULL!\n");
		return -ENOMEM;
	}

	sgm->fg_ext_iio_chans = devm_kcalloc(sgm->dev,
		ARRAY_SIZE(fg_ext_iio_chan_name), sizeof(*sgm->fg_ext_iio_chans), GFP_KERNEL);
	if (!sgm->fg_ext_iio_chans) {
		pr_err("sgm->fg_ext_iio_chans is NULL!\n");
		return -ENOMEM;
	}

	sgm->nopmi_chg_ext_iio_chans = devm_kcalloc(sgm->dev,
		ARRAY_SIZE(nopmi_chg_ext_iio_chan_name), sizeof(*sgm->nopmi_chg_ext_iio_chans), GFP_KERNEL);
	if (!sgm->nopmi_chg_ext_iio_chans) {
		pr_err("sgm->nopmi_chg_ext_iio_chans is NULL!\n");
		return -ENOMEM;
	}
	return 0;
}


static void sgm_charger_work(struct work_struct *work)
{
	struct sgm41542S_device *sgm =
		container_of(work,
			     struct sgm41542S_device,
			     sgm_delay_work.work);

	sgm41542S_watchdog_timer_reset(s_chg_dev);
	if (sgm->watchdog_enable)
		queue_delayed_work(sgm->sgm_monitor_wq,
				   &sgm->sgm_delay_work,
				   msecs_to_jiffies(1000 * 5));
}

static struct charger_ops sgm41542S_chg_ops = {
		
	.dump_registers = sgm41542S_dump_register,
	/* cable plug in/out */
  // .plug_in = mt6375_plug_in,
  // .plug_out = mt6375_plug_out,
   /* enable */
   .enable = sgm41542S_charging_switch,
   .is_enabled = sgm41542S_is_charging,
   /* charging current */
   .set_charging_current = sgm41542S_set_ichrg_curr,
   .get_charging_current = sgm41542S_get_ichrg_curr,
   .get_min_charging_current = sgm41542S_get_minichg_curr,
   /* charging voltage */
   .set_constant_voltage = sgm41542S_set_chrg_volt,
   .get_constant_voltage = sgm41542S_get_chrg_volt,
   /* input current limit */
   .set_input_current = sgm41542S_set_input_curr_lim,
   .get_input_current = sgm41542S_get_input_curr_lim,
   .get_min_input_current = sgm41542S_get_input_mincurr_lim,
   /* MIVR */
   .set_mivr = sgm41542S_set_input_volt_lim,
   .get_mivr = sgm41542S_get_input_volt_lim,
   //.get_mivr_state = sgm41542S_get_input_minvolt_lim,
   /* ADC */
   //.get_adc = mt6375_get_adc,
   //.get_vbus_adc = mt6375_get_vbus,
   //.get_ibus_adc = mt6375_get_ibus,
   //.get_ibat_adc = mt6375_get_ibat,
   //.get_tchg_adc = mt6375_get_tchg,
   //.get_zcv = mt6375_get_zcv,
   /* charing termination */
   .set_eoc_current = sgm41542S_set_term_curr,
   //.enable_termination = mt6375_enable_te,
   //.reset_eoc_state = mt6375_reset_eoc_state,
   //.safety_check = mt6375_sw_check_eoc,
   .is_charging_done = sgm41542S_get_ischarging_done,
   /* power path */
   //.enable_powerpath = mt6375_enable_buck,
   //.is_powerpath_enabled = mt6375_is_buck_enabled,
   /* timer */
   .enable_safety_timer = sgm41542S_enable_safetytimer,
   .is_safety_timer_enabled = sgm41542S_get_is_safetytimer_enable,
   .kick_wdt = sgm41542S_watchdog_timer_reset,
   /* AICL */
   //.run_aicl = mt6375_run_aicc,
   /* PE+/PE+20 */
	.send_ta_current_pattern = sgm41542S_en_pe_current_partern,

   //.set_pe20_efficiency_table = mt6375_set_pe20_efficiency_table,
   //.send_ta20_current_pattern = mt6375_set_pe20_current_pattern,
   //.reset_ta = mt6375_reset_pe_ta,
   //.enable_cable_drop_comp = mt6
   /* OTG */
	.enable_otg = sgm41542S_enable_vbus,
	.enable_hiz = sgm415452S_enable_hiz,
	.set_boost_current_limit = sgm41542S_set_otg_current,
};

static void sgm_monitor_dwork_handler(struct work_struct *work)
{
    struct sgm41542S_device *sgm = container_of(work, struct sgm41542S_device,
                                monitor_dwork.work);

    __pm_stay_awake(sgm->charger_wakelock);

    pr_info("monitor work\n");
    sgm41542S_dump_register(s_chg_dev);

    schedule_delayed_work(&sgm->monitor_dwork, msecs_to_jiffies(5*1000));

    __pm_relax(sgm->charger_wakelock);
}

static int sgm_force_dpm(struct sgm41542S_device *sgm)
{
	int ret;
	ret = sgm41542xx_update_bits(sgm,
				SGM41542S_CHRG_CTRL_4,
				SGM41542S_IINDET_EN, SGM41542S_IINDET_EN);
	if (ret){
		pr_err("force dpdm error! ret=%d\n", ret);
		return ret;	
	}

	pr_err("force dpdm successfully! ret=%d\n", ret);
	return 0;
}

static void sgm_force_dpm_dwork_handler(struct work_struct *work)
{
	struct sgm41542S_device *sgm = container_of(work, struct sgm41542S_device,
							force_dpdm.work);

	sgm41542_set_usbsw(sgm, USBSW_CHG);
	sgm_force_dpm(sgm);
}

// afc charging
static void sgm_afc_detect_work(struct work_struct *work)
{
	int ret = 0;
	struct sgm41542S_device *sgm = container_of(work, struct sgm41542S_device,
							afc_detect_dwork.work);
  	unsigned int afc_code = AFC_QUICK_CHARGE_POWER_CODE;
  	int afc_icl_current = 0;

  	pr_err("is_disble_afc=%d\n", sgm->is_disble_afc);
  	if (sgm->is_disble_afc) {
  		afc_code = AFC_COMMIN_CHARGE_POWER_CODE;
  		afc_icl_current = AFC_COMMON_ICL_CURR_MAX;
  	} else {
  		afc_code = AFC_QUICK_CHARGE_POWER_CODE;
  		afc_icl_current = AFC_ICL_CURR_MAX;
  	}

  	ret = afc_set_voltage_workq(afc_code);
	if (!ret) {
        if(sgm->is_disble_afc)
            vote(sgm->usb_icl_votable, DISABLE_AFC_VOTER, true, AFC_COMMON_ICL_CURR_MAX);

		pr_info("set afc adapter iindpm to 1700mA\n");
		vote(sgm->usb_icl_votable, CHARGER_TYPE_VOTER, true, 1670);
        sgm->charge_afc = 1;
	} else {
        if(sgm41542S_get_pdo_active(sgm)) {
            if (sgm->is_disble_afc)
                vote(sgm->usb_icl_votable, DISABLE_AFC_VOTER, true, PD_ICL_CURR_MAX);

            pr_err("afc communication failed, restore adapter iindpm to 1700mA\n");
            vote(sgm->usb_icl_votable, CHARGER_TYPE_VOTER, true, 1670);
        } else {
            pr_err("afc communication failed, restore adapter iindpm to 2000mA\n");
            vote(sgm->usb_icl_votable, CHARGER_TYPE_VOTER, true, 2000);
        }
        sgm->charge_afc = 0;
	}
}


static void __maybe_unused sgm41542_disable_afc_dwork(struct work_struct *work)
{
	unsigned int afc_code = AFC_QUICK_CHARGE_POWER_CODE;
	int afc_icl_current = 0;
	int afc_icharge_current = 0;
	int ret = 0;
	struct sgm41542S_device *sgm = container_of(work, struct sgm41542S_device,
				disable_afc_dwork.work);

	if (sgm->is_disble_afc) {
		afc_code = AFC_COMMIN_CHARGE_POWER_CODE;
		afc_icl_current = AFC_COMMON_ICL_CURR_MAX;
		afc_icharge_current = CHG_AFC_COMMON_CURR_MAX;
	} else {
		afc_code = AFC_QUICK_CHARGE_POWER_CODE;
		afc_icl_current = AFC_ICL_CURR_MAX;
		afc_icharge_current = CHG_AFC_CURR_MAX;
	}

	pr_err("%s: afc_code=%x\n", __func__,afc_code);

	ret = afc_set_voltage_workq(afc_code);
	if (!ret) {
		pr_info("set afc adapter iindpm\n");
		vote(sgm->usb_icl_votable, DISABLE_AFC_VOTER, true, afc_icl_current);
		vote(sgm->fcc_votable, DISABLE_AFC_VOTER, true, afc_icharge_current);
	}
}
static void __maybe_unused sgm41542_disable_afc(struct sgm41542S_device *sgm )
{
	vote(sgm->usb_icl_votable, DISABLE_AFC_VOTER, true, 500);

	cancel_delayed_work_sync(&sgm->disable_afc_dwork);
	schedule_delayed_work(&sgm->disable_afc_dwork, msecs_to_jiffies(500));
}

static void sgm_disable_pdo_dwork(struct work_struct *work)
{
	int pdo_icl_current = 0;
	struct sgm41542S_device *sgm = container_of(work, struct sgm41542S_device,
				disable_pdo_dwork.work);

	if (sgm->is_disble_afc) {
		pdo_icl_current = PD_ICL_CURR_MAX;
	} else {
		pdo_icl_current = DCP_ICL_CURR_MAX;
	}

	vote(sgm->usb_icl_votable, DISABLE_AFC_VOTER, true, pdo_icl_current);
}
//afc charging end

/* voter */
static int sgm_fcc_vote_callback(struct votable *votable, void *data,
			int fcc_ua, const char *client)
{
	int rc = 0;

	fcc_ua *=1000;
	if (fcc_ua < 0) {
		pr_err("fcc_ua: %d < 0, ERROR!\n", fcc_ua);
		return 0;
	}

	if (fcc_ua > SGM41542S_ICHRG_I_MAX_uA)
		fcc_ua = SGM41542S_ICHRG_I_MAX_uA;

	rc = sgm41542S_set_ichrg_curr(s_chg_dev, fcc_ua);
	if (rc < 0) {
		pr_err("failed to set charge current\n");
		return rc;
	}

	pr_err("ch_log  fcc[%d]\n",fcc_ua);
	sgm41542S_dump_register(s_chg_dev);
	return 0;
}

static int sgm_chg_dis_vote_callback(struct votable *votable, void *data,
			int disable, const char *client)
{
	struct sgm41542S_device *sgm  = data;
	int rc = 0;

	if (disable) {
		rc = sgm41542S_disable_charger(sgm);
	} else {
		rc = sgm41542S_enable_charger(sgm);
	}

	if (rc < 0) {
		pr_err("failed to disabl:%d\n", disable);
		return rc;
	}

	pr_info("disable:%d\n", disable);
	sgm41542S_dump_register(s_chg_dev);
	return 0;
}

static int sgm_fv_vote_callback(struct votable *votable, void *data,
			int fv_mv, const char *client)
{
	int rc = 0;
	u8 val = 0;
	fv_mv *= 1000;

    pr_err("fv_mv: %d \n", fv_mv);
	if (fv_mv < 0) {
		pr_err("fv_mv: %d < 0, ERROR!\n", fv_mv);
		return 0;
	}

	rc = sgm41542S_set_chrg_volt(s_chg_dev, fv_mv);
	if (rc < 0) {
		pr_err("failed to set chargevoltage\n");
		return rc;
	}

	pr_err("fv:%d,reg = %x\n", fv_mv, val);
	sgm41542S_dump_register(s_chg_dev);
	return 0;
}

static int sgm_usb_icl_vote_callback(struct votable *votable, void *data,
			int icl_ma, const char *client)
{
	int rc;

	icl_ma *= 1000;
	if (icl_ma < 0){
		pr_err("icl_ma: %d < 0, ERROR!\n", icl_ma);
		return 0;
	}

	if (icl_ma > SGM41542S_IINDPM_I_MAX_uA)
		icl_ma = SGM41542S_IINDPM_I_MAX_uA;

	rc = sgm41542S_set_input_curr_lim(s_chg_dev, icl_ma);
	if (rc < 0) {
		pr_err("failed to set input current limit\n");
		return rc;
	}
	pr_err("icl_ma[%d]\n",icl_ma);
	sgm41542S_dump_register(s_chg_dev);
	return 0;
}

static int sgm41542S_init_vote(struct sgm41542S_device *sgm)
{
	int ret = 0;
	sgm->fcc_votable = create_votable("FCC", VOTE_MIN,
					sgm_fcc_vote_callback, sgm);
	if (IS_ERR(sgm->fcc_votable)) {
		pr_err("fcc_votable is ERROR,goto destroy!\n");
		ret = PTR_ERR(sgm->fcc_votable);
		sgm->fcc_votable = NULL;
		return -1;
	}
	pr_info("fcc_votable create successful !\n");

	sgm->chg_dis_votable = create_votable("CHG_DISABLE", VOTE_SET_ANY,
					sgm_chg_dis_vote_callback, sgm);
	if (IS_ERR(sgm->chg_dis_votable)) {
		pr_err("chg_dis_votable is ERROR,goto destroy!\n");
		ret = PTR_ERR(sgm->chg_dis_votable);
		sgm->chg_dis_votable = NULL;
		goto destroy_votable;
	}
	pr_info("chg_dis_votable create successful !\n");

	sgm->fv_votable = create_votable("FV", VOTE_MIN,
					sgm_fv_vote_callback, sgm);
	if (IS_ERR(sgm->fv_votable)) {
		pr_err("fv_votable is ERROR,goto destroy!\n");
		ret = PTR_ERR(sgm->fv_votable);
		sgm->fv_votable = NULL;
		goto destroy_votable;
	}
	pr_info("fv_votable create successful !\n");

	sgm->usb_icl_votable = create_votable("USB_ICL", VOTE_MIN,
					sgm_usb_icl_vote_callback,
					sgm);
	if (IS_ERR(sgm->usb_icl_votable)) {
		pr_err("usb_icl_votable is ERROR,goto destroy!\n");
		ret = PTR_ERR(sgm->usb_icl_votable);
		sgm->usb_icl_votable = NULL;
		goto destroy_votable;
	}

	return 0;

	destroy_votable:
	pr_err("destory votable !\n");
	destroy_votable(sgm->fcc_votable);
	destroy_votable(sgm->chg_dis_votable);
	destroy_votable(sgm->fv_votable);
	destroy_votable(sgm->usb_icl_votable);

	return -1;
}

// voter end

int sgm41542S_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct sgm41542S_device *sgm;
	int ret;
	struct iio_dev *indio_dev = NULL;

	dev_err(dev, "start to  init sgm41542s_probe\n");

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(struct sgm41542S_device));
	if (!indio_dev) {
		pr_err("Failed to allocate memory\n");
		return -ENOMEM;
	}
	sgm = iio_priv(indio_dev);
	sgm->indio_dev = indio_dev;
    if (!sgm) {
        pr_err("alloc sgm41542S struct failed\n");
        return -ENOMEM;
    }

	dev_err(dev, "i2c addr%x:\n", client->addr);

	client->addr = 0x6b;
	sgm->client = client;
	sgm->dev = dev;

	mutex_init(&sgm->lock);
    sgm->charger_wakelock = wakeup_source_register(sgm->dev, "sgm_chg_wakelock");

	strncpy(sgm->model_name, id->name, I2C_NAME_SIZE);
/*
	sgm->regmap = devm_regmap_init_i2c(client, &sgm41542S_regmap_config);
	if (IS_ERR(sgm->regmap)) {
		dev_err(dev, "Failed to allocate register map\n");
		return PTR_ERR(sgm->regmap);
	}
*/
	i2c_set_clientdata(client, sgm);

	mutex_init(&i2c_rw_lock);
	ret = sgm41542S_parse_dt(sgm);
	if (ret) {
		dev_err(dev, "Failed to read device tree properties%d\n", ret);
		return ret;
	}

	ret = sgm41542S_hw_chipid_detect(sgm);
	if (!ret){
		dev_err(dev, "failed to detect chipid\n");
		sgm_init_flag = true;
		return ret;
	}
	sgm_init_flag = false; 
	device_init_wakeup(dev, 1);

	sgm->chg_dev = charger_device_register("primary_chg",
						&client->dev, sgm,
						&sgm41542S_chg_ops,
						&sgm41542S_chg_props);
	if (IS_ERR_OR_NULL(sgm->chg_dev)) {
		pr_info("%s: register charger device  failed\n", __func__);
		ret = PTR_ERR(sgm->chg_dev);
		return ret;
	}
	DBG("%s register chg devices success!\n");	
	/* otg regulator */
	s_chg_dev=sgm->chg_dev;
	client->irq = gpio_to_irq(sgm->intr_gpio);

	if (client->irq) {
		ret = devm_request_threaded_irq(dev, client->irq, NULL,
						sgm41542S_irq_handler_thread,
						IRQF_TRIGGER_FALLING |
						IRQF_ONESHOT,
						"sgm41542S-irq", sgm);
		if (ret) {
			dev_err(dev, "failed to requset irq\n");
			goto error_out;
		}
		enable_irq_wake(client->irq);
	}
	DBG("%s register chg irq success!\n");

	ret = sgm41542S_ext_init_iio_psy(sgm);
	if (ret < 0) {
		pr_err("Failed to initialize sgm41542S_ext_init_iio_psy IIO PSY, rc=%d\n", ret);
	}

	sgm41542S_init_iio_psy(sgm);
	INIT_DELAYED_WORK(&sgm->monitor_dwork, sgm_monitor_dwork_handler);
	INIT_DELAYED_WORK(&sgm->force_dpdm, sgm_force_dpm_dwork_handler);
	INIT_DELAYED_WORK(&sgm->afc_detect_dwork, sgm_afc_detect_work);
	INIT_DELAYED_WORK(&sgm->disable_afc_dwork, sgm41542_disable_afc_dwork);
	INIT_DELAYED_WORK(&sgm->disable_pdo_dwork, sgm_disable_pdo_dwork);
	sgm->pm_nb.notifier_call = sgm41542S_suspend_notifier;
	register_pm_notifier(&sgm->pm_nb);

	ret = sgm41542S_power_supply_init(sgm, dev);
	if (ret) {
		dev_err(dev, "Failed to register power supply\n");
		goto error_out;
	}
 	schedule_delayed_work(&sgm->monitor_dwork, msecs_to_jiffies(5*1000));
	ret = sgm41542S_hw_init(sgm);
	if (ret) {
		dev_err(dev, "Cannot initialize the chip.\n");
		goto error_out;
	}
	ret = sgm41542S_init_vote(sgm);
	if (ret) {
		dev_err(dev, "Cannot init the voter.\n");
		goto error_out;
	}

	sgm41542S_vbus_regulator_register(sgm);

	/* OTG setting 5V/1.2A */
	ret = sgm41542S_set_otg_voltage(s_chg_dev, 5000000);
	if (ret) {
		dev_err(sgm->dev, "set OTG voltage error!\n");
		return ret;
	}

	ret = sgm41542S_set_otg_current(s_chg_dev, 1200000);
	if (ret) {
		dev_err(sgm->dev, "set OTG current error!\n");
		return ret;
	}

	sgm->sgm_monitor_wq = alloc_ordered_workqueue("%s",
			WQ_MEM_RECLAIM | WQ_FREEZABLE, "sgm-monitor-wq");

	INIT_DELAYED_WORK(&sgm->sgm_delay_work, sgm_charger_work);
    sgm41542S_create_device_node(sgm->dev);

	schedule_delayed_work(&sgm->force_dpdm, msecs_to_jiffies(1000));
	sgm->curr_in_limit = DEFAULT_INPUT_CURRENT;
	
	pr_err("%s success!!!\n",__func__);
	return ret;
error_out:
	pr_err("failed to  init sgm41542s_probe\n");
	return 0;
}

EXPORT_SYMBOL_GPL(sgm41542S_probe);

static int sgm41542S_charger_remove(struct i2c_client *client)
{
    struct sgm41542S_device *sgm = i2c_get_clientdata(client);

    //cancel_delayed_work_sync(sgm->sgm_monitor_wq);

    regulator_unregister(sgm->otg_rdev);

    power_supply_unregister(sgm->charger); 
	
	mutex_destroy(&sgm->lock);       

    return 0;
}

static void sgm41542S_charger_shutdown(struct i2c_client *client)
{
    int ret = 0;
	
	struct sgm41542S_device *sgm = i2c_get_clientdata(client);
    ret = sgm41542S_disable_charger(sgm);
    if (ret) {
        pr_err("Failed to disable charger, ret = %d\n", ret);
    }
    pr_info("sgm41542S_charger_shutdown\n");
}

static const struct i2c_device_id sgm41542S_i2c_ids[] = {
	{ "sgm41542S", 0 },	
	{},
};
MODULE_DEVICE_TABLE(i2c, sgm41542S_i2c_ids);

static const struct of_device_id sgm41542S_of_match[] = {
	{ .compatible = "sgm41542S", },	
	{ },
};
MODULE_DEVICE_TABLE(of, sgm41542S_of_match);

static struct i2c_driver sgm41542S_driver = {
	.driver = {
		.name = "sgm41542S-charger",
		.of_match_table = sgm41542S_of_match,		
	},
	.probe = sgm41542S_probe,
	.remove = sgm41542S_charger_remove,
	.shutdown = sgm41542S_charger_shutdown,
	.id_table = sgm41542S_i2c_ids,
};
module_i2c_driver(sgm41542S_driver);

MODULE_AUTHOR("<hongqiang_qin@sg-micro.com>");
MODULE_DESCRIPTION("sgm41542S charger driver");
MODULE_LICENSE("GPL v2");

