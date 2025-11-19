
/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021 The Linux Foundation. All rights reserved.
 */

#ifndef __UPM6722_H__
#define __UPM6722_H__

/*
* upm6722 battery charging driver
*/

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
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/debugfs.h>
#include <linux/bitops.h>
#include <linux/math64.h>

typedef enum {
	CHG_SW_CAP_MODE,
	CHG_BYPASS_MODE,
} chg_mode_t;

typedef enum {
	UPM_ADC_IBUS,
	UPM_ADC_VBUS,
	UPM_ADC_VAC1,
	UPM_ADC_VAC2,
	UPM_ADC_VOUT,
	UPM_ADC_VBAT,
	UPM_ADC_IBAT,
	UPM_ADC_TSBUS,
	UPM_ADC_TSBAT,
	UPM_ADC_TDIE,
	UPM_ADC_MAX,
} adc_channel_t;

#define UPM6722_ROLE_STANDALONE  0
#define UPM6722_ROLE_SLAVE       1
#define UPM6722_ROLE_MASTER      2

enum {
    UPM6722_STDALONE,
    UPM6722_SLAVE,
    UPM6722_MASTER,
};

static int upm6722_mode_data[] = {
    [UPM6722_STDALONE] = UPM6722_ROLE_STANDALONE,
    [UPM6722_SLAVE] = UPM6722_ROLE_SLAVE,
    [UPM6722_MASTER] = UPM6722_ROLE_MASTER,
};

static const u32 upm6722_adc_accuracy_tbl[UPM_ADC_MAX] = {
	250000,	/* IBUS */
	35000,	/* VBUS */
	35000,	/* VAC1 */
    35000,	/* VAC2 */
	20000,	/* VOUT */
	20000,	/* VBAT */
	200000,	/* IBAT */
    0,	/* TSBUS */
    0,	/* TSBAT */
	4,	/* TDIE */
};


