#include "upm6722_reg.h"
#include "upm6722_iio.h"
#include "upm6722.h"
static int upm6722_iio_write_raw(struct iio_dev *indio_dev,
		struct iio_chan_spec const *chan, int val1,
		int val2, long mask)
{
	struct upm6722 *sc = iio_priv(indio_dev);
	int rc = 0;

	switch (chan->channel) {
	case PSY_IIO_CHARGING_ENABLED:
		upm6722_enable_charge(sc, val1);
		upm6722_check_charge_enabled(sc, &sc->charge_enabled);
		pr_err("POWER_SUPPLY_PROP_CHARGING_ENABLED: %s\n",
				val1 ? "enable" : "disable");
		break;
	case PSY_IIO_PRESENT:
		upm6722_set_present(sc, !!val1);
		break;
#if 0
	case PSY_IIO_UPM6722_ENABLE_ADC:
		upm6722_enable_adc(sc, !!val1);
		sc->adc_status = !!val1;
		break;
	case PSY_IIO_UPM6722_ACDRV1_ENABLED:
		upm6722_enable_acdrv1(sc, val1);
		pr_err("POWER_SUPPLY_PROP_ACDRV1_ENABLED: %s\n",
				val1 ? "enable" : "disable");
	case PSY_IIO_UPM6722_ENABLE_OTG:
		upm6722_enable_otg(sc, val1);
		pr_err("POWER_SUPPLY_PROP_OTG_ENABLED: %s\n",
				val1 ? "enable" : "disable");
#endif
	default:
		pr_debug("Unsupported upm6722 IIO chan %d\n", chan->channel);
		rc = -EINVAL;
		break;
	}

	if (rc < 0)
		pr_err("Couldn't write IIO channel %d, rc = %d\n",
			chan->channel, rc);

	return rc;
}

