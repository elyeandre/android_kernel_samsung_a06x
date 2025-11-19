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
#include "eeprom_i2c_sc820cstruly_driver.h"

static struct i2c_client *g_pstI2CclientG;
#define EEPROM_I2C_MSG_SIZE_READ 2
#define EEPROM_I2C_WRITE_MSG_LENGTH_MAX 32
#define SC820CS_TRULY_RET_FAIL 1
#define SC820CS_TRULY_RET_SUCCESS 0
#define SC820CS_TRULY_PAGE2 2
#define SC820CS_TRULY_DEBUG_ON 0
static int otp_group = 0;
static int SC820CS_TRULY_Otp_Read_I2C_CAM_CAL(u16 a_u2Addr)
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
		printk("SC820CS I2C read data failed!!\n");
		return -1;
	}
	data = a_puBuff[0];
	return data;
}

static int SC820CSTRULY_Otp_Write_I2C_CAM_CAL_U8(u16 a_u2Addr,u8 parameter)
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
		printk("SC820CS I2C write data failed!!\n");
		return -1;
	}
	mdelay(5);
	return 0;
}
static u16 read_cmos_sensor(u16 addr)
{
	u16 get_byte = 0xFF;
    get_byte = SC820CS_TRULY_Otp_Read_I2C_CAM_CAL(addr);

	return get_byte;
}

static int sc820cs_set_threshold(u8 threshold)//set thereshold
{
	int threshold_reg1[3] ={ 0x48,0x48,0x48};
	int threshold_reg2[3] ={ 0x38,0x18,0x58};
	int threshold_reg3[3] ={ 0x41,0x41,0x41};
	SC820CSTRULY_Otp_Write_I2C_CAM_CAL_U8(0x36b0,threshold_reg1[threshold]);
	SC820CSTRULY_Otp_Write_I2C_CAM_CAL_U8(0x36b1,threshold_reg2[threshold]);
	SC820CSTRULY_Otp_Write_I2C_CAM_CAL_U8(0x36b2,threshold_reg3[threshold]);
	pr_info("sc820cs_set_threshold %d\n", threshold);
	return SC820CS_TRULY_RET_SUCCESS;
}

static int sc820cs_set_page_and_load_data(int page)//set page
{
	uint64_t Startaddress = 0;
	uint64_t EndAddress = 0;
	int delay = 0;
	int pag = 0;
	Startaddress = page * 0x200 + 0x7E00;//set start address in page
	EndAddress = Startaddress + 0x1ff;//set end address in page
	pag = page*2 -1;//change page
	SC820CSTRULY_Otp_Write_I2C_CAM_CAL_U8(0x4408,(Startaddress>>8)&0xff);  //set start address in high 8 bit
	SC820CSTRULY_Otp_Write_I2C_CAM_CAL_U8(0x4409,Startaddress&0xff); //set start address in low 8 bit
	SC820CSTRULY_Otp_Write_I2C_CAM_CAL_U8(0x440a,(EndAddress>>8)&0xff); //set end address in high 8 bit
	SC820CSTRULY_Otp_Write_I2C_CAM_CAL_U8(0x440b,EndAddress&0xff); //set end address in low 8 bit


	SC820CSTRULY_Otp_Write_I2C_CAM_CAL_U8(0x4401,0x13); // address set finished
	SC820CSTRULY_Otp_Write_I2C_CAM_CAL_U8(0x4412,pag&0xff); // set page
	SC820CSTRULY_Otp_Write_I2C_CAM_CAL_U8(0x4407,0x00);// set page finished
	SC820CSTRULY_Otp_Write_I2C_CAM_CAL_U8(0x4400,0x11);// manual load begin
	while((read_cmos_sensor(0x4420)&0x01) == 0x01)// wait for manual load finished
	{
		delay++;
		pr_info("sc820cs_set_page waitting, OTP is still busy for loading %d times\n", delay);
		if(delay == 10)
		{
			pr_info("sc820cs_set_page fail, load timeout!!!\n");
			return SC820CS_TRULY_RET_FAIL;
		}
		mdelay(10);
	}
	pr_info("sc820cs_set_page:%d success\n", pag);
	return SC820CS_TRULY_RET_SUCCESS;
}

