// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */
#include <linux/backlight.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <drm/drm_modes.h>
#include <linux/delay.h>
#include <drm/drm_connector.h>
#include <drm/drm_device.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#define CONFIG_MTK_PANEL_EXT
#if defined(CONFIG_MTK_PANEL_EXT)
#include "../mediatek/mediatek_v2/mtk_panel_ext.h"
#include "../mediatek/mediatek_v2/mtk_drm_graphics_base.h"
#endif
#include "../mediatek/mediatek_v2/mtk_corner_pattern/mtk_data_hw_roundedpattern.h"
#include "../../../misc/mediatek/gate_ic/gate_i2c.h"
#include "../bias/bias.h"
/* enable this to check panel self -bist pattern */
/* #define PANEL_BIST_PATTERN */
/****************TPS65132***********/
static char bl_tb0[] = {0x51,0xFF,0x0F};
static struct class *lcd_class;
static struct device *lcd_device;
static dev_t lcd_devno;
extern int double_click_flag;
extern int lcm_i2c_write_bytes(u8 reg , u8 val);
//LCM porch
#define clk               136492  //H total *V total * V freq
#define data_rate_value   900   //H total *V total * total_bit_per_pixel*frame_per_second/total_lane_num
#define pll_clock         450
#define H_display   720
#define HSA         4
#define HBP         48
#define HFP         30
#define V_display   1600
#define VSA         6
#define VBP         30
#define VFP_60HZ    1203
#define VFP_90HZ    255
#define DYN_PLL_CLK 450
#define DYN_DATA_RATE 900
#define DYN_HFP     30
//TO DO: You have to do that remove macro BYPASSI2C and solve build error
//otherwise voltage will be unstable
#define BYPASSI2C   //iic  disable

struct xinli {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *bias_pos;
	struct gpio_desc *bias_neg;
	bool prepared;
	bool enabled;
	//unsigned int gate_ic;
	int error;
};
#define xinli_dcs_write_seq(ctx, seq...)                              \
	({                                                                 \
		const u8 d[] = { seq };                                        \
		BUILD_BUG_ON_MSG(ARRAY_SIZE(d) > 64,                           \
				 "DCS sequence too big for stack");                    \
		xinli_dcs_write(ctx, d, ARRAY_SIZE(d));                       \
	})
#define xinli_dcs_write_seq_static(ctx, seq...)                       \
	({                                                                 \
		static const u8 d[] = { seq };                                 \
		xinli_dcs_write(ctx, d, ARRAY_SIZE(d));                       \
	})
