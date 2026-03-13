// SPDX-License-Identifier: GPL-2.0
/*  Himax Android Driver Sample Code for common functions
 *
 *  Copyright (C) 2024 Himax Corporation.
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

/*#include "himax_common.h"*/
/*#include "himax_ic_core.h"*/
#include "himax_self_test.h"

/*------------------------- define block -------------------------------------*/
/*------------------------- define block -------------------------------------*/
/*------------------------- parameter block ----------------------------------*/

#if (HX_SMART_WAKEUP == 0x01) || (HX_TP_GTS_MODE == 0x01)
	static bool HX_PROC_SEND_FLAG;
#endif

static uint8_t HX_TOUCH_INFO_POINT_CNT;
static uint8_t AA_press;
static int probe_fail_flag;

#if (HX_GESTURE_TRACK == 0x01)
	static uint16_t gest_pt_cnt;
	static uint16_t gest_pt_x[GEST_PT_MAX_NUM];
	static uint16_t gest_pt_y[GEST_PT_MAX_NUM];
	static uint16_t gest_start_x, gest_start_y, gest_end_x, gest_end_y;
	static uint16_t gest_width, gest_height;
	static uint16_t gest_mid_x, gest_mid_y;
	static uint16_t hx_gesture_coor[16];
#endif

#if defined(CONFIG_TOUCHSCREEN_HIMAX_SELF_TEST)
	static bool chip_test_r_flag;
#endif

bool		g_hx_chip_inited;
EXPORT_SYMBOL(g_hx_chip_inited);

uint8_t		g_mmi_refcnt;
EXPORT_SYMBOL(g_mmi_refcnt);

int			g_ts_dbg;
EXPORT_SYMBOL(g_ts_dbg);

#if (HX_BOOT_UPGRADE == 0x01)
	bool g_boot_upgrade_flag;
	const struct firmware *hxfw;
	uint16_t g_i_FW_VER;
	uint16_t g_i_CFG_VER;
	uint8_t g_i_CID_MAJ; /*GUEST ID*/
	uint8_t g_i_CID_MIN; /*VER for GUEST*/
#endif

#if (HX_EXCP_RECOVERY == 0x01)
	bool HX_EXCP_RESET_ACTIVATE;
	int g_zero_event_count;
#endif

/* All Zero event recovery — active regardless of HX_EXCP_RECOVERY.
 * Counts consecutive all-zero IRQ events and triggers a reset after
 * the threshold to break interrupt floods caused by FPDLink latches.
 */
static int hx_all_zero_count;
#define HX_ALL_ZERO_RESET_THRESHOLD 5

#if (HX_TP_INSPECT_MODE == 0x01)
	uint8_t inspect_mode_flag;
#endif

#if (HX_SMART_WAKEUP == 0x01)
	uint8_t gest_event[GEST_SUP_NUM] = { 0x80, 0x90 };
	/*gest_event mapping to gest_key_def*/
	uint16_t gest_key_def[GEST_SUP_NUM] = { HX_KEY_DOUBLE_CLICK,
						HX_KEY_SINGLE_CLICK };
	uint8_t *wake_event_buffer;
#endif

/*------------------------- parameter block ----------------------------------*/
/*------------------------- structure block ----------------------------------*/
#if (HX_TP_INSPECT_MODE == 0x01)
	struct proc_dir_entry *himax_proc_INSPECT_MODE_file;
#endif

#if defined(CONFIG_TOUCHSCREEN_HIMAX_SELF_TEST)
	static struct proc_dir_entry *himax_proc_self_test_file;
#endif

#if (HX_TP_GTS_MODE == 0x01)
	struct proc_dir_entry *himax_proc_GTS_file;
#endif

#if (HX_SMART_WAKEUP == 0x01)
	static struct proc_dir_entry *himax_proc_GESTURE_file;
#endif

struct himax_ts_data *private_ts;
EXPORT_SYMBOL(private_ts);

struct himax_ic_data *ic_data;
EXPORT_SYMBOL(ic_data);

struct himax_report_data *hx_touch_data;
EXPORT_SYMBOL(hx_touch_data);

struct himax_debug *debug_data;
EXPORT_SYMBOL(debug_data);

struct proc_dir_entry *himax_touch_proc_dir;
EXPORT_SYMBOL(himax_touch_proc_dir);

struct himax_target_report_data *g_target_report_data;
EXPORT_SYMBOL(g_target_report_data);

struct himax_core_fp g_core_fp;
EXPORT_SYMBOL(g_core_fp);
#if (HX_TP_GTS_MODE == 0x01)
static struct himax_target_report_data *fixed_point_label;
#endif
static struct proc_dir_entry *himax_proc_WPBPlock_node_file;
static struct proc_dir_entry *himax_proc_fail_det_file;

/*------------------------- structure block ----------------------------------*/

