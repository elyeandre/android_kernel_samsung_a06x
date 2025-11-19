/*
 * UPM6922 battery charging driver
 *
 * Copyright (C) 2023 Unisemipower
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#define pr_fmt(fmt) "[upm6922-charger]:%s:" fmt, __func__

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/err.h>
#include <linux/bitops.h>
#include <linux/math64.h>

#include <linux/extcon-provider.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <dt-bindings/iio/qti_power_supply_iio.h>
#include "charger_class.h"
#include "mtk_charger.h"
#include "mtk_battery.h"
#include "upm6922_iio.h"
#include "upm6922_charger.h"
#include <linux/iio/consumer.h>
#include <linux/phy/phy.h>
#include <../../misc/mediatek/typec/tcpc/inc/tcpm.h>
#define PHY_MODE_BC11_SET 1
#define PHY_MODE_BC11_CLR 2
struct upm6922 {
	struct power_supply	*usb_psy;
	int pd_active;
    struct device *dev;
    struct i2c_client *client;

    int part_no;
    int revision;

    const char *chg_dev_name;

    int irq;
    u32 intr_gpio;

    struct mutex i2c_rw_lock;
    struct mutex lock;

    bool power_good;
    bool vbus_attach;
    u8 chrg_stat;
    u8 vbus_stat;
    u8 chrg_type;

    bool force_dpdm;
    bool chg_config;
    int typec_attached;
    struct upm6922_platform_data *upd;
    bool en_auto_bc12;
    bool en_adc_continuous;
    bool entry_shipmode;

    int pre_charging_state;
    int cur_charging_state;
    int chrg_psy_stat;
    struct charger_device *chg_dev;

    struct delayed_work psy_dwork;
    struct delayed_work force_detect_dwork;
    struct delayed_work charge_detect_delayed_work;
    struct delayed_work monitor_dwork;
    struct wakeup_source *monitor_ws;
    struct delayed_work hvdcp_dwork;
    struct delayed_work prob_dwork;
    struct delayed_work td_dwork;
    int psy_usb_type;
    bool is_vindpm;
    int retry_num;

    struct power_supply_desc this_psy_desc;
    struct power_supply *this_psy;

    struct regulator_dev *otg_rdev;
    struct power_supply *inform_psy;
	
	struct iio_dev  		*indio_dev;
	struct iio_chan_spec	*iio_chan;
	struct iio_channel		*int_iio_chans;
	struct iio_channel		**ds_ext_iio_chans;
	struct iio_channel		**fg_ext_iio_chans;
	struct iio_channel		**nopmi_chg_ext_iio_chans;
	int charge_afc;
	bool is_disble_afc;
	bool is_disble_afc_backup;
	enum power_supply_type chg_type;
	struct delayed_work disable_afc_dwork;
	struct delayed_work disable_pdo_dwork;
	struct delayed_work tcpc_dwork;
	struct tcpc_device *tcpc;
	struct notifier_block tcp_nb;
	struct votable			*fcc_votable;
	struct votable			*fv_votable;
	struct votable			*usb_icl_votable;
	struct votable			*chg_dis_votable;
	struct delayed_work hvdcp_qc20_dwork;
	bool vbus_insert_flag;
	int poweron_fcc_ma;
	struct workqueue_struct *wq;
	bool bc12_done;
};
extern int afc_set_voltage_workq(unsigned int afc_code);
extern int afc_cancel_voltage_workq(void);
struct upm6922_platform_data {
    int ilim;
    int ichg;
    int iprechg;
    int iterm;
    int cv;
    int vlim;
    int boostv;
    int boosti;
    int vac_ovp;
    int statctrl;
};
static int upm6922_set_input_current_limit(struct upm6922 *upm, int ma);
static int upm6922_set_chargecurrent(struct upm6922 *upm, int ma);
static int upm6922_read_byte(struct upm6922 *upm, u8 reg, u8 *data);
static int upm6922_set_term_current(struct upm6922 *upm, int ma);
static int __maybe_unused upm6922_disable_charger(struct upm6922 *upm);
static int __maybe_unused upm6922_enable_charger(struct upm6922 *upm);
static int upm6922_set_chargevolt(struct upm6922 *upm, int mv);
void upm6922_dump_regs(struct upm6922 *upm);
static int upm6922_get_pd_active(struct upm6922 *upm)
{
	int pd_active = 0;
	int rc = 0;

    	if (!upm->tcpc)
        	upm->tcpc = tcpc_dev_get_by_name("type_c_port0");

	if (!upm->tcpc)
		pr_err("tcpc_dev is null\n");
	else {
		rc = tcpm_get_pd_connect_type(upm->tcpc);
		//pr_err("%s: pd is %d\n", __func__, rc);
		if ((rc == PD_CONNECT_PE_READY_SNK_APDO) || (rc == PD_CONNECT_PE_READY_SNK_PD30)
			|| (rc == PD_CONNECT_PE_READY_SNK)) {
			pd_active = 1;
		} else {
			pd_active = 0;
		}
	}


	if (upm->pd_active != pd_active) {
		upm->pd_active = pd_active;
	}
	return pd_active;
}

static int upm6922_get_pd_pdo_type(struct upm6922 *upm)
{
	int pd_pdo_active = 0;
	int rc = 0;

    	if (!upm->tcpc)
        	upm->tcpc = tcpc_dev_get_by_name("type_c_port0");

	if (!upm->tcpc)
		pr_err("tcpc_dev is null\n");
	else {
		rc = tcpm_get_pd_connect_type(upm->tcpc);
		//pr_err("%s: pd is %d\n", __func__, rc);
		if (rc == PD_CONNECT_PE_READY_SNK_APDO) {
			pd_pdo_active = 1;
		} else {
			pd_pdo_active = 0;
		}
	}

	return pd_pdo_active;
}

static void upm6922_disable_pdo_dwork(struct work_struct *work)
{
	int pdo_icl_current = 0;
	struct upm6922 *upm = container_of(
		work, struct upm6922, disable_pdo_dwork.work);

	if (upm->is_disble_afc) {
		pdo_icl_current = PD_ICL_CURR_MAX;
	} else {
		pdo_icl_current = DCP_ICL_CURR_MAX;
	}

	vote(upm->usb_icl_votable, DISABLE_AFC_VOTER, true, pdo_icl_current);

}

static void upm6922_disable_pdo(struct upm6922 *upm)
{
	vote(upm->usb_icl_votable, DISABLE_AFC_VOTER, true, 500);

	cancel_delayed_work_sync(&upm->disable_pdo_dwork);
	schedule_delayed_work(&upm->disable_pdo_dwork, msecs_to_jiffies(500));
}
static int upm6922_get_pdo_active(struct upm6922 *upm)
{
	int pdo_active = 0;
	int rc = 0;

    	if (!upm->tcpc)
        	upm->tcpc = tcpc_dev_get_by_name("type_c_port0");
	
	if (!upm->tcpc)
		pr_err("tcpc_dev is null\n");
	else {
		rc = tcpm_get_pd_connect_type(upm->tcpc);
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

static void upm6922_disable_afc_dwork(struct work_struct *work)
{
	unsigned int afc_code = AFC_QUICK_CHARGE_POWER_CODE;
	int afc_icl_current = 0;
	int afc_icharge_current = 0;
	int ret = 0;
	struct upm6922 *upm = container_of(
		work, struct upm6922, disable_afc_dwork.work);

	if (upm->is_disble_afc) {
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
		vote(upm->usb_icl_votable, DISABLE_AFC_VOTER, true, afc_icl_current);
		vote(upm->fcc_votable, DISABLE_AFC_VOTER, true, afc_icharge_current);
	}
}
static void upm6922_disable_afc(struct upm6922 *upm)
{
	vote(upm->usb_icl_votable, DISABLE_AFC_VOTER, true, 500);

	cancel_delayed_work_sync(&upm->disable_afc_dwork);
	schedule_delayed_work(&upm->disable_afc_dwork, msecs_to_jiffies(500));
}
struct upm6922 *__upm;
enum attach_type {
	ATTACH_TYPE_NONE = 0,
	ATTACH_TYPE_PWR_RDY,
	ATTACH_TYPE_TYPEC,
	ATTACH_TYPE_PD,
	ATTACH_TYPE_PD_SDP,
	ATTACH_TYPE_PD_DCP,
	ATTACH_TYPE_PD_NONSTD,
};

static struct charger_device *s_chg_dev_otg = NULL;
int upm6922_set_iio_channel(struct upm6922 *upm, enum upm6922_iio_type type, int channel, int val);
static int __upm6922_read_reg(struct upm6922 *upm, u8 reg, u8 *data)
{
    s32 ret;

    ret = i2c_smbus_read_byte_data(upm->client, reg);
    if (ret < 0) {
        pr_err("i2c read fail: can't read from reg 0x%02X\n", reg);
        return ret;
    }

    *data = (u8) ret;

    return 0;
}
enum charg_stat upm6922_get_charging_status(struct upm6922 *upm)
{
        enum charg_stat status = CHRG_STAT_NOT_CHARGING;
        int ret = 0;
        u8 reg_val = 0;

        ret = upm6922_read_byte(upm, UPM6922_REG_08, &reg_val);
        if (ret) {
                pr_err("read UPM6922_REG_08 failed, ret:%d\n", ret);
                return ret;
        }

        status = (reg_val & REG08_CHRG_STAT_MASK) >> REG08_CHRG_STAT_SHIFT;

        return status;
}
EXPORT_SYMBOL_GPL(upm6922_get_charging_status);
static int __upm6922_write_reg(struct upm6922 *upm, int reg, u8 val)
{
    s32 ret;

    ret = i2c_smbus_write_byte_data(upm->client, reg, val);
    if (ret < 0) {
        pr_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n",
               val, reg, ret);
        return ret;
    }
    return 0;
}

static int upm6922_read_byte(struct upm6922 *upm, u8 reg, u8 *data)
{
    int ret;

    mutex_lock(&upm->i2c_rw_lock);
    ret = __upm6922_read_reg(upm, reg, data);
    mutex_unlock(&upm->i2c_rw_lock);

    return ret;
}

static int upm6922_write_byte(struct upm6922 *upm, u8 reg, u8 data)
{
    int ret;

    mutex_lock(&upm->i2c_rw_lock);
    ret = __upm6922_write_reg(upm, reg, data);
    mutex_unlock(&upm->i2c_rw_lock);

    if (ret)
        pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);

    return ret;
}

static int upm6922_update_bits(struct upm6922 *upm, u8 reg, u8 mask, u8 data)
{
    int ret;
    u8 tmp;

    mutex_lock(&upm->i2c_rw_lock);
    ret = __upm6922_read_reg(upm, reg, &tmp);
    if (ret) {
        pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
        goto out;
    }

    tmp &= ~mask;
    tmp |= data & mask;

    ret = __upm6922_write_reg(upm, reg, tmp);
    if (ret) {
        pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
    }

out:
    mutex_unlock(&upm->i2c_rw_lock);
    return ret;
}

int __maybe_unused upm6922_enter_hiz_mode(struct upm6922 *upm)
{
    u8 val = REG00_HIZ_ENABLE << REG00_ENHIZ_SHIFT;

    pr_err("hiz enter\n");
    return upm6922_update_bits(upm, UPM6922_REG_00, REG00_ENHIZ_MASK, val);
}
void upm6922_enter_hiz(void)
{
    upm6922_enter_hiz_mode(__upm);
    return ;
}
EXPORT_SYMBOL(upm6922_enter_hiz);




int __maybe_unused upm6922_exit_hiz_mode(struct upm6922 *upm)
{
    u8 val = REG00_HIZ_DISABLE << REG00_ENHIZ_SHIFT;

    pr_err("hiz exit\n");
    return upm6922_update_bits(upm, UPM6922_REG_00, REG00_ENHIZ_MASK, val);
}


void upm6922_exit_hiz(void)
{
    upm6922_exit_hiz_mode(__upm);
    return;
}
EXPORT_SYMBOL(upm6922_exit_hiz);

static int __maybe_unused upm6922_get_hiz_mode(struct upm6922 *upm, bool *en)
{
    int ret = 0;
    u8 val = 0;

    ret = upm6922_read_byte(upm, UPM6922_REG_00, &val);
    if (ret < 0) {
        pr_info("read UPM6922_REG_00 fail\n");
        return ret;
    }

    *en = (val & REG00_ENHIZ_MASK) ? true : false;

    return ret;
}
static int upm6922_isenable_hiz(struct charger_device *chg_dev, bool en)
{
    if (en)
        upm6922_enter_hiz_mode(__upm);
    else
        upm6922_exit_hiz_mode(__upm);

    return 0;
}

static int upm6922_set_input_current_limit(struct upm6922 *upm, int ma)
{
    u8 val;

    pr_info("ma:%d\n", ma);

    if (ma < REG00_IINLIM_MIN) {
        ma = REG00_IINLIM_MIN;
    } else if (ma > REG00_IINLIM_MAX) {
        ma = REG00_IINLIM_MAX;
    }

    val = (ma - REG00_IINLIM_BASE) / REG00_IINLIM_LSB;
    return upm6922_update_bits(upm, UPM6922_REG_00, REG00_IINLIM_MASK,
                   val << REG00_IINLIM_SHIFT);
}

static void upm6922_hvdcp_dwork(struct work_struct *work)
{
	int ret;

	struct upm6922 *upm = container_of(
		work, struct upm6922, hvdcp_dwork.work);

  	unsigned int afc_code = AFC_QUICK_CHARGE_POWER_CODE;
  	int afc_icl_current = 0;

  	pr_err("is_disble_afc=%d\n", upm->is_disble_afc);
  	if (upm->is_disble_afc) {
  		afc_code = AFC_COMMIN_CHARGE_POWER_CODE;
  		afc_icl_current = AFC_COMMON_ICL_CURR_MAX;
  	} else {
  		afc_code = AFC_QUICK_CHARGE_POWER_CODE;
  		afc_icl_current = AFC_ICL_CURR_MAX;
  	}

  	ret = afc_set_voltage_workq(afc_code);
	if (!ret) {
        upm->charge_afc = 1;

        if(upm->is_disble_afc)
            vote(upm->usb_icl_votable, DISABLE_AFC_VOTER, true, AFC_COMMON_ICL_CURR_MAX);

		pr_info("set afc adapter iindpm to 1700mA\n");
		vote(upm->usb_icl_votable, CHARGER_TYPE_VOTER, true, 1670);
	} else {
        upm->charge_afc = 0;

        if(upm6922_get_pd_active(upm)) {
            if (upm->is_disble_afc)
                vote(upm->usb_icl_votable, DISABLE_AFC_VOTER, true, PD_ICL_CURR_MAX);

            pr_err("afc communication failed, restore adapter iindpm to 1700mA\n");
            vote(upm->usb_icl_votable, CHARGER_TYPE_VOTER, true, 1670);
        } else {
            pr_err("afc communication failed, restore adapter iindpm to 2000mA\n");
            vote(upm->usb_icl_votable, CHARGER_TYPE_VOTER, true, 2000);
        }
	}

}

static void upm6922_hvdcp_dwork_cancel(struct upm6922 *upm)
{
    afc_cancel_voltage_workq();
    cancel_delayed_work(&upm->hvdcp_dwork);
}

static int fcc_vote_callback(struct votable *votable, void *data,
			int fcc_ua, const char *client)
{
	struct upm6922 *upm	= data;
	int rc = 0;
	if (fcc_ua < 0) {
		pr_err("fcc_ua: %d < 0, ERROR!\n", fcc_ua);
		return 0;
	}

	if (fcc_ua > UPM6922_MAX_FCC)
		fcc_ua = UPM6922_MAX_FCC;

	rc = upm6922_set_chargecurrent(upm, fcc_ua);
	if (rc < 0) {
		pr_err("failed to set charge current\n");
		return rc;
	}

	//pr_err("ch_log  fcc[%d]\n",fcc_ua);
	upm6922_dump_regs(upm);
	return 0;
}

static int chg_dis_vote_callback(struct votable *votable, void *data,
			int disable, const char *client)
{
	struct upm6922 *upm = data;
	int rc = 0;

	if (disable) {
		rc = upm6922_disable_charger(upm);
	} else {
		rc = upm6922_enable_charger(upm);
	}

	if (rc < 0) {
		pr_err("failed to disabl:e%d\n", disable);
		return rc;
	}

	//pr_info("disable:%d\n", disable);
	upm6922_dump_regs(upm);
	return 0;
}

static int fv_vote_callback(struct votable *votable, void *data,
			int fv_mv, const char *client)
{
	struct upm6922 *upm = data;
	struct power_supply *cp = NULL;
	int pd_active = 0;
	enum power_supply_type chg_type = 0;
	union power_supply_propval prop = {0, };
	int rc = 0, ret = 0;
	u8 val;

	/*if(fv_mv != 4200) //high temperature
		fv_mv = sm5602_CV;
	else
		pr_err("high temperature,fv = %d\n",fv_mv);*/
    pr_err("fv_mv: %d \n", fv_mv);
	//pr_err("fv_vote_callback start");
	if (fv_mv < 0) {
		pr_err("fv_mv: %d < 0, ERROR!\n", fv_mv);
		return 0;
	}
	pd_active = upm6922_get_pd_active(upm);
	cp = power_supply_get_by_name("charger_standalone");
	if (cp)
	{
		ret = power_supply_get_property(cp, POWER_SUPPLY_PROP_STATUS, &prop);
		if (!ret && prop.intval == POWER_SUPPLY_STATUS_CHARGING)
			chg_type = POWER_SUPPLY_TYPE_USB_PD;
	}

	if(fv_mv == FLOAT_VOLTAGE_DEFAULT_UPM6922)
	{
		if ((pd_active == 1) || (chg_type == POWER_SUPPLY_TYPE_USB_PD) || (upm->charge_afc == 1))
		{
			upm6922_update_bits(upm, UPM6922_REG_04, REG04_VREG_MASK,
				   FLOAT_VOLTAGE_DEFAULT_UPM6922_PD_AFC_VAL << REG04_VREG_SHIFT);
			pr_err("is pd or afc pd_active = %d,charge_afc = %d\n", upm->pd_active, upm->charge_afc);
		} else {
			upm6922_update_bits(upm, UPM6922_REG_04, REG04_VREG_MASK,
				   FLOAT_VOLTAGE_DEFAULT_UPM6922_VAL << REG04_VREG_SHIFT);
			pr_err("not is pd or afc pd_active = %d,charge_afc = %d\n", upm->pd_active, upm->charge_afc);
		}
	} else{
		rc = upm6922_set_chargevolt(upm, fv_mv);
		if (rc < 0) {
			pr_err("failed to set chargevoltage\n");
			return rc;
		}
	}
	rc = upm6922_read_byte(upm, UPM6922_REG_04, &val);
    if (rc) {
            pr_err("read UPM6922_REG_04 failed, ret:%d\n", rc);
            return rc;
    }
	pr_err("fv:%d,reg = %x\n", fv_mv, val);
	upm6922_dump_regs(upm);
	return 0;
}