static inline struct xinli *panel_to_xinli(struct drm_panel *panel)
{
	return container_of(panel, struct xinli, panel);
}
#ifdef PANEL_SUPPORT_READBACK
static int xinli_dcs_read(struct xinli *ctx, u8 cmd, void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;
	if (ctx->error < 0)
		return 0;
	ret = mipi_dsi_dcs_read(dsi, cmd, data, len);
	if (ret < 0) {
		dev_info(ctx->dev, "error %d reading dcs seq:(%#x)\n", ret,
			 cmd);
		ctx->error = ret;
	}
	return ret;
}
static void xinli_panel_get_data(struct xinli *ctx)
{
	u8 buffer[3] = { 0 };
	static int ret;
	pr_info("%s+\n", __func__);
	if (ret == 0) {
		ret = xinli_dcs_read(ctx, 0x0A, buffer, 1);
		pr_info("%s 0x%08x\n", __func__, buffer[0] | (buffer[1] << 8));
		dev_info(ctx->dev, "return %d data(0x%08x) to dsi engine\n",
			ret, buffer[0] | (buffer[1] << 8));
	}
}
#endif
static void xinli_dcs_write(struct xinli *ctx, const void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;
	char *addr;
	if (ctx->error < 0)
		return;
	addr = (char *)data;
	if ((int)*addr < 0xB0)
		{
		ret = mipi_dsi_dcs_write_buffer(dsi, data, len);
		//msleep(3);
		}
	else
		{
		ret = mipi_dsi_generic_write(dsi, data, len);
		//msleep(3);
		}
	if (ret < 0) {
		dev_info(ctx->dev, "error %zd writing seq: %ph\n", ret, data);
		ctx->error = ret;
	}
}
static void xinli_panel_init(struct xinli *ctx)
{
	xinli_dcs_write_seq_static(ctx,0x00,0x00);
	xinli_dcs_write_seq_static(ctx,0xFF,0x80,0x57,0x01);

	xinli_dcs_write_seq_static(ctx,0x00,0x80);
	xinli_dcs_write_seq_static(ctx,0xFF,0x80,0x57);

	xinli_dcs_write_seq_static(ctx,0x00,0x00);
	xinli_dcs_write_seq_static(ctx,0x2A,0x00,0x00,0x02,0xCF);//720

	xinli_dcs_write_seq_static(ctx,0x00,0x00);
	xinli_dcs_write_seq_static(ctx,0x2B,0x00,0x00,0x06,0x3F);//1600

	xinli_dcs_write_seq_static(ctx,0x00,0xA3);//Y=1600
	xinli_dcs_write_seq_static(ctx,0xB3,0x06,0x40,0x00,0x18);

	xinli_dcs_write_seq_static(ctx,0x00,0x93);//VGH_N16V
	xinli_dcs_write_seq_static(ctx,0xC5,0x61);

	xinli_dcs_write_seq_static(ctx,0x00,0x97);//VGH_I16V
	xinli_dcs_write_seq_static(ctx,0xC5,0x61);

	xinli_dcs_write_seq_static(ctx,0x00,0x9A);//VGL_N-11V
	xinli_dcs_write_seq_static(ctx,0xC5,0x37);

	xinli_dcs_write_seq_static(ctx,0x00,0x9C);//VGL_I-11V
	xinli_dcs_write_seq_static(ctx,0xC5,0x37);

	xinli_dcs_write_seq_static(ctx,0x00,0xB6);//VGHO1_N_I=15v
	xinli_dcs_write_seq_static(ctx,0xC5,0x4D,0x4D);

	xinli_dcs_write_seq_static(ctx,0x00,0xB8);//VGLO1_N_I-10V
	xinli_dcs_write_seq_static(ctx,0xC5,0x2D,0x2D);

	xinli_dcs_write_seq_static(ctx,0x00,0x00);
	xinli_dcs_write_seq_static(ctx,0xD8,0x33,0x37);//GVDDP/N5.4V-5.6V

	xinli_dcs_write_seq_static(ctx,0x00,0x82);
	xinli_dcs_write_seq_static(ctx,0xC5,0x55);//LVD

	xinli_dcs_write_seq_static(ctx,0x00,0x83);
	xinli_dcs_write_seq_static(ctx,0xC5,0x07);//LVDEnable

	xinli_dcs_write_seq_static(ctx,0x00,0x96);
	xinli_dcs_write_seq_static(ctx,0xf5,0x00);

	xinli_dcs_write_seq_static(ctx,0x00,0x86);
	xinli_dcs_write_seq_static(ctx,0xf5,0x19);

	xinli_dcs_write_seq_static(ctx,0x00,0x94);
	xinli_dcs_write_seq_static(ctx,0xC5,0x12);

	xinli_dcs_write_seq_static(ctx,0x00,0x9B);
	xinli_dcs_write_seq_static(ctx,0xC5,0x21);

	xinli_dcs_write_seq_static(ctx,0x00,0xA3);//GVDD_EN
	xinli_dcs_write_seq_static(ctx,0xA5,0x04);

	xinli_dcs_write_seq_static(ctx,0x00,0x99);
	xinli_dcs_write_seq_static(ctx,0xCF,0x56);

	xinli_dcs_write_seq_static(ctx,0x00,0x86);//I2CEN
	xinli_dcs_write_seq_static(ctx,0xB7,0x80);

	xinli_dcs_write_seq_static(ctx,0x00,0xA5);
	xinli_dcs_write_seq_static(ctx,0xB0,0x1D);//RCdelay95ns

	xinli_dcs_write_seq_static(ctx,0x00,0x90);//MIPI4lane
	xinli_dcs_write_seq_static(ctx,0xFF,0x00);

	xinli_dcs_write_seq_static(ctx,0x00,0x80);
	xinli_dcs_write_seq_static(ctx,0xCE,0x00);

	xinli_dcs_write_seq_static(ctx,0x00,0xD0);
	xinli_dcs_write_seq_static(ctx,0xCE,0x01);

	xinli_dcs_write_seq_static(ctx,0x00,0xE0);
	xinli_dcs_write_seq_static(ctx,0xCE,0x00);

	xinli_dcs_write_seq_static(ctx,0x00,0xA1);
	xinli_dcs_write_seq_static(ctx,0xC1,0xCC);

	xinli_dcs_write_seq_static(ctx,0x00,0xA5);
	xinli_dcs_write_seq_static(ctx,0xC1,0x00,0x10);

	xinli_dcs_write_seq_static(ctx,0x00,0x70);
	xinli_dcs_write_seq_static(ctx,0xC0,0x00,0xA1,0x00,0xFE,0x00,0x24);

	xinli_dcs_write_seq_static(ctx,0x00,0xD1);
	xinli_dcs_write_seq_static(ctx,0xCE,0x00,0x0A,0x01,0x01,0x00,0xEF,0x01);

	xinli_dcs_write_seq_static(ctx,0x00,0xE8);
	xinli_dcs_write_seq_static(ctx,0xCE,0x00,0xEF,0x00,0xEF);

	xinli_dcs_write_seq_static(ctx,0x00,0xB0);
	xinli_dcs_write_seq_static(ctx,0xC0,0x00,0xA1,0x00,0xFF,0x24);

	xinli_dcs_write_seq_static(ctx,0x00,0x80);
	xinli_dcs_write_seq_static(ctx,0xC1,0x00,0x00);

	xinli_dcs_write_seq_static(ctx,0x00,0x90);
	xinli_dcs_write_seq_static(ctx,0xC1,0x01);

	xinli_dcs_write_seq_static(ctx,0x00,0xF1);
	xinli_dcs_write_seq_static(ctx,0xCF,0x5A);

	xinli_dcs_write_seq_static(ctx,0x00,0xF5);
	xinli_dcs_write_seq_static(ctx,0xCF,0x01);

	xinli_dcs_write_seq_static(ctx,0x00,0xF6);
	xinli_dcs_write_seq_static(ctx,0xCF,0x5A);

	xinli_dcs_write_seq_static(ctx,0x00,0xF7);
	xinli_dcs_write_seq_static(ctx,0xCF,0x11);

	xinli_dcs_write_seq_static(ctx,0x00,0x70);
	xinli_dcs_write_seq_static(ctx,0xCF,0x06,0x06,0x68,0x6C,0x02,0x02,0x3E,0x42);

	xinli_dcs_write_seq_static(ctx,0x00,0xC0);
	xinli_dcs_write_seq_static(ctx,0xCF,0x04,0x04,0x16,0x1A);

	xinli_dcs_write_seq_static(ctx,0x00,0xC5);
	xinli_dcs_write_seq_static(ctx,0xCF,0x06,0x06,0x45,0x49);

	xinli_dcs_write_seq_static(ctx,0x00,0x80);
	xinli_dcs_write_seq_static(ctx,0xCC,0x1E,0x1E,0x17,0x17,0x1E,0x1E,0x0D,0x0D,0x0B,0x0B,0x09,0x09,0x07,0x07,0x03,0x03);

	xinli_dcs_write_seq_static(ctx,0x00,0x90);
	xinli_dcs_write_seq_static(ctx,0xCC,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);

	xinli_dcs_write_seq_static(ctx,0x00,0x80);
	xinli_dcs_write_seq_static(ctx,0xCD,0x1E,0x1E,0x16,0x16,0x1E,0x1E,0x0C,0x0C,0x0A,0x0A,0x08,0x08,0x06,0x06,0x02,0x02);

	xinli_dcs_write_seq_static(ctx,0x00,0x90);
	xinli_dcs_write_seq_static(ctx,0xCD,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);

	xinli_dcs_write_seq_static(ctx,0x00,0xA0);
	xinli_dcs_write_seq_static(ctx,0xCC,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x22,0x22,0x26,0x26,0x02,0x08,0x06);

	xinli_dcs_write_seq_static(ctx,0x00,0xB0);
	xinli_dcs_write_seq_static(ctx,0xCC,0x10,0x0E,0x0C,0x0A,0x1C,0x1C,0x00,0x00);

	xinli_dcs_write_seq_static(ctx,0x00,0xA0);
	xinli_dcs_write_seq_static(ctx,0xCD,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x26,0x23,0x23,0x26,0x26,0x03,0x09,0x07);

	xinli_dcs_write_seq_static(ctx,0x00,0xB0);
	xinli_dcs_write_seq_static(ctx,0xCD,0x11,0x0F,0x0D,0x0B,0x1C,0x1C,0x00,0x00);

	xinli_dcs_write_seq_static(ctx,0x00,0x80);
	xinli_dcs_write_seq_static(ctx,0xCB,0xC0,0xC1,0xC0,0xC1,0xC1,0xC1,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0);
	xinli_dcs_write_seq_static(ctx,0x00,0xED);
	xinli_dcs_write_seq_static(ctx,0xCB,0xCD);

	xinli_dcs_write_seq_static(ctx,0x00,0x90);
	xinli_dcs_write_seq_static(ctx,0xCB,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
	xinli_dcs_write_seq_static(ctx,0x00,0xEE);
	xinli_dcs_write_seq_static(ctx,0xCB,0x00);
	xinli_dcs_write_seq_static(ctx,0x00,0x90);
	xinli_dcs_write_seq_static(ctx,0xC3,0x00);

	xinli_dcs_write_seq_static(ctx,0x00,0xA0);
	xinli_dcs_write_seq_static(ctx,0xCB,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);

	xinli_dcs_write_seq_static(ctx,0x00,0xB0);
	xinli_dcs_write_seq_static(ctx,0xCB,0x00,0x00,0x00,0x00);

	xinli_dcs_write_seq_static(ctx,0x00,0xC0);
	xinli_dcs_write_seq_static(ctx,0xCB,0x10,0x54,0x00,0x00);

	xinli_dcs_write_seq_static(ctx,0x00,0xD2);
	xinli_dcs_write_seq_static(ctx,0xCB,0x83,0x00,0x83,0x83,0x00,0x83,0x83,0x00,0x83,0x83,0x00,0x83,0x83,0x00);

	xinli_dcs_write_seq_static(ctx,0x00,0xE0);
	xinli_dcs_write_seq_static(ctx,0xCB,0x83,0x83,0x00,0x83,0x83,0x00,0x83,0x83,0x00,0x83,0x83,0x00,0x83);

	xinli_dcs_write_seq_static(ctx,0x00,0x68);
	xinli_dcs_write_seq_static(ctx,0xC2,0x86,0x03,0x00,0x00);

	xinli_dcs_write_seq_static(ctx,0x00,0x6C);
	xinli_dcs_write_seq_static(ctx,0xC2,0x85,0x03,0x00,0x00);

	xinli_dcs_write_seq_static(ctx,0x00,0x78);
	xinli_dcs_write_seq_static(ctx,0xC2,0x01,0x03,0x00,0x00);

	xinli_dcs_write_seq_static(ctx,0x00,0x7C);
	xinli_dcs_write_seq_static(ctx,0xC2,0x02,0x03,0x00,0x00);

	xinli_dcs_write_seq_static(ctx,0x00,0x8C);
	xinli_dcs_write_seq_static(ctx,0xC2,0x82,0x02,0x03,0x00,0xA0);

	xinli_dcs_write_seq_static(ctx,0x00,0x91);
	xinli_dcs_write_seq_static(ctx,0xC2,0x81,0x03,0x03,0x00,0xA0);

	xinli_dcs_write_seq_static(ctx,0x00,0x96);
	xinli_dcs_write_seq_static(ctx,0xC2,0x80,0x04,0x03,0x00,0xA0);

	xinli_dcs_write_seq_static(ctx,0x00,0x9B);
	xinli_dcs_write_seq_static(ctx,0xC2,0x01,0x05,0x03,0x00,0xA0);

	xinli_dcs_write_seq_static(ctx,0x00,0xA0);
	xinli_dcs_write_seq_static(ctx,0xC2,0x02,0x06,0x03,0x00,0xA0);

	xinli_dcs_write_seq_static(ctx,0x00,0xA5);
	xinli_dcs_write_seq_static(ctx,0xC2,0x03,0x07,0x03,0x00,0xA0);

	xinli_dcs_write_seq_static(ctx,0x00,0xAA);
	xinli_dcs_write_seq_static(ctx,0xC2,0x04,0x08,0x03,0x00,0xA0);

	xinli_dcs_write_seq_static(ctx,0x00,0xAF);
	xinli_dcs_write_seq_static(ctx,0xC2,0x05,0x09,0x03,0x00,0xA0);

	xinli_dcs_write_seq_static(ctx,0x00,0xDC);
	xinli_dcs_write_seq_static(ctx,0xC2,0x77,0x77,0x77,0x77);

	xinli_dcs_write_seq_static(ctx,0x00,0x98);
	xinli_dcs_write_seq_static(ctx,0xC4,0x08);

	xinli_dcs_write_seq_static(ctx,0x00,0x90);
	xinli_dcs_write_seq_static(ctx,0xE9,0x10,0xFF,0xFF,0xFF,0x00);

	xinli_dcs_write_seq_static(ctx,0x00,0x85);//Posdmypch
	xinli_dcs_write_seq_static(ctx,0xC4,0x80);

	xinli_dcs_write_seq_static(ctx,0x00,0x86);//4VBpch
	xinli_dcs_write_seq_static(ctx,0xA4,0xB6);

	xinli_dcs_write_seq_static(ctx,0x00,0x95);//VBpchdata
	xinli_dcs_write_seq_static(ctx,0xC4,0x80);

	xinli_dcs_write_seq_static(ctx,0x00,0x81);//Sourcechopper
	xinli_dcs_write_seq_static(ctx,0xA4,0x73);

	xinli_dcs_write_seq_static(ctx,0x00,0x93);//ESD
	xinli_dcs_write_seq_static(ctx,0xC4,0x8F);

	xinli_dcs_write_seq_static(ctx,0x00,0x80);//ESD
	xinli_dcs_write_seq_static(ctx,0xB3,0x17);

	xinli_dcs_write_seq_static(ctx,0x00,0xCA);//Poweron3
	xinli_dcs_write_seq_static(ctx,0xC0,0x90,0x11);

	xinli_dcs_write_seq_static(ctx,0x00,0xB7);//sd_en_sdpl_sel
	xinli_dcs_write_seq_static(ctx,0xF5,0x1D);

	xinli_dcs_write_seq_static(ctx,0x00,0xB1);//VCOMdelay
	xinli_dcs_write_seq_static(ctx,0xF5,0x1F);

	xinli_dcs_write_seq_static(ctx,0x00,0xB0);
	xinli_dcs_write_seq_static(ctx,0xC5,0x00);

	xinli_dcs_write_seq_static(ctx,0x00,0xB3);
	xinli_dcs_write_seq_static(ctx,0xC5,0x00);

	xinli_dcs_write_seq_static(ctx,0x00,0xB2);
	xinli_dcs_write_seq_static(ctx,0xC5,0x0D);

	xinli_dcs_write_seq_static(ctx,0x00,0xB5);
	xinli_dcs_write_seq_static(ctx,0xC5,0x02);

	xinli_dcs_write_seq_static(ctx,0x00,0xC2);
	xinli_dcs_write_seq_static(ctx,0xF5,0x42);

	xinli_dcs_write_seq_static(ctx,0x00,0x8A);//Sourcedriverbiascurrent
	xinli_dcs_write_seq_static(ctx,0xC5,0x05);

	xinli_dcs_write_seq_static(ctx,0x00,0x84);//Analogibias_ap
	xinli_dcs_write_seq_static(ctx,0xC5,0x66);

	xinli_dcs_write_seq_static(ctx,0x00,0x80);//Sourcedriverbias
	xinli_dcs_write_seq_static(ctx,0xA4,0x16);

	xinli_dcs_write_seq_static(ctx,0x00,0x94);//pumpvghratiosetting
	xinli_dcs_write_seq_static(ctx,0xC5,0x13);

	xinli_dcs_write_seq_static(ctx,0x00,0x9B);//pumpvglclamp
	xinli_dcs_write_seq_static(ctx,0xC5,0x31);

	xinli_dcs_write_seq_static(ctx,0x00,0x86);
	xinli_dcs_write_seq_static(ctx,0xC3,0x0F,0x00,0x00);//CLKEQ

	xinli_dcs_write_seq_static(ctx,0x00,0xB0); //PWM 12bit 13K
	xinli_dcs_write_seq_static(ctx,0xCA,0x01,0x01,0x0C);
	xinli_dcs_write_seq_static(ctx,0x00,0xB5);
	xinli_dcs_write_seq_static(ctx,0xCA,0x04);

	xinli_dcs_write_seq_static(ctx,0x00,0x00);
	xinli_dcs_write_seq_static(ctx,0xFF,0x00,0x00,0x00);

	xinli_dcs_write_seq_static(ctx,0x00,0x80);
	xinli_dcs_write_seq_static(ctx,0xFF,0x00,0x00);

	xinli_dcs_write_seq_static(ctx,0x35,0x00);
	xinli_dcs_write_seq_static(ctx,0x51,0x00,0x00);
	xinli_dcs_write_seq_static(ctx,0x53,0x2C);
	xinli_dcs_write_seq_static(ctx,0x55,0x00);

	xinli_dcs_write_seq_static(ctx,0x11,0x00);
	msleep(100);
	xinli_dcs_write_seq_static(ctx,0x29,0x00);
	msleep(20);
	pr_info("init code %s-\n", __func__);
}
static int xinli_disable(struct drm_panel *panel)
{
	struct xinli *ctx = panel_to_xinli(panel);
	if (!ctx->enabled)
		return 0;
	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->backlight);
	}
	ctx->enabled = false;
	return 0;
}
static int xinli_unprepare(struct drm_panel *panel)
{
	struct xinli *ctx = panel_to_xinli(panel);
	if (!ctx->prepared)
		return 0;
	xinli_dcs_write_seq_static(ctx, MIPI_DCS_SET_DISPLAY_OFF);
	msleep(20);
	xinli_dcs_write_seq_static(ctx, MIPI_DCS_ENTER_SLEEP_MODE);
	msleep(120);
	xinli_dcs_write_seq_static(ctx,0x00,0x00);
	xinli_dcs_write_seq_static(ctx,0xF7,0x5A,0xA5,0x95,0x27);

	if (double_click_flag == 0) {
	ctx->bias_neg =
		devm_gpiod_get_index(ctx->dev, "bias", 1, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_neg)) {
		pr_info("cannot get bias-gpios 1 %ld\n",
			PTR_ERR(ctx->bias_neg));
		return PTR_ERR(ctx->bias_neg);
	}
	gpiod_set_value(ctx->bias_neg, 0);
	devm_gpiod_put(ctx->dev, ctx->bias_neg);
	msleep(20);

	ctx->bias_pos =
		devm_gpiod_get_index(ctx->dev, "bias", 0, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_pos)) {
		pr_info("cannot get bias-gpios 0 %ld\n",
			PTR_ERR(ctx->bias_pos));
		return PTR_ERR(ctx->bias_pos);
	}
	gpiod_set_value(ctx->bias_pos, 0);
	devm_gpiod_put(ctx->dev, ctx->bias_pos);
	} else {
		pr_info("%s TP double click wake up enable!!!\n", __func__);
	}
	ctx->error = 0;
	ctx->prepared = false;
	return 0;
}
static int xinli_prepare(struct drm_panel *panel)
{
	struct xinli *ctx = panel_to_xinli(panel);
	int ret;
	pr_info("%s+\n", __func__);
	if (ctx->prepared)
		return 0;

	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		pr_info("cannot get reset-gpios %ld\n",
			PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
        gpiod_set_value(ctx->reset_gpio, 0);
        msleep(1);
        devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	ctx->bias_pos =
		devm_gpiod_get_index(ctx->dev, "bias", 0, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_pos)) {
		pr_info("cannot get bias-gpios 0 %ld\n",
			PTR_ERR(ctx->bias_pos));
		return PTR_ERR(ctx->bias_pos);
	}
	gpiod_set_value(ctx->bias_pos, 1);
	lcm_i2c_write_bytes(0x00, 0x14);
	devm_gpiod_put(ctx->dev, ctx->bias_pos);
	pr_info(" bias-pos = %d\n",ctx->bias_pos );
	msleep(1);

	ctx->bias_neg =
		devm_gpiod_get_index(ctx->dev, "bias", 1, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_neg)) {
		pr_info("cannot get bias-gpios 1 %ld\n",
			PTR_ERR(ctx->bias_neg));
		return PTR_ERR(ctx->bias_neg);
	}
	gpiod_set_value(ctx->bias_neg, 1);
	lcm_i2c_write_bytes(0x01, 0x14);
	devm_gpiod_put(ctx->dev, ctx->bias_neg);
	pr_info(" bias-neg = %d\n",ctx->bias_neg );
	msleep(1);

	// lcd reset H -> L -> H
	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		pr_info("cannot get reset-gpios %ld\n",
			PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	gpiod_set_value(ctx->reset_gpio, 1);
	msleep(1);
	gpiod_set_value(ctx->reset_gpio, 0);
	msleep(1);
	gpiod_set_value(ctx->reset_gpio, 1);
	msleep(1);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	// end
	xinli_panel_init(ctx);
	ret = ctx->error;
	if (ret < 0)
		xinli_unprepare(panel);
	ctx->prepared = true;
#ifdef PANEL_SUPPORT_READBACK
	xinli_panel_get_data(ctx);
#endif
#ifdef VENDOR_EDIT
	// shifan@bsp.tp 20191226 add for loading tp fw when screen lighting on
	lcd_queue_load_tp_fw();
#endif
	pr_info("%s-\n", __func__);
	return ret;
}
static int xinli_enable(struct drm_panel *panel)
{
	struct xinli *ctx = panel_to_xinli(panel);
	if (ctx->enabled)
		return 0;
	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(ctx->backlight);
	}
	ctx->enabled = true;
	return 0;
}
static const struct drm_display_mode default_mode = {
	.clock       = clk,
	.hdisplay    = H_display,
	.hsync_start = H_display + HFP,
	.hsync_end   = H_display + HFP + HSA,
	.htotal      = H_display + HFP + HSA + HBP,
	.vdisplay    = V_display,
	.vsync_start = V_display + VFP_90HZ,
	.vsync_end   = V_display + VFP_90HZ + VSA,
	.vtotal      = V_display + VFP_90HZ + VSA + VBP,
};

