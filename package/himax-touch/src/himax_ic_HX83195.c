// SPDX-License-Identifier: GPL-2.0
/*  Himax Android Driver Sample Code for hx83195 chipset
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

static void hx83195_mcu_burst_mode_enable(void)
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

static void hx83195_mcu_burst_mode_disable(void)
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

static void hx83195_double_safe_mode(void)
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

static void hx83195_sense_on(void)
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

static void hx83195_sense_off(void)
{
	uint8_t cnt = 0;
	uint8_t tmp_data[DATA_LEN_4] = { 0 };
	uint8_t cMax = 14;
	uint8_t check = (uint8_t) 0x87;
	int ret = 0;
	uint8_t slave_id = 1U;

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

	if (tmp_data[0] != check) {
		for (slave_id = 1U; slave_id <= private_ts->slave_ic_num; slave_id++) {
			cnt = 0;
			do {
				tmp_data[0] = para_sense_off_0;
				tmp_data[1] = para_sense_off_1;

				ret = himax_bus_write_slave(slave_id, addr_sense_on_off_0, tmp_data, 2,
							HIMAX_I2C_RETRY_TIMES);
				if (ret < 0) {
					W("[IC_SLAVE_%d] %s: i2c access fail!\n", slave_id, __func__);
				}
				himax_mcu_register_read_slave(slave_id, addr_cs_central_state, DATA_LEN_4, tmp_data);

				if (tmp_data[0] == 0x0CU) {
					break;
				}
				cnt += 1U;
			} while (cnt < 15U);
			I("[IC_SLAVE_%d] %s: 0x9000_00A8 data[0]=0x%02X, Retry %d times\n",
				slave_id, __func__, tmp_data[0], cnt);
		}
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
			hx83195_double_safe_mode();
		} else if (cnt == 6U) {
			usleep_range(10000, 11000);
			himax_mcu_tp_reset();
		} else {
			/* do nothing*/
		}
		cnt += 1U;
	} while (cnt < 15U);

}

static void hx83195_mcu_flash_dump_func(uint32_t start_addr,
					unsigned int Flash_Size, uint8_t *flash_buffer)
{
	uint32_t page_prog_start = 0;
	unsigned int i = 0;

	I("%s,start addr = 0x%02X, dump size = 0x%02X\n", __func__, start_addr, Flash_Size);
	hx83195_sense_off();

	page_prog_start = start_addr;

	for (i = 0; i < Flash_Size; i += 256U) {
		himax_mcu_register_read(page_prog_start, 256,
					&flash_buffer[i]);
		page_prog_start += 256U;
	}

	hx83195_sense_on();
}