#if defined(KERNEL_VER_5_10)
struct timespec64 time_diff(struct timespec64 start, struct timespec64 end)
{
	struct timespec64 delta;
#else
struct timespec time_diff(struct timespec start, struct timespec end)
{
	struct timespec delta;
#endif
	if ((end.tv_nsec - start.tv_nsec) < 0) {
		delta.tv_sec = end.tv_sec - start.tv_sec - 1;
		delta.tv_nsec = 1000000000 + end.tv_nsec - start.tv_nsec;
	} else {
		delta.tv_sec = end.tv_sec - start.tv_sec;
		delta.tv_nsec = end.tv_nsec - start.tv_nsec;
	}
	return delta;
}
#if defined(CONFIG_TOUCHSCREEN_HIMAX_SELF_TEST)
ssize_t himax_self_test(struct seq_file *s, void *v)
{
	ssize_t ret = 0;

	I("%s: enter, %d\n", __func__, __LINE__);

	if (private_ts->suspended == 1) {
		E("%s: please do self test in normal active mode\n", __func__);
		return HX_FAIL;
	}
	himax_int_enable(0); /* disable irq */

	private_ts->in_self_test = 1;

	himax_chip_self_test(s, v);

	private_ts->in_self_test = 0;

#if (HX_EXCP_RECOVERY == 0x01)
	HX_EXCP_RESET_ACTIVATE = true;
#endif
	himax_int_enable(1);

	return ret;
}

int himax_self_test_show(struct seq_file *s, void *v)
{
	ssize_t ret = 0;

	if (chip_test_r_flag) {
		if (g_rslt_data != NULL) {
			seq_printf(s, "%s", g_rslt_data);
		} else {
			seq_puts(s, "No chip test data.\n");
		}
	} else {
		ret = himax_self_test(s, v);
	}

	return ret;
}

ssize_t himax_self_test_store(struct file *filp,
				      const char __user *buff, size_t len,
				      loff_t *data)
{
	char buf[80];
	unsigned int i;
	bool flag = false;

	UNUSED(filp);
	UNUSED(data);

	if (len >= 80U) {
		I("%s: no command exceeds 80 chars.\n", __func__);
		return -EFAULT;
	}

	if (copy_from_user(buf, buff, len) != 0) {
		return -EFAULT;
	}

	if (buf[0] == 'r') {
		chip_test_r_flag = true;
		I("%s: Start to read chip test data.\n", __func__);
	} else {
		chip_test_r_flag = false;
		I("%s: Back to do self test.\n", __func__);
	}


	if (len > 4U) {

		for (i = 2; i < len; i++) {
			if ((buf[i - 2U] == 'c') && (buf[i - 1U] == 's') && (buf[i] == 'v')) {
				flag = true;
			}
		}
		if (flag) {
			(void)memset(private_ts->self_test_file_ch, 0, sizeof(private_ts->self_test_file_ch));
			(void)memcpy(private_ts->self_test_file_ch, buf, len - 1U);	/* remove last eol */
			I("%s: Start to set self test file as name |%s|.\n", __func__, private_ts->self_test_file_ch);
		}
	}

	if ((buf[0] == 'M') && (buf[1] == 'P') && (buf[2] == 'A') && (buf[3] == 'P')
	&& (buf[4] == '_') && ((buf[5] == 'E') || (buf[5] == 'e'))) {
		I("%s: MPAP test enable.\n", __func__);
		private_ts->chip_test_mpap_flag = true;
	}
	if ((buf[0] == 'M') && (buf[1] == 'P') && (buf[2] == 'A') && (buf[3] == 'P')
	&& (buf[4] == '_') && ((buf[5] == 'D') || (buf[5] == 'd'))) {
		I("%s: MPAP test disable.\n", __func__);
		private_ts->chip_test_mpap_flag = false;
	}


	return len;
}
int himax_self_test_open(struct inode *inode, struct file *file)
{
	UNUSED(inode);
	return single_open(file, himax_self_test_show, NULL);
}

const struct proc_ops_name himax_self_test_ops = {
	owner_line
	.proc_op(open) = himax_self_test_open,
	.proc_op(write) = himax_self_test_store,
	.proc_op(read) = seq_read,
	.proc_opl(lseek) = seq_lseek,
	.proc_op(release) = single_release,
};
#endif

static int himax_WPBPlock_node_show(struct seq_file *s, void *v)
{
	ssize_t ret = 0;
	int status;

	UNUSED(v);
	status = himax_mcu_WP_BP_status();

	if (status == HX_LOCK) {
		seq_puts(s, "WP BP value & lock status is lock\n");
	} else if (status == HX_UNLOCK) {
		seq_puts(s, "WP BP lock status is unlock\n");
	} else {
		seq_puts(s, "Can't recognize flash id\n");
	}

	return ret;
}

ssize_t himax_WPBPlock_node_store(struct file *filp,
				      const char __user *buff, size_t len,
				      loff_t *data)
{
	char buf[80];
	ssize_t result = 0;

	UNUSED(filp);
	UNUSED(data);

	if (len >= 80U) {
		I("%s: no command exceeds 80 chars.\n", __func__);
		result = -EFAULT;
	} else if (copy_from_user(buf, buff, len) != 0) {
		result = -EFAULT;
	} else if ((buf[0] == 'd') || (buf[0] == 'D')) {
		I("%s: Start to disable BP lock.\n", __func__);
		himax_disable_flash_protected_mode();
		result = (ssize_t) len;
	} else if ((buf[0] == 'e') || (buf[0] == 'E')) {
#if (HX_WPBP_ENABLE == 0x01)
		I("%s: Start to enable BP lock.\n", __func__);
		himax_enable_flash_protected_mode();
#else
		W("%s: Not Open define HX_WPBP_ENABLE.\n", __func__);
#endif

		result = (ssize_t) len;
	} else {
		I("%s: Input cmd is incorrect!\n", __func__);
		result = -EFAULT;
	}

	return result;
}

int himax_WPBPlock_node_open(struct inode *inode, struct file *file)
{
	UNUSED(inode);
	return single_open(file, himax_WPBPlock_node_show, NULL);
}

const struct proc_ops_name himax_WPBPlock_node_ops = {
	owner_line
	.proc_op(open) = himax_WPBPlock_node_open,
	.proc_op(write) = himax_WPBPlock_node_store,
	.proc_op(read) = seq_read,
	.proc_opl(lseek) = seq_lseek,
	.proc_op(release) = single_release,
};

#define PA_LOG_1                                                               \
	"%s:para[1]=0x%02X, para[2]=0x%02X, para[3]=0x%02X, para[4]=0x%02X\n"
#define PA_LOG_2                                                               \
	"%s:para[5]=0x%02X, para[6]=0x%02X, para[7]=0x%02X\n"
#define PA_LOG_3                                                               \
	"%s:para[5]=0x%02X, para[6]=0x%02X, para[7]=0x%02X, para[8]=0x%02X\n"
#define PA_LOG_4                                                               \
	"%s:para[9]=0x%02X, para[10]=0x%02X, para[11]=0x%02X, para[12]=0x%02X\n"
#define PA_LOG_5                                                               \
	"%s:para[13]=0x%02X, para[14]=0x%02X, para[15]=0x%02X, para[16]=0x%02X\n"
#define PA_LOG_6                                                               \
	"%s:para[17]=0x%02X, para[18]=0x%02X, para[19]=0x%02X, para[20]=0x%02X\n"
#define PA_LOG_7                                                               \
	"%s:para[21]=0x%02X\n"
#define DLOG_1                                                               \
	"0x%08X:data[0]=0x%02X, data[1]=0x%02X, data[2]=0x%02X, data[3]=0x%02X\n"
#define SLOG_1                                                               \
	"%s:data[0]=0x%02X, data[1]=0x%02X, data[2]=0x%02X, data[3]=0x%02X\n"
int himax_fail_det_show(struct seq_file *s, void *v)
{
	ssize_t ret = 0;
	uint8_t data[30] = { 0 };
#if defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83195) || defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83194)
	uint8_t cnt = 0U;
	uint32_t addr_idx = addr_fail_det_MainItem_DD_Master;
#endif
	UNUSED(v);

	himax_mcu_write_dd_reg_password(IC_MASTER);

#if defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83195) || defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83194)
	himax_mcu_dd_reg_read(0xE5, 0, 8, data, 0, IC_MASTER);
	seq_printf(s, PA_LOG_1, "E5_Bank0:", data[1], data[2], data[3], data[4]);
	himax_mcu_register_read(addr_fail_det_MainItem_checksum, DATA_LEN_4, data);
	seq_printf(s, SLOG_1, "MainItem_checksum:", data[0], data[1], data[2], data[3]);
	himax_mcu_register_read(addr_fail_det_IC_summary_status, DATA_LEN_4, data);
	seq_printf(s, SLOG_1, "IC_summary_status:", data[0], data[1], data[2], data[3]);
	do {
		himax_mcu_register_read(addr_idx , DATA_LEN_4, data);
		seq_printf(s, DLOG_1, addr_idx, data[0], data[1], data[2], data[3]);
		addr_idx += 4U;

		himax_mcu_register_read(addr_idx , DATA_LEN_4, data);
		seq_printf(s, DLOG_1, addr_idx, data[0], data[1], data[2], data[3]);
		addr_idx += 4U;
	
		himax_mcu_register_read(addr_idx , DATA_LEN_4, data);
		seq_printf(s, DLOG_1, addr_idx, data[0], data[1], data[2], data[3]);
		addr_idx += 4U;

		cnt++;
	} while (cnt <= private_ts->slave_ic_num);

	cnt = 0U;
	addr_idx = addr_fail_det_SubItem_DD_Master;
	himax_mcu_register_read(addr_fail_det_SubItem_checksum, DATA_LEN_4, data);
	seq_printf(s, SLOG_1, "SubItem_checksum:", data[0], data[1], data[2], data[3]);

	do {
		himax_mcu_register_read(addr_idx , DATA_LEN_4, data);
		seq_printf(s, DLOG_1, addr_idx, data[0], data[1], data[2], data[3]);
		addr_idx += 4U;

		himax_mcu_register_read(addr_idx , DATA_LEN_4, data);
		seq_printf(s, DLOG_1, addr_idx, data[0], data[1], data[2], data[3]);
		addr_idx += 4U;
	
		himax_mcu_register_read(addr_idx , DATA_LEN_4, data);
		seq_printf(s, DLOG_1, addr_idx, data[0], data[1], data[2], data[3]);
		addr_idx += 4U;

		himax_mcu_register_read(addr_idx , DATA_LEN_4, data);
		seq_printf(s, DLOG_1, addr_idx, data[0], data[1], data[2], data[3]);
		addr_idx += 4U;

		himax_mcu_register_read(addr_idx , DATA_LEN_4, data);
		seq_printf(s, DLOG_1, addr_idx, data[0], data[1], data[2], data[3]);
		addr_idx += 4U;
	
		himax_mcu_register_read(addr_idx , DATA_LEN_4, data);
		seq_printf(s, DLOG_1, addr_idx, data[0], data[1], data[2], data[3]);
		addr_idx += 4U;

		cnt++;
	} while (cnt <= private_ts->slave_ic_num);

#else
	himax_mcu_dd_reg_read(0xE5, 0, 8, data, 0, IC_MASTER);
	seq_printf(s, PA_LOG_1, "E5_Bank0:", data[1], data[2], data[3], data[4]);
	seq_printf(s, PA_LOG_2, "E5_Bank0:", data[5], data[6], data[7]);

	himax_mcu_dd_reg_read(0xE5, 0, 8, data, 4, IC_MASTER);
	seq_printf(s, PA_LOG_1, "E5_Bank1:", data[1], data[2], data[3], data[4]);
	seq_printf(s, PA_LOG_2, "E5_Bank1:", data[5], data[6], data[7]);

	himax_mcu_dd_reg_read(0xE5, 0, 8, data, 12, IC_MASTER);
	seq_printf(s, PA_LOG_1, "E5_Bank3:", data[1], data[2], data[3], data[4]);
	seq_printf(s, PA_LOG_2, "E5_Bank3:", data[5], data[6], data[7]);

	himax_mcu_register_read(addr_fail_det_GPIO1_msg, DATA_LEN_4, data);
	I(TEMP_LOG, __func__, "0x100074C0", data[0], data[1], data[2], data[3]);
	seq_printf(s, TEMP_LOG, __func__, "0x100074C0",
		data[0], data[1], data[2], data[3]);
#endif

	himax_mcu_clear_dd_reg_password(IC_MASTER);

	return ret;
}

ssize_t himax_fail_det_store(struct file *filp,
				      const char __user *buff, size_t len,
				      loff_t *data)
{
	/*not implement write function*/
	char buf[80];
	uint8_t tmp_data[4] = { 0 };
	ssize_t result = 0;

	UNUSED(filp);
	UNUSED(data);

	if (len >= 80U) {
		I("%s: no command exceeds 80 chars.\n", __func__);
		result = -EFAULT;
	} else if (copy_from_user(buf, buff, len) != 0) {
		result = -EFAULT;
	} else if ((buf[0] == 'c') || (buf[0] == 'C')) {
		I("%s: now clear E5 Bank3 value.\n", __func__);
		himax_mcu_write_dd_reg_password(IC_MASTER);

		himax_mcu_dd_reg_read(0xE5, 0, 4, tmp_data, 0, IC_MASTER);
		I("0xE5 Bank0 pa1: data[0] = 0x%2.2x, data[1] = 0x%2.2x, data[2] = 0x%2.2x, data[3] = 0x%2.2x.\n",
		  tmp_data[0], tmp_data[1], tmp_data[2], tmp_data[3]);

		tmp_data[0] = 0x03;
		himax_mcu_dd_reg_write(0xE5, 1, 1, tmp_data, 0, IC_MASTER);

		tmp_data[0] = 0x00;
		himax_mcu_dd_reg_read(0xE5, 0, 4, tmp_data, 0, IC_MASTER);
		I("0xE5 Bank0 pa1: data[0] = 0x%2.2x, data[1] = 0x%2.2x, data[2] = 0x%2.2x, data[3] = 0x%2.2x.\n",
		  tmp_data[0], tmp_data[1], tmp_data[2], tmp_data[3]);

		himax_mcu_clear_dd_reg_password(IC_MASTER);
		result = (int) len;
	} else {
		/* do nothing*/
	}

	return result;
}

int himax_fail_det_open(struct inode *inode, struct file *file)
{
	UNUSED(inode);
	return single_open(file, himax_fail_det_show, NULL);
}

const struct proc_ops_name himax_fail_det_ops = {
	owner_line
	.proc_op(open) = himax_fail_det_open,
	.proc_op(write) = himax_fail_det_store,
	.proc_op(read) = seq_read,
	.proc_opl(lseek) = seq_lseek,
	.proc_op(release) = single_release,
};

#if (HX_TP_INSPECT_MODE == 0x01)
static void himax_chip_inspect_mode(struct seq_file *s, void *v)
{
	uint32_t tmp_addr_32 = 0;
	uint32_t addr_stack_depth = 0x8006000CU;
	uint8_t tmp_data[DATA_LEN_4] = { 0 };
	uint8_t buf[64] = { 0 };
	uint16_t cnt = 0;
	bool is_done = false;
	uint16_t tmp_value = 0;
	int16_t Min_value = 0;
	uint16_t Max_value = 0;
	uint16_t Short_lower_bound = 0;
	uint16_t Short_upper_bound = 0;
	uint16_t Open_lower_bound = 0;
	uint16_t Open_upper_bound = 0;
	uint16_t Noise_lower_bound = 0;
	uint16_t Noise_upper_bound = 0;
	struct time_var timeStart;
	struct time_var timeEnd;
	struct time_var timeDelta;
	int ret = NO_ERR;

	UNUSED(v);

	switch (inspect_mode_flag) {
	case 0x01: /*Start to do Short_test*/
		tmp_data[3] = 0x5A;
		tmp_data[2] = 0x00;
		tmp_data[1] = 0x01;
		tmp_data[0] = 0xA5;
		seq_puts(s, "Item : Short_test.\n");
		break;
	case 0x02: /*Start to do Open_test*/
		tmp_data[3] = 0x5A;
		tmp_data[2] = 0x00;
		tmp_data[1] = 0x02;
		tmp_data[0] = 0xA4;
		seq_puts(s, "Item : Open_test.\n");
		break;
	case 0x08: /*Start to do Noise_test*/
		tmp_data[3] = 0x5A;
		tmp_data[2] = 0x00;
		tmp_data[1] = 0x08;
		tmp_data[0] = 0x9E;
		seq_puts(s, "Item : Noise_test.\n");
		break;
	case 0x0B: /*Start to do All_test*/
		tmp_data[3] = 0x5A;
		tmp_data[2] = 0x00;
		tmp_data[1] = 0x0B;
		tmp_data[0] = 0x9B;
		seq_puts(s, "Item : All_test.\n");
		break;
	default:
		seq_puts(s, "Input cmd is incorrect.\n");
		ret = HX_FAIL;
		break;
	}
	time_func(&timeStart);

	if (ret == NO_ERR) {
		himax_mcu_config_reload_disable();
		himax_set_BS_UDT_frame(HX_INSPECT_MODE);

		usleep_range(20000, 21000);

		himax_mcu_register_write(addr_inspect_mode, DATA_LEN_4, tmp_data);
		himax_mcu_register_read(addr_inspect_mode, DATA_LEN_4, tmp_data);

		I("Now register =0x%02X, 0x%02X, 0x%02X, 0x%02X\n", tmp_data[3],
		  tmp_data[2], tmp_data[1], tmp_data[0]);

		cnt = 0;

		while (cnt < 100U) {
			himax_mcu_register_read(addr_inspect_mode, DATA_LEN_4, tmp_data);
			usleep_range(20000, 21000);
			I("%s : Current Status = 0x%02X\n", __func__, tmp_data[3]);
			if (tmp_data[3] == 0xA5U) {
				break;
			}
			cnt += 1U;
		}
		I("%s : Waiting for Inspect mode , loop conut = %d\n", __func__, cnt);
		if (tmp_data[3] != 0xA5U) {
			I("%s : Fail Status = 0x%02X, 0x%02X, 0x%02X, 0x%02X\n", __func__,
			tmp_data[3], tmp_data[2], tmp_data[1], tmp_data[0]);
			seq_puts(s, "FW not support Inspect mode.\n");
			ret = HX_FAIL;
		}
	}

	if (ret == NO_ERR) {
		cnt = 0;

		while (cnt < 600U) {
			usleep_range(20000, 21000);
			himax_mcu_register_read(addr_stack_depth, DATA_LEN_4, tmp_data);
			if (tmp_data[0] == 0x28U) {
				is_done = true;
				break;
			}
			cnt += 1U;
		}
		I("%s : stack depth 0x%02X , loop conut %d\n", __func__, tmp_data[0],
		  cnt);
		himax_mcu_config_reload_enable();

		is_done = himax_mcu_read_event_stack(buf, 40);

		I("%s : %s = 0x%02X, 0x%02X, 0x%02X, 0x%02X , loop conut %d\n",
		  __func__, "Stack result", buf[0], buf[1], buf[2], buf[3], cnt);

		time_func(&timeEnd);
		timeDelta = time_diff(timeStart, timeEnd);
#if defined(KERNEL_VER_5_10)
		seq_printf(s, "\tDuration :  %lld.%ld s\n", timeDelta.tv_sec, timeDelta.tv_nsec);
#else
		seq_printf(s, "\tDuration :  %ld.%ld s\n", timeDelta.tv_sec, timeDelta.tv_nsec);
#endif
	}

	if (is_done) {
		tmp_addr_32 = 0x100074A0U;
		himax_mcu_register_read(tmp_addr_32, DATA_LEN_4, tmp_data);
		Short_lower_bound = tmp_data[1];
		Short_lower_bound = (Short_lower_bound << 8U) + tmp_data[0];
		Short_upper_bound = tmp_data[3];
		Short_upper_bound = (Short_upper_bound << 8U) + tmp_data[2];
		tmp_addr_32 = 0x100074A4U;
		himax_mcu_register_read(tmp_addr_32, DATA_LEN_4, tmp_data);
		Open_lower_bound = tmp_data[1];
		Open_lower_bound = (Open_lower_bound << 8U) + tmp_data[0];
		Open_upper_bound = tmp_data[3];
		Open_upper_bound = (Open_upper_bound << 8U) + tmp_data[2];
		tmp_addr_32 = 0x100074ACU;
		himax_mcu_register_read(tmp_addr_32, DATA_LEN_4, tmp_data);
		Noise_lower_bound = tmp_data[1];
		Noise_lower_bound = (Noise_lower_bound << 8U) + tmp_data[0];
		Noise_upper_bound = tmp_data[3];
		Noise_upper_bound = (Noise_upper_bound << 8U) + tmp_data[2];

		if (buf[3] == 0x01U) {
			switch (inspect_mode_flag) {
			case 0x01:
				seq_printf(s, "\tShort Test : %s\n",
					   ((buf[7] & 0x01U) == 0x01U) ? "Fail" :
									     "Pass");
				tmp_value = buf[14];
				tmp_value = (tmp_value << 8U) + buf[15];
				Min_value = (int16_t)tmp_value;
				Max_value = buf[12];
				Max_value = (Max_value << 8U) + buf[13];
				seq_printf(s, "\t%s = %5d / %5d\n",
					   "Short Max. / Min.",
					   Max_value,
					   Min_value);
				seq_printf(s, "\t%s = %5d / %5d\n",
					   "Short Upper. / Lower. bound",
					   Short_upper_bound,
					   Short_lower_bound);
				break;
			case 0x02:
				seq_printf(s, "\tOpen  Test : %s\n",
					   ((buf[7] & 0x02U) == 0x02U) ? "Fail" :
									     "Pass");
				tmp_value = buf[18];
				tmp_value = (tmp_value << 8U) + buf[19];
				Min_value = (int16_t)tmp_value;
				Max_value = buf[16];
				Max_value = (Max_value << 8U) + buf[17];
				seq_printf(s, "\t%s = %5d / %5d\n",
					   "Open  Max. / Min.",
					   Max_value,
					   Min_value);
				seq_printf(s, "\t%s = %5d / %5d\n",
					   "Open  Upper. / Lower. bound",
					   Open_upper_bound,
					   Open_lower_bound);
				break;
			case 0x08:
				seq_printf(s, "\tNoise Test : %s\n",
					   ((buf[7] & 0x08U) == 0x08U) ? "Fail" :
									     "Pass");
				tmp_value = buf[26];
				tmp_value = (tmp_value << 8U) + buf[27];
				Min_value = (int16_t)tmp_value;
				Max_value = buf[24];
				Max_value = (Max_value << 8U) + buf[25];
				seq_printf(s, "\t%s = %5d / %5d\n",
					   "Noise Max. / Min.",
					   Max_value,
					   Min_value);
				seq_printf(s, "\t%s = %5d / %5d\n",
					   "Noise Upper. / Lower. bound",
					   Noise_upper_bound,
					   Noise_lower_bound);
				break;
			case 0x0B: /*Start to do All_test*/
				seq_printf(s, "\tShort Test : %s\n",
					   ((buf[7] & 0x01U) == 0x01U) ? "Fail" :
									     "Pass");
				seq_printf(s, "\tOpen  Test : %s\n",
					   ((buf[7] & 0x02U) == 0x02U) ? "Fail" :
									     "Pass");
				seq_printf(s, "\tNoise Test : %s\n",
					   ((buf[7] & 0x08U) == 0x08U) ? "Fail" :
									     "Pass");
				tmp_value = buf[14];
				tmp_value = (tmp_value << 8U) + buf[15];
				Min_value = (int16_t)tmp_value;
				Max_value = buf[12];
				Max_value = (Max_value << 8U) + buf[13];
				seq_printf(s, "\t%s = %5d / %5d\n",
					   "Short Max. / Min.",
					   Max_value,
					   Min_value);
				tmp_value = buf[18];
				tmp_value = (tmp_value << 8U) + buf[19];
				Min_value = (int16_t)tmp_value;
				Max_value = buf[16];
				Max_value = (Max_value << 8U) + buf[17];
				seq_printf(s, "\t%s = %5d / %5d\n",
					   "Open  Max. / Min.",
					   Max_value,
					   Min_value);
				tmp_value = buf[26];
				tmp_value = (tmp_value << 8U) + buf[27];
				Min_value = (int16_t)tmp_value;
				Max_value = buf[24];
				Max_value = (Max_value << 8U) + buf[25];
				seq_printf(s, "\t%s = %5d / %5d\n",
					   "Noise Max. / Min.",
					   Max_value,
					   Min_value);

				seq_printf(s, "\t%s = %5d / %5d\n",
					   "Short Upper. / Lower. bound",
					   Short_upper_bound,
					   Short_lower_bound);
				seq_printf(s, "\t%s = %5d / %5d\n",
					   "Open  Upper. / Lower. bound",
					   Open_upper_bound,
					   Open_lower_bound);
				seq_printf(s, "\t%s = %5d / %5d\n",
					   "Noise Upper. / Lower. bound",
					   Noise_upper_bound,
					   Noise_lower_bound);
				break;
			default:
				seq_puts(s, "Input cmd is incorrect.\n");
				break;
			}
		} else if (buf[3] == 0x10U) {
			seq_printf(s, "%s\n", "Self Test command error.");
		} else if (buf[3] == 0x20U) {
			seq_printf(s, "%s\n", "Self Test command CRC error.");
		} else {
			/* do nothing*/
		}

		I("%s = 0x%02X, 0x%02X, 0x%02X, 0x%02X\n", "Test Result",
		  buf[4], buf[5], buf[6], buf[7]);
		I("%s = 0x%02X, 0x%02X, 0x%02X, 0x%02X\n", "Result Information",
		  buf[8], buf[9], buf[10], buf[11]);

	} else {
		seq_puts(
			s,
			"[ERROR] Inspect Mode Suspended! Please check if FW support it!\n");
	}

}

static ssize_t himax_inspect_mode(struct seq_file *s, void *v)
{
	ssize_t ret = 0;

	I("%s: enter, %d\n", __func__, __LINE__);

	if (private_ts->suspended == 1) {
		E("%s: please do self test in normal active mode\n", __func__);
		ret = HX_FAIL;
	} else if (inspect_mode_flag == 0x0FU) { /*User Call HELP!*/
		seq_puts(s, "\n");
		seq_printf(s, "@ Short Test : %s\n",
			   "#echo Short > Inspect_mode ; #cat Inspect_mode");
		seq_printf(s, "@ Open  Test : %s\n",
			   "#echo Open  > Inspect_mode ; #cat Inspect_mode");
		seq_printf(s, "@ Noise Test : %s\n",
			   "#echo Noise > Inspect_mode ; #cat Inspect_mode");
		seq_printf(s, "@ All Test   : %s\n",
			   "#echo All   > Inspect_mode ; #cat Inspect_mode");
		seq_puts(s, "\n");
		seq_puts(
			s,
			"@ Set Inspect_mode Threshold by writing register :\n");
		seq_puts(
			s,
			"	Address    |  Threshold Item |    [31:16]    |    [15:0]\n");
		seq_puts(
			s,
			"	0x100074A0 |  Short Test     | High Boundary | Low Boundary\n");
		seq_puts(
			s,
			"	0x100074A4 |  Open Test      | High Boundary | Low Boundary\n");
		seq_puts(
			s,
			"	0x100074AC |  Noise Test     | High Boundary | Low Boundary\n");
		seq_puts(
			s,
			"	0x100074B0 |  Raw data       | High Boundary | Low Boundary\n");
		seq_puts(s, "\n");
		seq_puts(
			s,
			"Example : To set High Boundary = 0xFF ; Low Boundary = 0x11\n");
		seq_puts(s, "#echo register,w:x100074A4:x00FF0011 > debug\n");
		seq_puts(s, "\n");
	} else {
		himax_int_enable(0); /* disable irq */
		himax_chip_inspect_mode(s, v);
		himax_int_enable(1);
	}

	return ret;
}

int himax_inspect_mode_show(struct seq_file *s, void *v)
{
	ssize_t ret = 0;

	ret = himax_inspect_mode(s, v);

	return ret;
}

ssize_t himax_inspect_mode_store(struct file *filp,
				      const char __user *buff, size_t len,
				      loff_t *data)
{
	char buf[80];
	size_t len_tmp = len;
	ssize_t ret = (ssize_t) len_tmp;

	UNUSED(filp);
	UNUSED(data);

	if (len >= 80U) {
		I("%s: no command exceeds 80 chars.\n", __func__);
		ret = -EFAULT;
	} else if (copy_from_user(buf, buff, len) != 0) {
		ret = -EFAULT;
	} else if ((buf[0] == 's') || (buf[0] == 'S')) {
		inspect_mode_flag = 0x01;
		I("%s: Start to do Short_test.\n", __func__);
	} else if ((buf[0] == 'o') || (buf[0] == 'O')) {
		inspect_mode_flag = 0x02;
		I("%s: Start to do Open_test.\n", __func__);
	} else if ((buf[0] == 'n') || (buf[0] == 'N')) {
		inspect_mode_flag = 0x08;
		I("%s: Start to do Noise_test.\n", __func__);
	} else if ((buf[0] == 'a') || (buf[0] == 'A')) {
		inspect_mode_flag = 0x0B;
		I("%s: Start to do All_test.\n", __func__);
	} else if (buf[0] == 'h') {
		inspect_mode_flag = 0x0F;
		I("%s: User Call HELP! Lest's assist user to operate.\n",
		  __func__);
	} else {
		I("%s: Input cmd is incorrect!\n", __func__);
		ret = -EFAULT;
	}

	return ret;
}

int himax_inspect_mode_open(struct inode *inode, struct file *file)
{
	UNUSED(inode);
	return single_open(file, himax_inspect_mode_show, NULL);
}

const struct proc_ops_name himax_inspect_mode_ops = {
	owner_line
	.proc_op(open) = himax_inspect_mode_open,
	.proc_op(write) = himax_inspect_mode_store,
	.proc_op(read) = seq_read,
	.proc_opl(lseek) = seq_lseek,
	.proc_op(release) = single_release,
};

#endif

#if (HX_TP_GTS_MODE == 0x01)
ssize_t himax_GTS_read(struct file *file, char *buf, size_t len,
			      loff_t *pos)
{
	ssize_t count = 0;
	struct himax_ts_data *ts = private_ts;
	char *temp_buf = NULL;

	if (!HX_PROC_SEND_FLAG) {
		temp_buf = kcalloc(len, sizeof(char), GFP_KERNEL);
		if (temp_buf != NULL) {
			count = (ssize_t)snprintf(temp_buf, PAGE_SIZE, "%d\n",
					 ts->GTS_range);

			if (copy_to_user(buf, temp_buf, len)) {
				I("%s, here:%d\n", __func__, __LINE__);
			}

			kfree(temp_buf);
			temp_buf = NULL;
			HX_PROC_SEND_FLAG = true;
		} else {
			E("%s, Failed to allocate memory\n", __func__);
		}
	} else {
		HX_PROC_SEND_FLAG = false;
	}

	return count;
}

ssize_t himax_GTS_write(struct file *file, const char *buff, size_t len,
			       loff_t *pos)
{
	struct himax_ts_data *ts = private_ts;
	char buf[80] = { 0 };
	ssize_t ret = 0;
	int value = 0;

	if (copy_from_user(buf, buff, len) != 0) {
		ret = -EFAULT;
	} else if (kstrtoint(&buf[0], 10, &value) == 0) {
		if (value == 0) {
			ts->GTS_range = (uint8_t)value;
			I("%s: Ghost point protection disable, %d\n",
				__func__, value);
			ret = len;
		} else if ((value > 0) && (value < 9)) {
			ts->GTS_range = (uint8_t)value;
			I("%s: Ghost point protection enable, %d\n",
				__func__, value);
			ret = len;
		} else {
			/*Undefined behavior*/
			W("Undefined behavior\n");
		}
	} else {
		return -EINVAL;
	}
	return ret;
}

static const struct proc_ops_name himax_proc_GTS_ops = {
	owner_line
	.proc_op(read) = himax_GTS_read,
	.proc_op(write) = himax_GTS_write,
};
#endif

#if (HX_SMART_WAKEUP == 0x01)
ssize_t himax_GESTURE_read(struct file *file, char *buf, size_t len,
				  loff_t *pos)
{
	struct himax_ts_data *ts = private_ts;
	uint8_t i = 0;
	size_t ret = 0;
	char *temp_buf = NULL;

	if (!HX_PROC_SEND_FLAG) {
		temp_buf = kcalloc(len, sizeof(char), GFP_KERNEL);
		if (temp_buf != NULL) {
			for (i = 0; i < GEST_SUP_NUM; i++) {
				ret += (size_t)snprintf(&temp_buf[ret], len - ret,
						"ges_en[%d]=%d\n", i,
						ts->gesture_cust_en[i]);
			}

			if (copy_to_user(buf, temp_buf, len)) {
				I("%s, here:%d\n", __func__, __LINE__);
			}

			kfree(temp_buf);
			temp_buf = NULL;
			HX_PROC_SEND_FLAG = true;
		} else {
			E("%s, Failed to allocate memory\n", __func__);
		}
	} else {
		HX_PROC_SEND_FLAG = false;
		ret = 0;
	}

	return ret;
}

ssize_t himax_GESTURE_write(struct file *file, const char *buff,
				   size_t len, loff_t *pos)
{
	struct himax_ts_data *ts = private_ts;
	uint8_t i = 0;
	uint8_t j = 0;
	char buf[80] = { 0 };
	ssize_t ret = 0;

	if (len >= 80U) {
		I("%s: no command exceeds 80 chars.\n", __func__);
		ret =  -EFAULT;
	} else if (copy_from_user(buf, buff, len) != 0) {
		ret =  -EFAULT;
	} else {
		I("himax_GESTURE_store= %s, len = %d\n", buf, (int)len);
		for (i = 0; i < len; i++) {
			if (buf[i] == '0' && j < GEST_SUP_NUM) {
				ts->gesture_cust_en[j] = 0;
				I("gesture en[%d]=%d\n", j, ts->gesture_cust_en[j]);
				j++;
			} else if (buf[i] == '1' && j < GEST_SUP_NUM) {
				ts->gesture_cust_en[j] = 1;
				I("gesture en[%d]=%d\n", j, ts->gesture_cust_en[j]);
				j++;
			} else {
				/*do nothing*/
				I("Not 0/1 or >=GEST_SUP_NUM : buf[%d] = %c\n", i,
				  buf[i]);
			}
		}
		ret = len;
	}
	return ret;
}

static const struct proc_ops_name himax_proc_Gesture_ops = {
	owner_line
	.proc_op(read) = himax_GESTURE_read,
	.proc_op(write) = himax_GESTURE_write,
};
#endif

void himax_mcu_check_cascade_ic_num(void)
{
	uint8_t tmp_data[DATA_LEN_4] = { 0 };
	uint8_t slave_enb = 0xFFU;
	uint8_t i = 0x00U;
	struct himax_ts_data *ts = private_ts;

	for (i = 0; i < HIMAX_I2C_RETRY_TIMES; i++) {
		(void)memset(tmp_data, 0xFFU, sizeof(tmp_data));
		himax_mcu_register_read(addr_chk_tp_status, DATA_LEN_4, tmp_data);
		slave_enb = ((tmp_data[1] & 0x0CU) >> 2U);

		if (slave_enb == 0x00U) {
			ts->slave_ic_num = 0x02U;
			I("[IC_SLAVE_2] detected!\n");
			break;
		} else if (slave_enb == 0x02U) {
			ts->slave_ic_num = 0x01U;
			I("[IC_SLAVE_1] detected!\n");
			break;
		} else if (slave_enb == 0x03U) {
			ts->slave_ic_num = 0x00U;
			break;
		} else {
			slave_enb = 0xFFU;
		}
	}
	if (slave_enb == 0xFFU) {
		ts->slave_ic_num = 0x02U;
		W("Unexpected status, Default %d chips\n", (ts->slave_ic_num + 1U));
	} else {
		I("Number of cascaded ICs : [%d]\n", (ts->slave_ic_num + 1U));
	}
}

void himax_report_data_deinit(void)
{
	if (g_target_report_data->finger_id != NULL) {
		kfree(g_target_report_data->finger_id);
		g_target_report_data->finger_id = NULL;
	}
	if (g_target_report_data->w != NULL) {
		kfree(g_target_report_data->w);
		g_target_report_data->w = NULL;
	}
	if (g_target_report_data->y != NULL) {
		kfree(g_target_report_data->y);
		g_target_report_data->y = NULL;
	}
	if (g_target_report_data->x != NULL) {
		kfree(g_target_report_data->x);
		g_target_report_data->x = NULL;
	}
	if (g_target_report_data != NULL) {
		kfree(g_target_report_data);
		g_target_report_data = NULL;
	}
#if (HX_TP_GTS_MODE == 0x01)
	if (fixed_point_label->fpt_cnt != NULL) {
		kfree(fixed_point_label->fpt_cnt);
		fixed_point_label->fpt_cnt = NULL;
	}
	if (fixed_point_label->finger_id != NULL) {
		kfree(fixed_point_label->finger_id);
		fixed_point_label->finger_id = NULL;
	}
	if (fixed_point_label->y != NULL) {
		kfree(fixed_point_label->y);
		fixed_point_label->y = NULL;
	}
	if (fixed_point_label->x != NULL) {
		kfree(fixed_point_label->x);
		fixed_point_label->x = NULL;
	}
	if (fixed_point_label != NULL) {
		kfree(fixed_point_label);
		fixed_point_label = NULL;
	}
#endif
#if (HX_SMART_WAKEUP == 0x01)
	if (wake_event_buffer != NULL) {
		kfree(wake_event_buffer);
		wake_event_buffer = NULL;
	}
#endif
	if (hx_touch_data->hx_rawdata_buf != NULL) {
		kfree(hx_touch_data->hx_rawdata_buf);
		hx_touch_data->hx_rawdata_buf = NULL;
	}
	if (hx_touch_data->hx_coord_buf != NULL) {
		kfree(hx_touch_data->hx_coord_buf);
		hx_touch_data->hx_coord_buf = NULL;
	}
}

static void himax_common_proc_deinit(void)
{
	/*Remove all*/
#if (HX_TP_GTS_MODE == 0x01)
	if (himax_proc_GTS_file != NULL) {
		remove_proc_entry(HIMAX_PROC_GTS_FILE, himax_touch_proc_dir);
	}
#endif
#if (HX_TP_INSPECT_MODE == 0x01)
	if (himax_proc_INSPECT_MODE_file != NULL) {
		remove_proc_entry(HIMAX_PROC_INSPECT_MODE_FILE, himax_touch_proc_dir);
	}
#endif
	if (himax_proc_fail_det_file != NULL) {
		remove_proc_entry(HIMAX_PROC_FAIL_DET_FILE, himax_touch_proc_dir);
	}
	if (himax_proc_WPBPlock_node_file != NULL) {
		remove_proc_entry(HIMAX_PROC_WP_BP_LOCK_FILE, himax_touch_proc_dir);
	}
#if (HX_SMART_WAKEUP == 0x01)
	if (himax_proc_GESTURE_file != NULL) {
		remove_proc_entry(HIMAX_PROC_GESTURE_FILE, himax_touch_proc_dir);
	}
#endif
#if defined(CONFIG_TOUCHSCREEN_HIMAX_SELF_TEST)
	if (himax_proc_self_test_file != NULL) {
		remove_proc_entry(HIMAX_PROC_SELF_TEST_FILE, himax_touch_proc_dir);
	}
#endif
	if (himax_touch_proc_dir != NULL) {
		remove_proc_entry(HIMAX_PROC_TOUCH_FOLDER, NULL);
	}
}

static bool himax_common_proc_init(void)
{
	himax_touch_proc_dir = proc_mkdir(HIMAX_PROC_TOUCH_FOLDER, NULL);

	if (himax_touch_proc_dir == NULL) {
		E(" %s: himax_touch_proc_dir file create failed!\n", __func__);
		return false;
	} else {
#if defined(CONFIG_TOUCHSCREEN_HIMAX_SELF_TEST)
		himax_proc_self_test_file =
			proc_create(HIMAX_PROC_SELF_TEST_FILE, S_IRUGO,
					himax_touch_proc_dir, &himax_self_test_ops);
		if (himax_proc_self_test_file == NULL) {
			E(" %s: proc self_test file create failed!\n", __func__);
			himax_common_proc_deinit();
			return false;
		}
#endif
#if (HX_SMART_WAKEUP == 0x01)
		himax_proc_GESTURE_file =
			proc_create(HIMAX_PROC_GESTURE_FILE, (S_IRUGO | S_IWUGO), himax_touch_proc_dir,
					&himax_proc_Gesture_ops);

		if (himax_proc_GESTURE_file == NULL) {
			E(" %s: proc GESTURE file create failed!\n", __func__);
			himax_common_proc_deinit();
			return false;
		}
#endif
		himax_proc_WPBPlock_node_file =
			proc_create(HIMAX_PROC_WP_BP_LOCK_FILE, S_IRUGO,
					himax_touch_proc_dir,
					&himax_WPBPlock_node_ops);
		if (himax_proc_WPBPlock_node_file == NULL) {
			E(" %s: proc BPlock file create failed!\n", __func__);
			himax_common_proc_deinit();
			return false;
		}

		himax_proc_fail_det_file =
			proc_create(HIMAX_PROC_FAIL_DET_FILE, S_IRUGO,
					himax_touch_proc_dir, &himax_fail_det_ops);
		if (himax_proc_fail_det_file == NULL) {
			E(" %s: proc fail det file create failed!\n", __func__);
			himax_common_proc_deinit();
			return false;
		}
#if (HX_TP_INSPECT_MODE == 0x01)
		himax_proc_INSPECT_MODE_file =
			proc_create(HIMAX_PROC_INSPECT_MODE_FILE, (S_IRUGO | S_IWUGO),
					himax_touch_proc_dir, &himax_inspect_mode_ops);

		if (himax_proc_INSPECT_MODE_file == NULL) {
			E(" %s: proc INSPECT_MODE file create failed!\n", __func__);
			himax_common_proc_deinit();
			return false;
		}
#endif
#if (HX_TP_GTS_MODE == 0x01)
		himax_proc_GTS_file =
			proc_create(HIMAX_PROC_GTS_FILE, (S_IRUGO | S_IWUGO), himax_touch_proc_dir,
					&himax_proc_GTS_ops);

		if (himax_proc_GTS_file == NULL) {
			E(" %s: proc GTS file create failed!\n", __func__);
			himax_common_proc_deinit();
			return false;
		}
#endif
	}

	return true;

}

void himax_parse_assign_cmd(uint32_t addr, uint8_t *cmd, uint32_t len)
{
	/*I("%s: Entering!\n", __func__);*/
	switch (len) {
	case 1:
		cmd[0] = (uint8_t)(addr % 0x100U);
		break;
	case 2:
		cmd[0] = (uint8_t)(addr % 0x100U);
		cmd[1] = (uint8_t)((addr >> 8U) % 0x100U);
		break;
	case 4:
		cmd[0] = (uint8_t)(addr % 0x100U);
		cmd[1] = (uint8_t)((addr >> 8U) % 0x100U);
		cmd[2] = (uint8_t)((addr >> 16U) % 0x100U);
		cmd[3] = (uint8_t)(addr / 0x1000000U);
		break;
	default:
		E("%s: input length fault,len = %d!\n", __func__, len);
		break;
	}
}
EXPORT_SYMBOL(himax_parse_assign_cmd);

int himax_input_register(struct himax_ts_data *ts)
{
	int ret = NO_ERR;
#if (HX_SMART_WAKEUP == 0x01)
	uint8_t i = 0;
#endif

	ret = himax_dev_set(ts);

	if (ret < 0) {
		E("%s, input device register fail!\n", __func__);
		ret = INPUT_REGISTER_FAIL;
		goto input_device_fail;
	}

	set_bit(EV_SYN, ts->input_dev->evbit);
	set_bit(EV_ABS, ts->input_dev->evbit);
	set_bit(EV_KEY, ts->input_dev->evbit);
	set_bit(KEY_BACK, ts->input_dev->keybit);
	set_bit(KEY_HOME, ts->input_dev->keybit);
	set_bit(KEY_MENU, ts->input_dev->keybit);
	set_bit(KEY_SEARCH, ts->input_dev->keybit);

#if (HX_SMART_WAKEUP == 0x01)
	for (i = 0; i < GEST_SUP_NUM; i++) {
		set_bit(gest_key_def[i], ts->input_dev->keybit);
	}
#elif defined(CONFIG_TOUCHSCREEN_HIMAX_SELF_TEST)
	set_bit(KEY_POWER, ts->input_dev->keybit);
#endif
	set_bit(BTN_TOUCH, ts->input_dev->keybit);
	set_bit(KEY_APPSELECT, ts->input_dev->keybit);
	set_bit(INPUT_PROP_DIRECT, ts->input_dev->propbit);
#if (HX_PROTOCOL_A == 0x01)
	/*ts->input_dev->mtsize = ts->nFinger_support;*/
	input_set_abs_params(ts->input_dev, ABS_MT_TRACKING_ID, 1, ts->nFinger_support, 0, 0);
#else
	set_bit(MT_TOOL_FINGER, ts->input_dev->keybit);
#if (HX_PROTOCOL_B_3PA == 0x01)
	input_mt_init_slots(ts->input_dev, ts->nFinger_support,
			    INPUT_MT_DIRECT);
#else
	input_mt_init_slots(ts->input_dev, ts->nFinger_support);
#endif
#endif
	I("%s: mix_x %d, max_x %d, min_y %d, max_y %d\n", __func__,
	  ts->pdata->abs_x_min, ts->pdata->abs_x_max, ts->pdata->abs_y_min,
	  ts->pdata->abs_y_max);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X,
			     ts->pdata->abs_x_min, (ts->pdata->abs_x_max - 1U),
			     ts->pdata->abs_x_fuzz, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y,
			     ts->pdata->abs_y_min, (ts->pdata->abs_y_max - 1U),
			     ts->pdata->abs_y_fuzz, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR,
			     ts->pdata->abs_pressure_min,
			     ts->pdata->abs_pressure_max,
			     ts->pdata->abs_pressure_fuzz, 0);
#if (HX_PROTOCOL_A == 0x00)
	input_set_abs_params(ts->input_dev, ABS_MT_PRESSURE,
			     ts->pdata->abs_pressure_min,
			     ts->pdata->abs_pressure_max,
			     ts->pdata->abs_pressure_fuzz, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR,
			     ts->pdata->abs_width_min, ts->pdata->abs_width_max,
			     ts->pdata->abs_pressure_fuzz, 0);
#endif
	/*	input_set_abs_params(ts->input_dev, ABS_MT_AMPLITUDE, 0,*/
	/*			((ts->pdata->abs_pressure_max << 16)*/
	/*			| ts->pdata->abs_width_max),*/
	/*			0, 0);*/
	/*	input_set_abs_params(ts->input_dev, ABS_MT_POSITION,*/
	/*			0, (BIT(31)*/
	/*			| (ts->pdata->abs_x_max << 16)*/
	/*			| ts->pdata->abs_y_max),*/
	/*			0, 0);*/

