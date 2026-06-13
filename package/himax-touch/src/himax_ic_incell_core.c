// SPDX-License-Identifier: GPL-2.0
/*  Himax Android Driver Sample Code for incell ic core functions
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

#include "himax_ic_core.h"
#include "himax_common.h"
#include "himax_self_test.h"

/*------------------------- define block -------------------------------------*/
/*------------------------- define block -------------------------------------*/
/*------------------------- parameter block ----------------------------------*/

uint32_t dbg_reg_ary[10] = { addr_LTDI_fw_state, addr_fw_dbg_msg_addr, addr_fw_state, addr_fw_state2, addr_scu_reload_control
				 ,addr_psl, addr_cs_central_state, addr_flag_reset_event, addr_chk_dd_status, addr_chk_tp_status};
#if (HX_BOOT_UPGRADE == 0x01)
static uint32_t FW_VER_MAJ_FLASH_ADDR;
static uint32_t CFG_VER_MAJ_FLASH_ADDR;
static uint32_t FW_VER_MIN_FLASH_ADDR;
static uint32_t CFG_VER_MIN_FLASH_ADDR;
static uint32_t CID_VER_MAJ_FLASH_ADDR;
static uint32_t CID_VER_MIN_FLASH_ADDR;
static uint32_t CFG_TABLE_FLASH_ADDR;
#endif
#if defined(PANEL_ID_CHECK)
#if defined(FW_baseline_status_ready)
static bool FW_ready;
#endif
#endif

/*------------------------- parameter block ----------------------------------*/


void himax_mcu_burst_mode_enable(void)
{
	uint8_t tmp_data[DATA_LEN_4];
	uint8_t auto_add_4_byte = 0x01U;
	int ret;

	tmp_data[0] = ((uint8_t)para_AHB_INC4 | auto_add_4_byte);

	ret = himax_bus_write(addr_AHB_INC4, tmp_data, 1,
			      HIMAX_I2C_RETRY_TIMES);
	if (ret < 0) {
		E("%s: i2c access fail!\n", __func__);
	}
}

void himax_mcu_burst_mode_disable(void)
{
	uint8_t tmp_data[DATA_LEN_4];
	int ret;

	tmp_data[0] = (uint8_t)para_AHB_INC4;

	ret = himax_bus_write(addr_AHB_INC4, tmp_data, 1,
			      HIMAX_I2C_RETRY_TIMES);
	if (ret < 0) {
		E("%s: i2c access fail!\n", __func__);
	}
}

void himax_mcu_register_read(uint32_t read_addr, uint32_t read_length,
			    uint8_t *read_data)
{
	uint8_t tmp_data[DATA_LEN_4];
	int ret = 0;

	/*I("%s,Entering\n",__func__);*/

	if (read_length > FLASH_RW_MAX_LEN) {
		E("%s: read len over %d!\n", __func__, FLASH_RW_MAX_LEN);
		return;
	}

	if (read_length > DATA_LEN_4) {
		g_core_fp.fp_burst_mode_enable();
	}

	himax_parse_assign_cmd(read_addr, tmp_data, sizeof(tmp_data));

	ret = himax_bus_write(addr_AHB_address_byte_0, tmp_data, DATA_LEN_4,
			      HIMAX_I2C_RETRY_TIMES);
	if (ret < 0) {
		E("%s: i2c access fail!\n", __func__);
		return;
	}

	tmp_data[0] = (uint8_t)para_AHB_access_direction_read;

	ret = himax_bus_write(addr_AHB_access_direction, tmp_data, 1,
			      HIMAX_I2C_RETRY_TIMES);
	if (ret < 0) {
		E("%s: i2c access fail!\n", __func__);
		return;
	}

	ret = himax_bus_read(addr_AHB_rdata_byte_0, read_data, read_length,
			     HIMAX_I2C_RETRY_TIMES);
	if (ret < 0) {
		E("%s: i2c access fail!\n", __func__);
		return;
	}

}

void himax_mcu_register_read_slave(uint8_t device, uint32_t read_addr, uint32_t read_length,
			    uint8_t *read_data)
{
#if !defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83180) && !defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83181)
	uint8_t tmp_data[DATA_LEN_4];
	int ret = 0;

	if (read_length > FLASH_RW_MAX_LEN) {
		E("%s: read len over %d!\n", __func__, FLASH_RW_MAX_LEN);
		return;
	}

	himax_parse_assign_cmd(read_addr, tmp_data, sizeof(tmp_data));

	ret = himax_bus_write_slave(device, addr_AHB_address_byte_0, tmp_data, DATA_LEN_4,
			      HIMAX_I2C_RETRY_TIMES);
	if (ret < 0) {
		E("%s: i2c access fail!\n", __func__);
		return;
	}

	tmp_data[0] = (uint8_t)para_AHB_access_direction_read;

	ret = himax_bus_write_slave(device, addr_AHB_access_direction, tmp_data, 1,
			      HIMAX_I2C_RETRY_TIMES);
	if (ret < 0) {
		E("%s: i2c access fail!\n", __func__);
		return;
	}

	ret = himax_bus_read_slave(device, addr_AHB_rdata_byte_0, read_data, read_length,
			     HIMAX_I2C_RETRY_TIMES);
	if (ret < 0) {
		E("%s: i2c access fail!\n", __func__);
		return;
	}
#else
	g_core_fp.fp_slave_AHB_reg_broadcast_read(device,
		read_addr, read_data, read_length);
#endif
}

void himax_mcu_register_write_slave(uint8_t device, uint32_t write_addr, uint32_t write_length,
			    uint8_t *write_data)
{
#if !defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83180) && !defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83181)
	uint8_t tmp_addr[DATA_LEN_4];
	int ret = 0;
	uint8_t data_byte[FLASH_RW_MAX_LEN] = { 0 };

	himax_parse_assign_cmd(write_addr, tmp_addr, sizeof(tmp_addr));

	if (write_length > FLASH_RW_MAX_LEN) {
		E("%s: write len over %d!\n", __func__, FLASH_RW_MAX_LEN);
		return;
	}

	/* assign addr 4bytes */
	(void)memcpy(data_byte, tmp_addr, ADDR_LEN_4);
	/* assign data n bytes */
	(void)memcpy(&data_byte[ADDR_LEN_4], write_data, write_length);

	ret = himax_bus_write_slave(device, addr_AHB_address_byte_0, data_byte, write_length + ADDR_LEN_4,
			      HIMAX_I2C_RETRY_TIMES);
	if (ret < 0) {
		E("%s: i2c access fail!\n", __func__);
	}
#else
	g_core_fp.fp_slave_AHB_reg_broadcast_write(device,
		write_addr, write_data, write_length);
#endif
}

void himax_mcu_register_write_all_slave(uint32_t write_addr, uint32_t write_length,
			    uint8_t *write_data)
{
	uint8_t tmp_addr[DATA_LEN_4];
	int ret = 0;
		uint8_t data_byte[FLASH_RW_MAX_LEN] = { 0 };
	uint8_t IC_num = private_ts->slave_ic_num;
	bool control_flag = true;

	himax_parse_assign_cmd(write_addr, tmp_addr, sizeof(tmp_addr));

	if (write_length > FLASH_RW_MAX_LEN) {
		E("%s: read len over %d!\n", __func__, FLASH_RW_MAX_LEN);
		control_flag = false;
	}

	if (control_flag) {
		/* assign addr 4bytes */
		(void)memcpy(data_byte, tmp_addr, ADDR_LEN_4);
		/* assign data n bytes */
		(void)memcpy(&data_byte[ADDR_LEN_4], write_data, write_length);

		switch (IC_num)
		{
			case 0:
				ret = himax_bus_write_slave(IC_MASTER, addr_AHB_address_byte_0,
							data_byte, write_length + ADDR_LEN_4, HIMAX_I2C_RETRY_TIMES);
				if (ret < 0) {
					E("%s: Master i2c access fail!\n", __func__);
				}
				break;
			case 1:
				ret = himax_bus_write_slave(IC_SLAVE_1, addr_AHB_address_byte_0,
							data_byte, write_length + ADDR_LEN_4, HIMAX_I2C_RETRY_TIMES);
				if (ret < 0) {
					E("%s: Slave 1 i2c access fail!\n", __func__);
				}
				ret = himax_bus_write_slave(IC_MASTER, addr_AHB_address_byte_0,
							data_byte, write_length + ADDR_LEN_4, HIMAX_I2C_RETRY_TIMES);
				if (ret < 0) {
					E("%s: Master i2c access fail!\n", __func__);
				}
				break;
			case 2:
				ret = himax_bus_write_slave(IC_SLAVE_2, addr_AHB_address_byte_0,
							data_byte, write_length + ADDR_LEN_4, HIMAX_I2C_RETRY_TIMES);
				if (ret < 0) {
					E("%s: Slave 2 i2c access fail!\n", __func__);
				}
				ret = himax_bus_write_slave(IC_SLAVE_1, addr_AHB_address_byte_0,
							data_byte, write_length + ADDR_LEN_4, HIMAX_I2C_RETRY_TIMES);
				if (ret < 0) {
					E("%s: Slave 1 i2c access fail!\n", __func__);
				}
				ret = himax_bus_write_slave(IC_MASTER, addr_AHB_address_byte_0,
							data_byte, write_length + ADDR_LEN_4, HIMAX_I2C_RETRY_TIMES);
				if (ret < 0) {
					E("%s: Master i2c access fail!\n", __func__);
				}
				break;
			default:
				break;
		}

	}
}

static int himax_mcu_flash_write_burst_lenth(uint8_t *reg_byte,
					     uint8_t *write_data,
					     uint32_t length)
{
	uint8_t data_byte[FLASH_RW_MAX_LEN] = { 0 };
	int ret = 0;

	/* assign addr 4bytes */
	(void)memcpy(data_byte, reg_byte, ADDR_LEN_4);
	/* assign data n bytes */
	(void)memcpy(&data_byte[ADDR_LEN_4], write_data, length);

	ret = himax_bus_write(addr_AHB_address_byte_0, data_byte,
			      length + ADDR_LEN_4, HIMAX_I2C_RETRY_TIMES);
	if (ret < 0) {
		E("%s: xfer fail!\n", __func__);
		return I2C_FAIL;
	}

	return NO_ERR;
}

void himax_mcu_register_write(uint32_t write_addr, uint32_t write_length,
			     uint8_t *write_data)
{
	uint32_t address = 0;
	uint8_t tmp_addr[4];
	uint8_t *tmp_data;
	uint8_t total_write_times = 0;
	uint32_t max_bus_size = MAX_I2C_TRANS_SZ;
	uint32_t total_size_temp = 0;
	uint32_t i = 0;
	int ret = 0;

	total_size_temp = write_length;

	himax_parse_assign_cmd(write_addr, tmp_addr, sizeof(tmp_addr));

	if ((total_size_temp % max_bus_size) == 0U) {
		total_write_times = (uint8_t) (total_size_temp / max_bus_size);
	} else {
		total_write_times = (uint8_t) (total_size_temp / max_bus_size) + 1U;
	}

	if (write_length > DATA_LEN_4) {
		g_core_fp.fp_burst_mode_enable();
	}

	for (i = 0; i < (total_write_times); i++) {

		if (total_size_temp >= max_bus_size) {
			tmp_data = &write_data[i * max_bus_size];

			ret = himax_mcu_flash_write_burst_lenth(
				tmp_addr, tmp_data, max_bus_size);
			if (ret < 0) {
				I("%s: i2c access fail!\n", __func__);
				return;
			}
			total_size_temp = total_size_temp - max_bus_size;
		} else {
			tmp_data = &write_data[i * max_bus_size];
			/* I("last total_size_temp=%d\n",
			 *	total_size_temp % max_bus_size);
			 */
			ret = himax_mcu_flash_write_burst_lenth(
				tmp_addr, tmp_data, total_size_temp);
			if (ret < 0) {
				I("%s: i2c access fail!\n", __func__);
				return;
			}
		}

		address = ((i + 1U) * max_bus_size);
		tmp_addr[0] = (uint8_t)(write_addr & 0xFFU)
					+ (uint8_t)((address) & 0xFFU);
		tmp_addr[1] = (uint8_t)((write_addr >> 8U) & 0xFFU)
					+ (uint8_t)((address >> 8U) & 0xFFU);

		if (tmp_addr[0] < (uint8_t)(write_addr & 0xFFU)) {
			tmp_addr[1] += 1U;
		}

		udelay(100);
	}

}

int himax_write_read_reg(uint32_t addr_32, uint8_t *data, uint8_t hb,
				uint8_t lb)
{
	uint8_t cnt = 0;
	uint8_t tmp_data[4] = { 0 };
	uint8_t r_data[ADDR_LEN_4] = { 0 };

	himax_mcu_register_read(addr_32, DATA_LEN_4, tmp_data);
	tmp_data[1] = data[1];
	tmp_data[0] = data[0];

	do {
		himax_mcu_register_write(addr_32, DATA_LEN_4, tmp_data);
		usleep_range(10000, 11000);
		himax_mcu_register_read(addr_32, DATA_LEN_4, r_data);

		if ((r_data[1] == hb) && (r_data[0] == lb)) {
			I("0x%08X : handshaking OK\n", addr_32);
			return NO_ERR;
		}
		cnt += 1U;
	} while (cnt < 100U);

	E("%s: handshaking fail\n", __func__);
	E("%s: addr = 0x%08X; data = %02X%02X%02X%02X",
		__func__, addr_32, tmp_data[3], tmp_data[2], tmp_data[1], tmp_data[0]);
	E("%s: target = %02X%02X; r_data = %02X%02X\n",
		__func__, hb, lb, r_data[1], r_data[0]);

	return HX_RW_REG_FAIL;
}

void himax_mcu_interface_on(void)
{
	uint8_t tmp_data[DATA_LEN_4];
	int ret = 0;

	/* Read a dummy register to wake up I2C.*/
	ret = himax_bus_read(addr_CONV_I2C_cmd, tmp_data, DATA_LEN_4,
			     HIMAX_I2C_RETRY_TIMES);
	if (ret < 0) { /* to knock I2C*/
		E("%s: i2c access fail!\n", __func__);
		return;
	}
}

#define WIP_PRT_LOG "%s: retry:%d, bf[0]=0x%02X, bf[1]=0x%02X,bf[2]=0x%02X, bf[3]=0x%02X\n"
bool himax_mcu_wait_wip(int Timing)
{
	uint8_t tmp_data[DATA_LEN_4];
	int retry_cnt = 0;

	himax_parse_assign_cmd(data_spi200_trans_fmt, tmp_data,
			       sizeof(tmp_data));
	himax_mcu_register_write(addr_spi200_trans_fmt, DATA_LEN_4, tmp_data);
	tmp_data[0] = 0x01;

	do {
		himax_parse_assign_cmd(data_spi200_trans_ctrl_1, tmp_data,
				       sizeof(tmp_data));
		himax_mcu_register_write(addr_spi200_trans_ctrl, DATA_LEN_4,
					 tmp_data);

		himax_parse_assign_cmd(data_spi200_cmd_1, tmp_data,
				       sizeof(tmp_data));
		himax_mcu_register_write(addr_spi200_cmd, DATA_LEN_4, tmp_data);

		himax_mcu_register_read(addr_spi200_data, DATA_LEN_4, tmp_data);

		if ((tmp_data[0] & 0x01U) == 0x00U) {
			return true;
		}

		retry_cnt++;

		if ((tmp_data[0] != 0x00U) || (tmp_data[1] != 0x00U) ||
		    (tmp_data[2] != 0x00U) || (tmp_data[3] != 0x00U)) {
			I(WIP_PRT_LOG, __func__, retry_cnt, tmp_data[0],
			  tmp_data[1], tmp_data[2], tmp_data[3]);
		}

		if (retry_cnt > 100) {
			E("%s: Wait wip error!\n", __func__);
			return false;
		}

		msleep(Timing);
	} while ((tmp_data[0] & 0x01U) == 0x01U);

	return true;
}

/*power saving level*/
void himax_mcu_init_psl(void)
{
	uint8_t data[DATA_LEN_4] = { 0 };

	himax_parse_assign_cmd(data_clear, data, sizeof(data));
	himax_mcu_register_write(addr_psl, DATA_LEN_4, data);
	I("%s: power saving level reset OK!\n", __func__);
}

