// SPDX-License-Identifier: GPL-2.0
/*  Himax Android Driver Sample Code for debug nodes
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

#include "himax_debug.h"
#include "himax_ic_core.h"


static uint8_t process_type;
static uint8_t pre_diag_cmd;
static bool h_overflow;
static unsigned int dbg_cmd_flag;
char *dbg_cmd_par;
static int (*dbg_func_ptr_r[CMD_NUM])(struct seq_file *m);
static ssize_t (*dbg_func_ptr_w[CMD_NUM])(char *buf, size_t len);
int g_max_mutual;
int g_min_mutual = 0xFFFF;
int g_max_self;
int g_min_self = 0xFFFF;
uint8_t reg_cmd[4];
uint8_t *reg_read_data;
uint8_t g_flash_progress;
uint32_t g_page_prog_start;
bool fw_update_going;
bool diag_wq_alive;
uint8_t g_diag_arr_num;
static u8 *g_gma_buf;
uint8_t g_sram_cmd;

static char *dbg_cmd_str[] = {
	"crc_test",
	"fw_debug",
	"attn",
	"layout",
	"senseonoff",
	"debug_level",
	"int_en",
	"register",
	"reset",
	"diag_arr",
	"diag",
	"GMA",
	NULL
};

uint8_t diag_max_cnt;

struct time_var g_timeStart;
struct time_var g_timeEnd;
struct time_var g_timeDelta;

struct proc_dir_entry *himax_proc_diag_dir;
struct proc_dir_entry *himax_proc_vendor_file;
struct proc_dir_entry *himax_proc_stack_file;
struct proc_dir_entry *himax_proc_delta_file;
struct proc_dir_entry *himax_proc_dc_file;
struct proc_dir_entry *himax_proc_baseline_file;
struct proc_dir_entry *himax_proc_debug_file;
struct proc_dir_entry *himax_proc_flash_dump_file;
struct proc_dir_entry *himax_proc_pintest_file;
struct proc_dir_entry *himax_proc_SRAM_test_file;


static void himax_burn_GMA_to_flash(const u8 *gma_buf, char type)
{
	uint32_t start_addr;
	uint8_t tmp_addr[DATA_LEN_4] = { 0 };

	if (type == 'V') { /*Start to flash VCOM*/
		start_addr = 0x28000U;
		D("%s : VCOM\n", __func__);
	} else if (type == 'A') { /*Start to flash Analog GMA*/
		start_addr = 0x29000U;
		D("%s : AGMA\n", __func__);
	} else if (type == 'D') { /*Start to flash Analog GMA*/
		start_addr = 0x2A000U;
		D("%s : DGMA\n", __func__);
	} else {
		E("Input cmd is incorrect.\n");
		return;
	}
	if (ic_data->HX_FW_SIZE == FW_SIZE_255k) {
		start_addr += 0x20000U;
	} else if (ic_data->HX_FW_SIZE == FW_SIZE_192k) {
		start_addr += 0x9000U;
	}

	g_core_fp.fp_sense_off();
	himax_disable_flash_protected_mode();
	himax_mcu_sector_erase(start_addr, HX4K);
	g_core_fp.fp_flash_programming(gma_buf, start_addr, HX4K);

	himax_parse_assign_cmd(start_addr, tmp_addr,
		sizeof(tmp_addr));
	if (g_core_fp.fp_check_CRC(tmp_addr, HX4K) == 0x00000000U) {
		D("Burn GMA Success!\n");
	} else {
		E("Burn GMA FAIL!\n");
	}

	himax_mcu_tp_reset();

#if (HX_WPBP_ENABLE == 0x01)
	himax_enable_flash_protected_mode();
#endif
}

static int himax_convert_GMA_data(const u8 *data, size_t data_size, char type)
{
	unsigned long value = 0;
	uint32_t GMA_length = 0;
	uint32_t CRC_value = 0;
	int	ret = NO_ERR;
	size_t loop_i = 0;
	char GMA_content[DATA_LEN_4] = { 0 };
	char value_buff[5] = { 0 };

	g_gma_buf = kcalloc(HX4K, sizeof(uint8_t), GFP_KERNEL);

	if (g_gma_buf == NULL) {
		ret = OPEN_FILE_FAIL;
		E("%s: Memory allocation falied!\n", __func__);
	} else {
		(void)memset(g_gma_buf, 0x00U, HX4K * sizeof(uint8_t));
		(void)memset(GMA_content, 0xFFU, sizeof(GMA_content));
		(void)memset(value_buff, '\0', sizeof(value_buff));
		while (loop_i < data_size) {
			if ((data[loop_i] == 'x') && (GMA_content[1] != 'x')) {
				GMA_content[1] = 'x';
			} else if ((data[loop_i] == '0') && (GMA_content[0] != '0')) {
				GMA_content[0] = '0';
			} else if (isxdigit(data[loop_i]) != 0) {
				if ((GMA_content[1] == 'x') && (GMA_content[0] == '0')) {
					if (isxdigit(GMA_content[2]) == 0) {
						GMA_content[2] = data[loop_i];
					} else {
						GMA_content[3] = data[loop_i];
					}
				}
			} else if (data[loop_i] == ',') {
			(void)memcpy(&value_buff[0], &GMA_content[0], sizeof(GMA_content));
				if (!kstrtoul(value_buff, 0, &value)) {
					g_gma_buf[8U + GMA_length] = (uint8_t)value;
					++GMA_length;
					(void)memset(GMA_content, 0xFFU, sizeof(GMA_content));
				} else {
					E("Undefined behavior,i %d, %s\n", loop_i, GMA_content);
					ret = OPEN_FILE_FAIL;
					break;
				}
			} else {
				/*do nothing*/
			}
			loop_i++;
		}

		himax_parse_assign_cmd(GMA_length, g_gma_buf,
			DATA_LEN_4);
		if (type == 'V') {
			g_gma_buf[4] = 0x01U;
			g_gma_buf[5] = 0x01U;
			D("%s : Data Title ID = VCOM\n", __func__);
		} else if (type == 'A') {
			g_gma_buf[6] = 0x02U;
			D("%s : Data Title ID = AGMA\n", __func__);
		} else if (type == 'D') {
			g_gma_buf[5] = 0x02U;
			D("%s : Data Title ID = DGMA\n", __func__);
		} else {
			ret = INPUT_REGISTER_FAIL;
			E("Unknown input cmd\n");
		}
		g_gma_buf[7] = 0x01U; /*I("Data Type = ASCII\n");*/

		CRC_value =
			himax_mcu_calculate_CRC32_by_AP(g_gma_buf, HX4K - 4U);

		g_gma_buf[HX4K - 4U] = (uint8_t)((CRC_value) % 0x100U);
		g_gma_buf[HX4K - 3U] = (uint8_t)((CRC_value >> 8U) % 0x100U);
		g_gma_buf[HX4K - 2U] = (uint8_t)((CRC_value >> 16U) % 0x100U);
		g_gma_buf[HX4K - 1U] = (uint8_t)((CRC_value >> 24U) % 0x100U);

	}

	return ret;
}

int16_t *getMutualBuffer(void)
{
	return diag_mutual;
}
int16_t *getMutualNewBuffer(void)
{
	return diag_mutual_new;
}
int16_t *getMutualOldBuffer(void)
{
	return diag_mutual_old;
}
int16_t *getSelfBuffer(void)
{
	return diag_self;
}
int16_t *getSelfNewBuffer(void)
{
	return diag_self_new;
}
int16_t *getSelfOldBuffer(void)
{
	return diag_self_old;
}
static void setMutualBuffer(uint8_t x_num, uint8_t y_num)
{
	diag_mutual = kzalloc(x_num * y_num * sizeof(int16_t), GFP_KERNEL);
}
static void setMutualNewBuffer(uint8_t x_num, uint8_t y_num)
{
	diag_mutual_new = kzalloc(x_num * y_num * sizeof(int16_t), GFP_KERNEL);
}
static void setMutualOldBuffer(uint8_t x_num, uint8_t y_num)
{
	diag_mutual_old = kzalloc(x_num * y_num * sizeof(int16_t), GFP_KERNEL);
}
static void setSelfBuffer(uint8_t x_num, uint8_t y_num)
{
	diag_self = kzalloc((x_num + y_num) * sizeof(int16_t), GFP_KERNEL);
}
static void setSelfNewBuffer(uint8_t x_num, uint8_t y_num)
{
	diag_self_new = kzalloc((x_num + y_num) * sizeof(int16_t), GFP_KERNEL);
}
static void setSelfOldBuffer(uint8_t x_num, uint8_t y_num)
{
	diag_self_old = kzalloc((x_num + y_num) * sizeof(int16_t), GFP_KERNEL);
}

static int himax_crc_test_read(struct seq_file *m)
{
	int ret = 0;
	bool result = false;

	g_core_fp.fp_sense_off();
	msleep(20);

	result = g_core_fp.fp_calculateChecksum(ic_data->HX_FW_SIZE);
	g_core_fp.fp_sense_on();

	if (result) {
		seq_printf(m,
				"CRC test is Pass!\n");
	} else {
		seq_printf(m,
				"CRC test is Fail!\n");
	}

	return ret;
}

int himax_proc_FW_debug_read(struct seq_file *m)
{
	int ret = 0;
	uint8_t i = 0;
	uint8_t data[4] = { 0 };
	uint32_t len = 0;

	len = (uint32_t)(sizeof(dbg_reg_ary) / sizeof(uint32_t));

	for (i = 0; i < len; i++) {
		himax_mcu_register_read(dbg_reg_ary[i], DATA_LEN_4, data);

		seq_printf(m,
		"reg[0-3] : 0x%08X = 0x%02X, 0x%02X, 0x%02X, 0x%02X\n",
		dbg_reg_ary[i], data[0], data[1], data[2], data[3]);
		D("reg[0-3] : 0x%08X = 0x%02X, 0x%02X, 0x%02X, 0x%02X\n",
		dbg_reg_ary[i], data[0], data[1], data[2], data[3]);
	}

	return ret;
}

int himax_attn_read(struct seq_file *m)
{
	int ret = 0;
	struct himax_ts_data *ts_data;

	ts_data = private_ts;

	seq_printf(m, "attn = %x\n",
			himax_int_gpio_read(ts_data->pdata->TSIX));

	return ret;
}

int himax_layout_read(struct seq_file *m)
{
	struct himax_ts_data *ts = private_ts;
	int ret = 0;

	seq_printf(m, "%d ",
			ts->pdata->abs_x_min);
	seq_printf(m, "%d ",
			ts->pdata->abs_x_max);
	seq_printf(m, "%d ",
			ts->pdata->abs_y_min);
	seq_printf(m, "%d ",
			ts->pdata->abs_y_max);
	seq_puts(m, "\n");

	return ret;
}

ssize_t himax_layout_write(char *buf, size_t len)
{
	struct himax_ts_data *ts = private_ts;
	char *token = NULL;
	uint8_t arg_cnt = 0;
	uint32_t layout[DATA_LEN_4] = { 0 };
	int value = 0;

	if (len >= 80U) {
		D("%s: no command exceeds 80 chars.\n", __func__);
		return -EFAULT;
	}
	token = strsep(&buf, ",");
	while (token != NULL) {
		if (!kstrtoint(token, 0, &value)) {
			layout[arg_cnt] = (uint32_t)value;
			arg_cnt++;
		} else {
			W("[FAIL]kstrtoul\n");
		}
		token = strsep(&buf, ",");
	}

	if (arg_cnt == DATA_LEN_4) {
		ts->pdata->abs_x_min = layout[0];
		ts->pdata->abs_x_max = layout[1];
		ts->pdata->abs_y_min = layout[2];
		ts->pdata->abs_y_max = layout[3];
		D("layout:%d, %d, %d, %d\n", ts->pdata->abs_x_min,
		  ts->pdata->abs_x_max, ts->pdata->abs_y_min,
		  ts->pdata->abs_y_max);
		if (ts->input_dev != NULL) {
			input_unregister_device(ts->input_dev);
		} else {
			input_free_device(ts->input_dev);
		}

		(void)himax_input_register(ts);
	} else {
		D("ERR@%d, %d, %d, %d\n", ts->pdata->abs_x_min,
		  ts->pdata->abs_x_max, ts->pdata->abs_y_min,
		  ts->pdata->abs_y_max);
	}

	return len;
}

