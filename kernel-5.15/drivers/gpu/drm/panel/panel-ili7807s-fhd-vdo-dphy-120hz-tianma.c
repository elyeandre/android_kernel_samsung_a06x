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
/* enable this to check panel self -bist pattern */
/* #define PANEL_BIST_PATTERN */
/****************TPS65132***********/
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
//#include "lcm_i2c.h"
static char bl_tb0[] = { 0x51, 0xff };
//LCM porch
#define clk               184631   //334610 //H total *V total * V freq = 1125*2468*120=333180
#define data_rate_value   1240   //H total *V total * total_bit_per_pixel*frame_per_second/total_lane_num
#define pll_clock         620
#define H_display   720
#define HSA			4
#define HBP         16
#define HFP         32
#define V_display   1640
#define VSA			4
#define VBP			18
#define VFP			331
//TO DO: You have to do that remove macro BYPASSI2C and solve build error
//otherwise voltage will be unstable
#define BYPASSI2C   //iic  disable
#ifndef BYPASSI2C
/* i2c control start */
#define LCM_I2C_ID_NAME "I2C_LCD_BIAS"
static struct i2c_client *_lcm_i2c_client;
/*****************************************************************************
 * Function Prototype
 *****************************************************************************/
static int _lcm_i2c_probe(struct i2c_client *client,
		const struct i2c_device_id *id);
static int _lcm_i2c_remove(struct i2c_client *client);
/*****************************************************************************
 * Data Structure
 *****************************************************************************/
struct _lcm_i2c_dev {
	struct i2c_client *client;
};
static const struct of_device_id _lcm_i2c_of_match[] = {
	{
		.compatible = "mediatek,I2C_LCD_BIAS",
	},
	{},
};
static const struct i2c_device_id _lcm_i2c_id[] = { { LCM_I2C_ID_NAME, 0 },
						    {} };
static struct i2c_driver _lcm_i2c_driver = {
	.id_table = _lcm_i2c_id,
	.probe = _lcm_i2c_probe,
	.remove = _lcm_i2c_remove,
	/* .detect		   = _lcm_i2c_detect, */
	.driver = {
		.owner = THIS_MODULE,
		.name = LCM_I2C_ID_NAME,
		.of_match_table = _lcm_i2c_of_match,
	},
};
/*****************************************************************************
 * Function
 *****************************************************************************/
#ifdef VENDOR_EDIT
// shifan@bsp.tp 20191226 add for loading tp fw when screen lighting on
extern void lcd_queue_load_tp_fw(void);
#endif /*VENDOR_EDIT*/
static int _lcm_i2c_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	pr_debug("[LCM][I2C] %s\n", __func__);
	pr_debug("[LCM][I2C] NT: info==>name=%s addr=0x%x\n", client->name,
		client->addr);
	_lcm_i2c_client = client;
	return 0;
}
static int _lcm_i2c_remove(struct i2c_client *client)
{
	pr_debug("[LCM][I2C] %s\n", __func__);
	_lcm_i2c_client = NULL;
	i2c_unregister_device(client);
	return 0;
}
static int _lcm_i2c_write_bytes(unsigned char addr, unsigned char value)
{
	int ret = 0;
	struct i2c_client *client = _lcm_i2c_client;
	char write_data[2] = { 0 };
	if (client == NULL) {
		pr_debug("ERROR!! _lcm_i2c_client is null\n");
		return 0;
	}
	write_data[0] = addr;
	write_data[1] = value;
	ret = i2c_master_send(client, write_data, 2);
	if (ret < 0)
		pr_info("[LCM][ERROR] _lcm_i2c write data fail !!\n");
	return ret;
}
/*
 * module load/unload record keeping
 */