void himax_mcu_power_on_init(void)
{
	uint8_t data[DATA_LEN_4] = { 0 };

	I("%s:entering\n", __func__);
	himax_parse_assign_cmd(data_clear, data, sizeof(data));
	/*RawOut select initial*/
	himax_mcu_register_write(addr_raw_out_sel, DATA_LEN_4, data);
	/*DSRAM func initial*/
	himax_mcu_assign_sorting_mode(data);
	/* N frame initial :　reset N frame back to default value 1 for normal mode
	 */
	himax_mcu_register_write(addr_set_frame_addr, DATA_LEN_4, data);
	/*FW reload done initial*/
	himax_mcu_register_write(addr_chk_fw_reload2, DATA_LEN_4,
				 data);

	himax_mcu_tp_reset();
}

void himax_mcu_dd_reg_write(uint8_t addr, uint8_t pa_num, uint8_t len,
			    uint8_t *data, uint8_t bank, uint8_t ic_device)
{
	/*Calculate total write length*/
	uint8_t data_len = ((((len + pa_num - 1U) / 4U) - (pa_num / 4U)) + 1U) * 4U;
	uint8_t w_data[64];
	uint32_t tmp_addr_32 = 0;
	uint8_t tmp_addr[4] = { 0 };
	uint8_t tmp_data[4] = { 0 };
	bool *chk_data;
	uint8_t chk_idx = 0;
	uint32_t i = 0;

	chk_data = kcalloc(data_len, sizeof(bool), GFP_KERNEL);
	if (chk_data == NULL) {
		E("%s Allocate chk buf failed\n", __func__);
		return;
	}

	(void)memset(w_data, 0, sizeof(w_data));

	/*put input data*/
	chk_idx = pa_num % 4U;
	for (i = 0; i < len; i++) {
		w_data[chk_idx] = data[i];
		chk_data[chk_idx] = true;
		chk_idx++;
	}

	/*get original data*/
	chk_idx = (pa_num / 4U) * 4U;
	for (i = 0; i < data_len; i++) {
		if (!chk_data[i]) {
			himax_mcu_dd_reg_read(addr, (uint8_t)(chk_idx + i), 1,
					      tmp_data, bank, ic_device);

			w_data[i] = tmp_data[0];
			chk_data[i] = true;
		}
		D("%s w_data[%d] = %2X\n", __func__, i, w_data[i]);
	}

	tmp_addr[3] = 0x30;
	tmp_addr[2] = addr >> 4;
	tmp_addr[1] = (addr << 4) | bank;
	tmp_addr[0] = chk_idx;
	kfree(chk_data);
	chk_data = NULL;

	tmp_addr_32 = ((uint32_t)tmp_addr[3] << 24);
	tmp_addr_32 += ((uint32_t)tmp_addr[2] << 16);
	tmp_addr_32 += ((uint32_t)tmp_addr[1] << 8);
	tmp_addr_32 += (uint32_t)tmp_addr[0];
	D("%s Addr = 0x%08X.\n", __func__, tmp_addr_32);

	if (ic_device != (uint8_t)IC_MASTER) {
		himax_mcu_register_write_slave(ic_device, tmp_addr_32, data_len, w_data);
	} else {
		himax_mcu_register_write(tmp_addr_32, data_len, w_data);
	}
}

void himax_mcu_dd_reg_read(uint8_t addr, uint8_t pa_num, uint8_t len, uint8_t *data,
			   uint8_t bank, uint8_t ic_device)
{
	uint32_t tmp_addr_32 = 0;
	uint8_t tmp_addr[4] = { 0 };
	uint8_t tmp_data[4] = { 0 };
	uint8_t i = 0;

	if (len > 16U) {
		I("%s length = %d is over limitation\n", __func__, len);
		return;
	}
	if (ic_device == (uint8_t)IC_MASTER) {
		for (i = 0; i < len; i++) {
			tmp_addr[3] = 0x30;
			tmp_addr[2] = addr >> 4;
			tmp_addr[1] = (addr << 4) | bank;
			tmp_addr[0] = pa_num + i;

			tmp_addr_32 = ((uint32_t)tmp_addr[3] << 24);
			tmp_addr_32 += ((uint32_t)tmp_addr[2] << 16);
			tmp_addr_32 += ((uint32_t)tmp_addr[1] << 8);
			tmp_addr_32 += (uint32_t)tmp_addr[0];
			himax_mcu_register_read(tmp_addr_32, DATA_LEN_4, tmp_data);

			data[i] = tmp_data[(i % 4U)];

			D("%s Addr = 0x%08X .data = %2X\n", __func__,
			  tmp_addr_32, data[i]);
		}
	} else {
		for (i = 0; i < len; i++) {
			tmp_addr[3] = 0x30;
			tmp_addr[2] = addr >> 4;
			tmp_addr[1] = (addr << 4) | bank;
			tmp_addr[0] = pa_num + i;

			tmp_addr_32 = ((uint32_t)tmp_addr[3] << 24);
			tmp_addr_32 += ((uint32_t)tmp_addr[2] << 16);
			tmp_addr_32 += ((uint32_t)tmp_addr[1] << 8);
			tmp_addr_32 += (uint32_t)tmp_addr[0];
			himax_mcu_register_read_slave(ic_device, tmp_addr_32, DATA_LEN_4, tmp_data);

			data[i] = tmp_data[(i % 4U)];

			D("%s Addr = 0x%08X .data = %2X\n", __func__,
				tmp_addr_32, data[i]);
		}
	}
}

/* CORE_FW */
/* FW side start*/
static void diag_mcu_parse_raw_data(struct himax_report_data *hx_touch_data_tmp,
				    uint32_t mul_num, uint32_t self_num, uint8_t diag_cmd,
				    int16_t *mutual_data, int16_t *self_data)
{
	uint32_t RawDataLen_word;
	uint32_t index = 0;
	uint32_t temp1 = 0;
	uint32_t temp2 = 0;
	uint32_t i = 0;
	uint16_t mutual_temp = 0;
	uint16_t self_temp = 0;

	if ((hx_touch_data_tmp->hx_rawdata_buf[0] == 0x3AU) &&
	    (hx_touch_data_tmp->hx_rawdata_buf[1] == 0xA3U) &&
	    (hx_touch_data_tmp->hx_rawdata_buf[2] > 0U) &&
	    (hx_touch_data_tmp->hx_rawdata_buf[3] == diag_cmd)) {
		RawDataLen_word = hx_touch_data_tmp->rawdata_size / 2U;
		index = ((uint32_t)hx_touch_data_tmp->hx_rawdata_buf[2] - 1U) *
			RawDataLen_word;

		/*
		 * I("RawDataLen=%d , RawDataLen_word=%d ,
		 *	hx_touch_info_size=%d\n",
		 *	RawDataLen, RawDataLen_word, hx_touch_info_size);
		 */
		for (i = 0; i < RawDataLen_word; i++) {
			temp1 = index + i;

			if (temp1 < mul_num) { /*mutual*/
				mutual_temp = ((uint16_t) hx_touch_data_tmp->hx_rawdata_buf[(i * 2U) + 4U + 1U]) * 256U;
				mutual_temp += (uint16_t) hx_touch_data_tmp->hx_rawdata_buf[(i * 2U) + 4U];
				mutual_data[index + i] = (int16_t) mutual_temp;
			} else { /*self*/
				temp1 = i + index;
				temp2 = self_num + mul_num;

				if (temp1 >= temp2) {
					break;
				}
				self_temp = ((uint16_t) hx_touch_data_tmp->hx_rawdata_buf[(i * 2U) + 4U + 1U]) * 256U;
				self_temp += (uint16_t) hx_touch_data_tmp->hx_rawdata_buf[(i * 2U) + 4U];
				self_data[i + index - mul_num] = (int16_t) self_temp;
			}
		}
	}
}

/*-------------------------------------------------------------------------
 *
 *	Description:  Do software reset by setting addr 0x90000018 register with
 *value 0x55. Parameters: void
 *
 *	Returns: void
 *
 */
void himax_mcu_system_reset(void)
{
	uint8_t data[DATA_LEN_4] = { 0 };

	I("%s: Entering!\n", __func__);

	himax_parse_assign_cmd(data_system_reset, data, sizeof(data));
	himax_mcu_register_write(addr_system_reset, DATA_LEN_4, data);

	msleep(100);

#if defined(HIMAX_I2C_PLATFORM)
	himax_mcu_interface_on();
#endif
}

void himax_mcu_command_reset(void)
{
	uint8_t tmp_data[DATA_LEN_4] = { 0 };
	int ret = 0;

	I("%s: Entering!\n", __func__);

	/* reset code*/
	tmp_data[0] = 0x00;
	tmp_data[1] = 0x00;

	ret = himax_bus_write(addr_sense_on_off_0, tmp_data, 1,
			      HIMAX_I2C_RETRY_TIMES);
	if (ret < 0) {
		E("%s: i2c access fail!\n", __func__);
	}

	ret = himax_bus_write(addr_sense_on_off_1, tmp_data, 1,
			      HIMAX_I2C_RETRY_TIMES);
	if (ret < 0) {
		E("%s: i2c access fail!\n", __func__);
	}

	msleep(20);

#if defined(HIMAX_I2C_PLATFORM)
	himax_mcu_interface_on();
#endif
}

uint32_t himax_mcu_calculate_CRC32_by_AP(unsigned char *FW_content, uint32_t len)
{
	uint32_t i = 0;
	uint32_t j = 0;
	uint32_t length = 0;
	uint32_t fw_data;
	uint32_t fw_data_2;
	uint32_t CRC = 0xFFFFFFFFU;
	uint32_t PolyNomial = 0x82F63B78U;

	length = len / 4U;

	for (i = 0; i < length; i++) {
		fw_data = FW_content[i * 4U];

		for (j = 1U; j < 4U; j++) {
			fw_data_2 = FW_content[(i * 4U) + j];
			fw_data += (fw_data_2) << (8U * j);
		}
		CRC = fw_data ^ CRC;
		for (j = 0; j < 32U; j++) {
			if ((CRC % 2U) != 0U) {
				CRC = ((CRC >> 1U) & 0x7FFFFFFFU) ^ PolyNomial;
			} else {
				CRC = (((CRC >> 1U) & 0x7FFFFFFFU));
			}
		}
	}

	return CRC;
}

uint32_t himax_mcu_check_CRC(uint8_t *start_addr, unsigned int reload_length)
{
	uint32_t result = 0;
	uint8_t tmp_data[DATA_LEN_4] = { 0 };
	uint8_t i_counter = 0;
	unsigned int length = reload_length / DATA_LEN_4;

	I("%s Enter\n", __func__);

	tmp_data[0] = 0xA5;

	/* Disable retry wrapper to avoid I2C CLK low issue */
	himax_mcu_register_write(addr_retry_wrapper_clr_pw, 4, tmp_data);

	himax_mcu_register_write(addr_reload_addr_from, DATA_LEN_4,
					start_addr);

	tmp_data[3] = 0x00;
	tmp_data[2] = 0x99;
	tmp_data[1] = (uint8_t)((length >> 8U) & 0xFFU);
	tmp_data[0] = (uint8_t)(length & 0xFFU);

	himax_mcu_register_write(addr_reload_addr_cmd_beat, DATA_LEN_4,
					tmp_data);

	himax_mcu_register_read(addr_reload_status, DATA_LEN_4,
						tmp_data);

	if (tmp_data[1] != 0x99U) {
		E("%s: Reload status cmd fail and out of retry count!\n", __func__);
		return HW_CRC_FAIL;
	}
	I("%s:8005_0000 read data[1]=0x%02X,data[0]=0x%02X\n",
		__func__, tmp_data[1], tmp_data[0]);

	i_counter = 0;

	do {

		himax_mcu_register_read(addr_reload_status, DATA_LEN_4,
							tmp_data);

		if ((tmp_data[0] & 0x01U) != 0x01U) {
			himax_mcu_register_read(addr_reload_crc32_result,
						      DATA_LEN_4, tmp_data);
			result = ((uint32_t)tmp_data[3] << 24);
			result += ((uint32_t)tmp_data[2] << 16);
			result += ((uint32_t)tmp_data[1] << 8);
			result += (uint32_t)tmp_data[0];
			I("%s:Check CRC result=0x%08X\n",  __func__, result);
			goto END;
		} else {
			usleep_range(1000, 1100);
			if (i_counter >= 100U) {
				I("%s:CRC Wait loop timeout\n", __func__);
				himax_mcu_read_FW_status();
				return HW_CRC_FAIL;
			}
		}
		i_counter += 1U;
	} while (i_counter < 100U);
END:
	return result;
}

#define PRT_DATA "%s:[3]=0x%02X, [2]=0x%02X, [1]=0x%02X, [0]=0x%02X\n"
void himax_mcu_diag_register_set(uint8_t diag_command, uint8_t chip_id_sel)
{
	uint8_t tmp_data[DATA_LEN_4];
	uint8_t back_data[DATA_LEN_4];
	uint8_t cnt = 50;

	I("diag_command = %d, chip_id_sel = 0x%02X\n", diag_command, chip_id_sel);
	tmp_data[3] = 0x00;
	tmp_data[2] = chip_id_sel;
	tmp_data[1] = 0x00;
	tmp_data[0] = diag_command;

	do {
		himax_mcu_register_write(addr_raw_out_sel, DATA_LEN_4,
					 tmp_data);
		himax_mcu_register_read(addr_raw_out_sel, DATA_LEN_4,
					back_data);
		I(PRT_DATA, __func__, back_data[3], back_data[2], back_data[1],
		  back_data[0]);
		cnt--;
	} while ((tmp_data[0] != back_data[0]) && (cnt > 0U));
}

void himax_mcu_config_reload_disable(void)
{
	uint8_t data[DATA_LEN_4] = { 0 };

	/*reload disable*/
	himax_parse_assign_cmd(data_fw_define_flash_reload_dis, data,
		sizeof(data));
	himax_mcu_register_write(addr_fw_define_flash_reload,
		DATA_LEN_4, data);

	I("%s: setting OK!\n", __func__);
}

void himax_mcu_config_reload_enable(void)
{
	uint8_t data[DATA_LEN_4] = { 0 };

	/*clear config reload done*/
	himax_parse_assign_cmd(data_clear, data,
		sizeof(data));
	himax_mcu_register_write(addr_chk_fw_reload2,
		DATA_LEN_4, data);

	/*reload enable*/
	himax_parse_assign_cmd(data_fw_define_flash_reload_en, data,
		sizeof(data));
	himax_mcu_register_write(addr_fw_define_flash_reload,
		DATA_LEN_4, data);

	I("%s: setting OK!\n", __func__);
}
#if !defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83194)
void himax_mcu_rawdata_normalize_disable(int disable)
{
	uint8_t data[DATA_LEN_4] = { 0 };

	I("%s:entering\n", __func__);
	himax_mcu_register_read(addr_fw_define_rawdata_normalize, DATA_LEN_4,
				data);

	if (disable != 0) {/*normalize disable*/
		data[3] &= 0x7FU;
	} else {/*normalize enable*/
		data[3] |= 0x80U;
	}

	himax_mcu_register_write(addr_fw_define_rawdata_normalize, DATA_LEN_4,
				 data);
}
#endif
void himax_set_BS_UDT_frame(uint8_t checktype)
{
	uint8_t tmp_data[4];

	/*skip frame 0x100070F4*/
	himax_mcu_register_read(addr_skip_frame, DATA_LEN_4, tmp_data);

	switch (checktype) {
	case (uint8_t)HX_RAWDATA:
	case (uint8_t)HX_BPN_RAWDATA:
	case (uint8_t)HX_SBP_RAWDATA:
	case (uint8_t)HX_SC:
		tmp_data[0] = BS_RAWDATA;
		break;
	case (uint8_t)HX_WT_NOISE:
	case (uint8_t)HX_NOISE:
		tmp_data[0] = BS_NOISE;
		break;
	default:
		tmp_data[0] = BS_OPENSHORT;
		break;
	}
	if (checktype == HX_INSPECT_MODE) {
		tmp_data[0] = BS_INSPECT_MODE;
	}

	himax_mcu_register_write(addr_skip_frame, 4, tmp_data);
}