static const struct drm_display_mode performance_mode_60hz = {
	.clock       = 136612,
	.hdisplay    = H_display,
	.hsync_start = H_display + HFP,
	.hsync_end   = H_display + HFP + HSA,
	.htotal      = H_display + HFP + HSA + HBP,
	.vdisplay    = V_display,
	.vsync_start = V_display + VFP_60HZ,
	.vsync_end   = V_display + VFP_60HZ + VSA,
	.vtotal      = V_display + VFP_60HZ + VSA + VBP,
};

#if defined(CONFIG_MTK_PANEL_EXT)
static struct mtk_panel_params ext_params = {
	.pll_clk = pll_clock,
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
	.cmd = 0x0A, .count = 1, .para_list[0] = 0x9C,
	},
	.ssc_enable = 0,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,   //test for DSC

	.data_rate = data_rate_value,
	.lfr_enable = 0,
	.lfr_minimum_fps = 60,
	.dyn_fps = {
		.switch_en = 1,
		.vact_timing_fps = 90,
	},
	 //following MIPI hopping parameter might cause screen mess
	.dyn = {
		.switch_en = 1,
		.pll_clk = DYN_PLL_CLK,
		.data_rate = DYN_DATA_RATE,
		.vsa = VSA,
		.vbp = VBP,
		.vfp = VFP_90HZ,
		.hsa = HSA,
		.hbp = HBP,
		.hfp = DYN_HFP,
	},
};

