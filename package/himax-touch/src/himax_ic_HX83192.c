// SPDX-License-Identifier: GPL-2.0
/*  Himax Android Driver Sample Code for HX83192 chipset
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

#include "himax_platform.h"
#include "himax_common.h"
#include "himax_ic_core.h"

bool hx83192_init(void);

static void hx83192_double_safe_mode(void)
{
	uint8_t tmp_data[DATA_LEN_4];

	I("%s Enter\n", __func__);

	tmp_data[0] = para_sense_off_0;
	tmp_data[1] = para_sense_off_1;

	(void)himax_bus_write(addr_sense_on_off_0, tmp_data, 2,
				HIMAX_I2C_RETRY_TIMES);

	tmp_data[0] = 0x00U;
	(void)himax_bus_write(addr_sense_on_off_0, tmp_data, 1,
				  HIMAX_I2C_RETRY_TIMES);

	usleep_range(100, 110);

#if defined(HIMAX_I2C_PLATFORM)
	himax_mcu_interface_on();
#endif

	tmp_data[0] = para_sense_off_0;
	tmp_data[1] = para_sense_off_1;

	(void)himax_bus_write(addr_sense_on_off_0, tmp_data, 2,
				HIMAX_I2C_RETRY_TIMES);

}

static void hx83192_sense_on(void)
{
	uint8_t tmp_data[DATA_LEN_4];
	uint8_t retry = 0;
	int ret = 0;

	I("%s Enter\n", __func__);

	do {
		himax_parse_assign_cmd(data_clear, tmp_data,
					   sizeof(tmp_data));
		himax_mcu_register_write(addr_ctrl_fw, DATA_LEN_4,
					 tmp_data);

		usleep_range(20000, 21000);

		himax_mcu_register_read(addr_ctrl_fw, DATA_LEN_4,
					tmp_data);

		I("%s:Read status from IC = 0x%02X,0x%02X\n", __func__,
		  tmp_data[0], tmp_data[1]);
		if (tmp_data[0] == 0x00U) {
			break;
		}
		retry += 1U;
	} while (retry < 5U);

	if (tmp_data[0] != 0x00U) {
		E("%s: Fail:\n", __func__);
		himax_mcu_tp_reset();
	} else {
		/* Initial Setting*/
		tmp_data[0] = 0x00U;
		tmp_data[1] = 0x00U;

		ret = himax_bus_write(addr_sense_on_off_0, tmp_data, 2,
					  HIMAX_I2C_RETRY_TIMES);
		if (ret < 0) {
			E("%s: i2c access fail!\n", __func__);
		}

	}
	usleep_range(20000, 21000);

#if defined(HIMAX_I2C_PLATFORM)
	himax_mcu_interface_on();
#endif

}