void himax_mcu_read_FW_ver(void)
{
	uint8_t data[12] = { 0 };
	uint8_t pswd[DATA_LEN_4] = { 0 };
	uint8_t retry = 0;
	uint8_t reload_status = 0;

	while (reload_status == 0U) {
		himax_mcu_register_read(addr_chk_fw_reload2,
					DATA_LEN_4, data);
		pswd[1] = (uint8_t)((addr_chk_fw_reload2 & 0xFF00U) >> 8U);
		pswd[0] = (uint8_t)(addr_chk_fw_reload2 & 0xFFU);
		if ((data[1] == pswd[1]) && (data[0] == pswd[0])) {
			I("%s: FW finish reload done %d times\n", __func__,
			  retry);
			reload_status = 1;
			break;
		} else {
			retry++;
			usleep_range(10000, 11000);
		}
		if (retry == 100U) {
			E("%s: FW fail reload done !!!!!\n", __func__);
			himax_mcu_read_FW_status();
			ic_data->vendor_panel_ver = 0;
			ic_data->vendor_arch_ver = 0;
			ic_data->vendor_config_ver = 0;
			ic_data->vendor_touch_cfg_ver = 0;
			ic_data->vendor_display_cfg_ver = 0;
			ic_data->vendor_cid_maj_ver = 0;
			ic_data->vendor_cid_min_ver = 0;
			goto END;
		}
	}
	/*I("%s:pswd[0]=0x%2.2X,pswd[1]=0x%2.2X\n", __func__, pswd[0],
	 * pswd[1]);
	 */
	/*
	 * Read FW version
	 */
	himax_mcu_register_read(addr_fw_architecture_version, DATA_LEN_4, data);
	ic_data->vendor_panel_ver = data[0];
	ic_data->vendor_arch_ver = ((uint16_t)data[1] << 8);
	ic_data->vendor_arch_ver += (uint16_t)data[2];

	himax_mcu_register_read(addr_fw_config_version, DATA_LEN_4, data);
	ic_data->vendor_config_ver = ((uint16_t)data[2] << 8);
	ic_data->vendor_config_ver += (uint16_t)data[3];
	ic_data->vendor_touch_cfg_ver = data[2];
	ic_data->vendor_display_cfg_ver = data[3];

	himax_mcu_register_read(addr_fw_CID, DATA_LEN_4, data);
	ic_data->vendor_cid_maj_ver = data[2];
	ic_data->vendor_cid_min_ver = data[3];

	himax_mcu_register_read(addr_fw_customer, 12, data);
	(void)memcpy(ic_data->vendor_cus_info, data, 12);

	himax_mcu_register_read(addr_fw_project_name, 12, data);
	(void)memcpy(ic_data->vendor_proj_info, data, 12);

	himax_mcu_register_read(addr_fw_config_date, 12, data);
	(void)memcpy(ic_data->vendor_config_date, data, 12);

	if (ic_data->vendor_arch_ver >= 0x8098U) {
		himax_mcu_register_read(addr_fw_remark1, 12, data);
		(void)memcpy(ic_data->vendor_remark1, data, 12);

		himax_mcu_register_read(addr_fw_remark2, 12, data);
		(void)memcpy(ic_data->vendor_remark2, data, 12);

		himax_mcu_register_read(addr_fw_ticket, 12, data);
		(void)memcpy(ic_data->vendor_ticket, data, 12);
	}

	I("FW Architecture Version : %04X\n", ic_data->vendor_arch_ver);
	I("CID : %04X\n",
	  ((ic_data->vendor_cid_maj_ver << 8U) | ic_data->vendor_cid_min_ver));
#if defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX8530)
	I("FW Algorithm Config  : A%02X\n", ic_data->vendor_display_cfg_ver);
#else
	I("FW Display Config Version : D%02X\n", ic_data->vendor_display_cfg_ver);
#endif
	I("FW Touch Config Version : C%02X\n", ic_data->vendor_touch_cfg_ver);
	I("Panel Version : 0x%02X\n", ic_data->vendor_panel_ver);

	if (ic_data->vendor_arch_ver >= 0x8098U) {
		I("Remark 1 : %s\n", ic_data->vendor_remark1);
		I("Remark 2 : %s\n", ic_data->vendor_remark2);
		I("Himax Ticket : %s\n", ic_data->vendor_ticket);
	}

	I("FW Config Date = %s\n", ic_data->vendor_config_date);
	I("Project = %s\n", ic_data->vendor_proj_info);
	I("Customer = %s\n", ic_data->vendor_cus_info);
END:
	return;
}

void himax_print_define_function(void)
{

	I("HX_BOOT_UPGRADE : %d\n", HX_BOOT_UPGRADE);
	I("HX_EXCP_RECOVERY : %d\n", HX_EXCP_RECOVERY);
	I("HX_PROTOCOL_A : %d\n", HX_PROTOCOL_A);
	I("HX_PROTOCOL_B_3PA : %d\n", HX_PROTOCOL_B_3PA);
	I("HX_RST_PIN_FUNC : %d\n", HX_RST_PIN_FUNC);
	I("HX_TP_INSPECT_MODE : %d\n", HX_TP_INSPECT_MODE);
	I("HX_FIX_TOUCH_INFO : %d\n", HX_FIX_TOUCH_INFO);
	I("HX_WPBP_ENABLE : %d\n", HX_WPBP_ENABLE);
	I("HX_SMART_WAKEUP : %d\n", HX_SMART_WAKEUP);
	I("HX_GESTURE_TRACK : %d\n", HX_GESTURE_TRACK);
#if (HX_TP_GTS_MODE == 0x01)
	I("GTS_range : %d\n", private_ts->GTS_range);
#endif
	I("HIMAX_LTDI_CONFIG : %d\n", HIMAX_LTDI_CONFIG);
	I("HIMAX_PRODUCT_TYPE : HX%6X\n", HIMAX_PRODUCT_TYPE);
	I("Himax Touch Driver Version = %s\n", HIMAX_DRIVER_VER);


}

bool himax_mcu_read_event_stack(uint8_t *buf, uint8_t length)
{
	int len = (int)length;
	int ret = 0;
	int i2c_speed = 0;
	struct time_var timeStart;
	struct time_var timeEnd;
	struct time_var timeDelta;

	if ((private_ts->debug_log_level & BIT(2)) != 0U) {
		time_func(&timeStart);
	}

	ret = himax_bus_read(addr_read_event_stack, buf, length,
		       HIMAX_I2C_RETRY_TIMES);
	if (ret < 0) {
		E("%s: i2c access fail!\n", __func__);
		return false;
	}

	if ((private_ts->debug_log_level & BIT(2)) != 0U) {
		time_func(&timeEnd);
		timeDelta = time_diff(timeStart, timeEnd);

		i2c_speed =
			(len * 9 * 1000000 / (int)timeDelta.tv_nsec) * 13 / 10;
		private_ts->bus_speed = i2c_speed;
	}

	return true;
}

void himax_mcu_stop_DSRAM_output(void)
{
	uint8_t tmp_data[DATA_LEN_4] = { 0 };

	if (himax_write_read_reg(addr_rawdata, tmp_data, 0x00, 0x00) < 0) {
		I("%s Data NOT ready => bypass\n", __func__);
	}

	I("%s: End of setting!\n", __func__);
}

bool himax_mcu_calculateChecksum(uint32_t size)
{
	uint32_t CRC_result = 0;
	uint8_t tmp_addr[DATA_LEN_4] = { 0 };

	I("%s:Now size= %dk\n", __func__, (size / HX1K));
	himax_parse_assign_cmd(addr_program_reload_from, tmp_addr,
			       sizeof(tmp_addr));


	CRC_result = g_core_fp.fp_check_CRC(tmp_addr, size);
	msleep(50);

	if (CRC_result != 0U) {
		I("%s: CRC Fail=%d\n", __func__, CRC_result);
	}

	return (CRC_result == 0U) ? true : false;
}

void himax_mcu_read_FW_status(void)
{
	uint8_t len = 0;
	uint8_t i = 0;
	uint8_t data[DATA_LEN_4] = { 0 };

	len = (uint8_t)(sizeof(dbg_reg_ary) / sizeof(uint32_t));

	for (i = 0; i < len; i++) {
		himax_mcu_register_read(dbg_reg_ary[i], DATA_LEN_4, data);

		I("reg[0-3] : 0x%08X = 0x%02X, 0x%02X, 0x%02X, 0x%02X\n",
		  dbg_reg_ary[i], data[0], data[1], data[2], data[3]);
	}
}

void himax_mcu_irq_switch(int switch_on)
{
	if (switch_on != 0) {
		if (private_ts->use_irq) {
			himax_int_enable(switch_on);
		} else {
			hrtimer_start(&private_ts->timer, ktime_set(1, 0),
				      HRTIMER_MODE_REL);
		}
	} else {
		if (private_ts->use_irq) {
			himax_int_enable(switch_on);
		} else {
			hrtimer_cancel(&private_ts->timer);
			cancel_work_sync(&private_ts->work);
		}
	}
}

void himax_mcu_assign_sorting_mode(uint8_t *tmp_data)
{
	I("%s:data[1]=0x%02X,data[0]=0x%02X\n", __func__, tmp_data[1],
	  tmp_data[0]);
	himax_mcu_register_write(addr_sorting_mode_en, DATA_LEN_4, tmp_data);
}

void himax_mcu_check_sorting_mode(uint8_t *tmp_data)
{
	himax_mcu_register_read(addr_sorting_mode_en, DATA_LEN_4, tmp_data);
	I("%s: tmp_data[0]=0x%02X,tmp_data[1]=0x%02X\n", __func__, tmp_data[0],
	  tmp_data[1]);
}

void himax_mcu_check_N_frame(uint8_t *tmp_data)
{
	himax_mcu_register_read(addr_set_frame_addr, DATA_LEN_4, tmp_data);
	I("%s: tmp_data[0]=0x%02X\n", __func__, tmp_data[0]);
}

/* FW side end*/
/* CORE_FW */

/* CORE_FLASH */
/* FLASH side start*/


void himax_mcu_block_erase(uint32_t start_addr, uint32_t length)
{
	uint32_t page_prog_start = 0;
	uint32_t block_size = 0x10000;//64KB
	uint8_t tmp_data[DATA_LEN_4] = { 0 };


	himax_parse_assign_cmd(data_spi200_trans_fmt, tmp_data,
			       sizeof(tmp_data));
	himax_mcu_register_write(addr_spi200_trans_fmt, DATA_LEN_4, tmp_data);

	for (page_prog_start = start_addr;
	     page_prog_start < (start_addr + length);
	     page_prog_start += block_size) {
		himax_parse_assign_cmd(data_spi200_trans_ctrl_2, tmp_data,
				       sizeof(tmp_data));
		himax_mcu_register_write(addr_spi200_trans_ctrl, DATA_LEN_4,
					 tmp_data);

		himax_parse_assign_cmd(data_spi200_cmd_2, tmp_data,
				       sizeof(tmp_data));
		himax_mcu_register_write(addr_spi200_cmd, DATA_LEN_4, tmp_data);

		tmp_data[3] = (uint8_t)((page_prog_start >> 24U) & 0xFFU);
		tmp_data[2] = (uint8_t)((page_prog_start >> 16U) & 0xFFU);
		tmp_data[1] = (uint8_t)((page_prog_start >> 8U) & 0xFFU);
		tmp_data[0] = (uint8_t)(page_prog_start & 0xFFU);
		himax_mcu_register_write(addr_spi200_addr, DATA_LEN_4,
					 tmp_data);

		himax_parse_assign_cmd(data_spi200_trans_ctrl_3, tmp_data,
				       sizeof(tmp_data));
		himax_mcu_register_write(addr_spi200_trans_ctrl, DATA_LEN_4,
					 tmp_data);

		himax_parse_assign_cmd(data_spi200_cmd_4, tmp_data,
				       sizeof(tmp_data));
		himax_mcu_register_write(addr_spi200_cmd, DATA_LEN_4, tmp_data);

		msleep(100);

		if (!himax_mcu_wait_wip(100)) {
			E("%s:Erase Fail\n", __func__);
			return;
		}
	}

	I("%s:END\n", __func__);
}

void himax_mcu_sector_erase(uint32_t start_addr, uint32_t length)
{
	uint32_t tmp_addr_32 = 0;
	uint8_t data[DATA_LEN_4] = { 0 };
	uint32_t page_prog_start = 0;
	uint32_t sector_size = 0x1000;

	/*=====================================
	 *SPI Transfer Format : 0x8000_0010 ==> 0x0002_0780
	 *=====================================
	 */
	data[3] = 0x00;
	data[2] = 0x02;
	data[1] = 0x07;
	data[0] = 0x80;

	tmp_addr_32 = 0x80000010U;
	himax_mcu_register_write(tmp_addr_32, DATA_LEN_4, data);

	for (page_prog_start = start_addr;
	     page_prog_start < (start_addr + length);
	     page_prog_start += sector_size) {
		/*=====================================
		 *Write Enable : 1. 0x8000_0020 ==> 0x4700_0000 [control]
		 *			 2. 0x8000_0024 ==> 0x0000_0006 [WREN]
		 *=====================================
		 */
		data[3] = 0x47;
		data[2] = 0x00;
		data[1] = 0x00;
		data[0] = 0x00;
		tmp_addr_32 = 0x80000020U;
		himax_mcu_register_write(tmp_addr_32, DATA_LEN_4, data);
		data[3] = 0x00;
		data[2] = 0x00;
		data[1] = 0x00;
		data[0] = 0x06;
		tmp_addr_32 = 0x80000024U;
		himax_mcu_register_write(tmp_addr_32, DATA_LEN_4, data);

		/*=====================================
		 *Sector Erase
		 *Command : 0x8000_0028 ==> 0x0000_0000 [SPI addr]
		 *				0x8000_0020 ==> 0x6700_0000 [control]
		 *				0x8000_0024 ==> 0x0000_0020 [SE]
		 *=====================================
		 */

		data[3] = (uint8_t)(page_prog_start >> 24);
		data[2] = (uint8_t)(page_prog_start >> 16);
		data[1] = (uint8_t)(page_prog_start >> 8);
		data[0] = (uint8_t)page_prog_start;

		tmp_addr_32 = 0x80000028U;
		himax_mcu_register_write(tmp_addr_32, DATA_LEN_4, data);
		data[3] = 0x67;
		data[2] = 0x00;
		data[1] = 0x00;
		data[0] = 0x00;
		tmp_addr_32 = 0x80000020U;
		himax_mcu_register_write(tmp_addr_32, DATA_LEN_4, data);
		data[3] = 0x00;
		data[2] = 0x00;
		data[1] = 0x00;
		data[0] = 0x20;
		tmp_addr_32 = 0x80000024U;
		himax_mcu_register_write(tmp_addr_32, DATA_LEN_4, data);

		if (!himax_mcu_wait_wip(100)) {
			E("%s: Fail:\n", __func__);
		}
		I("%s:page_prog_start = 0x%8X,\n", __func__, page_prog_start);
	}

	I("%s:END\n", __func__);
}

bool himax_mcu_fts_ctpm_fw_upgrade(const u8 *fw_data, unsigned int bin_size)
{
	bool upgrade_success = false;
	uint8_t counter = 0U;
	uint8_t tmp_addr[4];
	struct time_var timeStart;
	struct time_var timeEnd;
	struct time_var timeDelta;

#if (HX_FIX_TOUCH_INFO == 0x01)
    if (bin_size !=  FIX_FW_SIZE) {
		E("%s: bin file size does not match FIX_FW_SIZE\n", __func__);
	}
#endif	
	if ((bin_size == FW_SIZE_255k) || (bin_size == FW_SIZE_128k) ||
		(bin_size == FW_SIZE_192k)) {
		time_func(&timeStart);
		for (counter = 0U; counter < 3U; counter++) {
			g_core_fp.fp_sense_off();
			himax_mcu_init_psl();

			himax_disable_flash_protected_mode();

			himax_mcu_block_erase(0x00U, bin_size);
			if (g_core_fp.fp_flash_programming(fw_data, 0U, bin_size) == false) {
				I("%s => flash_programming fail %d times\n", __func__, counter);
				himax_mcu_tp_reset();
				continue;
			}

			himax_parse_assign_cmd(addr_program_reload_from, tmp_addr,
							sizeof(tmp_addr));
			if (g_core_fp.fp_check_CRC(tmp_addr, bin_size) == 0x00000000U) {
				upgrade_success = true;
#if defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83195) || defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83194)
				ic_data->HX_FW_SIZE = bin_size;
#endif
				break;
			}

			I("%s => check_CRC fail %d times\n", __func__, counter);
			himax_mcu_tp_reset();

		}
		time_func(&timeEnd);
		timeDelta = time_diff(timeStart, timeEnd);
#if defined(KERNEL_VER_5_10)
		I("<<Timer>>%s => %lld.%ld s\n", __func__, timeDelta.tv_sec, timeDelta.tv_nsec);
#else
		I("<<Timer>>%s => %ld.%ld s\n", __func__, timeDelta.tv_sec, timeDelta.tv_nsec);
#endif
	} else {
		E("%s: Unknown bin file size\n", __func__);
	}
	return upgrade_success;
}