static int upm6722_iio_read_raw(struct iio_dev *indio_dev,
		struct iio_chan_spec const *chan, int *val1,
		int *val2, long mask)
{
	struct upm6722 *sc = iio_priv(indio_dev);
	int result = 0;
	int ret = 0;
	u8 reg_val;
	*val1 = 0;

	switch (chan->channel) {
	case PSY_IIO_CHARGING_ENABLED:
		upm6722_check_charge_enabled(sc, &sc->charge_enabled);
		*val1 = sc->charge_enabled;
		break;
	case PSY_IIO_STATUS:
		*val1 = 0;
		break;
	case PSY_IIO_PRESENT:
		*val1 = 1;
		break;
	case PSY_IIO_UPM6722_BATTERY_PRESENT:
		ret = upm6722_read_byte(sc, UPM6722_REG_15, &reg_val);
		if (!ret)
			sc->batt_present  = !!(reg_val & UPM6722_VOUT_PRESENT_STAT_MASK);
		*val1 = sc->batt_present;
		break;
	case PSY_IIO_UPM6722_VBUS_PRESENT:
		ret = upm6722_read_byte(sc, UPM6722_REG_15, &reg_val);
		if (!ret)
			sc->vbus_present  = !!(reg_val & UPM6722_VBUS_PRESENT_STAT_MASK);
		*val1 = sc->vbus_present;
		break;
	case PSY_IIO_UPM6722_BATTERY_VOLTAGE:
		ret = upm6722_get_adc_data(sc, UPM_ADC_VBAT, &result);
		if (!ret)
			sc->vbat_volt = result;
		*val1 = sc->vbat_volt;
		break;
	case PSY_IIO_UPM6722_BATTERY_CURRENT:
		ret = upm6722_get_adc_data(sc, UPM_ADC_IBAT, &result);
		if (!ret)
			sc->ibat_curr = result;

		*val1 = sc->ibat_curr;
		break;
	case PSY_IIO_UPM6722_BATTERY_TEMPERATURE:
		ret = upm6722_get_adc_data(sc, UPM_ADC_TSBAT, &result);
		if (!ret)
			sc->bat_temp = result;

		*val1 = sc->bat_temp;
		break;
	case PSY_IIO_UPM6722_BUS_VOLTAGE:
		ret = upm6722_get_adc_data(sc, UPM_ADC_VBUS, &result);
		if (!ret)
			sc->vbus_volt = result;

		*val1 = sc->vbus_volt;
		break;
	case PSY_IIO_UPM6722_BUS_CURRENT:
		ret = upm6722_get_adc_data(sc, UPM_ADC_IBUS, &result);
		if (!ret)
			sc->ibus_curr = result;

		*val1 = sc->ibus_curr;
		break;
	case PSY_IIO_UPM6722_BUS_TEMPERATURE:
		ret = upm6722_get_adc_data(sc, UPM_ADC_TSBUS, &result);
		if (!ret)
			sc->bus_temp = result;

		*val1 = sc->bus_temp;
		break;
	case PSY_IIO_UPM6722_DIE_TEMPERATURE:
		ret = upm6722_get_adc_data(sc, UPM_ADC_TDIE, &result);
		if (!ret)
			sc->die_temp = result;

		*val1= sc->die_temp;
		break;

	case PSY_IIO_UPM6722_ALARM_STATUS:
		upm6722_check_alarm_fault_status(sc);
		*val1 = ((sc->bat_ovp_alarm << BAT_OVP_ALARM_SHIFT)
			| (sc->bat_ocp_alarm << BAT_OCP_ALARM_SHIFT)
			| (sc->bat_ucp_alarm << BAT_UCP_ALARM_SHIFT)
			| (sc->bus_ovp_alarm << BUS_OVP_ALARM_SHIFT)
			| (sc->bus_ocp_alarm << BUS_OCP_ALARM_SHIFT)
			| (sc->bat_therm_alarm << BAT_THERM_ALARM_SHIFT)
			| (sc->bus_therm_alarm << BUS_THERM_ALARM_SHIFT)
			| (sc->die_therm_alarm << DIE_THERM_ALARM_SHIFT));
		break;
	case PSY_IIO_UPM6722_FAULT_STATUS:
		upm6722_check_alarm_fault_status(sc);
		*val1 = ((sc->bat_ovp_fault << BAT_OVP_FAULT_SHIFT)
			| (sc->bat_ocp_fault << BAT_OCP_FAULT_SHIFT)
			| (sc->bus_ovp_fault << BUS_OVP_FAULT_SHIFT)
			| (sc->bus_ocp_fault << BUS_OCP_FAULT_SHIFT)
			| (sc->bat_therm_fault << BAT_THERM_FAULT_SHIFT)
			| (sc->bus_therm_fault << BUS_THERM_FAULT_SHIFT)
			| (sc->die_therm_fault << DIE_THERM_FAULT_SHIFT));
		break;
	case PSY_IIO_UPM6722_VBUS_ERROR_STATUS:
		upm6722_check_alarm_fault_status(sc);
		*val1 = sc->vbus_errhi;
		break;
#if 0
	case PSY_IIO_UPM6722_ENABLE_ADC:
		*val1 = sc->adc_status;
		break;
	case PSY_IIO_UPM6722_ACDRV1_ENABLED:
		upm6722_check_enable_acdrv1(sc, &sc->acdrv1_enable);
		*val1 = sc->acdrv1_enable;
		pr_err("check acdrv1 enable: %d\n", *val1);
	case PSY_IIO_UPM6722_ENABLE_OTG:
		upm6722_check_enable_otg(sc, &sc->otg_enable);
		*val1 = sc->otg_enable;
		pr_err("check otg enable: %d\n", *val1);
#endif
	default:
		pr_debug("Unsupported QG IIO chan %d\n", chan->channel);
		ret = -EINVAL;
		break;
	}

	if (ret < 0) {
		pr_err("Couldn't read IIO channel %d, ret = %d\n",
			chan->channel, ret);
		return ret;
	}

	return IIO_VAL_INT;
}

static int upm6722_iio_of_xlate(struct iio_dev *indio_dev,
				const struct of_phandle_args *iiospec)
{
	struct upm6722 *chip = iio_priv(indio_dev);
	struct iio_chan_spec *iio_chan = chip->iio_chan;
	int i;

	for (i = 0; i < ARRAY_SIZE(upm6722_iio_psy_channels);
					i++, iio_chan++) {
		//pr_err("upm6722_iio_of_xlate: iio_chan->channel %d, iiospec->args[0]:%d\n",iio_chan->channel,iiospec->args[0]);
		if (iio_chan->channel == iiospec->args[0])
			return i;
	}
	return -EINVAL;
}

