// SPDX-License-Identifier: GPL-2.0
/*  Himax Android Driver Sample Code for hx8530 chipset
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

#define HX8530_ADDR_RELOAD_STATUS          0x80090000U
#define HX8530_ADDR_RELOAD_CRC32_RESULT    0x80090018U
#define HX8530_ADDR_RELOAD_ADDR_FROM       0x80090020U
#define HX8530_ADDR_RELOAD_ADDR_CMD_BEAT   0x80090028U

/****** Automotive Projerct Info. ******/

static void hx8530_mcu_burst_mode_enable(void)
{
	uint8_t tmp_data[DATA_LEN_4];
	uint8_t auto_add_4_byte = 0x01U;
	int ret;

#if defined(HIMAX_I2C_PLATFORM)
	tmp_data[0] = ((uint8_t)para_AHB_INC4 | auto_add_4_byte);
#else
	tmp_data[0] = (0x12U | auto_add_4_byte);
#endif

	ret = himax_bus_write(addr_AHB_INC4, tmp_data, 1,
			      HIMAX_I2C_RETRY_TIMES);
	if (ret < 0) {
		E("%s: i2c access fail!\n", __func__);
		return;
	}
}

static void hx8530_mcu_burst_mode_disable(void)
{
	uint8_t tmp_data[DATA_LEN_4];
	int ret;

#if defined(HIMAX_I2C_PLATFORM)
	tmp_data[0] = (uint8_t)para_AHB_INC4;
#else
	tmp_data[0] = 0x12U;
#endif

	ret = himax_bus_write(addr_AHB_INC4, tmp_data, 1,
			      HIMAX_I2C_RETRY_TIMES);
	if (ret < 0) {
		E("%s: i2c access fail!\n", __func__);
		return;
	}
}

static void hx8530_sense_on(void)
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
		/* reset code*/
		tmp_data[0] = 0x00;

		ret = himax_bus_write(addr_sense_on_off_0, tmp_data, 1,
					  HIMAX_I2C_RETRY_TIMES);
		if (ret < 0) {
			E("%s: i2c access fail!\n", __func__);
		}

		usleep_range(20000, 21000);

		ret = himax_bus_write(addr_sense_on_off_1, tmp_data, 1,
					  HIMAX_I2C_RETRY_TIMES);
		if (ret < 0) {
			E("%s: i2c access fail!\n", __func__);
		}
	}
	usleep_range(20000, 21000);

#if defined(HIMAX_I2C_PLATFORM)
	ret = himax_bus_read(addr_AHB_rdata_byte_0, tmp_data,
				 DATA_LEN_4, HIMAX_I2C_RETRY_TIMES);
	if (ret < 0) {
		E("%s: i2c access fail!\n", __func__);
	}
#endif

}

static void hx8530_sense_off(void)
{
	uint8_t cnt = 0;
	uint8_t tmp_data[DATA_LEN_4] = { 0 };
	uint8_t cMax = 14U;
	uint8_t check = 0x87U;

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
			if (cnt >= cMax) {
				break;
			}
			cnt += 1U;
		} while (tmp_data[0] != check);
		I("%s: 9000005C data[0]=0x%02X, Retry times = %d\n", __func__,
		  tmp_data[0], cnt);
	}
	cnt = 0;
	do {
		tmp_data[0] = para_sense_off_0;
		tmp_data[1] = para_sense_off_1;

		(void)himax_bus_write(addr_sense_on_off_0, tmp_data, 2,
			HIMAX_I2C_RETRY_TIMES);

		himax_mcu_register_read(addr_cs_central_state, DATA_LEN_4,
			tmp_data);

		I("Master 0x900000A8 =0x%02X\n", tmp_data[0]);

		if (tmp_data[0] == 0x0CU) {
			tmp_data[0] = para_sense_off_0;
			tmp_data[1] = para_sense_off_1;
			break;
		}

		if (cnt == 6U) {
			usleep_range(10000, 11000);
			himax_mcu_tp_reset();
		}
		cnt += 1U;
	} while (cnt < 15U);

}

