/*****************************************************************************
 *
 * Filename:
 * ---------
 *	 S5KJN1_txd_mipiraw_sensor.c
 *
 * Project:
 * --------
 *	 ALPS MT6873
 *
 * Description:
 * ------------
 *---------------------------------------------------------------------------
 * Upper this line, this part is controlled by CC/CQ. DO NOT MODIFY!!
 *============================================================================
 ****************************************************************************/

#include <asm/atomic.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/videodev2.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "kd_camera_typedef.h"
#include "kd_imgsensor.h"
#include "kd_imgsensor_define.h"
#include "kd_imgsensor_errcode.h"
#include "s5kjn1_txd_mipiraw_Sensor.h"

#define FPT_PDAF_SUPPORT  //for pdaf switch

#define MULTI_WRITE 1

#if MULTI_WRITE
#define I2C_BUFFER_LEN 1020 /* trans# max is 255, each 3 bytes */
#else
#define I2C_BUFFER_LEN 4
#endif

static kal_uint32 streaming_control(kal_bool enable);

#define S5KJN1_EEPROM_SLAVE_ADDR 0xA0

extern unsigned int sub_camera_flag;

#define LONG_EXP 1

/****************************Modify Following Strings for Debug****************************/
#define PFX "S5KJN1"
#define MY_LOGE(format, args...) LOG_INF_NEW(format, ##args)
#define LOG_INF_NEW(format, args...) pr_debug(PFX "[%s] " format, __func__, ##args)
#define LOG_INF(format, args...) printk(PFX "[JW][%s] " format, __func__, ##args)

#define LOG_1 LOG_INF("S5KJN1,MIPI 4LANE\n")
#define SENSORDB LOG_INF
/****************************   Modify end    *******************************************/

static DEFINE_SPINLOCK(imgsensor_drv_lock);

static MUINT32 g_sync_mode = SENSOR_MASTER_SYNC_MODE;

static char *scenarios[8] = {
	"MSDK_SCENARIO_ID_CAMERA_PREVIEW",
	"MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG",
	"MSDK_SCENARIO_ID_VIDEO_PREVIEW",
	"MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO",
	"MSDK_SCENARIO_ID_SLIM_VIDEO",
	"MSDK_SCENARIO_ID_CUSTOM1",
	"MSDK_SCENARIO_ID_CUSTOM2",
	"MSDK_SCENARIO_ID_MAX"
};

static imgsensor_info_struct imgsensor_info = {
	.sensor_id = S5KJN1_TXD_SENSOR_ID,
	.checksum_value = 0x40631970,
	.pre = {
		.pclk = 560000000,
		.linelength = 4996,
		.framelength = 3704,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 4080,
		.grabwindow_height = 3072,
		.mipi_data_lp2hs_settle_dc = 85,
		.max_framerate = 300,
		.mipi_pixel_rate = 590400000,
	},
	.cap = {
		.pclk = 560000000,
		.linelength = 4996,
		.framelength = 3704,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 4080,
		.grabwindow_height = 3072,
		.mipi_data_lp2hs_settle_dc = 85,
		.max_framerate = 300,
		.mipi_pixel_rate = 590400000,
	},
	.normal_video = {
		.pclk = 560000000,
		.linelength = 4996,
		.framelength = 3736,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 4080,
		.grabwindow_height = 2296,
		.mipi_data_lp2hs_settle_dc = 85,
		.max_framerate = 300,
		.mipi_pixel_rate = 590400000,
	},
	.hs_video = {
		.pclk = 600000000,
		.linelength = 2672,
		.framelength = 1866,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 2000,
		.grabwindow_height = 1132,
		.mipi_data_lp2hs_settle_dc = 85,
		.max_framerate = 1200,
		.mipi_pixel_rate = 600000000,
	},
	.slim_video = {
		.pclk = 600000000,
		.linelength = 2672,
		.framelength = 3736,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 1920,
		.grabwindow_height = 1080,
		.mipi_data_lp2hs_settle_dc = 85,
		.max_framerate = 600,
		.mipi_pixel_rate = 590400000,
	},
	.custom1 = {
		.pclk = 560000000,
		.linelength = 4996,
		.framelength = 4630,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 4080,
		.grabwindow_height = 3072,
		.mipi_data_lp2hs_settle_dc = 85,
		.max_framerate = 240,
		.mipi_pixel_rate = 590400000,
	},
	.custom2 = {
		.pclk = 560000000,
		.linelength = 8688,
		.framelength = 6400,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 8160,
		.grabwindow_height = 6144,
		.mipi_data_lp2hs_settle_dc = 85,
		.max_framerate = 100,
		.mipi_pixel_rate = 590400000,
	},

	.margin = 5,       // sensor framelength & shutter margin
	.min_shutter = 4,  // min shutter
	.min_gain = 64,    // 1x gain
	.max_gain = 4096,  // 64x gain
	.min_gain_iso = 25,
	.gain_step = 2,
	.exp_step = 2,
	.gain_type = 2,
	.max_frame_length = 0xFFFF,       // REG0x0202 <=REG0x0340-5//max framelength by sensor register's limitation
	.ae_shut_delay_frame = 0,         // shutter delay frame for AE cycle, 2 frame with ispGain_delay-shut_delay=2-0=2
	.ae_sensor_gain_delay_frame = 0,  // sensor gain delay frame for AE cycle,2 frame with ispGain_delay-sensor_gain_delay=2-0=2
	.ae_ispGain_delay_frame = 2,      // isp gain delay frame for AE cycle
	.ihdr_support = 0,                // 1, support; 0,not support
	.ihdr_le_firstline = 0,           // 1,le first ; 0, se first
	.temperature_support = 0,         // 1, support; 0,not support */
	.sensor_mode_num = 7,             // support sensor mode num ,don't support Slow motion

	.cap_delay_frame = 2,         // enter capture delay frame num
	.pre_delay_frame = 2,         // enter preview delay frame num
	.video_delay_frame = 2,       // enter video delay frame num
	.hs_video_delay_frame = 2,    // enter high speed video  delay frame num
	.slim_video_delay_frame = 2,  // enter slim video delay frame num
	.custom1_delay_frame = 2,
	.custom2_delay_frame = 2,
	.frame_time_delay_frame = 2,

	.isp_driving_current = ISP_DRIVING_6MA,                               // mclk driving current
	.sensor_interface_type = SENSOR_INTERFACE_TYPE_MIPI,                  // sensor_interface_type
	.mipi_sensor_type = MIPI_OPHY_NCSI2,                                  // 0,MIPI_OPHY_NCSI2;  1,MIPI_OPHY_CSI2
	.mipi_settle_delay_mode = 0,                                          // 0,MIPI_SETTLEDELAY_AUTO; 1,MIPI_SETTLEDELAY_MANNUAL
	.sensor_output_dataformat = SENSOR_OUTPUT_FORMAT_RAW_4CELL_Gr,              // sensor output first pixel color
	.mclk = 24,                                                           // mclk value, suggest 24 or 26 for 24Mhz or 26Mhz
	.mipi_lane_num = SENSOR_MIPI_4_LANE,                                  // mipi lane num
	.i2c_speed = 1000,
	.i2c_addr_table = {0x5a, 0xff},  // record sensor support all write id addr, only supprt 4must end with 0xff
};

static imgsensor_struct imgsensor = {
	.mirror = IMAGE_NORMAL,                                  // mirrorflip information
	.sensor_mode = IMGSENSOR_MODE_INIT,                      // IMGSENSOR_MODE enum value,record current sensor mode,such as: INIT, Preview, Capture, Video,High Speed Video, Slim Video
	.shutter = 0x200,                                        // current shutter
	.gain = 0x200,                                           // current gain
	.dummy_pixel = 0,                                        // current dummypixel
	.dummy_line = 0,                                         // current dummyline
	.current_fps = 0,                                        // full size current fps : 24fps for PIP, 30fps for Normal or ZSD
	.autoflicker_en = KAL_FALSE,                             // auto flicker enable: KAL_FALSE for disable auto flicker, KAL_TRUE for enable auto flicker
	.test_pattern = KAL_FALSE,                               // test pattern mode or not. KAL_FALSE for in test pattern mode, KAL_TRUE for normal output
	.current_scenario_id = MSDK_SCENARIO_ID_CAMERA_PREVIEW,  // current scenario id
	.ihdr_en = KAL_FALSE,                                    // sensor need support LE, SE with HDR feature
	.i2c_write_id = 0x5a,                                    // record current sensor's i2c write id

	// Long exposure
	.current_ae_effective_frame = 2,
};

/* Sensor output window information*/
static struct SENSOR_WINSIZE_INFO_STRUCT imgsensor_winsize_info[7] = {
	{8160, 6144,   0,   0, 8160, 6144, 4080, 3072, 0, 0, 4080, 3072, 0, 0, 4080, 3072}, // preveiw
	{8160, 6144,   0,   0, 8160, 6144, 4080, 3072, 0, 0, 4080, 3072, 0, 0, 4080, 3072}, // capture
	{8160, 6144,   0, 776, 8160, 4592, 4080, 2296, 0, 0, 4080, 2296, 0, 0, 4080, 2296}, // video
	{8160, 6144,   0, 808, 8160, 4528, 2040, 1132,20, 0, 2000, 1132, 0, 0, 2000, 1132}, // high speed
	{8160, 6144, 240, 912, 7680, 4320, 1920, 1080, 0, 0, 1920, 1080, 0, 0, 1920, 1080}, // slim video
	{8160, 6144,   0,   0, 8160, 6144, 4080, 3072, 0, 0, 4080, 3072, 0, 0, 4080, 3072}, // custom1 dualcam
	{8160, 6144,   0,   0, 8160, 6144, 8160, 6144, 0, 0, 8160, 6144, 0, 0, 8160, 6144}, // custom2 remosaic
};