static int __init _lcm_i2c_init(void)
{
	pr_debug("[LCM][I2C] %s\n", __func__);
	i2c_add_driver(&_lcm_i2c_driver);
	pr_debug("[LCM][I2C] %s success\n", __func__);
	return 0;
}
static void __exit _lcm_i2c_exit(void)
{
	pr_debug("[LCM][I2C] %s\n", __func__);
	i2c_del_driver(&_lcm_i2c_driver);
}
//module_init(_lcm_i2c_init);
//module_exit(_lcm_i2c_exit);
/***********************************/
#endif
struct tianma {
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
#define tianma_dcs_write_seq(ctx, seq...)                              \
	({                                                                 \
		const u8 d[] = { seq };                                        \
		BUILD_BUG_ON_MSG(ARRAY_SIZE(d) > 64,                           \
				 "DCS sequence too big for stack");                    \
		tianma_dcs_write(ctx, d, ARRAY_SIZE(d));                       \
	})
#define tianma_dcs_write_seq_static(ctx, seq...)                       \
	({                                                                 \
		static const u8 d[] = { seq };                                 \
		tianma_dcs_write(ctx, d, ARRAY_SIZE(d));                       \
	})
static inline struct tianma *panel_to_tianma(struct drm_panel *panel)
{
	return container_of(panel, struct tianma, panel);
}
#ifdef PANEL_SUPPORT_READBACK
static int tianma_dcs_read(struct tianma *ctx, u8 cmd, void *data, size_t len)
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
static void tianma_panel_get_data(struct tianma *ctx)
{
	u8 buffer[3] = { 0 };
	static int ret;
	pr_info(" syq %s+\n", __func__);
	if (ret == 0) {
		ret = tianma_dcs_read(ctx, 0x0A, buffer, 1);
		pr_info("%s syq  0x%08x\n", __func__, buffer[0] | (buffer[1] << 8));
		dev_info(ctx->dev, "return %d data(0x%08x) to dsi engine\n",
			ret, buffer[0] | (buffer[1] << 8));
	}
}
#endif
static void tianma_dcs_write(struct tianma *ctx, const void *data, size_t len)
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
		pr_info(" %s  syq 5555  0x%x \n",__func__,(int)*addr);
		//msleep(3);
		}
	else
		{
		ret = mipi_dsi_generic_write(dsi, data, len);
			//pr_info(" %s syq  0x%x \n",__func__,(int)*addr);
		//msleep(3);
		}
	if (ret < 0) {
		dev_info(ctx->dev, "error %zd writing seq: %ph\n", ret, data);
		ctx->error = ret;
	}
}
static void tianma_panel_init(struct tianma *ctx)
{
	ctx->bias_pos =
		devm_gpiod_get_index(ctx->dev, "bias", 0, GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->bias_pos, 1);
	devm_gpiod_put(ctx->dev, ctx->bias_pos);
	pr_info("  syq  bias-pos = %d\n",ctx->bias_pos );
	msleep(10);
	ctx->bias_neg =
		devm_gpiod_get_index(ctx->dev, "bias", 1, GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->bias_neg, 1);
	devm_gpiod_put(ctx->dev, ctx->bias_neg);
		pr_info("  syq	bias-neg = %d\n",ctx->bias_neg );
		msleep(10);
	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	mdelay (10);
	gpiod_set_value(ctx->reset_gpio, 1);
	mdelay (10);
	gpiod_set_value(ctx->reset_gpio, 0);
	mdelay (10);
	gpiod_set_value(ctx->reset_gpio, 1);
	mdelay (15);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	tianma_dcs_write_seq_static(ctx,0xFF,0x10);
	tianma_dcs_write_seq_static(ctx,0xFB,0x01);
	tianma_dcs_write_seq_static(ctx,0x53,0x2C);
	tianma_dcs_write_seq_static(ctx,0x55,0x00);
	tianma_dcs_write_seq_static(ctx,0x51,0x07,0xff);
	tianma_dcs_write_seq_static(ctx,0x68,0x02,0x01);
	tianma_dcs_write_seq_static(ctx,0x11,0x00);
	msleep(20);
	tianma_dcs_write_seq_static(ctx,0x29, 0x00);
	msleep(120);
	tianma_dcs_write_seq(ctx, bl_tb0[0], bl_tb0[1]);
	pr_info(" syq init code %s-\n", __func__);
}
static int tianma_disable(struct drm_panel *panel)
{
	struct tianma *ctx = panel_to_tianma(panel);
	if (!ctx->enabled)
		return 0;
	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->backlight);
	}
	ctx->enabled = false;
	return 0;
}
static int tianma_unprepare(struct drm_panel *panel)//下电时序
{
	struct tianma *ctx = panel_to_tianma(panel);
	if (!ctx->prepared)
		return 0;
	tianma_dcs_write_seq_static(ctx, MIPI_DCS_SET_DISPLAY_OFF);
	msleep(50);
	tianma_dcs_write_seq_static(ctx, MIPI_DCS_ENTER_SLEEP_MODE);
	msleep(150);
	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->reset_gpio, 0);
	msleep(5);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	ctx->bias_neg =
		devm_gpiod_get_index(ctx->dev, "bias", 1, GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->bias_neg, 0);
	devm_gpiod_put(ctx->dev, ctx->bias_neg);
	msleep(20);
	ctx->bias_pos =
		devm_gpiod_get_index(ctx->dev, "bias", 0, GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->bias_pos, 0);
	devm_gpiod_put(ctx->dev, ctx->bias_pos);
	ctx->error = 0;
	ctx->prepared = false;
	return 0;
}
static int tianma_prepare(struct drm_panel *panel)//上电时序
{
	struct tianma *ctx = panel_to_tianma(panel);
	int ret;
	pr_info("%s+\n", __func__);
	if (ctx->prepared)
		return 0;
	ctx->bias_pos =
		devm_gpiod_get_index(ctx->dev, "bias", 0, GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->bias_pos, 1);
	devm_gpiod_put(ctx->dev, ctx->bias_pos);
	pr_info("  syq  bias-pos = %d\n",ctx->bias_pos );
	msleep(10);
	ctx->bias_neg =
		devm_gpiod_get_index(ctx->dev, "bias", 1, GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->bias_neg, 1);
	devm_gpiod_put(ctx->dev, ctx->bias_neg);
		pr_info("  syq	bias-neg = %d\n",ctx->bias_neg );
	// lcd reset H -> L -> H
	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(10000, 10001);
	gpiod_set_value(ctx->reset_gpio, 0);
	msleep(10);
	gpiod_set_value(ctx->reset_gpio, 1);
	msleep(20);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	// end
#ifndef BYPASSI2C
	_lcm_i2c_write_bytes(0x0, 0xf);
	_lcm_i2c_write_bytes(0x1, 0xf);
#endif
	tianma_panel_init(ctx);
	ret = ctx->error;
	if (ret < 0)
		tianma_unprepare(panel);
	ctx->prepared = true;
#ifdef PANEL_SUPPORT_READBACK
	tianma_panel_get_data(ctx);
#endif
#ifdef VENDOR_EDIT
	// shifan@bsp.tp 20191226 add for loading tp fw when screen lighting on
	lcd_queue_load_tp_fw();
#endif
	pr_info("%s-\n", __func__);
	return ret;
}
static int tianma_enable(struct drm_panel *panel)
{
	struct tianma *ctx = panel_to_tianma(panel);
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
	.clock       = clk,//  H total *V total * V freq
	.hdisplay    = H_display,
	.hsync_start = H_display + HFP,//HFP
	.hsync_end   = H_display + HFP + HSA,//HSA
	.htotal      = H_display + HFP + HSA + HBP,//HBP 1184
	.vdisplay    = V_display,
	.vsync_start = V_display + VFP,//VFP
	.vsync_end   = V_display + VFP + VSA,//VSA
	.vtotal      = V_display + VFP + VSA + VBP,//VBP 2478
	//.vrefresh    = 60 ,
};
/*
static const struct drm_display_mode performance_mode_90hz = {
	.clock = 370431,
	.hdisplay = 1080,
	.hsync_start = 1080 + 76,//HFP
	.hsync_end = 1080 + 76 + 12,//HSA
	.htotal = 1080 + 76 + 12 + 80,//HBP
	.vdisplay = 2400,
	.vsync_start = 2400 + 878,//VFP
	.vsync_end = 2400 + 878 + 10,//VSA
	.vtotal = 2400 + 878 + 10 + 10,//VBP
};
static const struct drm_display_mode performance_mode_120hz = {
	.clock = 370506,
	.hdisplay = 1080,
	.hsync_start = 1080 + 76,//HFP
	.hsync_end = 1080 + 76 + 12,//HSA
	.htotal = 1080 + 76 + 12 + 80,//HBP
	.vdisplay = 2400,
	.vsync_start = 2400 + 60,//VFP
	.vsync_end = 2400 + 60 + 10,//VSA
	.vtotal = 2400 + 60 + 10 + 10,//VBP
};
*/
#if defined(CONFIG_MTK_PANEL_EXT)
static struct mtk_panel_params ext_params = {
	.pll_clk = pll_clock,//356,   //530
//	.vfp_low_power = 4180,//840,// 4180,
	.cust_esd_check = 0,
	.esd_check_enable = 0,
	.lcm_esd_check_table[0] = {
	.cmd = 0x0A, .count = 1, .para_list[0] = 0x9C,
	},
	.ssc_enable = 0,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,   //test for DSC
	.dsc_params = {
			.enable = 0,
			.ver = 17,
			.slice_mode = 1,
			.rgb_swap = 0,
			.dsc_cfg = 34,
			.rct_on = 1,
			.bit_per_channel = 8,
			.dsc_line_buf_depth = 9,
			.bp_enable = 1,
			.bit_per_pixel = 128,  //8
			.pic_height = 1600,
			.pic_width = 720,
			.slice_height = 12,//20  //10
			.slice_width = 540,
			.chunk_size = 540,
			.xmit_delay = 512,
			.dec_delay = 526,
			.scale_value = 32,
			.increment_interval = 287,
			.decrement_interval = 7,
			.line_bpg_offset = 12,
			.nfl_bpg_offset = 2235,
			.slice_bpg_offset = 2170,
			.initial_offset = 6144,
			.final_offset = 4336,
			.flatness_minqp = 3,
			.flatness_maxqp = 12,
			.rc_model_size = 8192,
			.rc_edge_factor = 6,
			.rc_quant_incr_limit0 = 11,
			.rc_quant_incr_limit1 = 11,
			.rc_tgt_offset_hi = 3,
			.rc_tgt_offset_lo = 3,
			},
		.data_rate = data_rate_value,
		.lfr_enable = 0,
		.lfr_minimum_fps = 120,
		/*.dyn_fps = {
			.switch_en = 1,
			.vact_timing_fps = 120,
			.dfps_cmd_table[0] = {0, 2, {0xFF, 0x25} },
			.dfps_cmd_table[1] = {0, 2, {0xFB, 0x01} },
			.dfps_cmd_table[2] = {0, 2, {0x18, 0x20} },
			//switch page for esd check
			.dfps_cmd_table[3] = {0, 2, {0xFF, 0x10} },
			.dfps_cmd_table[4] = {0, 2, {0xFB, 0x01} },
		},
		 //following MIPI hopping parameter might cause screen mess 
		.dyn = {
			.switch_en = 1,
			.pll_clk = 727,
			.vfp_lp_dyn = 4178,
			.hfp = 19,
			.vfp = 46,
		},*/
};
/*
static int panel_ata_check(struct drm_panel *panel)
{
	//Customer test by own ATA tool
	return 1;
}*/
static int tianma_setbacklight_cmdq(void *dsi, dcs_write_gce cb, void *handle,
				 unsigned int level)
{
	if (level > 255)
		level = 255;
	pr_info("%s backlight = -%d\n", __func__, level);
	bl_tb0[1] = (u8)level;
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
	struct drm_display_mode *m = get_mode_by_id_hfp(connector, mode);
	if (drm_mode_vrefresh(m) == 120)
		{
		ext->params = &ext_params;
			pr_info("%s+ syq lcm  v refresh 60", __func__);
		}
	else
		ret = 1;
	return ret;
}
static int panel_ext_reset(struct drm_panel *panel, int on)
{
	struct tianma *ctx = panel_to_tianma(panel);
	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->reset_gpio, on);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	return 0;
}
static struct mtk_panel_funcs ext_funcs = {
	.reset = panel_ext_reset,
	.set_backlight_cmdq = tianma_setbacklight_cmdq,
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
static int tianma_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	//struct drm_display_mode *mode2;
	//struct drm_display_mode *mode3;
	mode = drm_mode_duplicate(connector->dev, &default_mode);
		pr_info("%s+ syq lcm default mode\n", __func__);
	if (!mode) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			 default_mode.hdisplay, default_mode.vdisplay,
			 drm_mode_vrefresh(&default_mode));
		return -ENOMEM;
	}
	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);