static struct mtk_panel_params ext_params_60hz = {
	.pll_clk = pll_clock,
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
	.cmd = 0x0A, .count = 1, .para_list[0] = 0x9C,
	},
	.ssc_enable = 0,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,   //test for DSC

	.data_rate = data_rate_value,
	.lfr_enable = 0,
	.lfr_minimum_fps = 60,
	.dyn_fps = {
		.switch_en = 1,
		.vact_timing_fps = 90,
	},
	 //following MIPI hopping parameter might cause screen mess
	.dyn = {
		.switch_en = 1,
		.pll_clk = DYN_PLL_CLK,
		.data_rate = DYN_DATA_RATE,
		.vsa = VSA,
		.vbp = VBP,
		.vfp = VFP_60HZ,
		.hsa = HSA,
		.hbp = HBP,
		.hfp = DYN_HFP,
	},
};

/*
static int panel_ata_check(struct drm_panel *panel)
{
	//Customer test by own ATA tool
	return 1;
}*/
static int xinli_setbacklight_cmdq(void *dsi, dcs_write_gce cb, void *handle,
				 unsigned int level)
{
	if (level > 4095)
		level = 4095;
	pr_info("%s backlight = %d\n", __func__, level);
	bl_tb0[2] = (u8)(level & 0x0F);
	bl_tb0[1] = (u8)((level >> 4) & 0xFF);
	if (!cb)
		return -1;
	cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));
	return 0;
}