#if MULTI_WRITE
static kal_uint16 sensor_init_setting_array[] = {
	0x6226, 0x0001,
	0x6028, 0x2400,
	0x602A, 0x801C,
	0x6F12, 0x1743,
	0x6F12, 0x01FC,
	0x6F12, 0xE702,
	0x6F12, 0xA3F3,
	0x6F12, 0xB787,
	0x6F12, 0x0024,
	0x6F12, 0x3797,
	0x6F12, 0x0024,
	0x6F12, 0x1307,
	0x6F12, 0xC797,
	0x6F12, 0x9387,
	0x6F12, 0xC701,
	0x6F12, 0xBA97,
	0x6F12, 0x3787,
	0x6F12, 0x0024,
	0x6F12, 0x1307,
	0x6F12, 0xC701,
	0x6F12, 0xB785,
	0x6F12, 0x0024,
	0x6F12, 0x3755,
	0x6F12, 0x0020,
	0x6F12, 0x998F,
	0x6F12, 0x0146,
	0x6F12, 0x3777,
	0x6F12, 0x0024,
	0x6F12, 0x9385,
	0x6F12, 0x054D,
	0x6F12, 0x1305,
	0x6F12, 0xC504,
	0x6F12, 0x2320,
	0x6F12, 0xF73C,
	0x6F12, 0x9710,
	0x6F12, 0x00FC,
	0x6F12, 0xE780,
	0x6F12, 0x803B,
	0x6F12, 0xB797,
	0x6F12, 0x0024,
	0x6F12, 0x23A4,
	0x6F12, 0xA796,
	0x6F12, 0xB785,
	0x6F12, 0x0024,
	0x6F12, 0x3755,
	0x6F12, 0x0020,
	0x6F12, 0x0146,
	0x6F12, 0x9385,
	0x6F12, 0xE514,
	0x6F12, 0x1305,
	0x6F12, 0xE54B,
	0x6F12, 0x9710,
	0x6F12, 0x00FC,
	0x6F12, 0xE780,
	0x6F12, 0x6039,
	0x6F12, 0xB797,
	0x6F12, 0x0024,
	0x6F12, 0x23A8,
	0x6F12, 0xA796,
	0x6F12, 0xB785,
	0x6F12, 0x0024,
	0x6F12, 0x3765,
	0x6F12, 0x0020,
	0x6F12, 0x0146,
	0x6F12, 0x9385,
	0x6F12, 0x4520,
	0x6F12, 0x1305,
	0x6F12, 0x25C6,
	0x6F12, 0x9710,
	0x6F12, 0x00FC,
	0x6F12, 0xE780,
	0x6F12, 0x4037,
	0x6F12, 0xB797,
	0x6F12, 0x0024,
	0x6F12, 0x23A6,
	0x6F12, 0xA796,
	0x6F12, 0xB785,
	0x6F12, 0x0024,
	0x6F12, 0x3795,
	0x6F12, 0x0020,
	0x6F12, 0x0146,
	0x6F12, 0x9385,
	0x6F12, 0xC554,
	0x6F12, 0x1305,
	0x6F12, 0x857D,
	0x6F12, 0x9710,
	0x6F12, 0x00FC,
	0x6F12, 0xE780,
	0x6F12, 0x2035,
	0x6F12, 0xB797,
	0x6F12, 0x0024,
	0x6F12, 0x23A2,
	0x6F12, 0xA796,
	0x6F12, 0xB785,
	0x6F12, 0x0024,
	0x6F12, 0x3775,
	0x6F12, 0x0120,
	0x6F12, 0x0146,
	0x6F12, 0x9385,
	0x6F12, 0x255A,
	0x6F12, 0x1305,
	0x6F12, 0x65E8,
	0x6F12, 0x9710,
	0x6F12, 0x00FC,
	0x6F12, 0xE780,
	0x6F12, 0x0033,
	0x6F12, 0xB797,
	0x6F12, 0x0024,
	0x6F12, 0x23A0,
	0x6F12, 0xA796,
	0x6F12, 0xB785,
	0x6F12, 0x0024,
	0x6F12, 0x3715,
	0x6F12, 0x0120,
	0x6F12, 0x0146,
	0x6F12, 0x9385,
	0x6F12, 0x6561,
	0x6F12, 0x1305,
	0x6F12, 0xA520,
	0x6F12, 0x9710,
	0x6F12, 0x00FC,
	0x6F12, 0xE780,
	0x6F12, 0xE030,
	0x6F12, 0xB797,
	0x6F12, 0x0024,
	0x6F12, 0x23AE,
	0x6F12, 0xA794,
	0x6F12, 0xB785,
	0x6F12, 0x0024,
	0x6F12, 0x3715,
	0x6F12, 0x0120,
	0x6F12, 0x0146,
	0x6F12, 0x9385,
	0x6F12, 0xA56A,
	0x6F12, 0x1305,
	0x6F12, 0x052B,
	0x6F12, 0x9710,
	0x6F12, 0x00FC,
	0x6F12, 0xE780,
	0x6F12, 0xC02E,
	0x6F12, 0xB797,
	0x6F12, 0x0024,
	0x6F12, 0x23AC,
	0x6F12, 0xA794,
	0x6F12, 0x0D25,
	0x6F12, 0xE177,
	0x6F12, 0x3747,
	0x6F12, 0x0024,
	0x6F12, 0x9387,
	0x6F12, 0x5776,
	0x6F12, 0x2317,
	0x6F12, 0xF782,
	0x6F12, 0x1743,
	0x6F12, 0x01FC,
	0x6F12, 0x6700,
	0x6F12, 0x43E3,
	0x6F12, 0x1743,
	0x6F12, 0x01FC,
	0x6F12, 0xE702,
	0x6F12, 0x03DE,
	0x6F12, 0xB797,
	0x6F12, 0x0024,
	0x6F12, 0x03A9,
	0x6F12, 0x0797,
	0x6F12, 0x2E84,
	0x6F12, 0xAA89,
	0x6F12, 0x0146,
	0x6F12, 0xCA85,
	0x6F12, 0x1145,
	0x6F12, 0x9780,
	0x6F12, 0xFFFB,
	0x6F12, 0xE780,
	0x6F12, 0x0024,
	0x6F12, 0xA285,
	0x6F12, 0x4E85,
	0x6F12, 0x97D0,
	0x6F12, 0xFFFB,
	0x6F12, 0xE780,
	0x6F12, 0xA034,
	0x6F12, 0x3784,
	0x6F12, 0x0024,
	0x6F12, 0x9307,
	0x6F12, 0x8476,
	0x6F12, 0x03C7,
	0x6F12, 0x1700,
	0x6F12, 0x8547,
	0x6F12, 0x6312,
	0x6F12, 0xF706,
	0x6F12, 0xB794,
	0x6F12, 0x0024,
	0x6F12, 0x130C,
	0x6F12, 0x8476,
	0x6F12, 0x938A,
	0x6F12, 0x8481,
	0x6F12, 0x1304,
	0x6F12, 0x8476,
	0x6F12, 0x9384,
	0x6F12, 0x8481,
	0x6F12, 0x014A,
	0x6F12, 0x896B,
	0x6F12, 0x130B,
	0x6F12, 0x0004,
	0x6F12, 0x9377,
	0x6F12, 0xFA01,
	0x6F12, 0xE297,
	0x6F12, 0x83C7,
	0x6F12, 0x0709,
	0x6F12, 0x0355,
	0x6F12, 0x2400,
	0x6F12, 0x8A07,
	0x6F12, 0xCE97,
	0x6F12, 0x9C43,
	0x6F12, 0x3305,
	0x6F12, 0xF502,
	0x6F12, 0x8147,
	0x6F12, 0x3505,
	0x6F12, 0x3581,
	0x6F12, 0x23A0,
	0x6F12, 0xAA00,
	0x6F12, 0x63F9,
	0x6F12, 0xAB00,
	0x6F12, 0x9780,
	0x6F12, 0xFFFB,
	0x6F12, 0xE780,
	0x6F12, 0x60E7,
	0x6F12, 0x4915,
	0x6F12, 0x9377,
	0x6F12, 0xF50F,
	0x6F12, 0x2380,
	0x6F12, 0xF410,
	0x6F12, 0x050A,
	0x6F12, 0x0904,
	0x6F12, 0x910A,
	0x6F12, 0x8504,
	0x6F12, 0xE310,
	0x6F12, 0x6AFD,
	0x6F12, 0x0546,
	0x6F12, 0xCA85,
	0x6F12, 0x1145,
	0x6F12, 0x9780,
	0x6F12, 0xFFFB,
	0x6F12, 0xE780,
	0x6F12, 0x401B,
	0x6F12, 0x1743,
	0x6F12, 0x01FC,
	0x6F12, 0x6700,
	0x6F12, 0xA3D6,
	0x6F12, 0x1743,
	0x6F12, 0x01FC,
	0x6F12, 0xE702,
	0x6F12, 0x83D3,
	0x6F12, 0xB787,
	0x6F12, 0x0024,
	0x6F12, 0x83C7,
	0x6F12, 0x9776,
	0x6F12, 0x2A84,
	0x6F12, 0xAE84,
	0x6F12, 0x6391,
	0x6F12, 0x071C,
	0x6F12, 0x2E85,
	0x6F12, 0x97E0,
	0x6F12, 0xFFFB,
	0x6F12, 0xE780,
	0x6F12, 0x6090,
	0x6F12, 0x2685,
	0x6F12, 0x97E0,
	0x6F12, 0xFFFB,
	0x6F12, 0xE780,
	0x6F12, 0x009F,
	0x6F12, 0x1387,
	0x6F12, 0x01FE,
	0x6F12, 0x0347,
	0x6F12, 0x1700,
	0x6F12, 0x9387,
	0x6F12, 0x01FE,
	0x6F12, 0x8146,
	0x6F12, 0x19C7,
	0x6F12, 0x3747,
	0x6F12, 0x0024,
	0x6F12, 0x8346,
	0x6F12, 0x87BE,
	0x6F12, 0xB336,
	0x6F12, 0xD000,
	0x6F12, 0x03C6,
	0x6F12, 0x2700,
	0x6F12, 0x0147,
	0x6F12, 0x19C6,
	0x6F12, 0x3747,
	0x6F12, 0x0024,
	0x6F12, 0x0347,
	0x6F12, 0x47F8,
	0x6F12, 0x1337,
	0x6F12, 0x1700,
	0x6F12, 0x03C6,
	0x6F12, 0x3700,
	0x6F12, 0x1207,
	0x6F12, 0xD98E,
	0x6F12, 0x0147,
	0x6F12, 0x09CA,
	0x6F12, 0x3757,
	0x6F12, 0x0024,
	0x6F12, 0x1307,
	0x6F12, 0x078F,
	0x6F12, 0x0347,
	0x6F12, 0x07C2,
	0x6F12, 0x1337,
	0x6F12, 0x1700,
	0x6F12, 0x2207,
	0x6F12, 0x558F,
	0x6F12, 0xB7D6,
	0x6F12, 0x0040,
	0x6F12, 0x2391,
	0x6F12, 0xE600,
	0x6F12, 0x03D7,
	0x6F12, 0xA700,
	0x6F12, 0x239D,
	0x6F12, 0xE60C,
	0x6F12, 0x3717,
	0x6F12, 0x0024,
	0x6F12, 0x0357,
	0x6F12, 0x0725,
	0x6F12, 0x1207,
	0x6F12, 0x1367,
	0x6F12, 0x1700,
	0x6F12, 0x4207,
	0x6F12, 0x4183,
	0x6F12, 0x2394,
	0x6F12, 0xE600,
	0x6F12, 0x03C7,
	0x6F12, 0x0703,
	0x6F12, 0x39C3,
	0x6F12, 0x3747,
	0x6F12, 0x0024,
	0x6F12, 0x9306,
	0x6F12, 0x078F,
	0x6F12, 0x83D6,
	0x6F12, 0x066A,
	0x6F12, 0x1166,
	0x6F12, 0x7D16,
	0x6F12, 0x1307,
	0x6F12, 0x078F,
	0x6F12, 0x6381,
	0x6F12, 0xC61E,
	0x6F12, 0x8506,
	0x6F12, 0x2310,
	0x6F12, 0xD76A,
	0x6F12, 0x8356,
	0x6F12, 0x276A,
	0x6F12, 0x1166,
	0x6F12, 0x7D16,
	0x6F12, 0x638A,
	0x6F12, 0xC61C,
	0x6F12, 0x8506,
	0x6F12, 0x0356,
	0x6F12, 0x076A,
	0x6F12, 0x2311,
	0x6F12, 0xD76A,
	0x6F12, 0xB7D6,
	0x6F12, 0x0040,
	0x6F12, 0x2397,
	0x6F12, 0xC600,
	0x6F12, 0x0357,
	0x6F12, 0x276A,
	0x6F12, 0x239B,
	0x6F12, 0xE60C,
	0x6F12, 0x03D7,
	0x6F12, 0x4700,
	0x6F12, 0xB7D6,
	0x6F12, 0x0040,
	0x6F12, 0x2390,
	0x6F12, 0xE60E,
	0x6F12, 0x0347,
	0x6F12, 0x5409,
	0x6F12, 0x239C,
	0x6F12, 0xE60C,
	0x6F12, 0x03C7,
	0x6F12, 0x6700,
	0x6F12, 0x239F,
	0x6F12, 0xE60C,
	0x6F12, 0x03C7,
	0x6F12, 0x8700,
	0x6F12, 0x03C6,
	0x6F12, 0x7700,
	0x6F12, 0x1207,
	0x6F12, 0x518F,
	0x6F12, 0x239E,
	0x6F12, 0xE60C,
	0x6F12, 0x03C7,
	0x6F12, 0xA701,
	0x6F12, 0x4DEB,
	0x6F12, 0x83D6,
	0x6F12, 0xC701,
	0x6F12, 0x3766,
	0x6F12, 0x0024,
	0x6F12, 0x1306,
	0x6F12, 0x0652,
	0x6F12, 0x6380,
	0x6F12, 0x0618,
	0x6F12, 0x8326,
	0x6F12, 0x860A,
	0x6F12, 0xC177,
	0x6F12, 0xF58F,
	0x6F12, 0x91CB,
	0x6F12, 0xB706,
	0x6F12, 0x0101,
	0x6F12, 0x6385,
	0x6F12, 0xD716,
	0x6F12, 0xB706,
	0x6F12, 0x0001,
	0x6F12, 0x6393,
	0x6F12, 0xD700,
	0x6F12, 0x0947,
	0x6F12, 0x9306,
	0x6F12, 0x9706,
	0x6F12, 0xB747,
	0x6F12, 0x0024,
	0x6F12, 0x9387,
	0x6F12, 0x078F,
	0x6F12, 0x1207,
	0x6F12, 0x9206,
	0x6F12, 0xBE96,
	0x6F12, 0xBA97,
	0x6F12, 0x83D6,
	0x6F12, 0xC601,
	0x6F12, 0x83D5,
	0x6F12, 0xE76A,
	0x6F12, 0x03D7,
	0x6F12, 0x076B,
	0x6F12, 0x03D5,
	0x6F12, 0x276B,
	0x6F12, 0x83D9,
	0x6F12, 0x476B,
	0x6F12, 0x03D9,
	0x6F12, 0x676B,
	0x6F12, 0x83D4,
	0x6F12, 0x876B,
	0x6F12, 0x03D4,
	0x6F12, 0xA76B,
	0x6F12, 0x8327,
	0x6F12, 0x463B,
	0x6F12, 0x0946,
	0x6F12, 0x6380,
	0x6F12, 0xC702,
	0x6F12, 0x0546,
	0x6F12, 0x6389,
	0x6F12, 0xC700,
	0x6F12, 0xB717,
	0x6F12, 0x0024,
	0x6F12, 0x03D6,
	0x6F12, 0x6738,
	0x6F12, 0xAD47,
	0x6F12, 0x6306,
	0x6F12, 0xF600,
	0x6F12, 0x2A84,
	0x6F12, 0x2E89,
	0x6F12, 0xBA84,
	0x6F12, 0xB689,
	0x6F12, 0x97A0,
	0x6F12, 0x00FC,
	0x6F12, 0xE780,
	0x6F12, 0x204D,
	0x6F12, 0x19C5,
	0x6F12, 0xCA87,
	0x6F12, 0x2289,
	0x6F12, 0x3E84,
	0x6F12, 0xCE87,
	0x6F12, 0xA689,
	0x6F12, 0xBE84,
	0x6F12, 0xB7E7,
	0x6F12, 0x0040,
	0x6F12, 0x2391,
	0x6F12, 0x3781,
	0x6F12, 0x2393,
	0x6F12, 0x2781,
	0x6F12, 0x2392,
	0x6F12, 0x9780,
	0x6F12, 0x2394,
	0x6F12, 0x8780,
	0x6F12, 0x1743,
	0x6F12, 0x01FC,
	0x6F12, 0x6700,
	0x6F12, 0xE3B9,
	0x6F12, 0x37D7,
	0x6F12, 0x0040,
	0x6F12, 0xB795,
	0x6F12, 0x0024,
	0x6F12, 0x8D47,
	0x6F12, 0x37D9,
	0x6F12, 0x0040,
	0x6F12, 0x2319,
	0x6F12, 0xF70A,
	0x6F12, 0x1385,
	0x6F12, 0x8581,
	0x6F12, 0x1308,
	0x6F12, 0x0701,
	0x6F12, 0x9385,
	0x6F12, 0x8581,
	0x6F12, 0x1307,
	0x6F12, 0x0703,
	0x6F12, 0x9308,
	0x6F12, 0x090B,
	0x6F12, 0x034E,
	0x6F12, 0x0510,
	0x6F12, 0x9441,
	0x6F12, 0x2107,
	0x6F12, 0x1105,
	0x6F12, 0xB3D6,
	0x6F12, 0xC601,
	0x6F12, 0xC206,
	0x6F12, 0xC182,
	0x6F12, 0x231C,
	0x6F12, 0xD7FE,
	0x6F12, 0x8347,
	0x6F12, 0xD50F,
	0x6F12, 0xD441,
	0x6F12, 0x0908,
	0x6F12, 0xC105,
	0x6F12, 0xB3D6,
	0x6F12, 0xF600,
	0x6F12, 0xC206,
	0x6F12, 0xC182,
	0x6F12, 0x231D,
	0x6F12, 0xD7FE,
	0x6F12, 0x0346,
	0x6F12, 0xE50F,
	0x6F12, 0x83A6,
	0x6F12, 0x85FF,
	0x6F12, 0x9207,
	0x6F12, 0xB3D6,
	0x6F12, 0xC600,
	0x6F12, 0xC206,
	0x6F12, 0xC182,
	0x6F12, 0x231E,
	0x6F12, 0xD7FE,
	0x6F12, 0x0343,
	0x6F12, 0xF50F,
	0x6F12, 0x2206,
	0x6F12, 0xD18F,
	0x6F12, 0x9316,
	0x6F12, 0xC300,
	0x6F12, 0xB3E7,
	0x6F12, 0xC701,
	0x6F12, 0xD58F,
	0x6F12, 0x83A6,
	0x6F12, 0xC5FF,
	0x6F12, 0xC207,
	0x6F12, 0xC183,
	0x6F12, 0xB3D6,
	0x6F12, 0x6600,
	0x6F12, 0xC206,
	0x6F12, 0xC182,
	0x6F12, 0x231F,
	0x6F12, 0xD7FE,
	0x6F12, 0x231F,
	0x6F12, 0xF8FE,
	0x6F12, 0xE31A,
	0x6F12, 0x17F9,
	0x6F12, 0x9780,
	0x6F12, 0x00FC,
	0x6F12, 0xE780,
	0x6F12, 0xC003,
	0x6F12, 0xAA89,
	0x6F12, 0x9780,
	0x6F12, 0x00FC,
	0x6F12, 0xE780,
	0x6F12, 0x8002,
	0x6F12, 0x9307,
	0x6F12, 0x0008,
	0x6F12, 0x33D5,
	0x6F12, 0xA700,
	0x6F12, 0x9307,
	0x6F12, 0x0004,
	0x6F12, 0x1205,
	0x6F12, 0xB3D7,
	0x6F12, 0x3701,
	0x6F12, 0x1375,
	0x6F12, 0x0503,
	0x6F12, 0x8D8B,
	0x6F12, 0x5D8D,
	0x6F12, 0x2319,
	0x6F12, 0xA90C,
	0x6F12, 0x59B3,
	0x6F12, 0x8546,
	0x6F12, 0x0DB5,
	0x6F12, 0x8546,
	0x6F12, 0x05BD,
	0x6F12, 0x0547,
	0x6F12, 0x4DB5,
	0x6F12, 0x83D6,
	0x6F12, 0xE701,
	0x6F12, 0x83D5,
	0x6F12, 0x2702,
	0x6F12, 0x03D7,
	0x6F12, 0x0702,
	0x6F12, 0x03D5,
	0x6F12, 0x4702,
	0x6F12, 0x83D9,
	0x6F12, 0x6702,
	0x6F12, 0x03D9,
	0x6F12, 0xA702,
	0x6F12, 0x83D4,
	0x6F12, 0x8702,
	0x6F12, 0x03D4,
	0x6F12, 0xC702,
	0x6F12, 0x55BD,
	0x6F12, 0x1743,
	0x6F12, 0x01FC,
	0x6F12, 0xE702,
	0x6F12, 0xC3A6,
	0x6F12, 0xB797,
	0x6F12, 0x0024,
	0x6F12, 0x83A4,
	0x6F12, 0x8796,
	0x6F12, 0xAA89,
	0x6F12, 0x2E8A,
	0x6F12, 0x0146,
	0x6F12, 0xA685,
	0x6F12, 0x1145,
	0x6F12, 0x9780,
	0x6F12, 0xFFFB,
	0x6F12, 0xE780,
	0x6F12, 0xE0EB,
	0x6F12, 0xB787,
	0x6F12, 0x0024,
	0x6F12, 0x03C7,
	0x6F12, 0x8776,
	0x6F12, 0x1384,
	0x6F12, 0x8776,
	0x6F12, 0x0149,
	0x6F12, 0x11CF,
	0x6F12, 0x3767,
	0x6F12, 0x0024,
	0x6F12, 0x0357,
	0x6F12, 0x2777,
	0x6F12, 0xB777,
	0x6F12, 0x0024,
	0x6F12, 0x9387,
	0x6F12, 0x07C7,
	0x6F12, 0x0E07,
	0x6F12, 0x03D9,
	0x6F12, 0x871C,
	0x6F12, 0x2394,
	0x6F12, 0xE71C,
	0x6F12, 0xD285,
	0x6F12, 0x4E85,
	0x6F12, 0x97D0,
	0x6F12, 0xFFFB,
	0x6F12, 0xE780,
	0x6F12, 0xC0B2,
	0x6F12, 0x8347,
	0x6F12, 0x0400,
	0x6F12, 0x89C7,
	0x6F12, 0xB777,
	0x6F12, 0x0024,
	0x6F12, 0x239C,
	0x6F12, 0x27E3,
	0x6F12, 0x0546,
	0x6F12, 0xA685,
	0x6F12, 0x1145,
	0x6F12, 0x9780,
	0x6F12, 0xFFFB,
	0x6F12, 0xE780,
	0x6F12, 0xC0E6,
	0x6F12, 0x1743,
	0x6F12, 0x01FC,
	0x6F12, 0x6700,
	0x6F12, 0xC3A2,
	0x6F12, 0x1743,
	0x6F12, 0x01FC,
	0x6F12, 0xE702,
	0x6F12, 0xA3A0,
	0x6F12, 0xB797,
	0x6F12, 0x0024,
	0x6F12, 0x03A4,
	0x6F12, 0x4796,
	0x6F12, 0x0146,
	0x6F12, 0x1145,
	0x6F12, 0xA285,
	0x6F12, 0x9780,
	0x6F12, 0xFFFB,
	0x6F12, 0xE780,
	0x6F12, 0x60E4,
	0x6F12, 0x9710,
	0x6F12, 0x00FC,
	0x6F12, 0xE780,
	0x6F12, 0xE026,
	0x6F12, 0xB785,
	0x6F12, 0x0024,
	0x6F12, 0x3795,
	0x6F12, 0x0024,
	0x6F12, 0x1146,
	0x6F12, 0x9385,
	0x6F12, 0x057F,
	0x6F12, 0x1305,
	0x6F12, 0x4597,
	0x6F12, 0x9780,
	0x6F12, 0xFFFB,
	0x6F12, 0xE780,
	0x6F12, 0x0007,
	0x6F12, 0x0546,
	0x6F12, 0xA285,
	0x6F12, 0x1145,
	0x6F12, 0x9780,
	0x6F12, 0xFFFB,
	0x6F12, 0xE780,
	0x6F12, 0x60E1,
	0x6F12, 0x1743,
	0x6F12, 0x01FC,
	0x6F12, 0x6700,
	0x6F12, 0x039E,
	0x6F12, 0x1743,
	0x6F12, 0x01FC,
	0x6F12, 0xE702,
	0x6F12, 0xA399,
	0x6F12, 0xB797,
	0x6F12, 0x0024,
	0x6F12, 0x03A4,
	0x6F12, 0x0796,
	0x6F12, 0xAA84,
	0x6F12, 0x2E89,
	0x6F12, 0xB289,
	0x6F12, 0xA285,
	0x6F12, 0x0146,
	0x6F12, 0x1145,
	0x6F12, 0x9780,
	0x6F12, 0xFFFB,
	0x6F12, 0xE780,
	0x6F12, 0xA0DE,
	0x6F12, 0xB787,
	0x6F12, 0x0024,
	0x6F12, 0x83C7,
	0x6F12, 0x477F,
	0x6F12, 0x95C3,
	0x6F12, 0x3777,
	0x6F12, 0x0024,
	0x6F12, 0x1307,
	0x6F12, 0x07C7,
	0x6F12, 0xB796,
	0x6F12, 0x0024,
	0x6F12, 0x8347,
	0x6F12, 0x970C,
	0x6F12, 0x83C6,
	0x6F12, 0x8697,
	0x6F12, 0x93F7,
	0x6F12, 0xC7FC,
	0x6F12, 0x93F6,
	0x6F12, 0x3603,
	0x6F12, 0xD58F,
	0x6F12, 0xA304,
	0x6F12, 0xF70C,
	0x6F12, 0x4E86,
	0x6F12, 0xCA85,
	0x6F12, 0x2685,
	0x6F12, 0x97F0,
	0x6F12, 0x00FC,
	0x6F12, 0xE780,
	0x6F12, 0xE088,
	0x6F12, 0x0546,
	0x6F12, 0xA285,
	0x6F12, 0x1145,
	0x6F12, 0x9780,
	0x6F12, 0xFFFB,
	0x6F12, 0xE780,
	0x6F12, 0x20DA,
	0x6F12, 0x1743,
	0x6F12, 0x01FC,
	0x6F12, 0x6700,
	0x6F12, 0x2396,
	0x6F12, 0x1743,
	0x6F12, 0x01FC,
	0x6F12, 0xE702,
	0x6F12, 0x0394,
	0x6F12, 0xB797,
	0x6F12, 0x0024,
	0x6F12, 0x03A4,
	0x6F12, 0xC795,
	0x6F12, 0xAA84,
	0x6F12, 0x0146,
	0x6F12, 0xA285,
	0x6F12, 0x1145,
	0x6F12, 0x9780,
	0x6F12, 0xFFFB,
	0x6F12, 0xE780,
	0x6F12, 0xA0D7,
	0x6F12, 0x83D7,
	0x6F12, 0x2401,
	0x6F12, 0x9DCB,
	0x6F12, 0xB737,
	0x6F12, 0x0024,
	0x6F12, 0x03A7,
	0x6F12, 0x875C,
	0x6F12, 0xB786,
	0x6F12, 0x0024,
	0x6F12, 0x83C6,
	0x6F12, 0x467F,
	0x6F12, 0x8347,
	0x6F12, 0xC759,
	0x6F12, 0xA1C3,
	0x6F12, 0x99CE,
	0x6F12, 0xB776,
	0x6F12, 0x0024,
	0x6F12, 0x83C6,
	0x6F12, 0x4640,
	0x6F12, 0x3797,
	0x6F12, 0x0024,
	0x6F12, 0x1307,
	0x6F12, 0x4797,
	0x6F12, 0x958F,
	0x6F12, 0xBA97,
	0x6F12, 0x83C7,
	0x6F12, 0x0700,
	0x6F12, 0x2302,
	0x6F12, 0xF700,
	0x6F12, 0x2685,
	0x6F12, 0x9790,
	0x6F12, 0x00FC,
	0x6F12, 0xE780,
	0x6F12, 0x80B9,
	0x6F12, 0x0546,
	0x6F12, 0xA285,
	0x6F12, 0x1145,
	0x6F12, 0x9780,
	0x6F12, 0xFFFB,
	0x6F12, 0xE780,
	0x6F12, 0x80D2,
	0x6F12, 0x1743,
	0x6F12, 0x01FC,
	0x6F12, 0x6700,
	0x6F12, 0x238F,
	0x6F12, 0xE5D2,
	0x6F12, 0x8347,
	0x6F12, 0x070E,
	0x6F12, 0x0347,
	0x6F12, 0xF757,
	0x6F12, 0x8D8B,
	0x6F12, 0x1207,
	0x6F12, 0xD98F,
	0x6F12, 0x3797,
	0x6F12, 0x0024,
	0x6F12, 0x230C,
	0x6F12, 0xF796,
	0x6F12, 0xE1B7,
	0x6F12, 0x1743,
	0x6F12, 0x01FC,
	0x6F12, 0xE702,
	0x6F12, 0xC38A,
	0x6F12, 0xB797,
	0x6F12, 0x0024,
	0x6F12, 0x83A4,
	0x6F12, 0x8795,
	0x6F12, 0x2A84,
	0x6F12, 0x0146,
	0x6F12, 0xA685,
	0x6F12, 0x1145,
	0x6F12, 0x9780,
	0x6F12, 0xFFFB,
	0x6F12, 0xE780,
	0x6F12, 0x60CE,
	0x6F12, 0x2285,
	0x6F12, 0x9790,
	0x6F12, 0x00FC,
	0x6F12, 0xE780,
	0x6F12, 0x40BE,
	0x6F12, 0xB785,
	0x6F12, 0x0024,
	0x6F12, 0x9385,
	0x6F12, 0x8576,
	0x6F12, 0x83C7,
	0x6F12, 0xC508,
	0x6F12, 0x95C7,
	0x6F12, 0xB737,
	0x6F12, 0x0024,
	0x6F12, 0x83A7,
	0x6F12, 0x875C,
	0x6F12, 0x0547,
	0x6F12, 0x3794,
	0x6F12, 0x0024,
	0x6F12, 0x83C7,
	0x6F12, 0xC759,
	0x6F12, 0x6397,
	0x6F12, 0xE702,
	0x6F12, 0x83C7,
	0x6F12, 0x2508,
	0x6F12, 0x230A,
	0x6F12, 0xF496,
	0x6F12, 0x0347,
	0x6F12, 0x4497,
	0x6F12, 0x9307,
	0x6F12, 0x4497,
	0x6F12, 0x2382,
	0x6F12, 0xE700,
	0x6F12, 0x0546,
	0x6F12, 0xA685,
	0x6F12, 0x1145,
	0x6F12, 0x9780,
	0x6F12, 0xFFFB,
	0x6F12, 0xE780,
	0x6F12, 0x60C9,
	0x6F12, 0x1743,
	0x6F12, 0x01FC,
	0x6F12, 0x6700,
	0x6F12, 0x0386,
	0x6F12, 0x0947,
	0x6F12, 0x639C,
	0x6F12, 0xE700,
	0x6F12, 0x0946,
	0x6F12, 0x9385,
	0x6F12, 0x3508,
	0x6F12, 0x1305,
	0x6F12, 0x4497,
	0x6F12, 0x9780,
	0x6F12, 0xFFFB,
	0x6F12, 0xE780,
	0x6F12, 0x20EC,
	0x6F12, 0xD9B7,
	0x6F12, 0x0D47,
	0x6F12, 0x6396,
	0x6F12, 0xE700,
	0x6F12, 0x0D46,
	0x6F12, 0x9385,
	0x6F12, 0x5508,
	0x6F12, 0xDDB7,
	0x6F12, 0x1147,
	0x6F12, 0xE39A,
	0x6F12, 0xE7FA,
	0x6F12, 0x1146,
	0x6F12, 0x9385,
	0x6F12, 0x8508,
	0x6F12, 0xE1BF,
	0x6F12, 0xE177,
	0x6F12, 0x3747,
	0x6F12, 0x0024,
	0x6F12, 0x9387,
	0x6F12, 0x5776,
	0x6F12, 0x2318,
	0x6F12, 0xF782,
	0x6F12, 0x8280,
	0x6F12, 0x0000,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x0020,
	0x6F12, 0x1010,
	0x6F12, 0x3210,
	0x6F12, 0x3210,
	0x6F12, 0x1032,
	0x6F12, 0x1010,
	0x6F12, 0x0100,
	0x6F12, 0x0000,
	0x6F12, 0x0001,
	0x6F12, 0x0203,
	0x6F12, 0x0001,
	0x6F12, 0x0203,
	0x6F12, 0x0405,
	0x6F12, 0x0607,
	0x6F12, 0x0405,
	0x6F12, 0x0607,
	0x6F12, 0x0809,
	0x6F12, 0x0A0B,
	0x6F12, 0x0809,
	0x6F12, 0x0A0B,
	0x6F12, 0x0C0D,
	0x6F12, 0x0E0F,
	0x6F12, 0x0C0D,
	0x6F12, 0x0E0F,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x6F12, 0x0000,
	0x602A, 0x35cc,
	0x6F12, 0x1c80,
	0x6F12, 0x0024,
	0xFCFC, 0x2400,
	0x0650, 0x0600,
	0x0654, 0x0000,
	0x065A, 0x0000,
	0x0668, 0x0800,
	0x066A, 0x0800,
	0x066C, 0x0800,
	0x066E, 0x0800,
	0x0674, 0x0500,
	0x0676, 0x0500,
	0x0678, 0x0500,
	0x067A, 0x0500,
	0x0684, 0x4001,
	0x0688, 0x4001,
	0x06B6, 0x0A00,
	0x06BC, 0x1001,
	0x06FA, 0x1000,
	0x0812, 0x0000,
	0x0A76, 0x1000,
	0x0AEE, 0x1000,
	0x0B66, 0x1000,
	0x0BDE, 0x1000,
	0x0C56, 0x1000,
	0x0CF0, 0x0101,
	0x0CF2, 0x0101,
	0x120E, 0x1000,
	0x1236, 0x0000,
	0x1354, 0x0100,
	0x1356, 0x7017,
	0x1378, 0x6038,
	0x137A, 0x7038,
	0x137C, 0x8038,
	0x1386, 0x0B00,
	0x13B2, 0x0000,
	0x1A0A, 0x4C0A,
	0x1A0E, 0x9600,
	0x1A28, 0x4C00,
	0x1B26, 0x6F80,
	0x2042, 0x1A00,
	0x208C, 0xD244,
	0x208E, 0x14F4,
	0x2140, 0x0101,
	0x2148, 0x0100,
	0x2176, 0x6400,
	0x218E, 0x0000,
	0x21E4, 0x0400,
	0x2210, 0x3401,
	0x222E, 0x0001,
	0x3570, 0x0000,
	0x4A74, 0x0000,
	0x4A76, 0x0000,
	0x4A7A, 0x0000,
	0x4A7C, 0x0000,
	0x4A7E, 0x0000,
	0x4A80, 0x0000,
	0x4A82, 0x0000,
	0x4A86, 0x0000,
	0x4A88, 0x0000,
	0x4A8A, 0x0000,
	0x4A8C, 0x0000,
	0x4A8E, 0x0000,
	0x4A90, 0x0000,
	0x4A92, 0x0000,
	0x7F6C, 0x0100,
	0xFCFC, 0x4000,
	0x0118, 0x0002,
	0x011A, 0x0001,
	0x0136, 0x1800,
	0x0300, 0x0006,
	0x0302, 0x0001,
	0x0304, 0x0004,
	0x0308, 0x0008,
	0x030A, 0x0001,
	0x030C, 0x0000,
	0x030E, 0x0004,
	0x0312, 0x0000,
	0x080E, 0x0000,
	0x0D00, 0x0101,
	0xF44A, 0x0006,
	0xF44C, 0x0B0B,
	0xF44E, 0x0011,
	0xF46A, 0xAE80,
}; /* sensor_init_setting_array */