static void hx83192_sense_off(void)
{
	uint8_t cnt = 0;
	uint8_t tmp_data[DATA_LEN_4] = { 0 };
	uint8_t cMax = 14;
	uint8_t check = (uint8_t) 0x87;
	int ret = 0;

	usleep_range(40000, 41000);

	himax_mcu_register_read(addr_cs_central_state, DATA_LEN_4, tmp_data);

	if (tmp_data[0] == 0x05U) {
		do {
			tmp_data[3] = 0x00;
			tmp_data[2] = 0x00;
			tmp_data[1] = 0x00;
			tmp_data[0] = 0xA5;
			himax_mcu_register_write(addr_ctrl_fw, DATA_LEN_4,
						 tmp_data);

			usleep_range(20000, 21000);
			himax_mcu_register_read(addr_ctrl_fw, DATA_LEN_4,
						tmp_data);
			cnt += 1U;
			if (cnt >= cMax) {
				break;
			}
		} while (tmp_data[0] != check);
		I("%s: 9000005C data[0]=0x%02X, Retry times = %d\n", __func__,
		  tmp_data[0], cnt);
	}
	if (private_ts->slave_ic_num >= 1U) {
		cnt = 0;
		do {
			tmp_data[0] = para_sense_off_0;
			tmp_data[1] = para_sense_off_1;

			ret = himax_bus_write_slave(IC_SLAVE_1, addr_sense_on_off_0, tmp_data, 2,
						  HIMAX_I2C_RETRY_TIMES);
			if (ret < 0) {
				W("[IC_SLAVE_1] %s: i2c access fail!\n", __func__);
			}
			himax_mcu_register_read_slave(IC_SLAVE_1, addr_cs_central_state, DATA_LEN_4, tmp_data);
			I("[IC_SLAVE_1] %s: Check enter_save_mode data[0]=0x%02X\n", __func__,
			  tmp_data[0]);

			if (tmp_data[0] == 0x0CU) {
				break;
			}
			cnt += 1U;
		} while (cnt < 15U);
	}
	if (private_ts->slave_ic_num == 2U) {
		cnt = 0;
		do {
			tmp_data[0] = para_sense_off_0;
			tmp_data[1] = para_sense_off_1;

			ret = himax_bus_write_slave(IC_SLAVE_2, addr_sense_on_off_0, tmp_data, 2,
						  HIMAX_I2C_RETRY_TIMES);
			if (ret < 0) {
				W("[IC_SLAVE_2] %s: i2c access fail!\n", __func__);
			}
			himax_mcu_register_read_slave(IC_SLAVE_2, addr_cs_central_state, DATA_LEN_4, tmp_data);
			I("[IC_SLAVE_2] %s: Check enter_save_mode data[0]=0x%02X\n", __func__,
			  tmp_data[0]);

			if (tmp_data[0] == 0x0CU) {
				break;
			}
			cnt += 1U;
		} while (cnt < 15U);
	}
	cnt = 0;
	do {
		tmp_data[0] = para_sense_off_0;
		tmp_data[1] = para_sense_off_1;

		(void)himax_bus_write(addr_sense_on_off_0, tmp_data, 2,
					HIMAX_I2C_RETRY_TIMES);

		himax_mcu_register_read(addr_cs_central_state, DATA_LEN_4,
					tmp_data);
		I("%s: Check enter_save_mode data[0]=0x%02X\n", __func__,
		  tmp_data[0]);

		if (tmp_data[0] == 0x0CU) {
			break;
		} else if (cnt == 3U) {
			usleep_range(10000, 11000);
			hx83192_double_safe_mode();
		} else if (cnt == 6U) {
			usleep_range(10000, 11000);
			himax_mcu_tp_reset();
		} else {
			/* do nothing*/
		}
		cnt += 1U;
	} while (cnt < 15U);

}

static void hx83192_mcu_flash_dump_func(uint32_t start_addr,
					unsigned int Flash_Size, uint8_t *flash_buffer)
{
	uint8_t tmp_data[DATA_LEN_4] = { 0 };
	uint32_t page_prog_start = 0;
	unsigned int i = 0;

	I("%s,start addr = 0x%02X, dump size = 0x%02X\n", __func__, start_addr, Flash_Size);
	hx83192_sense_off();

	/* Disable retry wrapper to avoid I2C CLK low issue */
	tmp_data[0] = 0xA5;
	himax_mcu_register_write(addr_retry_wrapper_clr_pw, 4, tmp_data);
	I("%s: Disable retry wrapper for flash read.\n", __func__);

	page_prog_start = start_addr;

	for (i = 0; i < Flash_Size; i += 256U) {
		himax_mcu_register_read(page_prog_start, 256,
					&flash_buffer[i]);
		page_prog_start += 256U;
	}

	hx83192_sense_on();
}