	if (himax_input_register_device(ts->input_dev) == 0) {
		ret = NO_ERR;
	} else {
		E("%s: input register fail\n", __func__);
		ret = INPUT_REGISTER_FAIL;
	}

input_device_fail:
	return ret;
}
EXPORT_SYMBOL(himax_input_register);

static void calculate_point_number(void)
{
	HX_TOUCH_INFO_POINT_CNT = ic_data->HX_MAX_PT * 4U;

	if ((ic_data->HX_MAX_PT % 4U) == 0U) {
		HX_TOUCH_INFO_POINT_CNT += (ic_data->HX_MAX_PT / 4U) * 4U;
	} else {
		HX_TOUCH_INFO_POINT_CNT += ((ic_data->HX_MAX_PT / 4U) + 1U) * 4U;
	}
}

#if (HX_BOOT_UPGRADE == 0x01)
/*-------------------------------------------------------------------------
 *
 *	Create: Unknown
 *
 *	Description: Read FW_VER and CFG_VER value from FW file and compare with
 *		         FW/CFG version from MCU.
 *	Parameters: void
 *
 *	Returns: int (0 need update/ 1 no need update)
 *
 *	Side effects: None
 */
static int himax_auto_update_check(void)
{
	int32_t ret;

	if ((ic_data->vendor_touch_cfg_ver >= 0x80U) ||
	    (ic_data->vendor_display_cfg_ver >= 0x80U)) {
		ic_data->FW_update_flag = 0x03U;
		I("%s: Test FW, Need to update\n", __func__);
		ret = 0;
	} else if (himax_mcu_fw_ver_bin() == NO_ERR) {
		if ((ic_data->vendor_arch_ver < g_i_FW_VER) ||
		    (ic_data->vendor_config_ver < g_i_CFG_VER)) {
			ic_data->FW_update_flag = 0x04U;
			I("%s: FW BIN is newer version, Need to update\n",
			  __func__);
			ret = 0;
		} else if ((ic_data->vendor_arch_ver == g_i_FW_VER) &&
			   (ic_data->vendor_config_ver == g_i_CFG_VER)) {
			ic_data->FW_update_flag = 0x05U;
			if (himax_mcu_flash_lastdata_check_with_bin(
				    FW_SIZE_128k) == 1) {
				W("%s: Flash FW is different from BIN, Need update\n",
				  __func__);
				ret = 0;
			} else {
				I("%s: Flash FW is same with BIN, No need update!\n",
				  __func__);
				ret = 1;
			}
		} else {
			I("%s: Flash FW is newer version, No need update!\n",
			  __func__);
			ret = 1;
		}
	} else {
		E("%s: FW bin fail!\n", __func__);
		ret = 1;
	}

	return ret;
}

