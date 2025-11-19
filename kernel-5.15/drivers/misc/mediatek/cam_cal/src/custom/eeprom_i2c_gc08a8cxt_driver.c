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
#include "eeprom_i2c_gc08a8cxt_driver.h"

static struct i2c_client *g_pstI2CclientG;
#define EEPROM_I2C_MSG_SIZE_READ 2
#define EEPROM_I2C_WRITE_MSG_LENGTH_MAX 32
#define GC08A8_CXT_RET_FAIL 1
#define GC08A8_CXT_RET_SUCCESS 0
#define GC08A8_CXT_DEBUG_ON 0

static int otp_group = 0;

static int GC08A8CXT_Otp_Write_I2C_CAM_CAL_U8(u16 a_u2Addr,u8 parameter)
{
	int i4RetValue = 0;
	char puCmd[3];
	struct i2c_msg msg;
	puCmd[0] = (char)(a_u2Addr >> 8);
	puCmd[1] = (char)(a_u2Addr & 0xFF);
	puCmd[2] = (char)(parameter & 0xFF);
	msg.addr = g_pstI2CclientG->addr;
	msg.flags = g_pstI2CclientG->flags & I2C_M_TEN;
	msg.len = 3;
	msg.buf = puCmd;

	i4RetValue = i2c_transfer(g_pstI2CclientG->adapter, &msg, 1);

	if (i4RetValue != 1) {
		printk("gc08a8 I2C write data failed!!\n");
		return GC08A8_CXT_RET_FAIL;
	}
	mdelay(5);
	return GC08A8_CXT_RET_SUCCESS;
}
static int GC08A8CXT_Otp_Read_I2C_CAM_CAL(u16 a_u2Addr)
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
		printk("gc08a8 I2C read data failed!!\n");
		return -1;
	}
	data = a_puBuff[0];
	return data;
}
static u16 read_cmos_sensor(u16 addr)
{
	u16 get_byte = 0xFF;
    get_byte = GC08A8CXT_Otp_Read_I2C_CAM_CAL(addr);

	return get_byte;
}

static u16 gc08a8cxt_otp_read_byte(u16 addr)
{
	u16 val = 0;

	GC08A8CXT_Otp_Write_I2C_CAM_CAL_U8(0x0313, 0x00);
	GC08A8CXT_Otp_Write_I2C_CAM_CAL_U8(0x0a69, (addr >> 8) & 0xff);
	GC08A8CXT_Otp_Write_I2C_CAM_CAL_U8(0x0a6a, addr & 0xff);
	GC08A8CXT_Otp_Write_I2C_CAM_CAL_U8(0x0313, 0x20);

	val = read_cmos_sensor(0x0a6c);

	return val;
}

static u16 gc08a8cxt_otp_read_group(u16 addr, u8 *data, u16 length)
{
	u16 i = 0;

	GC08A8CXT_Otp_Write_I2C_CAM_CAL_U8(0x0313, 0x00);
	GC08A8CXT_Otp_Write_I2C_CAM_CAL_U8(0x0a69, (addr >> 8) & 0xff);
	GC08A8CXT_Otp_Write_I2C_CAM_CAL_U8(0x0a6a, addr & 0xff);
	GC08A8CXT_Otp_Write_I2C_CAM_CAL_U8(0x0313, 0x20);
	GC08A8CXT_Otp_Write_I2C_CAM_CAL_U8(0x0313, 0x12);

	for (i = 0; i < length; i++) {
		data[i] = read_cmos_sensor(0x0a6c);
#if GC08A8_CXT_DEBUG_ON
	pr_info("addr = 0x%x, data = 0x%x\n", addr + i * 8, data[i]);
#endif
	}
	return GC08A8_CXT_RET_SUCCESS;
}

static void gc08a8cxt_otp_init(void)
{
	GC08A8CXT_Otp_Write_I2C_CAM_CAL_U8(0x031c, 0x60);
	GC08A8CXT_Otp_Write_I2C_CAM_CAL_U8(0x0315, 0x80);

	GC08A8CXT_Otp_Write_I2C_CAM_CAL_U8(0x0324, 0x42);    
	GC08A8CXT_Otp_Write_I2C_CAM_CAL_U8(0x0316, 0x09);    
	GC08A8CXT_Otp_Write_I2C_CAM_CAL_U8(0x0a67, 0x80);    
	GC08A8CXT_Otp_Write_I2C_CAM_CAL_U8(0x0313, 0x00);    
	GC08A8CXT_Otp_Write_I2C_CAM_CAL_U8(0x0a53, 0x0e);//04
	GC08A8CXT_Otp_Write_I2C_CAM_CAL_U8(0x0a65, 0x17);    
	GC08A8CXT_Otp_Write_I2C_CAM_CAL_U8(0x0a68, 0xA1);    
	GC08A8CXT_Otp_Write_I2C_CAM_CAL_U8(0x0a47, 0x00);    
	GC08A8CXT_Otp_Write_I2C_CAM_CAL_U8(0x0a58, 0x00);    
	GC08A8CXT_Otp_Write_I2C_CAM_CAL_U8(0x0ace, 0x0c);    
	mdelay(10);//add
}