ssize_t himax_sense_on_off_write(char *buf, size_t len)
{
	if (len >= 80U) {
		D("%s: no command exceeds 80 chars.\n", __func__);
		return -EFAULT;
	}

	if (buf[0] == '0') {
		g_core_fp.fp_sense_off();
		D("Sense off\n");
	}
	if (buf[0] == '1') {
		if (buf[1] == 's') {
			himax_mcu_tp_reset();
			D("Sense on, system reset\n");
		} else {
			g_core_fp.fp_sense_on();
			D("Sense on, command reset\n");
		}
	}

	return len;
}

int himax_debug_level_read(struct seq_file *m)
{
	struct himax_ts_data *ts_data;
	int ret = 0;

	ts_data = private_ts;
	seq_printf(m, "tsdbg: %d\n",
			g_ts_dbg);
	seq_printf(m, "level: %X\n",
			ts_data->debug_log_level);

	return ret;
}

ssize_t himax_debug_level_write(char *buf, size_t len)
{
	struct himax_ts_data *ts;
	int value = 0;

	ts = private_ts;

	if (len >= 12U) {
		D("%s: no command exceeds 12 chars.\n", __func__);
		return -EFAULT;
	}

	ts->debug_log_level = 0;

	if (!kstrtoint(buf, 0, &value)) {
		ts->debug_log_level = (uint8_t)value;
	} else {
		W("[FAIL]kstrtoint\n");
	}

	D("debug level set to %d\n", ts->debug_log_level);

	if ((ts->debug_log_level & BIT(4)) != 0U) {
		D("Turn on/Enable Debug Mode for himax_self_test!\n");
		goto END_FUNC;
	}

	if ((ts->debug_log_level & BIT(3)) != 0U) {
		if ((ts->pdata->screenWidth > 0U) && (ts->pdata->screenHeight > 0U) &&
		    ((ts->pdata->abs_x_max - ts->pdata->abs_x_min) > 0U) &&
		    ((ts->pdata->abs_y_max - ts->pdata->abs_y_min) > 0U)) {
			ts->widthFactor =
				((uint32_t)ts->pdata->screenWidth << SHIFTBITS) /
				(ts->pdata->abs_x_max - ts->pdata->abs_x_min);
			ts->heightFactor =
				((uint32_t)ts->pdata->screenHeight << SHIFTBITS) /
				(ts->pdata->abs_y_max - ts->pdata->abs_y_min);

			if ((ts->widthFactor > 0U) && (ts->heightFactor > 0U)) {
				ts->useScreenRes = 1;
			} else {
				ts->heightFactor = 0;
				ts->widthFactor = 0;
				ts->useScreenRes = 0;
			}
		} else {
			D("Enable finger debug with raw position mode!\n");
		}
	} else {
		ts->useScreenRes = 0;
		ts->widthFactor = 0;
		ts->heightFactor = 0;
	}
END_FUNC:
	return len;
}

int himax_int_en_read(struct seq_file *m)
{
	struct himax_ts_data *ts = private_ts;
	int ret = 0;

	seq_printf(m, "%d\n", ts->irq_enabled);

	return ret;
}

ssize_t himax_int_en_write(char *buf, size_t len)
{
	struct himax_ts_data *ts = private_ts;
	int ret = 0;

	if (len >= 12U) {
		D("%s: no command exceeds 12 chars.\n", __func__);
		return -EFAULT;
	}

	if (buf[0] == '0') {
		himax_int_enable(0);
	} else if (buf[0] == '1') {
		himax_int_enable(1);
	} else if (buf[0] == '2') {
		himax_int_enable(0);
		free_irq(ts->hx_irq, ts);
		ts->irq_enabled = false;
	} else if (buf[0] == '3') {
		ret = himax_int_en_set();

		if (ret == 0) {
			ts->irq_enabled = true;
			atomic_set(&ts->irq_state, 1);
		}
	} else if (buf[0] == '4') {
		ic_data->HX_INT_IS_EDGE = true;
		ret = himax_int_en_set();

		if (ret == 0) {
			ts->irq_enabled = true;
			atomic_set(&ts->irq_state, 1);
		}
	} else if (buf[0] == '5') {
		ic_data->HX_INT_IS_EDGE = false;
		ret = himax_int_en_set();

		if (ret == 0) {
			ts->irq_enabled = true;
			atomic_set(&ts->irq_state, 1);
		}
	} else {
		return -EINVAL;
	}

	return len;
}

int himax_proc_register_read(struct seq_file *m)
{
	int ret = 0;
	uint16_t loop_i;
	uint32_t tmp_data;
	uint8_t wrapper_data[DATA_LEN_4] = { 0 };
	uint8_t dd_addr = 0;
	uint8_t bank = 0;
	uint8_t pa_num = 0;

	if (((char)cfg_flag == 'h') || ((char)cfg_flag == 'H')) {
		seq_puts(m, "*Read from Master:\n");
		seq_puts(m, " #echo register,r:x10007F10 > debug\n");
		seq_puts(m, "*Write to Master:\n");
		seq_puts(m, " #echo register,w:x10007F10:x00000000> debug\n");
		seq_puts(m, "*Read from Slave_2:\n");
		seq_puts(m, " #echo register,r:x02:x900000A8 > debug\n");
		seq_puts(m, "*Write to Slave_3:\n");
		seq_puts(m, " #echo register,w:x03:x10007F10:x03030303 > debug\n");
		seq_puts(m, "*Write to Slave_12:\n");
		seq_puts(m, " #echo register,w:x0C:x10007F10:x12121212 > debug\n");
		seq_puts(m, "*Broadcast write to all slaves:\n");
		seq_puts(m, " #echo register,w:x7E:x10007F10:x7E7E7E7E > debug\n");
		seq_puts(m, "*Read 1 byte from Master:\n");
		seq_puts(m, " #echo register,r:xFE0D > debug\n");
		seq_puts(m, "*Write 1 byte to Master:\n");
		seq_puts(m, " #echo register,w:xFE0D:x00 > debug\n");
		return NO_ERR;
	}

	tmp_data = ((uint32_t) reg_cmd[3] << 24);
	tmp_data += ((uint32_t) reg_cmd[2] << 16);
	tmp_data += ((uint32_t) reg_cmd[1] << 8);
	tmp_data += reg_cmd[0];
	(void)memset(reg_read_data, 0x00, 128U * sizeof(uint8_t));

	if (reg_cmd[3] == 0x00U) {
		/* Disable retry wrapper to avoid I2C CLK low issue */
		wrapper_data[0] = 0xA5;
		himax_mcu_register_write(addr_retry_wrapper_clr_pw, 4, wrapper_data);
		D("%s: Disable retry wrapper for flash read.\n", __func__);
	}

	D("himax_register_show: %02X,%02X,%02X,%02X\n", reg_cmd[3], reg_cmd[2],
	  reg_cmd[1], reg_cmd[0]);

	if (cfg_flag == 0xFEU) {
		ret = himax_bus_read(reg_cmd[0], reg_read_data, 128,
				     HIMAX_I2C_RETRY_TIMES);
		seq_printf(m, "command:  %02X\n", reg_cmd[0]);
	} else if (cfg_flag == 0xEEU) {
		seq_puts(m, "*The input value has exceeded total number of ICs\n");
	} else if (cfg_flag == (uint8_t)(IC_MASTER)) {
		seq_printf(m, "command:  %02X,%02X,%02X,%02X\n", reg_cmd[3],
			reg_cmd[2], reg_cmd[1], reg_cmd[0]);
		if (reg_cmd[3] != 0x30U) {
			himax_mcu_register_read(tmp_data, 128, reg_read_data);
		} else {
			himax_mcu_write_dd_reg_password(cfg_flag);
			dd_addr = (reg_cmd[2] << 0x04) | (reg_cmd[1] >> 0x04);
			bank = reg_cmd[1] & 0x0FU;
			pa_num = reg_cmd[0];
			D("dd_addr:%02X,bank:%02X,pa_num:%02X\n", dd_addr, bank, pa_num);
			himax_mcu_dd_reg_read(dd_addr, pa_num, 16, reg_read_data, bank, IC_MASTER);

			himax_mcu_clear_dd_reg_password(cfg_flag);
		}
	} else {
		seq_printf(m, "*Access Slave %d\n", cfg_flag);
		seq_printf(m, "command:  %02X,%02X,%02X,%02X\n", reg_cmd[3],
			reg_cmd[2], reg_cmd[1], reg_cmd[0]);
		if (reg_cmd[3] != 0x30U) {
			himax_mcu_register_read_slave(cfg_flag, tmp_data, 128, reg_read_data);
		} else {
			himax_mcu_write_dd_reg_password(cfg_flag);
			dd_addr = (reg_cmd[2] << 0x04) | (reg_cmd[1] >> 0x04);
			bank = reg_cmd[1] & 0x0FU;
			pa_num = reg_cmd[0];
			D("dd_addr:%02X,bank:%02X,pa_num:%02X\n", dd_addr, bank, pa_num);
			himax_mcu_dd_reg_read(dd_addr, pa_num, 16, reg_read_data, bank, cfg_flag);

			himax_mcu_clear_dd_reg_password(cfg_flag);
		}
	}
	if (cfg_flag != 0xEEU) {
		for (loop_i = 0; loop_i < 128U; loop_i++) {
			seq_printf(m, "0x%2.2X ", reg_read_data[loop_i]);
			if ((loop_i % 16U) == 15U) {
				seq_puts(m, "\n");
			}
		}
	} else {
		seq_puts(m, "*Please try again!\n");
	}

	seq_puts(m, "\n");

	return ret;
}