static int himax_force_update_FW_check(void)
{
	int ret = NO_ERR;

	ret = request_firmware(&hxfw, BOOT_FORCE_UPGRADE_FWNAME, private_ts->dev);
	if (ret < 0) {
		ret = OPEN_FILE_FAIL;
	} else {
		I("<<<Force update FW detected!!!>>>\n");
	}

	return ret;
}

static int i_get_FW(void)
{
	int ret = NO_ERR;

	I("%s: file name = %s\n", __func__, BOOT_UPGRADE_FWNAME);
	ret = request_firmware(&hxfw, BOOT_UPGRADE_FWNAME, private_ts->dev);
	if (ret < 0) {
		E("%s,%d: error code = %d\n", __func__, __LINE__, ret);
		ret = OPEN_FILE_FAIL;
	}

	return ret;
}

static int i_update_FW(void)
{
	uint8_t upgrade_times = 0;
	int8_t result = HX_FAIL;

	himax_int_enable(0);
	if (private_ts->hx_fail_det > 0) {
		himax_fail_det_enable(0);
	}

	while (upgrade_times < 3U) {
		if (!g_core_fp.fp_fts_ctpm_fw_upgrade(
			hxfw->data, (unsigned int)hxfw->size)) {
			upgrade_times++;
			E("%s: FW upgrade fail %d times\n", __func__,
				upgrade_times);
		} else {
			himax_mcu_config_reload_enable();
			himax_mcu_power_on_init();
			himax_mcu_read_FW_ver();
			g_core_fp.fp_touch_information();
			result = 1; /*upgrade success*/
			I("%s: FW upgrade OK\n", __func__);
#if defined(FW_update_history_record)
			himax_FW_update_record();
#endif
			break;
		}
	}

#if (HX_WPBP_ENABLE == 0x01)
	himax_enable_flash_protected_mode();
#endif
	himax_int_enable(1);
	if (private_ts->hx_fail_det > 0) {
		himax_fail_det_enable(1);
	}
	return result;
}
#endif
/*
 *static int himax_loadSensorConfig(struct himax_i2c_platform_data *pdata)
 *{
 *	I("%s: initialization complete\n", __func__);
 *	return NO_ERR;
 *}
 */
#if (HX_EXCP_RECOVERY == 0x01)
void himax_mcu_excp_ic_reset(void)
{
	HX_EXCP_RESET_ACTIVATE = false;
#if (HX_RST_PIN_FUNC == 0x01)
	himax_mcu_toggle_rst_gpio();
#elif (HX_RST_PIN_FUNC == 0x02)
	/* Need Customer do TP reset pin */
#else
	himax_mcu_system_reset();
#endif
	I("%s:\n", __func__);
}

static void himax_excp_hw_reset(void)
{
	if (g_ts_dbg != 0) {
		I("%s: Entering\n", __func__);
	}

	I("%s: START EXCEPTION Reset\n", __func__);

	if (private_ts->in_self_test == 1) {
		I("%s: In self test, not TP EXCEPTION Reset\n", __func__);
	} else {
		himax_mcu_excp_ic_reset();
	}

	I("%s: END EXCEPTION Reset\n", __func__);
}
#endif

#if (HX_SMART_WAKEUP == 0x01)
#if (HX_GESTURE_TRACK == 0x01)
static void gest_pt_log_coordinate(uint8_t rx, uint8_t tx)
{
	/*driver report x y with range 0 - 255 , we scale it up to x/y pixel*/
	gest_pt_x[gest_pt_cnt] = rx * (ic_data->HX_X_RES) / 255U;
	gest_pt_y[gest_pt_cnt] = tx * (ic_data->HX_Y_RES) / 255U;
}
#endif
static int himax_wake_event_parse(struct himax_ts_data *ts)
{
	uint8_t *buf = wake_event_buffer;
#if (HX_GESTURE_TRACK == 0x01)
	uint16_t tmp_max_x = 0x00U;
	uint16_t tmp_min_x = 0xFFFFU;
	uint16_t tmp_max_y = 0x00U;
	uint16_t tmp_min_y = 0xFFFFU;
	uint8_t gest_len;
#endif
	int ret = 0;
	uint8_t i = 0, check_FC = 0;
	uint8_t j = 0, gesture_pos = 0;
	uint8_t gesture_flag = 0;

	if (buf == NULL) {
		ret = -ENOMEM;
		goto END;
	}

	(void)memcpy(buf, hx_touch_data->hx_event_buf, hx_touch_data->event_size);

	for (i = 0; i < GEST_PTLG_ID_LEN; i++) {
		for (j = 0; j < GEST_SUP_NUM; j++) {
			if (buf[i] == gest_event[j]) {
				gesture_flag = buf[i];
				gesture_pos = j;
				break;
			}
		}
		I("0x%2.2X ", buf[i]);
		if (buf[i] == gesture_flag) {
			check_FC++;
		} else {
			I("ID START at 0x%02X , value = 0x%02X skip the event\n", i,
			  buf[i]);
			break;
		}
	}
	I("gesture_flag= 0x%02X, check_FC= %d\\n", gesture_flag, check_FC);

	if (check_FC != GEST_PTLG_ID_LEN) {
		ret = 0;
		goto END;
	}

	if (buf[GEST_PTLG_ID_LEN] != GEST_PTLG_HDR_ID1 ||
	    buf[GEST_PTLG_ID_LEN + 1U] != GEST_PTLG_HDR_ID2) {
		ret = 0;
		goto END;
	}

#if (HX_GESTURE_TRACK == 0x01)
	if (buf[GEST_PTLG_ID_LEN] == GEST_PTLG_HDR_ID1 &&
	    buf[GEST_PTLG_ID_LEN + 1U] == GEST_PTLG_HDR_ID2) {
		gest_len = buf[GEST_PTLG_ID_LEN + 2U];
		I("gest_len = %d\n", gest_len);
		i = 0;
		gest_pt_cnt = 0U;
		I("gest doornidate start\n %s", __func__);

		while (i < ((gest_len + 1U) / 2U)) {
			gest_pt_log_coordinate(
				buf[GEST_PTLG_ID_LEN + 4U + (i * 2U)],
				buf[GEST_PTLG_ID_LEN + 4U + (i * 2U) + 1U]);
			i++;
			I("gest_pt_x[%d]=%d,gest_pt_y[%d]=%d\n", gest_pt_cnt,
			  gest_pt_x[gest_pt_cnt], gest_pt_cnt,
			  gest_pt_y[gest_pt_cnt]);
			gest_pt_cnt += 1U;
		}

		if (gest_pt_cnt > 0U) {
			for (i = 0; i < gest_pt_cnt; i++) {
				if (tmp_max_x < gest_pt_x[i]) {
					tmp_max_x = gest_pt_x[i];
				}
				if (tmp_min_x > gest_pt_x[i]) {
					tmp_min_x = gest_pt_x[i];
				}
				if (tmp_max_y < gest_pt_y[i]) {
					tmp_max_y = gest_pt_y[i];
				}
				if (tmp_min_y > gest_pt_y[i]) {
					tmp_min_y = gest_pt_y[i];
				}
			}

			I("gest_point x_min=%d,x_max=%d,y_min=%d,y_max=%d\n",
			  tmp_min_x, tmp_max_x, tmp_min_y, tmp_max_y);

			gest_start_x = gest_pt_x[0];
			hx_gesture_coor[0] = gest_start_x;
			gest_start_y = gest_pt_y[0];
			hx_gesture_coor[1] = gest_start_y;
			gest_end_x = gest_pt_x[gest_pt_cnt - 1U];
			hx_gesture_coor[2] = gest_end_x;
			gest_end_y = gest_pt_y[gest_pt_cnt - 1U];
			hx_gesture_coor[3] = gest_end_y;
			if (tmp_max_x > tmp_min_x) {
				gest_width = tmp_max_x - tmp_min_x;
			}
			hx_gesture_coor[4] = gest_width;
			if (tmp_max_y > tmp_min_y) {
				gest_height = tmp_max_y - tmp_min_y;
			}
			hx_gesture_coor[5] = gest_height;
			gest_mid_x = (tmp_max_x + tmp_min_x) / 2U;
			hx_gesture_coor[6] = gest_mid_x;
			gest_mid_y = (tmp_max_y + tmp_min_y) / 2U;
			hx_gesture_coor[7] = gest_mid_y;
			/*gest_up_x*/
			hx_gesture_coor[8] = gest_mid_x;
			/*gest_up_y*/
			hx_gesture_coor[9] = gest_mid_y - (gest_height / 2U);
			/*gest_down_x*/
			hx_gesture_coor[10] = gest_mid_x;
			/*gest_down_y*/
			hx_gesture_coor[11] = gest_mid_y + (gest_height / 2U);
			/*gest_left_x*/
			hx_gesture_coor[12] = gest_mid_x - (gest_width / 2U);
			/*gest_left_y*/
			hx_gesture_coor[13] = gest_mid_y;
			/*gest_right_x*/
			hx_gesture_coor[14] = gest_mid_x + (gest_width / 2U);
			/*gest_right_y*/
			hx_gesture_coor[15] = gest_mid_y;
		}
	}
#endif

	/*if (!ts->gesture_cust_en[gesture_pos]) {*/
	if (gesture_flag == 0U) {
		I("%s NOT report key [%d] = %d\n", __func__, gesture_pos,
		  gest_key_def[gesture_pos]);
		g_target_report_data->SMWP_event_chk = 0;
		ret = 0;
	} else {
		g_target_report_data->SMWP_event_chk =
			(int)gest_key_def[gesture_pos];
		ret = (int)gesture_pos;
	}
END:
	return ret;
}

static void himax_wake_event_report(void)
{
	int KEY_EVENT = g_target_report_data->SMWP_event_chk;

	if (g_ts_dbg != 0) {
		I("%s: Entering!\n", __func__);
	}

	if (KEY_EVENT != 0) {
		I("%s SMART WAKEUP KEY event %d press\n", __func__, KEY_EVENT);
		input_report_key(private_ts->input_dev, KEY_EVENT, 1);
		input_sync(private_ts->input_dev);
		I("%s SMART WAKEUP KEY event %d release\n", __func__,
		  KEY_EVENT);
		input_report_key(private_ts->input_dev, KEY_EVENT, 0);
		input_sync(private_ts->input_dev);
#if (HX_GESTURE_TRACK == 0x01)
		I("gest_start_x=%d,start_y=%d,end_x=%d,end_y=%d\n",
		  gest_start_x, gest_start_y, gest_end_x, gest_end_y);
		I("gest_width=%d,height=%d,mid_x=%d,mid_y=%d\n", gest_width,
		  gest_height, gest_mid_x, gest_mid_y);
		I("gest_up_x=%d,up_y=%d,down_x=%d,down_y=%d\n",
		  hx_gesture_coor[8], hx_gesture_coor[9], hx_gesture_coor[10],
		  hx_gesture_coor[11]);
		I("gest_left_x=%d,left_y=%d,right_x=%d,right_y=%d\n",
		  hx_gesture_coor[12], hx_gesture_coor[13], hx_gesture_coor[14],
		  hx_gesture_coor[15]);
#endif
		g_target_report_data->SMWP_event_chk = 0;
	}
}

#endif

