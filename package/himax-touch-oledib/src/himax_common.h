/* SPDX-License-Identifier: GPL-2.0 */
/*  Himax Android Driver Sample Code for common functions
 *
 *  Copyright (C) 2026 Himax Corporation.
 *
 *  This software is licensed under the terms of the GNU General Public
 *  License version 2,  as published by the Free Software Foundation,  and
 *  may be copied,  distributed,  and modified under those terms.
 *
 *  This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#ifndef HIMAX_COMMON_H
#define HIMAX_COMMON_H

#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/async.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/input/mt.h>
#include <linux/firmware.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/pm_wakeup.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/kallsyms.h>
#include <linux/of_gpio.h>
#include <linux/version.h>
#include <linux/ctype.h>
#include <linux/rtc.h>
#include "himax_platform.h"

#define HIMAX_DRIVER_VER "Sample_code_A09.23_HX8530_Harman_SDC17_20260602"
/*#define Tp_inspect_mode_patch*/
#define FLASH_DUMP_FILE "/sdcard/HX_Flash_Dump.bin"

/*===========Himax Option function=============*/
#define HX_BOOT_UPGRADE					(0x00)
#define HX_EXCP_RECOVERY				(0x00)
#define HX_PROTOCOL_A					(0x00)
#define HX_PROTOCOL_B_3PA				(0x01)
#define HX_RST_PIN_FUNC					(0x01)
#define HX_TP_INSPECT_MODE				(0x01)
#define HX_FIX_TOUCH_INFO				(0x01)
#define HX_WPBP_ENABLE					(0x00)
#define HX_SMART_WAKEUP					(0x00)
#define HX_GESTURE_TRACK				(0x00)
#define HX_TP_GTS_MODE					(0x00)
#define HX_PANEL_ID_CHECK				(0x00)
#define HX_VERIFY_TICKET_NUMBER			(0x00)
#define HX_SRAM_TEST					(0x00)
#define HX_DUAL_BANK_FLASH				(0x00)
/*=============================================*/

#if defined(CONFIG_TOUCHSCREEN_HIMAX_OFR_SPI)
#undef HX_DUAL_BANK_FLASH
/* force enable */
#define HX_DUAL_BANK_FLASH				(0x01)  
#endif
/*  HOT_PLUG need before struct himax_ts_data  */
/*  #define HOT_PLUG */
#if defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83192)
	extern bool hx83192_init(void);
#endif
#if defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83193)
	extern bool hx83193_init(void);
#endif
#if defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83194)
	extern bool hx83194_init(void);
#endif
#if defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83195)
	extern bool hx83195_init(void);
#endif
#if defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83180)
	extern bool hx83180_init(void);
#endif
#if defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83181)
	extern bool hx83181_init(void);
#endif
#if defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX8530)
	#define	HX8530_ALG_2_SECTION 		 0x10000900U
	#define	HX8530_CFG_1_SECTION  		 0x10000901U
	extern bool hx8530_init(void);
	extern bool hx8530_mcu_program_to_slave_flash(uint8_t input_byte);
	extern bool hx8530_cascaded_arch_check(void);
#endif

#define TW_SE "<eason_chao@himax.com.tw><brian_huang@himax.com.tw>"
#define CN_FAE "<edwin_quan@himax.com.cn><nana_huang@himax.com.cn>"
#define Check_List "1.VCCD,VSP,VSN,VGH,VGL 2.RESX,TP_EXT_RSTN,PON 3.Power On Sequence"

struct himax_ic_data {
	uint16_t vendor_arch_ver;
	uint16_t vendor_config_ver;
	uint8_t vendor_touch_cfg_ver;
	uint8_t vendor_display_cfg_ver;
	uint8_t vendor_cid_maj_ver;
	uint8_t vendor_cid_min_ver;
	uint8_t vendor_panel_ver;
	uint8_t vendor_remark1[13];
	uint8_t vendor_remark2[13];
	uint8_t vendor_ticket[13];
	uint8_t vendor_config_date[13];
	uint8_t vendor_cus_info[13];
	uint8_t vendor_proj_info[13];
	uint8_t HX_RX_NUM;
	uint8_t HX_TX_NUM;
	uint16_t HX_X_RES;
	uint16_t HX_Y_RES;
	uint32_t HX_FW_SIZE;
	uint8_t HX_MAX_PT;
	bool HX_INT_IS_EDGE;
	bool HX_IS_ID_EN;
	bool HX_ID_PALM_EN;
	uint8_t STOP_FW_BY_HOST_EN;
	uint8_t HX_RX_IC_NUM;
	uint8_t HX_TX_IC_NUM;
	uint8_t HX_CHIP_RX_MAX;
	uint8_t HX_CHIP_TX_MAX;
	uint8_t FW_update_flag;
	uint8_t HX8530_upgrade_section;
	uint16_t HX8530_TOTAL_RX_NUM;
	uint16_t HX8530_TOTAL_TX_NUM;
	uint16_t HX8530_IC_RX_NUM[8];
	uint16_t HX8530_IC_TX_NUM[8];
	uint16_t HX8530_MAX_RX_NUM;
	uint16_t HX8530_MAX_TX_NUM;
	uint8_t HX8530_IC_order;
	uint8_t HX8530_is_distributed;
};