struct drm_display_mode *get_mode_by_id_hfp(struct drm_connector *connector,
	unsigned int mode)
{
	struct drm_display_mode *m;
	unsigned int i = 0;
	list_for_each_entry(m, &connector->modes, head) {
		if (i == mode)
			return m;
		i++;
	}
	return NULL;
}
static int mtk_panel_ext_param_set(struct drm_panel *panel,
			struct drm_connector *connector, unsigned int mode)
{
	struct mtk_panel_ext *ext = find_panel_ext(panel);
	int ret = 0;
	int dst_fps = 0;
	struct drm_display_mode *m = get_mode_by_id_hfp(connector, mode);

	dst_fps = m ? drm_mode_vrefresh(m) : -EINVAL;

	if (dst_fps == 60) {
		ext->params = &ext_params_60hz;
	} else if (dst_fps == 90) {
		ext->params = &ext_params;

	} else {
		pr_err("%s, dst_fps %d\n", __func__, dst_fps);
		ret = -EINVAL;
	}
	pr_info("%s+ lcm dst_fps = %d", __func__,dst_fps);

	return ret;
}

static int panel_ext_reset(struct drm_panel *panel, int on)
{
	struct xinli *ctx = panel_to_xinli(panel);
	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		pr_info("cannot get reset-gpios %ld\n",
			PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	gpiod_set_value(ctx->reset_gpio, on);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	return 0;
}
static struct mtk_panel_funcs ext_funcs = {
	.reset = panel_ext_reset,
	.set_backlight_cmdq = xinli_setbacklight_cmdq,
	.ext_param_set = mtk_panel_ext_param_set,
	//.mode_switch = mode_switch,
	//.ata_check = panel_ata_check,
};
#endif
struct panel_desc {
	const struct drm_display_mode *modes;
	unsigned int num_modes;
	unsigned int bpc;
	struct {
		unsigned int width;
		unsigned int height;
	} size;
	/**
	 * @prepare: the time (in milliseconds) that it takes for the panel to
	 *	   become ready and start receiving video data
	 * @enable: the time (in milliseconds) that it takes for the panel to
	 *	  display the first valid frame after starting to receive
	 *	  video data
	 * @disable: the time (in milliseconds) that it takes for the panel to
	 *	   turn the display off (no content is visible)
	 * @unprepare: the time (in milliseconds) that it takes for the panel
	 *		 to power itself down completely
	 */
	struct {
		unsigned int prepare;
		unsigned int enable;
		unsigned int disable;
		unsigned int unprepare;
	} delay;
};
static int xinli_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	struct drm_display_mode *mode2;

	mode = drm_mode_duplicate(connector->dev, &default_mode);
	pr_info("%s+ lcm default mode\n", __func__);
	if (!mode) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			 default_mode.hdisplay, default_mode.vdisplay,
			 drm_mode_vrefresh(&default_mode));
		return -ENOMEM;
	}
	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	mode2 = drm_mode_duplicate(connector->dev, &performance_mode_60hz);
	if (!mode2) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			 performance_mode_60hz.hdisplay, performance_mode_60hz.vdisplay,
			 drm_mode_vrefresh(&performance_mode_60hz));
		return -ENOMEM;
	}
	drm_mode_set_name(mode2);
	mode2->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode2);

	connector->display_info.width_mm = 72;
	connector->display_info.height_mm = 160;
	return 1;
}
static const struct drm_panel_funcs xinli_drm_funcs = {
	.disable = xinli_disable,
	.unprepare = xinli_unprepare,
	.prepare = xinli_prepare,
	.enable = xinli_enable,
	.get_modes = xinli_get_modes,
};