ssize_t himax_proc_register_write(char *buf, size_t len)
{
	unsigned long result = 0;
	char data_str[128] = { 0 };
	char addr_str[9] = { 0 };
	char ic_str[3] = { 0 };
	int ret = 0;
	uint8_t w_data[20] = { 0 };
	uint32_t tmp_addr_32 = 0;
	uint32_t loop_i = 0;
	uint8_t dd_addr = 0;
	uint8_t bank = 0;
	uint8_t pa_num = 0;
	char *token = NULL;
	size_t str_len = 0;
	char action = 0;
	char *buf_backup;

	buf_backup = kzalloc(sizeof(char) * (len), GFP_KERNEL);
	(void)memcpy(&buf_backup[0], &buf[0], len);
	(void)memset(reg_cmd, 0x0, sizeof(reg_cmd));

	if (len >= 80U) {
		W("%s: no command exceeds 80 chars.\n", __func__);
		ret = -EFAULT;
	} else if ((buf[0] == 'h') || (buf[0] == 'H')) {
		D("%s: Call Help!\n", __func__);
		cfg_flag = (uint8_t)buf[0];
		ret = NO_ERR;
	} else if (((buf[0] != 'r') && (buf[0] != 'w')) || (buf[1] != ':') ||
		(buf[2] != 'x')) {
		W("%s: Wrong input format=> %s\n", __func__, buf);
		D("Try [#echo register,help > debug] to call help!\n");
		ret = -EFAULT;
	} else {
		token = strsep(&buf_backup, "x");
		while (token  != NULL) {
			if (token[0] == 'w') {
				action = 'w';
			} else if (token[0] == 'r') {
				action = 'r';
			} else {
				str_len = 0;
				while (isxdigit(token[str_len]) != 0) {
					str_len++;
				}
				if (str_len < sizeof(ic_str) && (uint8_t)addr_str[0] == 0U) {
					(void)memcpy(&ic_str[0], &token[0], str_len);
				} else if (action == 'w') {
					if ((uint8_t)addr_str[0] == 0U) {
						(void)memcpy(&addr_str[0], &token[0], str_len);
					} else {
						(void)memcpy(&data_str[0], &token[0], str_len);
					}
				} else if (action == 'r') {
					if ((uint8_t)addr_str[0] == 0U) {
						(void)memcpy(&addr_str[0], &token[0], str_len);
					} else {
						W("Undefined behavior\n");
					}
				} else {
					/*Undefined behavior*/
					W("Undefined behavior\n");
				}
			}
			token = strsep(&buf_backup, "x");
		}

		if ((addr_str[0] == 'F') && (addr_str[1] == 'E')) {
			cfg_flag = (uint8_t) 0xFE;
			byte_length = 1;
		} else if ((uint8_t)ic_str[0] != 0U) {
			if (!kstrtoul(ic_str, 0, &result)) {
				cfg_flag = (uint8_t)result;
			} else {
				W("kstrtoul for ic_str\n");
			}
			byte_length = DATA_LEN_4;
		} else {
			cfg_flag = (uint8_t)IC_MASTER;
			byte_length = DATA_LEN_4;
		}

		D("action:%c,ic:%s,addr:%s,data:%s, cfg_flag:%d\n",
			action, ic_str, addr_str, data_str, cfg_flag);


		if ((cfg_flag > private_ts->slave_ic_num) && (cfg_flag != 0xFEU)
			&& (cfg_flag != 0x7EU)) {

			W("The input value %d has exceeded total number of ICs\n",
				cfg_flag);
			cfg_flag = 0xEEU;
			ret = HX_FAIL;
		}

		if (action == 'r') {
			if (!kstrtoul(addr_str, 16, &result)) {
				for (loop_i = 0; loop_i < byte_length; loop_i++) {
					reg_cmd[loop_i] = (uint8_t)(result >> (loop_i * 8U));
				}
			}
		} else if (action == 'w') {
			if (!kstrtoul(addr_str, 16, &result)) {
				/* addr */
				for (loop_i = 0; loop_i < byte_length; loop_i++) {
					reg_cmd[loop_i] = (uint8_t)(result >> (loop_i * 8U));
				}
			}
			if (!kstrtoul(data_str, 16, &result)) {
				/* data */
				for (loop_i = 0; loop_i < byte_length; loop_i++) {
					w_data[loop_i] = (uint8_t)(result >> (loop_i * 8U));
				}
			}
			if (cfg_flag == 0xFEU) {
				ret = himax_bus_write(
					reg_cmd[0], w_data, byte_length,
					HIMAX_I2C_RETRY_TIMES);
			} else if (cfg_flag == (uint8_t)(IC_MASTER)) {

				if (reg_cmd[3] != 0x30U) {
					tmp_addr_32 = ((uint32_t) reg_cmd[3] << 24);
					tmp_addr_32 += ((uint32_t) reg_cmd[2] << 16);
					tmp_addr_32 += ((uint32_t) reg_cmd[1] << 8);
					tmp_addr_32 += reg_cmd[0];
					himax_mcu_register_write(tmp_addr_32,
						byte_length, w_data);
				} else {
					himax_mcu_write_dd_reg_password(cfg_flag);
					dd_addr = (reg_cmd[2] << 0x04) | (reg_cmd[1] >> 0x04);
					bank = reg_cmd[1] & 0x0FU;
					pa_num = reg_cmd[0];
					D("dd_addr:%02X,bank:%02X,pa_num:%02X\n", dd_addr, bank, pa_num);
					himax_mcu_dd_reg_write(dd_addr,
							pa_num, byte_length, w_data, bank, IC_MASTER);
					himax_mcu_clear_dd_reg_password(cfg_flag);
				}
			} else {
				if (reg_cmd[3] != 0x30U) {
					tmp_addr_32 = ((uint32_t) reg_cmd[3] << 24);
					tmp_addr_32 += ((uint32_t) reg_cmd[2] << 16);
					tmp_addr_32 += ((uint32_t) reg_cmd[1] << 8);
					tmp_addr_32 += reg_cmd[0];
					himax_mcu_register_write_slave(cfg_flag,
						tmp_addr_32, byte_length, w_data);
				} else {
					himax_mcu_write_dd_reg_password(cfg_flag);
					dd_addr = (reg_cmd[2] << 0x04) | (reg_cmd[1] >> 0x04);
					bank = reg_cmd[1] & 0x0FU;
					pa_num = reg_cmd[0];
					D("dd_addr:%02X,bank:%02X,pa_num:%02X\n", dd_addr, bank, pa_num);
					himax_mcu_dd_reg_write(dd_addr,
							pa_num, byte_length, w_data, bank, cfg_flag);
					himax_mcu_clear_dd_reg_password(cfg_flag);
				}
			}
		} else {
			W("Undefined behavior\n");
			ret = HX_FAIL;
		}
		ret = (int)len;
		kfree(buf_backup);
	}
	return ret;
}

ssize_t himax_reset_write(char *buf, size_t len)
{
	uint8_t tmp_data[4] = { 0 };

	if (len >= 12U) {
		D("%s: no command exceeds 12 chars.\n", __func__);
		return -EFAULT;
	}
	switch (buf[0]) {
#if (HX_RST_PIN_FUNC == 0x01)
	case '1':
		himax_mcu_hw_reset(false);
		break;
	case '2':
		himax_mcu_hw_reset(true);
		break;
#endif
	case '5':
		himax_mcu_system_reset();
		himax_mcu_register_read(addr_flag_reset_event, DATA_LEN_4,
					tmp_data);
		D("%s: Read 0x900000E4 with value 0x%02X.\n", __func__,
		  tmp_data[0]);
		break;
	case '6':
		himax_mcu_command_reset();
		himax_mcu_register_read(addr_flag_reset_event, DATA_LEN_4,
					tmp_data);
		D("%s: Read 0x900000E4 with value 0x%02X.\n", __func__,
		  tmp_data[0]);
		break;
	default:
		W("%s: Undefined behavior\n", __func__);
		break;
	}

	return len;
}

ssize_t himax_diag_arrange_write(char *buf, size_t len)
{
	int value = 0;

	if (len >= 80U) {
		D("%s: no command exceeds 80 chars.\n", __func__);
		return -EFAULT;
	}
	if (!kstrtoint(buf, 0, &value)) {
		g_diag_arr_num = (uint8_t)value;
	} else {
		W("[FAIL]kstrtoul\n");
	}
	D("%s: g_diag_arr_num = %d\n", __func__, g_diag_arr_num);
	return len;
}

ssize_t himax_diag_cmd_write(char *buf, size_t len)
{
	struct himax_ts_data *ts = private_ts;
	int input_cmd = 0;
	uint8_t timeout_cnt = 0;

	/*Old version*/
	if (len <= 2U) {
		if (!kstrtoint(buf, 16, &input_cmd)) {
			ts->diag_cmd = (uint8_t)input_cmd;
			if (ts->diag_cmd <= 0x03U) {
				process_type = (uint8_t)Tch_mot_flag;
			} else if ((ts->diag_cmd >= 0x11U) && (ts->diag_cmd <= 0x1FU)) {
				process_type = (uint8_t)Dsram_delivery;
				ts->diag_cmd &= 0x0FU;
				if (ts->diag_cmd <= 0x03U) {
					ts->diag_cmd += 0x08U;
				}
			} else {
				W("%s: Wrong diag cmd!\n", __func__);
				return -EFAULT;
			}
			D("%s: Set process_type = 0x%02X\n", __func__, process_type);
		}
	}
	/*New version*/
	if (len == 3U) {
		if ((buf[0] == 'x') || (buf[0] == 'X')) {
			if (!kstrtoint(&buf[1], 16, &input_cmd)) {
				ts->diag_cmd = (uint8_t)input_cmd;
				if (ts->diag_cmd <= 0x03U) {
					process_type = (uint8_t)Tch_mot_flag;
				} else if ((ts->diag_cmd >= 0x08U) && (ts->diag_cmd <= 0x11U)) {
					process_type = (uint8_t)Dsram_delivery;
				} else {
					/* do nothing*/
				}
				D("%s: Set process_type = 0x%02X\n", __func__, process_type);
			}
		} else {
			W("%s: Wrong diag cmd!\n", __func__);
			return -EFAULT;
		}
	}
	if ((pre_diag_cmd != ts->diag_cmd) && (diag_wq_alive)) {
		while (diag_wq_alive == true) {
			usleep_range(10000, 11000);
			timeout_cnt++;
			if (timeout_cnt > 20U) {
				break;
			}
		}

		cancel_delayed_work_sync(&ts->himax_diag_delay_wrok);
		diag_wq_alive = false;
		himax_mcu_stop_DSRAM_output();
	}
	if (process_type == (uint8_t)Tch_mot_flag) {
		himax_int_enable(1);
		himax_mcu_diag_register_set(ts->diag_cmd, IC_MASTER);
		D("%s: Raw Data Delivery (Touch Monitor)\n", __func__);
	} else if (process_type == (uint8_t)Dsram_delivery) {
		himax_mcu_diag_register_set(
			ts->diag_cmd, IC_MASTER);
		if (!diag_wq_alive) {
			/*Start wrok queue*/
			himax_int_enable(0);
			queue_delayed_work(ts->himax_diag_wq,
					   &ts->himax_diag_delay_wrok,
					   2 * HZ / 100);
			diag_wq_alive = true;
			D("%s: Raw Data Delivery (DSRAM)\n", __func__);
		}
	} else {
		/* do nothing*/
	}
	D("%s: Set raw out select 0x%02X.\n", __func__, ts->diag_cmd);
	pre_diag_cmd = ts->diag_cmd;
	return len;
}

ssize_t himax_GMA_cmd_write(char *buf, size_t len)
{
	const struct firmware *fw = NULL;
	int ret = 0;
	D("%s : %s\n", __func__, buf);

	if (len >= 80U) {
		D("%s: no command exceeds 80 chars.\n", __func__);
		ret = -EFAULT;
	} else {
		if (strcmp(buf, "AGMA") == 0) {
			ret = request_firmware(&fw, "AGMA.txt", private_ts->dev);
		} else if (strcmp(buf, "DGMA") == 0) {
			ret = request_firmware(&fw, "DGMA.txt", private_ts->dev);
		} else if (strcmp(buf, "VCOM") == 0) {
			ret = request_firmware(&fw, "VCOM.txt", private_ts->dev);
		} else {
			E("Input string is incorrect.\n");
			ret = -EFAULT;
		}

		if (ret < 0) {
			E("%s: Open GMA file failed\n", __func__);
		} else {
			D("%s: fw->size: %d\n", __func__, fw->size);
			ret = himax_convert_GMA_data(fw->data,
										(size_t)fw->size, buf[0]);
			if (ret < 0) {
				E("%s: himax_convert_GMA_data failed!\n", __func__);
			} else {
				himax_burn_GMA_to_flash((const u8 *)g_gma_buf, buf[0]);
				ret = (int)len;
			}
			release_firmware(fw);
			kfree(g_gma_buf);
			g_gma_buf = NULL;
		}
	}

	return ret;
}

int himax_vendor_show(struct seq_file *s, void *v)
{
	ssize_t ret = 0;
	uint16_t cid = 0;

	UNUSED(v);

	himax_mcu_config_reload_enable();
	himax_mcu_power_on_init();
	himax_mcu_read_FW_ver();
	g_core_fp.fp_touch_information();

	seq_printf(s, "IC = %s\n", private_ts->chip_name);

	seq_printf(s, "FW Architecture Version = 0x%2.2X\n",
		ic_data->vendor_arch_ver);
#if defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX8530)
	seq_printf(s, "FW Algorithm Version = A%02X\n",
		ic_data->vendor_display_cfg_ver);
#else
	seq_printf(s, "FW Display Config Version = D%02X\n",
		ic_data->vendor_display_cfg_ver);
#endif
	seq_printf(s, "FW Touch Config Version = C%02X\n",
		ic_data->vendor_touch_cfg_ver);

	cid = ((uint16_t)ic_data->vendor_cid_maj_ver << 8);
	cid += ic_data->vendor_cid_min_ver;
	seq_printf(s, "CID = 0x%2.2X\n", cid);


	seq_printf(s, "Panel Version = 0x%2.2X\n",
		ic_data->vendor_panel_ver);


	if (ic_data->vendor_arch_ver >= 0x8098U) {
		seq_printf(s, "Remark 1 : %s\n", ic_data->vendor_remark1);
		seq_printf(s, "Remark 2 : %s\n", ic_data->vendor_remark2);
		seq_printf(s, "Himax Ticket : %s\n", ic_data->vendor_ticket);
	}

	seq_printf(s, "Customer = %s\n", ic_data->vendor_cus_info);
	seq_printf(s, "Project = %s\n", ic_data->vendor_proj_info);
	seq_printf(s, "FW Config Date  = %s\n", ic_data->vendor_config_date);

	seq_puts(s, "\n");
	seq_puts(s, "Himax Touch Driver Version:\n");
	seq_printf(s, "%s\n", HIMAX_DRIVER_VER);

	return ret;
}
int himax_vendor_open(struct inode *inode, struct file *file)
{
	UNUSED(inode);
	return single_open(file, himax_vendor_show, NULL);
}
const struct proc_ops_name himax_vendor_ops = {
	.proc_op(open) = himax_vendor_open,
	.proc_op(read) = seq_read,
};