//read
static int sc820cstruly_sensor_otp_read(unsigned int ui4_length, unsigned char *pinputdata, int page)
{
	int i;
	uint64_t Startaddress = 0;
	//uint64_t EndAddress = 0;
	unsigned int ui4_lsc_offset = 0;
	//unsigned int ui4_lsc_length = 0;
	Startaddress = page * 0x200 + 0x7E00;//set start address in page
	//EndAddress = Startaddress + 0x1ff;//set end address in page
	ui4_lsc_offset = Startaddress+122;

	for(i =0; i<ui4_length; i++)
	{
		pinputdata[i] = read_cmos_sensor(ui4_lsc_offset + i);
		#if SC820CS_TRULY_DEBUG_ON
		pr_info("sc820cstruly_sensor_otp_read addr=0x%x, data=0x%x\n", ui4_lsc_offset + i, pinputdata[i]);
		#endif
	}
	pr_info("sc820cstruly_sensor_otp_read success\n");

	return SC820CS_TRULY_RET_SUCCESS;
}

static int sc820cstruly_sensor_otp_checksum_module_info(unsigned int ui4_offset,unsigned int ui4_length, unsigned char *pinputdata)
{
    int ret = SC820CS_TRULY_RET_FAIL;
    int i = 0;
    unsigned int checksum = 0;
    unsigned int checksum_cal = 0;
    for(;i < ui4_length; i++)
	{
		checksum_cal += pinputdata[ui4_offset + i];
	}
    checksum_cal = checksum_cal%255+1;
    if(pinputdata[ui4_offset + ui4_length] == checksum_cal)
    {
        pr_info("sc820cstruly_sensor_otp_checksum_module_info pass in group:%d\n", otp_group+1);
        ret = SC820CS_TRULY_RET_SUCCESS;
    }
    else{
        pr_info("sc820cstruly_sensor_otp_checksum_module_info fail in group:%d, checksum_cal:0x%x,checksum:0x%x!\n", otp_group+1, checksum_cal, checksum);
    }
    return ret;
}

static int sc820cstruly_sensor_otp_checksum_awb(unsigned int ui4_offset,unsigned int ui4_length, unsigned char *pinputdata)
{
    int ret = SC820CS_TRULY_RET_FAIL;
    int i = 0;
    unsigned int checksum = 0;
    unsigned int checksum_cal = 0;
    for(;i < ui4_length; i++)
	{
		checksum_cal += pinputdata[ui4_offset + i];
	}
    checksum_cal = checksum_cal%255+1;
    if(pinputdata[ui4_offset + ui4_length] == checksum_cal)
    {
        pr_info("sc820cstruly_sensor_otp_checksum_awb pass in group:%d\n", otp_group+1);
        ret = SC820CS_TRULY_RET_SUCCESS;
    }
    else{
        pr_info("sc820cstruly_sensor_otp_checksum_awb fail in group:%d, checksum_cal:0x%x,checksum:0x%x!\n", otp_group+1, checksum_cal, checksum);
    }
    return ret;
}

static int sc820cstruly_sensor_otp_checksum_lsc(unsigned int ui4_offset,unsigned int ui4_length, unsigned char *pinputdata)
{
    int ret = SC820CS_TRULY_RET_FAIL;
    int i = 0;
    unsigned int checksum = 0;
    unsigned int checksum_cal = 0;
    for(;i < ui4_length; i++)
	{
		checksum_cal += pinputdata[ui4_offset + i];
	}
    checksum_cal = checksum_cal%255+1;
    if(pinputdata[ui4_offset + ui4_length] == checksum_cal)
    {
        pr_info("sc820cstruly_sensor_otp_checksum_lsc pass in group:%d\n", otp_group+1);
        ret = SC820CS_TRULY_RET_SUCCESS;
    }
    else{
        pr_info("sc820cstruly_sensor_otp_checksum_lsc fail in group:%d, checksum_cal:0x%x,checksum:0x%x!\n", otp_group+1, checksum_cal, checksum);
    }
    return ret;
}

static int sc820cstruly_sensor_otp_checksum(unsigned char *pinputdata)
{
	int ret = SC820CS_TRULY_RET_SUCCESS;

    if(sc820cstruly_sensor_otp_checksum_module_info(1+(otp_group)*1920, 7, pinputdata) == SC820CS_TRULY_RET_FAIL)
    {
		ret = SC820CS_TRULY_RET_FAIL;
        pr_info("sc820cstruly_sensor_otp_checksum for module info fail");
    }
    if(sc820cstruly_sensor_otp_checksum_awb(35+(otp_group)*1920, 16, pinputdata) == SC820CS_TRULY_RET_FAIL)
    {
		ret = SC820CS_TRULY_RET_FAIL;
        pr_info("sc820cstruly_sensor_otp_checksum for awb fail");
    }
    if(sc820cstruly_sensor_otp_checksum_lsc(52+(otp_group)*1920, 1868, pinputdata) == SC820CS_TRULY_RET_FAIL)
    {
		ret = SC820CS_TRULY_RET_FAIL;
        pr_info("sc820cstruly_sensor_otp_checksum for lsc fail");
    }
    return ret;
}