struct himax_target_report_data {
	uint32_t *x;
	uint32_t *y;
	uint32_t *w;
	int *finger_id;
	uint8_t finger_on;
	uint8_t finger_num;
	int *fpt_cnt;
#if (HX_SMART_WAKEUP == 0x01)
	int SMWP_event_chk;
#endif
};

struct himax_report_data {
	uint8_t touch_all_size;
	uint8_t raw_cnt_max;
	uint8_t raw_cnt_rmd;
	uint8_t touch_info_size;
	uint8_t	finger_on;
	uint8_t *hx_coord_buf;
	uint8_t hx_state_info[2];
#if (HX_SMART_WAKEUP == 0x01)
	uint8_t event_size;
	uint8_t *hx_event_buf;
#endif
	uint32_t rawdata_size;
	uint8_t diag_cmd;
	uint8_t *hx_rawdata_buf;
	uint8_t rawdata_frame_size;
};

struct himax_ts_data {
	bool initialized;
	bool suspended;
	bool chip_test_mpap_flag;
	atomic_t suspend_mode;
	uint8_t useScreenRes;
	uint8_t diag_cmd;
	char chip_name[30];
	uint8_t protocol_type;
	uint8_t first_pressed;
	uint8_t nFinger_support;
	bool irq_enabled;
	uint8_t panel_id;
	uint16_t last_slot;
	uint16_t pre_finger_mask;
	uint16_t old_finger;
	uint8_t hx_point_num;

	uint32_t debug_log_level;
	uint32_t widthFactor;
	uint32_t heightFactor;

	int lcm_gpio;
	int rst_gpio;
	int pon_gpio;
	bool use_irq;
	int (*power)(int on);
	uint32_t pre_finger_data[20][2];

	struct device *dev;
	struct workqueue_struct *himax_wq;
	struct work_struct work;
	struct input_dev *input_dev;

	struct hrtimer timer;
#if defined(HOT_PLUG)
	struct hrtimer himax_read_icid_timer;
#endif
	struct i2c_client *client;
	struct himax_i2c_platform_data *pdata;
	struct mutex rw_lock;
	atomic_t irq_state;
	spinlock_t irq_lock;

	atomic_t fail_det_state;
	spinlock_t fail_det_lock;

/******* SPI-start *******/
	struct spi_device	*spi;
	int hx_irq;
	uint8_t *xfer_buff;
/******* SPI-end *******/

	int hx_fail_det;

	int in_self_test;
	int suspend_resume_done;
	int bus_speed;

#if defined(HX_CONFIG_FB) || defined(HX_CONFIG_DRM)
	struct notifier_block fb_notif;
	struct workqueue_struct *himax_att_wq;
	struct delayed_work work_att;
#endif

	struct workqueue_struct *flash_wq;
	struct work_struct flash_work;

#if (HX_BOOT_UPGRADE == 0x01)
	struct workqueue_struct *himax_boot_upgrade_wq;
	struct delayed_work work_boot_upgrade;
#endif

	struct workqueue_struct *himax_diag_wq;
	struct delayed_work himax_diag_delay_wrok;
#if (HX_SMART_WAKEUP == 0x01)
	bool SMWP_enable;
	uint8_t gesture_cust_en[26];
	struct wakeup_source *ts_SMWP_wake_lock;
#endif


#if (HX_TP_GTS_MODE == 0x01)
	uint8_t GTS_range;
#endif