static bool hx83192_mcu_flash_programming(const u8 *FW_content, unsigned int start_addr,
					  unsigned int length)
{
	unsigned int page_prog_start = 0;
	uint8_t tmp_data[DATA_LEN_4] = { 0 };
	uint8_t buring_data[FLASH_RW_MAX_LEN] = { 0 }; 
	uint8_t Original_speed[DATA_LEN_4] = { 0 };
	bool ret_data = true;
	uint16_t index = 0;

	I("%s: programming flash, please wait...\n", __func__);
	/* ===Get Flash Speed===*/
	himax_mcu_register_read(addr_spi200_flash_speed, DATA_LEN_4,
				Original_speed);

	/* ===Set Flash Speed===*/
	himax_parse_assign_cmd(data_set_flash_speed, tmp_data,
			       sizeof(tmp_data));
	himax_mcu_register_write(addr_spi200_flash_speed, DATA_LEN_4, tmp_data);

	himax_mcu_burst_mode_disable();

	/* ===SPI TX-FIFO Reset===*/
	himax_parse_assign_cmd(data_spi200_txfifo_rst, tmp_data,
			       sizeof(tmp_data));
	himax_mcu_register_write(addr_spi200_fifo_rst, DATA_LEN_4, tmp_data);

	/* ===SPI Format===*/
	himax_parse_assign_cmd(data_spi200_trans_fmt, tmp_data,
			       sizeof(tmp_data));
	himax_mcu_register_write(addr_spi200_trans_fmt, DATA_LEN_4, tmp_data);

	for (page_prog_start = start_addr;
	     page_prog_start < (start_addr + length);
	     page_prog_start += FLASH_RW_MAX_LEN) {
		/* ===Flash Write Enable ===*/
		himax_parse_assign_cmd(data_spi200_trans_ctrl_2, tmp_data,
				       sizeof(tmp_data));
		himax_mcu_register_write(addr_spi200_trans_ctrl, DATA_LEN_4,
					 tmp_data);

		himax_parse_assign_cmd(data_spi200_cmd_2, tmp_data,
				       sizeof(tmp_data));
		himax_mcu_register_write(addr_spi200_cmd, DATA_LEN_4, tmp_data);

		/* ===WEL Write Control ===*/
		himax_parse_assign_cmd(data_spi200_trans_ctrl_6, tmp_data,
				       sizeof(tmp_data));
		himax_mcu_register_write(addr_spi200_trans_ctrl, DATA_LEN_4,
					 tmp_data);

		himax_parse_assign_cmd(data_spi200_cmd_1, tmp_data,
				       sizeof(tmp_data));
		himax_mcu_register_write(addr_spi200_cmd, DATA_LEN_4, tmp_data);

		himax_mcu_register_read(addr_spi200_data, DATA_LEN_4, tmp_data);
		/* === Check WEL Fail ===*/
		if (((tmp_data[0] & 0x02U) >> 1U) == 0U) {
			I("%s:SPI 0x8000002c = %d, Check WEL Fail\n", __func__, tmp_data[0]);
			ret_data = false;
		}

		/*Set 256 Bytes Page Write*/
		himax_parse_assign_cmd(data_spi200_trans_ctrl_4, tmp_data,
				       sizeof(tmp_data));
		himax_mcu_register_write(addr_spi200_trans_ctrl, DATA_LEN_4,
					 tmp_data);

		(void)memset(tmp_data, 0x00, sizeof(tmp_data));
		tmp_data[3] = (uint8_t)(page_prog_start >> 24U);
		tmp_data[2] = (uint8_t)(page_prog_start >> 16U);
		tmp_data[1] = (uint8_t)(page_prog_start >> 8U);
		tmp_data[0] = (uint8_t)page_prog_start;
		himax_mcu_register_write(addr_spi200_addr, DATA_LEN_4,
					 tmp_data);

		(void)memset(buring_data, 0x00, sizeof(buring_data));
		himax_parse_assign_cmd(addr_spi200_data, buring_data,
				       ADDR_LEN_4);
		for (index = 0; index < 16U; index++) {
			buring_data[ADDR_LEN_4 + index] =
				FW_content[page_prog_start - start_addr + index];
		}
		if (himax_bus_write(addr_AHB_address_byte_0, buring_data,
				    (ADDR_LEN_4 + 16U),
				    HIMAX_I2C_RETRY_TIMES) < 0) {
			E("%s: i2c access fail!\n", __func__);
			ret_data = false;
		}
		/*Write Command: PP*/
		himax_parse_assign_cmd(data_spi200_cmd_6, tmp_data,
				       sizeof(tmp_data));
		himax_mcu_register_write(addr_spi200_cmd, DATA_LEN_4, tmp_data);

		for (index = 0; index < 240U; index++) {
			buring_data[ADDR_LEN_4 + index] =
				FW_content[page_prog_start - start_addr + 16U + index];
		}
		if (himax_bus_write(addr_AHB_address_byte_0, buring_data,
				    (ADDR_LEN_4 + 240U),
				    HIMAX_I2C_RETRY_TIMES) < 0) {
			E("%s: i2c access fail!\n", __func__);
			ret_data = false;
		}

		if (!himax_mcu_wait_wip(1)) {
			E("%s:Flash_Programming Fail\n", __func__);
			ret_data = false;
		}
		if (ret_data == false) {
			break;
		}
	}
	/* ===Set Flash Speed===*/
	himax_mcu_register_write(addr_spi200_flash_speed, DATA_LEN_4,
				 Original_speed);
	return ret_data;
}