#define upm_err(fmt, ...)								\
do {											\
    if (upm->mode == UPM6722_ROLE_MASTER)						\
        printk(KERN_ERR "[UPM6722-MASTER]:%s:" fmt, __func__, ##__VA_ARGS__);	\
    else if (upm->mode == UPM6722_ROLE_SLAVE)					\
        printk(KERN_ERR "[UPM6722-SLAVE]:%s:" fmt, __func__, ##__VA_ARGS__);	\
    else										\
        printk(KERN_ERR "[UPM6722-STANDALONE]:%s:" fmt, __func__, ##__VA_ARGS__);\
} while(0);

#define upm_info(fmt, ...)								\
do {											\
    if (upm->mode == UPM6722_ROLE_MASTER)						\
        printk(KERN_INFO "[UPM6722-MASTER]:%s:" fmt, __func__, ##__VA_ARGS__);	\
    else if (upm->mode == UPM6722_ROLE_SLAVE)					\
        printk(KERN_INFO "[UPM6722-SLAVE]:%s:" fmt, __func__, ##__VA_ARGS__);	\
    else										\
        printk(KERN_INFO "[UPM6722-STANDALONE]:%s:" fmt, __func__, ##__VA_ARGS__);\
} while(0);

#define upm_dbg(fmt, ...)								\
do {											\
    if (upm->mode == UPM6722_ROLE_MASTER)						\
        printk(KERN_DEBUG "[UPM6722-MASTER]:%s:" fmt, __func__, ##__VA_ARGS__);	\
    else if (upm->mode == UPM6722_ROLE_SLAVE)					\
        printk(KERN_DEBUG "[UPM6722-SLAVE]:%s:" fmt, __func__, ##__VA_ARGS__);	\
    else										\
        printk(KERN_DEBUG "[UPM6722-STANDALONE]:%s:" fmt, __func__, ##__VA_ARGS__);\
} while(0);

struct upm6722_cfg {
	bool bat_ovp_disable;
	bool bat_ocp_disable;
	bool bus_ucp_disable;
	bool bus_rcp_disable;
	bool vout_ovp_disable;
	bool tdie_flt_disable;
	bool tsbus_flt_disable;
	bool tsbat_flt_disable;
	bool wdt_disable;
	bool vbus_errhi_disable;

	int bat_ovp_th;
	int bat_ocp_th;
	int bus_ovp_th;
	int bus_ocp_th;
	int bus_ucp_th;
	int bus_rcp_th;
	int vac1_ovp_th;
	int vac2_ovp_th;
	int vout_ovp_th;
	int tdie_flt_th;
	int tsbus_flt_th;
	int tsbat_flt_th;

	bool bat_ovp_mask;
	bool bat_ocp_mask;
	bool bus_ovp_mask;
	bool bus_ocp_mask;
	bool bus_ucp_mask;
	bool bus_rcp_mask;
	bool vout_ovp_mask;
	bool vac1_ovp_mask;
	bool vac2_ovp_mask;

	bool vout_present_mask;
	bool vac1_present_mask;
	bool vac2_present_mask;
	bool vbus_present_mask;
	bool acrb1_config_mask;
	bool acrb2_config_mask;
	bool cfly_short_mask;
	bool adc_done_mask;
	bool ss_timeout_mask;
	bool tsbus_flt_mask;
	bool tsbat_flt_mask;
	bool tdie_flt_mask;
	bool wd_mask;
	bool regn_good_mask;
	bool conv_active_mask;
	bool vbus_errhi_mask;

	bool bat_ovp_alm_disable;
	bool bat_ocp_alm_disable;
	bool bat_ucp_alm_disable;
	bool bus_ovp_alm_disable;
	bool tdie_alm_disable;

	int bat_ovp_alm_th;
	int bat_ocp_alm_th;
	int bat_ucp_alm_th;
	int bus_ovp_alm_th;
	int bus_ocp_alm_th;
	int tdie_alm_th;

	bool bat_ovp_alm_mask;
	bool bat_ocp_alm_mask;
	bool bat_ucp_alm_mask;
	bool bus_ovp_alm_mask;
	bool bus_ocp_alm_mask;
	bool tsbus_tsbat_alm_mask;
	bool tdie_alm_mask;

	bool bus_pd_en;
	bool vac1_pd_en;
	bool vac2_pd_en;

	int sense_r_mohm;
	int ss_timeout;
	int wdt_set;
	bool chg_config_1;
	int fsw_set;
	int freq_shift;
	int ibus_ucp_fall_dg_sel;

	bool adc_enable;
	int adc_rate;
	int adc_avg;
	int adc_avg_init;
	int adc_sample;
	bool ibus_adc_disable;
	bool vbus_adc_disable;
	bool vac1_adc_disable;
	bool vac2_adc_disable;
	bool vout_adc_disable;
	bool vbat_adc_disable;
	bool ibat_adc_disable;
	bool tsbus_adc_disable;
	bool tsbat_adc_disable;
	bool tdie_adc_disable;
};

struct upm6722 {
    struct device *dev;
    struct i2c_client *client;

    int part_no;
    int revision;

    int mode;

    struct mutex data_lock;
    struct mutex i2c_rw_lock;
    struct mutex irq_complete;

    bool irq_waiting;
    bool irq_disabled;
    bool resume_completed;

    bool usb_present;
    bool charge_enabled;	/* Register bit status */

    int irq_gpio;
    int irq;

    /* ADC reading */
    int vbat_volt;
    int vbus_volt;
    int vout_volt;
    int vac1_volt;
	int vac2_volt;

    int ibat_curr;
    int ibus_curr;

	int bus_temp;
	int bat_temp;
    int die_temp;
	bool batt_present;
	bool bat_ovp_flt;
	bool vout_ovp_flt;
	bool bat_ocp_flt;
	bool bus_ovp_flt;
	bool bus_ocp_flt;
	bool bus_ucp_flt;
	bool bus_rcp_flt;
	bool cfly_short_flt;
	bool vac1_ovp_flt;
	bool vac2_ovp_flt;
	bool tsbus_flt;
	bool tsbat_flt;
	bool tdie_flt;

	bool bat_ovp_alm;
	bool bat_ocp_alm;
	bool bat_ucp_alm;
	bool bus_ovp_alm;
	bool bus_ocp_alm;
	bool tsbus_tsbat_alm;
	bool tdie_alm;
	bool bat_ovp_alarm;
	bool bat_ocp_alarm;
	bool bat_ucp_alarm;
	bool bus_ovp_alarm;
	bool bus_ocp_alarm;
	bool bat_therm_alarm;
	bool bus_therm_alarm;
	bool die_therm_alarm;
	bool bat_ovp_fault;
	bool bat_ocp_fault;
	bool bus_ovp_fault;
	bool bus_ocp_fault;
	bool bat_therm_fault;
	bool bus_therm_fault;
	bool die_therm_fault;
	
	bool vout_present;
	bool vac1_present;
	bool vac2_present;
	bool vbus_present;
	bool acrb1_config;
	bool acrb2_config;

	bool adc_done;
	bool ss_timeout;
	bool wd_stat;
	bool regn_good;
	bool conv_active;
	bool vbus_errhi;

    bool vbat_reg;
    bool ibat_reg;

	int  prev_alarm;
	int  prev_fault;

    struct upm6722_cfg *cfg;

    int skip_writes;
    int skip_reads;

    struct upm6722_platform_data *platform_data;

    struct delayed_work monitor_work;

    struct dentry *debug_root;

    struct power_supply_desc psy_desc;
    struct power_supply_config psy_cfg;
    struct power_supply *fc2_psy;

	struct charger_device *chg_dev;
	struct regulator_dev *otg_rdev;
	const char *chg_dev_name;
	struct iio_dev          *indio_dev;
	struct iio_chan_spec    *iio_chan;
	struct iio_channel	*int_iio_chans;
};
int upm6722_enable_charge(struct upm6722 *upm, bool enable);
int upm6722_check_charge_enabled(struct upm6722 *upm, bool *enable);
int upm6722_set_present(struct upm6722 *upm, bool present);
int upm6722_read_byte(struct upm6722 *upm, u8 reg, u8 *data);
void upm6722_check_alarm_fault_status(struct upm6722 *upm);
int upm6722_get_adc_data(struct upm6722 *upm, adc_channel_t channel,  int *result);
void upm6722_check_alarm_fault_status(struct upm6722 *upm);

#define BAT_OVP_FAULT_SHIFT         0
#define BAT_OCP_FAULT_SHIFT         1
#define BUS_OVP_FAULT_SHIFT         2
#define BUS_OCP_FAULT_SHIFT         3
#define BAT_THERM_FAULT_SHIFT       4
#define BUS_THERM_FAULT_SHIFT       5
#define DIE_THERM_FAULT_SHIFT       6

#define BAT_OVP_FAULT_MASK          (1 << BAT_OVP_FAULT_SHIFT)
#define BAT_OCP_FAULT_MASK          (1 << BAT_OCP_FAULT_SHIFT)
#define BUS_OVP_FAULT_MASK          (1 << BUS_OVP_FAULT_SHIFT)
#define BUS_OCP_FAULT_MASK          (1 << BUS_OCP_FAULT_SHIFT)
#define BAT_THERM_FAULT_MASK        (1 << BAT_THERM_FAULT_SHIFT)
#define BUS_THERM_FAULT_MASK        (1 << BUS_THERM_FAULT_SHIFT)
#define DIE_THERM_FAULT_MASK        (1 << DIE_THERM_FAULT_SHIFT)

#define BAT_OVP_ALARM_SHIFT         0
#define BAT_OCP_ALARM_SHIFT         1
#define BUS_OVP_ALARM_SHIFT         2
#define BUS_OCP_ALARM_SHIFT         3
#define BAT_THERM_ALARM_SHIFT       4
#define BUS_THERM_ALARM_SHIFT       5
#define DIE_THERM_ALARM_SHIFT       6
#define BAT_UCP_ALARM_SHIFT         7

#define BAT_OVP_ALARM_MASK          (1 << BAT_OVP_ALARM_SHIFT)
#define BAT_OCP_ALARM_MASK          (1 << BAT_OCP_ALARM_SHIFT)
#define BUS_OVP_ALARM_MASK          (1 << BUS_OVP_ALARM_SHIFT)
#define BUS_OCP_ALARM_MASK          (1 << BUS_OCP_ALARM_SHIFT)
#define BAT_THERM_ALARM_MASK        (1 << BAT_THERM_ALARM_SHIFT)
#define BUS_THERM_ALARM_MASK        (1 << BUS_THERM_ALARM_SHIFT)
#define DIE_THERM_ALARM_MASK        (1 << DIE_THERM_ALARM_SHIFT)
#define BAT_UCP_ALARM_MASK          (1 << BAT_UCP_ALARM_SHIFT)

#define VBAT_REG_STATUS_SHIFT       0
#define IBAT_REG_STATUS_SHIFT       1

#define VBAT_REG_STATUS_MASK        (1 << VBAT_REG_STATUS_SHIFT)
#define IBAT_REG_STATUS_MASK        (1 << VBAT_REG_STATUS_SHIFT)



#endif /* __UPM6722_H__ */