static kal_uint16 preview_setting_array[] = {
	0xFCFC, 0x2400,
	0x0710, 0x0002,
	0x0712, 0x0804,
	0x0714, 0x0100,
	0x0718, 0x0001,
	0x0786, 0x7701,
	0x0874, 0x0100,
	0x09C0, 0x2008,
	0x09C4, 0x2000,
	0x0BE8, 0x3000,
	0x0BEA, 0x3000,
	0x0C60, 0x3000,
	0x0C62, 0x3000,
	0x11B8, 0x0100,
	0x11F6, 0x0020,
	0x1360, 0x0100,
	0x1376, 0x0100,
	0x139C, 0x0000,
	0x139E, 0x0100,
	0x13A0, 0x0A00,
	0x13A2, 0x0120,
	0x13AE, 0x0101,
	0x1444, 0x2000,
	0x1446, 0x2000,
	0x144C, 0x3F00,
	0x144E, 0x3F00,
	0x147C, 0x1000,
	0x1480, 0x1000,
	0x19E6, 0x0200,
	0x19F4, 0x0606,
	0x19F6, 0x0904,
	0x19F8, 0x1010,
	0x19FC, 0x0B00,
	0x19FE, 0x0E1C,
	0x1A02, 0x1800,
	0x1A30, 0x3401,
	0x1A3C, 0x6207,
	0x1A46, 0x8A00,
	0x1A48, 0x6207,
	0x1A52, 0xBF00,
	0x1A64, 0x0301,
	0x1A66, 0xFF00,
	0x1B28, 0xA060,
	0x1B5C, 0x0000,
	0x2022, 0x0500,
	0x2024, 0x0500,
	0x2072, 0x0000,
	0x2080, 0x0101,
	0x2082, 0xFF00,
	0x2084, 0x7F01,
	0x2086, 0x0001,
	0x2088, 0x8001,
	0x208A, 0xD244,
	0x2090, 0x0000,
	0x2092, 0x0000,
	0x2094, 0x0000,
	0x20BA, 0x141C,
	0x20BC, 0x111C,
	0x20BE, 0x54F4,
	0x212E, 0x0200,
	0x21EC, 0x1F04,
	0x3574, 0x1201,
	0x4A78, 0xD8FF,
	0x4A84, 0xD8FF,
	0x4A94, 0x0900,
	0x4A96, 0x0600,
	0x4A98, 0x0300,
	0x4A9A, 0x0600,
	0x4A9C, 0x0600,
	0x4A9E, 0x0600,
	0x4AA0, 0x0600,
	0x4AA2, 0x0600,
	0x4AA4, 0x0300,
	0x4AA6, 0x0600,
	0x4AA8, 0x0900,
	0x4AAA, 0x0600,
	0x4AAC, 0x0600,
	0x4AAE, 0x0600,
	0x4AB0, 0x0600,
	0x4AB2, 0x0600,
	0x4D92, 0x0100,
	0x4D94, 0x0005,
	0x4D96, 0x000A,
	0x4D98, 0x0010,
	0x4D9A, 0x0810,
	0x4D9C, 0x000A,
	0x4D9E, 0x0040,
	0x4DA0, 0x0810,
	0x4DA2, 0x0810,
	0x4DA4, 0x8002,
	0x4DA6, 0xFD03,
	0x4DA8, 0x0010,
	0x4DAA, 0x1510,
	0x7F6E, 0x2F00,
	0x7F70, 0xFA00,
	0x7F72, 0x2400,
	0x7F74, 0xE500,
	0x8768, 0x0100,
	0xFCFC, 0x4000,
	0x0114, 0x0301,
	0x0306, 0x008C,
	0x0310, 0x007B,
	0x0340, 0x0E78,
	0x0342, 0x1384,
	0x0344, 0x0000,
	0x0346, 0x0000,
	0x0348, 0x1FFF,
	0x034A, 0x181F,
	0x034C, 0x0FF0,
	0x034E, 0x0C00,
	0x0350, 0x0008,
	0x0352, 0x0008,
	0x0380, 0x0002,
	0x0382, 0x0002,
	0x0384, 0x0002,
	0x0386, 0x0002,
	0x0900, 0x0122,
	0x0D02, 0x0101,
	0x0D04, 0x0102,
	0x6226, 0x0000,
	//0x0100, 0x0100,
}; /* preview_setting_array */

static kal_uint16 capture_setting_array[] = {
	0xFCFC, 0x2400,
	0x0710, 0x0002,
	0x0712, 0x0804,
	0x0714, 0x0100,
	0x0718, 0x0001,
	0x0786, 0x7701,
	0x0874, 0x0100,
	0x09C0, 0x2008,
	0x09C4, 0x2000,
	0x0BE8, 0x3000,
	0x0BEA, 0x3000,
	0x0C60, 0x3000,
	0x0C62, 0x3000,
	0x11B8, 0x0100,
	0x11F6, 0x0020,
	0x1360, 0x0100,
	0x1376, 0x0100,
	0x139C, 0x0000,
	0x139E, 0x0100,
	0x13A0, 0x0A00,
	0x13A2, 0x0120,
	0x13AE, 0x0101,
	0x1444, 0x2000,
	0x1446, 0x2000,
	0x144C, 0x3F00,
	0x144E, 0x3F00,
	0x147C, 0x1000,
	0x1480, 0x1000,
	0x19E6, 0x0200,
	0x19F4, 0x0606,
	0x19F6, 0x0904,
	0x19F8, 0x1010,
	0x19FC, 0x0B00,
	0x19FE, 0x0E1C,
	0x1A02, 0x1800,
	0x1A30, 0x3401,
	0x1A3C, 0x6207,
	0x1A46, 0x8A00,
	0x1A48, 0x6207,
	0x1A52, 0xBF00,
	0x1A64, 0x0301,
	0x1A66, 0xFF00,
	0x1B28, 0xA060,
	0x1B5C, 0x0000,
	0x2022, 0x0500,
	0x2024, 0x0500,
	0x2072, 0x0000,
	0x2080, 0x0101,
	0x2082, 0xFF00,
	0x2084, 0x7F01,
	0x2086, 0x0001,
	0x2088, 0x8001,
	0x208A, 0xD244,
	0x2090, 0x0000,
	0x2092, 0x0000,
	0x2094, 0x0000,
	0x20BA, 0x141C,
	0x20BC, 0x111C,
	0x20BE, 0x54F4,
	0x212E, 0x0200,
	0x21EC, 0x1F04,
	0x3574, 0x1201,
	0x4A78, 0xD8FF,
	0x4A84, 0xD8FF,
	0x4A94, 0x0900,
	0x4A96, 0x0600,
	0x4A98, 0x0300,
	0x4A9A, 0x0600,
	0x4A9C, 0x0600,
	0x4A9E, 0x0600,
	0x4AA0, 0x0600,
	0x4AA2, 0x0600,
	0x4AA4, 0x0300,
	0x4AA6, 0x0600,
	0x4AA8, 0x0900,
	0x4AAA, 0x0600,
	0x4AAC, 0x0600,
	0x4AAE, 0x0600,
	0x4AB0, 0x0600,
	0x4AB2, 0x0600,
	0x4D92, 0x0100,
	0x4D94, 0x0005,
	0x4D96, 0x000A,
	0x4D98, 0x0010,
	0x4D9A, 0x0810,
	0x4D9C, 0x000A,
	0x4D9E, 0x0040,
	0x4DA0, 0x0810,
	0x4DA2, 0x0810,
	0x4DA4, 0x8002,
	0x4DA6, 0xFD03,
	0x4DA8, 0x0010,
	0x4DAA, 0x1510,
	0x7F6E, 0x2F00,
	0x7F70, 0xFA00,
	0x7F72, 0x2400,
	0x7F74, 0xE500,
	0x8768, 0x0100,
	0xFCFC, 0x4000,
	0x0114, 0x0301,
	0x0306, 0x008C,
	0x0310, 0x007B,
	0x0340, 0x0E78,
	0x0342, 0x1384,
	0x0344, 0x0000,
	0x0346, 0x0000,
	0x0348, 0x1FFF,
	0x034A, 0x181F,
	0x034C, 0x0FF0,
	0x034E, 0x0C00,
	0x0350, 0x0008,
	0x0352, 0x0008,
	0x0380, 0x0002,
	0x0382, 0x0002,
	0x0384, 0x0002,
	0x0386, 0x0002,
	0x0900, 0x0122,
	0x0D02, 0x0101,
	0x0D04, 0x0102,
	0x6226, 0x0000,
	//0x0100, 0x0100,
};

static kal_uint16 normal_video_setting_array[] = {
	0xFCFC,	0x2400,
	0x1A28,	0x4c00,
	0x065a,	0x0000,
	0x1B26,	0x6F80,
	0x7F6C,	0x0100,
	0x7F6E,	0x2f00,
	0x7F70,	0x0100,
	0x7F72,	0x2f00,
	0x7F74,	0xE500,
	0x0650,	0x0600,
	0x0654,	0x0000,
	0x0674,	0x0500,
	0x0676,	0x0500,
	0x0678,	0x0500,
	0x067a,	0x0500,
	0x0668,	0x0800,
	0x066A,	0x0800,
	0x066C,	0x0800,
	0x066E,	0x0800,
	0x0684,	0x4001,
	0x0688,	0x4001,
	0x0812,	0x0000,
	0x2148,	0x0100,
	0x2042,	0x1A00,
	0x84C8,	0x0100,
	0x3570,	0x0000,
	0x21E4,	0x0400,
	0x120E,	0x0100,
	0x1386,	0x0b00,
	0x06FA,	0x0000,
	0x0A76,	0x0100,
	0x0AEE,	0x1000,
	0x0B66,	0x1000,
	0x0BDE,	0x1000,
	0x0C56,	0x1000,
	0x0CB6,	0x0100,
	0x0CF2,	0x0001,
	0x0CF0,	0x0101,
	0x4A74,	0x0000,
	0x4A74, 0x0000,
	0x4A76, 0x0000,
	0x4A78, 0xD8FF,
	0x4A7A, 0x0000,
	0x4A7C, 0x0000,
	0x4A7E, 0x0000,
	0x4A80, 0x0000,
	0x4A82, 0x0000,
	0x4A84, 0xD8FF,
	0x4A86, 0x0000,
	0x4A88, 0x0000,
	0x4A8A, 0x0000,
	0x4A8C, 0x0000,
	0x4A8E, 0x0000,
	0x4A90, 0x0000,
	0x218E,	0x0000,
	0x2268,	0xF279,
	0x5006,	0x0000,
	0x500E,	0x0100,
	0x4E70,	0x2062,
	0x4E72,	0x5501,
	0x06DC,	0x0000,
	0x06DE,	0x0000,
	0x06E0, 0x0000,
	0x06E2, 0x0000,

	0x0710,	0x0002,
	0x0712,	0x0804,
	0x0714,	0x0100,
	0x0718,	0x0001,
	0x0786,	0x7701,
	0x0874,	0x0100,
	0x09C0,	0x2008,
	0x09C4,	0x2000,
	0x0BE8,	0x3000,
	0x0BEA,	0x3000,
	0x0C60,	0x3000,
	0x0C62,	0x3000,
	0x11B8,	0x0100,
	0x11F6,	0x0020,
	0x1360,	0x0100,
	0x1376,	0x0100,
	0x1378,	0x6038,
	0x137A,	0x8038,
	0x139C,	0x0000,
	0x139E,	0x0100,
	0x13A0,	0x0A00,
	0x13A2,	0x0120,
	0x13AE,	0x0101,
	0x1444,	0x2000,
	0x1446,	0x2000,
	0x144C,	0x3F00,
	0x144E,	0x3F00,
	0x147C,	0x1000,
	0x1480,	0x1000,
	0x19E6,	0x0200,
	0x19F4,	0x0606,
	0x19F6,	0x0904,
	0x19F8,	0x1010,
	0x19FC,	0x0B00,
	0x19FE,	0x0E1C,
	0x1A02,	0x1800,
	0x1A30,	0x3401,
	0x1A3C,	0x6207,
	0x1A46,	0x8A00,
	0x1A48,	0x6207,
	0x1A52,	0xBF00,
	0x1A64,	0x0301,
	0x1A66,	0xFF00,
	0x1B28,	0xA060,
	0x1B5C,	0x0000,
	0x2022,	0x0500,
	0x2024,	0x0500,
	0x2072,	0x0000,
	0x2080,	0x0101,
	0x2082,	0xFF00,
	0x2084,	0x7F01,
	0x2086,	0x0001,
	0x2088,	0x8001,
	0x208A,	0xD244,
	0x208c,	0xd244,
	0x208E,	0x14F4,
	0x2090,	0x0000,
	0x2092,	0x0000,
	0x2094,	0x0000,
	0x20BA,	0x121C,
	0x20BC,	0x111C,
	0x20BE,	0x54F4,
	0x212E,	0x0200,
	0x21EC,	0x1F04,
	0x3574,	0x1201,
	0x4A78,	0xD8FF,
	0x4A84,	0xD8FF,
	0x4A94,	0x0900,
	0x4A96,	0x0000,
	0x4A98,	0x0300,
	0x4A9A,	0x0000,
	0x4A9C,	0x0000,
	0x4A9E,	0x0000,
	0x4AA0,	0x0000,
	0x4AA2,	0x0000,
	0x4AA4,	0x0300,
	0x4AA6,	0x0000,
	0x4AA8,	0x0900,
	0x4AAA,	0x0000,
	0x4AAC,	0x0000,
	0x4AAE,	0x0000,
	0x4AB0,	0x0000,
	0x4AB2,	0x0000,
	0x4D92,	0x0100,
	0x4D94,	0x0005,
	0x4D96,	0x000A,
	0x4D98,	0x0010,
	0x4D9A,	0x0810,
	0x4D9C,	0x000A,
	0x4D9E,	0x0040,
	0x4DA0,	0x0810,
	0x4DA2,	0x0810,
	0x4DA4,	0x8002,
	0x4DA6,	0xFD03,
	0x4DA8,	0x0010,
	0x4DAA,	0x1510,
	0x7F6E,	0x2F00,
	0x7F70,	0xFA00,
	0x7F72,	0x2400,
	0x7F74,	0xE500,
	0x8768,	0x0100,
	0xFCFC,	0x4000,
	0xF46A,	0xAe80,
	0x0114,	0x0301,
	0x0306,	0x008C,
	0x030E,	0x0004,
	0x0310,	0x007A,
	0x0340,	0x0E78,
	0x0342,	0x1384,
	0x0344,	0x0000,
	0x0346,	0x0308,
	0x0348,	0x1FFF,
	0x034A,	0x1517,
	0x034C,	0x0FF0,
	0x034E,	0x08F8,
	0x0350,	0x0008,
	0x0352,	0x0008,
	0x0380,	0x0002,
	0x0382,	0x0002,
	0x0384,	0x0002,
	0x0386,	0x0002,
	0x0900,	0x0122,
	0x0D02,	0x0101,
	0x0D04,	0x0102,
	0x6226,	0x0000,
	//0x0100,	0x0100,
};