static void hx8530_mcu_flash_dump_func(uint32_t start_addr,
					unsigned int Flash_Size, uint8_t *flash_buffer)
{
	uint8_t tmp_data[DATA_LEN_4] = { 0 };
	uint8_t Original_speed[DATA_LEN_4] = { 0 };
	uint32_t page_prog_start = 0;
	unsigned int i = 0;

	I("%s,start addr = 0x%02X, dump size = 0x%02X\n", __func__, start_addr, Flash_Size);

	hx8530_sense_off();

	(void)memset(tmp_data, 0x0, sizeof(tmp_data));

	/* ===Get Flash Speed===*/
	himax_mcu_register_read(addr_spi200_flash_speed, DATA_LEN_4,
				Original_speed);

	/* ===Set Flash Speed===*/
	himax_parse_assign_cmd(data_set_flash_speed, tmp_data,
			       sizeof(tmp_data));
	himax_mcu_register_write(addr_spi200_flash_speed, DATA_LEN_4, tmp_data);

	page_prog_start = start_addr;

	for (i = 0; i < Flash_Size; i += 256U) {
		himax_mcu_register_read(page_prog_start, 256,
					&flash_buffer[i]);
		page_prog_start += 256U;
	}

	/* ===Set Flash Speed===*/
	himax_mcu_register_write(addr_spi200_flash_speed, DATA_LEN_4,
				 Original_speed);

	hx8530_sense_on();
}
/*
static void hx8530_mcu_active_Quad_enable(void)
{
	uint8_t tmp_data[DATA_LEN_4] = { 0 };

	I("%s Enter\n", __func__);

	himax_parse_assign_cmd(data_spi200_trans_ctrl_2, tmp_data,
					sizeof(tmp_data));
	himax_mcu_register_write(addr_spi200_trans_ctrl, DATA_LEN_4,
					tmp_data);

	himax_parse_assign_cmd(data_spi200_cmd_2, tmp_data,
					sizeof(tmp_data));
	himax_mcu_register_write(addr_spi200_cmd, DATA_LEN_4,
					tmp_data);

	himax_parse_assign_cmd(0x40000000U, tmp_data,
					sizeof(tmp_data));
	himax_mcu_register_write(addr_spi200_trans_ctrl, DATA_LEN_4,
					tmp_data);

	himax_parse_assign_cmd(0x00000002U, tmp_data,
					sizeof(tmp_data));
	himax_mcu_register_write(addr_spi200_data, DATA_LEN_4,
					tmp_data);
	himax_parse_assign_cmd(0x00000031U, tmp_data,
					sizeof(tmp_data));
	himax_mcu_register_write(addr_spi200_cmd, DATA_LEN_4,
					tmp_data);

	himax_parse_assign_cmd(0x00000000U, tmp_data,
					sizeof(tmp_data));
	himax_mcu_register_write(addr_spi200_data, DATA_LEN_4,
					tmp_data);
	himax_parse_assign_cmd(0x00000035U, tmp_data,
					sizeof(tmp_data));
	himax_mcu_register_write(addr_spi200_cmd, DATA_LEN_4,
					tmp_data);

}
*/
bool hx8530_mcu_flash_programming(const u8 *FW_content, unsigned int start_addr,
					  unsigned int length)
{
	unsigned int page_prog_start = 0;
	uint8_t tmp_data[DATA_LEN_4] = { 0 };
	uint8_t buring_data[FLASH_RW_MAX_LEN]; 
	uint8_t Original_speed[DATA_LEN_4] = { 0 };
	bool ret = true;

	I("%s: please wait...\n", __func__);

	/*hx8530_mcu_active_Quad_enable();*/

	/* ===Get Flash Speed===*/
	himax_mcu_register_read(addr_spi200_flash_speed, DATA_LEN_4,
				Original_speed);

	/* ===Set Flash Speed===*/
	himax_parse_assign_cmd(data_set_flash_speed, tmp_data,
			       sizeof(tmp_data));
	himax_mcu_register_write(addr_spi200_flash_speed, DATA_LEN_4, tmp_data);

	hx8530_mcu_burst_mode_disable();

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
			ret = false;
		}

		/*Set 256 Bytes Page Write*/
		himax_parse_assign_cmd(data_spi200_trans_ctrl_4, tmp_data,
				       sizeof(tmp_data));
		himax_mcu_register_write(addr_spi200_trans_ctrl, DATA_LEN_4,
					 tmp_data);

		/*Set SPI Address*/
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

		/*Write First 16 Bytes*/
		(void)memcpy(&buring_data[ADDR_LEN_4],
		       &FW_content[page_prog_start - start_addr], 16);
		if (himax_bus_write(addr_AHB_address_byte_0, buring_data,
				    (ADDR_LEN_4 + 16U),
				    HIMAX_I2C_RETRY_TIMES) < 0) {
			E("%s: i2c access fail!\n", __func__);
			ret = false;
			break;
		}
		/*Write Command: PP*/
		himax_parse_assign_cmd(data_spi200_cmd_6, tmp_data,
				       sizeof(tmp_data));
		himax_mcu_register_write(addr_spi200_cmd, DATA_LEN_4, tmp_data);

		/*Write Remaining 240 Bytes*/
		(void)memcpy(&buring_data[ADDR_LEN_4],
		       &FW_content[page_prog_start - start_addr + 16U], 240U);

		if (himax_bus_write(addr_AHB_address_byte_0, buring_data,
				    (ADDR_LEN_4 + 240U),
				    HIMAX_I2C_RETRY_TIMES) < 0) {
			E("%s: i2c access fail!\n", __func__);
			ret = false;
			break;
		}

		if (!himax_mcu_wait_wip(1)) {
			E("%s:Flash_Programming Fail\n", __func__);
			ret = false;
			break;
		}
	}
	/* ===Set Flash Speed===*/
	himax_mcu_register_write(addr_spi200_flash_speed, DATA_LEN_4,
				 Original_speed);
	return ret;
}