int himax_pintest_show(struct seq_file *s, void *v)
{
	ssize_t ret = 0;
	bool flag = true;

	UNUSED(v);

#if (HX_RST_PIN_FUNC == 0x01)
	himax_gpio_set(private_ts->rst_gpio, 0);
	usleep_range(1000, 1100);
	if (gpio_get_value(private_ts->rst_gpio) == 1) {
		flag = false;
		seq_puts(s, "TP_EXT_RSTN pin Low: Fail!\n");
	} else {
		seq_puts(s, "TP_EXT_RSTN pin Low: Pass!\n");
	}

#endif

	himax_gpio_set(private_ts->lcm_gpio, 0);

	if (gpio_get_value(private_ts->lcm_gpio) == 1) {
		flag = false;
		seq_puts(s, "RESX pin Low: Fail!\n");
	} else {
		seq_puts(s, "RESX pin Low: Pass!\n");
	}

	himax_gpio_set(private_ts->pon_gpio, 0);

	if (gpio_get_value(private_ts->pon_gpio) == 1) {
		flag = false;
		seq_puts(s, "PON pin Low: Fail!\n");
	} else {
		seq_puts(s, "PON pin Low: Pass!\n");
	}

	usleep_range(6000, 6100);

	himax_gpio_set(private_ts->lcm_gpio, 1);

	if (gpio_get_value(private_ts->lcm_gpio) == 0) {
		flag = false;
		seq_puts(s, "RESX pin High: Fail!\n");
	} else {
		seq_puts(s, "RESX pin High: Pass!\n");
	}

	usleep_range(1000, 1100);

#if (HX_RST_PIN_FUNC == 0x01)
	himax_gpio_set(private_ts->rst_gpio, 1);
	usleep_range(1000, 1100);
	if (gpio_get_value(private_ts->rst_gpio) == 0) {
		flag = false;
		seq_puts(s, "TP_EXT_RSTN pin High: Fail!\n");
	} else {
		seq_puts(s, "TP_EXT_RSTN pin High: Pass!\n");
	}
#endif

	msleep(95);

	himax_gpio_set(private_ts->pon_gpio, 1);

	if (gpio_get_value(private_ts->pon_gpio) == 0) {
		flag = false;
		seq_puts(s, "PON pin High: Fail!\n");
	} else {
		seq_puts(s, "PON pin High: Pass!\n");
	}

	seq_puts(s, "\n");
	if (flag) {
		seq_puts(s, "Himax pintest function pass!\n");
	} else {
		seq_puts(s, "Himax pintest function fail!\n");
	}

	return ret;
}
int himax_pintest_open(struct inode *inode, struct file *file)
{
	UNUSED(inode);
	return single_open(file, himax_pintest_show, NULL);
}
const struct proc_ops_name himax_pintest_ops = {
	.proc_op(open) = himax_pintest_open,
	.proc_op(read) = seq_read,
};

int himax_set_diag_cmd(struct himax_ic_data *ic_data_tmp,
		       struct himax_report_data *hx_touch_data_tmp)
{
	struct himax_ts_data *ts = private_ts;
	int16_t *mutual_data;
	int16_t *self_data;
	uint16_t mul_num;
	uint16_t self_num;
	/* int RawDataLen = 0; */
	hx_touch_data_tmp->diag_cmd = ts->diag_cmd;

	if ((hx_touch_data_tmp->diag_cmd >= 1U) && (hx_touch_data_tmp->diag_cmd <= 3U)) {
		/* Check event stack CRC */
		if (!himax_mcu_diag_check_sum(hx_touch_data_tmp)) {
			goto bypass_checksum_failed_packet;
		}

		mutual_data = getMutualBuffer();
		self_data = getSelfBuffer();
		/* initiallize the block number of mutual and self */
		mul_num = (uint16_t)ic_data_tmp->HX_RX_NUM * (uint16_t)ic_data_tmp->HX_TX_NUM;

		self_num = (uint16_t)ic_data_tmp->HX_RX_NUM + (uint16_t)ic_data_tmp->HX_TX_NUM;

		himax_mcu_diag_parse_raw_data(hx_touch_data_tmp, mul_num, self_num,
					      hx_touch_data_tmp->diag_cmd,
					      mutual_data, self_data);
	/*} else if (hx_touch_data_tmp->diag_cmd == 8) {
	 *	memset(diag_coor, 0x00, sizeof(diag_coor));
	 *	memcpy(&(diag_coor[0]), &hx_touch_data_tmp->hx_coord_buf[0],
	 *	       hx_touch_data_tmp->touch_info_size);
	 */
	}

	/* assign state info data */
	(void)memcpy(&(g_hx_state_info[0]), &hx_touch_data_tmp->hx_state_info[0], 2);
	return NO_ERR;
bypass_checksum_failed_packet:
	return 1;
}

/* #if defined(HX_DEBUG_LEVEL) */
static void himax_log_touch_data(int start)
{
	unsigned int loop_i = 0;
	unsigned int print_size = 0;
	uint8_t *buf = NULL;

	if (start == 1) {
		return; /* report data when end of ts_work*/
	}

	if (hx_touch_data->diag_cmd > 0U) {
		print_size = hx_touch_data->touch_all_size;
		buf = kcalloc(print_size, sizeof(uint8_t), GFP_KERNEL);
		if (buf == NULL) {
			E("%s, Failed to allocate memory\n", __func__);
			return;
		}

		(void)memcpy(buf, hx_touch_data->hx_coord_buf,
		       hx_touch_data->touch_info_size);
		(void)memcpy(&buf[hx_touch_data->touch_info_size],
		       hx_touch_data->hx_rawdata_buf,
		       print_size - hx_touch_data->touch_info_size);
	} else if (hx_touch_data->diag_cmd == 0U) {
		print_size = hx_touch_data->touch_info_size;
		buf = kcalloc(print_size, sizeof(uint8_t), GFP_KERNEL);
		if (buf == NULL) {
			E("%s, Failed to allocate memory\n", __func__);
			return;
		}

		(void)memcpy(buf, hx_touch_data->hx_coord_buf, print_size);
	} else {
		E("%s:cmd fault\n", __func__);
		return;
	}

	for (loop_i = 0U; loop_i < print_size; loop_i += 8U) {
		if ((loop_i + 7U) >= print_size) {
			D("P %2d = 0x%2.2X P %2d = 0x%2.2X ", loop_i,
			  buf[loop_i], loop_i + 1U, buf[loop_i + 1U]);
			D("P %2d = 0x%2.2X P %2d = 0x%2.2X\n", loop_i + 2U,
			  buf[loop_i + 2U], loop_i + 3U, buf[loop_i + 3U]);
			break;
		}

		D("P %2d = 0x%2.2X P %2d = 0x%2.2X ", loop_i, buf[loop_i],
		  loop_i + 1U, buf[loop_i + 1U]);
		D("P %2d = 0x%2.2X P %2d = 0x%2.2X ", loop_i + 2U,
		  buf[loop_i + 2U], loop_i + 3U, buf[loop_i + 3U]);
		D("P %2d = 0x%2.2X P %2d = 0x%2.2X ", loop_i + 4U,
		  buf[loop_i + 4U], loop_i + 5U, buf[loop_i + 5U]);
		D("P %2d = 0x%2.2X P %2d = 0x%2.2X ", loop_i + 6U,
		  buf[loop_i + 6U], loop_i + 7U, buf[loop_i + 7U]);
		D("\n");
	}
	kfree(buf);
	buf = NULL;
}

#define PRT_LOG "Finger %d=> X:%d, Y:%d W:%d, Z:%d\n"
static void himax_log_touch_event(struct himax_ts_data *ts, int start)
{
	unsigned int loop_i = 0;

	if (start == 1) {
		return; /*report data when end of ts_work*/
	}

	if ((g_target_report_data->finger_on > 0U) &&
	    (g_target_report_data->finger_num > 0U)) {
		for (loop_i = 0U; loop_i < ts->nFinger_support; loop_i++) {
			if ((g_target_report_data->x[loop_i] <
				    ts->pdata->abs_x_max) &&
			    (g_target_report_data->y[loop_i] <
				    ts->pdata->abs_y_max)) {
				I(PRT_LOG, loop_i + 1U,
				  g_target_report_data->x[loop_i],
				  g_target_report_data->y[loop_i],
				  g_target_report_data->w[loop_i],
				  g_target_report_data->w[loop_i]);
			}
		}
	}
	if ((g_target_report_data->finger_on == 0U) &&
		   (g_target_report_data->finger_num == 0U)) {
		D("All Finger leave\n");
	}
}
static void himax_log_touch_int_devation(int touched)
{
	if (touched == HX_FINGER_ON) {
		time_func(&g_timeStart);
		/* D(" Irq start time = %ld.%06ld s\n",
		 * g_timeStart.tv_sec, g_timeStart.tv_nsec/1000);
		 */
	} else if (touched == HX_FINGER_LEAVE) {
		time_func(&g_timeEnd);
		g_timeDelta = time_diff(g_timeStart, g_timeEnd);
		/*  D("Irq finish time = %ld.%06ld s\n",
		 *	g_timeEnd.tv_sec, g_timeEnd.tv_nsec/1000);
		 */
		D("Touch latency = %ld us\n", g_timeDelta.tv_nsec / 1000);
		D("bus_speed = %d kHz\n", private_ts->bus_speed);
		if ((g_target_report_data->finger_on == 0U) &&
		    (g_target_report_data->finger_num == 0U)) {
			 D("All Finger leave\n");
		}
	} else {
		/* Undefined behavior*/
		D("%s : wrong input!\n", __func__);
	}
}

#define RAW_DOWN_STATUS "status: Raw:F:%02d Down, X:%d, Y:%d, W:%d\n"
#define RAW_UP_STATUS "status: Raw:F:%02d Up, X:%d, Y:%d\n"

static void himax_log_touch_event_detail(struct himax_ts_data *ts, int start)
{
	uint32_t loop_i = 0;

	if (start == HX_FINGER_LEAVE) {
		for (loop_i = 0; loop_i < ts->nFinger_support; loop_i++) {
			if ((((ts->old_finger >> loop_i) & 1U) == 0U) &&
			    (((ts->pre_finger_mask >> loop_i) & 1U) == 1U)) {
				if ((g_target_report_data->x[loop_i] <
					    ts->pdata->abs_x_max) &&
				    (g_target_report_data->y[loop_i] <
					    ts->pdata->abs_y_max)) {
					I(RAW_DOWN_STATUS, loop_i + 1U,
					  g_target_report_data->x[loop_i],
					  g_target_report_data->y[loop_i],
					  g_target_report_data->w[loop_i]);
				}
			} else if (((((ts->old_finger >> loop_i) & 1U) == 1U) &&
				    (((ts->pre_finger_mask >> loop_i) & 1U) ==
				     0U))) {
				I(RAW_UP_STATUS, loop_i + 1U,
				  ts->pre_finger_data[loop_i][0],
				  ts->pre_finger_data[loop_i][1]);
			} else {
				/* D("dbg hx_point_num=%d, old_finger=0x%02X,"
				 * " pre_finger_mask=0x%02X\n",
				 * ts->hx_point_num, ts->old_finger,
				 * ts->pre_finger_mask);
				 */
			}
		}
	}
}

void himax_ts_dbg_func(struct himax_ts_data *ts, int start)
{
	if ((ts->debug_log_level & BIT(0)) != 0U) {
		/* D("debug level 1\n"); */
		himax_log_touch_data(start);
	}
	if ((ts->debug_log_level & BIT(1)) != 0U) {
		/* D("debug level 2\n"); */
		himax_log_touch_event(ts, start);
	}
	if ((ts->debug_log_level & BIT(2)) != 0U) {
		/* D("debug level 4\n"); */
		himax_log_touch_int_devation(start);
	}
	if ((ts->debug_log_level & BIT(3)) != 0U) {
		/* D("debug level 8\n"); */
		himax_log_touch_event_detail(ts, start);
	}
}

static void himax_himax_data_init(void)
{
	debug_data->fp_ts_dbg_func = himax_ts_dbg_func;
	debug_data->fp_set_diag_cmd = himax_set_diag_cmd;
}

static void himax_get_mutual_edge(void)
{
	uint32_t i = 0;
	uint32_t mutual_val = (uint32_t)ic_data->HX_RX_NUM
							* (uint32_t)ic_data->HX_TX_NUM;

	for (i = 0; i < mutual_val; i++) {
		if (diag_mutual[i] > g_max_mutual) {
			g_max_mutual = diag_mutual[i];
		}

		if (diag_mutual[i] < g_min_mutual) {
			g_min_mutual = diag_mutual[i];
		}
	}
}