int himax_mcu_SRAM_run_test_DTSRAM_function(uint8_t *write_buffer, uint8_t *read_buffer, uint8_t IC_num, int max_loop)
{
	int loop_count;
	int SRAM_test_status = 0;

	/* Start test */
	himax_mcu_register_write_all_slave(addr_SRAM_set_voltage, DATA_LEN_4, write_buffer);

	(void)memset(write_buffer, 0, DATA_LEN_4);
	write_buffer[0] = 0xA5;
	himax_mcu_register_write_all_slave(addr_SRAM_start_test, DATA_LEN_4, write_buffer);

	/* 0xA5 for D&T SRAM test, 0x5A for ISRAM test */
	(void)memset(write_buffer, 0, 4);
	write_buffer[0] = 0xA5;
	himax_mcu_register_write_all_slave(addr_SRAM_test_option, DATA_LEN_4, write_buffer);

	/* Set MCU Wake Up*/
	(void)memset(write_buffer, 0, 4);
	write_buffer[0] = 0x53;
	himax_mcu_register_write_all_slave(addr_SRAM_safe_mode_release, DATA_LEN_4, write_buffer);

	/* Check ready */
	switch(IC_num) {
		case 0:
			for (loop_count = 1; loop_count <= max_loop; loop_count++) {
				(void)memset(read_buffer, 0, 4);
				himax_mcu_register_read_slave(IC_MASTER, addr_SRAM_start_test, DATA_LEN_4, read_buffer);
				if (read_buffer[0] == 0xEEU) {
					break;
				}
				if(loop_count == max_loop) {
					SRAM_test_status = 1;
				}
				usleep_range(20000, 21100);
			}
			break;
		case 1:
			for (loop_count = 1; loop_count <= max_loop; loop_count++) {
				(void)memset(read_buffer, 0, 4);
				himax_mcu_register_read_slave(IC_MASTER, addr_SRAM_start_test, DATA_LEN_4, read_buffer);
				if (read_buffer[0] == 0xEEU) {
					break;
				}

				if(loop_count == max_loop) {
					SRAM_test_status = 1;
				}
				usleep_range(20000, 21100);
			}
			for (loop_count = 1; loop_count <= max_loop; loop_count++) {
				(void)memset(read_buffer, 0, 4);
				himax_mcu_register_read_slave(IC_SLAVE_1, addr_SRAM_start_test, DATA_LEN_4, read_buffer);
				if (read_buffer[0] == 0xEEU) {
					break;
				}

				if(loop_count == max_loop) {
					SRAM_test_status = 1;
				}
				usleep_range(20000, 21100);
			}
			break;
		case 2:
			for (loop_count = 1; loop_count <= max_loop; loop_count++) {
				(void)memset(read_buffer, 0, 4);
				himax_mcu_register_read_slave(IC_MASTER, addr_SRAM_start_test, DATA_LEN_4, read_buffer);
				if (read_buffer[0] == 0xEEU) {
					break;
				}

				if(loop_count == max_loop) {
					SRAM_test_status = 1;
				}
				usleep_range(20000, 21100);
			}
			for (loop_count = 1; loop_count <= max_loop; loop_count++) {
				(void)memset(read_buffer, 0, 4);
				himax_mcu_register_read_slave(IC_SLAVE_1, addr_SRAM_start_test, DATA_LEN_4, read_buffer);
				if (read_buffer[0] == 0xEEU) {
					break;
				}

				if(loop_count == max_loop) {
					SRAM_test_status = 1;
				}
				usleep_range(20000, 21100);
			}
			for (loop_count = 1; loop_count <= max_loop; loop_count++) {
				(void)memset(read_buffer, 0, 4);
				himax_mcu_register_read_slave(IC_SLAVE_2, addr_SRAM_start_test, DATA_LEN_4, read_buffer);
				if (read_buffer[0] == 0xEEU) {
					break;
				}

				if(loop_count == max_loop) {
					SRAM_test_status = 1;
				}
				usleep_range(20000, 21100);
			}
			break;
		default:
			break;
	}

	if(SRAM_test_status == 1) {
		E("%s: SRAM test fail \n", __func__);
		return SRAM_test_status;
	}

	/* Check result */

	switch(IC_num) {
		case 0:
			(void)memset(read_buffer, 0, 4);
			himax_mcu_register_read_slave(IC_MASTER, addr_SRAM_check_result, DATA_LEN_4, read_buffer);
			if (read_buffer[0] == 0xAAU) {
				I("%s: SRAM master test pass \n", __func__);
			} else {
				SRAM_test_status = 2;
				E("%s: SRAM master test fail \n", __func__);
				(void)memset(read_buffer, 0, 4);
				himax_mcu_register_read_slave(IC_MASTER, addr_SRAM_fail_result, DATA_LEN_4, read_buffer);
				E("%s: fail address : %02X%02X%02X%02X\n", __func__, read_buffer[3],
				 read_buffer[2], read_buffer[1], read_buffer[0]);
			}
			break;
		case 1:
			(void)memset(read_buffer, 0, 4);
			himax_mcu_register_read_slave(IC_MASTER, addr_SRAM_check_result, DATA_LEN_4, read_buffer);
			if (read_buffer[0] == 0xAAU) {
				I("%s: SRAM master test pass \n", __func__);
			} else {
				SRAM_test_status = 2;
				E("%s: SRAM master test fail \n", __func__);
				(void)memset(read_buffer, 0, 4);
				himax_mcu_register_read_slave(IC_MASTER, addr_SRAM_fail_result, DATA_LEN_4, read_buffer);
				E("%s: fail address : %02X%02X%02X%02X\n", __func__, read_buffer[3],
				 read_buffer[2], read_buffer[1], read_buffer[0]);
			}
			himax_mcu_register_read_slave(IC_SLAVE_1, addr_SRAM_check_result, DATA_LEN_4, read_buffer);
			if (read_buffer[0] == 0xAAU) {
				I("%s: SRAM slave1 test pass \n", __func__);
			} else {
				SRAM_test_status = 2;
				E("%s: SRAM slave1 test fail \n", __func__);
				(void)memset(read_buffer, 0, 4);
				himax_mcu_register_read_slave(IC_SLAVE_1, addr_SRAM_fail_result, DATA_LEN_4, read_buffer);
				E("%s: fail address : %02X%02X%02X%02X\n", __func__, read_buffer[3],
				 read_buffer[2], read_buffer[1], read_buffer[0]);
			}
			break;
		case 2:
			(void)memset(read_buffer, 0, 4);
			himax_mcu_register_read_slave(IC_MASTER, addr_SRAM_check_result, DATA_LEN_4, read_buffer);
			if (read_buffer[0] == 0xAAU) {
				I("%s: SRAM master test pass \n", __func__);
			} else {
				SRAM_test_status = 2;
				E("%s: SRAM master test fail \n", __func__);
				(void)memset(read_buffer, 0, 4);
				himax_mcu_register_read_slave(IC_MASTER, addr_SRAM_fail_result, DATA_LEN_4, read_buffer);
				E("%s: fail address : %02X%02X%02X%02X\n", __func__, read_buffer[3],
				 read_buffer[2], read_buffer[1], read_buffer[0]);
			}
			himax_mcu_register_read_slave(IC_SLAVE_1, addr_SRAM_check_result, DATA_LEN_4, read_buffer);
			if (read_buffer[0] == 0xAAU) {
				I("%s: SRAM slave1 test pass \n", __func__);
			} else {
				SRAM_test_status = 2;
				E("%s: SRAM slave1 test fail \n", __func__);
				(void)memset(read_buffer, 0, 4);
				himax_mcu_register_read_slave(IC_SLAVE_1, addr_SRAM_fail_result, DATA_LEN_4, read_buffer);
				E("%s: fail address : %02X%02X%02X%02X\n", __func__, read_buffer[3],
				 read_buffer[2], read_buffer[1], read_buffer[0]);
			}
			himax_mcu_register_read_slave(IC_SLAVE_2, addr_SRAM_check_result, DATA_LEN_4, read_buffer);
			if (read_buffer[0] == 0xAAU) {
				I("%s: SRAM slave2 test pass \n", __func__);
			} else {
				SRAM_test_status = 2;
				E("%s: SRAM slave2 test fail \n", __func__);
				(void)memset(read_buffer, 0, 4);
				himax_mcu_register_read_slave(IC_SLAVE_2, addr_SRAM_fail_result, DATA_LEN_4, read_buffer);
				E("%s: fail address : %02X%02X%02X%02X\n", __func__, read_buffer[3],
				 read_buffer[2], read_buffer[1], read_buffer[0]);
			}
			break;
		default:
			break;
	}

	return SRAM_test_status;
}

int himax_mcu_SRAM_run_test_ISRAM_function(uint8_t *write_buffer, uint8_t *read_buffer, uint8_t IC_num, int max_loop)
{
	int loop_count;
	int SRAM_test_status = 0;
	uint8_t tmp_buffer[4] = {0};

	(void)memcpy(tmp_buffer, write_buffer, DATA_LEN_4);

	/* Set ilm_boot to AHB */
	(void)memset(write_buffer, 0, sizeof(write_buffer));
	write_buffer[0] = 0x66U;
	himax_mcu_register_write_all_slave(addr_SRAM_ilm_set, DATA_LEN_4, write_buffer);

	/* Set default_ivb to 0x10 */
	(void)memset(write_buffer, 0, sizeof(write_buffer));
	write_buffer[0] = 0x10U;
	himax_mcu_register_write_all_slave(addr_SRAM_default_ivb_set, DATA_LEN_4, write_buffer);

	/* Start test */
	(void)memset(write_buffer, 0, 4);
	write_buffer[0] = 0xA5U;
	himax_mcu_register_write_all_slave(addr_SRAM_start_test, DATA_LEN_4, write_buffer);

	himax_mcu_register_write_all_slave(addr_SRAM_set_voltage, DATA_LEN_4, tmp_buffer);

	/* Option 1 for D&T SRAM test, Option 2 for ISRAM test */
	(void)memset(write_buffer, 0, 4);
	write_buffer[0] = 0x5AU;
	himax_mcu_register_write_all_slave(addr_SRAM_test_option, DATA_LEN_4, write_buffer);

	/* Set MCU Wake Up*/
	(void)memset(write_buffer, 0, 4);
	write_buffer[0] = 0x53U;
	himax_mcu_register_write_all_slave(addr_SRAM_safe_mode_release, DATA_LEN_4, write_buffer);

	/* Check ready */
	switch(IC_num) {
		case 0:
			for (loop_count = 1; loop_count <= max_loop; loop_count++) {
				(void)memset(read_buffer, 0, 4);
				himax_mcu_register_read_slave(IC_MASTER, addr_SRAM_start_test, DATA_LEN_4, read_buffer);
				if (read_buffer[0] == 0xEEU) {
					break;
				}
				if(loop_count == max_loop) {
					SRAM_test_status = 1;
				}
				usleep_range(20000, 21100);
			}
			break;
		case 1:
			for (loop_count = 1; loop_count <= max_loop; loop_count++) {
				(void)memset(read_buffer, 0, 4);
				himax_mcu_register_read_slave(IC_MASTER, addr_SRAM_start_test, DATA_LEN_4, read_buffer);
				if (read_buffer[0] == 0xEEU) {
					break;
				}

				if(loop_count == max_loop) {
					SRAM_test_status = 1;
				}
				usleep_range(20000, 21100);
			}
			for (loop_count = 1; loop_count <= max_loop; loop_count++) {
				(void)memset(read_buffer, 0, 4);
				himax_mcu_register_read_slave(IC_SLAVE_1, addr_SRAM_start_test, DATA_LEN_4, read_buffer);
				if (read_buffer[0] == 0xEEU) {
					break;
				}

				if(loop_count == max_loop) {
					SRAM_test_status = 1;
				}
				usleep_range(20000, 21100);
			}
			break;
		case 2:
			for (loop_count = 1; loop_count <= max_loop; loop_count++) {
				(void)memset(read_buffer, 0, 4);
				himax_mcu_register_read_slave(IC_MASTER, addr_SRAM_start_test, DATA_LEN_4, read_buffer);
				if (read_buffer[0] == 0xEEU) {
					break;
				}

				if(loop_count == max_loop) {
					SRAM_test_status = 1;
				}
				usleep_range(20000, 21100);
			}
			for (loop_count = 1; loop_count <= max_loop; loop_count++) {
				(void)memset(read_buffer, 0, 4);
				himax_mcu_register_read_slave(IC_SLAVE_1, addr_SRAM_start_test, DATA_LEN_4, read_buffer);
				if (read_buffer[0] == 0xEEU) {
					break;
				}

				if(loop_count == max_loop) {
					SRAM_test_status = 1;
				}
				usleep_range(20000, 21100);
			}
			for (loop_count = 1; loop_count <= max_loop; loop_count++) {
				(void)memset(read_buffer, 0, 4);
				himax_mcu_register_read_slave(IC_SLAVE_2, addr_SRAM_start_test, DATA_LEN_4, read_buffer);
				if (read_buffer[0] == 0xEEU) {
					break;
				}

				if(loop_count == max_loop) {
					SRAM_test_status = 1;
				}
				usleep_range(20000, 21100);
			}
			break;
		default:
			break;
	}

	if(SRAM_test_status == 1) {
		E("%s: SRAM test fail \n", __func__);
		return SRAM_test_status;
	}

	/* Check result */

	switch(IC_num) {
		case 0:
			(void)memset(read_buffer, 0, 4);
			himax_mcu_register_read_slave(IC_MASTER, addr_SRAM_check_result, DATA_LEN_4, read_buffer);
			if (read_buffer[0] == 0xAAU) {
				I("%s: SRAM test pass \n", __func__);
			} else {
				SRAM_test_status = 2;
				E("%s: SRAM test fail \n", __func__);
				(void)memset(read_buffer, 0, 4);
				himax_mcu_register_read_slave(IC_MASTER, addr_SRAM_fail_result, DATA_LEN_4, read_buffer);
				E("%s: fail address : %02X%02X%02X%02X\n", __func__, read_buffer[3],
				 read_buffer[2], read_buffer[1], read_buffer[0]);
			}
			break;
		case 1:
			(void)memset(read_buffer, 0, 4);
			himax_mcu_register_read_slave(IC_MASTER, addr_SRAM_check_result, DATA_LEN_4, read_buffer);
			if (read_buffer[0] == 0xAAU) {
				I("%s: SRAM test pass \n", __func__);
			} else {
				SRAM_test_status = 2;
				E("%s: SRAM test fail \n", __func__);
				(void)memset(read_buffer, 0, 4);
				himax_mcu_register_read_slave(IC_MASTER, addr_SRAM_fail_result, DATA_LEN_4, read_buffer);
				E("%s: fail address : %02X%02X%02X%02X\n", __func__, read_buffer[3],
				 read_buffer[2], read_buffer[1], read_buffer[0]);
			}
			himax_mcu_register_read_slave(IC_SLAVE_1, addr_SRAM_check_result, DATA_LEN_4, read_buffer);
			if (read_buffer[0] == 0xAAU) {
				I("%s: SRAM test pass \n", __func__);
			} else {
				SRAM_test_status = 2;
				E("%s: SRAM test fail \n", __func__);
				(void)memset(read_buffer, 0, 4);
				himax_mcu_register_read_slave(IC_SLAVE_1, addr_SRAM_fail_result, DATA_LEN_4, read_buffer);
				E("%s: fail address : %02X%02X%02X%02X\n", __func__, read_buffer[3],
				 read_buffer[2], read_buffer[1], read_buffer[0]);
			}
			break;
		case 2:
			(void)memset(read_buffer, 0, 4);
			himax_mcu_register_read_slave(IC_MASTER, addr_SRAM_check_result, DATA_LEN_4, read_buffer);
			if (read_buffer[0] == 0xAAU) {
				I("%s: SRAM test pass \n", __func__);
			} else {
				SRAM_test_status = 2;
				E("%s: SRAM test fail \n", __func__);
				(void)memset(read_buffer, 0, 4);
				himax_mcu_register_read_slave(IC_MASTER, addr_SRAM_fail_result, DATA_LEN_4, read_buffer);
				E("%s: fail address : %02X%02X%02X%02X\n", __func__, read_buffer[3],
				 read_buffer[2], read_buffer[1], read_buffer[0]);
			}
			himax_mcu_register_read_slave(IC_SLAVE_1, addr_SRAM_check_result, DATA_LEN_4, read_buffer);
			if (read_buffer[0] == 0xAAU) {
				I("%s: SRAM test pass \n", __func__);
			} else {
				SRAM_test_status = 2;
				E("%s: SRAM test fail \n", __func__);
				(void)memset(read_buffer, 0, 4);
				himax_mcu_register_read_slave(IC_SLAVE_1, addr_SRAM_fail_result, DATA_LEN_4, read_buffer);
				E("%s: fail address : %02X%02X%02X%02X\n", __func__, read_buffer[3],
				 read_buffer[2], read_buffer[1], read_buffer[0]);
			}
			himax_mcu_register_read_slave(IC_SLAVE_2, addr_SRAM_check_result, DATA_LEN_4, read_buffer);
			if (read_buffer[0] == 0xAAU) {
				I("%s: SRAM test pass \n", __func__);
			} else {
				SRAM_test_status = 2;
				E("%s: SRAM test fail \n", __func__);
				(void)memset(read_buffer, 0, 4);
				himax_mcu_register_read_slave(IC_SLAVE_2, addr_SRAM_fail_result, DATA_LEN_4, read_buffer);
				E("%s: fail address : %02X%02X%02X%02X\n", __func__, read_buffer[3],
				 read_buffer[2], read_buffer[1], read_buffer[0]);
			}
			break;
		default:
			break;
	}

	return SRAM_test_status;
}