static kal_uint16 hs_video_setting_array[] = {
	0xFCFC, 0x2400,
	0x0710, 0x0004,
	0x0712, 0x0401,
	0x0714, 0x0100,
	0x0718, 0x0005,
	0x0786, 0x7701,
	0x0874, 0x1100,
	0x09C0, 0x9800,
	0x09C4, 0x9800,
	0x0BE8, 0x3000,
	0x0BEA, 0x3000,
	0x0C60, 0x3000,
	0x0C62, 0x3000,
	0x11B8, 0x0000,
	0x11F6, 0x0010,
	0x1360, 0x0000,
	0x1376, 0x0200,
	0x139C, 0x0000,
	0x139E, 0x0300,
	0x13A0, 0x0A00,
	0x13A2, 0x0020,
	0x13AE, 0x0102,
	0x1444, 0x2100,
	0x1446, 0x2100,
	0x144C, 0x4200,
	0x144E, 0x4200,
	0x147C, 0x1000,
	0x1480, 0x1000,
	0x19E6, 0x0201,
	0x19F4, 0x0606,
	0x19F6, 0x0904,
	0x19F8, 0x1010,
	0x19FC, 0x0B00,
	0x19FE, 0x0E1C,
	0x1A02, 0x0800,
	0x1A30, 0x3401,
	0x1A3C, 0x5207,
	0x1A46, 0x8600,
	0x1A48, 0x5207,
	0x1A52, 0xBF00,
	0x1A64, 0x0301,
	0x1A66, 0x3F00,
	0x1B28, 0xA020,
	0x1B5C, 0x0300,
	0x2022, 0x0101,
	0x2024, 0x0101,
	0x2072, 0x0000,
	0x2080, 0x0100,
	0x2082, 0x7F00,
	0x2084, 0x0002,
	0x2086, 0x8000,
	0x2088, 0x0002,
	0x208A, 0xC244,
	0x2090, 0x161C,
	0x2092, 0x111C,
	0x2094, 0x54F4,
	0x20BA, 0x0000,
	0x20BC, 0x0000,
	0x20BE, 0x0000,
	0x212E, 0x0A00,
	0x21EC, 0x4F01,
	0x3574, 0x9400,
	0x4A78, 0xD8FF,
	0x4A84, 0xD8FF,
	0x4A94, 0x0C00,
	0x4A96, 0x0900,
	0x4A98, 0x0600,
	0x4A9A, 0x0900,
	0x4A9C, 0x0900,
	0x4A9E, 0x0900,
	0x4AA0, 0x0900,
	0x4AA2, 0x0900,
	0x4AA4, 0x0600,
	0x4AA6, 0x0900,
	0x4AA8, 0x0C00,
	0x4AAA, 0x0900,
	0x4AAC, 0x0900,
	0x4AAE, 0x0900,
	0x4AB0, 0x0900,
	0x4AB2, 0x0900,
	0x4D92, 0x0100,
	0x4D94, 0x4001,
	0x4D96, 0x0004,
	0x4D98, 0x0010,
	0x4D9A, 0x0810,
	0x4D9C, 0x0004,
	0x4D9E, 0x0010,
	0x4DA0, 0x0810,
	0x4DA2, 0x0810,
	0x4DA4, 0x0000,
	0x4DA6, 0x0000,
	0x4DA8, 0x0010,
	0x4DAA, 0x0010,
	0x7F6E, 0x3100,
	0x7F70, 0xF700,
	0x7F72, 0x2600,
	0x7F74, 0xE100,
	0x8768, 0x0100,
	0xFCFC, 0x4000,
	0x0114, 0x0300,
	0x0306, 0x0096,
	0x0310, 0x007D,
	0x0340, 0x074A,
	0x0342, 0x0A70,
	0x0344, 0x0040,
	0x0346, 0x0320,
	0x0348, 0x1FBF,
	0x034A, 0x14FF,
	0x034C, 0x07D0,
	0x034E, 0x046C,
	0x0350, 0x0008,
	0x0352, 0x0006,
	0x0380, 0x0002,
	0x0382, 0x0006,
	0x0384, 0x0002,
	0x0386, 0x0006,
	0x0900, 0x0144,
	0x0D02, 0x0001,
	0x0D04, 0x0002,
	0x6226, 0x0000,
	//0x0100, 0x0100,
};

static kal_uint16 slim_video_setting_array[] = {
	0xFCFC, 0x2400,
	0x0710, 0x0004,
	0x0712, 0x0401,
	0x0714, 0x0100,
	0x0718, 0x0005,
	0x0786, 0x7701,
	0x0874, 0x1100,
	0x09C0, 0x9800,
	0x09C4, 0x9800,
	0x0BE8, 0x3000,
	0x0BEA, 0x3000,
	0x0C60, 0x3000,
	0x0C62, 0x3000,
	0x11B8, 0x0000,
	0x11F6, 0x0010,
	0x1360, 0x0000,
	0x1376, 0x0200,
	0x139C, 0x0000,
	0x139E, 0x0300,
	0x13A0, 0x0A00,
	0x13A2, 0x0020,
	0x13AE, 0x0102,
	0x1444, 0x2100,
	0x1446, 0x2100,
	0x144C, 0x4200,
	0x144E, 0x4200,
	0x147C, 0x1000,
	0x1480, 0x1000,
	0x19E6, 0x0201,
	0x19F4, 0x0606,
	0x19F6, 0x0904,
	0x19F8, 0x1010,
	0x19FC, 0x0B00,
	0x19FE, 0x0E1C,
	0x1A02, 0x0800,
	0x1A30, 0x3401,
	0x1A3C, 0x5207,
	0x1A46, 0x8600,
	0x1A48, 0x5207,
	0x1A52, 0xBF00,
	0x1A64, 0x0301,
	0x1A66, 0x3F00,
	0x1B28, 0xA020,
	0x1B5C, 0x0300,
	0x2022, 0x0101,
	0x2024, 0x0101,
	0x2072, 0x0000,
	0x2080, 0x0100,
	0x2082, 0x7F00,
	0x2084, 0x0002,
	0x2086, 0x8000,
	0x2088, 0x0002,
	0x208A, 0xC244,
	0x2090, 0x161C,
	0x2092, 0x111C,
	0x2094, 0x54F4,
	0x20BA, 0x0000,
	0x20BC, 0x0000,
	0x20BE, 0x0000,
	0x212E, 0x0A00,
	0x21EC, 0x4F01,
	0x3574, 0x9400,
	0x4A78, 0xD8FF,
	0x4A84, 0xD8FF,
	0x4A94, 0x0C00,
	0x4A96, 0x0900,
	0x4A98, 0x0600,
	0x4A9A, 0x0900,
	0x4A9C, 0x0900,
	0x4A9E, 0x0900,
	0x4AA0, 0x0900,
	0x4AA2, 0x0900,
	0x4AA4, 0x0600,
	0x4AA6, 0x0900,
	0x4AA8, 0x0C00,
	0x4AAA, 0x0900,
	0x4AAC, 0x0900,
	0x4AAE, 0x0900,
	0x4AB0, 0x0900,
	0x4AB2, 0x0900,
	0x4D92, 0x0100,
	0x4D94, 0x4001,
	0x4D96, 0x0004,
	0x4D98, 0x0010,
	0x4D9A, 0x0810,
	0x4D9C, 0x0004,
	0x4D9E, 0x0010,
	0x4DA0, 0x0810,
	0x4DA2, 0x0810,
	0x4DA4, 0x0000,
	0x4DA6, 0x0000,
	0x4DA8, 0x0010,
	0x4DAA, 0x0010,
	0x7F6E, 0x3100,
	0x7F70, 0xF700,
	0x7F72, 0x2600,
	0x7F74, 0xE100,
	0x8768, 0x0100,
	0xFCFC, 0x4000,
	0x0114, 0x0301,
	0x0306, 0x0096,
	0x0310, 0x007B,
	0x0340, 0x0E98,
	0x0342, 0x0A70,
	0x0344, 0x00F0,
	0x0346, 0x0390,
	0x0348, 0x1F0F,
	0x034A, 0x148F,
	0x034C, 0x0780,
	0x034E, 0x0438,
	0x0350, 0x0004,
	0x0352, 0x0004,
	0x0380, 0x0002,
	0x0382, 0x0006,
	0x0384, 0x0002,
	0x0386, 0x0006,
	0x0900, 0x0144,
	0x0D02, 0x0001,
	0x0D04, 0x0102,
	0x6226, 0x0000,
	//0x0100, 0x0100,
};

static kal_uint16 custom1_setting_array[] = {
	0xFCFC, 0x2400,
	0x0710, 0x0002,
	0x0712, 0x0804,
	0x0714, 0x0100,
	0x0718, 0x0001,
	0x0786, 0x7701,
	0x0874, 0x0100,
	0x09C0, 0x2008,
	0x09C4, 0x2000,
	0x0BE8, 0x3000,
	0x0BEA, 0x3000,
	0x0C60, 0x3000,
	0x0C62, 0x3000,
	0x11B8, 0x0100,
	0x11F6, 0x0020,
	0x1360, 0x0100,
	0x1376, 0x0100,
	0x139C, 0x0000,
	0x139E, 0x0100,
	0x13A0, 0x0A00,
	0x13A2, 0x0120,
	0x13AE, 0x0101,
	0x1444, 0x2000,
	0x1446, 0x2000,
	0x144C, 0x3F00,
	0x144E, 0x3F00,
	0x147C, 0x1000,
	0x1480, 0x1000,
	0x19E6, 0x0200,
	0x19F4, 0x0606,
	0x19F6, 0x0904,
	0x19F8, 0x1010,
	0x19FC, 0x0B00,
	0x19FE, 0x0E1C,
	0x1A02, 0x1800,
	0x1A30, 0x3401,
	0x1A3C, 0x6207,
	0x1A46, 0x8A00,
	0x1A48, 0x6207,
	0x1A52, 0xBF00,
	0x1A64, 0x0301,
	0x1A66, 0xFF00,
	0x1B28, 0xA060,
	0x1B5C, 0x0000,
	0x2022, 0x0500,
	0x2024, 0x0500,
	0x2072, 0x0000,
	0x2080, 0x0101,
	0x2082, 0xFF00,
	0x2084, 0x7F01,
	0x2086, 0x0001,
	0x2088, 0x8001,
	0x208A, 0xD244,
	0x2090, 0x0000,
	0x2092, 0x0000,
	0x2094, 0x0000,
	0x20BA, 0x141C,
	0x20BC, 0x111C,
	0x20BE, 0x54F4,
	0x212E, 0x0200,
	0x21EC, 0x1F04,
	0x3574, 0x1201,
	0x4A78, 0xD8FF,
	0x4A84, 0xD8FF,
	0x4A94, 0x0900,
	0x4A96, 0x0600,
	0x4A98, 0x0300,
	0x4A9A, 0x0600,
	0x4A9C, 0x0600,
	0x4A9E, 0x0600,
	0x4AA0, 0x0600,
	0x4AA2, 0x0600,
	0x4AA4, 0x0300,
	0x4AA6, 0x0600,
	0x4AA8, 0x0900,
	0x4AAA, 0x0600,
	0x4AAC, 0x0600,
	0x4AAE, 0x0600,
	0x4AB0, 0x0600,
	0x4AB2, 0x0600,
	0x4D92, 0x0100,
	0x4D94, 0x0005,
	0x4D96, 0x000A,
	0x4D98, 0x0010,
	0x4D9A, 0x0810,
	0x4D9C, 0x000A,
	0x4D9E, 0x0040,
	0x4DA0, 0x0810,
	0x4DA2, 0x0810,
	0x4DA4, 0x8002,
	0x4DA6, 0xFD03,
	0x4DA8, 0x0010,
	0x4DAA, 0x1510,
	0x7F6E, 0x2F00,
	0x7F70, 0xFA00,
	0x7F72, 0x2400,
	0x7F74, 0xE500,
	0x8768, 0x0100,
	0xFCFC, 0x4000,
	0x0114, 0x0301,
	0x0306, 0x008C,
	0x0310, 0x007B,
	0x0340, 0x0E78,
	0x0342, 0x1384,
	0x0344, 0x0000,
	0x0346, 0x0000,
	0x0348, 0x1FFF,
	0x034A, 0x181F,
	0x034C, 0x0FF0,
	0x034E, 0x0C00,
	0x0350, 0x0008,
	0x0352, 0x0008,
	0x0380, 0x0002,
	0x0382, 0x0002,
	0x0384, 0x0002,
	0x0386, 0x0002,
	0x0900, 0x0122,
	0x0D02, 0x0101,
	0x0D04, 0x0102,
	0x6226, 0x0000,
	//0x0100, 0x0100,
};

static kal_uint16 custom2_setting_array[] = {
	0xFCFC, 0x2400,
	0x0710, 0x0010,
	0x0712, 0x0201,
	0x0714, 0x0800,
	0x0718, 0x0000,
	0x0786, 0x1401,
	0x0874, 0x0106,
	0x09C0, 0x4000,
	0x09C4, 0x4000,
	0x0BE8, 0x5000,
	0x0BEA, 0x5000,
	0x0C60, 0x5000,
	0x0C62, 0x5000,
	0x11B8, 0x0000,
	0x11F6, 0x0010,
	0x1360, 0x0000,
	0x1376, 0x0000,
	0x139C, 0x0100,
	0x139E, 0x0400,
	0x13A0, 0x0500,
	0x13A2, 0x0120,
	0x13AE, 0x0100,
	0x1444, 0x2000,
	0x1446, 0x2000,
	0x144C, 0x3F00,
	0x144E, 0x3F00,
	0x147C, 0x0400,
	0x1480, 0x0400,
	0x19E6, 0x0200,
	0x19F4, 0x0707,
	0x19F6, 0x0404,
	0x19F8, 0x0B0B,
	0x19FC, 0x0700,
	0x19FE, 0x0C1C,
	0x1A02, 0x1800,
	0x1A30, 0x3403,
	0x1A3C, 0x8207,
	0x1A46, 0x8500,
	0x1A48, 0x8207,
	0x1A52, 0x9800,
	0x1A64, 0x0001,
	0x1A66, 0x0000,
	0x1B28, 0xA060,
	0x1B5C, 0x0000,
	0x2022, 0x0500,
	0x2024, 0x0500,
	0x2072, 0x0101,
	0x2080, 0x0100,
	0x2082, 0xFF00,
	0x2084, 0x0002,
	0x2086, 0x0001,
	0x2088, 0x0002,
	0x208A, 0xD244,
	0x2090, 0x101C,
	0x2092, 0x0D1C,
	0x2094, 0x54F4,
	0x20BA, 0x0000,
	0x20BC, 0x0000,
	0x20BE, 0x0000,
	0x212E, 0x0200,
	0x21EC, 0x6902,
	0x3574, 0x7306,
	0x4A78, 0x0000,
	0x4A84, 0x0000,
	0x4A94, 0x0400,
	0x4A96, 0x0400,
	0x4A98, 0x0400,
	0x4A9A, 0x0400,
	0x4A9C, 0x0800,
	0x4A9E, 0x0800,
	0x4AA0, 0x0800,
	0x4AA2, 0x0800,
	0x4AA4, 0x0400,
	0x4AA6, 0x0400,
	0x4AA8, 0x0400,
	0x4AAA, 0x0400,
	0x4AAC, 0x0800,
	0x4AAE, 0x0800,
	0x4AB0, 0x0800,
	0x4AB2, 0x0800,
	0x4D92, 0x0000,
	0x4D94, 0x0000,
	0x4D96, 0x0000,
	0x4D98, 0x0000,
	0x4D9A, 0x0000,
	0x4D9C, 0x0000,
	0x4D9E, 0x0000,
	0x4DA0, 0x0000,
	0x4DA2, 0x0000,
	0x4DA4, 0x0000,
	0x4DA6, 0x0000,
	0x4DA8, 0x0000,
	0x4DAA, 0x0000,
	0x7F6E, 0x2F00,
	0x7F70, 0xFA00,
	0x7F72, 0x2400,
	0x7F74, 0xE500,
	0x8768, 0x0000,
	0xFCFC, 0x4000,
	0x0114, 0x0300,
	0x0306, 0x008C,
	0x0310, 0x007B,
	0x0340, 0x1900,
	0x0342, 0x21F0,
	0x0344, 0x0000,
	0x0346, 0x0000,
	0x0348, 0x1FFF,
	0x034A, 0x181F,
	0x034C, 0x1FE0,
	0x034E, 0x1800,
	0x0350, 0x0010,
	0x0352, 0x0010,
	0x0380, 0x0001,
	0x0382, 0x0001,
	0x0384, 0x0001,
	0x0386, 0x0001,
	0x0900, 0x0111,
	0x0D02, 0x0001,
	0x0D04, 0x0002,
	0x6226, 0x0000,
	//0x0100, 0x0100,
};

#endif

#ifdef FPT_PDAF_SUPPORT
/*VC1 for HDR(DT=0X35), VC2 for PDAF(DT=0X30), unit : 10bit */
static struct SENSOR_VC_INFO_STRUCT SENSOR_VC_INFO[2] = {
    /* Capture mode setting */
    /* Custom1 mode setting */
    {0x02, 0x0A,   0x00,   0x08, 0x40, 0x00,
     0x00, 0x2B, 0x0FF0, 0x0C00, 0x01, 0x00, 0x0000, 0x0000,
     0x01, 0x30, 0x027C, 0x0BF0, 0x03, 0x00, 0x0000, 0x0000},
    /* Video mode setting */
    {0x02, 0x0A,   0x00,   0x08, 0x40, 0x00,
     0x00, 0x2B, 0x0FF0, 0x08F8, 0x01, 0x00, 0x0000, 0x0000,
     0x01, 0x30, 0x027C, 0x08E8, 0x03, 0x00, 0x0000, 0x0000}
};
static struct SET_PD_BLOCK_INFO_T imgsensor_pd_info = {
    .i4OffsetX =  8,
    .i4OffsetY =  8,
    .i4PitchX  =  8,
    .i4PitchY  =  8,
    .i4PairNum =  4,
    .i4SubBlkW =  8,
    .i4SubBlkH =  2,
    .i4PosL = {{11, 8},{9, 11},{13, 12},{15, 15}},
    .i4PosR = {{10, 8},{8, 11},{12, 12},{14, 15}},
    .i4BlockNumX = 508,
    .i4BlockNumY = 382,
    .iMirrorFlip = 0,
    .i4Crop = { {0,0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}},
};
static struct SET_PD_BLOCK_INFO_T imgsensor_pd_info_16_9 = {
    .i4OffsetX = 8,
    .i4OffsetY = 8,
    .i4PitchX  = 8,
    .i4PitchY  = 8,
    .i4PairNum = 4,
    .i4SubBlkW = 8,
    .i4SubBlkH = 2,
    .i4PosL = {{9, 8},{11, 11},{15, 12},{13, 15}},
    .i4PosR = {{8, 8},{10, 11},{14, 12},{12, 15}},
    .i4BlockNumX = 508,
    .i4BlockNumY = 285,
    .iMirrorFlip = 0,
    .i4Crop = { {0, 388}, {0, 388}, {0, 388}, {0, 388}, {0, 388}, {0, 388}, {0, 388}, {0, 388}, {0, 388}, {0, 388}},
};
#endif
static kal_uint16 read_cmos_sensor_byte(kal_uint16 addr) {
	kal_uint16 get_byte = 0;
	char pu_send_cmd[2] = {(char)(addr >> 8), (char)(addr & 0xFF)};

	// kdSetI2CSpeed(imgsensor_info.i2c_speed); // Add this func to set i2c speed by each sensor
	iReadRegI2C(pu_send_cmd, 2, (u8*)&get_byte, 1, imgsensor.i2c_write_id);
	return get_byte;
}

static void write_cmos_sensor_byte(kal_uint32 addr, kal_uint32 para) {
	char pu_send_cmd[3] = {(char)(addr >> 8), (char)(addr & 0xFF), (char)(para & 0xFF)};

	// kdSetI2CSpeed(imgsensor_info.i2c_speed); // Add this func to set i2c speed by each sensor
	iWriteRegI2C(pu_send_cmd, 3, imgsensor.i2c_write_id);
}

#define EEPROM_READY 1
#if EEPROM_READY //stan
#define FOUR_CELL_SIZE 9490
#define FOUR_CELL_xtc_ADDR 0x0D7A
static char Four_Cell_Array[FOUR_CELL_SIZE + 2] = {0};
static void copy_4cell_from_eeprom(char *data)
{
    static int data_numbers = 0;

	LOG_INF("copy_4cell_from_eeprom ++\n");

	Four_Cell_Array[0] = (FOUR_CELL_SIZE & 0xff);/*Low*/
	Four_Cell_Array[1] = ((FOUR_CELL_SIZE >> 8) & 0xff);/*High*/

	if (data != NULL) {
		printk("return data\n");
		memcpy(data, (kal_uint8*)&Four_Cell_Array[data_numbers * 3164], 3164);
        data_numbers++;
	}
    if (data_numbers == 3)
    {
        data_numbers = 0;
    }

	LOG_INF("copy_4cell_from_eeprom %d--\n",data_numbers);
}
#endif

