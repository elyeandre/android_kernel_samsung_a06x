#ifndef __SGM41542S_CHG_IIO_H
#define __SGM41542S_CHG_IIO_H

#include <linux/qti_power_supply.h>
#include <linux/iio/iio.h>
#include <dt-bindings/iio/qti_power_supply_iio.h>

const char sgm41542S_driver_name[] = "sgm41542S";

struct sgm41542S_iio_channels {
	const char *datasheet_name;
	int channel_num;
	enum iio_chan_type type;
	long info_mask;
};

enum sgm41542S_iio_type {
	DS28E16,
	BMS,
	NOPMI,
};

#define sgm41542S_IIO_CHAN(_name, _num, _type, _mask)		\
	{						\
		.datasheet_name = _name,		\
		.channel_num = _num,			\
		.type = _type,				\
		.info_mask = _mask,			\
	},

#define SGM41542S_CHAN_ENERGY(_name, _num)			\
	sgm41542S_IIO_CHAN(_name, _num, IIO_CURRENT,		\
	BIT(IIO_CHAN_INFO_PROCESSED))

static const struct sgm41542S_iio_channels SGM41542S_iio_psy_channels[] = {
	SGM41542S_CHAN_ENERGY("sgm_charge_type", PSY_IIO_CHARGE_TYPE)
	SGM41542S_CHAN_ENERGY("sgm_charge_enabled", PSY_IIO_CHARGE_ENABLED)
	SGM41542S_CHAN_ENERGY("sgm_charge_disable", PSY_IIO_CHARGE_DISABLE)
	// SGM41542S_CHAN_ENERGY("sgm_charge_done", PSY_IIO_CHARGE_DONE)
	// SGM41542S_CHAN_ENERGY("sgm_charge_ic_type", PSY_IIO_CHARGE_IC_TYPE)
	// SGM41542S_CHAN_ENERGY("sgm_charge_pd_active", PSY_IIO_PD_ACTIVE)
	SGM41542S_CHAN_ENERGY("sgm_charge_usb_ma", PSY_IIO_USB_MA)
	SGM41542S_CHAN_ENERGY("sgm_charge_afc", PSY_IIO_CHARGE_AFC)
	SGM41542S_CHAN_ENERGY("sgm_charge_afc_disable", PSY_IIO_CHARGE_AFC_DISABLE)
	SGM41542S_CHAN_ENERGY("sgm_resistance_id", PSY_IIO_RESISTANCE_ID)
};

enum fg_ext_iio_channels {
	FG_FASTCHARGE_MODE,
	FG_MONITOR_WORK,
};

static const char * const fg_ext_iio_chan_name[] = {
	[FG_FASTCHARGE_MODE] = "fastcharge_mode",
	[FG_MONITOR_WORK] = "fg_monitor_work",
};

enum nopmi_chg_ext_iio_channels {
	NOPMI_CHG_MTBF_CUR,
	NOPMI_CHG_USB_REAL_TYPE,
	NOPMI_CHG_PD_ACTIVE,
	NOPMI_CHG_TYPEC_CC_ORIENTATION,
	NOPMI_CHG_TYPEC_MODE,
	NOPMI_CHG_INPUT_SUSPEND,
	NOPMI_CHG_FFC_DISABLE,
	NOPMI_CHG_CHARGER_AFC,
};


static const char * const nopmi_chg_ext_iio_chan_name[] = {
	[NOPMI_CHG_MTBF_CUR] = "sgm_mtbf_cur",
	[NOPMI_CHG_USB_REAL_TYPE] = "sgm_usb_real_type",
	[NOPMI_CHG_PD_ACTIVE] = "sgm_pd_active",
	[NOPMI_CHG_TYPEC_CC_ORIENTATION] = "sgm_typec_cc_orientation",
	[NOPMI_CHG_TYPEC_MODE] = "sgmtypec_mode",
	[NOPMI_CHG_INPUT_SUSPEND] = "sgm_input_suspend",
	[NOPMI_CHG_FFC_DISABLE] = "sgm_ffc_disable",
	[NOPMI_CHG_CHARGER_AFC] = "sgm_charge_afc",
};

enum ds_ext_iio_channels {
	DS_CHIP_OK,
};

static const char * const ds_ext_iio_chan_name[] = {
	[DS_CHIP_OK] = "ds_chip_ok",
};

enum main_iio_channels {
	MAIN_CHARGING_ENABLED,
	MAIN_CHARGER_TYPE,
};

static const char * const main_iio_chan_name[] = {
	[MAIN_CHARGING_ENABLED] = "sgm_charge_enabled",
	[MAIN_CHARGER_TYPE] = "sgm_charge_type",
};

#endif