static void himax_get_self_edge(void)
{
	uint16_t i = 0;
	uint16_t self_val = (uint16_t)ic_data->HX_RX_NUM
						+ (uint16_t)ic_data->HX_TX_NUM;

	for (i = 0; i < self_val; i++) {
		if (diag_self[i] > g_max_self) {
			g_max_self = diag_self[i];
		}

		if (diag_self[i] < g_min_self) {
			g_min_self = diag_self[i];
		}
	}
}

static void print_state_info(struct seq_file *s)
{
	/* seq_printf(s, "State_info_2bytes:%3d, %3d\n",
	 * _state_info[0],hx_state_info[1]);
	 */
	seq_printf(s, "ReCal = %d\t", g_hx_state_info[0] & 0x01U);
	seq_printf(s, "Palm = %d\t", (g_hx_state_info[0] >> 1U) & 0x01U);
	seq_printf(s, "AC mode = %d\t", (g_hx_state_info[0] >> 2U) & 0x01U);
	seq_printf(s, "Water = %d\n", (g_hx_state_info[0] >> 3U) & 0x01U);
	seq_printf(s, "Glove = %d\t", (g_hx_state_info[0] >> 4U) & 0x01U);
	seq_printf(s, "TX Hop = %d\t", (g_hx_state_info[0] >> 5U) & 0x01U);
	seq_printf(s, "Base Line = %d\t", (g_hx_state_info[0] >> 6U) & 0x01U);
	seq_printf(s, "OSR Hop = %d\t", (g_hx_state_info[1] >> 3U) & 0x01U);
	seq_printf(s, "KEY = %d\n", (g_hx_state_info[1] >> 4U) & 0x0FU);
}
#if (HIMAX_LTDI_CONFIG == 0x00)
static void himax_diag_arrange_print(struct seq_file *s, int i, int j,
				     bool transpose)
{
	if (transpose) {
		seq_printf(s, "%6d", diag_mutual[j + (i * (int)ic_data->HX_RX_NUM)]);
	} else {
		seq_printf(s, "%6d", diag_mutual[i + (j * (int)ic_data->HX_RX_NUM)]);
	}
}

/* ready to print second step which is column*/
static void himax_diag_arrange_inloop(struct seq_file *s, int in_init,
				      int out_init, bool transpose, int j)
{
	int x_channel = (int) ic_data->HX_RX_NUM;
	int y_channel = (int) ic_data->HX_TX_NUM;
	int i;
	int in_max = 0;

	if (transpose) {
		in_max = y_channel;
	} else {
		in_max = x_channel;
	}

	if (in_init > 0) { /* bit0 = 1 */
		for (i = in_init - 1; i >= 0; i--) {
			himax_diag_arrange_print(s, i, j, transpose);
		}

		if (transpose) {
			if (out_init > 0) {
				seq_printf(s, " %5d\n", diag_self[j]);
			} else {
				seq_printf(s, " %5d\n",
					   diag_self[x_channel - j - 1]);
			}
		}
	} else { /* bit0 = 0 */
		for (i = 0; i < in_max; i++) {
			himax_diag_arrange_print(s, i, j, transpose);
		}

		if (transpose) {
			if (out_init > 0) {
				seq_printf(s, " %5d\n",
					   diag_self[x_channel - j - 1]);
			} else {
				seq_printf(s, " %5d\n", diag_self[j]);
			}
		}
	}
}

/* print first step which is row */
static void himax_diag_arrange_outloop(struct seq_file *s, bool transpose,
				       int out_init, int in_init)
{
	int j;
	int x_channel = (int)ic_data->HX_RX_NUM;
	int y_channel = (int)ic_data->HX_TX_NUM;
	int out_max = 0;
	int self_cnt = 0;
	int base = out_init - 1;

	if (transpose) {
		out_max = x_channel;
	} else {
		out_max = y_channel;
	}

	if (out_init > 0) { /* bit1 = 1 */
		self_cnt = 1;

		for (j = base; j >= 0; j--) {
			seq_printf(s, "%3c%02d%c", '[', j + 1, ']');
			himax_diag_arrange_inloop(s, in_init, out_init,
						  transpose, j);

			if (!transpose) {
				seq_printf(s, " %5d\n",
					   diag_self[y_channel + x_channel -
						     self_cnt]);
				self_cnt++;
			}
		}
	} else { /* bit1 = 0 */
		/* self_cnt = x_channel; */
		for (j = 0; j < out_max; j++) {
			seq_printf(s, "%3c%02d%c", '[', j + 1, ']');
			himax_diag_arrange_inloop(s, in_init, out_init,
						  transpose, j);

			if (!transpose) {
				seq_printf(s, " %5d\n",
					   diag_self[j + x_channel]);
			}
		}
	}
}
#endif
/* determin the output format of diag */
static void himax_diag_arrange(struct seq_file *s)
{

#if (HIMAX_LTDI_CONFIG == 0x00)
	bool bit2 = false;
	int bit1 = 0;
	int bit0 = 0;
#endif
	int x_channel = (int)ic_data->HX_RX_NUM;
	int y_channel = (int)ic_data->HX_TX_NUM;
	int i;
#if (HIMAX_LTDI_CONFIG == 0x01)
	int j;
	int data_frame_size = 0;
	int chip_id_sel = 0;

	x_channel = (int)ic_data->HX_CHIP_RX_MAX;
	y_channel = (int)ic_data->HX_CHIP_TX_MAX;
	data_frame_size = x_channel * y_channel;

	seq_printf(s, "%6c", ' ');
	for (j = 0; j < (int)ic_data->HX_RX_NUM; j++) {
		seq_printf(s, "%3c%02d%c", '[', ((j % x_channel) + 1), ']');
	}

	seq_puts(s, "\n");
	for (i = 0; i < (int)ic_data->HX_TX_NUM; i++) {
		seq_printf(s, "%3c%02d%c", '[', ((i % y_channel) + 1), ']');
		for (j = 0; j < (int)ic_data->HX_RX_NUM; j++) {
			chip_id_sel = j / x_channel;
			seq_printf(s, "%6d", diag_mutual[(j % x_channel) + (i * x_channel) + (chip_id_sel * data_frame_size)]);
			/*seq_printf(s, "%6d", diag_mutual[i + (j * ic_data->HX_TX_NUM)]);*/
		}
		seq_puts(s, "\n");
	}
#else
	/* rotate bit */
	if ((g_diag_arr_num & 0x04U) == 0x04U) {
		bit2 = true;
	}
	/* reverse Y */
	if ((g_diag_arr_num & 0x02U) == 0x02U) {
		bit1 = 1;
	}
	/* reverse X */
	if ((g_diag_arr_num & 0x01U) == 0x01U) {
		bit0 = 1;
	}

	if (g_diag_arr_num < 4U) {
		for (i = 0; i <= x_channel; i++) {
			seq_printf(s, "%3c%02d%c", '[', i, ']');
		}

		seq_puts(s, "\n");
		himax_diag_arrange_outloop(s, bit2, bit1 * y_channel,
					   bit0 * x_channel);
		seq_printf(s, "%6c", ' ');

		if (bit0 == 1) {
			for (i = x_channel - 1; i >= 0; i--) {
				seq_printf(s, "%6d", diag_self[i]);
			}
		} else {
			for (i = 0; i < x_channel; i++) {
				seq_printf(s, "%6d", diag_self[i]);
			}
		}
	} else {
		for (i = 0; i <= y_channel; i++) {
			seq_printf(s, "%3c%02d%c", '[', i, ']');
		}

		seq_puts(s, "\n");
		himax_diag_arrange_outloop(s, bit2, bit1 * x_channel,
					   bit0 * y_channel);
		seq_printf(s, "%6c", ' ');

		if (bit1 == 1) {
			for (i = y_channel; i > 0; i--) {
				seq_printf(s, "%6d", diag_self[i + x_channel - 1]);
			}
		} else {
			for (i = x_channel; i < (x_channel + y_channel); i++) {
				seq_printf(s, "%6d", diag_self[i]);
			}
		}
	}
#endif
}

/* DSRAM thread */
bool himax_ts_diag_func(void)
{
	unsigned int i = 0;
	unsigned int j = 0;
	unsigned int index = 0;
	unsigned int x_channel = ic_data->HX_RX_NUM;
	unsigned int y_channel = ic_data->HX_TX_NUM;
	unsigned int total_size = ((y_channel * x_channel) + y_channel + x_channel) * 2U;
	uint8_t *tmp_rawdata = NULL;
	int16_t *mutual_data = NULL;
	int16_t *self_data = NULL;
	int16_t raw_data;
	uint16_t raw_data_tmp16;
	/* 1:common dsram,2:100 frame Max,3:N-(N-1)frame */
	struct himax_ts_data *ts = private_ts;

	tmp_rawdata = kcalloc(total_size, sizeof(uint8_t), GFP_KERNEL);
	if (tmp_rawdata == NULL) {
		E("%s: Failed to allocate memory\n", __func__);
		return false;
	}
	(void)memset(tmp_rawdata, 0U, total_size * sizeof(uint8_t));

	D("%s: process type=%d,  pre_diag_cmd : %d\n", __func__, process_type, pre_diag_cmd);
	if (process_type <= 2U) {
		mutual_data = getMutualBuffer();
		self_data = getSelfBuffer();
	} else {
		/* do nothing*/
	}

	if (g_core_fp.fp_get_DSRAM_data(tmp_rawdata) != true) {
		E("%s: Get DSRAM data failed\n", __func__);
		kfree(tmp_rawdata);
		tmp_rawdata = NULL;
		goto END_FUNC;
	}

	index = 0;
	for (i = 0; i < y_channel; i++) { /*mutual data*/
		for (j = 0; j < x_channel; j++) {
			raw_data_tmp16 = (uint16_t) tmp_rawdata[index + 1U] * 256U;
			raw_data_tmp16 += (uint16_t) tmp_rawdata[index];
			raw_data = (int16_t) raw_data_tmp16;
			if (process_type <= (uint8_t)Dsram_delivery) {
				mutual_data[(i * x_channel) + j] = raw_data;
			} else if (process_type == 2U) { /* Keep max data */
				if (mutual_data[(i * x_channel) + j] < raw_data) {
					mutual_data[(i * x_channel) + j] =
						raw_data;
				}
			} else {
				/* do nothing*/
			}
			index += 2U;
		}
	}

	for (i = 0; i < (x_channel + y_channel); i++) { /*self data*/
	raw_data_tmp16 = (uint16_t) tmp_rawdata[index + 1U] << 8U;
			raw_data_tmp16 += (uint16_t) tmp_rawdata[index];
			raw_data = (int16_t) raw_data_tmp16;
		if (process_type <= (uint8_t)Dsram_delivery) {
			self_data[i] = raw_data;
		} else if (process_type == 2U) { /* Keep max data */
			if (self_data[i] < raw_data) {
				self_data[i] = raw_data;
			}
		} else {
			/* do nothing*/
		}
		index += 2U;
	}

	kfree(tmp_rawdata);
	tmp_rawdata = NULL;

END_FUNC:

	diag_max_cnt++;
	if ((process_type >= (uint8_t)Dsram_delivery) && (ts->diag_cmd > 0U)
		&& (pre_diag_cmd == ts->diag_cmd)) {
		queue_delayed_work(private_ts->himax_diag_wq,
				   &private_ts->himax_diag_delay_wrok,
				   1 / 10 * HZ);
	} else {
		diag_wq_alive = false;
	}
	return true;
}

static void himax_diag_print(struct seq_file *s, void *v)
{
	unsigned int x_num = ic_data->HX_RX_NUM;
	unsigned int y_num = ic_data->HX_TX_NUM;

	UNUSED(v);

	seq_printf(s, "ChannelStart: %4d, %4d\n\n", x_num, y_num);

	/*	start to show out the raw data in adb shell */
	himax_diag_arrange(s);
	seq_puts(s, "\n");
	seq_puts(s, "ChannelEnd");
	seq_puts(s, "\n");

	/* print Mutual/Slef Maximum and Minimum */
	himax_get_mutual_edge();
	himax_get_self_edge();
	seq_printf(s, "Mutual Max:%3d, Min:%3d\n", g_max_mutual, g_min_mutual);
	seq_printf(s, "Self Max:%3d, Min:%3d\n", g_max_self, g_min_self);
	/* recovery status after print*/
	g_max_mutual = 0;
	g_min_mutual = 0xFFFF;
	g_max_self = 0;
	g_min_self = 0xFFFF;

	/*pring state info*/
	print_state_info(s);

	if (s->count >= s->size) {
		h_overflow = true;
	}
}