	char self_test_file_ch[30];
	uint8_t slave_ic_num;

#if defined(HOT_PLUG)
	struct workqueue_struct *himax_read_ic_id_wq;
	struct work_struct himax_work_read_icid;
#endif
	uint8_t LTDI_product;
	bool SID_Protocol;
	uint8_t spi_id;
	bool Partiton_A_CRC_OK;
	bool Partiton_B_CRC_OK;
};

#if defined(CONFIG_TOUCHSCREEN_HIMAX_OFR_SPI)
struct himax_OFR_ts_data {
	struct device *dev;
	struct input_dev *input_dev;
	struct mutex rw_lock;
	struct spi_device	*spi;
	uint8_t *xfer_buff;
	bool SID_Protocol;
	uint8_t spi_id;
};
#endif

struct himax_debug {
	void (*fp_ts_dbg_func)(struct himax_ts_data *ts, int start);
	int (*fp_set_diag_cmd)(struct himax_ic_data *ic_data_tmp,
				struct himax_report_data *hx_touch_data_tmp);
};

/*------------------------- define block -------------------------------------*/

#define	IC_MASTER		0U
#define	IC_SLAVE_1		1U
#define	IC_SLAVE_2		2U

#define	HX_REPORT_COORD			1
#define	HX_REPORT_SMWP_EVENT	2
#define	HX_REPORT_COORD_RAWDATA	3


#define	HX_TS_GET_DATA_FAIL		-4
#define	HX_EXCP_EVENT			-3
#define	HX_CHKSUM_FAIL			-2
#define	HX_PATH_FAIL			-1
#define	HX_TS_NORMAL_END		0
#define	HX_EXCP_REC_OK			1
#define	HX_READY_SERVE			2
#define	HX_REPORT_DATA			3
#define	HX_EXCP_WARNING			4
#define	HX_IC_RUNNING			5
#define	HX_ZERO_EVENT_COUNT		6
#define	HX_RST_OK				7

#define	PROTOCOL_TYPE_A			0x00
#define	PROTOCOL_TYPE_B			0x01

#if defined(HX_CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>
#elif defined(HX_CONFIG_DRM)
/*#include <linux/msm_drm_notify.h>*/
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0))
#define KERNEL_VER_ABOVE_4_14
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
#define KERNEL_VER_5_10
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 18, 0))
#define KERNEL_VER_5_18
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0))
#define KERNEL_VER_6_01
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
#define KERNEL_VER_6_06
#endif

/* WP GPIO setting, decided by which pin direct to OS side, WP need pin */
/* high either GPIO0 or GPIO4 */
/* #define WP_GPIO0 */
#define WP_GPIO4
/* #define FW_update_history_record */

#if defined(KERNEL_VER_5_10)
#define proc_op(name) proc_##name
#define proc_opl(name) proc_##name
#define proc_ops_name proc_ops
#define time_var timespec64
#define time_clock tm
#define time_func ktime_get_real_ts64
#define owner_line
#else
#define proc_op(name) name
#define proc_opl(name) l##name
#define proc_ops_name file_operations
#define time_var timespec
#define time_clock rtc_time
#define time_func getnstimeofday
#define owner_line .owner\
		=\
		THIS_MODULE,
#endif

#define addr_Tp_inspect_mode_cmd_current	0x10005B74U

#define HX_MAX_WRITE_SZ    ((64U * 1024U) + 4U)

#define	HX_83192D_PWON		"HX83192D"
#define	HX_83192A_PWON		"HX83192A"
#define	HX_83193A_PWON		"HX83193A"
#define	HX_83194A_PWON		"HX83194A"
#define	HX_83194B_PWON		"HX83194B"
#define	HX_83195A_PWON		"HX83195A"
#define	HX_83195B_PWON		"HX83195B"
#define	HX_83180A_PWON		"HX83180A"
#define	HX_83181A_PWON		"HX83181A"
#define	HX_8530_PWON		"HX8530"
#define	HIMAX_PROC_TOUCH_FOLDER		"android_touch"
#define	HIMAX_PROC_WP_BP_LOCK_FILE	"WPBPlock_node"
#define	HIMAX_PROC_FAIL_DET_FILE	"Fail_det_state"

#define OFR_FWNAME "Himax_OFR_firmware.bin"

#if defined(CONFIG_TOUCHSCREEN_HIMAX_SELF_TEST)
	#define HIMAX_PROC_SELF_TEST_FILE "self_test"
#endif

#if (HX_TP_GTS_MODE == 0x01)
	#define HIMAX_PROC_GTS_FILE "Ghost_touch_solution"