uint32_t hx8530_mcu_check_CRC(uint8_t *start_addr, unsigned int reload_length)
{
	uint32_t result = 0;
	uint8_t tmp_data[DATA_LEN_4] = { 0 };
	uint8_t counter = 0;
	unsigned int length = reload_length / DATA_LEN_4;

	himax_mcu_register_write(HX8530_ADDR_RELOAD_ADDR_FROM, DATA_LEN_4,
					start_addr);

	tmp_data[3] = 0x00;
	tmp_data[2] = 0x99;
	tmp_data[1] = (uint8_t)(length >> 8U);
	tmp_data[0] = (uint8_t)length;

	himax_mcu_register_write(HX8530_ADDR_RELOAD_ADDR_CMD_BEAT, DATA_LEN_4,
					tmp_data);

	do {
		himax_mcu_register_read(HX8530_ADDR_RELOAD_STATUS, DATA_LEN_4,
							tmp_data);

		if ((tmp_data[0] & 0x01U) != 0x01U) {
			himax_mcu_register_read(HX8530_ADDR_RELOAD_CRC32_RESULT,
						      DATA_LEN_4, tmp_data);

			result = (((uint32_t)tmp_data[3] << 24)
					+ ((uint32_t)tmp_data[2] << 16)
					+ ((uint32_t)tmp_data[1] << 8)
					+ (uint32_t)tmp_data[0]);
			if (result != 0x00000000U) {
				I("%s: CRC result=0x%08X\n",  __func__, result);
			}
			break;
		} else if (tmp_data[1] != 0x99U) {
			I("%s:*(0x8009_0000)  data[1]=0x%02X,data[0]=0x%02X\n",
				__func__, tmp_data[1], tmp_data[0]);
			E("%s: Reload status cmd fail and out of retry count!\n", __func__);
			result = HW_CRC_FAIL;
			break;
		} else {
			usleep_range(1000, 1100);
			if (counter >= 100U) {
				I("%s:CRC Wait loop timeout\n", __func__);
				himax_mcu_read_FW_status();
				result = HW_CRC_FAIL;
			}
		}
		counter += 1U;
	} while (counter < 100U);

	return result;
}