static ssize_t lcd_name_show(struct device* dev, struct device_attribute* attr, char* buf)
{
	pr_info("panel-ft8057p-hd-vdo-dphy-60hz-xinli\n");
	return sprintf(buf, "panel-ft8057p-hd-vdo-dphy-60hz-xinli\n");
}
static DEVICE_ATTR(lcd_name, 0664, lcd_name_show,NULL);

static int lcd_name_create_device(void)
{
	/* allocate char device number */

	pr_err("Create device node E.\n");
	if (alloc_chrdev_region(&lcd_devno, 0, 1, "lcd")) {
		pr_err("Failed to allocate char device region\n");
		return -1;
	}

	pr_err("Allocate major number and minor number: (%d, %d)\n", MAJOR(lcd_devno), MINOR(lcd_devno));
	/* create class */
	lcd_class = class_create(THIS_MODULE, "display");
	if (IS_ERR(lcd_class)) {
		pr_err("Failed to create class (%d)\n",
			   (int)PTR_ERR(lcd_class));
		return -1;
	}

	/* create device */
	lcd_device = device_create(lcd_class, NULL, lcd_devno, NULL, "lcd");
	if (!lcd_device) {
		pr_err("Failed to create device\n");
		return -1;
	}

	/* create device file */
	if (device_create_file(lcd_device, &dev_attr_lcd_name)) {
		pr_err("Failed to create device file(strobe)\n");
		return -1;
	}
	pr_err("Create device node X.\n");
	return 0;
}