#endif

#if (HX_TP_INSPECT_MODE == 0x01)
	#define HIMAX_PROC_INSPECT_MODE_FILE "Inspect_mode"
#endif


#define SHIFTBITS			5
#define NO_ERR					0
#define HX_FAIL					-1
#define I2C_FAIL				-1
#define MEM_ALLOC_FAIL			-2
#define INPUT_REGISTER_FAIL		-3
#define FW_NOT_READY			4
#define LENGTH_FAIL				-5
#define OPEN_FILE_FAIL			-6
#define PROBE_FAIL				-7
#define ERR_TEST_FAIL			-9
#define HW_CRC_FAIL				1

#define HX_FINGER_ON			1
#define HX_FINGER_LEAVE			2

#define HX_LOCK					6
#define HX_UNLOCK				7

#define SIZE_128k			131072U
#define SIZE_192k			196608U
#define SIZE_255k			261120U

#if (HX_SMART_WAKEUP == 0x01)
	#define HIMAX_PROC_GESTURE_FILE	"GESTURE"
	#define GEST_PTLG_ID_LEN	(4U)
	#define GEST_PTLG_HDR_LEN	(4U)
	#define GEST_PTLG_HDR_ID1	(0xCCU)
	#define GEST_PTLG_HDR_ID2	(0x44U)
	#define GEST_PT_MAX_NUM		(128U)
	#define GEST_SUP_NUM		(2U)
	/* Setting cust key define (DF = double finger) */
	/* {Double Tap, Tap}
	 */
#endif

#define TS_WAKE_LOCK_TIMEOUT (5000)

#define TEMP_LOG "%s:%#010x data[0]=0x%02X, data[1]=0x%02X, data[2]=0x%02X, data[3]=0x%02X\n"

#if (HX_SMART_WAKEUP == 0x01)
#define HX_KEY_DOUBLE_CLICK                KEY_POWER
#define HX_KEY_SINGLE_CLICK                KEY_POWER
#endif

#define UNUSED(x)((void)(x))

/*------------------------- define block -------------------------------------*/
/*------------------------- parameter block ----------------------------------*/

extern bool g_hx_chip_inited;
extern uint8_t g_mmi_refcnt;
extern int g_ts_dbg;

#if (HX_SMART_WAKEUP == 0x01)
	extern uint8_t *wake_event_buffer;
#endif

#if defined(CONFIG_TOUCHSCREEN_HIMAX_SELF_TEST)
	extern char *g_rslt_data;
#endif

#if (HX_TP_INSPECT_MODE == 0x01)
	extern uint8_t inspect_mode_flag;
#endif

/*------------------------- parameter block ----------------------------------*/
/*------------------------- structure block ----------------------------------*/

extern struct himax_core_fp g_core_fp;
extern struct himax_ic_data *ic_data;
extern struct proc_dir_entry *himax_touch_proc_dir;
extern struct himax_target_report_data *g_target_report_data;
extern struct himax_report_data *hx_touch_data;
extern struct proc_dir_entry *himax_proc_INSPECT_MODE_file;
extern struct himax_debug *debug_data;
extern struct himax_ts_data *private_ts;
#if defined(CONFIG_TOUCHSCREEN_HIMAX_OFR_SPI)
extern struct himax_OFR_ts_data *private_OFR_ts;
#endif

#if defined(KERNEL_VER_5_10)
	extern struct timespec64 time_diff(struct timespec64 start, struct timespec64 end);
#else
	extern struct timespec time_diff(struct timespec start, struct timespec end);
#endif

/*------------------------- structure block ----------------------------------*/
/*------------------------- function block -----------------------------------*/

#if defined(HX_CONFIG_FB)
int fb_notifier_callback(struct notifier_block *self,
		unsigned long event, void *data);
#elif defined(HX_CONFIG_DRM)
int drm_notifier_callback(struct notifier_block *self,
			unsigned long event, void *data);
#endif

void himax_chip_common_suspend(struct himax_ts_data *ts);
void himax_chip_common_resume(struct himax_ts_data *ts);

#if defined(CONFIG_TOUCHSCREEN_HIMAX_DEBUG)
	int himax_debug_init(void);
	void himax_debug_remove(void);
#endif

int himax_parse_dt(struct himax_ts_data *ts,
			struct himax_i2c_platform_data *pdata);

