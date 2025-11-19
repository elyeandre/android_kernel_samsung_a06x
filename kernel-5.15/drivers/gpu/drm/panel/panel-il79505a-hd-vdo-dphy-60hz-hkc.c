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
static char bl_tb0[] = {0x51,0x0F,0xFF};
static struct class *lcd_class;
static struct device *lcd_device;
static dev_t lcd_devno;
extern int double_click_flag;
void (*lcd_ili_resume_by_ddi)(void);
EXPORT_SYMBOL(lcd_ili_resume_by_ddi);
extern int lcm_i2c_write_bytes(u8 reg , u8 val);
//LCM porch
#define clk               131727  //H total *V total * V freq
#define data_rate_value   866   //H total *V total * total_bit_per_pixel*frame_per_second/total_lane_num
#define pll_clock         433
#define H_display   720
#define HSA         12
#define HBP         12
#define HFP         30
#define V_display   1600
#define VSA         4
#define VBP         30
#define VFP_60HZ    1202
#define VFP_90HZ    257
#define DYN_PLL_CLK 433
#define DYN_DATA_RATE 866
#define DYN_HFP     30

struct hkc {
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
#define hkc_dcs_write_seq(ctx, seq...)                              \
	({                                                                 \
		const u8 d[] = { seq };                                        \
		BUILD_BUG_ON_MSG(ARRAY_SIZE(d) > 64,                           \
				 "DCS sequence too big for stack");                    \
		hkc_dcs_write(ctx, d, ARRAY_SIZE(d));                       \
	})
#define hkc_dcs_write_seq_static(ctx, seq...)                       \
	({                                                                 \
		static const u8 d[] = { seq };                                 \
		hkc_dcs_write(ctx, d, ARRAY_SIZE(d));                       \
	})
static inline struct hkc *panel_to_hkc(struct drm_panel *panel)
{
	return container_of(panel, struct hkc, panel);
}
#ifdef PANEL_SUPPORT_READBACK
static int hkc_dcs_read(struct hkc *ctx, u8 cmd, void *data, size_t len)
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
static void hkc_panel_get_data(struct hkc *ctx)
{
	u8 buffer[3] = { 0 };
	static int ret;
	pr_info("%s+\n", __func__);
	if (ret == 0) {
		ret = hkc_dcs_read(ctx, 0x0A, buffer, 1);
		pr_info("%s 0x%08x\n", __func__, buffer[0] | (buffer[1] << 8));
		dev_info(ctx->dev, "return %d data(0x%08x) to dsi engine\n",
			ret, buffer[0] | (buffer[1] << 8));
	}
}
#endif
static void hkc_dcs_write(struct hkc *ctx, const void *data, size_t len)
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
static void hkc_panel_init(struct hkc *ctx)
{
	hkc_dcs_write_seq_static(ctx,0xFF,0x5A,0xA5,0x01);
	hkc_dcs_write_seq_static(ctx,0x30,0x2A);
	hkc_dcs_write_seq_static(ctx,0x31,0x2A);
	hkc_dcs_write_seq_static(ctx,0x32,0x2A);
	hkc_dcs_write_seq_static(ctx,0x35,0x2A);
	hkc_dcs_write_seq_static(ctx,0x36,0x2A);
	hkc_dcs_write_seq_static(ctx,0x37,0x2A);
	hkc_dcs_write_seq_static(ctx,0x48,0x2A);
	hkc_dcs_write_seq_static(ctx,0x49,0x2A);
	hkc_dcs_write_seq_static(ctx,0x4A,0x2A);
	hkc_dcs_write_seq_static(ctx,0x4D,0x2A);
	hkc_dcs_write_seq_static(ctx,0x4E,0x2A);
	hkc_dcs_write_seq_static(ctx,0x4F,0x2A);
	hkc_dcs_write_seq_static(ctx,0xFF,0x5A,0xA5,0x02);
	hkc_dcs_write_seq_static(ctx,0x06,0x5D);
	hkc_dcs_write_seq_static(ctx,0x3A,0xB3);
	hkc_dcs_write_seq_static(ctx,0x3D,0x01);
	hkc_dcs_write_seq_static(ctx,0x3E,0x01);
	hkc_dcs_write_seq_static(ctx,0xFF,0x5A,0xA5,0x0B);
	hkc_dcs_write_seq_static(ctx,0xA7,0xE0);
	hkc_dcs_write_seq_static(ctx,0xAA,0x93);
	hkc_dcs_write_seq_static(ctx,0xAB,0x93);
	hkc_dcs_write_seq_static(ctx,0xFF,0x5A,0xA5,0x0E);
	hkc_dcs_write_seq_static(ctx,0x14,0x0E);
	hkc_dcs_write_seq_static(ctx,0xFF,0x5A,0xA5,0x06);
	hkc_dcs_write_seq_static(ctx,0x3E,0x62); //Dont reload otp
	hkc_dcs_write_seq_static(ctx,0x07,0xA4);
	hkc_dcs_write_seq_static(ctx,0xFF,0x5A,0xA5,0x03);
	hkc_dcs_write_seq_static(ctx,0x83,0x20);
	hkc_dcs_write_seq_static(ctx,0x84,0x01);	 //PWM=14.8M
	hkc_dcs_write_seq_static(ctx,0xFF,0x5A,0xA5,0x00);
	hkc_dcs_write_seq_static(ctx,0x51,0x00,0x00);
	hkc_dcs_write_seq_static(ctx,0x68,0x04,0x00);
	hkc_dcs_write_seq_static(ctx,0x53,0x2C);
	hkc_dcs_write_seq_static(ctx,0x35,0x00);

	hkc_dcs_write_seq_static(ctx,0x11,0x00);
	msleep(60);
	hkc_dcs_write_seq_static(ctx,0x29, 0x00);
	pr_info("init code %s-\n", __func__);
}
static int hkc_disable(struct drm_panel *panel)
{
	struct hkc *ctx = panel_to_hkc(panel);
	if (!ctx->enabled)
		return 0;
	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->backlight);
	}
	ctx->enabled = false;
	return 0;
}
static int hkc_unprepare(struct drm_panel *panel)
{
	struct hkc *ctx = panel_to_hkc(panel);
	if (!ctx->prepared)
		return 0;

	pr_info("%s power off\n", __func__);

	hkc_dcs_write_seq_static(ctx, MIPI_DCS_SET_DISPLAY_OFF);
	msleep(50);
	hkc_dcs_write_seq_static(ctx, MIPI_DCS_ENTER_SLEEP_MODE);
	msleep(100);
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
	msleep(5);

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
static int hkc_prepare(struct drm_panel *panel)
{
	struct hkc *ctx = panel_to_hkc(panel);
	int ret;
	pr_info("%s+\n", __func__);
	if (ctx->prepared)
		return 0;
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
	msleep(2);

	// lcd reset H -> L -> H
	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		pr_info("cannot get reset-gpios %ld\n",
			PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	gpiod_set_value(ctx->reset_gpio, 0);
	msleep(2);
	gpiod_set_value(ctx->reset_gpio, 1);
	msleep(5);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	// end
	//add for TP download
	if(lcd_ili_resume_by_ddi){
		lcd_ili_resume_by_ddi();
	}

	hkc_panel_init(ctx);
	ret = ctx->error;
	if (ret < 0)
		hkc_unprepare(panel);
	ctx->prepared = true;
#ifdef PANEL_SUPPORT_READBACK
	hkc_panel_get_data(ctx);
#endif
#ifdef VENDOR_EDIT
	// shifan@bsp.tp 20191226 add for loading tp fw when screen lighting on
	lcd_queue_load_tp_fw();
#endif
	pr_info("%s-\n", __func__);
	return ret;
}
static int hkc_enable(struct drm_panel *panel)
{
	struct hkc *ctx = panel_to_hkc(panel);
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
	.clock       = 131703,
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
static int hkc_setbacklight_cmdq(void *dsi, dcs_write_gce cb, void *handle,
				 unsigned int level)
{
	if (level > 4095)
		level = 4095;
	pr_info("%s backlight = %d\n", __func__, level);
	bl_tb0[1] = (u8)(level >> 8) & 0x0F;
	bl_tb0[2] = (u8)level & 0xFF;
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
	struct hkc *ctx = panel_to_hkc(panel);
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
	.set_backlight_cmdq = hkc_setbacklight_cmdq,
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
static int hkc_get_modes(struct drm_panel *panel,
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
static const struct drm_panel_funcs hkc_drm_funcs = {
	.disable = hkc_disable,
	.unprepare = hkc_unprepare,
	.prepare = hkc_prepare,
	.enable = hkc_enable,
	.get_modes = hkc_get_modes,
};

static ssize_t lcd_name_show(struct device* dev, struct device_attribute* attr, char* buf)
{
	pr_info("panel-il79505a-hd-vdo-dphy-60hz-hkc\n");
	return sprintf(buf, "panel-il79505a-hd-vdo-dphy-60hz-hkc\n");
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

static int hkc_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct device_node *dsi_node, *remote_node = NULL, *endpoint = NULL;
	struct hkc *ctx;
	struct device_node *backlight;
	unsigned int value;
	int ret;
	pr_info("%s+ lcm hkc,il79505a,vdo,60hz\n", __func__);
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
	ctx = devm_kzalloc(dev, sizeof(struct hkc), GFP_KERNEL);
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
	drm_panel_init(&ctx->panel, dev, &hkc_drm_funcs, DRM_MODE_CONNECTOR_DSI);
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
static int hkc_remove(struct mipi_dsi_device *dsi)
{
	struct hkc *ctx = mipi_dsi_get_drvdata(dsi);
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

static void hkc_shutdown(struct mipi_dsi_device *dsi)
{
	struct hkc *ctxi = mipi_dsi_get_drvdata(dsi);
	struct hkc *ctx = panel_to_hkc(&ctxi->panel);

	pr_info("%s+\n", __func__);

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

static const struct of_device_id hkc_of_match[] = {
	{
	    .compatible = "hkc,il79505a,vdo,60hz",
	},
	{}
};
MODULE_DEVICE_TABLE(of, hkc_of_match);
static struct mipi_dsi_driver hkc_driver = {
	.probe = hkc_probe,
	.remove = hkc_remove,
	.shutdown = hkc_shutdown,
	.driver = {
		.name = "panel-il79505a-hd-vdo-dphy-60hz-hkc",
		.owner = THIS_MODULE,
		.of_match_table = hkc_of_match,
	},
};
module_mipi_dsi_driver(hkc_driver);
MODULE_AUTHOR("shaohua deng <shaohua.deng@mediatek.com>");
MODULE_DESCRIPTION("hkc il79505a VDO 60HZ lcd Panel Driver");  //AMOLED
MODULE_LICENSE("GPL v2");