static int xinli_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct device_node *dsi_node, *remote_node = NULL, *endpoint = NULL;
	struct xinli *ctx;
	struct device_node *backlight;
	unsigned int value;
	int ret;
	pr_info("%s+ lcm xinli,ft8057p,vdo,60hz\n", __func__);
	dsi_node = of_get_parent(dev->of_node);
	if (dsi_node) {
		endpoint = of_graph_get_next_endpoint(dsi_node, NULL);
		if (endpoint) {
			remote_node = of_graph_get_remote_port_parent(endpoint);
			if (!remote_node) {
				pr_info("No panel connected,skip probe lcm\n");
				return -ENODEV;
			}
			pr_info("device node name:%s\n", remote_node->name);
		}
	}
	if (remote_node != dev->of_node) {
		pr_info("%s+ skip probe due to not current lcm\n", __func__);
		return -ENODEV;
	}
	ctx = devm_kzalloc(dev, sizeof(struct xinli), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dev = dev;
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
			MIPI_DSI_MODE_LPM |
			MIPI_DSI_CLOCK_NON_CONTINUOUS;
/*
	ret = of_property_read_u32(dev->of_node, "gate-ic", &value);
	if (ret < 0)
		value = 0;
	else
		ctx->gate_ic = value;
*/
	value = 0;
	ret = of_property_read_u32(dev->of_node, "rc-enable", &value);
	if (ret < 0)
		value = 0;
	else {
		ext_params.round_corner_en = value;
	}
	backlight = of_parse_phandle(dev->of_node, "backlight", 0);
	if (backlight) {
		ctx->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);
		if (!ctx->backlight)
			return -EPROBE_DEFER;
	}
	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_info(dev, "cannot get reset-gpios %ld\n",
			 PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	devm_gpiod_put(dev, ctx->reset_gpio);
	/*
	if (ctx->gate_ic == 0) {
		ctx->bias_pos = devm_gpiod_get_index(dev, "bias", 0, GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->bias_pos)) {
			dev_info(dev, "cannot get bias-gpios 0 %ld\n",
				 PTR_ERR(ctx->bias_pos));
			return PTR_ERR(ctx->bias_pos);
		}
		devm_gpiod_put(dev, ctx->bias_pos);
		ctx->bias_neg = devm_gpiod_get_index(dev, "bias", 1, GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->bias_neg)) {
			dev_info(dev, "cannot get bias-gpios 1 %ld\n",
				 PTR_ERR(ctx->bias_neg));
			return PTR_ERR(ctx->bias_neg);
		}
		devm_gpiod_put(dev, ctx->bias_neg);
	}
	*/
	ctx->prepared = true;
	ctx->enabled = true;
	drm_panel_init(&ctx->panel, dev, &xinli_drm_funcs, DRM_MODE_CONNECTOR_DSI);
	drm_panel_add(&ctx->panel);
	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&ctx->panel);