static void hx83192_func_re_init(void)
{
	g_core_fp.fp_sense_on = hx83192_sense_on;
	g_core_fp.fp_sense_off = hx83192_sense_off;
	g_core_fp.fp_flash_dump_func = hx83192_mcu_flash_dump_func;
	g_core_fp.fp_flash_programming = hx83192_mcu_flash_programming;
	g_core_fp.fp_check_CRC = himax_mcu_check_CRC;
	g_core_fp.fp_calculateChecksum = himax_mcu_calculateChecksum;
	g_core_fp.fp_touch_information = himax_mcu_touch_information;
	g_core_fp.fp_get_DSRAM_data = himax_mcu_get_DSRAM_data;
	g_core_fp.fp_burst_mode_enable = himax_mcu_burst_mode_enable;
	g_core_fp.fp_fts_ctpm_fw_upgrade = himax_mcu_fts_ctpm_fw_upgrade;

}

static bool hx83192_chip_detect(void)
{
	uint8_t tmp_data[DATA_LEN_4] = { 0 };
	bool ret_data = false;
	int i = 0;

	hx83192_func_re_init();

	himax_mcu_check_cascade_ic_num();

	hx83192_sense_off();

	for (i = 0; i < 5; i++) {
		himax_mcu_register_read(addr_icid_addr, DATA_LEN_4, tmp_data);
		I("%s:Read driver IC ID = HX%X%X%X\n", __func__, tmp_data[3],
		  tmp_data[2], tmp_data[1]);

		if ((tmp_data[3] == 0x83U) && (tmp_data[2] == 0x19U) &&
			(tmp_data[1] == 0x2AU)) {

			if (tmp_data[0] == 0x05U) {
				strscpy(private_ts->chip_name, HX_83192D_PWON, 30);
			} else {
				strscpy(private_ts->chip_name, HX_83192A_PWON, 30);
			}

			ic_data->HX_FW_SIZE = FW_SIZE_128k;
			ret_data = true;
			break;
		}
	}

	if (!ret_data) {
		E("%s:Read driver ID register Fail:\n", __func__);
		E("Could NOT find Himax Chipset\n");
		E("Please check 1.VCCD,VCCA,VSP,VSN\n");
		E("2.LCM_RST,TP_RST\n");
		E("3.Power On Sequence\n");
	}

	return ret_data;
}

bool hx83192_init(void)
{
	bool ret = false;

	I("%s\n", __func__);
	ret = hx83192_chip_detect();
	return ret;
}