#define SENSOR_XTC_ADDR 0x1B2A
#define PD_XTC_ADDR 0x1E2C
#define SW_GGC_ADDR 0x2DCE
#define XTC_SIZE 3502
#define SENSOR_XTC_SIZE 768
#define PD_XTC_SIZE 4000
#define SW_GGC_SIZE 626
static uint8_t S5KJN1_eeprom[9490] = {0};
static void S5KJN1_read_data_from_eeprom(kal_uint8 slave, kal_uint32 start_add, uint32_t size)
{
    int i = 0;
    int backupWriteId;
    UINT32 add_start;
    spin_lock(&imgsensor_drv_lock);
    backupWriteId = imgsensor.i2c_write_id;
    imgsensor.i2c_write_id = slave;
    spin_unlock(&imgsensor_drv_lock);
    pr_err("[cameradebug-eeprom]HI5022Q_enter_read_data_from_eeprom,backupWriteId=0x%x,slave_writeid=0x%x\n",backupWriteId,imgsensor.i2c_write_id);
    //read eeprom data
    memset((kal_uint8*)&Four_Cell_Array[0], 0 , 9490);
    for (i = 0; i < XTC_SIZE; i++)
    {
        S5KJN1_eeprom[i] = read_cmos_sensor_byte(start_add);
        start_add ++;
    }
    add_start = SENSOR_XTC_ADDR;
    for (i = XTC_SIZE+594; i < XTC_SIZE+594+SENSOR_XTC_SIZE; i++)
    {
        S5KJN1_eeprom[i] = read_cmos_sensor_byte(add_start);
        add_start ++;
    }
    add_start = PD_XTC_ADDR;
    for (i = XTC_SIZE+594+SENSOR_XTC_SIZE; i < XTC_SIZE+594+SENSOR_XTC_SIZE+PD_XTC_SIZE; i++)
    {
        S5KJN1_eeprom[i] = read_cmos_sensor_byte(add_start);
        add_start ++;
    }
    add_start = SW_GGC_ADDR;
    for (i = XTC_SIZE+594+SENSOR_XTC_SIZE+PD_XTC_SIZE; i < FOUR_CELL_SIZE; i++)
    {
        S5KJN1_eeprom[i] = read_cmos_sensor_byte(add_start);
        add_start ++;
    }

    spin_lock(&imgsensor_drv_lock);
    imgsensor.i2c_write_id = backupWriteId;
    spin_unlock(&imgsensor_drv_lock);

    memcpy((kal_uint8*)&Four_Cell_Array[2], (kal_uint8*)&S5KJN1_eeprom[0], 9490);
}


static void write_cmos_sensor(kal_uint16 addr, kal_uint16 para) {
	char pusendcmd[4] = {(char)(addr >> 8), (char)(addr & 0xFF), (char)(para >> 8), (char)(para & 0xFF)};

	// kdSetI2CSpeed(imgsensor_info.i2c_speed); // Add this func to set i2c speed by each sensor
	iWriteRegI2C(pusendcmd, 4, imgsensor.i2c_write_id);
}

#if MULTI_WRITE
static kal_uint16 table_write_cmos_sensor(kal_uint16* para, kal_uint32 len) {
	char puSendCmd[I2C_BUFFER_LEN];
	kal_uint32 tosend, IDX;
	kal_uint16 addr = 0, addr_last = 0, data = 0;

	tosend = 0;
	IDX = 0;
	while (len > IDX) {
		addr = para[IDX];

		{
			puSendCmd[tosend++] = (char)(addr >> 8);
			puSendCmd[tosend++] = (char)(addr & 0xFF);
			data = para[IDX + 1];
			puSendCmd[tosend++] = (char)(data >> 8);
			puSendCmd[tosend++] = (char)(data & 0xFF);
			IDX += 2;
			addr_last = addr;
			// LOG_INF("i2c_write_id start[0x%x] Addr[0x%x] Data[0x%x] addr_last[0x%x]\n", imgsensor.i2c_write_id, addr, data, addr_last);
		}

		/* Write when remain buffer size is less than 4 bytes or reach end of data */
		if ((I2C_BUFFER_LEN - tosend) < 4 || IDX == len || addr != addr_last) {
			LOG_INF("i2c_write_id end[0x%x] Addr[0x%x] Data[0x%x] addr_last[0x%x], tosend[%d], IDX[%d], len[%d]\n", imgsensor.i2c_write_id, addr, data, addr_last,
					tosend, IDX, len);

			while (iBurstWriteReg_multi(puSendCmd, tosend,
										imgsensor.i2c_write_id, 4, imgsensor_info.i2c_speed) != 0) {
				LOG_INF("iBurstWriteReg_multi FAIL!, retry!");
			}
			tosend = 0;
		}
	}
	return 0;
}
#endif

static void set_dummy(void) {
	LOG_INF("dummyline = %d, dummypixels = %d\n", imgsensor.dummy_line, imgsensor.dummy_pixel);
	/* you can set dummy by imgsensor.dummy_line and imgsensor.dummy_pixel, or you can set dummy by imgsensor.frame_length and imgsensor.line_length */
	write_cmos_sensor(0x0340, imgsensor.frame_length & 0xFFFF);
	write_cmos_sensor(0x0342, imgsensor.line_length & 0xFFFF);
} /* set_dummy */

static void set_max_framerate(UINT16 framerate, kal_bool min_framelength_en) {
	kal_uint32 frame_length = imgsensor.frame_length;

	LOG_INF("framerate = %d, min framelength should enable(%d)\n", framerate, min_framelength_en);

	frame_length = imgsensor.pclk / framerate * 10 / imgsensor.line_length;
	spin_lock(&imgsensor_drv_lock);
	imgsensor.frame_length = (frame_length > imgsensor.min_frame_length) ? frame_length : imgsensor.min_frame_length;
	imgsensor.dummy_line = imgsensor.frame_length - imgsensor.min_frame_length;
	// dummy_line = frame_length - imgsensor.min_frame_length;
	// if (dummy_line < 0)
	// imgsensor.dummy_line = 0;
	// else
	// imgsensor.dummy_line = dummy_line;
	// imgsensor.frame_length = frame_length + imgsensor.dummy_line;
	if (imgsensor.frame_length > imgsensor_info.max_frame_length) {
		imgsensor.frame_length = imgsensor_info.max_frame_length;
		imgsensor.dummy_line = imgsensor.frame_length - imgsensor.min_frame_length;
	}
	if (min_framelength_en)
		imgsensor.min_frame_length = imgsensor.frame_length;
	spin_unlock(&imgsensor_drv_lock);
	set_dummy();
} /* set_max_framerate */

int bNeedSetNormalMode = 0;
// static void check_stream_is_on(void);
static void write_shutter(kal_uint32 shutter) {
	unsigned long flags;
	kal_uint16 realtime_fps = 0;
	kal_uint32 CintR = 0;
	kal_uint32 Time_Farme = 0;
	// kal_uint32 frame_length = 0;
	spin_lock_irqsave(&imgsensor_drv_lock, flags);
	imgsensor.shutter = shutter;
	spin_unlock_irqrestore(&imgsensor_drv_lock, flags);

	// write_shutter(shutter);
	/* 0x3500, 0x3501, 0x3502 will increase VBLANK to get exposure larger than frame exposure */
	/* AE doesn't update sensor gain at capture mode, thus extra exposure lines must be updated here. */
	// if shutter bigger than frame_length, should extend frame length first
	spin_lock(&imgsensor_drv_lock);
	if (shutter > imgsensor.min_frame_length - imgsensor_info.margin)
		imgsensor.frame_length = shutter + imgsensor_info.margin;
	else
		imgsensor.frame_length = imgsensor.min_frame_length;
	if (imgsensor.frame_length > imgsensor_info.max_frame_length)
		imgsensor.frame_length = imgsensor_info.max_frame_length;
	spin_unlock(&imgsensor_drv_lock);
	shutter = (shutter < imgsensor_info.min_shutter) ? imgsensor_info.min_shutter : shutter;
	if (imgsensor.autoflicker_en) {
		realtime_fps = imgsensor.pclk / imgsensor.line_length * 10 / imgsensor.frame_length;
		if (realtime_fps >= 297 && realtime_fps <= 305)
			set_max_framerate(296, 0);
		else if (realtime_fps >= 147 && realtime_fps <= 150)
			set_max_framerate(146, 0);
		else {
			// Extend frame length
			write_cmos_sensor(0x0340, imgsensor.frame_length & 0xFFFF);
		}
	} else {
		// Extend frame length
		write_cmos_sensor(0x0340, imgsensor.frame_length & 0xFFFF);
	}

	/* long expsoure */
	if (shutter >= 0x0000FFFF) {
		LOG_INF("Enter long exposure mode");
		bNeedSetNormalMode = 1;
		CintR = shutter / 128;
		Time_Farme = CintR + 0x0002;  // 1st framelength

		write_cmos_sensor(0x0340, Time_Farme & 0xFFFF);  // Framelength
		write_cmos_sensor(0x0202, CintR & 0xFFFF);  //shutter
		write_cmos_sensor(0x0702, 0x0700);
		write_cmos_sensor(0x0704, 0x0700);
		/* Frame exposure mode customization for LE*/
	} else {
		if (bNeedSetNormalMode == 1) {
			LOG_INF("Exit long exposure mode");
			write_cmos_sensor(0x0702, 0x0000);
			write_cmos_sensor(0x0704, 0x0000);
			bNeedSetNormalMode = 0;
		}
        write_cmos_sensor(0x0340, imgsensor.frame_length + 16);
        write_cmos_sensor(0x0202, shutter & 0xFFFF);
	}
	LOG_INF("Exit! shutter =%d, framelength =%d\n", shutter, imgsensor.frame_length);
}

/*************************************************************************
 * FUNCTION
 *	set_shutter
 *
 * DESCRIPTION
 *	This function set e-shutter of sensor to change exposure time.
 *
 * PARAMETERS
 *	iShutter : exposured lines
 *
 * RETURNS
 *	None
 *
 * GLOBALS AFFECTED
 *
 *************************************************************************/
static void set_shutter(kal_uint32 shutter) {
	unsigned long flags;

	spin_lock_irqsave(&imgsensor_drv_lock, flags);
	imgsensor.shutter = shutter;
	spin_unlock_irqrestore(&imgsensor_drv_lock, flags);

	write_shutter(shutter);
} /* set_shutter */

/*************************************************************************
 * FUNCTION
 *	set_shutter_frame_length
 *
 * DESCRIPTION
 *	for frame & 3A sync
 *
 *************************************************************************/
static void set_shutter_frame_length(kal_uint16 shutter, kal_uint16 frame_length) {
	unsigned long flags;
	kal_uint16 realtime_fps = 0;
	kal_int32 dummy_line = 0;

	spin_lock_irqsave(&imgsensor_drv_lock, flags);
	imgsensor.shutter = shutter;
	spin_unlock_irqrestore(&imgsensor_drv_lock, flags);

	spin_lock(&imgsensor_drv_lock);
	/*Change frame time*/
	if (frame_length > 1)
		dummy_line = frame_length - imgsensor.frame_length;
	imgsensor.frame_length = imgsensor.frame_length + dummy_line;
	//
	if (shutter > imgsensor.frame_length - imgsensor_info.margin)
		imgsensor.frame_length = shutter + imgsensor_info.margin;
	if (imgsensor.frame_length > imgsensor_info.max_frame_length)
		imgsensor.frame_length = imgsensor_info.max_frame_length;
	spin_unlock(&imgsensor_drv_lock);
	shutter = (shutter < imgsensor_info.min_shutter) ? imgsensor_info.min_shutter : shutter;
	shutter = (shutter > (imgsensor_info.max_frame_length - imgsensor_info.margin))
				  ? (imgsensor_info.max_frame_length - imgsensor_info.margin)
				  : shutter;

	if (imgsensor.autoflicker_en) {
		realtime_fps = imgsensor.pclk / imgsensor.line_length * 10 / imgsensor.frame_length;
		if (realtime_fps >= 297 && realtime_fps <= 305)
			set_max_framerate(296, 0);
		else if (realtime_fps >= 147 && realtime_fps <= 150)
			set_max_framerate(146, 0);
		else {
			write_cmos_sensor(0x0340, imgsensor.frame_length & 0xFFFF);
		}
	} else {
		// Extend frame length
		write_cmos_sensor(0x0340, imgsensor.frame_length & 0xFFFF);
	}

	// Update Shutter
	 write_cmos_sensor(0X0202, shutter & 0xFFFF);
	LOG_INF("Exit! shutter =%d, frame_length = %d, dummy_line = %d, imgsensor.frame_length =%d\n",
		shutter, frame_length, dummy_line, imgsensor.frame_length);
}

static kal_uint16 gain2reg(const kal_uint16 gain) {
	kal_uint16 reg_gain = 0x0000;
	// gain = 64 = 1x real gain.
	reg_gain = gain / 2;
	// reg_gain = reg_gain & 0xFFFF;
	return (kal_uint16)reg_gain;
}

/*************************************************************************
* FUNCTION
*	set_gain
*
* DESCRIPTION
*	This function is to set global gain to sensor.
*
* PARAMETERS
*	iGain : sensor global gain(base: 0x40)
*
* RETURNS
*	the actually gain set to sensor.
*
* GLOBALS AFFECTED
*
*************************************************************************/
static kal_uint16 set_gain(kal_uint16 gain) {
	// gain = 64 = 1x real gain.
	kal_uint16 reg_gain;

	if (gain < BASEGAIN || gain > 64 * BASEGAIN) {
		LOG_INF("Error gain setting");
		if (gain < BASEGAIN)
			gain = BASEGAIN;
		else if (gain > 64 * BASEGAIN)
			gain = 64 * BASEGAIN;
	}

	reg_gain = gain2reg(gain);
	spin_lock(&imgsensor_drv_lock);
	imgsensor.gain = reg_gain;
	spin_unlock(&imgsensor_drv_lock);
	LOG_INF("gain = %d , reg_gain = 0x%x\n ", gain, reg_gain);

	write_cmos_sensor(0x0204, (reg_gain & 0xFFFF));
	return gain;
} /* set_gain */

// ihdr_write_shutter_gain not support for s5k3L6
static void ihdr_write_shutter_gain(kal_uint16 le, kal_uint16 se, kal_uint16 gain) {
	LOG_INF("le:0x%x, se:0x%x, gain:0x%x\n", le, se, gain);
	if (imgsensor.ihdr_en) {
		spin_lock(&imgsensor_drv_lock);
		if (le > imgsensor.min_frame_length - imgsensor_info.margin)
			imgsensor.frame_length = le + imgsensor_info.margin;
		else
			imgsensor.frame_length = imgsensor.min_frame_length;
		if (imgsensor.frame_length > imgsensor_info.max_frame_length)
			imgsensor.frame_length = imgsensor_info.max_frame_length;
		spin_unlock(&imgsensor_drv_lock);
		if (le < imgsensor_info.min_shutter)
			le = imgsensor_info.min_shutter;
		if (se < imgsensor_info.min_shutter)
			se = imgsensor_info.min_shutter;

		// Extend frame length first
		set_gain(gain);
	}
}

static void set_mirror_flip(kal_uint8 image_mirror) {
	LOG_INF("image_mirror = %d\n", image_mirror);

	/********************************************************
	 *
	 *   0x3820[2] ISP Vertical flip
	 *   0x3820[1] Sensor Vertical flip
	 *
	 *   0x3821[2] ISP Horizontal mirror
	 *   0x3821[1] Sensor Horizontal mirror
	 *
	 *   ISP and Sensor flip or mirror register bit should be the same!!
	 *
	 ********************************************************/
	spin_lock(&imgsensor_drv_lock);
	// image_mirror &= 3;
	imgsensor.mirror = image_mirror;
	spin_unlock(&imgsensor_drv_lock);
	switch (image_mirror) {
		case IMAGE_NORMAL:
			// write_cmos_sensor_byte(0x0101, 0X03); //GR
			write_cmos_sensor_byte(0x0101, 0x00);  // GR
			break;
		case IMAGE_H_MIRROR:
			// write_cmos_sensor_byte(0x0101, 0X02); //R
			write_cmos_sensor_byte(0x0101, 0x01);  // R
			break;
		case IMAGE_V_MIRROR:
			// write_cmos_sensor_byte(0x0101, 0X01); //B
			write_cmos_sensor_byte(0x0101, 0x02);  // B
			break;
		case IMAGE_HV_MIRROR:
			// write_cmos_sensor_byte(0x0101, 0X00); //GB
			write_cmos_sensor_byte(0x0101, 0x03);  // GB
			break;
		default:
			LOG_INF("Error image_mirror setting\n");
	}
}

/*************************************************************************
* FUNCTION
*	night_mode
*
* DESCRIPTION
*	This function night mode of sensor.
*
* PARAMETERS
*	bEnable: KAL_TRUE -> enable night mode, otherwise, disable night mode
*
* RETURNS
*	None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static void night_mode(kal_bool enable) {
	/*No Need to implement this function*/
} /* night_mode	*/

static kal_uint32 streaming_control(kal_bool enable) {
	// write_cmos_sensor_byte

	int i = 0;
	int framecnt = 0;
	int isStreamOn = 0;

	LOG_INF("streaming_enable(0=Sw Standby,1=streaming): %d\n", enable);
	if (enable) {
		// while (1)
		for (i = 0; i < 1000; i++) {
			write_cmos_sensor_byte(0x0100, 0X01);
			isStreamOn = read_cmos_sensor_byte(0x0100); /* waiting for sensor to  stop output  then  set the  setting */
			LOG_INF("isStreamOn %d ", isStreamOn);

			if ((isStreamOn & 0x1) == 0x01) {
				return ERROR_NONE;
			} else {
				mdelay(1);
			}
		}
	} else {
		// while(1) {
		for (i = 0; i < 1000; i++) {
			write_cmos_sensor_byte(0x0100, 0x00);
			framecnt = read_cmos_sensor_byte(0x0005);
			if ((framecnt & 0xff) == 0xFF) {
				LOG_INF("StreamOff OK at framecnt=%d.\n", framecnt);
				break;
			} else {
				LOG_INF("StreamOFF is not on, %d, i=%d", framecnt, i);
				mdelay(1);
			}
		}
	}
	return ERROR_NONE;
}

static void sensor_init(void) {
	LOG_INF("[%s] +", __func__);
	LOG_INF("[%s] start", __func__);

	write_cmos_sensor(0x6028, 0x4000);
	write_cmos_sensor(0x0000, 0x0001);
	write_cmos_sensor(0x0000, 0x38E1);
	write_cmos_sensor(0x001E, 0x0005);
	write_cmos_sensor(0x6028, 0x4000);
	write_cmos_sensor(0x6010, 0x0001);
	mdelay(5);
	write_cmos_sensor(0x6226, 0x0001);
	mdelay(10);

	table_write_cmos_sensor(sensor_init_setting_array, sizeof(sensor_init_setting_array) / sizeof(kal_uint16));
	LOG_INF("[%s] -", __func__);
	LOG_INF("[%s] end", __func__);
}

static void preview_setting(void) {
	LOG_INF("[%s] +", __func__);
	LOG_INF("[%s] start", __func__);
	table_write_cmos_sensor(preview_setting_array, sizeof(preview_setting_array) / sizeof(kal_uint16));
	LOG_INF("[%s] -", __func__);
	LOG_INF("[%s] end", __func__);

} /* preview_setting */

static void capture_setting(kal_uint16 currefps) {
	LOG_INF("[%s] +", __func__);
	LOG_INF("[%s] start", __func__);
	table_write_cmos_sensor(capture_setting_array, sizeof(capture_setting_array) / sizeof(kal_uint16));
	LOG_INF("[%s] -", __func__);
	LOG_INF("[%s] end", __func__);
}

static void normal_video_setting(kal_uint16 currefps) {
	LOG_INF("[%s] +", __func__);
	LOG_INF("[%s] start", __func__);
	table_write_cmos_sensor(normal_video_setting_array, sizeof(normal_video_setting_array) / sizeof(kal_uint16));
	LOG_INF("[%s] -", __func__);
	LOG_INF("[%s] end", __func__);
}

static void hs_video_setting(void) {
	LOG_INF("[%s] start", __func__);
	LOG_INF("[%s] +", __func__);
	table_write_cmos_sensor(hs_video_setting_array, sizeof(hs_video_setting_array) / sizeof(kal_uint16));
	LOG_INF("[%s] -", __func__);
	LOG_INF("[%s] end", __func__);
}

static void slim_video_setting(void) {
	LOG_INF("[%s] start", __func__);
	LOG_INF("[%s] +", __func__);
	table_write_cmos_sensor(slim_video_setting_array, sizeof(slim_video_setting_array) / sizeof(kal_uint16));
	LOG_INF("[%s] -", __func__);
	LOG_INF("[%s] end", __func__);
}

static void custom1_setting(kal_uint16 currefps) {
	LOG_INF("[%s] +", __func__);
	LOG_INF("[%s] start", __func__);
	table_write_cmos_sensor(custom1_setting_array, sizeof(custom1_setting_array) / sizeof(kal_uint16));
	LOG_INF("[%s] -", __func__);
	LOG_INF("[%s] end", __func__);
}

static void custom2_setting(kal_uint16 currefps) {
	LOG_INF("[%s] +", __func__);
	LOG_INF("[%s] start", __func__);
	table_write_cmos_sensor(custom2_setting_array, sizeof(custom2_setting_array) / sizeof(kal_uint16));
	LOG_INF("[%s] -", __func__);
	LOG_INF("[%s] end", __func__);
}

static kal_uint32 return_sensor_id(void) {
	return ((read_cmos_sensor_byte(0x0000) << 8) | read_cmos_sensor_byte(0x0001));
}