static void gc08a8cxt_otp_close(void)
{
	GC08A8CXT_Otp_Write_I2C_CAM_CAL_U8(0x0316, 0x01);
	GC08A8CXT_Otp_Write_I2C_CAM_CAL_U8(0x0a67, 0x00);
}

static int gc08a8cxt_Read(unsigned int ui4_offset, unsigned int ui4_length, unsigned char *pinputdata)
{
	int ret = 0;
	int i4ResidueDataLength;
	u32 u4CurrentOffset;
	u8 *pBuff;


	pr_info("ui4_offset = 0x%x, ui4_length = %d \n", ui4_offset, ui4_length);

	i4ResidueDataLength = (int)ui4_length;
	u4CurrentOffset = ui4_offset;
	pBuff = pinputdata;

	ret = gc08a8cxt_otp_read_group((u16) u4CurrentOffset, pBuff, i4ResidueDataLength);
	if (ret != 0) {
		pr_info("I2C iReadData failed!!\n");
		return GC08A8_CXT_RET_FAIL;
	}


	return GC08A8_CXT_RET_SUCCESS;
}

static bool check_sum(unsigned int ui4_offset,unsigned int ui4_length, unsigned char *pinputdata)
{
	int i, sum = 0;

	for (i = 0; i < ui4_length; i++)
	{
		sum += pinputdata[ui4_offset + i];
		//LOG_INF("buf[%d] = 0x%x %d", i, buf[i], buf[i]);
	}
    sum = sum % 255 +1;
	if (sum != pinputdata[ui4_offset + i])
	{
		pr_info("chksum fail size = %d sum=0x%x sum-in-eeprom=0x%x", ui4_length, sum, pinputdata[ui4_offset + i]);
		return GC08A8_CXT_RET_FAIL;
	}

	return GC08A8_CXT_RET_SUCCESS;
}

static int gc08a8cxt_sensor_otp_read_data(unsigned int ui4_offset,unsigned int ui4_length, unsigned char *pinputdata)
{
	int ret = GC08A8_CXT_RET_FAIL;
    unsigned int groupflag = 0;
    unsigned int readLength = 1920;

    gc08a8cxt_otp_init();

    groupflag = gc08a8cxt_otp_read_byte(0x15A0);
    if(0x1 == groupflag)
	{
		otp_group = 0;//group 1
		pr_info(" gc08a8cxt otp use group1");
	}
	else if(0x7 == groupflag)
	{
		otp_group = 1;//group 2
		pr_info(" gc08a8cxt otp use group2");
	}
	else if(0x1F == groupflag)
	{
		otp_group = 2;//group 3
		pr_info(" gc08a8cxt otp use group3");
	}
	else
	{
		pr_info(" gc08a8cxt otp invalid group");
		return ret;
	}

	pr_info(" gc08a8cxt read lengh %d",  ui4_length);
	if(ui4_length < 1920)
	   readLength = ui4_length;

    ret = gc08a8cxt_Read(0x15A8 + 0x3C00 * otp_group, readLength, pinputdata);
    //check sum
	if(check_sum(0, 7, pinputdata) == check_sum(34, 16, pinputdata) == check_sum(51, 1868, pinputdata) == GC08A8_CXT_RET_SUCCESS)
	{
		pr_info("gc08a8 check sum pass");
	}
	else
	{
		ret = GC08A8_CXT_RET_FAIL;
		pr_info("gc08a8 check sum fail");
	}
    gc08a8cxt_otp_close();
	return ret;
}

static int gc08a8cxt_iReadData(unsigned int ui4_offset,unsigned int ui4_length, unsigned char *pinputdata)
{
	int ret = GC08A8_CXT_RET_FAIL;
	ret= gc08a8cxt_sensor_otp_read_data(ui4_offset, ui4_length, pinputdata);
	if(ret == GC08A8_CXT_RET_FAIL)
	{
		pr_info("gc08a8 iReadData failed!!!\n");
		return GC08A8_CXT_RET_FAIL;
	}
	pr_info("gc08a8 iReadData success!!!\n");
	return GC08A8_CXT_RET_SUCCESS;
}

unsigned int gc08a8cxt_otp_read_region(struct i2c_client *client, unsigned int addr,
				unsigned char *data, unsigned int size)
{
	g_pstI2CclientG = client;
	if (gc08a8cxt_iReadData(addr, size, data) == 0)
	{
		return size;
	}
	else
	{
		return GC08A8_CXT_RET_SUCCESS;
	}
}