void himax_mcu_SRAM_bus_reset_function(void)
{
	uint8_t write_buffer[4] = {0};
	uint8_t read_buffer[4] = {0};
	uint8_t IC_num = private_ts->slave_ic_num;

	/* AMBA reset */
	(void)memset(write_buffer, 0, sizeof(write_buffer));
	write_buffer[0] = 0xA5;
	himax_mcu_register_write_all_slave(addr_SRAM_amba_bus_reset, DATA_LEN_4, write_buffer);

	/* Sense off flow */
	g_core_fp.fp_sense_off();
	switch(IC_num) {
		case 0:
			himax_mcu_register_read_slave(IC_MASTER, addr_cs_central_state, DATA_LEN_4, read_buffer);
			break;
		case 1:
			himax_mcu_register_read_slave(IC_MASTER, addr_cs_central_state, DATA_LEN_4, read_buffer);
			himax_mcu_register_read_slave(IC_SLAVE_1, addr_cs_central_state, DATA_LEN_4, read_buffer);
			break;
		case 2:
			himax_mcu_register_read_slave(IC_MASTER, addr_cs_central_state, DATA_LEN_4, read_buffer);
			himax_mcu_register_read_slave(IC_SLAVE_1, addr_cs_central_state, DATA_LEN_4, read_buffer);
			himax_mcu_register_read_slave(IC_SLAVE_2, addr_cs_central_state, DATA_LEN_4, read_buffer);
			break;
		default:
			break;
	}

	/* WDTDIS */
	(void)memset(write_buffer, 0, sizeof(write_buffer));
	write_buffer[0] = 0x53U;
	write_buffer[1] = 0xACU;
	himax_mcu_register_write_all_slave(addr_SRAM_WDTDIS, DATA_LEN_4, write_buffer);

}

static void himax_mcu_program_SRAM_test_2K_FW(uint8_t * FW_data, int len)
{
	uint8_t write_buffer[4] = {0};
	uint32_t counter = 0;
	int flash_size;
	uint32_t address_offset = counter * 256U;
	uint32_t write_address = 0x20000000U + address_offset;
	uint8_t *write_data_ptr = &FW_data[address_offset];

	/* program FW to ISRAM */

	(void)memset(write_buffer, 0, sizeof(write_buffer));
	write_buffer[3] = 0x20U;
	counter = 0;
	for (flash_size = len; flash_size > 0; flash_size = flash_size-256) {
		address_offset = counter * 256U;
		write_address = 0x20000000U + address_offset;
		write_data_ptr = &FW_data[address_offset];
		if (flash_size > 256) {
			himax_mcu_register_write_all_slave(write_address, 256U, write_data_ptr);
		} else {
			himax_mcu_register_write_all_slave(write_address, flash_size, write_data_ptr);
		}
		counter++;
	}
}

int himax_mcu_SRAM_test(unsigned char *fw, int len)
{
	uint8_t write_buffer[4] = {0};
	uint8_t read_buffer[4] = {0};
	uint8_t ori_buffer[4] = {0};
	uint8_t * FW_data;
	uint8_t IC_num = private_ts->slave_ic_num;
	int max_loop = 200;
	int SRAM_test_status = 0;
	bool SRAM_test_1rd_status = true;
	uint8_t i = 0;
	uint8_t j = 0;


	himax_mcu_register_read_slave(IC_MASTER, addr_SRAM_set_voltage, DATA_LEN_4, ori_buffer);

	FW_data = kzalloc( sizeof(uint8_t) * (size_t)len, GFP_KERNEL);
	(void)memcpy(FW_data, fw, len);

	himax_mcu_SRAM_bus_reset_function();

	himax_mcu_program_SRAM_test_2K_FW(FW_data, len);

	(void)memset(write_buffer, 0, sizeof(write_buffer));
	for( i=0U ; i<=IC_num ; i++) {
		himax_mcu_write_dd_reg_password_sram_test(i);
		while( j<100U ) {
			himax_mcu_dd_reg_read(0xBCU, 1, 1, read_buffer, 0, i);
			if(read_buffer[0] != ((uint8_t) DATA_SRAM_1RD_TEST_VOLTAGE)) {
				write_buffer[0] = ((uint8_t) DATA_SRAM_1RD_TEST_VOLTAGE);
				himax_mcu_dd_reg_write(0xBCU, 1, 1, write_buffer, 0, i);
			} else {
				break;
			}
			msleep(100);
			j++;
		}
		j=0U;

		himax_mcu_clear_dd_reg_password_sram_test(i);
	}


	(void)memset(write_buffer, 0, sizeof(write_buffer));
	write_buffer[1] = DATA_SRAM_1RD_TEST_LOOP;		/* Stress test number 1 */
	SRAM_test_status = himax_mcu_SRAM_run_test_DTSRAM_function(write_buffer, read_buffer, IC_num, max_loop);

	if(SRAM_test_status == 1){
		(void)memset(write_buffer, 0, sizeof(write_buffer));
		write_buffer[0] = 0xA5U;
		himax_mcu_register_write_all_slave(addr_SRAM_amba_bus_reset, DATA_LEN_4, write_buffer);
		E("%s: SRAM test fail \n", __func__);
		return SRAM_test_status;
	}

	if(SRAM_test_status == 2){
		SRAM_test_1rd_status = false;
	}

	himax_mcu_SRAM_bus_reset_function();

	(void)memset(write_buffer, 0, sizeof(write_buffer));
	write_buffer[1] = DATA_SRAM_1RD_TEST_LOOP;		/* Stress test number 1 */
	SRAM_test_status = himax_mcu_SRAM_run_test_ISRAM_function(write_buffer, read_buffer, IC_num, max_loop);

	if(SRAM_test_status == 1){
		(void)memset(write_buffer, 0, sizeof(write_buffer));
		write_buffer[0] = 0xA5U;
		himax_mcu_register_write_all_slave(addr_SRAM_amba_bus_reset, DATA_LEN_4, write_buffer);
		E("%s: SRAM test fail \n", __func__);
		return SRAM_test_status;
	}

	if(SRAM_test_status == 2){
		SRAM_test_1rd_status = false;
	}

	/*IF PASS TEST 1.35 V*/
	if (SRAM_test_1rd_status) {
		himax_mcu_SRAM_bus_reset_function();
		/*VDDD 1.35*/
		(void)memset(write_buffer, 0, sizeof(write_buffer));
		for( i=0U ; i<=IC_num ; i++) {
			himax_mcu_write_dd_reg_password_sram_test(i);

			while( j<100U ) {
				himax_mcu_dd_reg_read(0xBCU, 1, 1, read_buffer, 0, i);
				if(read_buffer[0] != ((uint8_t) DATA_SRAM_3RD_TEST_VOLTAGE) ) {
					write_buffer[0] = ((uint8_t) DATA_SRAM_3RD_TEST_VOLTAGE);
					himax_mcu_dd_reg_write(0xBCU, 1, 1, write_buffer, 0, i);
				} else {
					break;
				}
				msleep(100);
				j++;
			}
			j=0U;

			himax_mcu_clear_dd_reg_password_sram_test(i);
		}

		(void)memset(write_buffer, 0U, sizeof(write_buffer));
		write_buffer[1] = ((uint8_t) DATA_SRAM_3RD_TEST_LOOP);		/* Stress test number 10 */
		SRAM_test_status = himax_mcu_SRAM_run_test_DTSRAM_function(write_buffer, read_buffer, IC_num, max_loop);

		if(SRAM_test_status == 1){
			(void)memset(write_buffer, 0U, sizeof(write_buffer));
			write_buffer[0] = 0xA5;
			himax_mcu_register_write_all_slave(addr_SRAM_amba_bus_reset, DATA_LEN_4, write_buffer);
			E("%s: SRAM test fail \n", __func__);
			return SRAM_test_status;
		}

		if(SRAM_test_status == 2){
			return SRAM_test_status;
		}

		himax_mcu_SRAM_bus_reset_function();

		(void)memset(write_buffer, 0U, sizeof(write_buffer));
		write_buffer[1] = ((uint8_t) DATA_SRAM_3RD_TEST_LOOP);		/* Stress test number 10 */
		SRAM_test_status = himax_mcu_SRAM_run_test_ISRAM_function(write_buffer, read_buffer, IC_num, max_loop);

		if(SRAM_test_status == 1){
			(void)memset(write_buffer, 0U, sizeof(write_buffer));
			write_buffer[0] = 0xA5U;
			himax_mcu_register_write_all_slave(addr_SRAM_amba_bus_reset, DATA_LEN_4, write_buffer);
			E("%s: SRAM test fail \n", __func__);
			return SRAM_test_status;
		}

		if(SRAM_test_status == 2){
			return SRAM_test_status;
		}


	} else {
		if(BOOL_SRAM_2RD_SAVEBACK) {
			himax_mcu_SRAM_bus_reset_function();

			(void)memset(write_buffer, 0U, sizeof(write_buffer));
			for( i=0U ; i<=IC_num ; i++) {
				himax_mcu_write_dd_reg_password_sram_test(i);

				while( j<100U ) {
					himax_mcu_dd_reg_read(0xBCU, 1, 1, read_buffer, 0, i);
					if(read_buffer[0] != ((uint8_t) DATA_SRAM_2RD_TEST_VOLTAGE)) {
						write_buffer[0] = ((uint8_t) DATA_SRAM_2RD_TEST_VOLTAGE);
						himax_mcu_dd_reg_write(0xBCU, 1, 1, write_buffer, 0, i);
					} else {
						break;
					}
					msleep(100);
					j++;
				}
				j=0U;

				himax_mcu_clear_dd_reg_password_sram_test(i);
			}

			(void)memset(write_buffer, 0U, sizeof(write_buffer));
			write_buffer[1] = ((uint8_t) DATA_SRAM_2RD_TEST_LOOP);		/* Stress test number 30 */
			SRAM_test_status = himax_mcu_SRAM_run_test_DTSRAM_function(write_buffer, read_buffer, IC_num, max_loop);

			if(SRAM_test_status == 1){
				(void)memset(write_buffer, 0U, sizeof(write_buffer));
				write_buffer[0] = 0xA5U;
				himax_mcu_register_write_all_slave(addr_SRAM_amba_bus_reset, DATA_LEN_4, write_buffer);
				E("%s: SRAM test fail \n", __func__);
				return SRAM_test_status;
			}

			if(SRAM_test_status == 2){
				return SRAM_test_status;
			}

			himax_mcu_SRAM_bus_reset_function();

			(void)memset(write_buffer, 0U, sizeof(write_buffer));
			write_buffer[1] = DATA_SRAM_2RD_TEST_LOOP;		/* Stress test number 30 */
			SRAM_test_status = himax_mcu_SRAM_run_test_ISRAM_function(write_buffer, read_buffer, IC_num, max_loop);

			if(SRAM_test_status == 1){
				(void)memset(write_buffer, 0, sizeof(write_buffer));
				write_buffer[0] = 0xA5U;
				himax_mcu_register_write_all_slave(addr_SRAM_amba_bus_reset, DATA_LEN_4, write_buffer);
				E("%s: SRAM test fail \n", __func__);
				return SRAM_test_status;
			}

			if(SRAM_test_status == 2){
				return SRAM_test_status;
			}
			himax_mcu_SRAM_bus_reset_function();
			/*VDDD 1.35*/
			(void)memset(write_buffer, 0, sizeof(write_buffer));
			for( i=0U ; i<=IC_num ; i++) {
				himax_mcu_write_dd_reg_password_sram_test(i);

				while( j<100U ) {
					himax_mcu_dd_reg_read(0xBCU, 1, 1, read_buffer, 0, i);
					if(read_buffer[0] != ((uint8_t) DATA_SRAM_3RD_TEST_VOLTAGE)) {
						write_buffer[0] = ((uint8_t) DATA_SRAM_3RD_TEST_VOLTAGE);
						himax_mcu_dd_reg_write(0xBCU, 1, 1, write_buffer, 0, i);
					} else {
						break;
					}
					msleep(100);
					j++;
				}
				j=0U;

				himax_mcu_clear_dd_reg_password_sram_test(i);
			}

			(void)memset(write_buffer, 0U, sizeof(write_buffer));
			write_buffer[1] = ((uint8_t) DATA_SRAM_3RD_TEST_LOOP);		/* Stress test number 10 */
			SRAM_test_status = himax_mcu_SRAM_run_test_DTSRAM_function(write_buffer, read_buffer, IC_num, max_loop);

			if(SRAM_test_status == 1){
				(void)memset(write_buffer, 0U, sizeof(write_buffer));
				write_buffer[0] = 0xA5U;
				himax_mcu_register_write_all_slave(addr_SRAM_amba_bus_reset, DATA_LEN_4, write_buffer);
				E("%s: SRAM test fail \n", __func__);
				return SRAM_test_status;
			}

			if(SRAM_test_status == 2){
				return SRAM_test_status;
			}

			himax_mcu_SRAM_bus_reset_function();

			(void)memset(write_buffer, 0, sizeof(write_buffer));
			write_buffer[1] = ((uint8_t) DATA_SRAM_3RD_TEST_LOOP);		/* Stress test number 10 */
			SRAM_test_status = himax_mcu_SRAM_run_test_ISRAM_function(write_buffer, read_buffer, IC_num, max_loop);

			if(SRAM_test_status == 1){
				(void)memset(write_buffer, 0U, sizeof(write_buffer));
				write_buffer[0] = 0xA5U;
				himax_mcu_register_write_all_slave(addr_SRAM_amba_bus_reset, DATA_LEN_4, write_buffer);
				E("%s: SRAM test fail \n", __func__);
				return SRAM_test_status;
			}

			if(SRAM_test_status == 2){
				return SRAM_test_status;
			}

		}
		else {
			E("%s: SRAM test fail\n", __func__);
		}
	}

	(void)memset(write_buffer, 0, sizeof(write_buffer));
	write_buffer[0] = 0xA5;
	himax_mcu_register_write_all_slave(addr_SRAM_amba_bus_reset, DATA_LEN_4, write_buffer);
	g_core_fp.fp_sense_on();
	himax_mcu_register_write_slave(IC_MASTER, addr_SRAM_set_voltage, DATA_LEN_4, ori_buffer);

	kfree(FW_data);
	FW_data = NULL;

	himax_mcu_tp_reset();

	return SRAM_test_status;
}