int himax_stack_show(struct seq_file *s, void *v)
{
	struct himax_ts_data *ts = private_ts;

	if (ts->diag_cmd != 0U) {
		himax_diag_print(s, v);
	} else {
		seq_puts(s,
			 "Please set raw out select 'echo diag,X > debug'\n\n");
	}

	return 0;
}
int himax_stack_open(struct inode *inode, struct file *file)
{
	UNUSED(inode);
	return single_open(file, himax_stack_show, NULL);
}
const struct proc_ops_name himax_stack_ops = {
	.proc_op(open) = himax_stack_open,
	.proc_op(read) = seq_read,
};

int himax_sram_read(struct seq_file *s, void *v, uint8_t raw_data_sel)
{
	struct himax_ts_data *ts = private_ts;
	uint8_t datatype = 0;

	if (!ts->diag_cmd) {
		datatype = raw_data_sel;
	} else {
		datatype = ts->diag_cmd;
	}

	if (h_overflow == false) {
		if (process_type == 0U) {
			himax_int_enable(0);
			himax_mcu_diag_register_set(datatype, (uint8_t) IC_MASTER);

			if (!himax_ts_diag_func()) {
				seq_puts(s, "Get sram data failed.");
			} else {
				himax_diag_print(s, v);
			}

			ts->diag_cmd = 0;
			himax_mcu_diag_register_set(0, IC_MASTER);
			himax_int_enable(1);
		}
	}

	if (((process_type <= 3U) && (ts->diag_cmd != 0U) && diag_wq_alive)
		|| h_overflow) {
		himax_diag_print(s, v);
		h_overflow = false;
	}

	return 0;
}

int himax_delta_show(struct seq_file *s, void *v)
{
	return himax_sram_read(s, v, 0x09);
}
int himax_delta_open(struct inode *inode, struct file *file)
{
	UNUSED(inode);
	return single_open(file, himax_delta_show, NULL);
}
const struct proc_ops_name himax_delta_ops = {
	.proc_op(open) = himax_delta_open,
	.proc_op(read) = seq_read,
};


int himax_dc_show(struct seq_file *s, void *v)
{
	return himax_sram_read(s, v, 0x0A);
}
int himax_dc_open(struct inode *inode, struct file *file)
{
	UNUSED(inode);
	return single_open(file, himax_dc_show, NULL);
}
const struct proc_ops_name himax_dc_ops = {
	.proc_op(open) = himax_dc_open,
	.proc_op(read) = seq_read,
};


int himax_baseline_show(struct seq_file *s, void *v)
{
	return himax_sram_read(s, v, 0x08);
}
int himax_baseline_open(struct inode *inode, struct file *file)
{
	UNUSED(inode);
	return single_open(file, himax_baseline_show, NULL);
}
const struct proc_ops_name himax_baseline_ops = {
	.proc_op(open) = himax_baseline_open,
	.proc_op(read) = seq_read,
};


static void setFlashBuffer(void)
{
	g_flash_buffer = kcalloc(g_Flash_Size, sizeof(uint8_t), GFP_KERNEL);
	if (g_flash_buffer == NULL) {
		E("%s, Failed to allocate memory\n", __func__);
		return;
	}
}

int himax_flash_dump_show(struct seq_file *s, void *v)
{
	ssize_t ret = 0;
	unsigned int i;

	UNUSED(v);

	if (!g_flash_dump_rst) {
		seq_puts(s, "Flash Dump - Failed ever\n");
		return ret;
	}

	if (g_flash_progress == (uint8_t)Dump_Ongoing) {
		seq_puts(s, "Flash dump - On-going\n");
	} else {
		seq_puts(s, "Flash dump Service - Available\n");
	}

	/*print flash dump data*/
	if ((g_flash_cmd == 1U) && (g_flash_progress == (uint8_t)(Dump_Finished))) {
		seq_puts(s, "Print flash data\n");
		for (i = 0; i < g_Flash_Size; i++) {
			seq_printf(s, "0x%02X,", g_flash_buffer[i]);
			if ((i % 16U) == 15U) {
				seq_puts(s, "\n");
			}
		}
	}

	return ret;
}

ssize_t himax_flash_dump_store(struct file *filp,
				      const char __user *buff, size_t len,
				      loff_t *data)
{
	char buf[80] = { 0 };
	char token[50] = { 0 };
	uint32_t tmp_start_addr = 0;
	uint32_t flash_size_limit = HX256K;
	unsigned int tmp_size = 0;
	unsigned int i = 0;
	unsigned int idx = 0;
	unsigned long value = 0;

	UNUSED(filp);
	UNUSED(data);

	if (ic_data->HX_FW_SIZE == FW_SIZE_255k) {
		flash_size_limit = HX512K;
	}

	if (len >= 80U) {
		D("%s: no command exceeds 80 chars.\n", __func__);
		return -EFAULT;
	}

	if (copy_from_user(buf, buff, len) != 0) {
		return -EFAULT;
	}

	D("%s: buf = %s\n", __func__, buf);

	if (g_flash_progress == (uint8_t)Dump_Ongoing) {
		E("%s: Flash dump - ONGOING\n", __func__);
		return len;
	}

	if ((buf[1] == '_') && (buf[2] == 'F')) {
		if ((buf[3] == 'U') && (buf[4] == 'L') && (buf[5] == 'L')) {
			g_page_prog_start = 0;
			g_Flash_Size = flash_size_limit;
		}
	} else if ((buf[1] == '_') && (buf[2] == '2')) {
		if (buf[3] == '8') {
			g_page_prog_start = 0;
			g_Flash_Size = FW_SIZE_128k;
		}
	} else {
		/* do nothing*/
	}
	/*1 : print data on console, 2 : dump to sdcard*/
	if (buf[0] == '1') {
		g_flash_cmd = 1;
	} else if (buf[0] == '2') {
		g_flash_cmd = 2;
	} else {
		/* do nothing*/
	}

	if ((buf[1] == '_') && ((buf[2] == 's') || (buf[2] == 'S'))) {
		for (i = 3U; i < (len - 1U); i++) {
			if (buf[i] == ':') {
				idx = i;
				D("%s: idx = %d\n", __func__, idx);
				break;
			}
		}
		if ((idx > 3U) && (idx != (len - 1U))) {
			for (i = 3U; i < idx; i++) {
				token[i-3U] = buf[i];
			}
			if (!kstrtoul(token, 16, &value)) {
				tmp_start_addr = (uint32_t) value;
			} else {
				E("[FAIL]hex token kstrtoul\n");
				return len;
			}
			(void)memset(token, 0, sizeof(token));
			idx += 1U;
			for (i = idx; i < (len - 1U); i++) {
				token[i-idx] = buf[i];
			}
			if (!kstrtoul(token, 10, &value)) {
				tmp_size = (uint32_t) value;
			} else {
				E("[FAIL]decimal token kstrtoul\n");
				return len;
			}
			if ((tmp_start_addr + (tmp_size * HX1K)) > flash_size_limit) {
				E("%s: dump size(start_addr + dump size) more than flash size\n", __func__);
				return len;
			}
			D("%s: start_addr = 0x%02X, dump_size = %dK\n", __func__, tmp_start_addr, tmp_size);
			g_Flash_Size = tmp_size * HX1K;
			g_page_prog_start = tmp_start_addr;
		}
	}
	g_flash_progress = Dump_Ongoing;
	queue_work(private_ts->flash_wq, &private_ts->flash_work);
	g_flash_dump_rst = true;

	return len;
}
int himax_flash_dump_open(struct inode *inode, struct file *file)
{
	UNUSED(inode);
	return single_open(file, himax_flash_dump_show, NULL);
}

const struct proc_ops_name himax_flash_dump_ops = {
	owner_line
	.proc_op(open) = himax_flash_dump_open,
	.proc_op(write) = himax_flash_dump_store,
	.proc_op(read) = seq_read,
	.proc_opl(lseek) = seq_lseek,
	.proc_op(release) = single_release,
};


int himax_SRAM_test_show(struct seq_file *s, void *v)
{
	ssize_t ret = 0;
	char fileName[128];
	int result = 0;
	const struct firmware *fw = NULL;

	UNUSED(v);

	(void)memset(fileName, 0, 128);
	/* parse the file name */
	snprintf(fileName, 128, SRAM_FW_FILE_NAME);
	I("%s: upgrade from file(%s) start!\n", __func__, fileName);
	result = request_firmware(&fw, fileName, private_ts->dev);
	if (result < 0) {
		seq_puts(s, "fail to request_firmware HX83192_SRAM_CHECK.bin\n");
		E("fail to request_firmware fwpath: %s (ret:%d)\n",
			fileName, result);
		return result;
	}
	if (himax_mcu_SRAM_test((unsigned char *)fw->data, fw->size) > 0) {
		seq_puts(s, "SRAM voltage test fail\n");
		E("%s: SRAM voltage test fail, line: %d\n", __func__,
			__LINE__);
		ret = -1;
	} else {
		seq_puts(s, "SRAM voltage test pass\n");
		I("%s: SRAM voltage test pass, line: %d\n", __func__,
			__LINE__);
	}

	release_firmware(fw);
	fw = NULL;

	return ret;
}


ssize_t himax_SRAM_test_store(struct file *filp,
				      const char __user *buff, size_t len,
				      loff_t *data)
{
	char buf[80] = { 0 };
	UNUSED(filp);
	UNUSED(data);

	if (len >= 80U) {
		D("%s: no command exceeds 80 chars.\n", __func__);
		return -EFAULT;
	}

	if (copy_from_user(buf, buff, len) != 0) {
		return -EFAULT;
	}

	D("%s: buf = %s\n", __func__, buf);

	I("%s: buf 1 for output voltage, buf 2 for inner gen voltage\n", __func__);

	if (buf[0] == '1')
		g_sram_cmd = 1;
	else if (buf[0] == '2')
		g_sram_cmd = 2;

	return len;
}

int himax_SRAM_test_open(struct inode *inode, struct file *file)
{
	UNUSED(inode);
	return single_open(file, himax_SRAM_test_show, NULL);
}

const struct proc_ops_name himax_SRAM_test_ops = {
	owner_line
	.proc_op(open) = himax_SRAM_test_open,
	.proc_op(write) = himax_SRAM_test_store,
	.proc_op(read) = seq_read,
	.proc_opl(lseek) = seq_lseek,
	.proc_op(release) = single_release,
};

