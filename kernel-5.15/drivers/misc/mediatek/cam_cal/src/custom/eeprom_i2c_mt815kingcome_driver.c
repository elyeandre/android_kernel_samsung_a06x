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
#include "eeprom_i2c_mt815kingcome_driver.h"

static struct i2c_client *g_pstI2CclientG;
#define EEPROM_I2C_MSG_SIZE_READ 2
#define EEPROM_I2C_WRITE_MSG_LENGTH_MAX 32
#define MT815_KINGCOME_RET_FAIL 1
#define MT815_KINGCOME_RET_SUCCESS 0
#define MT815_KINGCOME_DEBUG_ON 1

static int otp_group = 0;

static int MT815KINGCOME_Otp_Write_I2C_CAM_CAL_U8(u16 a_u2Addr,u8 parameter)
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
		printk("mt815 I2C write data failed!!\n");
		return MT815_KINGCOME_RET_FAIL;
	}
	mdelay(5);
	return MT815_KINGCOME_RET_SUCCESS;
}
static int MT815KINGCOME_Otp_Read_I2C_CAM_CAL(u16 a_u2Addr)
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
		printk("mt815 I2C read data failed!!\n");
		return -1;
	}
	data = a_puBuff[0];
	return data;
}
static u16 read_cmos_sensor(u16 addr)
{
	u16 get_byte = 0xFF;
    get_byte = MT815KINGCOME_Otp_Read_I2C_CAM_CAL(addr);
	return get_byte;
}

static u16 mt815kingcome_otp_read_byte(u16 addr)
{
	u16 val = 0;
    MT815KINGCOME_Otp_Write_I2C_CAM_CAL_U8(0x0c13, ((addr >> 8) & 0x1F));
    MT815KINGCOME_Otp_Write_I2C_CAM_CAL_U8(0x0c14, addr & 0xFF);
    MT815KINGCOME_Otp_Write_I2C_CAM_CAL_U8(0x0c16, 0x00);
    MT815KINGCOME_Otp_Write_I2C_CAM_CAL_U8(0x0c16, 0x20);

	val = read_cmos_sensor(0x0c1e);
	return val;
}

static u16 mt815kingcome_otp_read_group(u16 addr, u8 *data, u16 length)
{
	//u16 i = 0;
	int i,k,j;
	int block;  
	int last;             
	int total = 0;
	int temp = 0;
    
	block = (length - 1) / 4 + 1;
	last = length % 4;

	for (i = 0; i < block; i++)
    {
        int currentAddr;
		currentAddr = addr + i * 4;
        MT815KINGCOME_Otp_Write_I2C_CAM_CAL_U8(0x0c13, ((currentAddr >> 8) & 0x1F));
        MT815KINGCOME_Otp_Write_I2C_CAM_CAL_U8(0x0c14, currentAddr & 0xFF);
        if ((length - total) < 4) {
            MT815KINGCOME_Otp_Write_I2C_CAM_CAL_U8(0x0c16, 0x00 | (last - 1));
            MT815KINGCOME_Otp_Write_I2C_CAM_CAL_U8(0x0c16, 0x20 | (last - 1));
        }
        else {
            MT815KINGCOME_Otp_Write_I2C_CAM_CAL_U8(0x0c16, 0x43);
            MT815KINGCOME_Otp_Write_I2C_CAM_CAL_U8(0x0c16, 0x63);
        }
    for (k = 0; k < 50; k++)
    {
        temp = read_cmos_sensor(0x0c1a);
        if ((temp & 0x01) == 1)
            break;
        else {
            msleep(1);
            pr_info("status != 1, retry = %d",k); 
        }
    }

    for (j = 0; j < 4; j++)
	
    {
		data[total] = read_cmos_sensor(0x0c1e + j);
		total++;
        if (total >= length) break;
    }
	}
    //kfree(result);
	return MT815_KINGCOME_RET_SUCCESS;
}

static void mt815kingcome_otp_init(void)
{
	MT815KINGCOME_Otp_Write_I2C_CAM_CAL_U8(0x0d01, 0x00);
	MT815KINGCOME_Otp_Write_I2C_CAM_CAL_U8(0x0c00, 0x01);
	MT815KINGCOME_Otp_Write_I2C_CAM_CAL_U8(0x0c19, 0x00);

	mdelay(5);
}

static void mt815kingcome_otp_close(void)
{
	MT815KINGCOME_Otp_Write_I2C_CAM_CAL_U8(0x0c17, 0x01);
	MT815KINGCOME_Otp_Write_I2C_CAM_CAL_U8(0x0c00, 0x00);
}