int himax_dev_set(struct himax_ts_data *ts);
int himax_input_register_device(struct input_dev *input_dev);
void himax_chip_self_test(struct seq_file *s, void *v);
void himax_mcu_clear_event_stack(void);

void himax_report_data_deinit(void);
int himax_input_register(struct himax_ts_data *ts);
void himax_fail_det_work(void);
void himax_ts_work(struct himax_ts_data *ts);
enum hrtimer_restart himax_ts_timer_func(struct hrtimer *timer);
int himax_chip_common_init(void);
void himax_mcu_double_safe_mode(void);
void himax_mcu_reset_safe_mode(void);
#if defined(HOT_PLUG)
int himax_hotplug_common_init(void);
#endif
void himax_chip_common_deinit(void);
#if defined(FW_update_history_record)
	void himax_FW_update_record(void);
#endif

#if defined(__HIMAX_MOD__)
	extern int (*hx_msm_drm_register_client)(struct notifier_block *nb);
	extern int (*hx_msm_drm_unregister_client)(struct notifier_block *nb);
#endif

/*------------------------- function block -----------------------------------*/

#if (HX_FIX_TOUCH_INFO == 0x01)
struct fix_touch_info {
	uint8_t FIX_HX_RX_NUM;
	uint8_t FIX_HX_TX_NUM;
	uint8_t FIX_HX_MAX_PT;
	uint8_t FIX_HX_INT_IS_EDGE;
	uint8_t FIX_HX_IS_ID_EN;
	uint8_t FIX_HX_ID_PALM_EN;
	uint32_t FIX_FW_SIZE;
	uint8_t FIX_HX_RX_IC_NUM;
	uint8_t FIX_HX_TX_IC_NUM;
	uint8_t FIX_HX_CHIP_RX_MAX;
	uint8_t FIX_HX_CHIP_TX_MAX;
};
/* 	FIX_FW_SIZE: SIZE_255k/SIZE_128k/SIZE_192k */
static const struct fix_touch_info FIX_TOUCH_INFO_INIT = {
    .FIX_HX_RX_NUM = 74U,
    .FIX_HX_TX_NUM = 42U,
    .FIX_HX_MAX_PT = 10U,
    .FIX_HX_INT_IS_EDGE = 0U,
    .FIX_HX_IS_ID_EN = 1U,
    .FIX_HX_ID_PALM_EN = 0U,
    .FIX_FW_SIZE = SIZE_255k,
	.FIX_HX_RX_IC_NUM = 1U,
	.FIX_HX_TX_IC_NUM = 1U,
	.FIX_HX_CHIP_RX_MAX = 74U,
	.FIX_HX_CHIP_TX_MAX = 42U
};
extern const struct fix_touch_info *g_fix_info;
#endif

#if (HX_SRAM_TEST == 0x01)
	/* Change VDDD
	*0 = Data(0x18) = VDDD 1.05
	*1 = Data(0x19) = VDDD 1.1
	*2 = Data(0x1A) = VDDD 1.15
	*3 = Data(0x1B) = VDDD 1.2
	*4 = Data(0x1C) = VDDD 1.25
	*5 = Data(0x1D) = VDDD 1.3
	*6 = Data(0x1E) = VDDD 1.35
	*7 = Data(0x1F) = VDDD 1.4
	*/
struct sram_test_condition {
	uint8_t SRAM_1st_TEST_VOLTAGE;
	uint8_t SRAM_1st_TEST_LOOP;
	uint8_t SRAM_2nd_TEST_VOLTAGE;
	uint8_t SRAM_2nd_TEST_LOOP;
	bool SRAM_2nd_SAVEBACK;
	uint8_t SRAM_3rd_TEST_VOLTAGE;
	uint8_t SRAM_3rd_TEST_LOOP;
};

static const struct sram_test_condition default_sram_test_condition = {
    .SRAM_1st_TEST_VOLTAGE = 0x19U,
    .SRAM_1st_TEST_LOOP = 0x01U,
    .SRAM_2nd_TEST_VOLTAGE = 0x1BU,
    .SRAM_2nd_TEST_LOOP = 0x1EU,
    .SRAM_2nd_SAVEBACK = true,
    .SRAM_3rd_TEST_VOLTAGE = 0x1EU,
    .SRAM_3rd_TEST_LOOP = 0x05U
};
extern const struct sram_test_condition *g_sram_test;
extern const char *SRAM_FW_FILE_NAME;
#define SRAM_FW_FILE	"HX83192_SRAM_CHECK.bin"
#endif

