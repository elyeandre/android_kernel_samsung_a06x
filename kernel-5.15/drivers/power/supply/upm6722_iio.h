
/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021 The Linux Foundation. All rights reserved.
 */

#ifndef __UPM6722_IIO_H
#define __UPM6722_IIO_H

#include <linux/iio/iio.h>
#include <linux/iio/consumer.h>
#include <linux/qti_power_supply.h>
#include <dt-bindings/iio/qti_power_supply_iio.h>
#include "upm6722.h"
struct upm6722_iio_channels {
	const char *datasheet_name;
	int channel_num;
	enum iio_chan_type type;
	long info_mask;
};

#define upm6722_IIO_CHAN(_name, _num, _type, _mask)		\
	{						\
		.datasheet_name = _name,		\
		.channel_num = _num,			\
		.type = _type,				\
		.info_mask = _mask,			\
	},

#define upm6722_CHAN_VOLT(_name, _num)			\
	upm6722_IIO_CHAN(_name, _num, IIO_VOLTAGE,		\
		BIT(IIO_CHAN_INFO_PROCESSED))

#define upm6722_CHAN_CUR(_name, _num)			\
	upm6722_IIO_CHAN(_name, _num, IIO_CURRENT,		\
		BIT(IIO_CHAN_INFO_PROCESSED))

#define upm6722_CHAN_TEMP(_name, _num)			\
	upm6722_IIO_CHAN(_name, _num, IIO_TEMP,		\
		BIT(IIO_CHAN_INFO_PROCESSED))

#define upm6722_CHAN_POW(_name, _num)			\
	upm6722_IIO_CHAN(_name, _num, IIO_POWER,		\
		BIT(IIO_CHAN_INFO_PROCESSED))

#define upm6722_CHAN_ENERGY(_name, _num)			\
	upm6722_IIO_CHAN(_name, _num, IIO_ENERGY,		\
		BIT(IIO_CHAN_INFO_PROCESSED))

#define upm6722_CHAN_COUNT(_name, _num)			\
	upm6722_IIO_CHAN(_name, _num, IIO_COUNT,		\
		BIT(IIO_CHAN_INFO_PROCESSED))

static const struct upm6722_iio_channels upm6722_iio_psy_channels[] = {
	upm6722_CHAN_ENERGY("present", PSY_IIO_PRESENT)
	upm6722_CHAN_ENERGY("charging_enabled", PSY_IIO_CHARGING_ENABLED)
	upm6722_CHAN_ENERGY("status", PSY_IIO_STATUS)
	upm6722_CHAN_ENERGY("upm6722_battery_present", PSY_IIO_UPM6722_BATTERY_PRESENT)
	upm6722_CHAN_ENERGY("upm6722_vbus_present", PSY_IIO_UPM6722_VBUS_PRESENT)
	upm6722_CHAN_VOLT("upm6722_battery_voltage", PSY_IIO_UPM6722_BATTERY_VOLTAGE)
	upm6722_CHAN_CUR("upm6722_battery_current", PSY_IIO_UPM6722_BATTERY_CURRENT)
	upm6722_CHAN_TEMP("upm6722_battery_temperature", PSY_IIO_UPM6722_BATTERY_TEMPERATURE)
	upm6722_CHAN_VOLT("upm6722_bus_voltage", PSY_IIO_UPM6722_BUS_VOLTAGE)
	upm6722_CHAN_CUR("upm6722_bus_current", PSY_IIO_UPM6722_BUS_CURRENT)
	upm6722_CHAN_TEMP("upm6722_bus_temperature", PSY_IIO_UPM6722_BUS_TEMPERATURE)
	upm6722_CHAN_TEMP("upm6722_die_temperature", PSY_IIO_UPM6722_DIE_TEMPERATURE)
	upm6722_CHAN_ENERGY("upm6722_alarm_status", PSY_IIO_UPM6722_ALARM_STATUS)
	upm6722_CHAN_ENERGY("upm6722_fault_status", PSY_IIO_UPM6722_FAULT_STATUS)
	upm6722_CHAN_ENERGY("upm6722_vbus_error_status", PSY_IIO_UPM6722_VBUS_ERROR_STATUS)
//	upm6722_CHAN_ENERGY("upm6722_enable_adc", PSY_IIO_UPM6722_ENABLE_ADC)
//	upm6722_CHAN_ENERGY("upm6722_enable_acdrv1", PSY_IIO_UPM6722_ACDRV1_ENABLED)
//	upm6722_CHAN_ENERGY("upm6722_enable_otg", PSY_IIO_UPM6722_ENABLE_OTG)
};

static const struct upm6722_iio_channels upm6722_slave_iio_psy_channels[] = {
	upm6722_CHAN_ENERGY("upm6722_present_slave", PSY_IIO_PRESENT)
	upm6722_CHAN_ENERGY("upm6722_charging_enabled_slave", PSY_IIO_CHARGING_ENABLED)
	upm6722_CHAN_ENERGY("upm6722_status_slave", PSY_IIO_STATUS)
	upm6722_CHAN_ENERGY("upm6722_battery_present_slave", PSY_IIO_UPM6722_BATTERY_PRESENT)
	upm6722_CHAN_ENERGY("upm6722_vbus_present_slave", PSY_IIO_UPM6722_VBUS_PRESENT)
	upm6722_CHAN_VOLT("upm6722_battery_voltage_slave", PSY_IIO_UPM6722_BATTERY_VOLTAGE)
	upm6722_CHAN_CUR("upm6722_battery_current_slave", PSY_IIO_UPM6722_BATTERY_CURRENT)
	upm6722_CHAN_TEMP("upm6722_battery_temperature_slave", PSY_IIO_UPM6722_BATTERY_TEMPERATURE)
	upm6722_CHAN_VOLT("upm6722_bus_voltage_slave", PSY_IIO_UPM6722_BUS_VOLTAGE)
	upm6722_CHAN_CUR("upm6722_bus_current_slave", PSY_IIO_UPM6722_BUS_CURRENT)
	upm6722_CHAN_TEMP("upm6722_bus_temperature_slave", PSY_IIO_UPM6722_BUS_TEMPERATURE)
	upm6722_CHAN_TEMP("upm6722_die_temperature_slave", PSY_IIO_UPM6722_DIE_TEMPERATURE)
	upm6722_CHAN_ENERGY("upm6722_alarm_status_slave", PSY_IIO_UPM6722_ALARM_STATUS)
	upm6722_CHAN_ENERGY("upm6722_fault_status_slave", PSY_IIO_UPM6722_FAULT_STATUS)
	upm6722_CHAN_ENERGY("upm6722_vbus_error_status_slave", PSY_IIO_UPM6722_VBUS_ERROR_STATUS)
	//upm6722_CHAN_ENERGY("upm6722_enable_adc_slave", PSY_IIO_UPM6722_ENABLE_ADC)
	//upm6722_CHAN_ENERGY("upm6722_enable_acdrv1_slave", PSY_IIO_UPM6722_ACDRV1_ENABLED)
	//upm6722_CHAN_ENERGY("upm6722_enable_otg_slave", PSY_IIO_UPM6722_ENABLE_OTG)
};

int upm6722_init_iio_psy(struct upm6722 *chip);

#endif /* __UPM6722_IIO_H */