static bool hx83195_mcu_flash_programming(const u8 *FW_content, unsigned int start_addr,
					  unsigned int length)
{
	unsigned int page_prog_start = 0;
	uint8_t tmp_data[DATA_LEN_4] = { 0 };
	uint8_t buring_data[FLASH_RW_MAX_LEN] = { 0 }; /* Read for flash data, 128K*/
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

	hx83195_mcu_burst_mode_disable();

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

#if (HX_FIX_TOUCH_INFO == 0x01)
void hx83195_mcu_information_check(void)
{
	uint8_t data[DATA_LEN_8] = { 0 };
	uint8_t j, check_sum = 0, retry = 0;
	uint8_t FW_MAX_PT = 0;
	uint8_t FW_CHIP_RX_MAX = 0, FW_CHIP_TX_MAX = 0;
	uint8_t FW_RX_IC_NUM = 0, FW_TX_IC_NUM = 0;
	bool FW_INT_IS_EDGE = false;
	bool FW_IS_ID_EN = false;
	bool FW_ID_PALM_EN = false;
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
				FW_CHIP_RX_MAX = data[2];
				FW_CHIP_TX_MAX = data[3];
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
			} else if (addr == addr_fw_define_total_rx_tx_rxic_num) {
				FW_RX_NUM = data[1];
				FW_TX_NUM = data[2];
				FW_RX_IC_NUM = data[3];
			} else if (addr == addr_fw_define_total_txic_num) {
				FW_TX_IC_NUM = data[0];
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
	if (ic_data->HX_RX_IC_NUM != FW_RX_IC_NUM) {
		W("%s: RX_IC_NUM: %d mismatch with FW: %d\n", __func__,
		ic_data->HX_RX_IC_NUM, FW_RX_IC_NUM);
	}
	if (ic_data->HX_TX_IC_NUM != FW_TX_IC_NUM) {
		W("%s: TX_IC_NUM: %d mismatch with FW: %d\n", __func__,
		ic_data->HX_TX_IC_NUM, FW_TX_IC_NUM);
	}
}
#endif

bool hx83195_mcu_calculateChecksum(uint32_t size)
{
	uint8_t CRC_result = 0;
	uint8_t tmp_addr[DATA_LEN_4] = { 0 };

	himax_parse_assign_cmd(addr_program_reload_from, tmp_addr,
			       sizeof(tmp_addr));
#if (HX_FIX_TOUCH_INFO == 0x01)
	CRC_result = himax_mcu_check_CRC(tmp_addr, FIX_FW_SIZE);
#else
	CRC_result = himax_mcu_check_CRC(tmp_addr, FW_SIZE_255k);
	msleep(50);

	if (CRC_result != 0) {
		I("%s: Fail=%d when checking 255K region\n", __func__, CRC_result);
		CRC_result = himax_mcu_check_CRC(tmp_addr, FW_SIZE_192k);
		msleep(50);

		if (CRC_result != 0) {
			I("%s: Fail=%d when checking 192K region\n", __func__, CRC_result);
		} else {
			ic_data->HX_FW_SIZE = FW_SIZE_192k;
			I("%s: Pass when checking 192K region\n", __func__);
		}
	} else {
		ic_data->HX_FW_SIZE = FW_SIZE_255k;
		I("%s: Pass when checking 255K region\n", __func__);
	}
#endif

	return (CRC_result == 0) ? true : false;
}

#if (HIMAX_LTDI_CONFIG == 0x01)
void hx83195_mcu_touch_information(void)
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
		addr = fw_setting_addr;
		do {
			himax_mcu_register_read(addr, DATA_LEN_4, data);
			if (addr == addr_fw_define_chip_rx_tx_num) {
				ic_data->HX_CHIP_RX_MAX = data[2];
				ic_data->HX_CHIP_TX_MAX = data[3];
			} else if (addr == addr_fw_define_maxpt) {
				ic_data->HX_MAX_PT = data[0];
			} else if (addr == addr_fw_define_int_is_edge) {
				ic_data->HX_INT_IS_EDGE = ((data[1] & 0x01U) == 0x01U);
			} else if (addr == addr_fw_HX_ID_EN) {
				ic_data->HX_IS_ID_EN = ((data[1] & 0x02U) == 0x02U);
				ic_data->HX_ID_PALM_EN = ((data[1] & 0x80U) == 0x80U);
				ic_data->STOP_FW_BY_HOST_EN = ((data[1] & 0x01U) == 0x01U);
			} else if (addr == addr_fw_define_xy_res) {
				ic_data->HX_Y_RES = ((uint16_t)data[2] << 8U)
									| (uint16_t)data[3];
				ic_data->HX_X_RES = ((uint16_t)data[0] << 8U)
									| (uint16_t)data[1];
			} else if (addr == addr_fw_define_total_rx_tx_rxic_num) {
				ic_data->HX_RX_NUM = data[1];
				ic_data->HX_TX_NUM = data[2];
				ic_data->HX_RX_IC_NUM = data[3];
			} else if (addr == addr_fw_define_total_txic_num) {
				ic_data->HX_TX_IC_NUM = data[0];
			} else {
				/*do nothing*/
			}
			for (j = 0; j < DATA_LEN_4; j++) {
				check_sum += data[j];
			}
			addr += DATA_LEN_4;
		} while (addr < addr_fw_setting_end);
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
	ic_data->HX_RX_IC_NUM = (uint8_t)FIX_HX_RX_IC_NUM;
	ic_data->HX_TX_IC_NUM = (uint8_t)FIX_HX_TX_IC_NUM;
	ic_data->HX_CHIP_RX_MAX = (uint8_t)FIX_HX_CHIP_RX_MAX;
	ic_data->HX_CHIP_TX_MAX = (uint8_t)FIX_HX_CHIP_TX_MAX;

	hx83195_mcu_information_check();

	himax_mcu_register_read(addr_fw_HX_ID_EN, DATA_LEN_4, data);
	ic_data->STOP_FW_BY_HOST_EN = ((data[1] & 0x01U) == 0x01U);
#endif

	private_ts->nFinger_support = ic_data->HX_MAX_PT;
	private_ts->pdata->abs_x_min = 0;
	private_ts->pdata->abs_x_max = ic_data->HX_X_RES;
	private_ts->pdata->abs_y_min = 0;
	private_ts->pdata->abs_y_max = ic_data->HX_Y_RES;
	private_ts->slave_ic_num = (ic_data->HX_RX_IC_NUM
								* ic_data->HX_TX_IC_NUM) - 1U;

	I("Number of ICs : [%d]\n", (private_ts->slave_ic_num + 1U));

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
	I("%s:HX_RX_IC_NUM =%d,HX_TX_IC_NUM =%d\n", __func__,
		ic_data->HX_RX_IC_NUM,
		ic_data->HX_TX_IC_NUM);
	I("%s:HX_CHIP_RX_MAX =%d,HX_CHIP_TX_MAX =%d\n", __func__,
		ic_data->HX_CHIP_RX_MAX,
		ic_data->HX_CHIP_TX_MAX);
}