int himax_debug_show(struct seq_file *m, void *v)
{
	ssize_t ret = 0;
	uint16_t cid = 0;
#if (HX_FIX_TOUCH_INFO == 0x01)
	uint8_t data[DATA_LEN_4] = { 0 };
	bool FW_IS_ID_EN = 0;
	bool FW_ID_PALM_EN = 0;
#endif

	UNUSED(v);

	D("%s, Enter\n", __func__);

	if (dbg_cmd_flag != 0U) {
		if (dbg_func_ptr_r[dbg_cmd_flag] != NULL) {
			dbg_func_ptr_r[dbg_cmd_flag](m);
		}
	}

	if (debug_level_cmd == 't') {
		if (!fw_update_going) {
			if (fw_update_complete) {
				seq_printf(m,
					"FW Update-Complete\n");
			} else {
				seq_printf(m,
					"FW Update-Fail\n");
			}
		} else {
			seq_printf(m,
					"FW Update-Ongoing...\n");
		}

	} else if (debug_level_cmd == 'v') {
		seq_printf(m,
				"FW Architecture Version = 0x%2.2X\n",
				ic_data->vendor_arch_ver);
#if defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX8530)
		seq_printf(m,
				"FW Algorithm Version = A%2.2X\n",
				ic_data->vendor_display_cfg_ver);
#else
		seq_printf(m,
				"FW Display Config Version = D%2.2X\n",
				ic_data->vendor_display_cfg_ver);
#endif
		seq_printf(m,
				"FW Touch Config Version   = C%2.2X\n",
				ic_data->vendor_touch_cfg_ver);

		cid = ((uint16_t) ic_data->vendor_cid_maj_ver << 8U);
		cid += ic_data->vendor_cid_min_ver;
		seq_printf(m,
			"CID = 0x%2.2X\n", cid);

		seq_printf(m, "Panel Version = 0x%2.2X\n",
			ic_data->vendor_panel_ver);


		if (ic_data->vendor_arch_ver >= 0x8098U) {
			seq_printf(m,
				"Remark1 = %s\n",
				ic_data->vendor_remark1);
			seq_printf(m,
				"Remark2 = %s\n",
				ic_data->vendor_remark2);
			seq_printf(m,
				"Himax Ticket = %s\n",
				ic_data->vendor_ticket);
		}

		seq_printf(m, "Customer = %s\n",
				ic_data->vendor_cus_info);
		seq_printf(m, "Project = %s\n",
				ic_data->vendor_proj_info);
		seq_printf(m, "FW Config Date = %s\n",
				ic_data->vendor_config_date);
		seq_puts(m, "\n");
		seq_puts(m, "Himax Touch Driver Version:\n");
		seq_printf(m, "%s\n", HIMAX_DRIVER_VER);

	} else if (debug_level_cmd == 'd') {
		seq_printf(m,
				"Himax Touch IC Information :\n");
		seq_printf(m,
				"%s\n", private_ts->chip_name);

		seq_printf(m,
				"IC Checksum : CRC\n");

		if (ic_data->HX_INT_IS_EDGE) {
			seq_printf(m,
				"Driver register Interrupt : EDGE TIRGGER\n");
		} else {
			seq_printf(m,
				"Driver register Interrupt : LEVEL TRIGGER\n");
		}

		if (private_ts->protocol_type == (uint8_t)PROTOCOL_TYPE_A) {
			seq_printf(m,
				"Protocol : TYPE_A\n");
		} else {
			seq_printf(m,
				"Protocol : TYPE_B\n");
		}

		seq_printf(m,
				"RX Num : %d\n", ic_data->HX_RX_NUM);
		seq_printf(m,
				"TX Num : %d\n", ic_data->HX_TX_NUM);
		seq_printf(m,
				"X Resolution : %d\n",
				ic_data->HX_X_RES);
		seq_printf(m,
				"Y Resolution : %d\n",
				ic_data->HX_Y_RES);
		seq_printf(m,
				"Max Point : %d\n", ic_data->HX_MAX_PT);

#if (HX_FIX_TOUCH_INFO == 0x00)
		if (ic_data->HX_IS_ID_EN) {
			if (ic_data->HX_ID_PALM_EN) {
				seq_puts(m, "FW INFO Report point: Option 3\n");
			} else {
				seq_puts(m, "FW INFO Report point: Option 2\n");
			}
		} else {
			seq_puts(m, "FW INFO Report point: Option 1\n");
		}
#else
		if (ic_data->HX_IS_ID_EN) {
			if (ic_data->HX_ID_PALM_EN) {
				seq_puts(m, "FIX INFO Report point: Option 3\n");
			} else {
				seq_puts(m, "FIX INFO Report point: Option 2\n");
			}
		} else {
			seq_puts(m, "FIX INFO Report point: Option 1\n");
		}

		himax_mcu_register_read(addr_fw_HX_ID_EN, DATA_LEN_4, data);
		FW_IS_ID_EN = ((data[1] & 0x02U) == 0x02U);
		FW_ID_PALM_EN = ((data[1] & 0x80U) == 0x80U);
		if (FW_IS_ID_EN) {
			if (FW_ID_PALM_EN) {
				seq_puts(m, "FW INFO Report point: Option 3\n");
			} else {
				seq_puts(m, "FW INFO Report point: Option 2\n");
			}
		} else {
			seq_puts(m, "FW INFO Report point: Option 1\n");
		}
#endif
	} else {
		/* do nothing*/
	}

	return ret;
}

ssize_t himax_debug_store(struct file *file, const char *buff,
				 size_t len, loff_t *pos)
{
	char fileName[128];
	char buf[80] = {0};
	int result = 0;
	const struct firmware *fw = NULL;
	char *str_ptr = NULL;
	unsigned int str_len = 0;
	unsigned int i = 0;

	UNUSED(file);
	UNUSED(pos);

	if (len >= 80U) {
		D("%s: no command exceeds 80 chars.\n", __func__);
		return -EFAULT;
	}

	if (copy_from_user(buf, buff, len) != 0) {
		return -EFAULT;
	}

	str_len = (unsigned int) len;
	buf[str_len - 1U] = 0; /*remove \n*/

	while (dbg_cmd_str[i] != NULL) {
		str_ptr = strnstr(buf, dbg_cmd_str[i], len);
		if (str_ptr != NULL) {
			str_len = strlen(dbg_cmd_str[i]);
			dbg_cmd_flag = i + 1U;
			debug_level_cmd = 0;
			D("Cmd is correct :%s, dbg_cmd = %d\n", str_ptr,
			  dbg_cmd_flag);
			break;
		}
		i++;
	}
	if (str_ptr == NULL) {
		dbg_cmd_flag = 0U;
	}

	if (buf[str_len] == ',') {
		dbg_cmd_par = &buf[str_len + 1U];
		if (dbg_func_ptr_w[dbg_cmd_flag] != NULL) {
			/* 2 => '/n' + ','*/
			dbg_func_ptr_w[dbg_cmd_flag](dbg_cmd_par,
						     len - str_len - 2U);
		}

		D("string of paremeter is %s, dbg_cmd_par = %s\n",
		  &buf[str_len + 1U], dbg_cmd_par);
	}

	if (dbg_cmd_flag != 0U) {
		return len;
	}

	if ((buf[0] == 'v') ||
	    (buf[0] == 'd')) { /* firmware version */ /* ic information */
		himax_int_enable(0);
		debug_level_cmd = buf[0];
		himax_mcu_config_reload_enable();
		himax_mcu_power_on_init();
		himax_mcu_read_FW_ver();
		g_core_fp.fp_touch_information();
		himax_int_enable(1);
		return len;
	} else if (buf[0] == 't') {
		if ((buf[1] == 's') && (buf[2] == 'd') && (buf[3] == 'b') &&
		    (buf[4] == 'g')) {
			if (buf[5] == '1') {
				D("Open Ts Debug!\n");
				g_ts_dbg = 1;
			}
			if (buf[5] == '0') {
				D("Close Ts Debug!\n");
				g_ts_dbg = 0;
			}
			goto ENDFUCTION;
		}
		himax_int_enable(0);
		if (private_ts->hx_fail_det > 0) {
			himax_fail_det_enable(0);
		}
		debug_level_cmd = buf[0];
		fw_update_complete = false;
		fw_update_going = true;
		(void)memset(fileName, 0, sizeof(fileName));
		/* parse the file name */
		(void)snprintf(fileName, len - 2U, "%s", &buf[2]);
#if defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX8530)
		if ((buf[1] == 'c') && (buf[2] == 'f')
			 && (buf[3] == 'g')) {
			ic_data->HX8530_upgrade_section |= 0xF0U;
			(void)memset(fileName, 0, sizeof(fileName));
			(void)snprintf(fileName, len - 5U, "%s", &buf[5]);
			D("%s: HX8530 update CFG section!\n", __func__);
		} else if ((buf[1] == 'a') && (buf[2] == 'l')
			 && (buf[3] == 'g')) {
			ic_data->HX8530_upgrade_section |= 0x0FU;
			(void)memset(fileName, 0, sizeof(fileName));
			(void)snprintf(fileName, len - 5U, "%s", &buf[5]);
			D("%s: HX8530 update ALG section!\n", __func__);
		} else {
			ic_data->HX8530_upgrade_section |= 0xFFU;
		}
#endif
		D("%s: upgrade with file(%s) start!\n", __func__, fileName);
		result = request_firmware(&fw, fileName, private_ts->dev);

		if (result < 0) {
			D("fail to request_firmware fwpath: %s (ret:%d)\n",
			  fileName, result);
		} else {
			ic_data->FW_update_flag = 0x06U;

			D("%s: FW image last 4 bytes: 0x%02X, 0x%02X, 0x%02X, 0x%02X\n",
			  __func__, fw->data[fw->size - 4], fw->data[fw->size - 3],
			  fw->data[fw->size - 2], fw->data[fw->size - 1]);
			/*	start to upgrade */
			D("Now FW file size is : %dk\n", ((fw->size) / 1024));
 
			if (!g_core_fp.fp_fts_ctpm_fw_upgrade(
				fw->data, (unsigned int)fw->size)) {
				E("%s: TP upgrade error, line: %d\n", __func__,
					  __LINE__);
				fw_update_complete = false;
			} else {
				D("%s: TP upgrade OK, line: %d\n", __func__,
				  __LINE__);
				fw_update_complete = true;
			}

			release_firmware(fw);
			goto firmware_upgrade_done;
		}

	} else if ((buf[0] == 'i') && (buf[1] == '2') && (buf[2] == 'c')) {
		/* i2c communication */
		debug_level_cmd = 'i';
		return len;
	} else {
		/* do nothing*/
	}
	/* others,do nothing */
	debug_level_cmd = 0;
	return len;

firmware_upgrade_done:
	fw_update_going = false;
	himax_mcu_config_reload_enable();
	himax_mcu_power_on_init();

#if (HX_WPBP_ENABLE == 0x01)
	himax_enable_flash_protected_mode();
#endif
	himax_mcu_read_FW_ver();
	g_core_fp.fp_touch_information();
#if defined(FW_update_history_record)
	himax_FW_update_record();
#endif
	himax_int_enable(1);
	if (private_ts->hx_fail_det > 0) {
		himax_fail_det_enable(1);
	}
	/* todo himax_chip->tp_firmware_upgrade_proceed = 0;
	 * todo himax_chip->suspend_state = 0;
	 * todo enable_irq(himax_chip->irq);
	 */
ENDFUCTION:
	return len;
}
int himax_debug_open(struct inode *inode, struct file *file)
{
	UNUSED(inode);
	return single_open(file, himax_debug_show, NULL);
}

const struct proc_ops_name himax_debug_ops = {
	owner_line
	.proc_op(open) = himax_debug_open,
	.proc_op(write) = himax_debug_store,
	.proc_op(read) = seq_read,
	.proc_opl(lseek) = seq_lseek,
	.proc_op(release) = single_release,
};


static void himax_ts_flash_func(void)
{
	himax_int_enable(0);
	D("%s: flash_command = %d, g_Flash_Size 0x%X.\n", __func__, g_flash_cmd,
	  g_Flash_Size);

	if ((g_flash_cmd == 1U) || (g_flash_cmd == 2U)) {
		g_core_fp.fp_flash_dump_func(g_page_prog_start, g_Flash_Size,
					     g_flash_buffer);
	}

	if (g_flash_cmd == 2U) {
		struct file *fn;

		fn = filp_open(FLASH_DUMP_FILE, O_CREAT | O_WRONLY | O_TRUNC, 0);

		if (!IS_ERR(fn)) {
			D("%s Create file and write data\n", __func__);
			fn->f_op->write(fn, g_flash_buffer,
					g_Flash_Size * sizeof(uint8_t),
					&fn->f_pos);
			filp_close(fn, NULL);
		} else {
			E("%s Open file failed!\n", __func__);
			g_flash_dump_rst = false;
		}
		D("%s file %s with dump size 0x%X\n", __func__, FLASH_DUMP_FILE,
		  g_Flash_Size);
	}

	himax_int_enable(1);
	g_flash_progress = Dump_Finished;
}

void himax_ts_flash_work_func(struct work_struct *work)
{
	UNUSED(work);

	himax_ts_flash_func();
}

void himax_ts_diag_work_func(struct work_struct *work)
{
	UNUSED(work);

	(void)himax_ts_diag_func();
}

static void dbg_func_ptr_init(void)
{
	/*debug function ptr init*/
	dbg_func_ptr_r[1] = himax_crc_test_read;
	dbg_func_ptr_r[2] = himax_proc_FW_debug_read;
	dbg_func_ptr_r[3] = himax_attn_read;
	dbg_func_ptr_r[4] = himax_layout_read;
	dbg_func_ptr_w[4] = himax_layout_write;
	dbg_func_ptr_w[5] = himax_sense_on_off_write;
	dbg_func_ptr_r[6] = himax_debug_level_read;
	dbg_func_ptr_w[6] = himax_debug_level_write;
	dbg_func_ptr_r[7] = himax_int_en_read;
	dbg_func_ptr_w[7] = himax_int_en_write;
	dbg_func_ptr_r[8] = himax_proc_register_read;
	dbg_func_ptr_w[8] = himax_proc_register_write;
	dbg_func_ptr_w[9] = himax_reset_write;
	dbg_func_ptr_w[10] = himax_diag_arrange_write;
	dbg_func_ptr_w[11] = himax_diag_cmd_write;
	dbg_func_ptr_w[12] = himax_GMA_cmd_write;
}