static const struct iio_info sc_iio_info = {
	.read_raw	= upm6722_iio_read_raw,
	.write_raw	= upm6722_iio_write_raw,
	.of_xlate	= upm6722_iio_of_xlate,
};

int upm6722_init_iio_psy(struct upm6722 *chip)
{
	struct iio_dev *indio_dev = chip->indio_dev;
	struct iio_chan_spec *chan;
	int num_iio_channels = ARRAY_SIZE(upm6722_iio_psy_channels);
	int rc, i;

	pr_err("upm6722 upm6722_init_iio_psy start\n");
	chip->iio_chan = devm_kcalloc(chip->dev, num_iio_channels,
				sizeof(*chip->iio_chan), GFP_KERNEL);
	if (!chip->iio_chan)
		return -ENOMEM;

	chip->int_iio_chans = devm_kcalloc(chip->dev,
				num_iio_channels,
				sizeof(*chip->int_iio_chans),
				GFP_KERNEL);
	if (!chip->int_iio_chans)
		return -ENOMEM;

	indio_dev->info = &sc_iio_info;
	indio_dev->dev.parent = chip->dev;
	indio_dev->dev.of_node = chip->dev->of_node;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = chip->iio_chan;
	indio_dev->num_channels = num_iio_channels;
#if 0
	if (chip->mode == upm6722_ROLE_MASTER) {
		indio_dev->name = "upm6722-master";
		for (i = 0; i < num_iio_channels; i++) {
			chip->int_iio_chans[i].indio_dev = indio_dev;
			chan = &chip->iio_chan[i];
			chip->int_iio_chans[i].channel = chan;
			chan->address = i;
			chan->channel = upm6722_iio_psy_channels[i].channel_num;
			chan->type = upm6722_iio_psy_channels[i].type;
			chan->datasheet_name =
				upm6722_iio_psy_channels[i].datasheet_name;
			chan->extend_name =
				upm6722_iio_psy_channels[i].datasheet_name;
			chan->info_mask_separate =
				upm6722_iio_psy_channels[i].info_mask;
		}
	} else if (chip->mode == upm6722_ROLE_SLAVE) {
		indio_dev->name = "upm6722-slave";
		for (i = 0; i < num_iio_channels; i++) {
			chip->int_iio_chans[i].indio_dev = indio_dev;
			chan = &chip->iio_chan[i];
			chip->int_iio_chans[i].channel = chan;
			chan->address = i;
			chan->channel = upm6722_slave_iio_psy_channels[i].channel_num;
			chan->type = upm6722_slave_iio_psy_channels[i].type;
			chan->datasheet_name =
				upm6722_slave_iio_psy_channels[i].datasheet_name;
			chan->extend_name =
				upm6722_slave_iio_psy_channels[i].datasheet_name;
			chan->info_mask_separate =
				upm6722_slave_iio_psy_channels[i].info_mask;
		}
	} else {
#endif
		indio_dev->name = "upm6722-standalone";
		for (i = 0; i < num_iio_channels; i++) {
			chip->int_iio_chans[i].indio_dev = indio_dev;
			chan = &chip->iio_chan[i];
			chip->int_iio_chans[i].channel = chan;
			chan->address = i;
			chan->channel = upm6722_iio_psy_channels[i].channel_num;
			chan->type = upm6722_iio_psy_channels[i].type;
			chan->datasheet_name =
				upm6722_iio_psy_channels[i].datasheet_name;
			chan->extend_name =
				upm6722_iio_psy_channels[i].datasheet_name;
			chan->info_mask_separate =
				upm6722_iio_psy_channels[i].info_mask;
		}
//	}

	rc = devm_iio_device_register(chip->dev, indio_dev);
	if (rc)
		pr_err("Failed to register upm6722 IIO device, rc=%d\n", rc);

	pr_err("upm6722 IIO device, rc=%d\n", rc);
	return rc;
}