static int himax_report_data_init(void)
{
	int err = NO_ERR;

	if (hx_touch_data->hx_coord_buf != NULL) {
		kfree(hx_touch_data->hx_coord_buf);
		hx_touch_data->hx_coord_buf = NULL;
	}
	if (hx_touch_data->hx_rawdata_buf != NULL) {
		kfree(hx_touch_data->hx_rawdata_buf);
		hx_touch_data->hx_rawdata_buf = NULL;
	}

#if (HX_SMART_WAKEUP == 0x01)
	hx_touch_data->event_size = HIMAX_TOUCH_DATA_SIZE;

	if (hx_touch_data->hx_event_buf != NULL) {
		kfree(hx_touch_data->hx_event_buf);
		hx_touch_data->hx_event_buf = NULL;
	}

	if (wake_event_buffer != NULL) {
		kfree(wake_event_buffer);
		wake_event_buffer = NULL;
	}

#endif
	hx_touch_data->touch_all_size = HIMAX_TOUCH_DATA_SIZE;
	hx_touch_data->raw_cnt_max = ic_data->HX_MAX_PT / 4U;
	hx_touch_data->raw_cnt_rmd = ic_data->HX_MAX_PT % 4U;
	/* more than 4 fingers */
	if (hx_touch_data->raw_cnt_rmd != 0x00U) {
		hx_touch_data->rawdata_size =
			himax_mcu_cal_data_len(hx_touch_data->raw_cnt_rmd,
					       ic_data->HX_MAX_PT,
					       hx_touch_data->raw_cnt_max);

		hx_touch_data->touch_info_size =
			(ic_data->HX_MAX_PT + hx_touch_data->raw_cnt_max + 2U) *
			4U;
	} else { /* less than 4 fingers */
		hx_touch_data->rawdata_size =
			himax_mcu_cal_data_len(hx_touch_data->raw_cnt_rmd,
					       ic_data->HX_MAX_PT,
					       hx_touch_data->raw_cnt_max);

		hx_touch_data->touch_info_size =
			(ic_data->HX_MAX_PT + hx_touch_data->raw_cnt_max + 1U) *
			4U;
	}

	I("%s:HX_MAX_PT:%d,hx_raw_cnt_max:%d\n", __func__,
	  ic_data->HX_MAX_PT, hx_touch_data->raw_cnt_max);
	I("%s:hx_raw_cnt_rmd:%d,hx_rawdata_size:%d,touch_info_size:%d\n",
	  __func__, hx_touch_data->raw_cnt_rmd, hx_touch_data->rawdata_size,
	  hx_touch_data->touch_info_size);

	hx_touch_data->hx_coord_buf = kzalloc(
		sizeof(uint8_t) * (hx_touch_data->touch_info_size), GFP_KERNEL);

	if (hx_touch_data->hx_coord_buf == NULL) {
		himax_report_data_deinit();
		E("%s: Failed to allocate memory, %d\n", __func__, __LINE__);
		err = MEM_ALLOC_FAIL;
	}

#if (HX_SMART_WAKEUP == 0x01)
	wake_event_buffer =
		kcalloc(hx_touch_data->event_size, sizeof(uint8_t), GFP_KERNEL);
	if (wake_event_buffer == NULL) {
		himax_report_data_deinit();
		E("%s: Failed to allocate memory, %d\n", __func__, __LINE__);
		err = MEM_ALLOC_FAIL;
	}

	hx_touch_data->hx_event_buf = kzalloc(
		sizeof(uint8_t) * (hx_touch_data->event_size), GFP_KERNEL);
	if (hx_touch_data->hx_event_buf == NULL) {
		himax_report_data_deinit();
		E("%s: Failed to allocate memory, %d\n", __func__, __LINE__);
		err = MEM_ALLOC_FAIL;
	}
#endif

	hx_touch_data->hx_rawdata_buf =
		kzalloc((uint8_t)sizeof(uint8_t) * (hx_touch_data->touch_all_size -
					   hx_touch_data->touch_info_size),
			GFP_KERNEL);
	if (hx_touch_data->hx_rawdata_buf == NULL) {
		himax_report_data_deinit();
		E("%s: Failed to allocate memory, %d\n", __func__, __LINE__);
		err = MEM_ALLOC_FAIL;
	}

	if (g_target_report_data == NULL) {
		g_target_report_data = kzalloc(
			sizeof(struct himax_target_report_data), GFP_KERNEL);
		if (g_target_report_data == NULL) {
			himax_report_data_deinit();
			E("%s: Failed to allocate memory, %d\n", __func__, __LINE__);
			err = MEM_ALLOC_FAIL;
		}

		g_target_report_data->x =
			kzalloc(sizeof(int) * (ic_data->HX_MAX_PT), GFP_KERNEL);
		if (g_target_report_data->x == NULL) {
			himax_report_data_deinit();
			E("%s: Failed to allocate memory, %d\n", __func__, __LINE__);
			err = MEM_ALLOC_FAIL;
		}

		g_target_report_data->y =
			kzalloc(sizeof(int) * (ic_data->HX_MAX_PT), GFP_KERNEL);
		if (g_target_report_data->y == NULL) {
			himax_report_data_deinit();
			E("%s: Failed to allocate memory, %d\n", __func__, __LINE__);
			err = MEM_ALLOC_FAIL;
		}

		g_target_report_data->w =
			kzalloc(sizeof(int) * (ic_data->HX_MAX_PT), GFP_KERNEL);
		if (g_target_report_data->w == NULL) {
			himax_report_data_deinit();
			E("%s: Failed to allocate memory, %d\n", __func__, __LINE__);
			err = MEM_ALLOC_FAIL;
		}

		g_target_report_data->finger_id =
			kzalloc(sizeof(int) * (ic_data->HX_MAX_PT), GFP_KERNEL);
		if (g_target_report_data->finger_id == NULL) {
			himax_report_data_deinit();
			E("%s: Failed to allocate memory, %d\n", __func__, __LINE__);
			err = MEM_ALLOC_FAIL;
		}
	}
#if (HX_TP_GTS_MODE == 0x01)
	if (fixed_point_label == NULL) {
		fixed_point_label = kzalloc(
			sizeof(struct himax_target_report_data), GFP_KERNEL);
		if (fixed_point_label == NULL) {
			himax_report_data_deinit();
			E("%s: Failed to allocate memory, %d\n", __func__, __LINE__);
			err = MEM_ALLOC_FAIL;
		}

		fixed_point_label->x =
			kzalloc(sizeof(int) * (ic_data->HX_MAX_PT), GFP_KERNEL);
		if (fixed_point_label->x == NULL) {
			himax_report_data_deinit();
			E("%s: Failed to allocate memory, %d\n", __func__, __LINE__);
			err = MEM_ALLOC_FAIL;
		}

		fixed_point_label->y =
			kzalloc(sizeof(int) * (ic_data->HX_MAX_PT), GFP_KERNEL);
		if (fixed_point_label->y == NULL) {
			himax_report_data_deinit();
			E("%s: Failed to allocate memory, %d\n", __func__, __LINE__);
			err = MEM_ALLOC_FAIL;
		}

		fixed_point_label->finger_id =
			kzalloc(sizeof(int) * (ic_data->HX_MAX_PT), GFP_KERNEL);
		if (fixed_point_label->finger_id == NULL) {
			himax_report_data_deinit();
			E("%s: Failed to allocate memory, %d\n", __func__, __LINE__);
			err = MEM_ALLOC_FAIL;
		}

		fixed_point_label->fpt_cnt =
			kzalloc(sizeof(int) * (ic_data->HX_MAX_PT), GFP_KERNEL);
		if (fixed_point_label->fpt_cnt == NULL) {
			himax_report_data_deinit();
			E("%s: Failed to allocate memory, %d\n", __func__, __LINE__);
			err = MEM_ALLOC_FAIL;
		}

	}
	(void)memset(fixed_point_label->x, 0xFF, ic_data->HX_MAX_PT * sizeof(int));
	(void)memset(fixed_point_label->y, 0xFF, ic_data->HX_MAX_PT * sizeof(int));
#endif

	return err;
}

static int himax_ts_work_status(struct himax_ts_data *ts)
{
	/* 1: normal */
	int result = HX_REPORT_COORD;

	hx_touch_data->diag_cmd = ts->diag_cmd;
	if (hx_touch_data->diag_cmd != 0U) {
		result = HX_REPORT_COORD_RAWDATA;
	}

#if (HX_SMART_WAKEUP == 0x01)
	if (atomic_read(&ts->suspend_mode) && (ts->SMWP_enable) &&
	    (!hx_touch_data->diag_cmd)) {
		result = HX_REPORT_SMWP_EVENT;
	}
#endif
	/* I("Now Status is %d\n", result); */
	return result;
}

static void himax_touch_get(struct himax_ts_data *ts, uint8_t *buf, int ts_path,
			   int *ts_status)
{
	UNUSED(ts);

	if (g_ts_dbg != 0) {
		I("%s: Entering, ts_status=%d!\n", __func__, *ts_status);
	}

	switch (ts_path) {
	/*normal*/
	case HX_REPORT_COORD:
#if (HX_EXCP_RECOVERY == 0x01)
		if (HX_EXCP_RESET_ACTIVATE) {
			if (!himax_mcu_read_event_stack(buf, 128)) {
				E("%s: can't read data from chip!\n", __func__);
				*ts_status = HX_TS_GET_DATA_FAIL;
			}
		} else {
			if (!himax_mcu_read_event_stack(
				    buf, hx_touch_data->touch_info_size)) {
				E("%s: can't read data from chip!\n", __func__);
				*ts_status = HX_TS_GET_DATA_FAIL;
			}
		}
		break;
#else
		if (!himax_mcu_read_event_stack(
			    buf, hx_touch_data->touch_info_size)) {
			E("%s: can't read data from chip!\n", __func__);
			*ts_status = HX_TS_GET_DATA_FAIL;
		}
		break;
#endif

#if (HX_SMART_WAKEUP == 0x01)

	/*SMWP*/
	case HX_REPORT_SMWP_EVENT:
		__pm_wakeup_event(ts->ts_SMWP_wake_lock, TS_WAKE_LOCK_TIMEOUT);
		msleep(20);

		if (!himax_mcu_read_event_stack(buf,
						hx_touch_data->event_size)) {
			E("%s: can't read data from chip!\n", __func__);
			*ts_status = HX_TS_GET_DATA_FAIL;
		}
		break;
#endif
	case HX_REPORT_COORD_RAWDATA:
		if (!himax_mcu_read_event_stack(buf, 128)) {
			E("%s: can't read data from chip!\n", __func__);
			*ts_status = HX_TS_GET_DATA_FAIL;
		}
		break;
	default:
		/*do nothing*/
		break;
	}

}

/* start error_control*/
static void himax_checksum_cal(struct himax_ts_data *ts, uint8_t *buf,
			      int ts_path, int *ts_status)
{
	uint16_t check_sum_cal = 0;
	uint16_t i = 0;
	uint16_t length = 0;
	uint16_t zero_cnt = 0;
	uint8_t raw_data_sel = 0;

	if (g_ts_dbg != 0) {
		I("%s: Entering, ts_status=%d!\n", __func__, *ts_status);
	}

	/* Normal */
	switch (ts_path) {
	case HX_REPORT_COORD:
		length = hx_touch_data->touch_info_size;
		break;
#if (HX_SMART_WAKEUP == 0x01)
		/* SMWP */
	case HX_REPORT_SMWP_EVENT:
		length = (GEST_PTLG_ID_LEN + GEST_PTLG_HDR_LEN);
		break;
#endif
	case HX_REPORT_COORD_RAWDATA:
		length = hx_touch_data->touch_info_size;
		break;
	default:
		I("%s, Normal error!\n", __func__);
		*ts_status = HX_PATH_FAIL;
		break;
	}
	if (*ts_status != HX_PATH_FAIL) {

		for (i = 0; i < length; i++) {
			check_sum_cal += buf[i];
			if (buf[i] == 0x00U) {
				zero_cnt++;
			}
		}

		if (ts_path == HX_REPORT_COORD_RAWDATA) {
			zero_cnt = 0;
		}

		if ((check_sum_cal % 0x100U) != 0U) {
			I("point data_checksum not match check_sum_cal: 0x%02X",
			  check_sum_cal);
			*ts_status = HX_CHKSUM_FAIL;
		} else if (zero_cnt == length) {
			if (ts->use_irq) {
				I("[HIMAX TP MSG] All Zero event\n");
			}

			*ts_status = HX_CHKSUM_FAIL;
		} else {
			raw_data_sel = (buf[HX_TOUCH_INFO_POINT_CNT] >> 4U) & 0x0FU;
			/*I("%s:raw_out_sel=%x , hx_touch_data->diag_cmd=%x.\n",*/
			/*		__func__, raw_data_sel,*/
			/*		hx_touch_data->diag_cmd);*/
			/*raw data out not match skip it*/
			if ((raw_data_sel != 0x0FU) &&
				(raw_data_sel != hx_touch_data->diag_cmd)) {
				/*I("%s:raw data out not match.\n", __func__);*/
				if (hx_touch_data->diag_cmd == 0U) {
					/*Need to clear event stack here*/
					(void)himax_mcu_read_event_stack(
						buf,
						(128U - hx_touch_data->touch_info_size));
					/*I("%s: size =%d, buf[0]=%x ,buf[1]=%x,*/
					/*	buf[2]=%x, buf[3]=%x.\n",*/
					/*	__func__,*/
					/*	(128-hx_touch_data->touch_info_size),*/
					/*	buf[0], buf[1], buf[2], buf[3]);*/
					/*I("%s:also clear event stack.\n", __func__);*/
				}
				*ts_status = HX_READY_SERVE;
			}
		}
	}

	if (g_ts_dbg != 0) {
		I("%s: END, ts_status=%d!\n", __func__, *ts_status);
	}

}

#if (HX_EXCP_RECOVERY == 0x01)
int himax_mcu_ic_excp_recovery(uint32_t hx_excp_event, uint32_t hx_zero_event,
			       uint32_t length)
{
	int ret_val = NO_ERR;

	if (hx_excp_event == length) {
		g_zero_event_count = 0;
		ret_val = HX_EXCP_EVENT;
	} else if (hx_zero_event == length) {
		if (g_zero_event_count > 5) {
			g_zero_event_count = 0;
			I("EXCEPTION event checked - ALL Zero.\n");
			ret_val = HX_EXCP_EVENT;
		} else {
			g_zero_event_count++;
			I("ALL Zero event is %d times.\n", g_zero_event_count);
			ret_val = HX_ZERO_EVENT_COUNT;
		}
	} else {
		/* do nothing*/
	}

	return ret_val;
}

static void himax_ts_event_check(struct himax_ts_data *ts, uint8_t *buf,
				int ts_path, int *ts_status)
{
	uint32_t hx_excp_event = 0;
	uint32_t hx_zero_event = 0;
	int shaking_ret = 0;

	uint32_t loop_i = 0;
	uint32_t length = 0;

	if (g_ts_dbg != 0) {
		I("%s: Entering, ts_status=%d!\n", __func__, *ts_status);
	}

	/* Normal */
	switch (ts_path) {
	case HX_REPORT_COORD:
		length = hx_touch_data->touch_info_size;
		break;
#if (HX_SMART_WAKEUP == 0x01)
		/* SMWP */
	case HX_REPORT_SMWP_EVENT:
		length = (GEST_PTLG_ID_LEN + GEST_PTLG_HDR_LEN);
		break;
#endif
	case HX_REPORT_COORD_RAWDATA:
		length = hx_touch_data->touch_info_size;
		break;
	default:
		I("%s, Neither Normal Nor SMWP error!\n", __func__);
		*ts_status = HX_PATH_FAIL;
		break;
	}

	if (*ts_status != HX_PATH_FAIL) {
		if (g_ts_dbg != 0) {
			I("Now Path=%d, Now status=%d, length=%d\n", ts_path, *ts_status,
			  length);
		}

		for (loop_i = 0; loop_i < length; loop_i++) {
			if (ts_path == HX_REPORT_COORD ||
				ts_path == HX_REPORT_COORD_RAWDATA) {
				/* case 2 EXCEPTION recovery flow-Disable */
				if (buf[loop_i] == 0x00U) {
					hx_zero_event++;
				} else {
					hx_zero_event = 0;
					g_zero_event_count = 0;
				}
			}
		}

		if ((hx_excp_event == length || hx_zero_event == length) &&
			(!HX_EXCP_RESET_ACTIVATE) && (hx_touch_data->diag_cmd == 0U) &&
			(ts->in_self_test == 0)) {
			shaking_ret = himax_mcu_ic_excp_recovery(hx_excp_event,
								 hx_zero_event, length);

			if (shaking_ret == HX_EXCP_EVENT) {
				himax_excp_hw_reset();
				*ts_status = HX_EXCP_EVENT;
			} else if (shaking_ret == HX_ZERO_EVENT_COUNT) {
				*ts_status = HX_ZERO_EVENT_COUNT;
			} else {
				I("I2C running. Nothing to be done!\n");
				*ts_status = HX_IC_RUNNING;
			}

			/* drop 1st interrupts after chip reset */
		} else if (HX_EXCP_RESET_ACTIVATE) {
			HX_EXCP_RESET_ACTIVATE = false;
			I("%s: Skip by HX_EXCP_RESET_ACTIVATE.\n", __func__);
			*ts_status = HX_EXCP_REC_OK;
		} else {
			/* do nothing*/
		}
	}

	if (g_ts_dbg != 0) {
		I("%s: END, ts_status=%d!\n", __func__, *ts_status);
	}

}
#endif

static void himax_err_ctrl(struct himax_ts_data *ts, uint8_t *buf, int ts_path,
			  int *ts_status)
{
	himax_checksum_cal(ts, buf, ts_path, ts_status);

#if (HX_EXCP_RECOVERY == 0x01)
	if (*ts_status == HX_CHKSUM_FAIL) {
		himax_ts_event_check(ts, buf, ts_path, ts_status);
	} else {
		/* continuous N times record, not total N times. */
		g_zero_event_count = 0;
	}
#endif

	/* All Zero recovery — breaks interrupt floods from FPDLink latches.
	 * After N consecutive all-zero events, disable IRQ, reset the TDDI,
	 * clear the event stack, and re-enable.  This runs in threaded IRQ
	 * context so sleeping is safe.
	 */
	if (*ts_status == HX_CHKSUM_FAIL) {
		hx_all_zero_count++;
		if (hx_all_zero_count >= HX_ALL_ZERO_RESET_THRESHOLD) {
			I("%s: %d consecutive All Zero events, resetting TDDI\n",
			  __func__, hx_all_zero_count);
			himax_int_enable(0);
			g_core_fp.fp_sense_off();
			g_core_fp.fp_sense_on();
			msleep(120);
			hx_all_zero_count = 0;
			himax_int_enable(1);
			*ts_status = HX_TS_NORMAL_END;
		}
	} else {
		hx_all_zero_count = 0;
	}

	if (g_ts_dbg != 0) {
		I("%s: END, ts_status=%d!\n", __func__, *ts_status);
	}
}
/* end error_control*/