#if (HX_BOOT_UPGRADE == 0x01)
bool himax_mcu_flash_lastdata_check_with_bin(uint32_t size)
{
	uint32_t start_addr = 0xFFFFFFFFU;
	uint8_t flash_buffer[DATA_LEN_4];
	uint8_t i = 0;

	start_addr = size - DATA_LEN_4;
	himax_mcu_register_read(start_addr, DATA_LEN_4, &flash_buffer[0]);

	for (i = 1; i <= DATA_LEN_4; i++) {
		if (flash_buffer[DATA_LEN_4 - i] != hxfw->data[size - i]) {
			E("%s: Flash content is different from BIN file, Need Update\n",
			  __func__);
			I("FLASH[%08X] ~ FLASH[%08X] = %02X%02X%02X%02X\n",
			  size - 4, size - 1, flash_buffer[DATA_LEN_4 - 4],
			  flash_buffer[DATA_LEN_4 - 3],
			  flash_buffer[DATA_LEN_4 - 2],
			  flash_buffer[DATA_LEN_4 - 1]);
			I("BIN[%08X] ~ BIN[%08X] = %02X%02X%02X%02X\n",
			  size - 4, size - 1, hxfw->data[size - 4],
			  hxfw->data[size - 3], hxfw->data[size - 2],
			  hxfw->data[size - 1]);
			return 1;
		}
	}

	return 0;
}

static bool hx_bin_desc_data_get(uint32_t addr, uint8_t *flash_buf)
{
	uint8_t data_sz = 0x10;
	uint32_t i = 0;
	uint32_t j = 0;
	uint16_t chk_end = 0;
	uint16_t chk_sum = 0;
	uint32_t map_code = 0;
	uint32_t flash_addr = 0;
	uint32_t flash_size = 0;

	for (i = 0; i < FW_PAGE_SZ; i = i + data_sz) {
		for (j = i; j < (i + data_sz); j++) {
			chk_end |= flash_buf[j];
			chk_sum += flash_buf[j];
		}
		if (chk_end == 0U) { /*1. Check all zero*/
			I("%s: End in [%08X]\n", __func__, i + addr);
			return false;
		} else if ((chk_sum % 0x100U) != 0U) { /*2. Check sum*/
			I("%s: chk sum failed in %X\n", __func__, i + addr);
		} else { /*3. get data*/
			map_code = (uint32_t)flash_buf[i] +
				((uint32_t)flash_buf[i + 1U] << 8U) +
				((uint32_t)flash_buf[i + 2U] << 16U) +
				((uint32_t)flash_buf[i + 3U] << 24U);
			flash_addr = (uint32_t)flash_buf[i + 4U] +
				((uint32_t)flash_buf[i + 5U] << 8U) +
				((uint32_t)flash_buf[i + 6U] << 16U) +
				((uint32_t)flash_buf[i + 7U] << 24U);
			flash_size = (uint32_t)flash_buf[i + 8U] +
				((uint32_t)flash_buf[i + 9U] << 8U) +
				((uint32_t)flash_buf[i + 10U] << 16U) +
				((uint32_t)flash_buf[i + 11U] << 24U);	
			switch (map_code) {
			case FW_CID:
				CID_VER_MAJ_FLASH_ADDR = flash_addr;
				CID_VER_MIN_FLASH_ADDR = flash_addr + 1U;
				I("%s: CID in [%08X]\n", __func__,
				CID_VER_MAJ_FLASH_ADDR);
				break;
			case FW_VER:
				FW_VER_MAJ_FLASH_ADDR = flash_addr;
				FW_VER_MIN_FLASH_ADDR = flash_addr + 1U;
				I("%s: FW_VER in [%08X]\n", __func__,
				FW_VER_MAJ_FLASH_ADDR);
				break;
			case CFG_VER:
				CFG_VER_MAJ_FLASH_ADDR = flash_addr;
				CFG_VER_MIN_FLASH_ADDR = flash_addr + 1U;
				I("%s: CFG_VER in = [%08X]\n", __func__,
				CFG_VER_MAJ_FLASH_ADDR);
				break;
			case TP_CONFIG_TABLE:
				CFG_TABLE_FLASH_ADDR = (uint32_t)flash_addr;
				I("%s: CONFIG_TABLE in [%08X]\n", __func__,
				CFG_TABLE_FLASH_ADDR);
				break;
#if defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX8530)
			case HX8530_ALG_2_SECTION:
				HX8530_ALG_2_SECTION_ADDR = (uint32_t)flash_addr;
				HX8530_ALG_2_SECTION_SIZE =  (uint32_t)flash_size;
				I("%s: HX8530_ALG_2_SECTION in [%08X]\n", __func__,
				HX8530_ALG_2_SECTION_ADDR);
				I("%s: HX8530_ALG_2_SECTION_SIZE= %08X\n", __func__,
				HX8530_ALG_2_SECTION_SIZE);
				break;
			case HX8530_CFG_1_SECTION:
				HX8530_CFG_1_SECTION_ADDR = (uint32_t)flash_addr;
				HX8530_CFG_1_SECTION_SIZE =  (uint32_t)flash_size;
				I("%s: HX8530_CFG_1_SECTION in [%08X]\n", __func__,
				HX8530_CFG_1_SECTION_ADDR);				
				I("%s: HX8530_CFG_1_SECTION_SIZE= %08X\n", __func__,
				HX8530_CFG_1_SECTION_SIZE);
				break;
#endif
			default:
				/*do nothing*/
				break;
			}
		}
		chk_end = 0;
		chk_sum = 0;
	}

	return true;
}

bool hx_mcu_bin_desc_get(unsigned char *fw, uint32_t max_sz)
{
	uint32_t addr_t = 0;
	unsigned char *fw_buf = NULL;
	bool keep_on_flag = false;
	bool g_bin_desc_flag = false;

	do {
		fw_buf = &fw[addr_t];

		/*Check bin is with description table or not*/
		if (!g_bin_desc_flag) {
			if ((fw_buf[0x00] == 0x00U) && (fw_buf[0x01] == 0x00U) &&
			    (fw_buf[0x02] == 0x00U) && (fw_buf[0x03] == 0x00U) &&
			    (fw_buf[0x04] == 0x00U) && (fw_buf[0x05] == 0x00U) &&
			    (fw_buf[0x06] == 0x00U) && (fw_buf[0x07] == 0x00U) &&
			    (fw_buf[0x0E] == 0x87U)) {
				g_bin_desc_flag = true;
			}
		}
		if (!g_bin_desc_flag) {
			I("%s: fw_buf[0x00] = %2X, fw_buf[0x0E] = %2X\n",
			  __func__, fw_buf[0x00], fw_buf[0x0E]);
			I("%s: No description table\n", __func__);
			break;
		}

		/*Get related data*/
		keep_on_flag = hx_bin_desc_data_get(addr_t, fw_buf);

		addr_t = addr_t + FW_PAGE_SZ;
	} while ((max_sz > addr_t) &&keep_on_flag);

	return g_bin_desc_flag;
}
#endif
/* FLASH side end*/
/* CORE_FLASH */

/* CORE_SRAM */
bool himax_mcu_get_DSRAM_data(uint8_t *tmp_rawdata)
{
	unsigned int i = 0;
	unsigned char tmp_data[DATA_LEN_4];
	uint8_t max_i2c_size = MAX_I2C_TRANS_SZ;

	uint16_t x_num = ic_data->HX_RX_NUM;
	uint16_t y_num = ic_data->HX_TX_NUM;
	uint16_t total_data_size = ((x_num * y_num) + x_num + y_num) * 2U;

	uint16_t total_size = (((x_num * y_num) + x_num + y_num) * 2U) + 4U;
	uint8_t *rawdata_buffer = NULL;
	uint16_t check_sum_cal = 0;

	rawdata_buffer = kcalloc((total_size + 8U), sizeof(uint8_t), GFP_KERNEL);
	if (rawdata_buffer == NULL) {
		E("%s, Failed to allocate memory\n", __func__);
		return false;
	}


	/* 1. Start DSRAM Rawdata and Wait Data Ready */
	tmp_data[3] = 0x00;
	tmp_data[2] = 0x00;
	tmp_data[1] = 0x5A;
	tmp_data[0] = 0xA5;

	if (himax_write_read_reg(addr_rawdata, tmp_data, 0xA5, 0x5A) < 0) {
		I("%s 1.Data NOT ready => bypass\n", __func__);
		himax_mcu_read_FW_status();
		goto FAIL_Lable;
	}

	/* 2. Read RawData */
	for (i = 0; i < total_size; i = i + max_i2c_size) {
		/*I("%s address = %08X\n", __func__, (addr_rawdata + i));*/
		if ((total_size - i) >= max_i2c_size) {
			himax_mcu_register_read(
				(addr_rawdata + i), max_i2c_size,
				&rawdata_buffer[i]);
		} else {
			himax_mcu_register_read(
				(addr_rawdata + i), (total_size - i),
				&rawdata_buffer[i]);
		}
	}

	/* 3. FW stop outputing */
	tmp_data[3] = rawdata_buffer[3];
	tmp_data[2] = rawdata_buffer[2];
	tmp_data[1] = 0x00;
	tmp_data[0] = 0x00;

	if (himax_write_read_reg(addr_rawdata, tmp_data, 0x00, 0x00) < 0) {
		I("%s 2. Data NOT ready => bypass\n", __func__);
		himax_mcu_read_FW_status();
		goto FAIL_Lable;
	}

	/* 4. Data Checksum Check */
	for (i = 2U; i < total_size; i += 2U) { /*PASSWORD NOT included */
		check_sum_cal +=
			((rawdata_buffer[i + 1U] * 256U) + rawdata_buffer[i]);
	}

	if ((check_sum_cal % 0x10000U) != 0U) {
		I("%s check_sum_cal fail=%2X\n", __func__, check_sum_cal);
		goto FAIL_Lable;
	} else {

		(void)memcpy(tmp_rawdata, &rawdata_buffer[4],
			total_data_size * sizeof(uint8_t));

		/*I("%s checksum PASS\n", __func__);*/
	}
	kfree(rawdata_buffer);
	rawdata_buffer = NULL;
	return true;
FAIL_Lable:
	kfree(rawdata_buffer);
	rawdata_buffer = NULL;
	return false;
}
/* CORE_SRAM */
#if (HX_BOOT_UPGRADE == 0x01)
/*-------------------------------------------------------------------------
 *
 *	Description:  Read FW_VER and CFG_VER value from FW file
 *
 *	Parameters: void
 *
 *	Returns: int (0 success/ 1 fail)
 *
 */
int himax_mcu_fw_ver_bin(void)
{
	I("%s: use default incell address.\n", __func__);
	if (hxfw != NULL) {
		I("Catch fw version in bin file!\n");
		g_i_FW_VER = ((uint16_t)hxfw->data[FW_VER_MAJ_FLASH_ADDR] << 8) |
			    (uint16_t)hxfw->data[FW_VER_MIN_FLASH_ADDR];
		g_i_CFG_VER = ((uint16_t)hxfw->data[CFG_VER_MAJ_FLASH_ADDR] << 8) |
			    (uint16_t)hxfw->data[CFG_VER_MIN_FLASH_ADDR];
		g_i_CID_MAJ = hxfw->data[CID_VER_MAJ_FLASH_ADDR];
		g_i_CID_MIN = hxfw->data[CID_VER_MIN_FLASH_ADDR];
	} else {
		I("FW data is null!\n");
		return 1;
	}
	return NO_ERR;
}
#endif

#if (HX_RST_PIN_FUNC == 0x01)
bool himax_mcu_tp_lcm_pin_reset(void)
{
	bool ret = false;

	if (gpio_is_valid(private_ts->rst_gpio) && gpio_is_valid(private_ts->lcm_gpio)) {
		I("%s: Now reset the Touch chip and LCM.\n", __func__);
		himax_gpio_set(private_ts->rst_gpio, 0);
		himax_gpio_set(private_ts->lcm_gpio, 0);
		msleep(60);
		himax_gpio_set(private_ts->lcm_gpio, 1);
		msleep(110);
		himax_gpio_set(private_ts->rst_gpio, 1);
		msleep(20);
	} else {
		ret = true;
		if (!gpio_is_valid(private_ts->rst_gpio)) {
			E("%s: Please check tp rst pin, pin is invalid .\n", __func__);
		}
		if (!gpio_is_valid(private_ts->lcm_gpio)) {
			E("%s: Please check LCM rst pin, pin is invalid .\n", __func__);
		}
	}
	return ret;
}

void himax_mcu_toggle_rst_gpio(void)
{
	I("%s: Now reset the Touch chip.\n", __func__);
	if (private_ts->pdata->g_customer_control_tp_reset == 1) {
		/* please add control TP_EXT_RSTN function in here */
		E("%s: unable control TP_EXT_RSTN, please check it\n", __func__);
	} else {
		himax_gpio_set(private_ts->rst_gpio, 0);
		msleep(20);
		himax_gpio_set(private_ts->rst_gpio, 1);
		msleep(100);
	}
}

void himax_mcu_hw_reset(bool int_off)
{
	struct himax_ts_data *ts = private_ts;

	I("%s: int_off=%d\n", __func__, int_off);

	if (ts->rst_gpio >= 0) {
		if (int_off) {
			himax_mcu_irq_switch(0);
		}

		himax_mcu_toggle_rst_gpio();

		if (int_off) {
			himax_mcu_irq_switch(1);
		}
	}
#if defined(HIMAX_I2C_PLATFORM)
	himax_mcu_interface_on();
#endif
}
#elif (HX_RST_PIN_FUNC == 0x02)
	/* Need Customer set TP reset pin and follow sequence time
	 * Need at least two function control
	 * 1.himax_mcu_tp_lcm_pin_reset control TP and LCM reset pin for AP recovery
	 * 2.himax_mcu_toggle_rst_gpio simple tp hardware reset pin
	 */
#endif

void himax_mcu_tp_reset(void)
{
	I("%s,Enter\n", __func__);
#if (HX_RST_PIN_FUNC == 0x01)
	himax_mcu_hw_reset(false);
#elif (HX_RST_PIN_FUNC == 0x02)
	/* Need Customer do TP reset pin */
#else
	himax_mcu_system_reset();
#endif
}
/*-------------------------------------------------------------------------
 *
 *	Description: Compare DTS/FW/FIX_INFO data is match or not.
 *
 *	Parameters: void
 *
 *	Returns: void
 *
 */

#if (HX_FIX_TOUCH_INFO == 0x01)
void himax_mcu_information_check(void)
{
	uint8_t data[DATA_LEN_8] = { 0 };
	uint8_t j, check_sum = 0, retry = 0;
	uint8_t FW_MAX_PT = 0;
	bool FW_INT_IS_EDGE = 0;
	bool FW_IS_ID_EN = 0;
	bool FW_ID_PALM_EN = 0;
	uint16_t FW_Y_RES = 0;
	uint16_t FW_X_RES = 0;
	uint8_t FW_RX_NUM = 0;
	uint8_t FW_TX_NUM = 0;
	uint32_t fw_setting_addr = addr_fw_setting_start;
	uint32_t addr = 0;

	for (retry = 0; retry < 5U; retry++) {
		himax_mcu_register_read(fw_setting_addr, DATA_LEN_4, data);
		check_sum = data[2] + data[3];
		fw_setting_addr += DATA_LEN_4;
		for (addr = fw_setting_addr; addr < addr_fw_setting_end;
			addr += DATA_LEN_4) {

			himax_mcu_register_read(addr, DATA_LEN_4, data);
			if (addr == addr_fw_define_chip_rx_tx_num) {
				FW_RX_NUM = data[2];
				FW_TX_NUM = data[3];
			} else if (addr == addr_fw_define_maxpt) {
				FW_MAX_PT = data[0];
			} else if (addr == addr_fw_define_int_is_edge) {
				FW_INT_IS_EDGE = ((data[1] & 0x01U) == 0x01U);
			} else if (addr == addr_fw_HX_ID_EN) {
				FW_IS_ID_EN = ((data[1] & 0x02U) == 0x02U);
				FW_ID_PALM_EN = ((data[1] & 0x80U) == 0x80U);
				ic_data->STOP_FW_BY_HOST_EN = ((data[1] & 0x01U) == 0x01U);
			} else if (addr == addr_fw_define_xy_res) {
				FW_Y_RES = ((uint16_t)data[2] << 8) | (uint16_t)data[3];
				FW_X_RES = ((uint16_t)data[0] << 8) | (uint16_t)data[1];
			} else {
				/*do nothing*/
			}
			for (j = 0; j < DATA_LEN_4; j++) {
				check_sum += data[j];
			}
		}
		himax_mcu_register_read(addr, DATA_LEN_4, data);
		check_sum += data[0];
		check_sum = (uint8_t)(0x100U - check_sum);

		if (check_sum == data[1]) {
			I("%s:check_sum Pass\n", __func__);
			break;
		} else {
			W("check_sum Fail 0x%02X\n", check_sum);
		}
	}

	if (ic_data->HX_TX_NUM != FW_TX_NUM) {
		W("%s: TX_NUM: %d mismatch with FW: %d\n", __func__,
		ic_data->HX_TX_NUM, FW_TX_NUM);
	}

	if (ic_data->HX_RX_NUM != FW_RX_NUM) {
		W("%s: RX_NUM: %d mismatch with FW: %d\n", __func__,
		ic_data->HX_RX_NUM, FW_RX_NUM);
	}

	if (ic_data->HX_MAX_PT != FW_MAX_PT) {
		W("%s: MAX_PT: %d mismatch with FW: %d\n", __func__,
		ic_data->HX_MAX_PT, FW_MAX_PT);
	}

	if (ic_data->HX_INT_IS_EDGE != FW_INT_IS_EDGE) {
		W("%s: INT_type: %d mismatch with FW: %d\n", __func__,
		ic_data->HX_INT_IS_EDGE, FW_INT_IS_EDGE);
	}

	if (ic_data->HX_IS_ID_EN != FW_IS_ID_EN) {
		W("%s: ID_EN: %d mismatch with FW: %d\n", __func__,
		ic_data->HX_IS_ID_EN, FW_IS_ID_EN);
	}

	if (ic_data->HX_ID_PALM_EN != FW_ID_PALM_EN) {
		W("%s: ID_PALM_EN: %d mismatch with FW: %d\n", __func__,
		ic_data->HX_ID_PALM_EN, FW_ID_PALM_EN);
	}

	if (ic_data->HX_X_RES != FW_X_RES) {
		W("%s: X_RES: %d mismatch with FW: %d\n", __func__,
		ic_data->HX_X_RES, FW_X_RES);
	}

	if (ic_data->HX_Y_RES != FW_Y_RES) {
		W("%s: Y_RES: %d mismatch with FW: %d\n", __func__,
		ic_data->HX_Y_RES, FW_Y_RES);
	}

}
#endif
/*-------------------------------------------------------------------------
 *
 *	Description: Read related touch information from mcu or assign fixed values
 *				to ic_data value.
 *	Parameters: void
 *
 *	Returns: void
 *
 */