static bool himax_touch_proc_init(void)
{
	himax_proc_diag_dir =
		proc_mkdir(HIMAX_PROC_DIAG_FOLDER, himax_touch_proc_dir);

	if (himax_proc_diag_dir == NULL) {
		E(" %s: himax_proc_diag_dir file create failed!\n", __func__);
		return false;
	}

	himax_proc_vendor_file =
		proc_create(HIMAX_PROC_VENDOR_FILE, S_IRUGO, himax_touch_proc_dir,
			    &himax_vendor_ops);
	if (himax_proc_vendor_file == NULL) {
		E(" %s: proc vendor file create failed!\n", __func__);
		goto fail_1;
	}

	himax_proc_stack_file =
		proc_create(HIMAX_PROC_STACK_FILE, S_IRUGO, himax_proc_diag_dir,
			    &himax_stack_ops);
	if (himax_proc_stack_file == NULL) {
		E(" %s: proc stack file create failed!\n", __func__);
		goto fail_2_1;
	}

	himax_proc_delta_file =
		proc_create(HIMAX_PROC_DELTA_FILE, S_IRUGO, himax_proc_diag_dir,
			    &himax_delta_ops);
	if (himax_proc_delta_file == NULL) {
		E(" %s: proc delta file create failed!\n", __func__);
		goto fail_2_2;
	}

	himax_proc_dc_file =
		proc_create(HIMAX_PROC_DC_FILE, S_IRUGO, himax_proc_diag_dir,
			    &himax_dc_ops);
	if (himax_proc_dc_file == NULL) {
		E(" %s: proc dc file create failed!\n", __func__);
		goto fail_2_3;
	}

	himax_proc_baseline_file =
		proc_create(HIMAX_PROC_BASELINE_FILE, S_IRUGO, himax_proc_diag_dir,
			    &himax_baseline_ops);
	if (himax_proc_baseline_file == NULL) {
		E(" %s: proc baseline file create failed!\n", __func__);
		goto fail_2_4;
	}

	himax_proc_debug_file =
		proc_create(HIMAX_PROC_DEBUG_FILE, (S_IRUGO | S_IWUSR), himax_touch_proc_dir,
			    &himax_debug_ops);
	if (himax_proc_debug_file == NULL) {
		E(" %s: proc debug file create failed!\n", __func__);
		goto fail_3;
	}
	dbg_func_ptr_init();

	himax_proc_flash_dump_file =
		proc_create(HIMAX_PROC_FLASH_DUMP_FILE, (S_IRUGO | S_IWUSR), himax_touch_proc_dir,
			    &himax_flash_dump_ops);
	if (himax_proc_flash_dump_file == NULL) {
		E(" %s: proc flash dump file create failed!\n", __func__);
		goto fail_4;
	}

	himax_proc_pintest_file =
		proc_create(HIMAX_PROC_PINTEST_FILE, (S_IRUGO | S_IWUSR), himax_touch_proc_dir,
			    &himax_pintest_ops);
	if (himax_proc_pintest_file == NULL) {
		E(" %s: proc pintest file create failed!\n", __func__);
		goto fail_5;
	}

	himax_proc_SRAM_test_file =
		proc_create(HIMAX_PROC_SRAM_TEST_FILE, (S_IRUGO | S_IWUSR), himax_touch_proc_dir,
			    &himax_SRAM_test_ops);
	if (himax_proc_SRAM_test_file == NULL) {
		E(" %s: proc SRAM test file create failed!\n", __func__);
		goto fail_6;
	}

	return true;

	remove_proc_entry(HIMAX_PROC_SRAM_TEST_FILE, himax_touch_proc_dir);
fail_6:
	remove_proc_entry(HIMAX_PROC_PINTEST_FILE, himax_touch_proc_dir);
fail_5:
	remove_proc_entry(HIMAX_PROC_FLASH_DUMP_FILE, himax_touch_proc_dir);
fail_4:
	remove_proc_entry(HIMAX_PROC_DEBUG_FILE, himax_touch_proc_dir);
fail_3:
	remove_proc_entry(HIMAX_PROC_BASELINE_FILE, himax_proc_diag_dir);
fail_2_4:
	remove_proc_entry(HIMAX_PROC_DC_FILE, himax_proc_diag_dir);
fail_2_3:
	remove_proc_entry(HIMAX_PROC_DELTA_FILE, himax_proc_diag_dir);
fail_2_2:
	remove_proc_entry(HIMAX_PROC_STACK_FILE, himax_proc_diag_dir);
fail_2_1:
	remove_proc_entry(HIMAX_PROC_VENDOR_FILE, himax_touch_proc_dir);
fail_1:
	remove_proc_entry(HIMAX_PROC_DIAG_FOLDER, himax_touch_proc_dir);
	return false;
}

static void himax_touch_proc_deinit(void)
{
	if (himax_proc_SRAM_test_file != NULL) {
		remove_proc_entry(HIMAX_PROC_SRAM_TEST_FILE, himax_touch_proc_dir);
	}

	if (himax_proc_pintest_file != NULL) {
		remove_proc_entry(HIMAX_PROC_PINTEST_FILE, himax_touch_proc_dir);
	}

	if (himax_proc_flash_dump_file != NULL) {
		remove_proc_entry(HIMAX_PROC_FLASH_DUMP_FILE, himax_touch_proc_dir);
	}

	if (himax_proc_debug_file != NULL) {
		remove_proc_entry(HIMAX_PROC_DEBUG_FILE, himax_touch_proc_dir);
	}

	if (himax_proc_baseline_file != NULL) {
		remove_proc_entry(HIMAX_PROC_BASELINE_FILE, himax_proc_diag_dir);
	}

	if (himax_proc_dc_file != NULL) {
		remove_proc_entry(HIMAX_PROC_DC_FILE, himax_proc_diag_dir);
	}

	if (himax_proc_delta_file != NULL) {
		remove_proc_entry(HIMAX_PROC_DELTA_FILE, himax_proc_diag_dir);
	}

	if (himax_proc_stack_file != NULL) {
		remove_proc_entry(HIMAX_PROC_STACK_FILE, himax_proc_diag_dir);
	}

	if (himax_proc_vendor_file != NULL) {
		remove_proc_entry(HIMAX_PROC_VENDOR_FILE, himax_touch_proc_dir);
	}

	if (himax_proc_diag_dir != NULL) {
		remove_proc_entry(HIMAX_PROC_DIAG_FOLDER, himax_touch_proc_dir);
	}
}

int himax_debug_init(void)
{
	struct himax_ts_data *ts = private_ts;

	diag_max_cnt = 0;

	D("%s:Enter\n", __func__);

	if (ts == NULL) {
		E("%s: ts struct is NULL\n", __func__);
		return -EPROBE_DEFER;
	}

	reg_read_data = kzalloc(128U * sizeof(uint8_t), GFP_KERNEL);
	if (reg_read_data == NULL) {
		E("%s: reg_read_data allocate failed\n", __func__);
		goto err_alloc_reg_read_data_fail;
	}

	debug_data = kzalloc(sizeof(struct himax_debug), GFP_KERNEL);
	if (debug_data == NULL) { /*Allocate debug data space*/
		E("%s: debug_data allocate failed\n", __func__);
		goto err_alloc_debug_data_fail;
	}

	himax_himax_data_init();

	ts->flash_wq = create_singlethread_workqueue("himax_flash_wq");

	if (ts->flash_wq == NULL) {
		E("%s: create flash workqueue failed\n", __func__);
		goto err_create_flash_dump_wq_failed;
	}

	INIT_WORK(&ts->flash_work, himax_ts_flash_work_func);
	setFlashBuffer();

	if (g_flash_buffer == NULL) {
		E("%s: flash buffer allocate fail failed\n", __func__);
		goto err_flash_buf_alloc_failed;
	}

	ts->himax_diag_wq = create_singlethread_workqueue("himax_diag");

	if (ts->himax_diag_wq == NULL) {
		E("%s: create diag workqueue failed\n", __func__);
		goto err_create_diag_wq_failed;
	}

	INIT_DELAYED_WORK(&ts->himax_diag_delay_wrok, himax_ts_diag_work_func);

	setSelfBuffer(ic_data->HX_RX_NUM, ic_data->HX_TX_NUM);
	if (getSelfBuffer() == NULL) {
		E("%s: self buffer allocate failed\n", __func__);
		goto err_self_buf_alloc_failed;
	}

	setSelfNewBuffer(ic_data->HX_RX_NUM, ic_data->HX_TX_NUM);
	if (getSelfNewBuffer() == NULL) {
		E("%s: self new buffer allocate failed\n", __func__);
		goto err_self_new_alloc_failed;
	}

	setSelfOldBuffer(ic_data->HX_RX_NUM, ic_data->HX_TX_NUM);
	if (getSelfOldBuffer() == NULL) {
		E("%s: self old buffer allocate failed\n", __func__);
		goto err_self_old_alloc_failed;
	}

	setMutualBuffer(ic_data->HX_RX_NUM, ic_data->HX_TX_NUM);
	if (getMutualBuffer() == NULL) {
		E("%s: mutual buffer allocate failed\n", __func__);
		goto err_mut_buf_alloc_failed;
	}

	setMutualNewBuffer(ic_data->HX_RX_NUM, ic_data->HX_TX_NUM);
	if (getMutualNewBuffer() == NULL) {
		E("%s: mutual new buffer allocate failed\n", __func__);
		goto err_mut_new_alloc_failed;
	}

	setMutualOldBuffer(ic_data->HX_RX_NUM, ic_data->HX_TX_NUM);
	if (getMutualOldBuffer() == NULL) {
		E("%s: mutual old buffer allocate failed\n", __func__);
		goto err_mut_old_alloc_failed;
	}

	if (!himax_touch_proc_init()) {
		goto err_proc_init_failed;
	}

	return 0;

err_proc_init_failed:
	kfree(diag_mutual_old);
	diag_mutual_old = NULL;
err_mut_old_alloc_failed:
	kfree(diag_mutual_new);
	diag_mutual_new = NULL;
err_mut_new_alloc_failed:
	kfree(diag_mutual);
	diag_mutual = NULL;
err_mut_buf_alloc_failed:
	kfree(diag_self_old);
	diag_self_old = NULL;
err_self_old_alloc_failed:
	kfree(diag_self_new);
	diag_self_new = NULL;
err_self_new_alloc_failed:
	kfree(diag_self);
	diag_self = NULL;
err_self_buf_alloc_failed:
	cancel_delayed_work_sync(&ts->himax_diag_delay_wrok);
	destroy_workqueue(ts->himax_diag_wq);
err_create_diag_wq_failed:

	kfree(g_flash_buffer);
	g_flash_buffer = NULL;
err_flash_buf_alloc_failed:
	destroy_workqueue(ts->flash_wq);
err_create_flash_dump_wq_failed:
	kfree(debug_data);
	debug_data = NULL;
err_alloc_debug_data_fail:
	kfree(reg_read_data);
	reg_read_data = NULL;
err_alloc_reg_read_data_fail:

	return -ENOMEM;
}
EXPORT_SYMBOL(himax_debug_init);

void himax_debug_remove(void)
{
	struct himax_ts_data *ts = private_ts;

	himax_touch_proc_deinit();
	
	cancel_delayed_work_sync(&ts->himax_diag_delay_wrok);

	destroy_workqueue(ts->himax_diag_wq);
	destroy_workqueue(ts->flash_wq);

	if (diag_mutual_old != NULL) {
		kfree(diag_mutual_old);
		diag_mutual_old = NULL;
	}

	if (diag_mutual_new != NULL) {
		kfree(diag_mutual_new);
		diag_mutual_new = NULL;
	}

	if (diag_mutual != NULL) {
		kfree(diag_mutual);
		diag_mutual = NULL;
	}

	if (diag_self_old != NULL) {
		kfree(diag_self_old);
		diag_self_old = NULL;
	}

	if (diag_self_new != NULL) {
		kfree(diag_self_new);
		diag_self_new = NULL;
	}

	if (diag_self != NULL) {
		kfree(diag_self);
		diag_self = NULL;
	}

	if (g_flash_buffer != NULL) {
		kfree(g_flash_buffer);
		g_flash_buffer = NULL;
	}
	if (debug_data != NULL) {
		kfree(debug_data);
		debug_data = NULL;
	}
	if (reg_read_data != NULL) {
		kfree(reg_read_data);
		reg_read_data = NULL;
	}

}
EXPORT_SYMBOL(himax_debug_remove);
