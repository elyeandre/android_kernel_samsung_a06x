/****************************************************************************************
 *
 * @File Name   : lct_tp_driver.h
 * @Author      : longcheer
 * @Create Time : 2024-05-17 17:34:43
 * @Description : Display touchpad information and Longcheer TP-selftest.
 *
 ****************************************************************************************/

#ifndef __LCT_TP_DRIVER_H__
#define __LCT_TP_DRIVER_H__


/*Debug log start*/
#define TP_INFO(fmt, arg...) pr_err("LCT_TP_INFO:[INFO] (%s, %d): " fmt, __func__, __LINE__, ##arg)
#define TP_ERR(fmt, arg...) pr_err("LCT_TP_INFO:[ERR] (%s, %d): " fmt, __func__, __LINE__, ##arg)

#define TP_INFO_TAG "LCT_TP_SELFTEST"
#define TP_LOGW(log, ...) printk(KERN_WARNING "[%s] %s (line %d): " log, TP_INFO_TAG, __func__, __LINE__, ##__VA_ARGS__)
#define TP_LOGE(log, ...) printk(KERN_ERR "[%s] %s ERROR (line %d): " log, TP_INFO_TAG, __func__, __LINE__, ##__VA_ARGS__)

#define TP_GEST_INFO(fmt, arg...) pr_err("TP_GESTURE:[INFO] (%s, %d): " fmt, __func__, __LINE__, ##arg)
#define TP_GEST_ERR(fmt, arg...) pr_err("TP_GESTURE:[ERR] (%s, %d): " fmt, __func__, __LINE__, ##arg)
/*Debug log end*/

struct touch_panel {
	void (*charger_mode_switch_status)(int status);
};

/*add lct tp info start*/
extern void update_lct_tp_info(char *tp_info_buf);
/*add lct tp info end*/


/*add lct tp selftest start*/
#define TP_SELF_TEST_RETROY_COUNT 3
#define TP_SELF_TEST_CHECK_STATE_COUNT 30

#define TP_SELF_TEST_PROC_FILE "tp_selftest"

#define TP_SELF_TEST_RESULT_UNKNOWN "0\n"
#define TP_SELF_TEST_RESULT_FAIL "1\n"
#define TP_SELF_TEST_RESULT_PASS "2\n"

#define TP_SELF_TEST_LONGCHEER_MMI_CMD "mmi"

enum lct_tp_selftest_cmd {
	TP_SELFTEST_CMD_LONGCHEER_MMI = 0x00,
};

//define touchpad self-test callback type
typedef int (*tp_selftest_callback_t)(unsigned char cmd);
static tp_selftest_callback_t tp_selftest_callback_func;

//set touchpad self-test callback funcation
extern void lct_tp_selftest_init(tp_selftest_callback_t callback);
/*add lct tp selftest end*/

/*add lct tp gesture start*/
extern int double_click_flag;
/*add lct tp gesture end*/

#endif //__LCT_TP_DRIVER_H__