/* start distribute_data*/
static void himax_distribute_touch_data(uint8_t *buf, int ts_path, int *ts_status)
{
	uint8_t hx_state_info_pos = hx_touch_data->touch_info_size - 3U;

	if (g_ts_dbg != 0) {
		I("%s: Entering, ts_status=%d!\n", __func__, *ts_status);
	}

	if (ts_path == HX_REPORT_COORD) {
		(void)memcpy(hx_touch_data->hx_coord_buf, &buf[0],
		       hx_touch_data->touch_info_size);

		if ((buf[hx_state_info_pos] != 0xFFU) &&
		    (buf[hx_state_info_pos + 1U] != 0xFFU)) {
			(void)memcpy(hx_touch_data->hx_state_info,
			       &buf[hx_state_info_pos], 2);
		} else {
			(void)memset(hx_touch_data->hx_state_info, 0x00,
			       sizeof(hx_touch_data->hx_state_info));
		}

#if (HX_EXCP_RECOVERY == 0x01)
		if (HX_EXCP_RESET_ACTIVATE) {
			(void)memcpy(hx_touch_data->hx_rawdata_buf,
			       &buf[hx_touch_data->touch_info_size],
			       hx_touch_data->touch_all_size -
				       hx_touch_data->touch_info_size);
		}
#endif

	} else if (ts_path == HX_REPORT_COORD_RAWDATA) {
		(void)memcpy(hx_touch_data->hx_coord_buf, &buf[0],
		       hx_touch_data->touch_info_size);

		if ((buf[hx_state_info_pos] != 0xFFU) &&
		    (buf[hx_state_info_pos + 1U] != 0xFFU)) {
			(void)memcpy(hx_touch_data->hx_state_info,
			       &buf[hx_state_info_pos], 2);
		} else {
			(void)memset(hx_touch_data->hx_state_info, 0x00,
			       sizeof(hx_touch_data->hx_state_info));
		}

		(void)memcpy(hx_touch_data->hx_rawdata_buf,
		       &buf[hx_touch_data->touch_info_size],
		       hx_touch_data->touch_all_size -
			       hx_touch_data->touch_info_size);
#if (HX_SMART_WAKEUP == 0x01)
	} else if (ts_path == HX_REPORT_SMWP_EVENT) {
		(void)memcpy(hx_touch_data->hx_event_buf, buf,
		       hx_touch_data->event_size);
#endif
	} else {
		E("%s, Fail Path!\n", __func__);
		*ts_status = HX_PATH_FAIL;
	}

	if (g_ts_dbg != 0) {
		I("%s: End, ts_status=%d!\n", __func__, *ts_status);
	}

}
/* end assign_data*/

/* start parse_report_data*/
static void himax_parse_report_points(struct himax_ts_data *ts, int ts_path)
{
	uint32_t x = 0;
	uint32_t y = 0;
	uint32_t w = 0;
	uint8_t base = 0;
	uint8_t event_id = 0;
	uint8_t	palm_id = 0;
	uint8_t loop_i = 0;
	UNUSED(ts_path);

	if (g_ts_dbg != 0) {
		I("%s: start!\n", __func__);
	}

	if (g_target_report_data == NULL ||
	    g_target_report_data->x == NULL ||
	    g_target_report_data->y == NULL ||
	    g_target_report_data->w == NULL ||
	    g_target_report_data->finger_id == NULL) {
		E("%s: report data not initialized\n", __func__);
		return;
	}

	ts->old_finger = ts->pre_finger_mask;
	if (ts->hx_point_num == 0U) {
		if (g_ts_dbg != 0) {
			I("%s: hx_point_num = 0!\n", __func__);
		}
		return;
	}
	ts->pre_finger_mask = 0;
	hx_touch_data->finger_on = 1;
	AA_press = 1;

	g_target_report_data->finger_num = ts->hx_point_num;
	g_target_report_data->finger_on = hx_touch_data->finger_on;

	if (g_ts_dbg != 0) {
		I("%s:finger_num = 0x%2X, finger_on = %d\n", __func__, g_target_report_data->finger_num, g_target_report_data->finger_on);
	}

	for (loop_i = 0; loop_i < ts->nFinger_support; loop_i++) {
		base = loop_i * 4U;

		x = hx_touch_data->hx_coord_buf[base];
		x = (x << 8U) | hx_touch_data->hx_coord_buf[base + 1U];
		y = hx_touch_data->hx_coord_buf[base + 2U];
		y = (y << 8U) | hx_touch_data->hx_coord_buf[base + 3U];
		w = hx_touch_data
			    ->hx_coord_buf[(ts->nFinger_support * 4U) + loop_i];

		if (g_ts_dbg != 0) {
			D("%s: now parsing[%d]:x=%d, y=%d, w=%d\n", __func__,
			  loop_i, x, y, w);
		}

		if (ic_data->HX_IS_ID_EN) {
			event_id = hx_touch_data->hx_coord_buf[base] >> 0x06U;
			x = (uint32_t)hx_touch_data->hx_coord_buf[base] & 0x3FU;
			x = (x << 8U) | hx_touch_data->hx_coord_buf[base + 1U];

			if (ic_data->HX_ID_PALM_EN) {
				palm_id = hx_touch_data->hx_coord_buf[base + 2U] >> 0x06U;
				y = (uint32_t)hx_touch_data->hx_coord_buf[base + 2U] & 0x3FU;
				y = (y << 8U) | hx_touch_data->hx_coord_buf[base + 3U];
			}

			if ((event_id == 0U) || (event_id == 3U)) { /*No touch event or Leave event*/
				x = 0xFFFF;
				y = 0xFFFF;
			}
			if (g_ts_dbg != 0) {
				switch (event_id) {
				case 1:
					I("%s Event Enter!\n", __func__);
					break;
				case 2:
					I("%s Event Moving!\n", __func__);
					break;
				case 3:
					I("%s Event Leave!\n", __func__);
					break;
				default:
					W("%s Undefined behavior!\n", __func__);
					break;
				}

				if (ic_data->HX_ID_PALM_EN) {
					if (palm_id == 1U) {
						I("Plam event\n");
					}
				}
				I("Parsing[%d]:x=%d, y=%d, event_id=%d, palm_id=%d\n",
					loop_i, x, y, event_id, palm_id);
			}
		}

		if ((x < ts->pdata->abs_x_max) &&
		    (y < ts->pdata->abs_y_max)) {

			g_target_report_data->x[loop_i] = x;
			g_target_report_data->y[loop_i] = y;
			g_target_report_data->w[loop_i] = w;
			g_target_report_data->finger_id[loop_i] = 1;

			if (ts->first_pressed == 0U) {
				ts->first_pressed = 1;
				I("S1@%d, %d\n", x, y);
			}

			ts->pre_finger_data[loop_i][0] = x;
			ts->pre_finger_data[loop_i][1] = y;

			ts->pre_finger_mask += (uint16_t)(1U << loop_i);
		} else { /* report coordinates */
			g_target_report_data->x[loop_i] = x;
			g_target_report_data->y[loop_i] = y;
			g_target_report_data->w[loop_i] = w;
			g_target_report_data->finger_id[loop_i] = 0;

			if ((loop_i == 0U) && (ts->first_pressed == 1U)) {
				ts->first_pressed = 2;
				I("E1@%d, %d\n", ts->pre_finger_data[0][0],
				  ts->pre_finger_data[0][1]);
			}
		}
	}

	if (g_ts_dbg != 0) {
		for (loop_i = 0; loop_i < 10U; loop_i++) {
			D("DBG X=%d  Y=%d ID=%d\n",
			  g_target_report_data->x[loop_i],
			  g_target_report_data->y[loop_i],
			  g_target_report_data->finger_id[loop_i]);
		}

		D("DBG finger number %d\n", g_target_report_data->finger_num);
	}

	if (g_ts_dbg != 0) {
		I("%s: end!\n", __func__);
	}

}

static void himax_parse_report_data(struct himax_ts_data *ts, int ts_path,
				   int *ts_status)
{
	uint8_t EN_NoiseFilter;

	if (g_ts_dbg != 0) {
		I("%s: start now_status=%d!\n", __func__, *ts_status);
	}

	EN_NoiseFilter =
		(hx_touch_data->hx_coord_buf[HX_TOUCH_INFO_POINT_CNT + 2U] >> 3U);
	/* I("EN_NoiseFilter=%d\n", EN_NoiseFilter); */
	EN_NoiseFilter = EN_NoiseFilter & 0x01U;
	/* I("EN_NoiseFilter2=%d\n", EN_NoiseFilter); */

	if (hx_touch_data->hx_coord_buf[HX_TOUCH_INFO_POINT_CNT] == 0xFFU) {
		ts->hx_point_num = 0;
	} else {
		ts->hx_point_num =
			hx_touch_data->hx_coord_buf[HX_TOUCH_INFO_POINT_CNT] &
			0x0FU;
	}

	switch (ts_path) {
	case HX_REPORT_COORD:
		himax_parse_report_points(ts, ts_path);
		break;
	case HX_REPORT_COORD_RAWDATA:
		/* touch monitor rawdata */
		if (debug_data != NULL) {
			if (debug_data->fp_set_diag_cmd(ic_data, hx_touch_data) != 0) {
				I("%s:raw data_checksum not match\n", __func__);
			}
		} else {
			E("%s,There is no init set_diag_cmd\n", __func__);
		}
		himax_parse_report_points(ts, ts_path);
		break;
#if (HX_SMART_WAKEUP == 0x01)
	case HX_REPORT_SMWP_EVENT:
		(void)himax_wake_event_parse(ts);
		break;
#endif
	default:
		E("%s:Fail Path!\n", __func__);
		*ts_status = HX_PATH_FAIL;
		break;
	}
	if (g_ts_dbg != 0) {
		I("%s: end now_status=%d!\n", __func__, *ts_status);
	}
}

/* end parse_report_data*/

static void himax_report_all_leave_event(struct himax_ts_data *ts)
{
	uint8_t loop_i = 0;

	for (loop_i = 0; loop_i < ts->nFinger_support; loop_i++) {
#if (HX_PROTOCOL_A == 0x00)
		input_mt_slot(ts->input_dev, loop_i);
		input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);
		input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0);
		input_report_abs(ts->input_dev, ABS_MT_PRESSURE, 0);
		input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, 0);
#endif
	}
	input_report_key(ts->input_dev, BTN_TOUCH, 0);
	input_sync(ts->input_dev);
}

void himax_mcu_clear_event_stack(void)
{
	uint32_t addr_clear_event_stack = 0x80060014U;
	uint8_t data[DATA_LEN_4] = {0};

	himax_mcu_register_read(addr_clear_event_stack, DATA_LEN_4, data);
	data[0] |= 0x02U;
	himax_mcu_register_write(addr_clear_event_stack, DATA_LEN_4,
					 data);

}

/* start report_point*/
static void himax_finger_report(struct himax_ts_data *ts)
{
	uint8_t i = 0;
	bool valid = false;
#if (HX_TP_GTS_MODE == 0x01)
	int debounce_cnt = 3620; /* 3620 * 8.3ms = 30 sec. */
#endif

	if (g_ts_dbg != 0) {
		I("%s:start ts->hx_point_num=%d\n", __func__, ts->hx_point_num);
	}

	for (i = 0; i < ts->nFinger_support; i++) {
#if (HX_TP_GTS_MODE == 0x01)
		if (ts->GTS_range > 0U) {
			if ((g_target_report_data->x[i] != 0xFFFFU) ||
			    (g_target_report_data->y[i] != 0xFFFFU)) {
				/*
				 *I("report_data->x[i]=%d,y[i]=%d",
					g_target_report_data->x[i],
					g_target_report_data->y[i]);
				I("pre_report_data->pre_x[i]=%d,pre_y[i]=%d",
					fixed_point_label->x[i],
					fixed_point_label->y[i]);
				*/
				if ((fixed_point_label->x[i] == 0xFFFFU) &&
				    (fixed_point_label->y[i] == 0xFFFFU)) {
					/*I("fixed point appears\n");*/
					fixed_point_label->fpt_cnt[i] = 0;
					fixed_point_label->x[i] =
						g_target_report_data->x[i];
					fixed_point_label->y[i] =
						g_target_report_data->y[i];
				}
				if (((g_target_report_data->x[i] -
					     fixed_point_label->x[i]) <
				     ts->GTS_range) &&
				    ((fixed_point_label->x[i] -
					     g_target_report_data->x[i]) <
				     ts->GTS_range) &&
				    ((fixed_point_label->y[i] -
					     g_target_report_data->y[i]) <
				     ts->GTS_range) &&
				    ((g_target_report_data->y[i] -
					     fixed_point_label->y[i]) <
				     ts->GTS_range)) { /* in range */
					fixed_point_label->fpt_cnt[i]++;
					/*I("fpt_cnt[%d] = %d\n", i, fixed_point_label->fpt_cnt[i]);*/
				} else { /* out of range */
					fixed_point_label->fpt_cnt[i] = 0;
					fixed_point_label->x[i] = 0xFFFF;
					fixed_point_label->y[i] = 0xFFFF;
				}
			} else { /* finger leave */
				fixed_point_label->fpt_cnt[i] = 0;
				fixed_point_label->x[i] = 0xFFFF;
				fixed_point_label->y[i] = 0xFFFF;
			}
			if (fixed_point_label->fpt_cnt[i] >
			    debounce_cnt) { /* 3620 * 8.3ms = 30 sec. */
				I("[Ghost] point happens !!!");
				fixed_point_label->fpt_cnt[i] = 0;
				himax_report_all_leave_event(ts);
				himax_mcu_system_reset();
				return;
			}
		}
#endif

		if ((g_target_report_data->x[i] < ts->pdata->abs_x_max) &&
		    (g_target_report_data->y[i] < ts->pdata->abs_y_max)) {
			valid = true;
		} else {
			valid = false;
		}
		if (g_ts_dbg != 0) {
			I("valid=%d\n", valid);
		}
		if (valid) {
			if (g_ts_dbg != 0) {
				I("report_data->x[i]=%d,y[i]=%d,w[i]=%d",
				  g_target_report_data->x[i],
				  g_target_report_data->y[i],
				  g_target_report_data->w[i]);
			}
#if (HX_PROTOCOL_A == 0x00)
			input_mt_slot(ts->input_dev, i);
#else
			input_report_key(ts->input_dev, BTN_TOUCH, 1);
#endif
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR,
					 g_target_report_data->w[i]);
#if (HX_PROTOCOL_A == 0x00)
			input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR,
					 g_target_report_data->w[i]);
			input_report_abs(ts->input_dev, ABS_MT_PRESSURE,
					 g_target_report_data->w[i]);
#else
			input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, i);
#endif
			input_report_abs(ts->input_dev, ABS_MT_POSITION_X,
					 g_target_report_data->x[i]);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_Y,
					 g_target_report_data->y[i]);
#if (HX_PROTOCOL_A == 0x00)
			ts->last_slot = i;
			input_mt_report_slot_state(ts->input_dev,
						   MT_TOOL_FINGER, 1);
#else
			input_mt_sync(ts->input_dev);
#endif
		} else {
#if (HX_PROTOCOL_A == 0x00)
			input_mt_slot(ts->input_dev, i);
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);
			input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0);
			input_report_abs(ts->input_dev, ABS_MT_PRESSURE, 0);
			input_mt_report_slot_state(ts->input_dev,
						   MT_TOOL_FINGER, 0);
#endif
		}
	}
#if (HX_PROTOCOL_A == 0x00)
	input_report_key(ts->input_dev, BTN_TOUCH, 1);
#endif
	input_sync(ts->input_dev);

	if (g_ts_dbg != 0) {
		I("%s:end\n", __func__);
	}
}

static void himax_finger_leave(struct himax_ts_data *ts)
{
#if (HX_PROTOCOL_A == 0x00)
	uint8_t loop_i = 0;
#endif

	if (g_ts_dbg != 0) {
		I("%s: start!\n", __func__);
	}

	hx_touch_data->finger_on = 0;
	g_target_report_data->finger_on = 0;
	g_target_report_data->finger_num = 0;
	AA_press = 0;

#if (HX_PROTOCOL_A == 0x01)
	input_mt_sync(ts->input_dev);
#endif
#if (HX_PROTOCOL_A == 0x00)
	for (loop_i = 0; loop_i < ts->nFinger_support; loop_i++) {
		input_mt_slot(ts->input_dev, loop_i);
		input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);
		input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0);
		input_report_abs(ts->input_dev, ABS_MT_PRESSURE, 0);
		input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, 0);
	}
#endif
	if (ts->pre_finger_mask > 0U) {
		ts->pre_finger_mask = 0U;
	}

	if (ts->first_pressed == 1U) {
		ts->first_pressed = 2U;
		I("E1@%d, %d\n", ts->pre_finger_data[0][0],
		  ts->pre_finger_data[0][1]);
	}

	input_report_key(ts->input_dev, BTN_TOUCH, 0);
	input_sync(ts->input_dev);

	if (g_ts_dbg != 0) {
		I("%s: end!\n", __func__);
	}

}

static void himax_report_points(struct himax_ts_data *ts)
{
	if (g_ts_dbg != 0) {
		I("%s: start!\n", __func__);
	}

	if (ts->hx_point_num != 0U) {
		himax_finger_report(ts);
	} else {
		himax_finger_leave(ts);
	}

	if (g_ts_dbg != 0) {
		I("%s: end!\n", __func__);
	}
}
/* end report_points*/

static void himax_report_data(struct himax_ts_data *ts, int ts_path, int *ts_status)
{
	if (g_ts_dbg != 0) {
		I("%s: Entering, ts_status=%d!\n", __func__, *ts_status);
	}

	if ((ts_path == HX_REPORT_COORD) || (ts_path == HX_REPORT_COORD_RAWDATA)) {
		/* Touch Point information */
		himax_report_points(ts);

#if (HX_SMART_WAKEUP == 0x01)
	} else if (ts_path == HX_REPORT_SMWP_EVENT) {
		himax_wake_event_report();
#endif
	} else {
		E("%s:Fail Path!\n", __func__);
		*ts_status = HX_PATH_FAIL;
	}

	if (g_ts_dbg != 0) {
		I("%s: END, ts_status=%d!\n", __func__, *ts_status);
	}
}
/* end report_data */