static int sc820cstruly_sensor_otp_read_all_data(unsigned char *pinputdata)
{
	int ret = SC820CS_TRULY_RET_FAIL;
	int page = SC820CS_TRULY_PAGE2;

	pr_info("sc820cstruly_sensor_otp_read_all_data begin!\n");

    for(;page <= 10;page++)
	{
		sc820cs_set_page_and_load_data(page);//set page--3,4,5,6,7,8,9,10 && load data
		ret = sc820cstruly_sensor_otp_read(0x186,pinputdata+(page-2)*0x186, page);
	}

    sc820cs_set_page_and_load_data(page);//set page11
	ret = sc820cstruly_sensor_otp_read(0x14B,pinputdata+(page-2)*0x186, page);

    if(pinputdata[0] == 0x1) // group1
	{
        pr_info("sc820cs_sensor_otp use group1!\n");
        otp_group = 0;//group 1
	}
	else if(pinputdata[0] == 0x13) // group2
	{
        pr_info("sc820cs_sensor_otp use group2!\n");
        otp_group = 1;//group 2
	}
	else
	{
		pr_info("This otp belong to group NULL\n");
		return SC820CS_TRULY_RET_FAIL;
	}
	ret = sc820cstruly_sensor_otp_checksum(pinputdata);

	pr_info("sc820cstruly_sensor_otp_read_all_data end!\n");
    return ret;
}

static int sc820cstruly_sensor_otp_read_data(unsigned int ui4_offset,unsigned int ui4_length, unsigned char *pinputdata)
{
	int ret = SC820CS_TRULY_RET_FAIL;
	int threshold = 0;
    int delay = 0;

    //read module info to judge the otp data which group
	for(threshold=0; threshold<3; threshold++)
	{
		sc820cs_set_threshold(threshold);

		pr_info("sc820cs read data in treshold R%d\n", threshold);
		ret = sc820cstruly_sensor_otp_read_all_data(pinputdata);
		if(ret == SC820CS_TRULY_RET_FAIL)
		{
			pr_info("sc820cs read all data in treshold R%d fail\n", threshold);
			continue;
		}
		else
		{
			pr_info("sc820cs read  all data in treshold R%d success\n", threshold);
			break;
		}
	}

	if(ret == SC820CS_TRULY_RET_FAIL)
	{
		pr_info("sc820cs read lsc info in treshold R1 R2 R3 all failed!!!\n");
		return ret;
	}
	pr_info("read sc820cs otp data success\n");

	SC820CSTRULY_Otp_Write_I2C_CAM_CAL_U8(0x4408,0x80);
	SC820CSTRULY_Otp_Write_I2C_CAM_CAL_U8(0x4409,0x00);
	SC820CSTRULY_Otp_Write_I2C_CAM_CAL_U8(0x440a,0x81);
	SC820CSTRULY_Otp_Write_I2C_CAM_CAL_U8(0x440b,0xff);

    SC820CSTRULY_Otp_Write_I2C_CAM_CAL_U8(0x4401,0x13);
	SC820CSTRULY_Otp_Write_I2C_CAM_CAL_U8(0x4412,0x1);
	SC820CSTRULY_Otp_Write_I2C_CAM_CAL_U8(0x4407,0x0e);
	SC820CSTRULY_Otp_Write_I2C_CAM_CAL_U8(0x4400, 0x11);
	while((read_cmos_sensor(0x4420)&0x01) == 0x01)// wait for manual load finished
	{
		delay++;
		pr_info("sc820cs_set_page waitting, OTP is still busy for loading %d times\n", delay);
		if(delay == 10)
		{
			pr_info("sc820cs_set_page fail, load timeout!!!\n");
			return SC820CS_TRULY_RET_FAIL;
		}
		mdelay(10);
	}

	return ret;
}

static int sc820cstruly_iReadData(unsigned int ui4_offset,unsigned int ui4_length, unsigned char *pinputdata)
{
	int ret = SC820CS_TRULY_RET_FAIL;
	ret= sc820cstruly_sensor_otp_read_data(ui4_offset, ui4_length, pinputdata);
	if(ret == SC820CS_TRULY_RET_FAIL)
	{
		pr_info("sc820cs iReadData failed!!!\n");
		return SC820CS_TRULY_RET_FAIL;
	}
	pr_info("sc820cs iReadData success!!!\n");
	return SC820CS_TRULY_RET_SUCCESS;
}

unsigned int sc820cstruly_otp_read_region(struct i2c_client *client, unsigned int addr,
				unsigned char *data, unsigned int size)
{
	g_pstI2CclientG = client;
	if (sc820cstruly_iReadData(addr, size, data) == 0)
	{
		return size;
	}
	else
	{
		return SC820CS_TRULY_RET_SUCCESS;
	}
}