bool hx83195_mcu_get_DSRAM_data(uint8_t *tmp_rawdata)
{
	unsigned int i = 0;
	unsigned char tmp_data[DATA_LEN_4];
	uint8_t max_i2c_size = MAX_I2C_TRANS_SZ;
	uint8_t chip_id_sel = 0;
	uint8_t total_ic_num = ic_data->HX_TX_IC_NUM * ic_data->HX_RX_IC_NUM;
	uint8_t chip_rx_num = 0;
	uint8_t chip_tx_num = 0;
	uint16_t x_num = ic_data->HX_CHIP_RX_MAX;
	uint16_t y_num = ic_data->HX_CHIP_TX_MAX;
	uint16_t chip_frame_size = 0;
	unsigned int rawdata_index = 0;
	uint16_t total_size = (((x_num * y_num) + x_num + y_num) * 2U) + 4U;
	uint8_t *rawdata_buffer = NULL;
	uint16_t check_sum_cal = 0;

	rawdata_buffer = kcalloc((total_size + 8U), sizeof(uint8_t), GFP_KERNEL);
	if (rawdata_buffer == NULL) {
		E("%s, Failed to allocate memory\n", __func__);
		return false;
	}

	for (chip_id_sel = 0; chip_id_sel < total_ic_num; chip_id_sel++) {
		himax_mcu_register_read(addr_raw_out_sel, DATA_LEN_4, tmp_data);
		himax_mcu_diag_register_set(tmp_data[0], chip_id_sel);

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

			if (chip_id_sel != rawdata_buffer[total_size - 17U]) {
				W("chip_id_sel not match FW = %d\n", rawdata_buffer[total_size - 17U]);
			}
			/*I("raw_out_sel in FW = %d\n", rawdata_buffer[total_size - 18]);*/
			chip_rx_num = rawdata_buffer[total_size - 15U];
			chip_tx_num = rawdata_buffer[total_size - 16U];
			/*I("rx_num:%d,tx_num:%d\n", chip_rx_num, chip_tx_num);*/
			chip_frame_size = (x_num * y_num * 2U);

			(void)memcpy(&tmp_rawdata[rawdata_index], &rawdata_buffer[4],
				chip_frame_size * sizeof(uint8_t));
			rawdata_index += chip_frame_size;

		}

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
#endif

static void hx83195_func_re_init(void)
{
	g_core_fp.fp_sense_on = hx83195_sense_on;
	g_core_fp.fp_sense_off = hx83195_sense_off;
	g_core_fp.fp_flash_dump_func = hx83195_mcu_flash_dump_func;
	g_core_fp.fp_flash_programming = hx83195_mcu_flash_programming;
	g_core_fp.fp_check_CRC = himax_mcu_check_CRC;
	g_core_fp.fp_calculateChecksum = hx83195_mcu_calculateChecksum;
	g_core_fp.fp_burst_mode_enable = hx83195_mcu_burst_mode_enable;
	g_core_fp.fp_fts_ctpm_fw_upgrade = himax_mcu_fts_ctpm_fw_upgrade;
#if (HIMAX_LTDI_CONFIG == 0x00)
	g_core_fp.fp_get_DSRAM_data = himax_mcu_get_DSRAM_data;
	g_core_fp.fp_touch_information = himax_mcu_touch_information;
#else
	g_core_fp.fp_get_DSRAM_data = hx83195_mcu_get_DSRAM_data;
	g_core_fp.fp_touch_information = hx83195_mcu_touch_information;
#endif
}

static bool hx83195_chip_detect(void)
{
	uint8_t tmp_data[DATA_LEN_4] = { 0 };
	uint8_t product_data[DATA_LEN_4] = { 0 };
	bool ret_data = true;
	uint8_t i = 0U;
	uint32_t id_check = 0U;
	uint8_t total_num = 0U;

	hx83195_func_re_init();

	for (i = 0; i < 5U; i++) {
		himax_mcu_register_read(addr_chk_tp_status, DATA_LEN_4, tmp_data);
		total_num = ((tmp_data[1] & 0xF0U) >> 4U);
		private_ts->LTDI_product = ((tmp_data[0] & 0x10U) >> 4U);

		if ((total_num >= 1U) || (total_num <= 8U)) {
			private_ts->slave_ic_num = (total_num - 1U);
		} else {
			E("%s:Wrong ic number detected :\n", __func__);
			E(">> 0x9000_00EC: 0xXXXX_%02X%02X\n", tmp_data[1], tmp_data[0]);
			ret_data = false;
		}

		if (private_ts->LTDI_product != HIMAX_LTDI_CONFIG) {
			E("Wrong product type setting :\n");
			if (private_ts->LTDI_product == 0x01U) {
				E(">> LTDI detected, but TDDI setting using\n");
			} else if (private_ts->LTDI_product == 0x00U) {
				E(">> TDDI detected, but LDTI setting using\n");
			} else {
				E(">> Unknown product type\n");
			}
			E(">> 0x9000_00EC: 0xXXXX_%02X%02X\n", tmp_data[1], tmp_data[0]);
			ret_data = false;
		}

		himax_mcu_register_read(addr_icid_addr, DATA_LEN_4, tmp_data);
		himax_mcu_register_read(addr_product_id, DATA_LEN_4, product_data);
		id_check = 0U;
		id_check |= ((uint32_t) tmp_data[3] << 16U);
		id_check |= ((uint32_t) tmp_data[2] << 8U);
		id_check |= ((uint32_t) product_data[1]);
		if (id_check != HIMAX_PRODUCT_TYPE) {
			ret_data = false;
			E("IC ID: Current [HX%06X] not match setting!\n", id_check);
		}

		if (ret_data == false) {
			E("%s:Product or IC Type reading fail:\n", __func__);
			E("Could NOT find Himax Chipset\n");
			E("Please check %s\n",Check_List);
			E("Please contact FAE:%s SE:%s for technical support!\n", CN_FAE, TW_SE);
			break;
		}
	}

	if (ret_data == true) {
		I("Number of ICs: [%d]\n", (private_ts->slave_ic_num + 1U));
		I("%s:Read IC ID = HX%06X\n", __func__, id_check);
		ic_data->HX_FW_SIZE = FW_SIZE_255k;
		hx83195_sense_off();
	}

	return ret_data;
}

bool hx83195_init(void)
{
	bool ret = false;

	I("%s\n", __func__);
	ret = hx83195_chip_detect();
	return ret;
}