void himax_mcu_touch_information(void)
{
	uint8_t data[DATA_LEN_8] = { 0 };
#if (HX_FIX_TOUCH_INFO == 0x00)
	uint32_t fw_setting_addr = addr_fw_setting_start;
	uint32_t addr = 0;
	uint8_t j = 0;
	uint8_t	check_sum = 0;
	uint8_t	retry = 0;

	I("%s Enter\n", __func__);

	for (retry = 0; retry < 5U; retry++) {
		himax_mcu_register_read(fw_setting_addr, DATA_LEN_4, data);
		check_sum = data[2] + data[3];
		fw_setting_addr += DATA_LEN_4;
		for (addr = fw_setting_addr; addr < addr_fw_setting_end;
			addr += DATA_LEN_4) {

			himax_mcu_register_read(addr, DATA_LEN_4, data);
			if (addr == addr_fw_define_chip_rx_tx_num) {
				ic_data->HX_RX_NUM = data[2];
				ic_data->HX_TX_NUM = data[3];
			} else if (addr == addr_fw_define_maxpt) {
				ic_data->HX_MAX_PT = data[0];
			} else if (addr == addr_fw_define_int_is_edge) {
				ic_data->HX_INT_IS_EDGE = ((data[1] & 0x01U) == 0x01U);
			} else if (addr == addr_fw_HX_ID_EN) {
				ic_data->HX_IS_ID_EN = ((data[1] & 0x02U) == 0x02U);
				ic_data->HX_ID_PALM_EN = ((data[1] & 0x80U) == 0x80U);
				ic_data->STOP_FW_BY_HOST_EN = ((data[1] & 0x01U) == 0x01U);
			} else if (addr == addr_fw_define_xy_res) {
				ic_data->HX_Y_RES = ((uint16_t)data[2] << 8U);
				ic_data->HX_Y_RES += (uint16_t)data[3];
				ic_data->HX_X_RES = ((uint16_t)data[0] << 8U);
				ic_data->HX_X_RES += (uint16_t)data[1];
			} else {
				/*do nothing*/
			}
			for (j = 0; j < DATA_LEN_4; j++) {
				check_sum += data[j];
			}
		}
		himax_mcu_register_read(addr, DATA_LEN_4, data);
		check_sum += data[0];
		check_sum = (uint8_t)(0x100U - check_sum);

		if (check_sum == data[1]) {
			I("%s:check_sum Pass\n", __func__);
			break;
		} else {
			W("check_sum Fail 0x%02X\n", check_sum);
		}
	}

#elif (HX_FIX_TOUCH_INFO == 0x01)
	ic_data->HX_RX_NUM = (uint8_t)FIX_HX_RX_NUM;
	ic_data->HX_TX_NUM = (uint8_t)FIX_HX_TX_NUM;
	ic_data->HX_MAX_PT = (uint8_t)FIX_HX_MAX_PT;
	ic_data->HX_INT_IS_EDGE = (uint8_t)FIX_HX_INT_IS_EDGE;
	ic_data->HX_Y_RES = private_ts->pdata->screenHeight;
	ic_data->HX_X_RES = private_ts->pdata->screenWidth;
	ic_data->HX_IS_ID_EN = (uint8_t)FIX_HX_IS_ID_EN;
	ic_data->HX_ID_PALM_EN = (uint8_t)FIX_HX_ID_PALM_EN;

	himax_mcu_information_check();
	himax_mcu_register_read(addr_fw_HX_ID_EN, DATA_LEN_4, data);
	ic_data->STOP_FW_BY_HOST_EN = ((data[1] & 0x01U) == 0x01U);
#endif

	private_ts->nFinger_support = ic_data->HX_MAX_PT;
	private_ts->pdata->abs_x_min = 0U;
	private_ts->pdata->abs_x_max = ic_data->HX_X_RES;
	private_ts->pdata->abs_y_min = 0U;
	private_ts->pdata->abs_y_max = ic_data->HX_Y_RES;

	I("%s:HX_RX_NUM =%d,HX_TX_NUM =%d\n", __func__,
		ic_data->HX_RX_NUM,
		ic_data->HX_TX_NUM);
	I("%s:HX_Y_RES=%d,HX_X_RES =%d,HX_INT_IS_EDGE =%d,\n", __func__,
		ic_data->HX_Y_RES,
		ic_data->HX_X_RES,
		ic_data->HX_INT_IS_EDGE);
	I("%s:HX_IS_ID_EN=%d,HX_ID_PALM_EN =%d\n", __func__,
		ic_data->HX_IS_ID_EN,
		ic_data->HX_ID_PALM_EN);
	I("%s:STOP_FW_BY_HOST_EN=%d\n", __func__,
		ic_data->STOP_FW_BY_HOST_EN);

}

unsigned int himax_mcu_cal_data_len(unsigned int raw_cnt_rmd, uint8_t HX_MAX_PT, unsigned int raw_cnt_max)
{
	unsigned int RawDataLen;

	if (raw_cnt_rmd != 0x00U) {
		RawDataLen = MAX_I2C_TRANS_SZ -
			     ((HX_MAX_PT + raw_cnt_max + 3U) * 4U) - 2U;
	} else {
		RawDataLen = MAX_I2C_TRANS_SZ -
			     ((HX_MAX_PT + raw_cnt_max + 2U) * 4U) - 2U;
	}

	return RawDataLen;
}

bool himax_mcu_diag_check_sum(struct himax_report_data *hx_touch_data_tmp)
{
	uint16_t check_sum_cal = 0;
	uint8_t i = 0;

	/* Check 128th byte CRC */
	for (i = 0; i < (hx_touch_data_tmp->touch_all_size -
					    hx_touch_data_tmp->touch_info_size); i += 2U) {

		check_sum_cal += ((hx_touch_data_tmp->hx_rawdata_buf[i + 1U] * FLASH_RW_MAX_LEN) +
				  hx_touch_data_tmp->hx_rawdata_buf[i]);
	}

	if ((check_sum_cal % HX64K) != 0U) {
		I("%s fail=%2X\n", __func__, check_sum_cal);
		return 0;
	}

	return 1;
}

void himax_mcu_diag_parse_raw_data(struct himax_report_data *hx_touch_data_tmp,
				   uint16_t mul_num, uint16_t self_num, uint8_t diag_cmd,
				   int16_t *mutual_data, int16_t *self_data)
{
	diag_mcu_parse_raw_data(hx_touch_data_tmp, mul_num, self_num, diag_cmd,
				mutual_data, self_data);
}

void himax_disable_flash_protected_mode(void)
{
	struct himax_ts_data *ts = private_ts;
	uint8_t data[DATA_LEN_4] = { 0 };
	uint8_t loop_count = 0;

	/*Disable Write Protect*/
#if defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83192) || \
    defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83193) || \
    defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX8530)
	if (strnstr(ts->chip_name, HX_83192D_PWON, 30) != NULL) {
		/*Disable WP for HX83192D*/
		himax_parse_assign_cmd(data_WP_disable_HX83192D, data, sizeof(data));
		himax_mcu_register_write(addr_WP_pin_HX83192D, DATA_LEN_4, data);
	} else {

		if (strnstr(ts->chip_name, HX_83193A_PWON, 30) != NULL) {
			/* WP pin pull high*/
			himax_parse_assign_cmd(data_WP_disable_HX83193, data, sizeof(data));
			himax_mcu_register_write(addr_WP_pin_HX83193, DATA_LEN_4, data);
		}

#if defined(WP_GPIO4)
		/*disable WP by gpio4*/

		himax_mcu_register_read(addr_WP_gpio4_cmd_04, DATA_LEN_4, data);
		data[0] = data[0] | 0x10U;
		himax_mcu_register_write(addr_WP_gpio4_cmd_04, DATA_LEN_4, data);

		himax_parse_assign_cmd(data_WP_gpio4_cmd_01, data, sizeof(data));
		himax_mcu_register_write(addr_WP_gpio4_cmd_B4, DATA_LEN_4, data);

		himax_parse_assign_cmd(data_WP_gpio4_cmd_01, data, sizeof(data));
		himax_mcu_register_write(addr_WP_gpio4_cmd_1C, DATA_LEN_4, data);

#elif defined(WP_GPIO0)
		/*disable WP by gpio0*/

		himax_mcu_register_read(addr_WP_gpio0_cmd_04, DATA_LEN_4, data);
		data[0] = data[0] | 0x01U;
		himax_mcu_register_write(addr_WP_gpio0_cmd_04, DATA_LEN_4, data);

		himax_parse_assign_cmd(data_WP_gpio0_cmd_01, data, sizeof(data));
		himax_mcu_register_write(addr_WP_gpio0_cmd_B4, DATA_LEN_4, data);

		himax_parse_assign_cmd(data_WP_gpio0_cmd_01, data, sizeof(data));
		himax_mcu_register_write(addr_WP_gpio0_cmd_0C, DATA_LEN_4, data);
#endif
	}

#elif defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83194) || \
      defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83195)
	himax_parse_assign_cmd(data_WP_disable_HX83195, data, sizeof(data));
	himax_mcu_register_write(addr_WP_pin_HX83195, DATA_LEN_4, data);

#elif defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83180) || \
      defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83181)
	himax_mcu_register_read(addr_WP_pin_HX83180, DATA_LEN_4, data);
	data[1] = data[1] | 0x10U;
	data[2] = data[2] | 0x10U;
	himax_mcu_register_write(addr_WP_pin_HX83180, DATA_LEN_4, data);

#endif

	/*Disable Block Protect*/
	himax_parse_assign_cmd(data_BP_lock_cmd_1, data, sizeof(data));
	himax_mcu_register_write(addr_BP_lock_cmd_10, DATA_LEN_4, data);

	himax_parse_assign_cmd(data_BP_lock_cmd_2, data, sizeof(data));
	himax_mcu_register_write(addr_BP_lock_cmd_20, DATA_LEN_4, data);

	himax_parse_assign_cmd(data_BP_lock_cmd_3, data, sizeof(data));
	himax_mcu_register_write(addr_BP_lock_cmd_24, DATA_LEN_4, data);

	himax_parse_assign_cmd(data_BP_lock_cmd_4, data, sizeof(data));
	himax_mcu_register_write(addr_BP_lock_cmd_20, DATA_LEN_4, data);

	himax_parse_assign_cmd(data_BP_lock_cmd_5, data, sizeof(data));
	himax_mcu_register_write(addr_BP_lock_cmd_2C, DATA_LEN_4, data);

	himax_parse_assign_cmd(data_BP_lock_cmd_6, data, sizeof(data));
	himax_mcu_register_write(addr_BP_lock_cmd_24, DATA_LEN_4, data);

	/*Check Block Protect */
	himax_parse_assign_cmd(data_BP_check_cmd_1, data, sizeof(data));
	himax_mcu_register_write(addr_BP_lock_cmd_20, DATA_LEN_4, data);

	do {
		himax_parse_assign_cmd(data_BP_check_cmd_2, data, sizeof(data));
		himax_mcu_register_write(addr_BP_lock_cmd_24, DATA_LEN_4, data);

		himax_mcu_register_read(addr_BP_lock_cmd_2C, DATA_LEN_4, data);
		loop_count++;
		if (loop_count == 30U) {
			W("%s: time out of loop_count: %d.\n",
			  __func__, loop_count);
			  break;
		}
		usleep_range(1000, 1100);
	} while ((data[0] & 0x03U) != 0x00U);

	if (data[0] != 0x00U) {
		W("%s: Fail. value :0x%02X, loop_count: %d\n",
		  __func__, data[0], loop_count);
	} else {
		I("%s: Finish. loop_count %d.\n",
	  		__func__, loop_count);
	}
}

#if (HX_WPBP_ENABLE == 0x01)
void himax_enable_flash_protected_mode(void)
{
	struct himax_ts_data *ts = private_ts;
	uint8_t data[DATA_LEN_4] = { 0 };
	uint8_t lock_code = FLASH_LOCK_CODE;
	uint32_t lock_code_32bit = (uint32_t)lock_code;
	uint8_t loop_count = 0;

	/*Enable Block Protect*/
	himax_parse_assign_cmd(data_BP_lock_cmd_1, data, sizeof(data));
	himax_mcu_register_write(addr_BP_lock_cmd_10, DATA_LEN_4, data);

	himax_parse_assign_cmd(data_BP_lock_cmd_2, data, sizeof(data));
	himax_mcu_register_write(addr_BP_lock_cmd_20, DATA_LEN_4, data);

	himax_parse_assign_cmd(data_BP_lock_cmd_3, data, sizeof(data));
	himax_mcu_register_write(addr_BP_lock_cmd_24, DATA_LEN_4, data);

	himax_parse_assign_cmd(data_BP_lock_cmd_4, data, sizeof(data));
	himax_mcu_register_write(addr_BP_lock_cmd_20, DATA_LEN_4, data);

	himax_parse_assign_cmd(lock_code_32bit, data, sizeof(data));
	himax_mcu_register_write(addr_BP_lock_cmd_2C, DATA_LEN_4, data);

	himax_parse_assign_cmd(data_BP_lock_cmd_6, data, sizeof(data));
	himax_mcu_register_write(addr_BP_lock_cmd_24, DATA_LEN_4, data);

	/*Check Block Protect*/
	himax_parse_assign_cmd(data_BP_check_cmd_1, data, sizeof(data));
	himax_mcu_register_write(addr_BP_lock_cmd_20, DATA_LEN_4, data);

	do {
		himax_parse_assign_cmd(data_BP_check_cmd_2, data, sizeof(data));
		himax_mcu_register_write(addr_BP_lock_cmd_24, DATA_LEN_4, data);

		himax_mcu_register_read(addr_BP_lock_cmd_2C, DATA_LEN_4, data);
		loop_count++;
		if (loop_count == 30U) {
			W("%s: time out of loop_count: %d.\n",
				__func__, loop_count);
			  break;
		}
		usleep_range(1000, 1100);
	} while ((data[0] & 0x03U) != 0x00U);



	if ((data[0] & lock_code) != lock_code) {
		W("%s: Fail, value = 0x%02X, lock code 0x%02X\n",
			__func__, data[0], lock_code);
	} else {
		I("%s: Finish. value = 0x%02X\n",
			__func__, data[0]);

	/*Enable Write Protect*/
#if defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83192) || \
    defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83193) || \
    defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX8530)
		if (strnstr(ts->chip_name, HX_83192D_PWON, 30) != NULL) {
			/*Enable WP for HX83192D*/
			himax_parse_assign_cmd(data_WP_enable_HX83192D, data, sizeof(data));
			himax_mcu_register_write(addr_WP_pin_HX83192D, DATA_LEN_4, data);
		} else {
#if defined(WP_GPIO4)
			/*Enable WP by gpio4 */

			himax_mcu_register_read(addr_WP_gpio4_cmd_04, DATA_LEN_4, data);
			data[0] = data[0] | 0x10;
			himax_mcu_register_write(addr_WP_gpio4_cmd_04, DATA_LEN_4, data);

			himax_parse_assign_cmd(data_WP_gpio4_cmd_01, data, sizeof(data));
			himax_mcu_register_write(addr_WP_gpio4_cmd_B4, DATA_LEN_4, data);

			himax_parse_assign_cmd(data_WP_gpio4_cmd_00, data, sizeof(data));
			himax_mcu_register_write(addr_WP_gpio4_cmd_1C, DATA_LEN_4, data);
#elif defined(WP_GPIO0)
			/*enable WP by gpio0 */
			himax_mcu_register_read(addr_WP_gpio0_cmd_04, DATA_LEN_4, data);
			data[0] = data[0] | 0x01;
			himax_mcu_register_write(addr_WP_gpio0_cmd_04, DATA_LEN_4, data);

			himax_parse_assign_cmd(data_WP_gpio0_cmd_01, data, sizeof(data));
			himax_mcu_register_write(addr_WP_gpio0_cmd_B4, DATA_LEN_4, data);

			himax_parse_assign_cmd(data_WP_gpio0_cmd_00, data, sizeof(data));
			himax_mcu_register_write(addr_WP_gpio0_cmd_0C, DATA_LEN_4, data);
#endif
		}