/*
	mode2 = drm_mode_duplicate(connector->dev, &performance_mode_90hz);
	if (!mode2) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			 performance_mode_90hz.hdisplay, performance_mode_90hz.vdisplay,
			 drm_mode_vrefresh(&performance_mode_90hz));
		return -ENOMEM;
	}
	drm_mode_set_name(mode2);
	mode2->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode2);
	mode3 = drm_mode_duplicate(connector->dev, &performance_mode_120hz);
	if (!mode3) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			 performance_mode_120hz.hdisplay, performance_mode_120hz.vdisplay,
			 drm_mode_vrefresh(&performance_mode_120hz));
		return -ENOMEM;
	}
	drm_mode_set_name(mode3);
	mode3->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode3);
*/
	connector->display_info.width_mm = 72;
	connector->display_info.height_mm = 160;
	return 1;
}
static const struct drm_panel_funcs tianma_drm_funcs = {
	.disable = tianma_disable,
	.unprepare = tianma_unprepare,
	.prepare = tianma_prepare,
	.enable = tianma_enable,
	.get_modes = tianma_get_modes,
};
static int tianma_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct device_node *dsi_node, *remote_node = NULL, *endpoint = NULL;
	struct tianma *ctx;
	struct device_node *backlight;
	unsigned int value;
	int ret;
	pr_info("%s+ syq lcm tianma,ili7807s,vdo,120hz\n", __func__);
	dsi_node = of_get_parent(dev->of_node);
	if (dsi_node) {
		endpoint = of_graph_get_next_endpoint(dsi_node, NULL);
		if (endpoint) {
			remote_node = of_graph_get_remote_port_parent(endpoint);
			if (!remote_node) {
				pr_info("No panel connected,skip probe lcm\n");
				return -ENODEV;
			}
			pr_info(" syq device node name:%s\n", remote_node->name);
		}
	}
	if (remote_node != dev->of_node) {
		pr_info("%s+ skip probe due to not current lcm\n", __func__);
		return -ENODEV;
	}
	ctx = devm_kzalloc(dev, sizeof(struct tianma), GFP_KERNEL);
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
	drm_panel_init(&ctx->panel, dev, &tianma_drm_funcs, DRM_MODE_CONNECTOR_DSI);
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
	pr_info("%s-  syq lcm tianma,ili7807s,vdo,120hz\n", __func__);
	return ret;
}
static int tianma_remove(struct mipi_dsi_device *dsi)
{
	struct tianma *ctx = mipi_dsi_get_drvdata(dsi);
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
static const struct of_device_id tianma_of_match[] = {
	{
	    .compatible = "tianma,ili7807s,vdo,120hz",
	},
	{}
};
MODULE_DEVICE_TABLE(of, tianma_of_match);
static struct mipi_dsi_driver tianma_driver = {
	.probe = tianma_probe,
	.remove = tianma_remove,
	.driver = {
		.name = "panel-ili7807s-fhd-vdo-dphy-120hz-tianma",
		.owner = THIS_MODULE,
		.of_match_table = tianma_of_match,
	},
};
module_mipi_dsi_driver(tianma_driver);
MODULE_AUTHOR("shaohua deng <shaohua.deng@mediatek.com>");
MODULE_DESCRIPTION("tianma ili7807s VDO 120HZ lcd Panel Driver");  //AMOLED
MODULE_LICENSE("GPL v2");