static int usb_icl_vote_callback(struct votable *votable, void *data,
			int icl_ma, const char *client)
{
	struct upm6922 *upm = data;
	int rc;

	if (icl_ma < 0){
		pr_err("icl_ma: %d < 0, ERROR!\n", icl_ma);
		return 0;
	}

	if (icl_ma > UPM6922_MAX_ICL)
		icl_ma = UPM6922_MAX_ICL;

	rc = upm6922_set_input_current_limit(upm, icl_ma);
	if (rc < 0) {
		pr_err("failed to set input current limit\n");
		return rc;
	}
	pr_err("icl_ma[%d]\n",icl_ma);
	upm6922_dump_regs(upm);
	return 0;
}

static int upm6922_set_stat_ctrl(struct upm6922 *upm, int ctrl)
{
    u8 val;

    pr_info("ctrl:%d\n", ctrl);
    val = ctrl;

    return upm6922_update_bits(upm, UPM6922_REG_00, REG00_STAT_CTRL_MASK,
                   val << REG00_STAT_CTRL_SHIFT);
}



static int upm6922_chg_attach_pre_process(struct charger_device *chg_dev,
                    int attach)
{
    struct upm6922 *upm = charger_get_data(chg_dev);
    upm->typec_attached = attach;
    mutex_lock(&upm->lock);

    switch(attach) {
        case ATTACH_TYPE_TYPEC:
            schedule_delayed_work(&upm->force_detect_dwork, msecs_to_jiffies(150));
            break;
        case ATTACH_TYPE_PD_SDP:
            upm->this_psy_desc.type = POWER_SUPPLY_TYPE_USB;
            upm->psy_usb_type = POWER_SUPPLY_USB_TYPE_SDP;
            break;
        case ATTACH_TYPE_PD_DCP:
            upm->this_psy_desc.type = POWER_SUPPLY_TYPE_USB_DCP;
            upm->psy_usb_type = POWER_SUPPLY_USB_TYPE_DCP;
            break;
        case ATTACH_TYPE_PD_NONSTD:
            upm->this_psy_desc.type = POWER_SUPPLY_TYPE_USB;
            upm->psy_usb_type = POWER_SUPPLY_USB_TYPE_DCP;
            break;
        case ATTACH_TYPE_NONE:
            upm->this_psy_desc.type = POWER_SUPPLY_TYPE_UNKNOWN;
            upm->psy_usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
            break;
        default:
            pr_err("using tradtional bc12 flow!\n");
            break;
    }

    power_supply_changed(upm->this_psy);

    pr_err("type(%d %d), attach:%d\n",
        upm->this_psy_desc.type, upm->psy_usb_type, attach);
    mutex_unlock(&upm->lock);

    return 0;
}

static int __maybe_unused __upm6922_get_input_current_limit(struct upm6922 *upm, int *ma)
{
    u8 val = 0;
    int ret;
    int iindpm;

    ret = upm6922_read_byte(upm, UPM6922_REG_00, &val);
    if (ret < 0){
        pr_err("read UPM6922_REG_00 fail, ret:%d\n", ret);
        return ret;
    }

    iindpm = (val & REG00_IINLIM_MASK) >> REG00_IINLIM_SHIFT;
    iindpm = iindpm * REG00_IINLIM_LSB + REG00_IINLIM_BASE;
    *ma = iindpm;

    return ret;
}

static int __maybe_unused upm6922_reset_watchdog_timer(struct upm6922 *upm)
{
    u8 val = REG01_WDT_RESET << REG01_WDT_RESET_SHIFT;

    pr_info("reset\n");
    return upm6922_update_bits(upm, UPM6922_REG_01, REG01_WDT_RESET_MASK,
                   val);
}

static int __maybe_unused upm6922_enable_otg(struct upm6922 *upm)
{
    u8 val = REG01_OTG_ENABLE << REG01_OTG_CONFIG_SHIFT;

    pr_info("enable otg\n");
    return upm6922_update_bits(upm, UPM6922_REG_01, REG01_OTG_CONFIG_MASK,
                   val);
}

static int __maybe_unused upm6922_disable_otg(struct upm6922 *upm)
{
    u8 val = REG01_OTG_DISABLE << REG01_OTG_CONFIG_SHIFT;

    pr_info("disable otg\n");
    return upm6922_update_bits(upm, UPM6922_REG_01, REG01_OTG_CONFIG_MASK,
                   val);
}

static int __maybe_unused upm6922_enable_charger(struct upm6922 *upm)
{
    int ret;
    u8 val = REG01_CHG_ENABLE << REG01_CHG_CONFIG_SHIFT;

    pr_info("enable charge\n");
    ret = upm6922_update_bits(upm, UPM6922_REG_01,
                    REG01_CHG_CONFIG_MASK, val);

    return ret;
}

static int __maybe_unused upm6922_disable_charger(struct upm6922 *upm)
{
    int ret;
    u8 val = REG01_CHG_DISABLE << REG01_CHG_CONFIG_SHIFT;

    pr_info("disable charge\n");
    ret = upm6922_update_bits(upm, UPM6922_REG_01,
                    REG01_CHG_CONFIG_MASK, val);

    return ret;
}

static int upm6922_set_boost_current(struct upm6922 *upm, int ma)
{
    u8 val1 = 0;
    u8 val2 = 0;
    int ret = 0;

    pr_info("ma:%d\n", ma);

    if (ma >= 2000) {
        val2 = REG15_BOOST_LIM1_2A;
    } else if (ma >= 1200) {
        val1 = REG02_BOOST_LIM0_1P2A;
        val2 = REG15_BOOST_LIM1_DIS;
    } else {
        val1 = REG02_BOOST_LIM0_0P5A;
        val2 = REG15_BOOST_LIM1_DIS;
    }

    ret = upm6922_update_bits(upm, UPM6922_REG_02, REG02_BOOST_LIM0_MASK,
                   val1 << REG02_BOOST_LIM0_SHIFT);
    if (ret < 0) {
        pr_err("write UPM6922_REG_02 fail, ret:%d\n", ret);
        return ret;
    }

    ret = upm6922_update_bits(upm, UPM6922_REG_15, REG15_BOOST_LIM1_MASK,
                   val2 << REG15_BOOST_LIM1_SHIFT);
    if (ret < 0) {
        pr_err("write UPM6922_REG_02 fail, ret:%d\n", ret);
    }

    return ret;
}

static int upm6922_set_chargecurrent(struct upm6922 *upm, int ma)
{
    u8 ichg;

    pr_info("ma:%d\n", ma);

    if (ma < REG02_ICHG_MIN) {
        ma = REG02_ICHG_MIN;
    } else if (ma > REG02_ICHG_MAX) {
        ma = REG02_ICHG_MAX;
    }

    ichg = (ma - REG02_ICHG_BASE) / REG02_ICHG_LSB;
    return upm6922_update_bits(upm, UPM6922_REG_02, REG02_ICHG_MASK,
                   ichg << REG02_ICHG_SHIFT);
}

static int __maybe_unused upm6922_get_chargecurrent(struct upm6922 *upm, int *ma)
{
    u8 val = 0;
    int ret = 0;

    ret = upm6922_read_byte(upm, UPM6922_REG_02, &val);
    if (ret < 0){
        pr_err("read UPM6922_REG_02 fail, ret:%d\n", ret);
        return ret;
    }

    *ma = ((val & REG02_ICHG_MASK) >> REG02_ICHG_SHIFT) * REG02_ICHG_LSB;

    return ret;
}

static int upm6922_set_prechg_current(struct upm6922 *upm, int ma)
{
    u8 iprechg;

    pr_info("ma:%d\n", ma);

    if (ma < REG03_IPRECHG_MIN) {
        ma = REG03_IPRECHG_MIN;
    } else if (ma < REG03_IPRECHG_MAX) {
        ma = REG03_IPRECHG_MAX;
    }

    iprechg = (ma - REG03_IPRECHG_BASE) / REG03_IPRECHG_LSB;

    return upm6922_update_bits(upm, UPM6922_REG_03, REG03_IPRECHG_MASK,
                   iprechg << REG03_IPRECHG_SHIFT);
}

static int upm6922_set_term_current(struct upm6922 *upm, int ma)
{
    u8 iterm;

    pr_info("ma:%d\n", ma);

    if (ma < REG03_ITERM_MIN) {
        ma = REG03_ITERM_MIN;
    } else if (ma > REG03_ITERM_MAX) {
        ma = REG03_ITERM_MAX;
    }

    iterm = (ma - REG03_ITERM_BASE) / REG03_ITERM_LSB;

    return upm6922_update_bits(upm, UPM6922_REG_03, REG03_ITERM_MASK,
                   iterm << REG03_ITERM_SHIFT);
}

static int upm6922_set_chargevolt(struct upm6922 *upm, int mv)
{
    u8 val1, val2;
    int ret = 0;

    pr_info("mv:%d\n", mv);

    if (mv < REG04_VREG_MIN) {
        mv = REG04_VREG_MIN;
    } else if (mv > REG04_VREG_MAX) {
        mv = REG04_VREG_MAX;
    }

    val1 = (mv - REG04_VREG_BASE) / REG04_VREG_LSB;
    ret = upm6922_update_bits(upm, UPM6922_REG_04, REG04_VREG_MASK,
                   val1 << REG04_VREG_SHIFT);
    if (ret < 0) {
        pr_err("write UPM6922_REG_04 fail, ret:%d\n", ret);
        return ret;
    }

    val2 = ((mv - REG04_VREG_BASE) % REG04_VREG_LSB) / REG0D_VREG_FT_LSB;
    ret = upm6922_update_bits(upm, UPM6922_REG_0D, REG0D_VREG_FT_MASK,
                   val2 << REG0D_VREG_FT_SHIFT);
    if (ret < 0) {
        pr_err("write UPM6922_REG_0D fail, ret:%d\n", ret);
    }

    return ret;
}