#if (HX_BOOT_UPGRADE == 0x01)
/* FW Auto upgrade, you need to setup the fix_touch_info of module*/
	#define BOOT_UPGRADE_FWNAME "Himax_firmware.bin"
	#define BOOT_FORCE_UPGRADE_FWNAME "Himax_Force_Update_FW.bin"
	extern const char *UPGRADE_FWNAME;
	extern const char *FORCE_UPGRADE_FWNAME;
	#if (HX_VERIFY_TICKET_NUMBER == 0x01)
		#define HIMAX_TICKET_NUMBER "5637"
	#endif
#endif

#if (HX_PANEL_ID_CHECK == 0x01)
	#define	PANEL_UNKNOWN		0xFFU
	#define	PANEL_TIANMA		0x01U
	#define	PANEL_BOE			0x02U

	#if (HX_BOOT_UPGRADE == 0x01)
	#define TIANMA_BOOT_UPGRADE_FWNAME "For_TIANMA_firmware.bin";
	#define TIANMA_BOOT_FORCE_UPGRADE_FWNAME "Force_Update_TIANMA_firmware.bin";

	#define BOE_BOOT_UPGRADE_FWNAME "For_BOE_firmware.bin";
	#define BOE_BOOT_FORCE_UPGRADE_FWNAME "Force_Update_BOE_firmware.bin";
	#endif

	#if (HX_FIX_TOUCH_INFO == 0x01)
	static const struct fix_touch_info TIANMA_TOUCH_INFO_INIT = {
		.FIX_HX_RX_NUM = 48U,
		.FIX_HX_TX_NUM = 18U,
		.FIX_HX_MAX_PT = 4U,
		.FIX_HX_INT_IS_EDGE = 1U,
		.FIX_HX_IS_ID_EN = 1U,
		.FIX_HX_ID_PALM_EN = 1U,
		.FIX_FW_SIZE = SIZE_255k,
		.FIX_HX_RX_IC_NUM = 0U,
		.FIX_HX_TX_IC_NUM = 0U,
		.FIX_HX_CHIP_RX_MAX = 0U,
		.FIX_HX_CHIP_TX_MAX = 0U
	};

	static const struct fix_touch_info BOE_TOUCH_INFO_INIT = {
		.FIX_HX_RX_NUM = 33U,
		.FIX_HX_TX_NUM = 55U,
		.FIX_HX_MAX_PT = 10U,
		.FIX_HX_INT_IS_EDGE = 1U,
		.FIX_HX_IS_ID_EN = 1U,
		.FIX_HX_ID_PALM_EN = 0U,
		.FIX_FW_SIZE = SIZE_128k,
		.FIX_HX_RX_IC_NUM = 0U,
		.FIX_HX_TX_IC_NUM = 0U,
		.FIX_HX_CHIP_RX_MAX = 0U,
		.FIX_HX_CHIP_TX_MAX = 0U
	};
	#endif
	
	#if (HX_SRAM_TEST == 0x01)
	#define TIANMA_SRAM_FW_FILE	"TIANMA_SRAM_CHECK.bin"
	#define BOE_SRAM_FW_FILE	"BOE_SRAM_CHECK.bin"

	static const struct sram_test_condition TIANMA_sram_test_condition = {
		.SRAM_1st_TEST_VOLTAGE = 0x19U,
		.SRAM_1st_TEST_LOOP = 0x01U,
		.SRAM_2nd_TEST_VOLTAGE = 0x1BU,
		.SRAM_2nd_TEST_LOOP = 0x1EU,
		.SRAM_2nd_SAVEBACK = true,
		.SRAM_3rd_TEST_VOLTAGE = 0x1EU,
		.SRAM_3rd_TEST_LOOP = 0x05U
	};
	static const struct sram_test_condition BOE_sram_test_condition = {
		.SRAM_1st_TEST_VOLTAGE = 0x19U,
		.SRAM_1st_TEST_LOOP = 0x01U,
		.SRAM_2nd_TEST_VOLTAGE = 0x1BU,
		.SRAM_2nd_TEST_LOOP = 0x1EU,
		.SRAM_2nd_SAVEBACK = true,
		.SRAM_3rd_TEST_VOLTAGE = 0x1EU,
		.SRAM_3rd_TEST_LOOP = 0x05U
	};
	#endif

#endif

#endif