/*************************************************************************
* FUNCTION
*	get_imgsensor_id
*
* DESCRIPTION
*	This function get the sensor ID
*
* PARAMETERS
*	*sensorID : return the sensor ID
*
* RETURNS
*	None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static kal_uint32 get_imgsensor_id(UINT32* sensor_id) {
	kal_uint8 i = 0;
	kal_uint8 retry = 2;
	// sensor have two i2c address 0x5b 0x5a & 0x21 0x20, we should detect the module used i2c address

	LOG_INF("[%s] +", __func__);

	while (imgsensor_info.i2c_addr_table[i] != 0xff) {
		spin_lock(&imgsensor_drv_lock);
		imgsensor.i2c_write_id = imgsensor_info.i2c_addr_table[i];
		spin_unlock(&imgsensor_drv_lock);
		do {
			*sensor_id = return_sensor_id();
			if (*sensor_id == imgsensor_info.sensor_id) {
				LOG_INF("i2c write id: 0x%x, ReadOut sensor id: 0x%x, imgsensor_info.sensor_id:0x%x.\n",
					imgsensor.i2c_write_id, *sensor_id, imgsensor_info.sensor_id);
                S5KJN1_read_data_from_eeprom(S5KJN1_EEPROM_SLAVE_ADDR,0x0D7A, 9490);
				return ERROR_NONE;
			}
			LOG_INF("Read sensor id fail, i2c write id: 0x%x, ReadOut sensor id: 0x%x, imgsensor_info.sensor_id:0x%x.\n",
				imgsensor.i2c_write_id, *sensor_id, imgsensor_info.sensor_id);
			retry--;
		} while (retry > 0);
		i++;
		retry = 1;
	}

	if (*sensor_id != imgsensor_info.sensor_id) {
		*sensor_id = 0xFFFFFFFF;
		LOG_INF("[%s] -error-", __func__);
		return ERROR_SENSOR_CONNECT_FAIL;
	}
	LOG_INF("[%s] -", __func__);
	return ERROR_NONE;
}

/*************************************************************************
* FUNCTION
*	open
*
* DESCRIPTION
*	This function initialize the registers of CMOS sensor
*
* PARAMETERS
*	None
*
* RETURNS
*	None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static kal_uint32 open(void) {
	kal_uint8 i = 0;
	kal_uint8 retry = 2;
	kal_uint32 sensor_id = 0;
	LOG_INF("[%s] +", __func__);
	LOG_1;
	// sensor have two i2c address 0x6c 0x6d & 0x21 0x20, we should detect the module used i2c address
	while (imgsensor_info.i2c_addr_table[i] != 0xff) {
		spin_lock(&imgsensor_drv_lock);
		imgsensor.i2c_write_id = imgsensor_info.i2c_addr_table[i];
		spin_unlock(&imgsensor_drv_lock);
		do {
			sensor_id = return_sensor_id();
			if (sensor_id == imgsensor_info.sensor_id) {
				LOG_INF("i2c write id: 0x%x, sensor id: 0x%x\n", imgsensor.i2c_write_id, sensor_id);
				break;
			}
			LOG_INF("Read sensor id fail, id: 0x%x, sensor id: 0x%x\n", imgsensor.i2c_write_id, sensor_id);
			retry--;
		} while (retry > 0);
		i++;
		if (sensor_id == imgsensor_info.sensor_id)
			break;
		retry = 2;
	}
	if (imgsensor_info.sensor_id != sensor_id) {
		return ERROR_SENSOR_CONNECT_FAIL;
	}
	/* initail sequence write in  */
	LOG_INF("[jw] imgsensor init start");
	sensor_init();
	LOG_INF("[jw] imgsensor init fin");
	spin_lock(&imgsensor_drv_lock);

	imgsensor.autoflicker_en = KAL_FALSE;
	imgsensor.sensor_mode = IMGSENSOR_MODE_INIT;
	imgsensor.pclk = imgsensor_info.pre.pclk;
	imgsensor.frame_length = imgsensor_info.pre.framelength;
	imgsensor.line_length = imgsensor_info.pre.linelength;
	imgsensor.min_frame_length = imgsensor_info.pre.framelength;
	imgsensor.dummy_pixel = 0;
	imgsensor.dummy_line = 0;
	imgsensor.ihdr_en = KAL_FALSE;
	imgsensor.test_pattern = KAL_FALSE;
	imgsensor.current_fps = imgsensor_info.pre.max_framerate;
	spin_unlock(&imgsensor_drv_lock);

	LOG_INF("[%s] -", __func__);
	return ERROR_NONE;
} /* open */

/*************************************************************************
* FUNCTION
*	close
*
* DESCRIPTION
*
*
* PARAMETERS
*	None
*
* RETURNS
*	None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static kal_uint32 close(void) {
	LOG_INF("[%s] +", __func__);

	/*No Need to implement this function*/

	LOG_INF("[%s] -", __func__);
	return ERROR_NONE;
} /* close */

/*************************************************************************
* FUNCTION
* preview
*
* DESCRIPTION
*	This function start the sensor preview.
*
* PARAMETERS
*	*image_window : address pointer of pixel numbers in one period of HSYNC
*  *sensor_config_data : address pointer of line numbers in one period of VSYNC
*
* RETURNS
*	None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static kal_uint32 preview(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT* image_window,
						  MSDK_SENSOR_CONFIG_STRUCT* sensor_config_data) {
	LOG_INF("E\n");

	spin_lock(&imgsensor_drv_lock);
	imgsensor.sensor_mode = IMGSENSOR_MODE_PREVIEW;
	imgsensor.pclk = imgsensor_info.pre.pclk;
	// imgsensor.video_mode = KAL_FALSE;
	imgsensor.line_length = imgsensor_info.pre.linelength;
	imgsensor.frame_length = imgsensor_info.pre.framelength;
	imgsensor.min_frame_length = imgsensor_info.pre.framelength;
	imgsensor.autoflicker_en = KAL_FALSE;
	spin_unlock(&imgsensor_drv_lock);
	preview_setting();
	set_mirror_flip(IMAGE_NORMAL);
	mdelay(2);

	return ERROR_NONE;
} /* preview */

/*************************************************************************
* FUNCTION
*	capture
*
* DESCRIPTION
*	This function setup the CMOS sensor in capture MY_OUTPUT mode
*
* PARAMETERS
*
* RETURNS
*	None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static kal_uint32 capture(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT* image_window,
						  MSDK_SENSOR_CONFIG_STRUCT* sensor_config_data) {
	LOG_INF("E\n");
	spin_lock(&imgsensor_drv_lock);
	imgsensor.sensor_mode = IMGSENSOR_MODE_CAPTURE;
	imgsensor.pclk = imgsensor_info.cap.pclk;
	imgsensor.line_length = imgsensor_info.cap.linelength;
	imgsensor.frame_length = imgsensor_info.cap.framelength;
	imgsensor.min_frame_length = imgsensor_info.cap.framelength;
	imgsensor.autoflicker_en = KAL_FALSE;

	spin_unlock(&imgsensor_drv_lock);
	capture_setting(imgsensor.current_fps);
	set_mirror_flip(IMAGE_NORMAL);
	mdelay(2);

	return ERROR_NONE;
} /* capture() */

static kal_uint32 normal_video(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT* image_window,
							   MSDK_SENSOR_CONFIG_STRUCT* sensor_config_data) {
	LOG_INF("E\n");

	spin_lock(&imgsensor_drv_lock);
	imgsensor.sensor_mode = IMGSENSOR_MODE_VIDEO;
	imgsensor.pclk = imgsensor_info.normal_video.pclk;
	imgsensor.line_length = imgsensor_info.normal_video.linelength;
	imgsensor.frame_length = imgsensor_info.normal_video.framelength;
	imgsensor.min_frame_length = imgsensor_info.normal_video.framelength;
	// imgsensor.current_fps = 300;
	imgsensor.autoflicker_en = KAL_FALSE;
	spin_unlock(&imgsensor_drv_lock);
	normal_video_setting(imgsensor.current_fps);
	set_mirror_flip(IMAGE_NORMAL);

	return ERROR_NONE;
} /* normal_video */

static kal_uint32 hs_video(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT* image_window,
						   MSDK_SENSOR_CONFIG_STRUCT* sensor_config_data) {
	LOG_INF("E\n");

	spin_lock(&imgsensor_drv_lock);
	imgsensor.sensor_mode = IMGSENSOR_MODE_HIGH_SPEED_VIDEO;
	imgsensor.pclk = imgsensor_info.hs_video.pclk;
	// imgsensor.video_mode = KAL_TRUE;
	imgsensor.line_length = imgsensor_info.hs_video.linelength;
	imgsensor.frame_length = imgsensor_info.hs_video.framelength;
	imgsensor.min_frame_length = imgsensor_info.hs_video.framelength;
	imgsensor.dummy_line = 0;
	imgsensor.dummy_pixel = 0;
	// imgsensor.current_fps = 300;
	imgsensor.autoflicker_en = KAL_FALSE;
	spin_unlock(&imgsensor_drv_lock);
	hs_video_setting();
	set_mirror_flip(IMAGE_NORMAL);

	return ERROR_NONE;
} /* hs_video */

static kal_uint32 slim_video(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT* image_window,
							 MSDK_SENSOR_CONFIG_STRUCT* sensor_config_data) {
	LOG_INF("E\n");

	spin_lock(&imgsensor_drv_lock);
	imgsensor.sensor_mode = IMGSENSOR_MODE_SLIM_VIDEO;
	imgsensor.pclk = imgsensor_info.slim_video.pclk;
	// imgsensor.video_mode = KAL_TRUE;
	imgsensor.line_length = imgsensor_info.slim_video.linelength;
	imgsensor.frame_length = imgsensor_info.slim_video.framelength;
	imgsensor.min_frame_length = imgsensor_info.slim_video.framelength;
	imgsensor.dummy_line = 0;
	imgsensor.dummy_pixel = 0;
	// imgsensor.current_fps = 300;
	imgsensor.autoflicker_en = KAL_FALSE;
	spin_unlock(&imgsensor_drv_lock);
	slim_video_setting();
	set_mirror_flip(IMAGE_NORMAL);
	return ERROR_NONE;
}

static kal_uint32 custom1(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT* image_window,
						  MSDK_SENSOR_CONFIG_STRUCT* sensor_config_data) {
	LOG_INF("E\n");
	spin_lock(&imgsensor_drv_lock);
	imgsensor.sensor_mode = IMGSENSOR_MODE_CUSTOM1;
	imgsensor.pclk = imgsensor_info.custom1.pclk;
	imgsensor.line_length = imgsensor_info.custom1.linelength;
	imgsensor.frame_length = imgsensor_info.custom1.framelength;
	imgsensor.min_frame_length = imgsensor_info.custom1.framelength;
	imgsensor.autoflicker_en = KAL_FALSE;

	spin_unlock(&imgsensor_drv_lock);
	custom1_setting(imgsensor.current_fps);
	set_mirror_flip(IMAGE_NORMAL);
	mdelay(2);

	return ERROR_NONE;
} /* custom1 24fps */

static kal_uint32 custom2(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT* image_window,
						  MSDK_SENSOR_CONFIG_STRUCT* sensor_config_data) {
	LOG_INF("E\n");
	spin_lock(&imgsensor_drv_lock);
	imgsensor.sensor_mode = IMGSENSOR_MODE_CUSTOM2;
	imgsensor.pclk = imgsensor_info.custom2.pclk;
	imgsensor.line_length = imgsensor_info.custom2.linelength;
	imgsensor.frame_length = imgsensor_info.custom2.framelength;
	imgsensor.min_frame_length = imgsensor_info.custom2.framelength;
	imgsensor.autoflicker_en = KAL_FALSE;

	spin_unlock(&imgsensor_drv_lock);
	custom2_setting(imgsensor.current_fps);
	set_mirror_flip(IMAGE_NORMAL);
	mdelay(2);

	return ERROR_NONE;
} /* custom2 remosaic */

static kal_uint32 get_resolution(
	MSDK_SENSOR_RESOLUTION_INFO_STRUCT* sensor_resolution) {
	LOG_INF("E\n");
	sensor_resolution->SensorFullWidth = imgsensor_info.cap.grabwindow_width;
	sensor_resolution->SensorFullHeight = imgsensor_info.cap.grabwindow_height;

	sensor_resolution->SensorPreviewWidth = imgsensor_info.pre.grabwindow_width;
	sensor_resolution->SensorPreviewHeight = imgsensor_info.pre.grabwindow_height;

	sensor_resolution->SensorVideoWidth = imgsensor_info.normal_video.grabwindow_width;
	sensor_resolution->SensorVideoHeight = imgsensor_info.normal_video.grabwindow_height;

	sensor_resolution->SensorHighSpeedVideoWidth = imgsensor_info.hs_video.grabwindow_width;
	sensor_resolution->SensorHighSpeedVideoHeight = imgsensor_info.hs_video.grabwindow_height;

	sensor_resolution->SensorSlimVideoWidth = imgsensor_info.slim_video.grabwindow_width;
	sensor_resolution->SensorSlimVideoHeight = imgsensor_info.slim_video.grabwindow_height;

	sensor_resolution->SensorCustom1Width = imgsensor_info.custom1.grabwindow_width;
	sensor_resolution->SensorCustom1Height = imgsensor_info.custom1.grabwindow_height;

	sensor_resolution->SensorCustom2Width = imgsensor_info.custom2.grabwindow_width;
	sensor_resolution->SensorCustom2Height = imgsensor_info.custom2.grabwindow_height;

	return ERROR_NONE;
} /* get_resolution */

static kal_uint32 get_info(enum MSDK_SCENARIO_ID_ENUM scenario_id,
						   MSDK_SENSOR_INFO_STRUCT* sensor_info,
						   MSDK_SENSOR_CONFIG_STRUCT* sensor_config_data) {
	LOG_INF("scenario_id = %d\n", scenario_id);

	// sensor_info->SensorVideoFrameRate = imgsensor_info.normal_video.max_framerate/10; /* not use */
	// sensor_info->SensorStillCaptureFrameRate= imgsensor_info.cap.max_framerate/10; /* not use */
	// imgsensor_info->SensorWebCamCaptureFrameRate= imgsensor_info.v.max_framerate; /* not use */

	sensor_info->SensorClockPolarity = SENSOR_CLOCK_POLARITY_LOW;
	sensor_info->SensorClockFallingPolarity = SENSOR_CLOCK_POLARITY_LOW; /* not use */
	sensor_info->SensorHsyncPolarity = SENSOR_CLOCK_POLARITY_LOW;        // inverse with datasheet
	sensor_info->SensorVsyncPolarity = SENSOR_CLOCK_POLARITY_LOW;
	sensor_info->SensorInterruptDelayLines = 4; /* not use */
	sensor_info->SensorResetActiveHigh = FALSE; /* not use */
	sensor_info->SensorResetDelayCount = 5;     /* not use */

	sensor_info->SensroInterfaceType = imgsensor_info.sensor_interface_type;
	sensor_info->MIPIsensorType = imgsensor_info.mipi_sensor_type;
	sensor_info->SettleDelayMode = imgsensor_info.mipi_settle_delay_mode;
	sensor_info->SensorOutputDataFormat = imgsensor_info.sensor_output_dataformat;

	sensor_info->CaptureDelayFrame = imgsensor_info.cap_delay_frame;
	sensor_info->PreviewDelayFrame = imgsensor_info.pre_delay_frame;
	sensor_info->VideoDelayFrame = imgsensor_info.video_delay_frame;
	sensor_info->HighSpeedVideoDelayFrame = imgsensor_info.hs_video_delay_frame;
	sensor_info->SlimVideoDelayFrame = imgsensor_info.slim_video_delay_frame;
	sensor_info->SlimVideoDelayFrame = imgsensor_info.slim_video_delay_frame;
	sensor_info->Custom1DelayFrame = imgsensor_info.custom1_delay_frame;
	sensor_info->Custom2DelayFrame = imgsensor_info.custom2_delay_frame;
	sensor_info->SensorMasterClockSwitch = 0; /* not use */
	sensor_info->SensorDrivingCurrent = imgsensor_info.isp_driving_current;

	sensor_info->AEShutDelayFrame = imgsensor_info.ae_shut_delay_frame;              /* The frame of setting shutter default 0 for TG int */
	sensor_info->AESensorGainDelayFrame = imgsensor_info.ae_sensor_gain_delay_frame; /* The frame of setting sensor gain */
	sensor_info->AEISPGainDelayFrame = imgsensor_info.ae_ispGain_delay_frame;
	sensor_info->IHDR_Support = imgsensor_info.ihdr_support;
	sensor_info->IHDR_LE_FirstLine = imgsensor_info.ihdr_le_firstline;
	sensor_info->SensorModeNum = imgsensor_info.sensor_mode_num;

	sensor_info->SensorMIPILaneNumber = imgsensor_info.mipi_lane_num;
	sensor_info->TEMPERATURE_SUPPORT = imgsensor_info.temperature_support;
	sensor_info->SensorClockFreq = imgsensor_info.mclk;
	sensor_info->SensorClockDividCount = 3; /* not use */
	sensor_info->SensorClockRisingCount = 0;
	sensor_info->SensorClockFallingCount = 2; /* not use */
	sensor_info->SensorPixelClockCount = 3;   /* not use */
	sensor_info->SensorDataLatchCount = 2;    /* not use */

	sensor_info->MIPIDataLowPwr2HighSpeedTermDelayCount = 0;
	sensor_info->MIPICLKLowPwr2HighSpeedTermDelayCount = 0;
	sensor_info->SensorWidthSampling = 0;  // 0 is default 1x
	sensor_info->SensorHightSampling = 0;  // 0 is default 1x
	sensor_info->SensorPacketECCOrder = 1;
#ifdef FPT_PDAF_SUPPORT
	sensor_info->PDAF_Support = PDAF_SUPPORT_CAMSV;  // 6;
#endif

	sensor_info->FrameTimeDelayFrame =
		imgsensor_info.frame_time_delay_frame;

	switch (scenario_id) {
		case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
			sensor_info->SensorGrabStartX = imgsensor_info.pre.startx;
			sensor_info->SensorGrabStartY = imgsensor_info.pre.starty;
			sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.pre.mipi_data_lp2hs_settle_dc;
			break;
		case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
			sensor_info->SensorGrabStartX = imgsensor_info.cap.startx;
			sensor_info->SensorGrabStartY = imgsensor_info.cap.starty;
			sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.cap.mipi_data_lp2hs_settle_dc;
			break;
		case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
			sensor_info->SensorGrabStartX = imgsensor_info.normal_video.startx;
			sensor_info->SensorGrabStartY = imgsensor_info.normal_video.starty;
			sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.normal_video.mipi_data_lp2hs_settle_dc;
			break;
		case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
			sensor_info->SensorGrabStartX = imgsensor_info.hs_video.startx;
			sensor_info->SensorGrabStartY = imgsensor_info.hs_video.starty;
			sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.hs_video.mipi_data_lp2hs_settle_dc;
			break;
		case MSDK_SCENARIO_ID_SLIM_VIDEO:
			sensor_info->SensorGrabStartX = imgsensor_info.slim_video.startx;
			sensor_info->SensorGrabStartY = imgsensor_info.slim_video.starty;
			sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.slim_video.mipi_data_lp2hs_settle_dc;
			break;
		case MSDK_SCENARIO_ID_CUSTOM1:
			sensor_info->PDAF_Support = 0;
			sensor_info->SensorGrabStartX = imgsensor_info.custom1.startx;
			sensor_info->SensorGrabStartY = imgsensor_info.custom1.starty;
			sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.custom1.mipi_data_lp2hs_settle_dc;
			break;
		case MSDK_SCENARIO_ID_CUSTOM2:
			sensor_info->SensorGrabStartX = imgsensor_info.custom2.startx;
			sensor_info->SensorGrabStartY = imgsensor_info.custom2.starty;
			sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.custom2.mipi_data_lp2hs_settle_dc;
			break;
		default:
			sensor_info->SensorGrabStartX = imgsensor_info.pre.startx;
			sensor_info->SensorGrabStartY = imgsensor_info.pre.starty;
			sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.pre.mipi_data_lp2hs_settle_dc;
			break;
	}

	return ERROR_NONE;
} /* get_info */

static kal_uint32 control(enum MSDK_SCENARIO_ID_ENUM scenario_id,
						  MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT* image_window,
						  MSDK_SENSOR_CONFIG_STRUCT* sensor_config_data) {
	LOG_INF("scenario_id = %d[%s]\n", scenario_id, scenarios[scenario_id]);
	LOG_INF("[%s] +", __func__);
	spin_lock(&imgsensor_drv_lock);
	imgsensor.current_scenario_id = scenario_id;
	spin_unlock(&imgsensor_drv_lock);
	switch (scenario_id) {
		case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
			preview(image_window, sensor_config_data);
			break;
		case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
			capture(image_window, sensor_config_data);
			break;
		case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
			normal_video(image_window, sensor_config_data);
			break;
		case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
			hs_video(image_window, sensor_config_data);
			break;
		case MSDK_SCENARIO_ID_SLIM_VIDEO:
			slim_video(image_window, sensor_config_data);
			break;
		case MSDK_SCENARIO_ID_CUSTOM1:
			custom1(image_window, sensor_config_data);
			break;
		case MSDK_SCENARIO_ID_CUSTOM2:
			custom2(image_window, sensor_config_data);
			break;
		default:
			LOG_INF("Error ScenarioId setting");
			preview(image_window, sensor_config_data);
			return ERROR_INVALID_SCENARIO_ID;
	}
	LOG_INF("[%s] -", __func__);
	return ERROR_NONE;
} /* control() */