static void himax_ts_operation(struct himax_ts_data *ts, int ts_path,
			      int *ts_status)
{
	(void)memset(ts->xfer_buff, 0x00, 128U * sizeof(uint8_t));

	himax_touch_get(ts, ts->xfer_buff, ts_path, ts_status);
	if (*ts_status == HX_TS_GET_DATA_FAIL) {
		/* do nothing */
	} else {
		himax_distribute_touch_data(ts->xfer_buff, ts_path, ts_status);
		himax_err_ctrl(ts, ts->xfer_buff, ts_path, ts_status);

		if ((*ts_status == HX_REPORT_DATA) || (*ts_status == HX_TS_NORMAL_END) ||
			(*ts_status == HX_READY_SERVE)) {
			himax_parse_report_data(ts, ts_path, ts_status);
			himax_report_data(ts, ts_path, ts_status);
		}
	}
}

#if defined(PANEL_ID_CHECK)
static int himax_check_panel_id(void)
{
	uint8_t data[10] = { 0 };
	int ret = -1;

	himax_mcu_write_dd_reg_password(IC_MASTER);

	himax_mcu_dd_reg_read(0xD1, 1, 10, data, 2 * 4, IC_MASTER);

	himax_mcu_clear_dd_reg_password(IC_MASTER);

	if (((char) data[0] == 'S') && ((char) data[1] == '1') && ((char) data[2] == '0') &&
	    ((char) data[3] == '6') && ((char) data[4] == 'A') && ((char) data[5] == 'K') &&
	    ((char) data[6] == '1') && ((char) data[7] == '1') && ((char) data[8] == '0') &&
	    ((char) data[9] == '0')) {
		I("%s: Current panel id is TIANMA\n", __func__);
		ret = PANEL_TIANMA;
	} else if (((char) data[0] == 'S') && ((char) data[1] == '1') && ((char) data[2] == '0') &&
		   ((char) data[3] == '6') && ((char) data[4] == 'A') && ((char) data[5] == 'K') &&
		   ((char) data[6] == '1') && ((char) data[7] == '3') && ((char) data[8] == '0') &&
		   ((char) data[9] == '0')) {
		I("%s: Current panel id is BOE\n", __func__);
		ret = PANEL_BOE;
	} else {
		/* do nothing*/
	}

	return ret;
}
#endif


void himax_fail_det_work(void)
{
	uint8_t cnt = 0U;
	uint8_t data[30] = { 0 };
	uint8_t tmp_data[DATA_LEN_4] = { 0 };
	uint32_t addr_idx = addr_fail_det_MainItem_DD_Master;
	/*
	 *	uint8_t addr[4] = {0xD4, 0x74, 0x00, 0x10};
	 *	uint32_t tmp_addr_32 = 0x100074D4;
	 *	Clear Simulation Register
	 *	himax_mcu_register_write(tmp_addr_32, DATA_LEN_4, data);
	 */
	himax_mcu_write_dd_reg_password(IC_MASTER);

#if defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83195) || defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83194)
	himax_mcu_dd_reg_read(0xE5, 0, 8, data, 0, IC_MASTER);
	I(PA_LOG_1, "E5_Bank0:", data[1], data[2], data[3], data[4]);
	himax_mcu_register_read(addr_fail_det_MainItem_checksum, DATA_LEN_4, data);
	I(PA_LOG_1, "MainItem_checksum:", data[0], data[1], data[2], data[3]);
	himax_mcu_register_read(addr_fail_det_IC_summary_status, DATA_LEN_4, data);
	I(PA_LOG_1, "IC_summary_status:", data[0], data[1], data[2], data[3]);
	do {
		himax_mcu_register_read(addr_idx , DATA_LEN_4, data);
		I(PA_LOG_1, addr_idx, data[0], data[1], data[2], data[3]);
		addr_idx += 4U;

		himax_mcu_register_read(addr_idx , DATA_LEN_4, data);
		I(PA_LOG_1, addr_idx, data[0], data[1], data[2], data[3]);
		addr_idx += 4U;
	
		himax_mcu_register_read(addr_idx , DATA_LEN_4, data);
		I(PA_LOG_1, addr_idx, data[0], data[1], data[2], data[3]);
		addr_idx += 4U;

		cnt++;
	} while (cnt <= private_ts->slave_ic_num);

	cnt = 0U;
	addr_idx = addr_fail_det_SubItem_DD_Master;
	himax_mcu_register_read(addr_fail_det_SubItem_checksum, DATA_LEN_4, data);
	I(PA_LOG_1, "SubItem_checksum:", data[0], data[1], data[2], data[3]);

	do {
		himax_mcu_register_read(addr_idx , DATA_LEN_4, data);
		I(PA_LOG_1, addr_idx, data[0], data[1], data[2], data[3]);
		addr_idx += 4U;

		himax_mcu_register_read(addr_idx , DATA_LEN_4, data);
		I(PA_LOG_1, addr_idx, data[0], data[1], data[2], data[3]);
		addr_idx += 4U;
	
		himax_mcu_register_read(addr_idx , DATA_LEN_4, data);
		I(PA_LOG_1, addr_idx, data[0], data[1], data[2], data[3]);
		addr_idx += 4U;

		himax_mcu_register_read(addr_idx , DATA_LEN_4, data);
		I(PA_LOG_1, addr_idx, data[0], data[1], data[2], data[3]);
		addr_idx += 4U;

		himax_mcu_register_read(addr_idx , DATA_LEN_4, data);
		I(PA_LOG_1, addr_idx, data[0], data[1], data[2], data[3]);
		addr_idx += 4U;
	
		himax_mcu_register_read(addr_idx , DATA_LEN_4, data);
		I(PA_LOG_1, addr_idx, data[0], data[1], data[2], data[3]);
		addr_idx += 4U;

		cnt++;
	} while (cnt <= private_ts->slave_ic_num);

#else
	cnt = 0U;
	addr_idx = addr_fail_det_GPIO1_msg;
	himax_mcu_dd_reg_read(0xE5, 0, 8, data, 0, IC_MASTER);
	I(PA_LOG_1, "E5_Bank0:", data[1], data[2], data[3], data[4]);
	I(PA_LOG_2, "E5_Bank0:", data[5], data[6], data[7]);

	himax_mcu_dd_reg_read(0xE5, 0, 8, data, 4, IC_MASTER);
	I(PA_LOG_1, "E5_Bank1:", data[1], data[2], data[3], data[4]);
	I(PA_LOG_2, "E5_Bank1:", data[5], data[6], data[7]);

	himax_mcu_dd_reg_read(0xE5, 0, 8, data, 12, IC_MASTER);
	I(PA_LOG_1, "E5_Bank3:", data[1], data[2], data[3], data[4]);
	I(PA_LOG_2, "E5_Bank3:", data[5], data[6], data[7]);

	himax_mcu_register_read(addr_idx, DATA_LEN_4, tmp_data);
	I("%s: 100074C0 value: tmp_data[1] = 0x%2.2x, tmp_data[1] = 0x%2.2x\n",
	  __func__, tmp_data[0], tmp_data[1]);
	I("%s: 100074C0 value: tmp_data[2] = 0x%2.2x, tmp_data[3] = 0x%2.2x\n",
	  __func__, tmp_data[2], tmp_data[3]);
#endif

	himax_mcu_clear_dd_reg_password(IC_MASTER);

	/*	It depends on customer: */
	goto AP_recovery;

AP_recovery:

	I("%s: Now FAIL_DET pulls high means IC need external recovery\n",
	  __func__);
#if (HX_RST_PIN_FUNC == 0x01)
	if (himax_mcu_tp_lcm_pin_reset()) {
		himax_mcu_tp_reset();
	}
#elif (HX_RST_PIN_FUNC == 0x02)
	/* Need Customer do AP recovery */
#endif
}

#define HX_MAX_RESET_RETRIES   5
#define HX_RESET_BACKOFF_MS    1000
#define HX_RECOVERY_INTERVAL   50

void himax_ts_work(struct himax_ts_data *ts)
{
	static int consecutive_reset_count;
	int *ts_status = kzalloc(sizeof(int), GFP_KERNEL);
	int ts_path = 0;

	*ts_status = HX_TS_NORMAL_END;

	if (debug_data != NULL) {
		debug_data->fp_ts_dbg_func(ts, HX_FINGER_ON);
	}

	ts_path = himax_ts_work_status(ts);

	switch (ts_path) {
	case HX_REPORT_COORD:
		himax_ts_operation(ts, ts_path, ts_status);
		break;
	case HX_REPORT_COORD_RAWDATA:
		himax_ts_operation(ts, ts_path, ts_status);
		break;
#if (HX_SMART_WAKEUP == 0x01)
	case HX_REPORT_SMWP_EVENT:
		himax_ts_operation(ts, ts_path, ts_status);
		break;
#endif
	default:
		E("%s:Path Fault! value=%d\n", __func__, ts_path);
		*ts_status = HX_PATH_FAIL;
		break;
	}
	if (*ts_status != HX_PATH_FAIL) {
		if (*ts_status == HX_TS_GET_DATA_FAIL) {
			consecutive_reset_count++;
			if (consecutive_reset_count <= HX_MAX_RESET_RETRIES) {
				I("%s: Reset Touch chip (attempt %d/%d)\n",
				  __func__, consecutive_reset_count,
				  HX_MAX_RESET_RETRIES);
				msleep(HX_RESET_BACKOFF_MS);
#if (HX_RST_PIN_FUNC == 0x01)
				himax_mcu_hw_reset(true);
#elif (HX_RST_PIN_FUNC == 0x02)
				/* Need Customer do TP reset pin */
#else
				himax_mcu_system_reset();
#endif
			} else if (consecutive_reset_count == HX_MAX_RESET_RETRIES + 1) {
				E("%s: I2C failed after %d resets, "
				  "backing off (possible I2C bus contention)\n",
				  __func__, HX_MAX_RESET_RETRIES);
			} else if ((consecutive_reset_count % HX_RECOVERY_INTERVAL) == 0) {
				/* Periodic recovery: retry a reset in case bus freed up */
				I("%s: Attempting recovery reset\n", __func__);
				consecutive_reset_count = 0;
			}
		} else {
			consecutive_reset_count = 0;
		}
	}

	if (debug_data != NULL) {
		debug_data->fp_ts_dbg_func(ts, HX_FINGER_LEAVE);
	}
	kfree(ts_status);
	ts_status = NULL;
}
/*end ts_work*/
enum hrtimer_restart himax_ts_timer_func(struct hrtimer *timer)
{
	struct himax_ts_data *ts;

	ts = container_of(timer, struct himax_ts_data, timer);
	queue_work(ts->himax_wq, &ts->work);
	hrtimer_start(&ts->timer, ktime_set(0, 12500000), HRTIMER_MODE_REL);
	return HRTIMER_NORESTART;
}

#if (HX_BOOT_UPGRADE == 0x01)
void himax_boot_upgrade(struct work_struct *work)
{
#if (HX_FIX_TOUCH_INFO == 0x00)
	int ret = 0;
#endif

	if (himax_force_update_FW_check() == 0) {
		ic_data->FW_update_flag = 0x01U;
		goto UPDATE_FW;
	}

	if (i_get_FW() != 0) {
		return;
	}

	(void)hx_mcu_bin_desc_get((unsigned char *)hxfw->data, HX1K);

	if (g_boot_upgrade_flag == true) {
		ic_data->FW_update_flag = 0x02U;
		I("%s: Forced upgrade\n", __func__);
		goto UPDATE_FW;
	}

	if (himax_auto_update_check() != 0) {
		goto SKIP_UPDATE_FW;
	}

UPDATE_FW:
	if (i_update_FW() <= 0) {
		E("%s: Update FW fail\n", __func__);
	} else {
		g_boot_upgrade_flag = false;
		I("%s: Update FW success\n", __func__);
#if (HX_FIX_TOUCH_INFO == 0x00)
		if (private_ts->input_dev) {
			input_unregister_device(private_ts->input_dev);
		} else {
			input_free_device(private_ts->input_dev);
		}

		himax_report_data_deinit();
		calculate_point_number();
		ret |= himax_input_register(private_ts);
		ret |= himax_report_data_init();
#endif
	}

SKIP_UPDATE_FW:
	release_firmware(hxfw);
	hxfw = NULL;
}
#endif

#if defined(HX_CONFIG_FB) || defined(HX_CONFIG_DRM)
static void himax_fb_register(struct work_struct *work)
{
	int ret = 0;

	struct himax_ts_data *ts =
		container_of(work, struct himax_ts_data, work_att.work);

	I("%s in\n", __func__);
#if defined(HX_CONFIG_FB)
	ts->fb_notif.notifier_call = fb_notifier_callback;
	ret = fb_register_client(&ts->fb_notif);
#elif defined(HX_CONFIG_DRM)
#if defined(__HIMAX_MOD__)
	hx_msm_drm_register_client =
		(void *)kallsyms_lookup_name("msm_drm_register_client");
	if (hx_msm_drm_register_client != NULL) {
		ts->fb_notif.notifier_call = drm_notifier_callback;
		ret = hx_msm_drm_register_client(&ts->fb_notif);
	} else {
		E("hx_msm_drm_register_client is NULL\n");
	}
#else
	ts->fb_notif.notifier_call = drm_notifier_callback;
	ret = msm_drm_register_client(&ts->fb_notif);
#endif
#endif
	if (ret != 0) {
		E("Unable to register fb_notifier: %d\n", ret);
	}
}
#endif

static bool hx_ic_register(void)
{
	/* Multi-chip auto-detection: try all enabled chips until one succeeds */
#if defined(__HIMAX_HX83192_MOD__) || defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83192)
	I("%s: Trying HX83192...\n", __func__);
	if (hx83192_init()) {
		I("%s: HX83192 detected!\n", __func__);
		return true;
	}
#endif
#if defined(__HIMAX_HX83193_MOD__) || defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83193)
	I("%s: Trying HX83193...\n", __func__);
	if (hx83193_init()) {
		I("%s: HX83193 detected!\n", __func__);
		return true;
	}
#endif
#if defined(__HIMAX_HX83180_MOD__) || defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83180)
	I("%s: Trying HX83180...\n", __func__);
	if (hx83180_init()) {
		I("%s: HX83180 detected!\n", __func__);
		return true;
	}
#endif
#if defined(__HIMAX_HX83181_MOD__) || defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83181)
	I("%s: Trying HX83181...\n", __func__);
	if (hx83181_init()) {
		I("%s: HX83181 detected!\n", __func__);
		return true;
	}
#endif
#if defined(__HIMAX_HX83194_MOD__) || defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83194)
	I("%s: Trying HX83194...\n", __func__);
	if (hx83194_init()) {
		I("%s: HX83194 detected!\n", __func__);
		return true;
	}
#endif
#if defined(__HIMAX_HX83195_MOD__) || defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83195)
	I("%s: Trying HX83195...\n", __func__);
	if (hx83195_init()) {
		I("%s: HX83195 detected!\n", __func__);
		return true;
	}
#endif
#if defined(__HIMAX_HX8530_MOD__) || defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX8530)
	I("%s: Trying HX8530...\n", __func__);
	if (hx8530_init()) {
		I("%s: HX8530 detected!\n", __func__);
		return true;
	}
#endif
	E("%s: No supported IC detected!\n", __func__);
	return false;
}

