/*
 * Copyright (C) 2016 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */
#define PFX "CAM_CAL"
#define pr_fmt(fmt) PFX "[%s] " fmt, __func__


#define PFX "CAM_CAL"
#define pr_fmt(fmt) PFX "[%s] " fmt, __func__


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/of.h>
#include "cam_cal.h"
#include "cam_cal_define.h"
#include <linux/dma-mapping.h>
#ifdef CONFIG_COMPAT
/* 64 bit */
#include <linux/fs.h>
#include <linux/compat.h>
#endif
#include "eeprom_i2c_sc202csunimage_driver.h"

static struct i2c_client *g_pstI2CclientG;
#define EEPROM_I2C_MSG_SIZE_READ 2
#define EEPROM_I2C_WRITE_MSG_LENGTH_MAX 32
#define SC202CS_UNIMAGE_RET_FAIL 1
#define SC202CS_UNIMAGE_RET_SUCCESS 0
#define SC202CS_UNIMAGE_DEBUG_ON 0

static int SC202CS_UNIMAGE_Otp_Read_I2C_CAM_CAL(u16 a_u2Addr)
{
	int i4RetValue = 0;
	u16 data;
	char puReadCmd[2] = { (char)(a_u2Addr >> 8), (char)(a_u2Addr & 0xFF) };
	struct i2c_msg msg[EEPROM_I2C_MSG_SIZE_READ];
    u8 a_puBuff[1];
	msg[0].addr = g_pstI2CclientG->addr;
	msg[0].flags = g_pstI2CclientG->flags & I2C_M_TEN;
	msg[0].len = 2;
	msg[0].buf = puReadCmd;

	msg[1].addr = g_pstI2CclientG->addr;
	msg[1].flags = g_pstI2CclientG->flags & I2C_M_TEN;
	msg[1].flags |= I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = a_puBuff;

	i4RetValue = i2c_transfer(g_pstI2CclientG->adapter, msg,
				EEPROM_I2C_MSG_SIZE_READ);

	if (i4RetValue != EEPROM_I2C_MSG_SIZE_READ) {
		printk("SC202CS I2C read data failed!!\n");
		return -1;
	}
	data = a_puBuff[0];
	return data;
}

static u16 read_cmos_sensor(u16 addr)
{
	u16 get_byte = 0xFF;
    get_byte = SC202CS_UNIMAGE_Otp_Read_I2C_CAM_CAL(addr);

	return get_byte;
}

static int sc202csunimage_sensor_otp_read_data(unsigned int ui4_offset,unsigned int ui4_length, unsigned char *pinputdata)
{
	int ret = SC202CS_UNIMAGE_RET_SUCCESS;
	uint64_t Startaddress = 0x8000;
	int length = 0;
	int i;
#if SC202CS_UNIMAGE_DEBUG_ON
	int j;
#endif

	for(i = 0;i < 12; i++)
	{
		pinputdata[length++] = read_cmos_sensor(Startaddress++);

	}
	Startaddress = 0x8011;
	for(i = 0;i < 11; i++)
	{
		
		pinputdata[length++] = read_cmos_sensor(Startaddress);
	}

	pinputdata[length++] =  read_cmos_sensor(0x801D);
	pinputdata[length++] =  read_cmos_sensor(0x801F);
#if SC202CS_UNIMAGE_DEBUG_ON
	for(j = 0;j < length;j++)
	{
		pr_info("sc202cs data[%d]=0x%x", j, pinputdata[j]);
	}
#endif
	return ret;
}

static int sc202csunimage_iReadData(unsigned int ui4_offset,unsigned int ui4_length, unsigned char *pinputdata)
{
	int ret = SC202CS_UNIMAGE_RET_FAIL;
	ret= sc202csunimage_sensor_otp_read_data(ui4_offset, ui4_length, pinputdata);
	if(ret == SC202CS_UNIMAGE_RET_FAIL)
	{
		pr_info("sc202cs iReadData failed!!!\n");
		return SC202CS_UNIMAGE_RET_FAIL;
	}
	pr_info("sc202cs iReadData success!!!\n");
	return SC202CS_UNIMAGE_RET_SUCCESS;
}

unsigned int sc202csunimage_otp_read_region(struct i2c_client *client, unsigned int addr,
				unsigned char *data, unsigned int size)
{
	g_pstI2CclientG = client;
	if (sc202csunimage_iReadData(addr, size, data) == 0)
	{
		return size;
	}
	else
	{
		return SC202CS_UNIMAGE_RET_SUCCESS;
	}
}