static kal_uint32 set_video_mode(UINT16 framerate) {
	LOG_INF("framerate = %d\n ", framerate);
	// SetVideoMode Function should fix framerate
	if (framerate == 0)
		// Dynamic frame rate
		return ERROR_NONE;
	spin_lock(&imgsensor_drv_lock);
	if ((framerate == 300) && (imgsensor.autoflicker_en == KAL_TRUE))
		imgsensor.current_fps = 296;
	else if ((framerate == 150) && (imgsensor.autoflicker_en == KAL_TRUE))
		imgsensor.current_fps = 146;
	else
		imgsensor.current_fps = framerate;
	spin_unlock(&imgsensor_drv_lock);
	set_max_framerate(imgsensor.current_fps, 1);

	return ERROR_NONE;
}

static kal_uint32 set_auto_flicker_mode(kal_bool enable, UINT16 framerate) {
	LOG_INF("enable = %d, framerate = %d\n", enable, framerate);
	spin_lock(&imgsensor_drv_lock);
	if (enable)  // enable auto flicker
		imgsensor.autoflicker_en = KAL_TRUE;
	else  // Cancel Auto flick
		imgsensor.autoflicker_en = KAL_FALSE;
	spin_unlock(&imgsensor_drv_lock);
	return ERROR_NONE;
}

static kal_uint32 set_max_framerate_by_scenario(enum MSDK_SCENARIO_ID_ENUM scenario_id, MUINT32 framerate) {
	kal_uint32 frame_length;

	LOG_INF("scenario_id = %d[%s], framerate = %d\n", scenario_id, scenarios[scenario_id], framerate);

	switch (scenario_id) {
		case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
			frame_length = imgsensor_info.pre.pclk / framerate * 10 / imgsensor_info.pre.linelength;
			spin_lock(&imgsensor_drv_lock);
			imgsensor.dummy_line = (frame_length > imgsensor_info.pre.framelength) ? (frame_length - imgsensor_info.pre.framelength) : 0;
			imgsensor.frame_length = imgsensor_info.pre.framelength + imgsensor.dummy_line;
			imgsensor.min_frame_length = imgsensor.frame_length;
			spin_unlock(&imgsensor_drv_lock);
			if (imgsensor.frame_length > imgsensor.shutter)
				set_dummy();
			break;
		case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
			if (framerate == 0)
				return ERROR_NONE;
			frame_length = imgsensor_info.normal_video.pclk / framerate * 10 / imgsensor_info.normal_video.linelength;
			spin_lock(&imgsensor_drv_lock);
			imgsensor.dummy_line = (frame_length > imgsensor_info.normal_video.framelength) ? (frame_length - imgsensor_info.normal_video.framelength) : 0;
			imgsensor.frame_length = imgsensor_info.normal_video.framelength + imgsensor.dummy_line;
			imgsensor.min_frame_length = imgsensor.frame_length;
			spin_unlock(&imgsensor_drv_lock);
			if (imgsensor.frame_length > imgsensor.shutter)
				set_dummy();
			break;
		case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
			frame_length = imgsensor_info.cap.pclk / framerate * 10 / imgsensor_info.cap.linelength;
			spin_lock(&imgsensor_drv_lock);
			imgsensor.dummy_line = (frame_length > imgsensor_info.cap.framelength) ? (frame_length - imgsensor_info.cap.framelength) : 0;
			imgsensor.frame_length = imgsensor_info.cap.framelength + imgsensor.dummy_line;
			imgsensor.min_frame_length = imgsensor.frame_length;
			spin_unlock(&imgsensor_drv_lock);

			if (imgsensor.frame_length > imgsensor.shutter)
				set_dummy();
			break;
		case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
			frame_length = imgsensor_info.hs_video.pclk / framerate * 10 / imgsensor_info.hs_video.linelength;
			spin_lock(&imgsensor_drv_lock);
			imgsensor.dummy_line = (frame_length > imgsensor_info.hs_video.framelength) ? (frame_length - imgsensor_info.hs_video.framelength) : 0;
			imgsensor.frame_length = imgsensor_info.hs_video.framelength + imgsensor.dummy_line;
			imgsensor.min_frame_length = imgsensor.frame_length;
			spin_unlock(&imgsensor_drv_lock);
			if (imgsensor.frame_length > imgsensor.shutter)
				set_dummy();
			break;
		case MSDK_SCENARIO_ID_SLIM_VIDEO:
			frame_length = imgsensor_info.slim_video.pclk / framerate * 10 / imgsensor_info.slim_video.linelength;
			spin_lock(&imgsensor_drv_lock);
			imgsensor.dummy_line = (frame_length > imgsensor_info.slim_video.framelength) ? (frame_length - imgsensor_info.slim_video.framelength) : 0;
			imgsensor.frame_length = imgsensor_info.slim_video.framelength + imgsensor.dummy_line;
			imgsensor.min_frame_length = imgsensor.frame_length;
			spin_unlock(&imgsensor_drv_lock);
			if (imgsensor.frame_length > imgsensor.shutter)
				set_dummy();
			break;
		case MSDK_SCENARIO_ID_CUSTOM1:
			frame_length = imgsensor_info.custom1.pclk / framerate * 10 / imgsensor_info.custom1.linelength;
			spin_lock(&imgsensor_drv_lock);
			imgsensor.dummy_line = (frame_length > imgsensor_info.custom1.framelength) ? (frame_length - imgsensor_info.custom1.framelength) : 0;
			if (imgsensor.dummy_line < 0)
				imgsensor.dummy_line = 0;
			imgsensor.frame_length = imgsensor_info.custom1.framelength + imgsensor.dummy_line;
			imgsensor.min_frame_length = imgsensor.frame_length;
			spin_unlock(&imgsensor_drv_lock);
			if (imgsensor.frame_length > imgsensor.shutter)
				set_dummy();
			break;
		case MSDK_SCENARIO_ID_CUSTOM2:
			frame_length = imgsensor_info.custom2.pclk / framerate * 10 / imgsensor_info.custom2.linelength;
			spin_lock(&imgsensor_drv_lock);
			imgsensor.dummy_line = (frame_length > imgsensor_info.custom2.framelength) ? (frame_length - imgsensor_info.custom2.framelength) : 0;
			if (imgsensor.dummy_line < 0)
				imgsensor.dummy_line = 0;
			imgsensor.frame_length = imgsensor_info.custom2.framelength + imgsensor.dummy_line;
			imgsensor.min_frame_length = imgsensor.frame_length;
			spin_unlock(&imgsensor_drv_lock);
			if (imgsensor.frame_length > imgsensor.shutter)
				set_dummy();
			break;
		default:  // coding with  preview scenario by default
			frame_length = imgsensor_info.pre.pclk / framerate * 10 / imgsensor_info.pre.linelength;
			spin_lock(&imgsensor_drv_lock);
			imgsensor.dummy_line = (frame_length > imgsensor_info.pre.framelength) ? (frame_length - imgsensor_info.pre.framelength) : 0;
			imgsensor.frame_length = imgsensor_info.pre.framelength + imgsensor.dummy_line;
			imgsensor.min_frame_length = imgsensor.frame_length;
			spin_unlock(&imgsensor_drv_lock);
			if (imgsensor.frame_length > imgsensor.shutter)
				set_dummy();
			LOG_INF("error scenario_id = %d, we use preview scenario\n", scenario_id);
			break;
	}
	return ERROR_NONE;
}

static kal_uint32 get_default_framerate_by_scenario(enum MSDK_SCENARIO_ID_ENUM scenario_id, MUINT32* framerate) {
	LOG_INF("scenario_id = %d\n", scenario_id);

	switch (scenario_id) {
		case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
			*framerate = imgsensor_info.pre.max_framerate;
			break;
		case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
			*framerate = imgsensor_info.normal_video.max_framerate;
			break;
		case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
			*framerate = imgsensor_info.cap.max_framerate;
			break;
		case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
			*framerate = imgsensor_info.hs_video.max_framerate;
			break;
		case MSDK_SCENARIO_ID_SLIM_VIDEO:
			*framerate = imgsensor_info.slim_video.max_framerate;
			break;
		case MSDK_SCENARIO_ID_CUSTOM1:
			*framerate = imgsensor_info.custom1.max_framerate;
			break;
		case MSDK_SCENARIO_ID_CUSTOM2:
			*framerate = imgsensor_info.custom2.max_framerate;
			break;
		default:
			break;
	}

	return ERROR_NONE;
}

static kal_uint32 set_test_pattern_mode(kal_bool enable) {
	LOG_INF("enable: %d\n", enable);

	if (enable) {
		// 0x5E00[8]: 1 enable,  0 disable
		// 0x5E00[1:0]; 00 Color bar, 01 Random Data, 10 Square, 11 BLACK
		write_cmos_sensor(0x3202, 0x0080);
		write_cmos_sensor(0x3204, 0x0080);
		write_cmos_sensor(0x3206, 0x0080);
		write_cmos_sensor(0x3208, 0x0080);
		write_cmos_sensor(0x3232, 0x0000);
		write_cmos_sensor(0x3234, 0x0000);
		write_cmos_sensor(0x32a0, 0x0100);
		write_cmos_sensor(0x3300, 0x0001);
		write_cmos_sensor(0x3400, 0x0001);
		write_cmos_sensor(0x3402, 0x4e00);
		write_cmos_sensor(0x3268, 0x0000);
		write_cmos_sensor(0x0600, 0x0001);
	} else {
		// 0x5E00[8]: 1 enable,  0 disable
		// 0x5E00[1:0]; 00 Color bar, 01 Random Data, 10 Square, 11 BLACK
		write_cmos_sensor(0x3202, 0x0000);
		write_cmos_sensor(0x3204, 0x0000);
		write_cmos_sensor(0x3206, 0x0000);
		write_cmos_sensor(0x3208, 0x0000);
		write_cmos_sensor(0x3232, 0x0000);
		write_cmos_sensor(0x3234, 0x0000);
		write_cmos_sensor(0x32a0, 0x0000);
		write_cmos_sensor(0x3300, 0x0000);
		write_cmos_sensor(0x3400, 0x0000);
		write_cmos_sensor(0x3402, 0x0000);
		write_cmos_sensor(0x3268, 0x0000);
		write_cmos_sensor(0x0600, 0x0000);
	}
	spin_lock(&imgsensor_drv_lock);
	imgsensor.test_pattern = enable;
	spin_unlock(&imgsensor_drv_lock);
	return ERROR_NONE;
}

static char *features[300] = {
	"SENSOR_FEATURE_BEGIN,                              ",
	"SENSOR_FEATURE_GET_RESOLUTION,                     ",
	"SENSOR_FEATURE_GET_OFFSET_TO_START_OF_EXPOSURE,    ",
	"SENSOR_FEATURE_GET_PERIOD,                         ",
	"SENSOR_FEATURE_GET_PIXEL_CLOCK_FREQ,               ",
	"SENSOR_FEATURE_SET_ESHUTTER,                       ",
	"SENSOR_FEATURE_SET_NIGHTMODE,                      ",
	"SENSOR_FEATURE_SET_GAIN,                           ",
	"SENSOR_FEATURE_SET_DUAL_GAIN,                      ",
	"SENSOR_FEATURE_SET_GAIN_AND_ESHUTTER,              ",
	"SENSOR_FEATURE_SET_FLASHLIGHT,                     ",
	"SENSOR_FEATURE_SET_ISP_MASTER_CLOCK_FREQ,          ",
	"SENSOR_FEATURE_SET_REGISTER,                       ",
	"SENSOR_FEATURE_GET_REGISTER,                       ",
	"SENSOR_FEATURE_SET_CCT_REGISTER,                   ",
	"SENSOR_FEATURE_GET_CCT_REGISTER,                   ",
	"SENSOR_FEATURE_SET_ENG_REGISTER,                   ",
	"SENSOR_FEATURE_GET_ENG_REGISTER,                   ",
	"SENSOR_FEATURE_GET_REGISTER_DEFAULT,               ",
	"SENSOR_FEATURE_GET_CONFIG_PARA,                    ",
	"SENSOR_FEATURE_CAMERA_PARA_TO_SENSOR,              ",
	"SENSOR_FEATURE_SENSOR_TO_CAMERA_PARA,              ",
	"SENSOR_FEATURE_GET_GROUP_COUNT,                    ",
	"SENSOR_FEATURE_GET_GROUP_INFO,                     ",
	"SENSOR_FEATURE_GET_ITEM_INFO,                      ",
	"SENSOR_FEATURE_SET_ITEM_INFO,                      ",
	"SENSOR_FEATURE_GET_ENG_INFO,                       ",
	"SENSOR_FEATURE_GET_LENS_DRIVER_ID,                 ",
	"SENSOR_FEATURE_SET_YUV_CMD,                        ",
	"SENSOR_FEATURE_SET_VIDEO_MODE,                     ",
	"SENSOR_FEATURE_SET_TARGET_FRAME_RATE,              ",
	"SENSOR_FEATURE_SET_CALIBRATION_DATA,               ",
	"SENSOR_FEATURE_SET_SENSOR_SYNC,                    ",
	"SENSOR_FEATURE_INITIALIZE_AF,                      ",
	"SENSOR_FEATURE_CONSTANT_AF,                        ",
	"SENSOR_FEATURE_INFINITY_AF,                        ",
	"SENSOR_FEATURE_MOVE_FOCUS_LENS,                    ",
	"SENSOR_FEATURE_GET_AF_STATUS,                      ",
	"SENSOR_FEATURE_GET_AE_STATUS,                      ",
	"SENSOR_FEATURE_GET_AWB_STATUS,                     ",
	"SENSOR_FEATURE_GET_AF_INF,                         ",
	"SENSOR_FEATURE_GET_AF_MACRO,                       ",
	"SENSOR_FEATURE_CHECK_SENSOR_ID,                    ",
	"SENSOR_FEATURE_SET_AUTO_FLICKER_MODE,              ",
	"SENSOR_FEATURE_SET_TEST_PATTERN,                   ",
	"SENSOR_FEATURE_SET_SOFTWARE_PWDN,                  ",
	"SENSOR_FEATURE_SINGLE_FOCUS_MODE,                  ",
	"SENSOR_FEATURE_CANCEL_AF,                          ",
	"SENSOR_FEATURE_SET_AF_WINDOW,                      ",
	"SENSOR_FEATURE_GET_EV_AWB_REF,                     ",
	"SENSOR_FEATURE_GET_SHUTTER_GAIN_AWB_GAIN,          ",
	"SENSOR_FEATURE_GET_AF_MAX_NUM_FOCUS_AREAS,         ",
	"SENSOR_FEATURE_GET_AE_MAX_NUM_METERING_AREAS,      ",
	"SENSOR_FEATURE_SET_AE_WINDOW,                      ",
	"SENSOR_FEATURE_GET_EXIF_INFO,                      ",
	"SENSOR_FEATURE_GET_DELAY_INFO,                     ",
	"SENSOR_FEATURE_SET_SLAVE_I2C_ID,                   ",
	"SENSOR_FEATURE_SUSPEND,                            ",
	"SENSOR_FEATURE_RESUME,                             ",
	"SENSOR_FEATURE_SET_MAX_FRAME_RATE_BY_SCENARIO,     ",
	"SENSOR_FEATURE_GET_DEFAULT_FRAME_RATE_BY_SCENARIO, ",
	"SENSOR_FEATURE_GET_AE_AWB_LOCK_INFO,               ",
	"SENSOR_FEATURE_AUTOTEST_CMD,                       ",
	"SENSOR_FEATURE_GET_TEST_PATTERN_CHECKSUM_VALUE,    ",
	"SENSOR_FEATURE_GET_TEMPERATURE_VALUE,              ",
	"SENSOR_FEATURE_GET_SENSOR_CURRENT_TEMPERATURE,     ",
	"SENSOR_FEATURE_GET_AE_FLASHLIGHT_INFO,             ",
	"SENSOR_FEATURE_GET_TRIGGER_FLASHLIGHT_INFO,        ",
	"SENSOR_FEATURE_SET_YUV_3A_CMD,                     ",
	"SENSOR_FEATURE_SET_N3D_I2C_STREAM_REGDATA,         ",
	"SENSOR_FEATURE_SET_N3D_STOP_STREAMING,             ",
	"SENSOR_FEATURE_SET_N3D_START_STREAMING,            ",
	"SENSOR_FEATURE_GET_SENSOR_N3D_STREAM_TO_VSYNC_TIME,",
	"SENSOR_FEATURE_SET_ESHUTTER_GAIN,                  ",
	"SENSOR_FEATURE_SET_OB_LOCK,                        ",
	"SENSOR_FEATURE_SET_SENSOR_OTP_AWB_CMD,             ",
	"SENSOR_FEATURE_SET_SENSOR_OTP_LSC_CMD,             ",
	"SENSOR_FEATURE_GET_YUV_CAPTURE_OUTPUT_JPEG,        ",
	"SENSOR_FEATURE_SET_YUV_JPEG_PARA,                  ",
	"SENSOR_FEATURE_GET_YUV_JPEG_INFO,                  ",
	"SENSOR_FEATURE_SET_FRAMERATE,                      ",
	"SENSOR_FEATURE_SET_HDR,                            ",
	"SENSOR_FEATURE_GET_CROP_INFO,                      ",
	"SENSOR_FEATURE_GET_VC_INFO,                        ",
	"SENSOR_FEATURE_SET_IHDR_SHUTTER_GAIN,              ",
	"SENSOR_FEATURE_SET_AWB_GAIN,                       ",
	"SENSOR_FEATURE_SET_MIN_MAX_FPS,                    ",
	"SENSOR_FEATURE_GET_PDAF_INFO,                      ",
	"SENSOR_FEATURE_GET_PDAF_DATA,                      ",
	"SENSOR_FEATURE_SET_PDFOCUS_AREA,                   ",
	"SENSOR_FEATURE_GET_SENSOR_PDAF_CAPACITY,           ",
	"SENSOR_FEATURE_DEBUG_IMGSENSOR,                    ",
	"SENSOR_FEATURE_SET_HDR_SHUTTER,                    ",
	"SENSOR_FEATURE_SET_ISO,                            ",
	"SENSOR_FEATURE_SET_PDAF,                           ",
	"SENSOR_FEATURE_SET_SHUTTER_FRAME_TIME,             ",
	"SENSOR_FEATURE_SET_SHUTTER_BUF_MODE,               ",
	"SENSOR_FEATURE_SET_GAIN_BUF_MODE,                  ",
	"SENSOR_FEATURE_SET_I2C_BUF_MODE_EN,                ",
	"SENSOR_FEATURE_GET_SENSOR_HDR_CAPACITY,            ",
	"SENSOR_FEATURE_GET_PDAF_TYPE,                      ",
	"SENSOR_FEATURE_SET_PDAF_TYPE,                      ",
	"SENSOR_FEATURE_GET_PDAF_REG_SETTING,               ",
	"SENSOR_FEATURE_SET_PDAF_REG_SETTING,               ",
	"SENSOR_FEATURE_SET_STREAMING_SUSPEND,              ",
	"SENSOR_FEATURE_SET_STREAMING_RESUME,               ",
	"SENSOR_FEATURE_OPEN,                               ",
	"SENSOR_FEATURE_CLOSE,                              ",
	"SENSOR_FEATURE_SET_DRIVER,                         ",
	"SENSOR_FEATURE_CHECK_IS_ALIVE,                     ",
	"SENSOR_FEATURE_GET_4CELL_DATA,                     ",
	"SENSOR_FEATURE_SET_WAKE_LOCK,                      ",
	"SENSOR_FEATURE_GET_MIPI_PIXEL_RATE,                ",
	"SENSOR_FEATURE_SET_HDR_ATR,                        ",
	"SENSOR_FEATURE_SET_HDR_TRI_GAIN,                   ",
	"SENSOR_FEATURE_SET_HDR_TRI_SHUTTER,                ",
	"SENSOR_FEATURE_SET_LSC_TBL,                        ",
	"SENSOR_FEATURE_GET_SENSOR_SYNC_MODE_CAPACITY,      ",
	"SENSOR_FEATURE_GET_SENSOR_SYNC_MODE,               ",
	"SENSOR_FEATURE_SET_SENSOR_SYNC_MODE,               ",
	"SENSOR_FEATURE_GET_PIXEL_RATE,                     ",
	"SENSOR_FEATURE_MAX                                 "
};