#elif defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83194) || \
      defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83195)
		himax_parse_assign_cmd(data_WP_enable_HX83195, data, sizeof(data));
		himax_mcu_register_write(addr_WP_pin_HX83195, DATA_LEN_4, data);

#elif defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83180) || \
      defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83181)
		himax_mcu_register_read(addr_WP_pin_HX83180, DATA_LEN_4, data);
		data[1] = data[1] & 0xEFU;
		data[2] = data[2] | 0x10U;
		himax_mcu_register_write(addr_WP_pin_HX83180, DATA_LEN_4, data);
#endif
	}
}
#endif

static int himax_mcu_flash_id_check(uint8_t *flash_lock_type)
{
	uint8_t data[DATA_LEN_4] = { 0 };
	uint8_t i;
	uint8_t Flash_list_tmp[][3] = Flash_list;
	size_t len = sizeof(Flash_list_tmp) / (sizeof(uint8_t) * 3U);
	uint8_t flash_idx;
	int result = NO_ERR;
	int retry_count = 0;

	flash_idx = 0xFFU;
	*flash_lock_type = 0xFFU;
	do {
		himax_parse_assign_cmd(data_BP_check_cmd_3, data, sizeof(data));
		himax_mcu_register_write(addr_BP_lock_cmd_20, DATA_LEN_4, data);

		himax_parse_assign_cmd(data_BP_check_cmd_4, data, sizeof(data));
		himax_mcu_register_write(addr_BP_lock_cmd_24, DATA_LEN_4, data);

		himax_mcu_register_read(addr_BP_lock_cmd_2C, DATA_LEN_4, data);

		I("%s: FlashList len : %zd, Flash type ID data = %X,%X,%X\n", __func__,
		 len, data[0], data[1], data[2]);


		for (i = 0; i < len; i++) {
			if ((Flash_list_tmp[i][0] == data[0]) &&
				(Flash_list_tmp[i][1] == data[1]) &&
				(Flash_list_tmp[i][2] == data[2])) {
				flash_idx = i;
				break;
			}
		}

		switch (flash_idx) {
		case 0U:
		case 8U:
			*flash_lock_type = 1U;
			break;

		case 1U:
		case 2U:
		case 11U:
		case 18U:
		case 21U:
		case 22U:
		case 23U:
			*flash_lock_type = 2U;
			break;

		case 9U:
		case 10U:
		case 12U:
		case 13U:
		case 17U:
		case 19U:
		case 20U:
			*flash_lock_type = 3U;
			break;

		case 3U:
		case 4U:
		case 5U:
		case 6U:
		case 7U:
		case 14U:
		case 15U:
		case 16U:
			*flash_lock_type = 4U;
			break;

		default:
			*flash_lock_type = 0xFFU;
			result = HX_FAIL;
			break;
		}

		if (*flash_lock_type != 0xFFU) {

			break;
		}

		usleep_range(10000, 11000);
		retry_count++;
	} while (retry_count < 3);

	return result;
}

/*---------------------------------------------------------------------------------
 *
 *	+---------------------+-----------+----+------+-----------+-------------------+
 *	|      ID             |    BP     | WP | type | Lock code | delay time(ms)    |
 *	+---------------------+-----------+----+------+-----------+-------------------+
 *	| 0/8                 | 2_3       |  7 |    1 | 0x8C      | 10/10             |
 *	| 1/2/11/18/21/22/23  | 2_3_4     |  7 |    2 | 0x9C      | 1/10/0/1/2/2/2    |
 *	| 9/10/12/13/17/19/20 | 2_3_4_5   |  7 |    3 | 0xBC      | 10/10/10/0/5/2/2  |
 *	| 3/4/5/6/7/14/15/16  | 2_3_4_5_6 |  7 |    4 | 0xFC      | 5/5/5/40/30/8/8/8 |
 *	+---------------------+-----------+----+------+-----------+-------------------+
 *
 */
int himax_mcu_WP_BP_status(void)
{
	uint8_t data[DATA_LEN_4] = { 0 };
	int ret = NO_ERR;
	uint8_t lock_code = 0;
	uint8_t flash_lock_type = 0;

	ret = himax_mcu_flash_id_check(&flash_lock_type);

	if (ret == HX_FAIL) {
		E("%s: Cannot recognize flash id type\n", __func__);
	} else {

		switch (flash_lock_type) {
		case 1:
			lock_code = 0x8CU;
			break;
		case 2:
			lock_code = 0x9CU;
			break;
		case 3:
			lock_code = 0xBCU;
			break;
		case 4:
			lock_code = 0xFCU;
			break;
		default:
			I("%s: Unknown lock type with value : 0x%02X\n", __func__,
			flash_lock_type);
			break;
		}

		/*Check Addr 0x8000_002C value, if 0x9C BP is lock*/
		himax_parse_assign_cmd(data_BP_lock_cmd_1, data, sizeof(data));
		himax_mcu_register_write(addr_BP_lock_cmd_10, DATA_LEN_4, data);

		himax_parse_assign_cmd(data_BP_check_cmd_1, data, sizeof(data));
		himax_mcu_register_write(addr_BP_lock_cmd_20, DATA_LEN_4, data);

		himax_parse_assign_cmd(data_BP_check_cmd_2, data, sizeof(data));
		himax_mcu_register_write(addr_BP_lock_cmd_24, DATA_LEN_4, data);

		himax_mcu_register_read(addr_BP_lock_cmd_2C, DATA_LEN_4, data);

		if ((data[0] & lock_code) == lock_code) {
			I("%s: Locked, lock_code = 0x%02X\n",
			__func__, lock_code);
			ret = HX_LOCK;
		} else {
			I("%s: Unlocked, lock_code = 0x%02X\n",
			__func__, lock_code);
			ret = HX_UNLOCK;
		}
	}

	return ret;

}

void himax_mcu_write_dd_reg_password(uint8_t ic_device)
{
	uint8_t data[DATA_LEN_4] = { 0 };
	uint8_t cMax = 7U;
	uint8_t cnt = 0U;
#if defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83195) || defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83194)
    uint8_t check_STOP_FW = 0x6BU;
    uint8_t write_STOP_FW = 0x5BU;
#else
    uint8_t check_STOP_FW = 0x6AU;
    uint8_t write_STOP_FW = 0x5AU;
#endif
#if defined(PANEL_ID_CHECK)
#if defined(FW_baseline_status_ready)
	uint8_t retry = 0;
#endif
#endif

#if defined(PANEL_ID_CHECK)
#if defined(FW_baseline_status_ready)
	FW_ready = false;

	do {
		himax_mcu_register_read(addr_FW_baseline_ready, DATA_LEN_4, data);
		if ((data[0] == 0x32U) || (data[0] == 0x35U) || (data[0] == 0x38U)) {
			FW_ready = true;
			break;
		} else {
			retry++;
			usleep_range(20000, 21000);
		}
	} while (retry < 15U);

	(void)memset(data, 0x00, sizeof(data));

	if (FW_ready) {
#endif
#endif
		if (ic_data->STOP_FW_BY_HOST_EN) {
			do {
                data[0] = write_STOP_FW;
				himax_mcu_register_write(addr_ctrl_fw, DATA_LEN_4,
							data);

				usleep_range(20000, 21000);
				himax_mcu_register_read(addr_ctrl_fw, DATA_LEN_4, data);
				I("%s: Check 9000005C data[0]=0x%02X\n", __func__, data[0]);
				cnt	+=	1U;
				if (cnt >= cMax) {
					break;
				}
			} while (data[0] != check_STOP_FW);
			if (data[0] == check_STOP_FW) {
				I("%s: STOP_FW_BY_HOST finished!\n", __func__);
			} else {
				W("%s: STOP_FW_BY_HOST Fail!\n", __func__);
			}

		} else {
			g_core_fp.fp_sense_off();
		}
#if defined(PANEL_ID_CHECK)
#if defined(FW_baseline_status_ready)
	} else {
		g_core_fp.fp_sense_off();
	}
#endif
#endif
	data[0] = 0xDDU;
	data[1] = 0x00U;
	data[2] = 0x00U;
	data[3] = 0x00U;
	if (ic_device == (uint8_t)IC_MASTER) {
		himax_mcu_register_write(addr_osc_en, DATA_LEN_4, data);
	} else {
		himax_mcu_register_write_slave(ic_device, addr_osc_en, DATA_LEN_4, data);
	}

#if defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83180) || defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83181)

	data[0] = 0xA5U;
	data[1] = 0x00U;
	data[2] = 0x00U;
	data[3] = 0x00U;
 	if (ic_device == (uint8_t)IC_MASTER) {
		himax_mcu_register_write(addr_osc_pw, DATA_LEN_4, data);
	} else {
		himax_mcu_register_write_slave(ic_device, addr_osc_pw, DATA_LEN_4, data);
	}
#endif
	data[0] = 0x00U;
	data[1] = 0x55U;
	data[2] = 0x66U;
	data[3] = 0xCCU;
	himax_mcu_dd_reg_write(0xEB, 0, 4, data, 0, ic_device);
}

void himax_mcu_clear_dd_reg_password(uint8_t ic_device)
{
	uint8_t data[DATA_LEN_4] = { 0 };
#if defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83195) || defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83194)
    uint8_t check_STOP_FW = 0x6BU;
#else
    uint8_t check_STOP_FW = 0x6AU;
#endif
	uint8_t cMax = 7U;
	uint8_t cnt = 0U;

	data[0] = 0x00U;
	data[1] = 0x00U;
	data[2] = 0x00U;
	data[3] = 0x00U;
	himax_mcu_dd_reg_write(0xEB, 0, 4, data, 0, ic_device);

#if defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83180) || defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83181)

	data[0] = 0x00U;
	data[1] = 0x00U;
	data[2] = 0x00U;
	data[3] = 0x00U;
 	if (ic_device == (uint8_t)IC_MASTER) {
		himax_mcu_register_write(addr_osc_pw, DATA_LEN_4, data);
	} else {
		himax_mcu_register_write_slave(ic_device, addr_osc_pw, DATA_LEN_4, data);
	}
#endif

	data[0] = 0x00U;
	data[1] = 0x00U;
	data[2] = 0x00U;
	data[3] = 0x00U;
	if (ic_device == (uint8_t)IC_MASTER) {
		himax_mcu_register_write(addr_osc_en, DATA_LEN_4, data);
	} else {
		himax_mcu_register_write_slave(ic_device, addr_osc_en, DATA_LEN_4, data);
	}

#if defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83181)
	data[0] = 0x7EU;
	data[1] = 0x00U;
	data[2] = 0x00U;
	data[3] = 0x00U;
 	if (ic_device == (uint8_t)IC_MASTER) {
		himax_mcu_register_write(addr_83181_dd_reg_flow_broadcast, DATA_LEN_4, data);
	} else {
		himax_mcu_register_write_slave(ic_device, addr_83181_dd_reg_flow_broadcast, DATA_LEN_4, data);
	}

	data[0] = 0x90U;
	data[1] = 0x00U;
	data[2] = 0x00U;
	data[3] = 0x00U;
 	if (ic_device == (uint8_t)IC_MASTER) {
		himax_mcu_register_write(addr_83181_dd_reg_flow_high_addr, DATA_LEN_4, data);
	} else {
		himax_mcu_register_write_slave(ic_device, addr_83181_dd_reg_flow_high_addr, DATA_LEN_4, data);
	}

	data[0] = 0x00U;
	data[1] = 0x00U;
	data[2] = 0x00U;
	data[3] = 0x00U;
 	if (ic_device == (uint8_t)IC_MASTER) {
		himax_mcu_register_write(addr_83181_dd_reg_flow_low_addr, DATA_LEN_4, data);
	} else {
		himax_mcu_register_write_slave(ic_device, addr_83181_dd_reg_flow_low_addr, DATA_LEN_4, data);
	}
#endif

#if defined(PANEL_ID_CHECK)
#if defined(FW_baseline_status_ready)
	if (FW_ready) {
#endif
#endif
		if (ic_data->STOP_FW_BY_HOST_EN) {
			do {
				data[0] = 0x00U;
				himax_mcu_register_write(addr_ctrl_fw, DATA_LEN_4,
							data);

				usleep_range(20000, 21000);
				himax_mcu_register_read(addr_ctrl_fw, DATA_LEN_4, data);
				I("%s: Check 9000005C data[0]=0x%02X\n", __func__, data[0]);
				cnt += 1U;
				if (cnt >= cMax) {
					break;
				}
			} while (data[0] == check_STOP_FW);
			if (data[0] != check_STOP_FW) {
				I("%s: START_FW_BY_HOST finished!\n", __func__);
			} else {
				E("%s: START_FW_BY_HOST Fail!\n", __func__);
			}

		} else {
			g_core_fp.fp_sense_on();
		}
#if defined(PANEL_ID_CHECK)
#if defined(FW_baseline_status_ready)
	} else {
		g_core_fp.fp_sense_on();
	}

	FW_ready = false;
#endif
#endif
}

void himax_mcu_write_dd_reg_password_sram_test(uint8_t ic_device)
{
	uint8_t data[DATA_LEN_4] = { 0 };

	data[0] = 0xDD;
	data[1] = 0x00;
	data[2] = 0x00;
	data[3] = 0x00;
	if (ic_device == IC_MASTER)
		himax_mcu_register_write(addr_osc_en, DATA_LEN_4, data);
	else
		himax_mcu_register_write_slave(ic_device, addr_osc_en, DATA_LEN_4, data);

	data[0] = 0x00;
	data[1] = 0x55;
	data[2] = 0x66;
	data[3] = 0xCC;
	himax_mcu_dd_reg_write(0xEB, 0, 4, data, 0, ic_device);
}

void himax_mcu_clear_dd_reg_password_sram_test(uint8_t ic_device)
{
	uint8_t data[DATA_LEN_4] = { 0 };

	data[0] = 0x00;
	data[1] = 0x00;
	data[2] = 0x00;
	data[3] = 0x00;
	himax_mcu_dd_reg_write(0xEB, 0, 4, data, 0, ic_device);

	data[0] = 0x00;
	data[1] = 0x00;
	data[2] = 0x00;
	data[3] = 0x00;
	if (ic_device == IC_MASTER)
		himax_mcu_register_write(addr_osc_en, DATA_LEN_4, data);
	else
		himax_mcu_register_write_slave(ic_device, addr_osc_en, DATA_LEN_4, data);
}
/* init end*/
/* CORE_INIT */