bool hx8530_mcu_calculateChecksum(uint32_t size)
{
	uint8_t tmp_addr[DATA_LEN_4] = { 0 };
	uint8_t start_addr[DATA_LEN_4] = { 0 };

	HX8530_CFG_1_SECTION_ADDR = 0x00000000U;
	HX8530_CFG_1_SECTION_SIZE = HX8K;

	himax_parse_assign_cmd(HX8530_CFG_1_SECTION_ADDR,
		start_addr, sizeof(start_addr));
	if (hx8530_mcu_check_CRC(start_addr,
			HX8530_CFG_1_SECTION_SIZE) != 0x00000000U) {
		ic_data->HX8530_upgrade_section |= 0xF0U;
		E("[CFG 8K Section] CRC Fail \n");
	} else {
		ic_data->HX8530_upgrade_section &= 0x0FU;
		I("[CFG 8K Section] CRC Pass\n");
	}
	msleep(50);
	
	HX8530_ALG_2_SECTION_ADDR = HX8K;
	HX8530_ALG_2_SECTION_SIZE = HX247K;
	himax_parse_assign_cmd(HX8530_ALG_2_SECTION_ADDR,
		start_addr, sizeof(start_addr));
	if (hx8530_mcu_check_CRC(start_addr,
			HX8530_ALG_2_SECTION_SIZE) != 0x00000000U) {
		ic_data->HX8530_upgrade_section |= 0x0FU;
		E("[ALG 247K Section] CRC Fail \n");
	} else {
		ic_data->HX8530_upgrade_section &= 0xF0U;
		I("[ALG 247K Section] CRC Pass\n");
	}
	msleep(50);
	return (ic_data->HX8530_upgrade_section == 0U) ? true : false;
}