static kal_uint32 feature_control(MSDK_SENSOR_FEATURE_ENUM feature_id,
								  UINT8* feature_para,
								  UINT32* feature_para_len) {
	UINT16* feature_return_para_16 = (UINT16*)feature_para;
	UINT16* feature_data_16 = (UINT16*)feature_para;
	UINT32* feature_return_para_32 = (UINT32*)feature_para;
	UINT32* feature_data_32 = (UINT32*)feature_para;
	// UINT32 *feature_data_tmp;
	unsigned long long* feature_data = (unsigned long long*)feature_para;
	// unsigned long long *feature_return_para=(unsigned long long *) feature_para;

	struct SENSOR_WINSIZE_INFO_STRUCT* wininfo;
	MSDK_SENSOR_REG_INFO_STRUCT* sensor_reg_data = (MSDK_SENSOR_REG_INFO_STRUCT*)feature_para;
#ifdef FPT_PDAF_SUPPORT
	struct SET_PD_BLOCK_INFO_T* PDAFinfo;
    struct SENSOR_VC_INFO_STRUCT *pvcinfo;
#endif

	LOG_INF("feature_id = %d[%s]\n", feature_id, features[feature_id - SENSOR_FEATURE_START]);
	switch (feature_id) {
		case SENSOR_FEATURE_GET_ANA_GAIN_TABLE:
			break;
		case SENSOR_FEATURE_GET_AWB_REQ_BY_SCENARIO:
			switch (*feature_data) {
				case MSDK_SCENARIO_ID_CUSTOM3:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) = 1;
					break;
				default:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) = 0;
					break;
			}
			break;
		case SENSOR_FEATURE_GET_GAIN_RANGE_BY_SCENARIO:
			*(feature_data + 1) = imgsensor_info.min_gain;
			*(feature_data + 2) = imgsensor_info.max_gain;
			break;
		case SENSOR_FEATURE_GET_BASE_GAIN_ISO_AND_STEP:
			*(feature_data + 0) = imgsensor_info.min_gain_iso;
			*(feature_data + 1) = imgsensor_info.gain_step;
			*(feature_data + 2) = imgsensor_info.gain_type;
			break;
		case SENSOR_FEATURE_GET_MIN_SHUTTER_BY_SCENARIO:
			*(feature_data + 1) = imgsensor_info.min_shutter;
			*(feature_data + 2) = imgsensor_info.exp_step;
			switch (*feature_data) {
				case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
				case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
				case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
				case MSDK_SCENARIO_ID_SLIM_VIDEO:
				case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
				case MSDK_SCENARIO_ID_CUSTOM1:
				case MSDK_SCENARIO_ID_CUSTOM2:
					*(feature_data + 2) = 2;
					break;
				case MSDK_SCENARIO_ID_CUSTOM3:
				default:
					*(feature_data + 2) = 1;
					break;
			}
			break;
		case SENSOR_FEATURE_GET_AE_EFFECTIVE_FRAME_FOR_LE:
			*feature_return_para_32 = imgsensor.current_ae_effective_frame;
			break;

		case SENSOR_FEATURE_GET_AE_FRAME_MODE_FOR_LE:
			memcpy(feature_return_para_32, &imgsensor.ae_frm_mode,
				   sizeof(struct IMGSENSOR_AE_FRM_MODE));
			break;
		case SENSOR_FEATURE_GET_OFFSET_TO_START_OF_EXPOSURE:
			*(MUINT32*)(uintptr_t)(*(feature_data + 1)) = 3000000;
			break;
		case SENSOR_FEATURE_GET_PERIOD:
			*feature_return_para_16++ = imgsensor.line_length;
			*feature_return_para_16 = imgsensor.frame_length;
			*feature_para_len = 4;
			break;
		case SENSOR_FEATURE_GET_PERIOD_BY_SCENARIO:
			switch (*feature_data) {
				case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) = (imgsensor_info.cap.framelength << 16) + imgsensor_info.cap.linelength;
					break;
				case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) = (imgsensor_info.normal_video.framelength << 16) + imgsensor_info.normal_video.linelength;
					break;
				case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) = (imgsensor_info.hs_video.framelength << 16) + imgsensor_info.hs_video.linelength;
					break;
				case MSDK_SCENARIO_ID_SLIM_VIDEO:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) = (imgsensor_info.slim_video.framelength << 16) + imgsensor_info.slim_video.linelength;
					break;
				case MSDK_SCENARIO_ID_CUSTOM1:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) = (imgsensor_info.custom1.framelength << 16) + imgsensor_info.custom1.linelength;
					break;
				case MSDK_SCENARIO_ID_CUSTOM2:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) = (imgsensor_info.custom2.framelength << 16) + imgsensor_info.custom2.linelength;
					break;
				case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
				default:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) = (imgsensor_info.pre.framelength << 16) + imgsensor_info.pre.linelength;
					break;
			}
			break;
		case SENSOR_FEATURE_GET_PIXEL_CLOCK_FREQ:
			*feature_return_para_32 = imgsensor.pclk;
			*feature_para_len = 4;
			break;
		case SENSOR_FEATURE_GET_PIXEL_CLOCK_FREQ_BY_SCENARIO:
			switch (*feature_data) {
				case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) = imgsensor_info.cap.pclk;
					break;
				case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) = imgsensor_info.normal_video.pclk;
					break;
				case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) = imgsensor_info.hs_video.pclk;
					break;
				case MSDK_SCENARIO_ID_SLIM_VIDEO:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) = imgsensor_info.slim_video.pclk;
					break;
				case MSDK_SCENARIO_ID_CUSTOM1:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) = imgsensor_info.custom1.pclk;
					break;
				case MSDK_SCENARIO_ID_CUSTOM2:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) = imgsensor_info.custom2.pclk;
					break;
				case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
				default:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) = imgsensor_info.pre.pclk;
					break;
			}
			break;
		case SENSOR_FEATURE_SET_ESHUTTER:
			set_shutter(*feature_data);
			break;
		case SENSOR_FEATURE_SET_NIGHTMODE:
			night_mode((BOOL)*feature_data);
			break;
		case SENSOR_FEATURE_SET_GAIN:
			set_gain((UINT16)*feature_data);
			break;
		case SENSOR_FEATURE_SET_FLASHLIGHT:
			break;
		case SENSOR_FEATURE_SET_ISP_MASTER_CLOCK_FREQ:
			break;
		case SENSOR_FEATURE_SET_REGISTER:
			write_cmos_sensor_byte(sensor_reg_data->RegAddr, sensor_reg_data->RegData);
			break;
		case SENSOR_FEATURE_GET_REGISTER:
			sensor_reg_data->RegData = read_cmos_sensor_byte(sensor_reg_data->RegAddr);
			break;
		case SENSOR_FEATURE_GET_LENS_DRIVER_ID:
			// get the lens driver ID from EEPROM or just return LENS_DRIVER_ID_DO_NOT_CARE
			// if EEPROM does not exist in camera module.
			*feature_return_para_32 = LENS_DRIVER_ID_DO_NOT_CARE;
			*feature_para_len = 4;
			break;
		case SENSOR_FEATURE_SET_VIDEO_MODE:
			set_video_mode(*feature_data);
			break;
		case SENSOR_FEATURE_CHECK_SENSOR_ID:
			get_imgsensor_id(feature_return_para_32);
			break;
		case SENSOR_FEATURE_SET_AUTO_FLICKER_MODE:
			set_auto_flicker_mode((BOOL)(*feature_data_16), *(feature_data_16 + 1));
			break;
		case SENSOR_FEATURE_SET_MAX_FRAME_RATE_BY_SCENARIO:
			set_max_framerate_by_scenario((enum MSDK_SCENARIO_ID_ENUM) * feature_data, *(feature_data + 1));
			break;
		case SENSOR_FEATURE_GET_DEFAULT_FRAME_RATE_BY_SCENARIO:
			get_default_framerate_by_scenario((enum MSDK_SCENARIO_ID_ENUM) * (feature_data), (MUINT32*)(uintptr_t)(*(feature_data + 1)));
			break;
		case SENSOR_FEATURE_SET_TEST_PATTERN:
			set_test_pattern_mode((BOOL)(*feature_data));
			break;
		case SENSOR_FEATURE_GET_TEST_PATTERN_CHECKSUM_VALUE:  // for factory mode auto testing
			*feature_return_para_32 = imgsensor_info.checksum_value;
			*feature_para_len = 4;
			break;
		case SENSOR_FEATURE_SET_FRAMERATE:
			LOG_INF("current fps :%d\n", (UINT32)*feature_data);
			spin_lock(&imgsensor_drv_lock);
			imgsensor.current_fps = *feature_data;
			spin_unlock(&imgsensor_drv_lock);
			break;
		case SENSOR_FEATURE_SET_HDR:
			LOG_INF("ihdr enable :%d\n", (BOOL)*feature_data);
			spin_lock(&imgsensor_drv_lock);
			imgsensor.ihdr_en = (BOOL)*feature_data;
			spin_unlock(&imgsensor_drv_lock);
			break;
		case SENSOR_FEATURE_GET_CROP_INFO:
			// LOG_INF("SENSOR_FEATURE_GET_CROP_INFO scenarioId:%lld\n", (UINT32)*feature_data);

			wininfo = (struct SENSOR_WINSIZE_INFO_STRUCT*)(uintptr_t)(*(feature_data + 1));

			switch (*feature_data_32) {
				case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
					memcpy((void*)wininfo, (void*)&imgsensor_winsize_info[1], sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
					break;
				case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
					memcpy((void*)wininfo, (void*)&imgsensor_winsize_info[2], sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
					break;
				case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
					memcpy((void*)wininfo, (void*)&imgsensor_winsize_info[3], sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
					break;
				case MSDK_SCENARIO_ID_SLIM_VIDEO:
					memcpy((void*)wininfo, (void*)&imgsensor_winsize_info[4], sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
					break;
				case MSDK_SCENARIO_ID_CUSTOM1:
					memcpy((void*)wininfo, (void*)&imgsensor_winsize_info[5], sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
					break;
				case MSDK_SCENARIO_ID_CUSTOM2:
					memcpy((void*)wininfo, (void*)&imgsensor_winsize_info[6], sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
					break;
				case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
				default:
					memcpy((void*)wininfo, (void*)&imgsensor_winsize_info[0], sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
					break;
			}
			break;
		case SENSOR_FEATURE_SET_IHDR_SHUTTER_GAIN:
			LOG_INF("SENSOR_SET_SENSOR_IHDR LE=%d, SE=%d, Gain=%d\n", (UINT16)*feature_data, (UINT16) * (feature_data + 1), (UINT16) * (feature_data + 2));
			ihdr_write_shutter_gain((UINT16)*feature_data, (UINT16) * (feature_data + 1), (UINT16) * (feature_data + 2));
			break;
#if EEPROM_READY //stan
        case SENSOR_FEATURE_GET_4CELL_DATA:/*get 4 cell data from eeprom*/
            {
                int type = (kal_uint16)(*feature_data);
                char *data = (char *)(uintptr_t)(*(feature_data+1));
                if ( type == FOUR_CELL_CAL_TYPE_GAIN_TBL ) {
                pr_debug("Read Cross Talk Start");
                copy_4cell_from_eeprom(data);
                pr_err("Read Cross Talk = %02x %02x %02x %02x %02x %02x\n",
                    (UINT16)data[0], (UINT16)data[1],
                    (UINT16)data[2], (UINT16)data[3],
                    (UINT16)data[4], (UINT16)data[5]);
                }
            break;
        }
#endif

		case SENSOR_FEATURE_SET_SHUTTER_FRAME_TIME:
			set_shutter_frame_length((UINT32) *feature_data, (UINT16) *(feature_data + 1));
			break;

#ifdef FPT_PDAF_SUPPORT
			//////////////////////////////////////////////////
			/*					PDAF START					*/
			//////////////////////////////////////////////////
		case SENSOR_FEATURE_GET_PDAF_INFO:
			LOG_INF("SENSOR_FEATURE_GET_PDAF_INFO scenarioID:%lld\n ", *feature_data);
			PDAFinfo = (struct SET_PD_BLOCK_INFO_T*)(uintptr_t)(*(feature_data + 1));

			switch (*feature_data) {
                                case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
				case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
					memcpy((void*)PDAFinfo, (void*)&imgsensor_pd_info, sizeof(struct SET_PD_BLOCK_INFO_T));
					break;
				case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
					memcpy((void*)PDAFinfo, (void*)&imgsensor_pd_info_16_9, sizeof(struct SET_PD_BLOCK_INFO_T));
					break;
				case MSDK_SCENARIO_ID_CUSTOM1:
					memcpy((void*)PDAFinfo, (void*)&imgsensor_pd_info, sizeof(struct SET_PD_BLOCK_INFO_T));
					break;
				case MSDK_SCENARIO_ID_CUSTOM2:
				case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
				case MSDK_SCENARIO_ID_SLIM_VIDEO:
				default:
					break;
			}
			break;
        case SENSOR_FEATURE_GET_VC_INFO:
                LOG_INF("SENSOR_FEATURE_GET_VC_INFO %d\n",(UINT16)*feature_data);
                pvcinfo = (struct SENSOR_VC_INFO_STRUCT*)(uintptr_t)(*(feature_data+1));
                switch (*feature_data_32)
                {
                    case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
                        LOG_INF("SENSOR_FEATURE_GET_VC_INFO CAPTURE_JPEG\n");
                        memcpy((void *)pvcinfo,(void*)&SENSOR_VC_INFO[0],sizeof(struct SENSOR_VC_INFO_STRUCT));
                        break;
                    case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
                        LOG_INF("SENSOR_FEATURE_GET_VC_INFO PREVIEW\n");
                        memcpy((void *)pvcinfo,(void*)&SENSOR_VC_INFO[0],sizeof(struct SENSOR_VC_INFO_STRUCT));
                        break;
                    case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
                        LOG_INF("SENSOR_FEATURE_GET_VC_INFO PREVIEW\n");
                        memcpy((void *)pvcinfo,(void*)&SENSOR_VC_INFO[1],sizeof(struct SENSOR_VC_INFO_STRUCT));
                        break;
                    default:
                         LOG_INF("SENSOR_FEATURE_GET_VC_INFO DEFAULT_PREVIEW\n");
                         //memcpy((void *)pvcinfo,(void*)&SENSOR_VC_INFO[0],sizeof(struct SENSOR_VC_INFO_STRUCT));
                         memcpy((void *)pvcinfo,(void*)&SENSOR_VC_INFO[0],sizeof(struct SENSOR_VC_INFO_STRUCT));
                         break;
                }
                break;
		case SENSOR_FEATURE_GET_SENSOR_PDAF_CAPACITY:
			LOG_INF("SENSOR_FEATURE_GET_SENSOR_PDAF_CAPACITY scenarioId:%lld\n", *feature_data);

			switch (*feature_data) {
				case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) = 1;
					break;
				case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) = 1;  // video & capture use same setting
					break;
				case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) = 0;
					break;
				case MSDK_SCENARIO_ID_SLIM_VIDEO:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) = 0;
					break;
				case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) = 1;
					break;
				case MSDK_SCENARIO_ID_CUSTOM1:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) = 1;
					break;
				case MSDK_SCENARIO_ID_CUSTOM2:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) = 0;
					break;
				default:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) = 0;
					break;
			}
			break;

			// case SENSOR_FEATURE_GET_PDAF_DATA:
			// 		LOG_INF("SENSOR_FEATURE_GET_PDAF_DATA scenarioId:%lld\n", *feature_data);
			// 		S5KJN1_read_eeprom((kal_uint16)(*feature_data),
			// 	     (BYTE *)(uintptr_t)(*(feature_data+1)),
			// 	     (kal_uint32)(*(feature_data+2)));
			// 		LOG_INF("SENSOR_FEATURE_GET_PDAF_DATA success\n");
			// 		break;

		case SENSOR_FEATURE_SET_PDAF:
			LOG_INF("PDAF mode :%d\n", *feature_data_16);
			// imgsensor.pdaf_mode = *feature_Data_16;
			break;
			//////////////////////////////////////////////////
			/*					PDAF END					*/
			//////////////////////////////////////////////////
#endif

		case SENSOR_FEATURE_GET_SENSOR_SYNC_MODE_CAPACITY:
			*feature_return_para_32 = SENSOR_MASTER_SYNC_MODE;
			*feature_para_len = 4;
			break;
		case SENSOR_FEATURE_GET_SENSOR_SYNC_MODE:
			*feature_return_para_32 = g_sync_mode;
			*feature_para_len = 4;
			break;
		case SENSOR_FEATURE_SET_SENSOR_SYNC_MODE:
			g_sync_mode = (MUINT32)(*feature_data_32);
			LOG_INF("[hwadd]mode = %d\n", g_sync_mode);
			break;

		case SENSOR_FEATURE_SET_STREAMING_SUSPEND:
			pr_debug("SENSOR_FEATURE_SET_STREAMING_SUSPEND\n");
			streaming_control(KAL_FALSE);
			break;
		case SENSOR_FEATURE_SET_STREAMING_RESUME:
			pr_debug("SENSOR_FEATURE_SET_STREAMING_RESUME, shutter:%llu\n", *feature_data);
			if (*feature_data != 0)
				set_shutter(*feature_data);
			streaming_control(KAL_TRUE);
			break;
		case SENSOR_FEATURE_GET_PIXEL_RATE:
			switch (*feature_data) {
				case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) =
						(imgsensor_info.cap.pclk /
						 (imgsensor_info.cap.linelength - 80)) *
						imgsensor_info.cap.grabwindow_width;

					break;
				case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) =
						(imgsensor_info.normal_video.pclk /
						 (imgsensor_info.normal_video.linelength - 80)) *
						imgsensor_info.normal_video.grabwindow_width;

					break;
				case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) =
						(imgsensor_info.hs_video.pclk /
						 (imgsensor_info.hs_video.linelength - 80)) *
						imgsensor_info.hs_video.grabwindow_width;

					break;
				case MSDK_SCENARIO_ID_SLIM_VIDEO:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) =
						(imgsensor_info.slim_video.pclk /
						 (imgsensor_info.slim_video.linelength - 80)) *
						imgsensor_info.slim_video.grabwindow_width;

					break;
				case MSDK_SCENARIO_ID_CUSTOM1:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) =
						(imgsensor_info.custom1.pclk /
						 (imgsensor_info.custom1.linelength - 80)) *
						imgsensor_info.custom1.grabwindow_width;

					break;
				case MSDK_SCENARIO_ID_CUSTOM2:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) =
						(imgsensor_info.custom2.pclk /
						 (imgsensor_info.custom2.linelength - 80)) *
						imgsensor_info.custom2.grabwindow_width;

					break;
				case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
				default:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) =
						(imgsensor_info.pre.pclk /
						 (imgsensor_info.pre.linelength - 80)) *
						imgsensor_info.pre.grabwindow_width;
					break;
			}
			break;

		case SENSOR_FEATURE_GET_BINNING_TYPE:
			switch (*(feature_data + 1)) {
				case MSDK_SCENARIO_ID_CUSTOM3:
					*feature_return_para_32 = 1; /*BINNING_NONE*/
					break;
				case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
				case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
				case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
				case MSDK_SCENARIO_ID_SLIM_VIDEO:
				case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
				case MSDK_SCENARIO_ID_CUSTOM1:
				case MSDK_SCENARIO_ID_CUSTOM2:
				default:
					*feature_return_para_32 = 1; /*BINNING_AVERAGED*/
					break;
			}
			pr_debug("SENSOR_FEATURE_GET_BINNING_TYPE AE_binning_type:%d,\n",
					 *feature_return_para_32);
			*feature_para_len = 4;

			break;

		case SENSOR_FEATURE_GET_MIPI_PIXEL_RATE:
			switch (*feature_data) {
				case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) =
						imgsensor_info.cap.mipi_pixel_rate;
					break;
				case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) =
						imgsensor_info.normal_video.mipi_pixel_rate;
					break;
				case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) =
						imgsensor_info.hs_video.mipi_pixel_rate;
					break;
				case MSDK_SCENARIO_ID_SLIM_VIDEO:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) =
						imgsensor_info.slim_video.mipi_pixel_rate;
					break;
				case MSDK_SCENARIO_ID_CUSTOM1:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) =
						imgsensor_info.custom1.mipi_pixel_rate;
					break;
				case MSDK_SCENARIO_ID_CUSTOM2:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) =
						imgsensor_info.custom2.mipi_pixel_rate;
					break;
				case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
				default:
					*(MUINT32*)(uintptr_t)(*(feature_data + 1)) =
						imgsensor_info.pre.mipi_pixel_rate;
					break;
			}
			break;

		default:
			break;
	}

	return ERROR_NONE;
} /* feature_control() */

static struct SENSOR_FUNCTION_STRUCT sensor_func = {
	open,
	get_info,
	get_resolution,
	feature_control,
	control,
	close
};

UINT32 S5KJN1_TXD_MIPI_RAW_SensorInit(struct SENSOR_FUNCTION_STRUCT** pfFunc) {
	LOG_INF("S5KJN1_TXD_MIPI_RAW_SensorInit in\n");
	/* To Do : Check Sensor status here */
	if (pfFunc != NULL)
		*pfFunc = &sensor_func;
	return ERROR_NONE;
} /* S5KJN1_TXD_MIPI_RAW_SensorInit	*/

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("camera information");