#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_tch_handle_reg(&ctx->panel);
	ret = mtk_panel_ext_create(dev, &ext_params, &ext_funcs, &ctx->panel);
	if (ret < 0)
		return ret;
#endif
	lcd_name_create_device();
	return ret;
}
static int xinli_remove(struct mipi_dsi_device *dsi)
{
	struct xinli *ctx = mipi_dsi_get_drvdata(dsi);
#if defined(CONFIG_MTK_PANEL_EXT)
	struct mtk_panel_ctx *ext_ctx = find_panel_ctx(&ctx->panel);
#endif
	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);
#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_detach(ext_ctx);
	mtk_panel_remove(ext_ctx);
#endif
	return 0;
}

static void xinli_shutdown(struct mipi_dsi_device *dsi)
{
	struct xinli *ctxi = mipi_dsi_get_drvdata(dsi);
	struct xinli *ctx = panel_to_xinli(&ctxi->panel);

	pr_info("%s+\n", __func__);

	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		pr_info("cannot get reset-gpios %ld\n",
			PTR_ERR(ctx->reset_gpio));
	}
	else{
		gpiod_set_value(ctx->reset_gpio, 0);
		msleep(5);
		devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	}

	ctx->bias_neg =
		devm_gpiod_get_index(ctx->dev, "bias", 1, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_neg)) {
		pr_info("cannot get bias-gpios 1 %ld\n",
			PTR_ERR(ctx->bias_neg));
	}
	else{
		gpiod_set_value(ctx->bias_neg, 0);
		devm_gpiod_put(ctx->dev, ctx->bias_neg);
		msleep(3);
	}

	ctx->bias_pos =
		devm_gpiod_get_index(ctx->dev, "bias", 0, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_pos)) {
		pr_info("cannot get bias-gpios 0 %ld\n",
			PTR_ERR(ctx->bias_pos));
	}
	else{
		gpiod_set_value(ctx->bias_pos, 0);
		devm_gpiod_put(ctx->dev, ctx->bias_pos);
	}
}

static const struct of_device_id xinli_of_match[] = {
	{
	    .compatible = "xinli,ft8057p,vdo,60hz",
	},
	{}
};
MODULE_DEVICE_TABLE(of, xinli_of_match);
static struct mipi_dsi_driver xinli_driver = {
	.probe = xinli_probe,
	.remove = xinli_remove,
	.shutdown = xinli_shutdown,
	.driver = {
		.name = "panel-ft8057p-hd-vdo-dphy-60hz-xinli",
		.owner = THIS_MODULE,
		.of_match_table = xinli_of_match,
	},
};
module_mipi_dsi_driver(xinli_driver);
MODULE_AUTHOR("shaohua deng <shaohua.deng@mediatek.com>");
MODULE_DESCRIPTION("xinli ft8057p VDO 60HZ lcd Panel Driver");  //AMOLED
MODULE_LICENSE("GPL v2");