static int __maybe_unused upm6922_get_chargevolt(struct upm6922 *upm, int *mv)
{
    u8 val1, val2;
    int cv = 0;
    int ret = 0;

    ret = upm6922_read_byte(upm, UPM6922_REG_04, &val1);
    if (ret < 0){
        pr_err("read UPM6922_REG_04 fail, ret:%d\n", ret);
        return ret;
    }

    ret = upm6922_read_byte(upm, UPM6922_REG_0D, &val2);
    if (ret < 0){
        pr_err("read UPM6922_REG_0D fail, ret:%d\n", ret);
        return ret;
    }

    cv = ((UPM6922_REG_04 & REG04_VREG_MASK) >> REG04_VREG_SHIFT) *
                REG04_VREG_LSB + REG04_VREG_BASE;
    cv += ((val2 & REG0D_VREG_FT_MASK) >> REG0D_VREG_FT_SHIFT) * REG0D_VREG_FT_LSB;

    *mv = cv;

    return ret;
}

static int __maybe_unused upm6922_set_recharge_volt(struct upm6922 *upm, int mv)
{
    u8 val = 0;

    pr_info("mv:%d\n", mv);

    if (mv < 200) {
        val = REG04_VRECHG_100MV;
    } else {
        val = REG04_VRECHG_200MV;
    }

    return upm6922_update_bits(upm, UPM6922_REG_04, REG04_VRECHG_MASK,
                    val << REG04_VRECHG_SHIFT);
}

static int __maybe_unused upm6922_enable_term(struct upm6922 *upm, bool enable)
{
    u8 val;
    int ret;

    pr_info("enable:%d\n", enable);

    if (enable) {
        val = REG05_TERM_ENABLE << REG05_EN_TERM_SHIFT;
    } else {
        val = REG05_TERM_DISABLE << REG05_EN_TERM_SHIFT;
    }

    ret = upm6922_update_bits(upm, UPM6922_REG_05, REG05_EN_TERM_MASK, val);

    return ret;
}

static int __maybe_unused upm6922_set_watchdog_timer(struct upm6922 *upm, u8 seconds)
{
    u8 val = 0;

    pr_info("seconds:%d\n", seconds);

    if (seconds >= 160) {
        val = REG05_WDT_160S;
    } else if (seconds >= 80) {
        val = REG05_WDT_80S;
    } else if (seconds >= 40) {
        val = REG05_WDT_40S;
    } else {
        val = REG05_WDT_DISABLE;
    }

    return upm6922_update_bits(upm, UPM6922_REG_05, REG05_WDT_MASK,
                    val << REG05_WDT_SHIFT);
}

static int upm6922_disable_watchdog_timer(struct upm6922 *upm)
{
    u8 val = REG05_WDT_DISABLE << REG05_WDT_SHIFT;

    pr_info("disable wd\n");

    return upm6922_update_bits(upm, UPM6922_REG_05, REG05_WDT_MASK, val);
}

static int __maybe_unused upm6922_enable_safety_timer(struct upm6922 *upm)
{
    const u8 val = REG05_CHG_TIMER_ENABLE << REG05_EN_TIMER_SHIFT;

    pr_info("enable timer\n");

    return upm6922_update_bits(upm, UPM6922_REG_05, REG05_EN_TIMER_MASK,
                   val);
}

static int __maybe_unused upm6922_disable_safety_timer(struct upm6922 *upm)
{
    const u8 val = REG05_CHG_TIMER_DISABLE << REG05_EN_TIMER_SHIFT;

    pr_info("disable timer\n");

    return upm6922_update_bits(upm, UPM6922_REG_05, REG05_EN_TIMER_MASK,
                   val);
}

static int __maybe_unused upm6922_set_safety_timer(struct upm6922 *upm, u8 hours)
{
    u8 val;

    pr_info("hours:%d\n", hours);

    if (hours > 10) {
        val = REG05_CHG_TIMER_10HOURS;
    } else {
        val = REG05_CHG_TIMER_5HOURS;
    }

    return upm6922_update_bits(upm, UPM6922_REG_05, REG05_CHG_TIMER_MASK,
                   val << REG05_CHG_TIMER_SHIFT);
}

static int upm6922_set_acovp_threshold(struct upm6922 *upm, int mv)
{
    u8 val;

    pr_info("mv:%d\n", mv);

    if (mv >= 14000) {
        val = REG06_OVP_14V;
    } else if (mv >= 11000) {
        val = REG06_OVP_11V;
    } else if (mv >= 6500) {
        val = REG06_OVP_6P5V;
    } else {
        val = REG06_OVP_5P85V;
    }

    return upm6922_update_bits(upm, UPM6922_REG_06, REG06_OVP_MASK,
                   val << REG06_OVP_SHIFT);
}

static int upm6922_set_boost_voltage(struct upm6922 *upm, int mv)
{
    u8 val;

    pr_info("mv:%d\n", mv);

    if (mv >= 5300) {
        val = REG06_BOOSTV_5P3V;
    } else if (mv >= 5150) {
        val = REG06_BOOSTV_5P15V;
    } else if (mv >= 5000) {
        val = REG06_BOOSTV_5V;
    } else {
        val = REG06_BOOSTV_4P85V;
    }

    return upm6922_update_bits(upm, UPM6922_REG_06, REG06_BOOSTV_MASK,
                   val << REG06_BOOSTV_SHIFT);
}

static int upm6922_set_input_volt_limit(struct upm6922 *upm, int mv)
{
    u8 val1, val2;
    int ret = 0;

    pr_info("mv:%d\n", mv);

    if (mv < 3900) {
        val1 = 0;
        val2 = REG0D_VINDPM_OS_3P9V;
    } else if (mv <= 5400) {
        val1 = (mv - 3900) / REG06_VINDPM_LSB;
        val2 = REG0D_VINDPM_OS_3P9V;
    } else if (mv < 5900) {
        val1 = 0xf;
        val2 = REG0D_VINDPM_OS_3P9V;
    } else if (mv < 7500) {
        val1 = (mv - 5900) / REG06_VINDPM_LSB;
        val2 = REG0D_VINDPM_OS_5P9V;
    } else if (mv <= 9000) {
        val1 = (mv - 7500) / REG06_VINDPM_LSB;
        val2 = REG0D_VINDPM_OS_7P5V;
    } else if (mv < 10500) {
        val1 = 0xf;
        val2 = REG0D_VINDPM_OS_7P5V;
    } else if (mv <= 12000) {
        val1 = (mv - 10500) / REG06_VINDPM_LSB;
        val2 = REG0D_VINDPM_OS_10P5V;
    } else {
        val1 = 0xf;
        val2 = REG0D_VINDPM_OS_10P5V;
    }

    ret =  upm6922_update_bits(upm, UPM6922_REG_06, REG06_VINDPM_MASK,
                   val1 << REG06_VINDPM_SHIFT);
    if (ret < 0) {
        pr_err("write UPM6922_REG_06 fail, ret:%d\n", ret);
        return ret;
    }

    ret =  upm6922_update_bits(upm, UPM6922_REG_0D, REG0D_VINDPM_OS_MASK,
                   val2 << REG0D_VINDPM_OS_SHIFT);
    if (ret < 0) {
        pr_err("write UPM6922_REG_0D fail, ret:%d\n", ret);
    }

    return ret;
}


enum upm6922_usbsw {
	USBSW_CHG,
	USBSW_USB,
};

static int upm6922_chg_set_usbsw(struct  upm6922 *upm,
                enum upm6922_usbsw usbsw)
{
    struct phy *phy = NULL;
    int ret = 0;
    int mode = (usbsw == USBSW_CHG) ? PHY_MODE_BC11_SET :
                           PHY_MODE_BC11_CLR;

    pr_err("usbsw=%d\n", usbsw);
    phy = phy_get(upm->dev, "usb2-phy");
    if (IS_ERR_OR_NULL(phy)) {
        pr_err("failed to get usb2-phy\n");
        return -ENODEV;
    }
    ret = phy_set_mode_ext(phy, PHY_MODE_USB_DEVICE, mode);
    if (ret)
        pr_err("failed to set phy ext mode\n");
    phy_put(upm->dev, phy);

    return ret;
}

static int upm6922_force_dpdm(struct upm6922 *upm)
{
    int ret = 0;
    const u8 val = REG07_FORCE_DPDM << REG07_FORCE_DPDM_SHIFT;

    pr_info("iindet en\n");

    ret = upm6922_update_bits(upm, UPM6922_REG_07,
            REG07_FORCE_DPDM_MASK, val);

    return ret;
}

static int __maybe_unused upm6922_enable_batfet(struct upm6922 *upm)
{
    const u8 val = REG07_BATFET_ON << REG07_BATFET_DIS_SHIFT;

    pr_info("batfet turn on\n");

    return upm6922_update_bits(upm, UPM6922_REG_07, REG07_BATFET_DIS_MASK,
                   val);
}

static int __maybe_unused upm6922_disable_batfet(struct upm6922 *upm)
{
    const u8 val = REG07_BATFET_OFF << REG07_BATFET_DIS_SHIFT;

    pr_info("batfet turn off\n");

    return upm6922_update_bits(upm, UPM6922_REG_07, REG07_BATFET_DIS_MASK,
                   val);
}

static int __maybe_unused upm6922_en_batfet_delay(struct upm6922 *upm, bool en)
{
    u8 val;

    pr_info("en:%d\n", en);

    if (en) {
        val = REG07_BATFET_DLY_10S;
    } else {
        val = REG07_BATFET_DLY_0S;
    }

    return upm6922_update_bits(upm, UPM6922_REG_07, REG07_BATFET_DLY_MASK,
                   val << REG07_BATFET_DLY_SHIFT);
}

static int __maybe_unused upm6922_batfet_rst_en(struct upm6922 *upm, bool en)
{
    u8 val = 0;

    pr_info("en:%d\n", en);

    if (en) {
        val = REG07_BATFET_RST_ENABLE << REG07_BATFET_RST_EN_SHIFT;
    } else {
        val = REG07_BATFET_RST_DISABLE << REG07_BATFET_RST_EN_SHIFT;
    }

    return upm6922_update_bits(upm, UPM6922_REG_07, REG07_BATFET_RST_EN_MASK,
                   val);
}

static int __maybe_unused upm6922_vdpm_bat_track(struct upm6922 *upm, int mv)
{
    u8 val = 0;

    pr_info("mv:%d\n", mv);

    if (mv >= 300) {
        val = REG07_VDPM_BAT_TRACK_300MV;
    } else if (mv >= 250) {
        val = REG07_VDPM_BAT_TRACK_250MV;
    } else if (mv >= 200) {
        val = REG07_VDPM_BAT_TRACK_200MV;
    } else {
        val = REG07_VDPM_BAT_TRACK_DISABLE;
    }

    return upm6922_update_bits(upm, UPM6922_REG_07, REG07_VDPM_BAT_TRACK_MASK,
                   val << REG07_VDPM_BAT_TRACK_SHIFT);
}

static void upm6922_force_detection_dwork_handler(struct work_struct *work)
{
    int ret = 0;
    struct delayed_work *force_detect_dwork = NULL;
    struct upm6922 *upm = NULL;

    force_detect_dwork = container_of(work, struct delayed_work, work);
    if (force_detect_dwork == NULL) {
        pr_err("Cann't get force_detect_dwork\n");
        return;
    }

    upm = container_of(force_detect_dwork, struct upm6922, force_detect_dwork);
    if (upm == NULL) {
        pr_err("Cann't get upm6922_device\n");
        return;
    }

    if ((upm->vbus_attach == false) && (upm->typec_attached == ATTACH_TYPE_TYPEC)) {
        pr_info("a good VBUS is not attached, wait~~~\n");
        schedule_delayed_work(&upm->force_detect_dwork, msecs_to_jiffies(10));
        return;
    } else if (upm->vbus_attach == false) {
        pr_info("TypeC has been plug out\n");
        return;
    }

    msleep(100);
    upm6922_chg_set_usbsw(upm, USBSW_CHG);
    msleep(100);
    ret = upm6922_force_dpdm(upm);
    upm->force_dpdm = true;
    if (ret) {
        pr_err("force dpdm failed(%d)\n", ret);
        return;
    }
}

static int upm6922_get_vbus_stat(struct upm6922 *upm)
{
    u8 data=0;

    upm6922_read_byte(upm, UPM6922_REG_0A, &data);

    return  !!(data & REG0A_VBUS_GD_MASK);
}

static int upm6922_get_charging_stat(struct upm6922 *upm, int *stat)
{
    int ret = 0;
    u8 reg_val1 = 0, reg_val2 = 0;
    u8 chrg_stat = 0;

    ret = upm6922_read_byte(upm, UPM6922_REG_08, &reg_val1);
    if (ret) {
        pr_err("read UPM6922_REG_08 fail, ret:%d\n", ret);
        return ret;
    }

    chrg_stat = (reg_val1 & REG08_CHRG_STAT_MASK) >> REG08_CHRG_STAT_SHIFT;
    upm->power_good = !!(reg_val1 & REG08_PG_STAT_MASK);
	upm->cur_charging_state = chrg_stat;
    switch (chrg_stat) {
        case REG08_CHRG_STAT_CHGDONE:
            *stat = POWER_SUPPLY_STATUS_FULL;
            break;

        case REG08_CHRG_STAT_PRECHG:
        case REG08_CHRG_STAT_FASTCHG:
            *stat = POWER_SUPPLY_STATUS_CHARGING;
            break;

        case REG08_CHRG_STAT_IDLE:
            ret = upm6922_read_byte(upm, UPM6922_REG_0A, &reg_val2);
            if (ret) {
                pr_err("read UPM6922_REG_0A fail, ret:%d\n", ret);
            }

            if (upm->is_vindpm && upm->power_good) {
                 *stat = POWER_SUPPLY_STATUS_CHARGING;
            } else if (reg_val2 & REG0A_VBUS_GD_MASK) {
                *stat = POWER_SUPPLY_STATUS_NOT_CHARGING;
            } else {
                *stat = POWER_SUPPLY_STATUS_DISCHARGING;
            }
            break;

        default:
            *stat = POWER_SUPPLY_STATUS_UNKNOWN;
            break;
    }
    pr_err("*stat=%d\n", *stat);
	if (upm->pre_charging_state != upm->cur_charging_state) {
		upm->pre_charging_state = upm->cur_charging_state;
		//power_supply_changed(upm->this_psy);
}

    return ret;
}

static int upm6922_enable_vindpm_int(struct upm6922 *upm, bool en)
{
    u8 val = 0;

    pr_info("en:%d\n", en);

    if (en) {
        val = REG0A_VINDPM_INT_ENABLE;
    } else {
        val = REG0A_VINDPM_INT_DISABLE;
    }

    return upm6922_update_bits(upm, UPM6922_REG_0A, REG0A_VINDPM_INT_MASK,
                   val << REG0A_VINDPM_INT_SHIFT);
}