void hx8530_mcu_touch_information(void)
{
	uint8_t data[DATA_LEN_8] = { 0 };

	I("%s Enter\n", __func__);

	himax_mcu_register_read(
		addr_fw_define_chip_rx_tx_num, DATA_LEN_4, data);
	ic_data->HX_RX_NUM = data[2];
	ic_data->HX_TX_NUM = data[3];

	himax_mcu_register_read(
		addr_fw_define_maxpt, DATA_LEN_4, data);
	ic_data->HX_MAX_PT = data[0];

	himax_mcu_register_read(
		addr_fw_define_int_is_edge, DATA_LEN_4, data);
	ic_data->HX_INT_IS_EDGE = ((data[1] & 0x01U) == 0x01U);

	himax_mcu_register_read(
		addr_fw_HX_ID_EN, DATA_LEN_4, data);
	ic_data->HX_IS_ID_EN = ((data[1] & 0x02U) == 0x02U);
	ic_data->HX_ID_PALM_EN = ((data[1] & 0x80U) == 0x80U);
	ic_data->STOP_FW_BY_HOST_EN = ((data[1] & 0x01U) == 0x01U);

	himax_mcu_register_read(
		addr_fw_define_xy_res, DATA_LEN_4, data);
	ic_data->HX_Y_RES = ((uint16_t)data[2] << 8U);
	ic_data->HX_Y_RES += (uint16_t)data[3];
	ic_data->HX_X_RES = ((uint16_t)data[0] << 8U);
	ic_data->HX_X_RES += (uint16_t)data[1];

	private_ts->nFinger_support = ic_data->HX_MAX_PT;
	private_ts->pdata->abs_x_min = 0;
	private_ts->pdata->abs_x_max = ic_data->HX_X_RES;
	private_ts->pdata->abs_y_min = 0;
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

bool hx8530_mcu_fts_ctpm_fw_upgrade(const u8 *fw_data, unsigned int bin_size)
{
	uint8_t counter = 0U;
	struct time_var timeStart;
	struct time_var timeEnd;
	struct time_var timeDelta;
	uint8_t tmp_addr[DATA_LEN_4] = { 0 };
	uint32_t start_addr = 0U;
	uint32_t process_size = 0U;
	u8 *hybrid_fw = NULL;

	hybrid_fw = kzalloc(sizeof(uint8_t) * FW_SIZE_255k, GFP_KERNEL);
	if (hybrid_fw == NULL) {
		E("%s: Memory allocation falied!\n", __func__);
		return;
	}
	time_func(&timeStart);
	if (bin_size == FW_SIZE_255k) {
		if ((ic_data->HX8530_upgrade_section & 0xFFU) == 0xFFU) {
			/*Entire 255K*/
			for (counter = 0U; counter < 3U; counter++) {
				g_core_fp.fp_sense_off();
				himax_mcu_init_psl();
				himax_disable_flash_protected_mode();
				himax_mcu_block_erase(0x00U, FW_SIZE_255k);
				if (g_core_fp.fp_flash_programming(fw_data,
					0U, FW_SIZE_255k) == false) {
					E("[255K_FW] upgrade fail %d times\n",
						counter);
					himax_mcu_tp_reset();
					continue;
				}

				(void)hx8530_mcu_calculateChecksum(process_size);
				if ((ic_data->HX8530_upgrade_section & 0xFFU) == 0x00U) {
					I("[255K_FW] upgrade done \n");
					break;
				}
				himax_mcu_tp_reset();
			}
		} else if ((ic_data->HX8530_upgrade_section & 0xF0U) == 0xF0U) {
			/*CFG 8K*/
			for (counter = 0U; counter < 3U; counter++) {
				g_core_fp.fp_sense_off();
				himax_mcu_init_psl();
				himax_disable_flash_protected_mode();
				start_addr = 0x00000000U;
				process_size = HX8K;
				himax_mcu_sector_erase(start_addr, process_size);
				if (g_core_fp.fp_flash_programming(fw_data,
					start_addr, process_size) == false) {
					E("[8K_CFG] upgrade fail %d times\n",
						counter);
					himax_mcu_tp_reset();
					continue;
				}

				(void)hx8530_mcu_calculateChecksum(process_size);
				if ((ic_data->HX8530_upgrade_section & 0xF0U) == 0x00U) {
					I("[8K_CFG] upgrade done \n");
					break;
				}
				himax_mcu_tp_reset();
			}
		} else if ((ic_data->HX8530_upgrade_section & 0x0FU) == 0x0FU) {
			/*ALG 247K*/
			start_addr = 0x00000000U;
			process_size = HX8K;
			g_core_fp.fp_flash_dump_func(start_addr, process_size, hybrid_fw);
			(void)memcpy(&hybrid_fw[HX8K], &fw_data[HX8K], HX247K);

			for (counter = 0U; counter < 3U; counter++) {
				g_core_fp.fp_sense_off();
				himax_mcu_init_psl();
				himax_disable_flash_protected_mode();
				himax_mcu_block_erase(0x00U, FW_SIZE_255k);
				if (g_core_fp.fp_flash_programming(hybrid_fw,
					0U, FW_SIZE_255k) == false) {
					E("[247K_ALG] upgrade fail %d times\n",
						counter);
					himax_mcu_tp_reset();
					continue;
				}

				(void)hx8530_mcu_calculateChecksum(process_size);
				if ((ic_data->HX8530_upgrade_section & 0x0FU) == 0x00U) {
					I("[247K_ALG] upgrade done \n");
					break;
				}
				himax_mcu_tp_reset();
			}
		}
	} else if (bin_size == HX8K) {
		if ((ic_data->HX8530_upgrade_section & 0xF0U) == 0xF0U) {
			/*CFG 8K*/
			for (counter = 0U; counter < 3U; counter++) {
				g_core_fp.fp_sense_off();
				himax_mcu_init_psl();
				himax_disable_flash_protected_mode();
				start_addr = 0x00000000U;
				process_size = HX8K;
				himax_mcu_sector_erase(start_addr, process_size);
				if (g_core_fp.fp_flash_programming(fw_data,
					start_addr, process_size) == false) {
					E("[8K_CFG] upgrade fail %d times\n",
						counter);
					himax_mcu_tp_reset();
					continue;
				}

				(void)hx8530_mcu_calculateChecksum(process_size);
				if ((ic_data->HX8530_upgrade_section & 0xF0U) == 0x00U) {
					I("[8K_CFG] upgrade done \n");
					break;
				}
				himax_mcu_tp_reset();
			}
		}
	} else if (bin_size == HX247K) {
		if ((ic_data->HX8530_upgrade_section & 0x0FU) == 0x0FU) {
			/*ALG 247K*/
			start_addr = 0x00000000U;
			process_size = HX8K;
			g_core_fp.fp_flash_dump_func(start_addr, process_size, hybrid_fw);
			(void)memcpy(&hybrid_fw[HX8K], &fw_data[0], HX247K);

			for (counter = 0U; counter < 3U; counter++) {
				g_core_fp.fp_sense_off();
				himax_mcu_init_psl();
				himax_disable_flash_protected_mode();
				himax_mcu_block_erase(0x00U, FW_SIZE_255k);
				if (g_core_fp.fp_flash_programming(hybrid_fw,
					0U, FW_SIZE_255k) == false) {
					E("[247K_ALG] upgrade fail %d times\n",
						counter);
					himax_mcu_tp_reset();
					continue;
				}

				(void)hx8530_mcu_calculateChecksum(process_size);
				if ((ic_data->HX8530_upgrade_section & 0x0FU) == 0x00U) {
					I("[247K_ALG] upgrade done \n");
					break;
				}
				himax_mcu_tp_reset();
			}
		}
	} else {
		E("%s: Undefined file szie\n", __func__);
	}
	time_func(&timeEnd);
	timeDelta = time_diff(timeStart, timeEnd);
#if defined(KERNEL_VER_5_10)
	I("<<Timer>>%s => %lld.%ld s\n", __func__,
		timeDelta.tv_sec, timeDelta.tv_nsec);
#else
	I("<<Timer>>%s => %ld.%ld s\n", __func__,
		timeDelta.tv_sec, timeDelta.tv_nsec);
#endif
	kfree(hybrid_fw);
	hybrid_fw = NULL;
	return (ic_data->HX8530_upgrade_section == 0U) ? true : false;
}

uint8_t hx8530_identify_project_type(void)
{
	uint8_t flash_buf[FLASH_RW_MAX_LEN] = { 0 };
	uint8_t tmp_data[DATA_LEN_4] = { 0 };
	uint8_t data_sz = 0x10U;
	uint8_t i = 0;
	uint16_t j = 0;
	uint16_t chk_end = 0;
	uint16_t chk_sum = 0;
	uint32_t map_code = 0;
	uint32_t flash_addr = 0x00000000U;

	himax_mcu_register_read(flash_addr, FLASH_RW_MAX_LEN, flash_buf);

	for (i = 0; i < 0xF0U; i = i + data_sz) {
		for (j = i; j < (i + data_sz); j++) {
			chk_end |= flash_buf[j];
			chk_sum += flash_buf[j];
		}
		if (chk_end == 0x0000U) { /*1. Check all zero*/
			return 0x00000000U;
		} else if ((chk_sum % 0x100U) != 0U) { /*2. Check sum*/
			I("%s: chk sum failed in %X\n", __func__, i);
		} else { /*3. get data*/
			map_code = (uint32_t)flash_buf[i]
					+ ((uint32_t)flash_buf[i + 1U] << 8U)
					+ ((uint32_t)flash_buf[i + 2U] << 16U)
					+ ((uint32_t)flash_buf[i + 3U] << 24U);
			flash_addr = (uint32_t)flash_buf[i + 4U]
					+ ((uint32_t)flash_buf[i + 5U] << 8U)
					+ ((uint32_t)flash_buf[i + 6U] << 16U)
					+ ((uint32_t)flash_buf[i + 7U] << 24U);
			if (map_code == FW_VER) {
				break;
			}
		}
		chk_end = 0;
		chk_sum = 0;
	}
	himax_mcu_register_read(flash_addr, DATA_LEN_4, tmp_data);
	I("FW_VER_MAJ_FLASH_ADDR *(0x%08X) = 0x%02X\n",
		flash_addr, tmp_data[1]);
	return tmp_data[1];

}

static void hx8530_Automotive_Project_init(void)
{
	g_core_fp.fp_sense_on = hx8530_sense_on;
	g_core_fp.fp_sense_off = hx8530_sense_off;
	g_core_fp.fp_flash_programming = hx8530_mcu_flash_programming;
	g_core_fp.fp_flash_dump_func = hx8530_mcu_flash_dump_func;
	g_core_fp.fp_check_CRC = hx8530_mcu_check_CRC;
	g_core_fp.fp_calculateChecksum = hx8530_mcu_calculateChecksum;
	g_core_fp.fp_touch_information = hx8530_mcu_touch_information;
	g_core_fp.fp_get_DSRAM_data = himax_mcu_get_DSRAM_data;
	g_core_fp.fp_burst_mode_enable = hx8530_mcu_burst_mode_enable;
	g_core_fp.fp_fts_ctpm_fw_upgrade = hx8530_mcu_fts_ctpm_fw_upgrade;

}

static bool HX8530_chip_detect(void)
{
	bool ret_data = false;
	uint8_t tmp_data[DATA_LEN_4] = { 0 };
	uint8_t i = 0U;
	uint8_t project_id = 0x00U;
	int ret = 0;

	ret = himax_bus_read(addr_AHB_continous, tmp_data,
				 1, HIMAX_I2C_RETRY_TIMES);
	if (ret < 0) {
		E("%s: bus access fail!\n", __func__);
	} else {

		hx8530_sense_off();

		for (i = 0; i < 5U; i++) {
			himax_mcu_register_read(addr_icid_addr, DATA_LEN_4, tmp_data);
			I("%s:Read driver IC ID = HX%2X%2X%02X\n", __func__, tmp_data[3],
			tmp_data[2], tmp_data[1]);

			if ((tmp_data[3] == 0x85U) && (tmp_data[2] == 0x30U) &&
				((tmp_data[1] == 0x0AU) || (tmp_data[1] == 0x0BU))) {

				strlcpy(private_ts->chip_name, HX_8530_PWON, 30);
				ic_data->HX_FW_SIZE = FW_SIZE_255k;
				ret_data = true;
				break;
			}
		}
		if (ret_data == false) {
			E("%s:Read driver ID register Fail:\n", __func__);
			E("Could NOT find Himax Chipset\n");
			E("Please check 1.VCCD,VCCA,VSP,VSN\n");
			E("2.LCM_RST,TP_RST\n");
			E("3.Power On Sequence\n");
		} else {
			for (i = 0; i < 5U; i++) {
				project_id = 0x80;//hx8530_identify_project_type();
				if (project_id == 0x80U) {
					I("[Welcome] to Automotive Project \n");
					hx8530_Automotive_Project_init();
					break;
				} else {
					/*UB*/
					W("??? Undefined behavior ???\n");
				}

			}
		}
	}
	return ret_data;
}

bool hx8530_init(void)
{
	bool ret = false;

	I("%s\n", __func__);
	ret = HX8530_chip_detect();
	return ret;
}