int himax_chip_common_init(void)
{
	int ret = 0;
	int err = PROBE_FAIL;
	struct himax_ts_data *ts = private_ts;
	struct himax_i2c_platform_data *pdata;

#if (HX_TP_GTS_MODE == 0x01)
	ts->GTS_range = 4;
#endif

	himax_print_define_function();

	ts->xfer_buff =
		devm_kzalloc(ts->dev, 128U * sizeof(uint8_t), GFP_KERNEL);
	if (ts->xfer_buff == NULL) {
		err = -ENOMEM;
		goto err_xfer_buff_fail;
	}

	pdata = kzalloc(sizeof(struct himax_i2c_platform_data), GFP_KERNEL);
	if (pdata == NULL) { /*Allocate Platform data space*/
		err = -ENOMEM;
		goto err_dt_platform_data_fail;
	}

	ic_data = kzalloc(sizeof(struct himax_ic_data), GFP_KERNEL);
	if (ic_data == NULL) { /*Allocate IC data space*/
		err = -ENOMEM;
		goto err_dt_ic_data_fail;
	}

	/* allocate report data */
	hx_touch_data = kzalloc(sizeof(struct himax_report_data), GFP_KERNEL);
	if (hx_touch_data == NULL) {
		err = -ENOMEM;
		goto err_alloc_touch_data_failed;
	}

	ts->pdata = pdata;
	if (himax_parse_dt(ts, pdata) < 0) {
		I(" pdata is NULL for DT\n");
		goto err_alloc_dt_pdata_failed;
	}

	ts->lcm_gpio = pdata->RESX;
	ts->pon_gpio = pdata->PON;
	ts->chip_test_mpap_flag = false;

#if (HX_RST_PIN_FUNC == 0x01)
	ts->rst_gpio = pdata->tp_ext_rstn;
#endif
	(void)himax_gpio_power_config(pdata);
	(void)himax_interrupt_gpio_config(pdata);

#if defined(HIMAX_I2C_PLATFORM)
	himax_mcu_interface_on();
#endif

	g_hx_chip_inited = false;

	if (!hx_ic_register()) {
		E("%s: can't detect IC!\n", __func__);
		goto error_ic_detect_failed;
	}

	if (!g_core_fp.fp_calculateChecksum(ic_data->HX_FW_SIZE)) {
		E("%s: check flash fail, please upgrade FW\n", __func__);
#if (HX_BOOT_UPGRADE == 0x01)
		g_boot_upgrade_flag = true;
#endif

#if (HX_FIX_TOUCH_INFO == 0x01)
		g_core_fp.fp_touch_information();
#endif

	} else {
		himax_mcu_config_reload_enable();
		himax_mcu_power_on_init();
		himax_mcu_read_FW_ver();
		g_core_fp.fp_touch_information();
	}

#if (HX_BOOT_UPGRADE == 0x01)
	ts->himax_boot_upgrade_wq =
		create_singlethread_workqueue("HX_boot_upgrade");
	if (!ts->himax_boot_upgrade_wq) {
		E("allocate himax_boot_upgrade_wq failed\n");
		err = -ENOMEM;
		goto err_boot_upgrade_wq_failed;
	}
	INIT_DELAYED_WORK(&ts->work_boot_upgrade, himax_boot_upgrade);
	queue_delayed_work(ts->himax_boot_upgrade_wq, &ts->work_boot_upgrade,
			   msecs_to_jiffies(2000));
#endif
	calculate_point_number();

#if defined(PANEL_ID_CHECK)
	ts->panel_id = himax_check_panel_id();
#endif

#if defined(CONFIG_OF)
	ts->power = pdata->power;
#endif

#if defined(CONFIG_OF)
	ts->pdata->abs_pressure_min = 0;
	ts->pdata->abs_pressure_max = 200;
	ts->pdata->abs_width_min = 0;
	ts->pdata->abs_width_max = 200;
#endif
	ts->suspended = false;

#if (HX_PROTOCOL_A == 0x01)
	ts->protocol_type = (uint8_t)PROTOCOL_TYPE_A;
#else
	ts->protocol_type = (uint8_t)PROTOCOL_TYPE_B;
#endif
	I("%s: Use Protocol Type %c\n", __func__,
	  (ts->protocol_type == (uint8_t)PROTOCOL_TYPE_A) ? 'A' : 'B');

	ret = himax_input_register(ts);
	if (ret != 0) {
		goto err_input_register_device_failed;
	}

	spin_lock_init(&ts->irq_lock);
	ts->initialized = true;

#if defined(HX_CONFIG_FB) || defined(HX_CONFIG_DRM)
	ts->himax_att_wq = create_singlethread_workqueue("HMX_ATT_request");

	if (!ts->himax_att_wq) {
		E(" allocate himax_att_wq failed\n");
		err = -ENOMEM;
		goto err_get_intr_bit_failed;
	}

	INIT_DELAYED_WORK(&ts->work_att, himax_fb_register);
	queue_delayed_work(ts->himax_att_wq, &ts->work_att,
			   msecs_to_jiffies(15000));
#endif

#if (HX_SMART_WAKEUP == 0x01)
	ts->SMWP_enable = true;
#if defined(KERNEL_VER_ABOVE_4_14)
	ts->ts_SMWP_wake_lock =
		wakeup_source_register(ts->dev, HIMAX_common_NAME);
#else
	wakeup_source_init(ts->ts_SMWP_wake_lock, HIMAX_common_NAME);
#endif
#endif

	/*touch data init*/
	err = himax_report_data_init();

	if (err < 0) {
		goto err_report_data_init_failed;
	}

	if (!himax_common_proc_init()) {
		E(" %s: himax_common proc_init failed!\n", __func__);
		goto err_creat_proc_file_failed;
	}

	/* Stop TDDI firmware before registering IRQ handler.
	 * After system_reset() in power_on_init, the TDDI resumes active
	 * reporting immediately (unlike cold boot where it starts idle).
	 * Without this, touch events arrive before the handler is ready,
	 * causing "All Zero event" floods on rmmod/modprobe cycles.
	 */
	g_core_fp.fp_sense_off();

	(void)himax_ts_register_interrupt();

	/* Disable IRQ immediately — the line may already be LOW from a
	 * latched interrupt in the FPDLink 983/988 chain, which causes
	 * the level-triggered handler to fire before the TDDI is ready.
	 */
	himax_int_enable(0);

	/* Restart TDDI firmware and allow it to complete its first scan
	 * cycle so the event stack contains valid data when interrupts
	 * are finally enabled.
	 */
	g_core_fp.fp_sense_on();
	msleep(120);

	/* Re-enable IRQ — himax_int_enable(1) clears the event stack
	 * first, which drains any stale all-zero events and releases
	 * the interrupt line before the handler starts processing.
	 */
	hx_all_zero_count = 0;
	himax_int_enable(1);

#if (HX_BOOT_UPGRADE == 0x01)
	if (g_boot_upgrade_flag) {
		himax_int_enable(0);
	}
#endif

	(void)himax_fail_det_register_interrupt();

	spin_lock_init(&ts->fail_det_lock);

#if defined(CONFIG_TOUCHSCREEN_HIMAX_DEBUG)
	if (himax_debug_init() != 0) {
		E(" %s: debug initial failed!\n", __func__);
	}
#endif

	g_hx_chip_inited = true;
	return 0;

err_creat_proc_file_failed:
	himax_report_data_deinit();
err_report_data_init_failed:
#if (HX_SMART_WAKEUP == 0x01)
#if defined(KERNEL_VER_ABOVE_4_14)
	wakeup_source_unregister(ts->ts_SMWP_wake_lock);
#else
	wakeup_source_trash(ts->ts_SMWP_wake_lock);
#endif
#endif
#if defined(HX_CONFIG_FB) || defined(HX_CONFIG_DRM)
	cancel_delayed_work_sync(&ts->work_att);
	destroy_workqueue(ts->himax_att_wq);
err_get_intr_bit_failed:
#endif
err_input_register_device_failed:
	if (ts->input_dev != NULL) {
		input_unregister_device(ts->input_dev);
	} else {
		input_free_device(ts->input_dev);
	}
	/*err_detect_failed:*/

#if (HX_BOOT_UPGRADE == 0x01)
	cancel_delayed_work_sync(&ts->work_boot_upgrade);
	destroy_workqueue(ts->himax_boot_upgrade_wq);
err_boot_upgrade_wq_failed:
#endif

error_ic_detect_failed:
	himax_gpio_power_deconfig(pdata);
#if !defined(CONFIG_OF)
err_power_failed:
#endif
err_alloc_dt_pdata_failed:
	kfree(hx_touch_data);
	hx_touch_data = NULL;
err_alloc_touch_data_failed:
	kfree(ic_data);
	ic_data = NULL;
err_dt_ic_data_fail:
	kfree(pdata);
	pdata = NULL;
err_dt_platform_data_fail:
	devm_kfree(ts->dev, ts->xfer_buff);
	ts->xfer_buff = NULL;
err_xfer_buff_fail:
	probe_fail_flag = 1;
	return err;
}

void himax_chip_common_deinit(void)
{
	struct himax_ts_data *ts = private_ts;

	/* Stop TDDI firmware before removing driver.
	 * Without this, the TDDI continues generating interrupts after rmmod,
	 * leaving touch_int asserted. On subsequent modprobe, the stuck
	 * interrupt causes continuous "All Zero event" polling.
	 */
	g_core_fp.fp_sense_off();

	(void)himax_ts_unregister_interrupt();

#if defined(CONFIG_TOUCHSCREEN_HIMAX_SELF_TEST)
	himax_self_test_data_clear();
#endif

#if defined(CONFIG_TOUCHSCREEN_HIMAX_DEBUG)
	himax_debug_remove();
#endif

	himax_common_proc_deinit();
	himax_report_data_deinit();

#if (HX_SMART_WAKEUP == 0x01)
#if defined(KERNEL_VER_ABOVE_4_14)
	wakeup_source_unregister(ts->ts_SMWP_wake_lock);
#else
	wakeup_source_trash(ts->ts_SMWP_wake_lock);
#endif
#endif
#if defined(HX_CONFIG_FB)
	if (fb_unregister_client(&ts->fb_notif) != 0) {
		E("Error occurred while unregistering fb_notifier.\n");
	}
	cancel_delayed_work_sync(&ts->work_att);
	destroy_workqueue(ts->himax_att_wq);
#elif defined(HX_CONFIG_DRM)
#if defined(__HIMAX_MOD__)
	hx_msm_drm_unregister_client =
		(void *)kallsyms_lookup_name("msm_drm_unregister_client");
	if (hx_msm_drm_unregister_client != NULL) {
		if (hx_msm_drm_unregister_client(&ts->fb_notif) != 0) {
			E("Error occurred while unregistering drm_notifier.\n");
		}
	} else {
		E("hx_msm_drm_unregister_client is NULL\n");
	}
#else
	if (msm_drm_unregister_client(&ts->fb_notif) != 0) {
		E("Error occurred while unregistering drm_notifier.\n");
	}
#endif
	cancel_delayed_work_sync(&ts->work_att);
	destroy_workqueue(ts->himax_att_wq);
#endif
	if (ts->input_dev != NULL) {
		input_unregister_device(ts->input_dev);
	} else {
		input_free_device(ts->input_dev);
	}

#if (HX_BOOT_UPGRADE == 0x01)
	cancel_delayed_work_sync(&ts->work_boot_upgrade);
	destroy_workqueue(ts->himax_boot_upgrade_wq);
#endif
	himax_gpio_power_deconfig(ts->pdata);

	kfree(hx_touch_data);
	hx_touch_data = NULL;
	kfree(ic_data);
	ic_data = NULL;
	devm_kfree(ts->dev, ts->xfer_buff);
	ts->xfer_buff = NULL;
	kfree(ts->pdata);
	ts->pdata = NULL;
	kfree(ts);
	ts = NULL;
	probe_fail_flag = 0;

	I("%s: Common section deinited!\n", __func__);
}

void himax_chip_common_suspend(struct himax_ts_data *ts)
{
	if (ts->suspended) {
		I("%s: Already suspended. Skipped.\n", __func__);
		goto END;
	} else {
		ts->suspended = true;
		I("%s: enter\n", __func__);
	}


#if (HX_SMART_WAKEUP == 0x01)
	if (gpio_is_valid(ts->pdata->PON) != 0) {
		if (gpio_direction_output(ts->pdata->PON, 0) != 0) {
			E("unable to set PON direction\n");
		}
	}

	if (ts->SMWP_enable) {
		atomic_set(&ts->suspend_mode, 1);
		ts->pre_finger_mask = 0;
		I("%s: SMART_WAKEUP enable, reject suspend\n",
		  __func__);
		goto END;
	}
#endif
	himax_int_enable(0);

	if (!ts->use_irq) {
		if (cancel_work_sync(&ts->work) != 0) {
			himax_int_enable(1);
		}
	}

	/*ts->first_pressed = 0;*/
	atomic_set(&ts->suspend_mode, 1);
	ts->pre_finger_mask = 0;

END:
	if (ts->in_self_test == 1) {
		ts->suspend_resume_done = 1;
	}

	I("%s: END\n", __func__);
}

void himax_chip_common_resume(struct himax_ts_data *ts)
{
	I("%s: enter\n", __func__);

	if (ts->suspended == false) {
		I("%s: It had entered resume, skip this step\n", __func__);
		goto END;
	} else {
		ts->suspended = false;
	}

#if (HX_EXCP_RECOVERY == 0x01)
	/* continuous N times record, not total N times. */
	g_zero_event_count = 0;
#endif

	atomic_set(&ts->suspend_mode, 0);
	ts->diag_cmd = 0;

	himax_mcu_tp_reset();

#if (HX_SMART_WAKEUP == 0x01)
	if (gpio_is_valid(ts->pdata->PON) != 0) {
		if (gpio_direction_output(ts->pdata->PON, 1) != 0) {
			E("unable to set PON direction\n");
		}
	}
#endif
	himax_report_all_leave_event(ts);

	himax_int_enable(1);

END:
	if (ts->in_self_test == 1) {
		ts->suspend_resume_done = 1;
	}

	I("%s: END\n", __func__);
}

#if defined(FW_update_history_record)
void himax_FW_update_record(void)
{
	struct time_var time_now;
	struct time_clock tm_time;
	const u8 history_string[] = "History";
	uint8_t record_log[16] = { 0 };
	uint8_t tmp_addr[DATA_LEN_4] = { 0 };
	uint8_t log_count = 0;
	uint8_t index = 0;
	uint32_t addr_fw_update_log = 0x3F000U;
	uint32_t CRC_value = 0;
	bool update_header = true;
	u8 *flash_buf = NULL;


	flash_buf = kzalloc(sizeof(uint8_t) * HX4K, GFP_KERNEL);
	if (flash_buf == NULL) {
		E("%s: Memory allocation falied!\n", __func__);
		return;
	}

	if (ic_data->HX_FW_SIZE == FW_SIZE_255k) {
		addr_fw_update_log = 0x7F000U;
	}

	/* Get the current time in seconds and nanoseconds */
	/* Convert the time to year, month, day, hour, minute, second */
	time_func(&time_now);

#if defined(KERNEL_VER_5_10)
	time64_to_tm(time_now.tv_sec, 0, &tm_time);
#else
	rtc_time_to_tm(time_now.tv_sec, &tm_time);
#endif

	/* Print the date and time */
	I("%s: The current date and time: %04d-%02d-%02d %02d:%02d:%02d\n",
	 __func__, tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
	 tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec);

	/* Year */
	record_log[0] = ((tm_time.tm_year + 1900) >> 8) & 0xFFU;
	record_log[1] = (tm_time.tm_year + 1900) & 0xFFU;
	/* Month */
	record_log[2] = (tm_time.tm_mon + 1);
	/* Day */
	record_log[3] = tm_time.tm_mday;
	/* Hour */
	record_log[4] = tm_time.tm_hour;
	/* Minute */
	record_log[5] = tm_time.tm_min;

	/* Display config version */
	record_log[6] = 0x0DU;
	record_log[7] = ic_data->vendor_display_cfg_ver;
	/* Touch config version */
	record_log[8] = 0x0CU;
	record_log[9] = ic_data->vendor_touch_cfg_ver;
	/* Fail result
	 * 0x01 force update
	 * 0x02 CRC fail
	 * 0x03 Test FW, Update
	 * 0x04 Big FW version, Update
	 * 0x05 Same FW but different last 4 bytes, Update
	 * 0x06 Debug node FW update
	 */
	record_log[10] = ic_data->FW_update_flag;
	/* Separate Byte */
	record_log[11] = 0xFFU;

	g_core_fp.fp_flash_dump_func(addr_fw_update_log, HX4K, flash_buf);

	himax_mcu_sector_erase(addr_fw_update_log, HX4K);

	if ((flash_buf[0] == 0xFFU)
		&& (flash_buf[1] == 0xFFU)
		&& (flash_buf[HX4K - 1U] == 0xFFU)
		&& (flash_buf[HX4K - 2U] == 0xFFU)) {
		I("empty flash zone\n");
		for (index = 0; index < 8U; index++) {
			flash_buf[index] = history_string[index];
		}

	} else if (strncmp(flash_buf, history_string, 8U) == 0) {
		flash_buf[8] += 1U;
		log_count = (uint8_t)(flash_buf[8] % 0x100U);/*update log count*/
	} else {
		W("Undefined behavior, flash zone: 0x%08X may be occupied\n",
			addr_fw_update_log);
		update_header = false;
	}

	if (update_header) {
		if (log_count == 0U) { /*first line log*/
			log_count++;
			flash_buf[8] = log_count;
		}

		for (index = 0; index < 16U; index++) {
			flash_buf[(log_count * 16U) + index] = record_log[index];
		}
		CRC_value =
			himax_mcu_calculate_CRC32_by_AP(flash_buf, HX4K - 4U);

		flash_buf[HX4K - 4U] = (uint8_t)((CRC_value) % 0x100U);
		flash_buf[HX4K - 3U] = (uint8_t)((CRC_value >> 8U) % 0x100U);
		flash_buf[HX4K - 2U] = (uint8_t)((CRC_value >> 16U) % 0x100U);
		flash_buf[HX4K - 1U] = (uint8_t)((CRC_value >> 24U) % 0x100U);

		g_core_fp.fp_sense_off();
		himax_disable_flash_protected_mode();
		(void)g_core_fp.fp_flash_programming((const u8 *)flash_buf, addr_fw_update_log,
			HX4K);

		himax_parse_assign_cmd(addr_fw_update_log, tmp_addr,
			sizeof(tmp_addr));
		if (g_core_fp.fp_check_CRC(tmp_addr, HX4K) == 0x00000000U) {
			I("%s Success!\n", __func__);
		} else {
			E("%s Fail!\n", __func__);
		}

		himax_mcu_tp_reset();
	}
	kfree(flash_buf);
	flash_buf = NULL;

}
#endif