static int upm6922_enable_iindpm_int(struct upm6922 *upm, bool en)
{
    u8 val = 0;

    pr_info("en:%d\n", en);

    if (en) {
        val = REG0A_IINDPM_INT_ENABLE;
    } else {
        val = REG0A_IINDPM_INT_DISABLE;
    }

    return upm6922_update_bits(upm, UPM6922_REG_0A, REG0A_IINDPM_INT_MASK,
                   val << REG0A_IINDPM_INT_SHIFT);
}

static int __maybe_unused upm6922_reset_chip(struct upm6922 *upm)
{
    u8 val = REG0B_REG_RESET << REG0B_REG_RESET_SHIFT;

    pr_info("reset chip\n");

    return upm6922_update_bits(upm, UPM6922_REG_0B, REG0B_REG_RESET_MASK, val);

}

static int upm6922_detect_device(struct upm6922 *upm)
{
    int ret;
    u8 data;

    ret = upm6922_read_byte(upm, UPM6922_REG_0B, &data);
    if (!ret) {
        upm->part_no = (data & REG0B_PN_MASK) >> REG0B_PN_SHIFT;
        upm->revision =
            (data & REG0B_DEV_REV_MASK) >> REG0B_DEV_REV_SHIFT;
    }

    return ret;
}

static int __maybe_unused upm6922_enable_dpdm_mux(struct upm6922 *upm, bool en)
{
    const u8 val = en << REG0C_DPDM_LOCK_SHIFT;

    pr_info("en:%d\n", en);

    return upm6922_update_bits(upm, UPM6922_REG_0C, REG0C_DPDM_LOCK_MASK,
                   val);
}

int upm6922_set_dp(struct upm6922 *upm, int dp_stat)
{
    const u8 val = dp_stat << REG0C_DP_MUX_SHIFT;

    pr_info("dp_stat:%d\n", dp_stat);

    return upm6922_update_bits(upm, UPM6922_REG_0C, REG0C_DP_MUX_MASK,
                   val);
}

static int __maybe_unused upm6922_set_dm(struct upm6922 *upm, int dm_stat)
{
    const u8 val = dm_stat << REG0C_DM_MUX_SHIFT;

    pr_info("dm_stat:%d\n", dm_stat);

    return upm6922_update_bits(upm, UPM6922_REG_0C, REG0C_DM_MUX_MASK,
                   val);
}

static int __maybe_unused upm6922_auto_bc12_en(struct upm6922 *upm, bool en)
{
    pr_info("en:%d\n", en);

    /* not support */

    return 0;
}

static int __maybe_unused upm6922_fast_chg_volt_th(struct upm6922 *upm, int mv)
{
    u8 val = 0;

    pr_info("mv:%d\n", mv);

    if (mv <= 2700) {
        val = REG0D_PRE2FAST_2P7V;
    } else if (mv <= 2800) {
        val = REG0D_PRE2FAST_2P8V;
    } else if (mv <= 2900) {
        val = REG0D_PRE2FAST_2P9V;
    } else {
        val = REG0D_PRE2FAST_3V;
    }

    return upm6922_update_bits(upm, UPM6922_REG_0D, REG0D_V_RRE2FAST_MASK,
                   val << REG0D_V_RRE2FAST_SHIFT);
}

static int __maybe_unused upm6922_get_bat_voltage(struct upm6922 *upm, int *mv)
{
    u8 reg_val = 0;
    int ret = 0;
    int vbat = 0;

    ret = upm6922_read_byte(upm, UPM6922_REG_0E, &reg_val);
    if (ret) {
        pr_err("read UPM6922_REG_0E fail, ret:%d\n", ret);
        return ret;
    }

    reg_val = ((reg_val & REG0E_VBAT_MASK) >> REG0E_VBAT_SHIFT);
    vbat = reg_val * REG0E_VBAT_LSB + REG0E_VBAT_BASE;

    *mv = vbat;

    return ret;
}

static int __maybe_unused upm6922_get_sys_voltage(struct upm6922 *upm, int *mv)
{
    u8 reg_val = 0;
    int ret = 0;
    int vsys = 0;

    ret = upm6922_read_byte(upm, UPM6922_REG_10, &reg_val);
    if (ret) {
        pr_err("read UPM6922_REG_10 fail, ret:%d\n", ret);
        return ret;
    }

    reg_val = ((reg_val & REG10_VSYS_MASK) >> REG10_VSYS_SHIFT);
    vsys = reg_val * REG10_VSYS_LSB + REG10_VSYS_BASE;

    *mv = vsys;

    return ret;
}

static int __maybe_unused upm6922_get_bus_voltage(struct upm6922 *upm, int *mv)
{
    u8 reg_val = 0;
    int ret = 0;
    int vbus = 0;

    ret = upm6922_read_byte(upm, UPM6922_REG_12, &reg_val);
    if (ret) {
        pr_err("read UPM6922_REG_12 fail, ret:%d\n", ret);
        return ret;
    }

    reg_val = ((reg_val & REG12_VBUS_MASK) >> REG12_VBUS_SHIFT);
    vbus = reg_val * REG12_VBUS_LSB + REG12_VBUS_BASE;

    *mv = vbus;

    return ret;
}

static int __maybe_unused upm6922_adc_start(struct upm6922 *upm, bool en)
{
    u8 val = 0;

    pr_info("en:%d\n", en);

    if (en) {
        val = REG15_CONV_START_ACTIVE;
    } else {
        val = REG15_CONV_START_INACTIVE;
    }

    return upm6922_update_bits(upm, UPM6922_REG_15, REG15_CONV_START_MASK,
                   val << REG15_CONV_START_SHIFT);
}

static int upm6922_set_adc_rate(struct upm6922 *upm, u8 mode)
{
    u8 val = 0;

    pr_info("mode:%d\n", mode);

    val = !!mode;
    return upm6922_update_bits(upm, UPM6922_REG_15, REG15_CONV_RATE_MASK,
                   val << REG15_CONV_RATE_SHIFT);
}

void upm6922_dump_regs(struct upm6922 *upm)
{
    int addr;
    u8 val;
    int ret;
    char buf[512] = {0};
    int n = 0;

    for (addr = 0x0; addr <= 0x16; addr++) {
        ret = upm6922_read_byte(upm, addr, &val);
        if (ret == 0)
	  	sprintf(buf + n,"Reg[%.2x] = 0x%.2x ", addr, val);
			n = strlen(buf);
    }
		pr_err("%s\n",buf);
}

static struct upm6922_platform_data *upm6922_parse_dt(struct device_node *np,
                              struct upm6922 *upm)
{
    int ret;
    struct upm6922_platform_data *pdata;

    pdata = devm_kzalloc(&upm->client->dev, sizeof(struct upm6922_platform_data),
                 GFP_KERNEL);
    if (!pdata)
        return NULL;

    if (of_property_read_string(np, "charger_name", &upm->chg_dev_name) < 0) {
        upm->chg_dev_name = "primary_chg";
        pr_err("no charger name, default primary chg\n");
    }

    upm->intr_gpio = of_get_named_gpio(np, "up,intr_gpio", 0);
    if (upm->intr_gpio < 0) {
        pr_err("no up,intr_gpio, ret:%d\n", upm->intr_gpio);
    }
    pr_err("intr_gpio = %d\n", upm->intr_gpio);

    ret = of_property_read_u32(np, "up,upm6922,iindpm", &pdata->ilim);
    if (ret) {
        pdata->ilim = 1700;
		pr_err("Failed to read node of up,upm6922,iindpm, default 1700\n");
    }

    ret = of_property_read_u32(np, "up,upm6922,ichg", &pdata->ichg);
    if (ret) {
        pdata->ichg = 2000;
        pr_err("Failed to read node of up,upm6922,ichg, default 2000\n");
    }

    ret = of_property_read_u32(np, "up,upm6922,iprechg", &pdata->iprechg);
    if (ret) {
        pdata->iprechg = 180;
        pr_err("Failed to read node of up,upm6922,iprechg, default 180\n");
    }

    ret = of_property_read_u32(np, "up,upm6922,iterm", &pdata->iterm);
    if (ret) {
        pdata->iterm = 180;
        pr_err("Failed to read node of up,upm6922,iterm, default 180\n");
    }

    ret = of_property_read_u32(np, "up,upm6922,vreg", &pdata->cv);
    if (ret) {
        pdata->cv = 4200;
        pr_err("Failed to read node of up,upm6922,vreg, default 4200\n");
    }

    ret = of_property_read_u32(np, "up,upm6922,vindpm", &pdata->vlim);
    if (ret) {
        pdata->vlim = 4700;
        pr_err("Failed to read node of up,upm6922,vindpm, default 4700\n");
    }

    ret = of_property_read_u32(np, "up,upm6922,boostv", &pdata->boostv);
    if (ret) {
        pdata->boostv = 5000;
        pr_err("Failed to read node of up,upm6922,boostv, default 5000\n");
    }

    ret = of_property_read_u32(np, "up,upm6922,boosti", &pdata->boosti);
    if (ret) {
        pdata->boosti = 1200;
        pr_err("Failed to read node of up,upm6922,boosti, default 1200\n");
    }

    ret = of_property_read_u32(np, "up,upm6922,vac-ovp", &pdata->vac_ovp);
    if (ret) {
        pdata->vac_ovp = 6500;
        pr_err("Failed to read node of up,upm6922,vac-ovp, default 6500\n");
    }

    ret = of_property_read_u32(np, "up,upm6922,stat-pin-ctrl", &pdata->statctrl);
    if (ret) {
        pdata->statctrl = 0;
        pr_err("Failed to read node of up,upm6922,stat-pin-ctrl, default 0\n");
    }

    upm->en_auto_bc12 = !of_property_read_bool(np, "up,upm6922,auto-bc12-disable");

    upm->en_adc_continuous = of_property_read_bool(np, "up,upm6922,continuous-adc-enable");

    return pdata;
}

static int upm6922_get_state(struct upm6922 *upm)
{
    u8 chrg_stat = 0;
    u8 chrg_param_0 = 0;
    int ret = 0;

    ret = upm6922_read_byte(upm, UPM6922_REG_08, &chrg_stat);
    if (ret) {
        pr_err("read UPM6922_REG_08 fail\n");
        return ret;
    }

    upm->chrg_stat = (chrg_stat & REG08_CHRG_STAT_MASK) >> REG08_CHRG_STAT_SHIFT;
    upm->power_good = !!(chrg_stat & REG08_PG_STAT_MASK);
    pr_err("chrg_type =%d,chrg_stat =%d online = %d\n",
           upm->vbus_stat, upm->chrg_stat, upm->power_good);

    /* get vbus state*/
    ret = upm6922_read_byte(upm, UPM6922_REG_0A, &chrg_param_0);
    if (ret) {
        pr_err("read UPM6922_REG_0A fail\n");
        return ret;
    }

    upm->vbus_attach = !!(chrg_param_0 & REG0A_VBUS_GD_MASK);
    pr_err("vbus_attach = %d\n", upm->vbus_attach);

    return 0;
}

static irqreturn_t upm6922_irq_handler(int irq, void *data)
{
    struct upm6922 *upm = (struct upm6922 *)data;
    schedule_delayed_work(&upm->charge_detect_delayed_work,

        msecs_to_jiffies(300));

    schedule_delayed_work(&upm->td_dwork,

        msecs_to_jiffies(0));

    return IRQ_HANDLED;
}
int upm6922_enable_hvdcp(struct upm6922 *upm, bool en)
{
	const u8 val = en << REG0C_EN_HVDCP_SHIFT;

	return upm6922_update_bits(upm, UPM6922_REG_0C, REG0C_EN_HVDCP_MASK,
				   val);
}
EXPORT_SYMBOL_GPL(upm6922_enable_hvdcp);

static int upm6922_get_chgtype(struct upm6922 *upm, int *type)
{
    int ret = 0;
    u8 chrg_stat = 0;
    u8 chg_type = 0;
    int hw_icl = 0;
    bool pd_active = false;

    pd_active = upm6922_get_pd_active(upm);

    ret = upm6922_read_byte(upm, UPM6922_REG_08, &chrg_stat);
    if (ret) {
        pr_err("read UPM6922_CHRG_STAT fail\n");
        return ret;
    }

    upm->chrg_type = (chrg_stat & REG08_VBUS_STAT_MASK) >> REG08_VBUS_STAT_SHIFT;
    __upm6922_get_input_current_limit(upm, &hw_icl);
    pr_err("chrg_type = 0x%x, hw_icl=%dma\n", upm->chrg_type, hw_icl);
    switch (upm->chrg_type) {
        case REG08_VBUS_TYPE_NONE:
            chg_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
            upm->this_psy_desc.type = POWER_SUPPLY_TYPE_USB;
            pr_info("charger type: none\n");
            break;
        case REG08_VBUS_TYPE_SDP:
            chg_type = POWER_SUPPLY_USB_TYPE_SDP;
            upm->this_psy_desc.type = POWER_SUPPLY_TYPE_USB;
            pr_info("charger type: SDP\n");
            break;
        case REG08_VBUS_TYPE_CDP:
            chg_type = POWER_SUPPLY_USB_TYPE_CDP;
            upm->this_psy_desc.type = POWER_SUPPLY_TYPE_USB;
            pr_info("charger type: CDP\n");
            break;
        case REG08_VBUS_TYPE_DCP:
            chg_type = POWER_SUPPLY_USB_TYPE_DCP;
            upm->this_psy_desc.type = POWER_SUPPLY_TYPE_USB_DCP;
            pr_info("charger type: DCP\n");
            break;
        case REG08_VBUS_TYPE_UNKNOWN:
            chg_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
            upm->this_psy_desc.type = POWER_SUPPLY_TYPE_USB;
            if (upm->retry_num < 3 && !pd_active) {
                upm->retry_num++;
                schedule_delayed_work(&upm->force_detect_dwork, msecs_to_jiffies(5000));
                pr_info("charger type: unknown adapter, after 5s retry bc12 %ds\n \n",upm->retry_num);
            }
            break;
        case REG08_VBUS_TYPE_NON_STD:
            chg_type = POWER_SUPPLY_USB_TYPE_DCP;
            upm->this_psy_desc.type = POWER_SUPPLY_TYPE_USB_DCP;
            pr_info("charger type: non-std charger\n");
            break;
        default:
            chg_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
            upm->this_psy_desc.type = POWER_SUPPLY_TYPE_USB;
            pr_info("charger type: invalid value\n");
            break;
    }

    if (chg_type == POWER_SUPPLY_USB_TYPE_UNKNOWN) {
        pr_err("unknown type\n");
    } else if (chg_type == POWER_SUPPLY_USB_TYPE_SDP ||
        chg_type == POWER_SUPPLY_USB_TYPE_CDP) {
    } else if (chg_type == POWER_SUPPLY_USB_TYPE_DCP) {
		pr_err("dcp deteced\n");
		upm6922_enable_hvdcp(upm, true);
		upm6922_set_dp(upm, REG0C_DPDM_OUT_0P6V);
		upm6922_set_dm(upm, REG0C_DPDM_OUT_HIZ);
        if(pd_active) {
            pr_info("pd_active, no do afc detection\n");
            if (upm->is_disble_afc)
                vote(upm->usb_icl_votable, DISABLE_AFC_VOTER, true, PD_ICL_CURR_MAX);
        } else
            queue_delayed_work(upm->wq, &upm->hvdcp_dwork, msecs_to_jiffies(1400));
    }

    *type = chg_type;
    power_supply_changed(upm->this_psy);

    return 0;
}
static bool is_ds_chan_valid(struct upm6922 *upm,
		enum ds_ext_iio_channels chan)
{
	int rc;
	if (IS_ERR(upm->ds_ext_iio_chans[chan]))
		return false;
	if (!upm->ds_ext_iio_chans[chan]) {
		upm->ds_ext_iio_chans[chan] = iio_channel_get(upm->dev,
					ds_ext_iio_chan_name[chan]);
		if (IS_ERR(upm->ds_ext_iio_chans[chan])) {
			rc = PTR_ERR(upm->ds_ext_iio_chans[chan]);
			if (rc == -EPROBE_DEFER)
				upm->ds_ext_iio_chans[chan] = NULL;
			pr_err("Failed to get IIO channel %s, rc=%d\n",
				ds_ext_iio_chan_name[chan], rc);
			return false;
		}
	}
	return true;
}