static int mt815kingcome_Read(unsigned int ui4_offset, unsigned int ui4_length, unsigned char *pinputdata)
{
	int ret = 0;
	int i4ResidueDataLength;
	u32 u4CurrentOffset;
	u8 *pBuff;

	pr_info("ui4_offset = 0x%x, ui4_length = %d \n", ui4_offset, ui4_length);

	i4ResidueDataLength = (int)ui4_length;
	u4CurrentOffset = ui4_offset;
	pBuff = pinputdata;

	ret = mt815kingcome_otp_read_group((u16) u4CurrentOffset, pBuff, i4ResidueDataLength);
	if (ret != 0) {
		pr_info("I2C iReadData failed!!\n");
		return MT815_KINGCOME_RET_FAIL;
	}

	return MT815_KINGCOME_RET_SUCCESS;
}

static bool check_sum(unsigned int ui4_offset,unsigned int ui4_length, unsigned char *pinputdata)
{
	int i, sum = 0;

	for (i = 0; i < ui4_length; i++)
	{
		sum += pinputdata[ui4_offset + i];
	}
    sum = sum % 255 + 1;
	pr_info("sum = 0x%x, ui4_offset = %d\n", sum, ui4_offset);
	if (sum != pinputdata[ui4_offset + i])
	{
		pr_info("checksum fail size = %d sum=0x%x sum-in-eeprom=0x%x", ui4_length, sum, pinputdata[ui4_offset + i]);
		return MT815_KINGCOME_RET_FAIL;
	}

	return MT815_KINGCOME_RET_SUCCESS;
}

static int mt815kingcome_sensor_otp_read_data(unsigned int ui4_offset,unsigned int ui4_length, unsigned char *pinputdata)
{
    unsigned int groupflag1,groupflag2 = 0;

    mt815kingcome_otp_init();
	
    groupflag1 = mt815kingcome_otp_read_byte(0x0700);
	groupflag2 = mt815kingcome_otp_read_byte(0x0e84);

    if(0x01 == groupflag1)
	{
		otp_group = 0;//group 1
		pr_info(" mt815kingcome otp use group1");
	}
	else if(0x01 == groupflag2)
	{
		otp_group = 1;//group 2
		pr_info(" mt815kingcome otp use group2");
	}
	else
	{
		pr_info(" mt815kingcome otp invalid group");
		return MT815_KINGCOME_RET_FAIL;
	}

	if(otp_group == 0){
		mt815kingcome_Read(0x0700, 1924, pinputdata);
	}
	else if(otp_group == 1){
		mt815kingcome_Read(0x0e84, 1924, pinputdata);
	}

    //check sum
	if((check_sum(1, 7, pinputdata) == MT815_KINGCOME_RET_SUCCESS) && (check_sum(10, 25, pinputdata) == MT815_KINGCOME_RET_SUCCESS) && (check_sum(37, 16, pinputdata) == MT815_KINGCOME_RET_SUCCESS) && (check_sum(55, 1868, pinputdata) == MT815_KINGCOME_RET_SUCCESS))
	{
		pr_info("mt815 check sum pass");
	}
	else
	{
		pr_info("mt815 check sum fail");
		mt815kingcome_otp_close();
		return MT815_KINGCOME_RET_FAIL;
	}
    mt815kingcome_otp_close();
	return MT815_KINGCOME_RET_SUCCESS;
}

static int mt815kingcome_iReadData(unsigned int ui4_offset,unsigned int ui4_length, unsigned char *pinputdata)
{
	int ret = MT815_KINGCOME_RET_FAIL;
	ret= mt815kingcome_sensor_otp_read_data(ui4_offset, ui4_length, pinputdata);
	if(ret == MT815_KINGCOME_RET_FAIL)
	{
		pr_info("mt815 iReadData failed!!!\n");
		return MT815_KINGCOME_RET_FAIL;
	}
	pr_info("mt815 iReadData success!!!\n");
	return MT815_KINGCOME_RET_SUCCESS;
}

unsigned int mt815kingcome_otp_read_region(struct i2c_client *client, unsigned int addr,
				unsigned char *data, unsigned int size)
{
	g_pstI2CclientG = client;
	if (mt815kingcome_iReadData(addr, size, data) == 0)
	{
		return size;
	}
	else
	{
		return MT815_KINGCOME_RET_SUCCESS;
	}
}