static bool is_bms_chan_valid(struct upm6922 *upm,
		enum fg_ext_iio_channels chan)
{
	int rc;
	if (IS_ERR(upm->fg_ext_iio_chans[chan]))
		return false;
	if (!upm->fg_ext_iio_chans[chan]) {
		upm->fg_ext_iio_chans[chan] = iio_channel_get(upm->dev,
					fg_ext_iio_chan_name[chan]);
		if (IS_ERR(upm->fg_ext_iio_chans[chan])) {
			rc = PTR_ERR(upm->fg_ext_iio_chans[chan]);
			if (rc == -EPROBE_DEFER)
				upm->fg_ext_iio_chans[chan] = NULL;
			pr_err("bms_chan_valid Failed to get fg_ext_iio channel %s, rc=%d\n",
				fg_ext_iio_chan_name[chan], rc);
			return false;
		}
	}
	return true;
}

static bool is_nopmi_chg_chan_valid(struct upm6922 *upm,
		enum nopmi_chg_ext_iio_channels chan)
{
	int rc;
	if (IS_ERR(upm->nopmi_chg_ext_iio_chans[chan]))
		return false;
	if (!upm->nopmi_chg_ext_iio_chans[chan]) {
		upm->nopmi_chg_ext_iio_chans[chan] = iio_channel_get(upm->dev,
					nopmi_chg_ext_iio_chan_name[chan]);
		if (IS_ERR(upm->nopmi_chg_ext_iio_chans[chan])) {
			rc = PTR_ERR(upm->nopmi_chg_ext_iio_chans[chan]);
			if (rc == -EPROBE_DEFER)
				upm->nopmi_chg_ext_iio_chans[chan] = NULL;
			pr_err("nopmi_chg_chan_valid failed to get IIO channel %s, rc=%d\n",
				nopmi_chg_ext_iio_chan_name[chan], rc);
			return false;
		}
	}
	return true;
}

int upm6922_get_iio_channel(struct upm6922 *upm,
			enum upm6922_iio_type type, int channel, int *val)
{
	struct iio_channel *iio_chan_list;
	int rc;
/*
	if(upm->shutdown_flag)
		return -ENODEV;
*/
	switch (type) {
	case DS28E16:
		if (!is_ds_chan_valid(upm, channel))
			return -ENODEV;
		iio_chan_list = upm->ds_ext_iio_chans[channel];
		break;
	case BMS:
		if (!is_bms_chan_valid(upm, channel))
			return -ENODEV;
		iio_chan_list = upm->fg_ext_iio_chans[channel];
		break;
	case NOPMI:
		if (!is_nopmi_chg_chan_valid(upm, channel))
			return -ENODEV;
		iio_chan_list = upm->nopmi_chg_ext_iio_chans[channel];
		break;
	default:
		pr_err_ratelimited("iio_type %d is not supported\n", type);
		return -EINVAL;
	}
	rc = iio_read_channel_processed(iio_chan_list, val);
	return rc < 0 ? rc : 0;
}

int upm6922_set_iio_channel(struct upm6922 *upm,
			enum upm6922_iio_type type, int channel, int val)
{
	struct iio_channel *iio_chan_list;
	int rc;
/*
	if(chg->shutdown_flag)
		return -ENODEV;
*/
	switch (type) {
	case DS28E16:
		if (!is_ds_chan_valid(upm, channel)){
			pr_err("%s: iio_type is DS28E16 \n",__func__);
			return -ENODEV;
		}
		iio_chan_list = upm->ds_ext_iio_chans[channel];
		break;
	case BMS:
		if (!is_bms_chan_valid(upm, channel)){
			pr_err("%s: iio_type is BMS \n",__func__);
			return -ENODEV;
		}
			
		iio_chan_list = upm->fg_ext_iio_chans[channel];
		break;
	case NOPMI:
		if (!is_nopmi_chg_chan_valid(upm, channel)){
			pr_err("%s: iio_type is NOPMI \n",__func__);
			return -ENODEV;
		}
		iio_chan_list = upm->nopmi_chg_ext_iio_chans[channel];
		break;
	default:
		pr_err_ratelimited("iio_type %d is not supported\n", type);
		pr_err("iio_type %d is not supported\n", type);
		return -EINVAL;
	}
	rc = iio_write_channel_raw(iio_chan_list, val);
	return rc < 0 ? rc : 0;
}
static int upm6922_ops_dump_register(struct charger_device *chg_dev);
static int upm6922_set_usb(struct upm6922 *upm, bool online_val)
{
	int ret = 0;
	union power_supply_propval propval;
	if(upm->usb_psy == NULL) {
		upm->usb_psy = power_supply_get_by_name("usb");
		if (upm->usb_psy == NULL) {
			pr_err("%s : fail to get psy usb\n", __func__);
			return -ENODEV;
		}
	}
	propval.intval = online_val;
    if(propval.intval) {
        ret = power_supply_set_property(upm->usb_psy, POWER_SUPPLY_PROP_PRESENT, &propval);
        if (ret < 0)
            pr_err("set usb present fail ret:%d\n", ret);
        ret = power_supply_set_property(upm->usb_psy, POWER_SUPPLY_PROP_ONLINE, &propval);
        if (ret < 0)
            pr_err("%s : set upm6922_set_usb fail ret:%d\n", __func__, ret);
    } else {
        ret = power_supply_set_property(upm->usb_psy, POWER_SUPPLY_PROP_ONLINE, &propval);
        if (ret < 0)
            pr_err("%s : set upm6922_set_usb fail ret:%d\n", __func__, ret);
        ret = power_supply_set_property(upm->usb_psy, POWER_SUPPLY_PROP_PRESENT, &propval);
        if (ret < 0)
            pr_err("set usb present fail ret:%d\n", ret);
    }
	return ret;
}

static int upm6922_ops_charging(struct upm6922 *upm, bool enable);

void upm6922_td_test_work_func(struct work_struct *work)
{
    struct delayed_work *td_dwork = NULL;
    struct upm6922 *upm = NULL;
    u8 bc12_flag;
    u8 data=0;
    
    td_dwork = container_of(work, struct delayed_work, work);
    if (td_dwork == NULL) {
        pr_err("Cann't get td_dwork\n");
        return;
    }

    upm = container_of(td_dwork, struct upm6922,
    td_dwork);
    if (upm == NULL) {
        pr_err("Cann't get upm6922_device\n");
        return;
    }

    upm6922_read_byte(upm, UPM6922_REG_08, &bc12_flag);
    bc12_flag = (bc12_flag & 0X04) >> 2;

    if(bc12_flag == 0)
    {
        pr_notice("%s adapter/usb insert first\n", __func__);

        upm6922_read_byte(upm, UPM6922_REG_0A, &data);
        if((data & REG0A_VBUS_GD_MASK) >> REG0A_VBUS_GD_SHIFT) {
            upm6922_chg_set_usbsw(upm, USBSW_CHG);
            upm->vbus_insert_flag = true;
        } else {
            upm->vbus_insert_flag = false;
        }

        upm->is_vindpm = true;
        upm6922_set_input_volt_limit(upm, 12000);
        msleep(800);
        upm6922_set_input_volt_limit(upm, upm->upd->vlim);
        upm->is_vindpm = false;

        pr_notice("end");
    }
}

static void charger_detect_work_func(struct work_struct *work)
{
    struct delayed_work *charge_detect_delayed_work = NULL;
    struct upm6922 *upm = NULL;
    int ret = 0;
    bool vbus_attach_pre = false;
    //bool power_good_pre = false;
    charge_detect_delayed_work = container_of(work, struct delayed_work, work);
    if (charge_detect_delayed_work == NULL) {
        pr_err("Cann't get charge_detect_delayed_work\n");
        return;
    }
    upm = container_of(charge_detect_delayed_work, struct upm6922,
    charge_detect_delayed_work);
    if (upm == NULL) {
        pr_err("Cann't get upm6922_device\n");
        return;
    }
    
    vbus_attach_pre = upm->vbus_attach;
    //power_good_pre = upm->power_good;
    mutex_lock(&upm->lock);
    ret = upm6922_get_state(upm);
    mutex_unlock(&upm->lock);
    pr_err("bc12 trigger, v_pre = %d, v_now = %d, ret = %d bc12_done = %d\n",
    vbus_attach_pre, upm->vbus_attach, ret, upm->bc12_done);
    if (!vbus_attach_pre && upm->vbus_attach) {
        pr_notice("adapter/usb inserted\n");
        vote(upm->usb_icl_votable, DISABLE_AFC_VOTER, false, 0);
        vote(upm->fcc_votable, DISABLE_AFC_VOTER, false, 0);

        upm->chg_config = true;
        upm->force_dpdm = true;
        upm->vbus_insert_flag = false;
    } else if (vbus_attach_pre && !upm->vbus_attach) {
        pr_notice("adapter/usb removed\n");
        upm->charge_afc = 0;
        if(upm->bc12_done) {
            if(upm6922_get_pd_pdo_type(upm)) {
                pr_notice("wait pps disconnect\n");
            } else {
                upm6922_set_usb(upm, false);
            }

            upm->bc12_done = false;
        }
        upm->chg_config = false;
        cancel_delayed_work_sync(&upm->force_detect_dwork);
        upm6922_hvdcp_dwork_cancel(upm);
        upm->force_dpdm = false;
        upm->retry_num = 0;
		upm6922_set_iio_channel(upm, NOPMI, NOPMI_CHG_INPUT_SUSPEND, 0);
        upm6922_ops_charging(upm, true);

        vote(upm->usb_icl_votable, DISABLE_AFC_VOTER, false, 0);
        vote(upm->fcc_votable, DISABLE_AFC_VOTER, false, 0);

        upm->psy_usb_type = POWER_SUPPLY_USB_TYPE_DCP;
        upm->this_psy_desc.type = POWER_SUPPLY_TYPE_USB_DCP;

		upm6922_set_input_current_limit(upm, 500);
    } else if (vbus_attach_pre && upm->vbus_attach) {
        if(upm->vbus_insert_flag) {
            pr_notice("adapter/usb quick unplug and plug\n");

            rerun_election(upm->usb_icl_votable);
        }
        upm->vbus_insert_flag = false;
    }
    
    /* bc12 done, get charger type */
    if ((upm->force_dpdm == true) && (upm->power_good == true)) {
        pr_notice("start get chgtype!\n");
        upm->bc12_done = true;
		upm6922_get_chgtype(upm, &upm->psy_usb_type);
		upm6922_set_usb(upm, true);
        rerun_election(upm->usb_icl_votable);
        upm->force_dpdm = false;
        if(upm->psy_usb_type != POWER_SUPPLY_USB_TYPE_DCP && upm->psy_usb_type != POWER_SUPPLY_USB_TYPE_UNKNOWN)
            upm6922_chg_set_usbsw(upm, USBSW_USB);
		pr_err("get chgtype end!\n");
    }
    
    upm6922_get_charging_stat(upm, &upm->chrg_psy_stat);
    upm6922_ops_dump_register(upm->chg_dev);
    power_supply_changed(upm->this_psy);
}
int upm6922_chg_get_iio_channel(struct upm6922 *upm,
			enum upm6922_iio_type type, int channel, int *val)
{
	struct iio_channel *iio_chan_list = NULL;
	int rc = 0;

	switch (type) {
		default:
		pr_err_ratelimited("iio_type %d is not supported\n", type);
		return -EINVAL;
	}

	rc = iio_read_channel_processed(iio_chan_list, val);

	return rc < 0 ? rc : 0;
}
EXPORT_SYMBOL(upm6922_chg_get_iio_channel);
int upm6922_get_input_current_limit(struct upm6922 *upm)
{
        u8 reg_val;

	upm6922_read_byte(upm, UPM6922_REG_00, &reg_val);

	reg_val &= REG00_IINLIM_MASK;

        if (reg_val)
		return 0;
	else
		return 1;
}
static int upm6922_iio_read_raw(struct iio_dev *indio_dev,
		struct iio_chan_spec const *chan, int *val1,
		int *val2, long mask)
{
    struct mtk_battery *gm;
	struct power_supply *psy;
	struct upm6922 *upm = iio_priv(indio_dev);
	int rc = 0;
	*val1 = 0;

	if(!upm){
		pr_err("upm is NULL,Fail !\n");
		return -EINVAL;
	}

	switch (chan->channel) {
	case PSY_IIO_CHARGE_TYPE:
		rc = upm6922_get_charging_status(upm);
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
		*val1 = upm6922_get_input_current_limit(upm);
		break;
	case PSY_IIO_CHARGE_AFC:
		*val1 = upm->charge_afc;
		break;
    case PSY_IIO_RESISTANCE_ID:
		psy = power_supply_get_by_name(MTK_BATTERY_PSY_NAME);
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
		pr_err("Unsupported upm6922 IIO chan %d\n", chan->channel);
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

static int upm6922_iio_write_raw(struct iio_dev *indio_dev,
		struct iio_chan_spec const *chan, int val1,
		int val2, long mask)
{
	struct  upm6922 *upm = iio_priv(indio_dev);
	int rc = 0;

	if(!upm){
		pr_err("upm is NULL,Fail !\n");
		return -EINVAL;
	}

	pr_info("Write IIO channel %d, val = %d\n", chan->channel, val1);
	switch (chan->channel) {
	case PSY_IIO_CHARGE_TYPE:
		upm->chg_type = val1;
		break;
	case PSY_IIO_CHARGE_ENABLED:
		upm6922_enable_charger(upm);
		 pr_err("Write IIO channel %d, val = %d\n", chan->channel, val1);
		break;
	case PSY_IIO_CHARGE_DISABLE:
		upm6922_disable_charger(upm);
		pr_err("Write IIO channel %d, val = %d\n", chan->channel, val1);
		break;
	case PSY_IIO_CHARGE_AFC_DISABLE:
		upm->is_disble_afc = val1;
		pr_info("%s, bakup afc:%d disafc:%d\n",__func__, upm->is_disble_afc_backup, upm->is_disble_afc);

		if (upm->is_disble_afc_backup != upm->is_disble_afc) {
			if (upm->charge_afc == 1) {
				pr_err("disable afc\n");
				upm6922_disable_afc(upm);
			} else if ((upm6922_get_pdo_active(upm) == 1)) {
				pr_err("disable pdo\n");
				upm6922_disable_pdo(upm);
			}
			upm->is_disble_afc_backup = upm->is_disble_afc;
                  	pr_info("%s, 2 bakup afc:%d disafc:%d\n",__func__, upm->is_disble_afc_backup, upm->is_disble_afc);
		} else {
			pr_info("No changes, no need adjust vbus\n");
		}
		//pr_err("%s: is_disble_afc=%d\n", __func__, upm->is_disble_afc);
		break;
	default:
		pr_err("Unsupported upm6922 IIO chan %d\n", chan->channel);
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

static int upm6922_iio_of_xlate(struct iio_dev *indio_dev,
				const struct of_phandle_args *iiospec)
{
	struct upm6922 *upm = iio_priv(indio_dev);
	struct iio_chan_spec *iio_chan = upm->iio_chan;
	int i = 0;

	if(!upm){
		pr_err("upm is NULL,Fail !\n");
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(upm6922_iio_psy_channels);
					i++, iio_chan++)
		if (iio_chan->channel == iiospec->args[0]){
			return i;
		}
	return -EINVAL;

}

static const struct iio_info upm6922_iio_info = {
	.read_raw	= upm6922_iio_read_raw,
	.write_raw	= upm6922_iio_write_raw,
	.of_xlate	= upm6922_iio_of_xlate,
};

static int upm6922_init_iio_psy(struct upm6922 *upm)
{
	struct iio_dev *indio_dev = upm->indio_dev;
	struct iio_chan_spec *chan = NULL;
	int num_iio_channels = ARRAY_SIZE(upm6922_iio_psy_channels);
	int rc = 0, i = 0;

	pr_info("start !\n");

	if(!upm){
		pr_err("upm is NULL,Fail !\n");
		return -EINVAL;
	}

	upm->iio_chan = devm_kcalloc(upm->dev, num_iio_channels,
						sizeof(*upm->iio_chan), GFP_KERNEL);
	if (!upm->iio_chan) {
		pr_err("upm->iio_chan is null!\n");
		return -ENOMEM;
	}

	upm->int_iio_chans = devm_kcalloc(upm->dev,
				num_iio_channels,
				sizeof(*upm->int_iio_chans),
				GFP_KERNEL);
	if (!upm->int_iio_chans) {
		pr_err("upm->int_iio_chans is null!\n");
		return -ENOMEM;
	}

	indio_dev->info = &upm6922_iio_info;
	indio_dev->dev.parent = upm->dev;
	indio_dev->dev.of_node = upm->dev->of_node;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = upm->iio_chan;
	indio_dev->num_channels = num_iio_channels;
	indio_dev->name = "main_chg";

	for (i = 0; i < num_iio_channels; i++) {
		upm->int_iio_chans[i].indio_dev = indio_dev;
		chan = &upm->iio_chan[i];
		upm->int_iio_chans[i].channel = chan;
		chan->address = i;
		chan->channel = upm6922_iio_psy_channels[i].channel_num;
		chan->type = upm6922_iio_psy_channels[i].type;
		chan->datasheet_name =
			upm6922_iio_psy_channels[i].datasheet_name;
		chan->extend_name =
			upm6922_iio_psy_channels[i].datasheet_name;
		chan->info_mask_separate =
			upm6922_iio_psy_channels[i].info_mask;
	}

	rc = devm_iio_device_register(upm->dev, indio_dev);
	if (rc)
		pr_err("Failed to register upm6922 IIO device, rc=%d\n", rc);

	pr_info("upm6922 IIO device, rc=%d\n", rc);
	return rc;
}

static int upm6922_ext_init_iio_psy(struct upm6922 *upm)
{
	if (!upm){
		pr_err("upm6922_ext_init_iio_psy, upm is NULL!\n");
		return -ENOMEM;
	}

	upm->ds_ext_iio_chans = devm_kcalloc(upm->dev,
				ARRAY_SIZE(ds_ext_iio_chan_name),
				sizeof(*upm->ds_ext_iio_chans),
				GFP_KERNEL);
	if (!upm->ds_ext_iio_chans) {
		pr_err("upm->ds_ext_iio_chans is NULL!\n");
		return -ENOMEM;
	}

	upm->fg_ext_iio_chans = devm_kcalloc(upm->dev,
		ARRAY_SIZE(fg_ext_iio_chan_name), sizeof(*upm->fg_ext_iio_chans), GFP_KERNEL);
	if (!upm->fg_ext_iio_chans) {
		pr_err("upm->fg_ext_iio_chans is NULL!\n");
		return -ENOMEM;
	}

	upm->nopmi_chg_ext_iio_chans = devm_kcalloc(upm->dev,
		ARRAY_SIZE(nopmi_chg_ext_iio_chan_name), sizeof(*upm->nopmi_chg_ext_iio_chans), GFP_KERNEL);
	if (!upm->nopmi_chg_ext_iio_chans) {
		pr_err("upm->nopmi_chg_ext_iio_chans is NULL!\n");
		return -ENOMEM;
	}
	return 0;
}


static void upm6922_inform_prob_dwork_handler(struct work_struct *work)
{
    struct upm6922 *upm = container_of(work, struct upm6922,prob_dwork.work);
	u8 power_good_flag;

    upm6922_read_byte(upm, UPM6922_REG_08, &power_good_flag);
    upm->power_good = !!(power_good_flag & REG08_PG_STAT_MASK);
    upm6922_chg_set_usbsw(upm, USBSW_CHG);
    upm6922_force_dpdm(upm);
    upm->force_dpdm = true;
}

static int upm6922_register_interrupt(struct upm6922 *upm)
{
    int ret = 0;

    if (upm->intr_gpio < 0) {
        pr_err("no intr gpio, skip register interrupt\n");
        return 0;
    }

    ret = devm_gpio_request_one(upm->dev, upm->intr_gpio, GPIOF_DIR_IN,
            devm_kasprintf(upm->dev, GFP_KERNEL,
            "upm6922_intr_gpio.%s", dev_name(upm->dev)));
    if (ret < 0) {
        pr_err("gpio request fail(%d)\n", ret);
        return ret;
    }

    upm->client->irq = gpio_to_irq(upm->intr_gpio);
    if (upm->client->irq < 0) {
        pr_err("gpio2irq fail(%d)\n", upm->client->irq);
        return upm->client->irq;
    }

    ret = devm_request_threaded_irq(upm->dev, upm->client->irq, NULL,
                    upm6922_irq_handler,
                    IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
                    "upm_irq", upm);
    if (ret < 0) {
        pr_err("request thread irq failed:%d\n", ret);
        return ret;
    }

    return 0;
}

static int upm6922_set_vdpm_bat_track(struct upm6922 *upm, int val)
{
	return upm6922_update_bits(upm, UPM6922_REG_07, REG07_VDPM_BAT_TRACK_MASK,
					val << REG07_VDPM_BAT_TRACK_SHIFT);
}

static int upm6922_init_device(struct upm6922 *upm)
{
    int ret;

    upm6922_disable_watchdog_timer(upm);
    
    upm6922_set_vdpm_bat_track(upm, 1);

    ret = upm6922_set_input_current_limit(upm, upm->upd->ilim);
    if (ret) {
        pr_err("Failed to set iindpm, ret = %d\n", ret);
    }

    /*
    ret = upm6922_set_chargecurrent(upm, upm->upd->ichg);
    if (ret) {
        pr_err("Failed to set ichg, ret = %d\n", ret);
    }
    */
    ret = upm6922_get_chargecurrent(upm, &upm->poweron_fcc_ma);
    if (ret) {
        pr_err("get ichg fail, ret:%d\n", ret);
        return ret;
    }
    pr_err("poweron_fcc_ma %d\n", upm->poweron_fcc_ma);

    ret = upm6922_set_prechg_current(upm, upm->upd->iprechg);
    if (ret) {
        pr_err("Failed to set prechg current, ret = %d\n", ret);
    }

    ret = upm6922_set_term_current(upm, upm->upd->iterm);
    if (ret) {
        pr_err("Failed to set termination current, ret = %d\n", ret);
    }

    ret = upm6922_set_chargevolt(upm, upm->upd->cv);
    if (ret) {
        pr_err("Failed to set cv, ret = %d\n", ret);
    }

    ret = upm6922_set_input_volt_limit(upm, upm->upd->vlim);
    if (ret) {
        pr_err("Failed to set vlim, ret = %d\n", ret);
    }

    ret = upm6922_set_boost_voltage(upm, upm->upd->boostv);
    if (ret) {
        pr_err("Failed to set boost voltage, ret = %d\n", ret);
    }

    ret = upm6922_set_boost_current(upm, upm->upd->boosti);
    if (ret) {
        pr_err("Failed to set boost current, ret = %d\n", ret);
    }

    ret = upm6922_set_acovp_threshold(upm, upm->upd->vac_ovp);
    if (ret) {
        pr_err("Failed to set acovp threshold, ret = %d\n", ret);
    }

    ret = upm6922_set_stat_ctrl(upm, upm->upd->statctrl);
    if (ret) {
        pr_err("Failed to set stat pin control mode, ret = %d\n", ret);
    }

    ret = upm6922_enable_vindpm_int(upm, false);
    if (ret) {
        pr_err("Failed to mask vindpm int, ret = %d\n", ret);
    }

    ret = upm6922_enable_iindpm_int(upm, false);
    if (ret) {
        pr_err("Failed to mask iindpm int, ret = %d\n", ret);
    }

    ret = upm6922_set_adc_rate(upm, 0);
    if (ret) {
        pr_err("Failed to set adc rate, ret = %d\n", ret);
    }
    upm6922_batfet_rst_en(upm, false);
    upm6922_dump_regs(upm);

    return 0;
}

#ifndef __MTK_CHARGER_TYPE_H__
#define __MTK_CHARGER_TYPE_H__
enum charger_type {
    CHARGER_UNKNOWN = 0,
    STANDARD_HOST,
    CHARGING_HOST,
    NONSTANDARD_CHARGER,
    STANDARD_CHARGER,
    APPLE_2_1A_CHARGER,
    APPLE_1_0A_CHARGER,
    APPLE_0_5A_CHARGER,
    WIRELESS_CHARGER,
};
#endif /* __MTK_CHARGER_TYPE_H__ */

static void upm6922_inform_psy_dwork_handler(struct work_struct *work)
{
	struct upm6922 *upm = container_of(work, struct upm6922,
								psy_dwork.work);
    int ret = 0;
	union power_supply_propval propval;
    int vbus_stat = 0;
    u8 reg_val = 0;

    if (!upm->inform_psy) {
        upm->inform_psy = power_supply_get_by_name("charger");
        if (!upm->inform_psy) {
            pr_err("Couldn't get psy\n");
            mod_delayed_work(system_wq, &upm->psy_dwork,
                    msecs_to_jiffies(2*1000));
            return;
        }
    }

    propval.intval = upm->psy_usb_type ? 1 : 0;
    ret = power_supply_set_property(upm->inform_psy,
                POWER_SUPPLY_PROP_ONLINE, &propval);
	if (ret < 0) {
		pr_err("inform power supply online failed:%d\n", ret);
    }

    ret = upm6922_read_byte(upm, UPM6922_REG_08, &reg_val);
    if (ret) {
        pr_err("read UPM6922_REG_08 fail, ret:%d\n", ret);
        return ;
    }

    vbus_stat = (reg_val & REG08_VBUS_STAT_MASK);
    vbus_stat >>= REG08_VBUS_STAT_SHIFT;

    switch (vbus_stat) {
        case REG08_VBUS_TYPE_NONE:
            propval.intval = CHARGER_UNKNOWN;
            break;

        case REG08_VBUS_TYPE_SDP:
            propval.intval = STANDARD_HOST;
            break;

        case REG08_VBUS_TYPE_CDP:
            propval.intval = CHARGING_HOST;
            break;

        case REG08_VBUS_TYPE_DCP:
        case REG08_VBUS_TYPE_NON_STD:
            propval.intval = STANDARD_CHARGER;
            break;

        case REG08_VBUS_TYPE_UNKNOWN:
            propval.intval = NONSTANDARD_CHARGER;
            break;

        default:
            pr_err("is otg? vbus_stat:%d\n", vbus_stat);
            propval.intval = CHARGER_UNKNOWN;
            break;
    }

    ret = power_supply_set_property(upm->inform_psy,
                POWER_SUPPLY_PROP_CHARGE_TYPE, &propval);
	if (ret < 0) {
		pr_err("inform power supply charge type failed:%d\n", ret);
    }
}

static void upm6922_monitor_dwork_handler(struct work_struct *work)
{
    struct upm6922 *upm = container_of(work, struct upm6922,
                                monitor_dwork.work);

    __pm_stay_awake(upm->monitor_ws);

    pr_info("monitor work\n");
    upm6922_dump_regs(upm);

    schedule_delayed_work(&upm->monitor_dwork, msecs_to_jiffies(10*1000));

    __pm_relax(upm->monitor_ws);
}

static ssize_t upm6922_show_registers(struct device *dev, 
                    struct device_attribute *attr, char *buf)
{
    struct upm6922 *upm = dev_get_drvdata(dev);
    u8 addr;
    u8 val;
    u8 tmpbuf[200];
    int len;
    int idx = 0;
    int ret;

    idx = snprintf(buf, PAGE_SIZE, "%s:\n", "upm6922 Reg");
    for (addr = 0x0; addr <= 0x16; addr++) {
        ret = upm6922_read_byte(upm, addr, &val);
        if (ret == 0) {
            len = snprintf(tmpbuf, PAGE_SIZE - idx,
                       "Reg[%.2x] = 0x%.2x\n", addr, val);
            memcpy(&buf[idx], tmpbuf, len);
            idx += len;
        }
    }

    return idx;
}

static ssize_t upm6922_store_registers(struct device *dev,
            struct device_attribute *attr, const char *buf, size_t count)
{
    struct upm6922 *upm = dev_get_drvdata(dev);
    int ret;
    unsigned int reg;
    unsigned int val;

    ret = sscanf(buf, "%x %x", &reg, &val);
    if (ret == 2 && reg <= 0x16) {
        upm6922_write_byte(upm, (unsigned char) reg,
                   (unsigned char) val);
    }

    return count;
}

static DEVICE_ATTR(registers, S_IRUGO | S_IWUSR, upm6922_show_registers,
           upm6922_store_registers);

static ssize_t upm6922_store_shipmode(struct device *dev,
            struct device_attribute *attr, const char *buf, size_t count)
{
    struct upm6922 *upm = dev_get_drvdata(dev);

    if (buf[0] == '1') {
        upm->entry_shipmode = true;
    }

    pr_info("shipmode=%d\n", upm->entry_shipmode);

    return count;
}

static DEVICE_ATTR(shipmode, S_IWUSR, NULL, upm6922_store_shipmode);


static struct attribute *upm6922_attributes[] = {
    &dev_attr_registers.attr,
    &dev_attr_shipmode.attr,
    NULL,
};

static const struct attribute_group upm6922_attr_group = {
    .attrs = upm6922_attributes,
};

static enum power_supply_usb_type upm6922_usb_types[] = {
    POWER_SUPPLY_USB_TYPE_UNKNOWN,
    POWER_SUPPLY_USB_TYPE_SDP,
    POWER_SUPPLY_USB_TYPE_CDP,
    POWER_SUPPLY_USB_TYPE_DCP,
};

static enum power_supply_property upm6922_charger_props[] = {
    POWER_SUPPLY_PROP_STATUS,
    POWER_SUPPLY_PROP_ONLINE,
    POWER_SUPPLY_PROP_USB_TYPE,
    POWER_SUPPLY_PROP_MANUFACTURER,
    POWER_SUPPLY_PROP_TYPE,
    POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
    POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
    POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
    POWER_SUPPLY_PROP_PRECHARGE_CURRENT,
    POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
    POWER_SUPPLY_PROP_MODEL_NAME,
};
/*
bool charger_manager_pd_is_online(struct upm6922 *upm )
{
	struct mtk_charger *info;
	struct power_supply *psy;
	psy = power_supply_get_by_name("mtk-master-charger");
	if (IS_ERR_OR_NULL(psy)) {
		pr_err("%s get mtk-charger psy fail\n", __func__);
		return 0;
	}
	info = power_supply_get_drvdata(psy);
	if (IS_ERR_OR_NULL(info)) {
		pr_err("%s get mtk-charger info fail\n", __func__);
		return 0;
	}
	if(info->pd_type == MTK_PD_CONNECT_PE_READY_SNK_APDO
		|| info->pd_type == MTK_PD_CONNECT_PE_READY_SNK
		|| info->pd_type == MTK_PD_CONNECT_PE_READY_SNK_PD30){
		upm->this_psy_desc.type =  POWER_SUPPLY_TYPE_USB_PD;
		pr_info("%s pd is online\n",__func__);
		return 1;
	} else {
		pr_info("%s pd is not online\n",__func__);
		return 0;
	}
}
*/

static int upm6922_charger_get_property(struct power_supply *psy,
                enum power_supply_property psp,
                union power_supply_propval *val)
{
    struct upm6922 *upm = power_supply_get_drvdata(psy);
    int ret = 0;

    val->intval = 0;
    switch (psp) {
        case POWER_SUPPLY_PROP_STATUS:
            val->intval = upm->chrg_psy_stat;
            if((val->intval == POWER_SUPPLY_STATUS_DISCHARGING || val->intval == POWER_SUPPLY_STATUS_NOT_CHARGING)
                            && upm6922_get_pd_active(upm)){
                //pr_info("main charge is discharging but pps online, set status to charging \n", __func__);
                val->intval = POWER_SUPPLY_STATUS_CHARGING;
            }
            break;

        case POWER_SUPPLY_PROP_ONLINE:
            if(upm6922_get_pd_pdo_type(upm)){
                val->intval = 1;
            } else {
                val->intval = upm->power_good;
            }
	    break;

        case POWER_SUPPLY_PROP_USB_TYPE:
            if(!upm->power_good && upm6922_get_pd_pdo_type(upm)) {
                val->intval = POWER_SUPPLY_USB_TYPE_DCP;
            } else {
                val->intval = upm->psy_usb_type;
            }
            break;

        case POWER_SUPPLY_PROP_MANUFACTURER:
            val->strval = "Unisemipower";
            break;
        case POWER_SUPPLY_PROP_TYPE:
            val->intval = upm->this_psy_desc.type;
            break;
        case POWER_SUPPLY_PROP_MODEL_NAME:
            val->strval = upm6922_driver_name;
            break;
        default:
            break;
    }

    if (ret) {
        pr_err("err: psp:%d, ret:%d", psp, ret);
    }

    return ret;
}

static int upm6922_ops_set_icl(struct charger_device *chg_dev, u32 ua);
static int upm6922_charger_set_property(struct power_supply *psy,
                    enum power_supply_property prop,
                    const union power_supply_propval *val)
{
    int ret = -EINVAL;

    switch (prop) {
        case POWER_SUPPLY_PROP_ONLINE:
            pr_info("%d\n", val->intval);
            ret = upm6922_chg_attach_pre_process(s_chg_dev_otg, val->intval);
            break;
        case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
			ret = upm6922_ops_set_icl(s_chg_dev_otg, val->intval);
            break;
        default:
            return -EINVAL;
    }

    return ret;
}

static int upm6922_charger_is_writeable(struct power_supply *psy,
                    enum power_supply_property prop)
{
    switch (prop) {
        case POWER_SUPPLY_PROP_ONLINE:
        case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
        case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
        case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
        case POWER_SUPPLY_PROP_PRECHARGE_CURRENT:
        case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
            return true;
        default:
            return false;
    }
}

static char *upm6922_charger_supplied_to[] = {
    "battery",
    "mtk-master-charger",
    "primary_chg",
	"mtk-battery"
};

static struct power_supply_desc upm6922_power_supply_desc = {
    .name = "charger",
    .type = POWER_SUPPLY_TYPE_USB,
    .usb_types = upm6922_usb_types,
    .num_usb_types = ARRAY_SIZE(upm6922_usb_types),
    .properties = upm6922_charger_props,
    .num_properties = ARRAY_SIZE(upm6922_charger_props),
    .get_property = upm6922_charger_get_property,
    .set_property = upm6922_charger_set_property,
    .property_is_writeable = upm6922_charger_is_writeable,
};

static int upm6922_charger_psy_register(struct upm6922 *upm)
{

    struct power_supply_config psy_cfg = {
        .drv_data = upm,
        .of_node = upm->dev->of_node,
    };

    psy_cfg.supplied_to = upm6922_charger_supplied_to;
    psy_cfg.num_supplicants = ARRAY_SIZE(upm6922_charger_supplied_to);

    memcpy(&upm->this_psy_desc, &upm6922_power_supply_desc,
        sizeof(upm->this_psy_desc));

    upm->this_psy = devm_power_supply_register(upm->dev,
            &upm->this_psy_desc, &psy_cfg);
    if (IS_ERR(upm->this_psy)) {
        pr_err("failed to register charger_psy\n");
        return PTR_ERR(upm->this_psy);
    }

    pr_err("%s power supply register successfully\n", upm->this_psy_desc.name);

    return 0;
}


static int upm6922_ops_charging(struct upm6922 *upm, bool enable)
{
	int ret = 0;

	if (enable) {
        ret = upm6922_enable_charger(upm);
    } else {
        ret = upm6922_disable_charger(upm);
    }

	return ret;
}

/*************************mtk interface start*************************/
/*
static int upm6922_ops_plug_in(struct charger_device *chg_dev)
{
	int ret;

	ret = upm6922_ops_charging(chg_dev, true);
	if (ret) {
		pr_err("Failed to enable charging:%d\n", ret);
    }

	return ret;
}

static int upm6922_ops_plug_out(struct charger_device *chg_dev)
{
	int ret;

	ret = upm6922_ops_charging(chg_dev, false);
	if (ret) {
		pr_err("Failed to disable charging:%d\n", ret);
    }

	return ret;
}
*/

static int upm6922_ops_dump_register(struct charger_device *chg_dev)
{
	struct upm6922 *upm = dev_get_drvdata(&chg_dev->dev);

	upm6922_dump_regs(upm);

	return 0;
}

static int upm6922_ops_is_charging_enable(struct charger_device *chg_dev, bool *en)
{
	struct upm6922 *upm = dev_get_drvdata(&chg_dev->dev);
    int ret = 0;
    u8 val = 0;

	ret = upm6922_read_byte(upm, UPM6922_REG_01, &val);
	if (ret) {
        pr_err("read UPM6922_REG_01 fail, ret:%d\n", ret);
        return ret;
    }

    *en = !!(val & REG01_CHG_CONFIG_MASK);

	return 0;
}

static int upm6922_ops_get_ichg(struct charger_device *chg_dev, u32 *ua)
{
	struct upm6922 *upm = dev_get_drvdata(&chg_dev->dev);
	int ma = 0;
	int ret = 0;

    ret = upm6922_get_chargecurrent(upm, &ma);
    if (ret) {
        pr_err("get ichg fail, ret:%d\n", ret);
        return ret;
    }
    *ua = ma * 1000;

	return ret;
}

/*static int upm6922_ops_set_ichg(struct charger_device *chg_dev, u32 ua)
{
	struct upm6922 *upm = dev_get_drvdata(&chg_dev->dev);
	pr_err("%s ua = %d\n",__func__,ua);
	return upm6922_set_chargecurrent(upm, ua / 1000);
}

static int upm6922_ops_get_icl(struct charger_device *chg_dev, u32 *ua)
{
	struct upm6922 *upm = dev_get_drvdata(&chg_dev->dev);
	int ma = 0;
	int ret = 0;

    ret = upm6922_get_input_current_limit(upm, &ma);
    if (ret) {
        pr_err("get icl fail, ret:%d\n", ret);
        return ret;
    }

    *ua = ma * 1000;

	return ret;
}
*/

static int upm6922_ops_set_icl(struct charger_device *chg_dev, u32 ua)
{
	struct upm6922 *upm = dev_get_drvdata(&chg_dev->dev);
	ua = 1700000;
	pr_err("%s ua =%d ma\n", __func__,ua);
	
	return vote(upm->usb_icl_votable, OPS_SET_VOTER, true, 1700000 / 1000);
}

static int upm6922_ops_get_vchg(struct charger_device *chg_dev, u32 *uv)
{
	struct upm6922 *upm = dev_get_drvdata(&chg_dev->dev);
	int mv;
	int ret;

	ret = upm6922_get_chargevolt(upm, &mv);
    if (ret) {
        pr_err("get charge voltage fail, ret:%d\n", ret);
        return ret;
    }
    *uv = mv * 1000;

	return ret;
}

static int upm6922_ops_set_vchg(struct charger_device *chg_dev, u32 uv)
{
	struct upm6922 *upm = dev_get_drvdata(&chg_dev->dev);

	return upm6922_set_chargevolt(upm, uv / 1000);
}

static int upm6922_ops_kick_wdt(struct charger_device *chg_dev)
{
	struct upm6922 *upm = dev_get_drvdata(&chg_dev->dev);

	return upm6922_reset_watchdog_timer(upm);
}

static int upm6922_ops_set_ivl(struct charger_device *chg_dev, u32 uv)
{
	struct upm6922 *upm = dev_get_drvdata(&chg_dev->dev);

	return upm6922_set_input_volt_limit(upm, uv / 1000);
}

static int upm6922_ops_is_charging_done(struct charger_device *chg_dev, bool *done)
{
	struct upm6922 *upm = dev_get_drvdata(&chg_dev->dev);
	int ret;
	u8 val;

	ret = upm6922_read_byte(upm, UPM6922_REG_08, &val);
	if (!ret) {
		val = val & REG08_CHRG_STAT_MASK;
		val = val >> REG08_CHRG_STAT_SHIFT;
		*done = (val == REG08_CHRG_STAT_CHGDONE);
	}

	return ret;
}

static int upm6922_ops_get_min_ichg(struct charger_device *chg_dev, u32 *ua)
{
	*ua = REG02_ICHG_LSB * 1000;

	return 0;
}

static int upm6922_ops_set_safety_timer(struct charger_device *chg_dev, bool en)
{
	struct upm6922 *upm = dev_get_drvdata(&chg_dev->dev);
	int ret;

	if (en) {
		ret = upm6922_enable_safety_timer(upm);
	} else {
		ret = upm6922_disable_safety_timer(upm);
    }

	return ret;
}

static int upm6922_ops_is_safety_timer_enabled(struct charger_device *chg_dev,
					   bool *en)
{
	struct upm6922 *upm = dev_get_drvdata(&chg_dev->dev);
	int ret;
	u8 reg_val;

	ret = upm6922_read_byte(upm, UPM6922_REG_05, &reg_val);

	if (!ret) {
		*en = !!(reg_val & REG05_EN_TIMER_MASK);
    }

	return ret;
}

static int upm6922_ops_set_otg(struct charger_device *chg_dev, bool en)
{
	int ret;
	struct upm6922 *upm = dev_get_drvdata(&chg_dev->dev);

	if (en) {
		ret = upm6922_enable_otg(upm);
	} else {
		ret = upm6922_disable_otg(upm);
    }

	return ret;
}

static int upm6922_ops_set_boost_ilmt(struct charger_device *chg_dev, u32 ua)
{
	struct upm6922 *upm = dev_get_drvdata(&chg_dev->dev);
	int ret;

	ret = upm6922_set_boost_current(upm, ua / 1000);

	return ret;
}

static const struct charger_properties upm6922_chg_props = {
	.alias_name = "upm6922",
};

static struct charger_ops upm6922_chg_ops = {
	/* Normal charging */
	//.plug_in = upm6922_ops_plug_in,
	//.plug_out = upm6922_ops_plug_out,
	.dump_registers = upm6922_ops_dump_register,
	//.enable = upm6922_ops_charging,
	.is_enabled = upm6922_ops_is_charging_enable,
	.get_charging_current = upm6922_ops_get_ichg,
	//.set_charging_current = upm6922_ops_set_ichg,
	//.get_input_current = upm6922_ops_get_icl,
	//.set_input_current = upm6922_ops_set_icl,
	.get_constant_voltage = upm6922_ops_get_vchg,
	.set_constant_voltage = upm6922_ops_set_vchg,
	.kick_wdt = upm6922_ops_kick_wdt,
	.set_mivr = upm6922_ops_set_ivl,
	.is_charging_done = upm6922_ops_is_charging_done,
	.get_min_charging_current = upm6922_ops_get_min_ichg,

	/* Safety timer */
	.enable_safety_timer = upm6922_ops_set_safety_timer,
	.is_safety_timer_enabled = upm6922_ops_is_safety_timer_enabled,

	/* Power path */
	.enable_powerpath = NULL,
	.is_powerpath_enabled = NULL,

	/* OTG */
	.enable_otg = upm6922_ops_set_otg,
	.set_boost_current_limit = upm6922_ops_set_boost_ilmt,
	.enable_discharge = NULL,

	/* PE+/PE+20 */
	.send_ta_current_pattern = NULL,
	.set_pe20_efficiency_table = NULL,
	.send_ta20_current_pattern = NULL,
	.enable_cable_drop_comp = NULL,

   /* Event */
    .event = NULL,

	/* ADC */
	.get_tchg_adc = NULL,
    .enable_hiz = upm6922_isenable_hiz,
};

static int upm6922_enable_vbus(struct regulator_dev *rdev)
{
	int ret;
	struct upm6922 *upm = rdev_get_drvdata(rdev);

    ret = upm6922_enable_otg(upm);
    pr_err("ret = %d\n", ret);

	return 0;
}

static int upm6922_disable_vbus(struct regulator_dev *rdev)
{
    int ret;
	struct upm6922 *upm = rdev_get_drvdata(rdev);

    ret = upm6922_disable_otg(upm);
    pr_err("ret = %d\n", ret);

	return 0;
}

static int upm6922_is_enabled_vbus(struct regulator_dev *rdev)
{
    int ret;
	struct upm6922 *upm = rdev_get_drvdata(rdev);
	u8 val = 0;

	ret = upm6922_read_byte(upm, UPM6922_REG_01, &val);
    if (ret < 0)
		return ret;

	return (val & REG01_OTG_CONFIG_MASK) >> REG01_OTG_CONFIG_SHIFT;
}

static bool upm6922_is_non_std(struct upm6922 *upm)
{
    int ret = 0;
    u8 chrg_stat = 0;
    u8 chrg_type = 0;

    ret = upm6922_read_byte(upm, UPM6922_REG_08, &chrg_stat);
    if (ret) {
        pr_err("read UPM6922_CHRG_STAT fail\n");
        return false;
    }
    chrg_type = (chrg_stat & REG08_VBUS_STAT_MASK) >> REG08_VBUS_STAT_SHIFT;

    if (chrg_type == REG08_VBUS_TYPE_NON_STD) {
        pr_info("probe charger type: non-std charger\n");
        return true;
    }

    return false;
}

static const struct regulator_ops upm6922_vbus_ops = {
	.enable = upm6922_enable_vbus,
	.disable = upm6922_disable_vbus,
	.is_enabled = upm6922_is_enabled_vbus,
};

static const struct regulator_desc upm6922_otg_rdesc = {
	.of_match = "usb-otg-vbus",
	.name = "usb-otg-vbus",
	.ops = &upm6922_vbus_ops,
	.owner = THIS_MODULE,
	.type = REGULATOR_VOLTAGE,
	.fixed_uV = 5000000,
	.n_voltages = 1,
};

/**************************mtk interface end**************************/

static int upm6922_charger_probe(struct i2c_client *client,
                 const struct i2c_device_id *id)
{
    struct upm6922 *upm;
    struct device_node *node = client->dev.of_node;
    int ret = 0;
    __maybe_unused struct regulator_config config = { };
	struct iio_dev *indio_dev = NULL;
	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(struct upm6922));
	if (!indio_dev) {
		pr_err("Failed to allocate memory\n");
		return -ENOMEM;
	}
	upm = iio_priv(indio_dev);
	upm->indio_dev = indio_dev;
    if (!upm) {
        pr_err("alloc upm6922 struct failed\n");
        return -ENOMEM;
    }

    client->addr = 0x6B;
    upm->dev = &client->dev;
    upm->client = client;

    i2c_set_clientdata(client, upm);
    mutex_init(&upm->i2c_rw_lock);
    mutex_init(&upm->lock);

    ret = upm6922_detect_device(upm);
    if (ret) {
        pr_err("No upm6922 device found!\n");
        ret = -ENODEV;
        goto err_nodev;
    }

    if (upm->part_no != REG0B_PN_UPM6922) {
        pr_err("part no not match, not upm6922!\n");
        ret = -ENODEV;
        goto err_nodev;
    }

    upm->upd = upm6922_parse_dt(node, upm);
    if (!upm->upd) {
        pr_err("No platform data provided.\n");
        ret = -EINVAL;
        goto err_parse_dt;
    }
    upm->wq = create_singlethread_workqueue(dev_name(upm->dev));
    if (!upm->wq) {
        dev_err(upm->dev, "failed to create workqueue\n");
        ret = -ENOMEM;
        goto err_dev_init;
    }

    INIT_DELAYED_WORK(&upm->hvdcp_dwork, upm6922_hvdcp_dwork);
    INIT_DELAYED_WORK(&upm->psy_dwork, upm6922_inform_psy_dwork_handler);
    INIT_DELAYED_WORK(&upm->force_detect_dwork, upm6922_force_detection_dwork_handler);
    INIT_DELAYED_WORK(&upm->charge_detect_delayed_work, charger_detect_work_func);
    INIT_DELAYED_WORK(&upm->td_dwork, upm6922_td_test_work_func);
    INIT_DELAYED_WORK(&upm->prob_dwork, upm6922_inform_prob_dwork_handler);
	INIT_DELAYED_WORK(&upm->disable_afc_dwork, upm6922_disable_afc_dwork);
    upm->monitor_ws = wakeup_source_register(upm->dev, "upm6922_monitor_ws");
    INIT_DELAYED_WORK(&upm->monitor_dwork, upm6922_monitor_dwork_handler);
	INIT_DELAYED_WORK(&upm->disable_pdo_dwork, upm6922_disable_pdo_dwork);
    upm->chrg_psy_stat = POWER_SUPPLY_STATUS_DISCHARGING;
    upm->psy_usb_type = POWER_SUPPLY_USB_TYPE_DCP;
    upm->this_psy_desc.type = POWER_SUPPLY_TYPE_USB_DCP;
    upm->retry_num = 0;
    ret = upm6922_init_device(upm);
    if (ret) {
        pr_err("Failed to init device\n");
        ret = -EINVAL;
        goto err_dev_init;
    }
    upm6922_dump_regs(upm);

    ret = sysfs_create_group(&upm->dev->kobj, &upm6922_attr_group);
    if (ret)
        pr_err("failed to register sysfs. err: %d\n", ret);

    ret = upm6922_charger_psy_register(upm);
    if (ret) {
        pr_err("failed to register chg psy. err: %d\n", ret);
    }

	ret = upm6922_ext_init_iio_psy(upm);
	if (ret < 0) {
		pr_err("Failed to initialize upm6922_ext_init_iio_psy IIO PSY, rc=%d\n", ret);
	}
    upm->chg_dev = charger_device_register(upm->chg_dev_name,
					      &client->dev, upm,
					      &upm6922_chg_ops,
					      &upm6922_chg_props);
	if (IS_ERR_OR_NULL(upm->chg_dev)) {
		ret = PTR_ERR(upm->chg_dev);
		pr_err("register charge device failed, ret:%d\n", ret);
	}

    s_chg_dev_otg = upm->chg_dev;

    /* otg regulator */
	config.dev = upm->dev;
	config.driver_data = upm;
	upm->otg_rdev = devm_regulator_register(upm->dev,
						&upm6922_otg_rdesc, &config);
	if (IS_ERR(upm->otg_rdev)) {
		ret = PTR_ERR(upm->otg_rdev);
		pr_err("register otg regulator failed (%d)\n", ret);
	}

	__upm = upm;
	ret = upm6922_init_iio_psy(upm);
 	if (ret < 0) {
		pr_err("Failed to initialize upm6922 IIO PSY, ret=%d\n", ret);
		goto err_dev_init;
	}
	upm->fcc_votable = create_votable("FCC", VOTE_MIN,
					fcc_vote_callback, upm);
	if (IS_ERR(upm->fcc_votable)) {
		pr_err("fcc_votable is ERROR,goto destroy!\n");
		ret = PTR_ERR(upm->fcc_votable);
		upm->fcc_votable = NULL;
		goto err_free;
	}
	pr_info("fcc_votable create successful !\n");

	upm->chg_dis_votable = create_votable("CHG_DISABLE", VOTE_SET_ANY,
					chg_dis_vote_callback, upm);
	if (IS_ERR(upm->chg_dis_votable)) {
		pr_err("chg_dis_votable is ERROR,goto destroy!\n");
		ret = PTR_ERR(upm->chg_dis_votable);
		upm->chg_dis_votable = NULL;
		goto destroy_votable;
	}
	pr_info("chg_dis_votable create successful !\n");

	upm->fv_votable = create_votable("FV", VOTE_MIN,
					fv_vote_callback, upm);
	if (IS_ERR(upm->fv_votable)) {
		pr_err("fv_votable is ERROR,goto destroy!\n");
		ret = PTR_ERR(upm->fv_votable);
		upm->fv_votable = NULL;
		goto destroy_votable;
	}
	pr_info("fv_votable create successful !\n");

	upm->usb_icl_votable = create_votable("USB_ICL", VOTE_MIN,
					usb_icl_vote_callback,
					upm);
	if (IS_ERR(upm->usb_icl_votable)) {
		pr_err("usb_icl_votable is ERROR,goto destroy!\n");
		ret = PTR_ERR(upm->usb_icl_votable);
		upm->usb_icl_votable = NULL;
		goto destroy_votable;
	}
	pr_info("usb_icl_votable create successful !\n");
	vote(upm->usb_icl_votable, PROFILE_CHG_VOTER, true, CHG_ICL_CURR_MAX);
	vote(upm->chg_dis_votable, BMS_FC_VOTER, false, 0);
    /*Prevent the current exceeding the jeita standard at the moment of poweron*/
    if(upm6922_get_vbus_stat(upm)) {
        /*Maintain LK settings*/
        vote(upm->fcc_votable, JEITA_VOTER, true, max(upm->poweron_fcc_ma,JEITA_MIN_FCC));
    } else {
        vote(upm->fcc_votable, JEITA_VOTER, true, JEITA_MIN_FCC);
    }

    upm6922_register_interrupt(upm);

    if (upm6922_is_non_std(upm)) {
        schedule_delayed_work(&upm->charge_detect_delayed_work,msecs_to_jiffies(1300));
        schedule_delayed_work(&upm->td_dwork,msecs_to_jiffies(1000));
    } else {
        mod_delayed_work(system_wq, &upm->prob_dwork, msecs_to_jiffies(1000));
    }

    enable_irq_wake(upm->client->irq);
    device_init_wakeup(upm->dev, true);
    upm->entry_shipmode = false;
    upm->pre_charging_state = 0;
    upm->cur_charging_state = 0;
    pr_err("upm6922 probe successfully, Part Num:%d, Revision:%d\n!",
           upm->part_no, upm->revision);

    return 0;
destroy_votable:
	pr_err("destory votable !\n");
	destroy_votable(upm->fcc_votable);
	destroy_votable(upm->chg_dis_votable);
	destroy_votable(upm->fv_votable);
	destroy_votable(upm->usb_icl_votable);
err_dev_init:
err_parse_dt:
err_nodev:
err_free:
    mutex_destroy(&upm->i2c_rw_lock);
    //devm_kfree(&client->dev, upm);

    return ret;
}

static int upm6922_charger_remove(struct i2c_client *client)
{
    struct upm6922 *upm = i2c_get_clientdata(client);

    sysfs_remove_group(&upm->dev->kobj, &upm6922_attr_group);
    mutex_destroy(&upm->i2c_rw_lock);

    return 0;

}

static void upm6922_charger_shutdown(struct i2c_client *client)
{
    struct upm6922 *upm = i2c_get_clientdata(client);

    upm6922_set_adc_rate(upm, REG15_CONV_RATE_ONE_SHOT);
    if (upm->entry_shipmode) {
        struct charger_device *chg_dev = get_charger_by_name("charger");
        pr_info("entry shipmode\n");
        if (chg_dev != NULL && chg_dev->ops != NULL && chg_dev->ops->enable_adc) {
            chg_dev->ops->enable_adc(chg_dev, false);
        } else {
            pr_err("cp enable_adc not find\n");
        }
        upm6922_enter_hiz_mode(upm);
        upm6922_en_batfet_delay(upm, false);
        upm6922_disable_batfet(upm);
    }

    pr_info("shutdown\n");
}

static int upm6922_pm_suspend(struct device *dev)
{
    pr_info("suspend\n");

    return 0;
}

static int upm6922_pm_resume(struct device *dev)
{
    pr_info("resume\n");

    return 0;
}

static const struct dev_pm_ops upm6922_pm_ops = {
    .resume = upm6922_pm_resume,
    .suspend = upm6922_pm_suspend,
};

static struct of_device_id upm6922_charger_match_table[] = {
    {.compatible = "up,upm6922_charger",},
    {},
};
MODULE_DEVICE_TABLE(of, upm6922_charger_match_table);

static struct i2c_driver upm6922_charger_driver = {
    .driver = {
        .name = "upm6922-charger",
        .owner = THIS_MODULE,
        .of_match_table = upm6922_charger_match_table,
        .pm = &upm6922_pm_ops,
    },

    .probe = upm6922_charger_probe,
    .remove = upm6922_charger_remove,
    .shutdown = upm6922_charger_shutdown,
};

module_i2c_driver(upm6922_charger_driver);

MODULE_DESCRIPTION("Unisemipower UPM6922 Charger Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("lai.du@unisemipower.com");
