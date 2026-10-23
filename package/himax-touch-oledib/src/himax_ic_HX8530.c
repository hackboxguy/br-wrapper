// SPDX-License-Identifier: GPL-2.0
/*  Himax Android Driver Sample Code for hx8530 chipset
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

#include "himax_platform.h"
#include "himax_common.h"
#include "himax_ic_core.h"

void hx8530_set_DSRAM_data_func(void);

/****** Automotive Projerct Info. ******/

void hx8530_mcu_burst_mode_enable(void)
{
	uint8_t tmp_data[DATA_LEN_4];
	uint8_t auto_add_4_byte = 0x01U;
	int ret;

#if defined(HIMAX_I2C_PLATFORM)
	tmp_data[0] = ((uint8_t)para_AHB_INC4 | auto_add_4_byte);
#else
	tmp_data[0] = (0x12U | auto_add_4_byte);
#endif

	ret = himax_bus_write(addr_AHB_INC4, tmp_data, 1U,
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

	ret = himax_bus_write(addr_AHB_INC4, tmp_data, 1U,
			      HIMAX_I2C_RETRY_TIMES);
	if (ret < 0) {
		E("%s: i2c access fail!\n", __func__);
		return;
	}
}

static void hx8530_sense_on(void)
{
	uint8_t tmp_data[DATA_LEN_4] = { 0U };
	uint8_t retry = 0U;
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

		ret = himax_bus_write(addr_sense_on_off_0, tmp_data, 1U,
					  HIMAX_I2C_RETRY_TIMES);
		if (ret < 0) {
			E("%s: i2c access fail!\n", __func__);
		}

		usleep_range(20000, 21000);

		ret = himax_bus_write(addr_sense_on_off_1, tmp_data, 1U,
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
	uint8_t cnt = 0U;
	uint8_t tmp_data[DATA_LEN_4] = { 0U };
	uint8_t cMax = 14U;
	uint8_t check = 0x87U;

	usleep_range(40000, 41000);

	himax_mcu_register_read(addr_cs_central_state, DATA_LEN_4, tmp_data);

	if (tmp_data[0] == 0x05U) {
		do {
			tmp_data[3] = 0x00U;
			tmp_data[2] = 0x00U;
			tmp_data[1] = 0x00U;
			tmp_data[0] = 0xA5U;
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
	cnt = 0U;
	do {
		tmp_data[0] = para_sense_off_0;
		tmp_data[1] = para_sense_off_1;

		(void)himax_bus_write(addr_sense_on_off_0, tmp_data, 2U,
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

void hx8530_mcu_flash_dump_func(uint32_t start_addr,
					unsigned int Flash_Size, uint8_t *flash_buffer)
{
	uint8_t tmp_data[DATA_LEN_4] = { 0U };
	uint8_t Original_speed[DATA_LEN_4] = { 0U };
	uint32_t page_prog_start = 0U;
	unsigned int i = 0U;

	I("%s,start addr = 0x%02X, dump size = 0x%02X\n", __func__, start_addr, Flash_Size);

	hx8530_sense_off();

	(void)memset(tmp_data, 0x00U, sizeof(tmp_data));

	/* ===Get Flash Speed===*/
	himax_mcu_register_read(addr_spi200_flash_speed, DATA_LEN_4,
				Original_speed);

	/* ===Set Flash Speed===*/
	himax_parse_assign_cmd(data_set_flash_speed, tmp_data,
			       sizeof(tmp_data));
	himax_mcu_register_write(addr_spi200_flash_speed, DATA_LEN_4, tmp_data);

	page_prog_start = start_addr;

	while (i < Flash_Size) {
		himax_mcu_register_read(page_prog_start, 256U,
					&flash_buffer[i]);
		page_prog_start += 256U;
		i += 256U;
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
	unsigned int page_prog_start = 0U;
	uint8_t tmp_data[DATA_LEN_4] = { 0U };
	uint8_t buring_data[FLASH_RW_MAX_LEN];
	uint8_t Original_speed[DATA_LEN_4] = { 0U };
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

	page_prog_start = start_addr;
	while (page_prog_start < (start_addr + length)) {
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
		(void)memset(tmp_data, 0x00U, sizeof(tmp_data));
		tmp_data[3] = (uint8_t)(page_prog_start >> 24U);
		tmp_data[2] = (uint8_t)(page_prog_start >> 16U);
		tmp_data[1] = (uint8_t)(page_prog_start >> 8U);
		tmp_data[0] = (uint8_t)page_prog_start;
		himax_mcu_register_write(addr_spi200_addr, DATA_LEN_4,
					 tmp_data);

		(void)memset(buring_data, 0x00U, sizeof(buring_data));
		himax_parse_assign_cmd(addr_spi200_data, buring_data,
				       ADDR_LEN_4);

		/*Write First 16 Bytes*/
		(void)memcpy(&buring_data[ADDR_LEN_4],
		       &FW_content[page_prog_start - start_addr], 16U);
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
		page_prog_start += FLASH_RW_MAX_LEN;
	}
	/* ===Set Flash Speed===*/
	himax_mcu_register_write(addr_spi200_flash_speed, DATA_LEN_4,
				 Original_speed);
	return ret;
}

uint32_t hx8530_mcu_check_CRC(uint8_t *start_addr, unsigned int reload_length)
{
	uint32_t result = 0U;
	uint8_t tmp_data[DATA_LEN_4] = { 0U };
	uint8_t counter = 0U;
	unsigned int length = reload_length / DATA_LEN_4;

	tmp_data[0] = 0xA5U;
	himax_mcu_register_write(HX8530_ADDR_RETRY_RECODE, 4, tmp_data);

	himax_mcu_register_write(HX8530_ADDR_RELOAD_ADDR_FROM, DATA_LEN_4,
					start_addr);

	tmp_data[3] = 0x00U;
	tmp_data[2] = 0x99U;
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

			result = (((uint32_t)tmp_data[3] << 24U)
					+ ((uint32_t)tmp_data[2] << 16U)
					+ ((uint32_t)tmp_data[1] << 8U)
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
	uint8_t start_addr[DATA_LEN_4] = { 0U };
	uint32_t HX8530_ALG_2_SECTION_ADDR;
	uint32_t HX8530_ALG_2_SECTION_SIZE;
	uint32_t HX8530_CFG_1_SECTION_ADDR;
	uint32_t HX8530_CFG_1_SECTION_SIZE;
	uint8_t counter = 0U;

	HX8530_CFG_1_SECTION_ADDR = 0x00000000U;
	HX8530_CFG_1_SECTION_SIZE = HX8K;

	for (counter = 0U; counter < 5U; counter++) {
		himax_parse_assign_cmd((uint32_t)HX8530_CFG_1_SECTION_ADDR,
			start_addr, sizeof(start_addr));
		if (hx8530_mcu_check_CRC(start_addr,
			(unsigned int)HX8530_CFG_1_SECTION_SIZE) != 0x00000000U) {
			ic_data->HX8530_upgrade_section |= 0xF0U;
			E("[CFG 8K Section] CRC Fail, retry=%d\n", counter);
		} else {
			ic_data->HX8530_upgrade_section &= 0x0FU;
			I("[CFG 8K Section] CRC Pass\n");
			break;
		}
	}
	msleep(50);

	for (counter = 0U; counter < 5U; counter++) {
		HX8530_ALG_2_SECTION_ADDR = HX8K;
		HX8530_ALG_2_SECTION_SIZE = HX247K;
		himax_parse_assign_cmd((uint32_t)HX8530_ALG_2_SECTION_ADDR,
			start_addr, sizeof(start_addr));
		if (hx8530_mcu_check_CRC(start_addr,
			(unsigned int)HX8530_ALG_2_SECTION_SIZE) != 0x00000000U) {
			ic_data->HX8530_upgrade_section |= 0x0FU;
			E("[ALG 247K Section] CRC Fail, retry=%d\n", counter);
		} else {
			ic_data->HX8530_upgrade_section &= 0xF0U;
			I("[ALG 247K Section] CRC Pass\n");
			break;
		}
	}
	msleep(50);
	return (ic_data->HX8530_upgrade_section == 0U) ? true : false;
}

void hx8530_mcu_touch_information(void)
{
	uint8_t data[DATA_LEN_8] = { 0U };
	uint8_t IC_index = 0U;
	uint8_t i = 0U;

	I("%s Enter\n", __func__);

	hx8530_set_DSRAM_data_func();

#if (HX_FIX_TOUCH_INFO == 0x00)

	if (private_ts->LTDI_product == 1U) {
		himax_mcu_register_read(
			addr_fw_define_chip_rx_tx_num, DATA_LEN_4, data);
		ic_data->HX_CHIP_RX_MAX = data[2];
		ic_data->HX_CHIP_TX_MAX = data[3];
	} else {
		himax_mcu_register_read(
			addr_fw_define_chip_rx_tx_num, DATA_LEN_4, data);
		ic_data->HX_RX_NUM = data[2];
		ic_data->HX_TX_NUM = data[3];
	}

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

	himax_mcu_register_read(
		addr_fw_define_xy_res, DATA_LEN_4, data);
	ic_data->HX_Y_RES = ((uint16_t)data[2] << 8U);
	ic_data->HX_Y_RES += (uint16_t)data[3];
	ic_data->HX_X_RES = ((uint16_t)data[0] << 8U);
	ic_data->HX_X_RES += (uint16_t)data[1];


	if (private_ts->LTDI_product == 1U) {

		himax_mcu_register_read(
			addr_fw_define_total_rx_tx_num, DATA_LEN_4, data);
		ic_data->HX8530_TOTAL_RX_NUM = (data[1] << 8) | data[0];
		ic_data->HX8530_TOTAL_TX_NUM = (data[3] << 8) | data[2];

		himax_mcu_register_read(addr_fw_8530_total_TX_RX_IC_NUM, DATA_LEN_4, data);
		ic_data->HX_RX_IC_NUM = data[2];
		if (ic_data->HX_RX_IC_NUM == 0U) {
			private_ts->slave_ic_num = 1U;
			ic_data->HX_RX_IC_NUM = 1U;
		} else {
			private_ts->slave_ic_num = ic_data->HX_RX_IC_NUM;
		}

	} else {
		ic_data->HX_RX_IC_NUM = 1;
	}



#elif (HX_FIX_TOUCH_INFO == 0x01)
	ic_data->HX_RX_NUM = g_fix_info->FIX_HX_RX_NUM;
	ic_data->HX_TX_NUM = g_fix_info->FIX_HX_TX_NUM;
	ic_data->HX_MAX_PT = g_fix_info->FIX_HX_MAX_PT;
	ic_data->HX_INT_IS_EDGE = g_fix_info->FIX_HX_INT_IS_EDGE;
	ic_data->HX_Y_RES = private_ts->pdata->screenHeight;
	ic_data->HX_X_RES = private_ts->pdata->screenWidth;
	ic_data->HX_IS_ID_EN = g_fix_info->FIX_HX_IS_ID_EN;
	ic_data->HX_ID_PALM_EN = g_fix_info->FIX_HX_ID_PALM_EN;
	ic_data->HX_CHIP_RX_MAX = g_fix_info->FIX_HX_CHIP_RX_MAX;
	ic_data->HX_CHIP_TX_MAX = g_fix_info->FIX_HX_CHIP_TX_MAX;

	ic_data->HX8530_TOTAL_RX_NUM = g_fix_info->FIX_HX_RX_NUM;
	ic_data->HX8530_TOTAL_TX_NUM = g_fix_info->FIX_HX_TX_NUM;
	ic_data->HX_RX_IC_NUM = g_fix_info->FIX_HX_RX_IC_NUM;

#endif

	if (private_ts->LTDI_product == 1U) {
		himax_mcu_register_read(
			addr_HX8530_fw_distributed_bit, DATA_LEN_4, data);

		if (((data[3] & 0x01) == 0x01)) {
			ic_data->HX8530_is_distributed = 1U;
		} else {
			ic_data->HX8530_is_distributed = 0U;
		}

		if ((data[3] & 0x02) == 0x02) {
			ic_data->HX8530_IC_order = 1U;
		} else {
			ic_data->HX8530_IC_order = 0U;
		}

		himax_mcu_register_read(
			addr_fw_define_chip_rx_tx_num, DATA_LEN_4, data);
		ic_data->HX8530_IC_RX_NUM[0] = data[2];
		ic_data->HX8530_IC_TX_NUM[0] = data[3];
		himax_mcu_register_read(
			addr_HX8530_Slave12_tx_rx_Addr, DATA_LEN_4, data);
		ic_data->HX8530_IC_RX_NUM[1] = data[0];
		ic_data->HX8530_IC_TX_NUM[1] = data[1];
		ic_data->HX8530_IC_RX_NUM[2] = data[2];
		ic_data->HX8530_IC_TX_NUM[2] = data[3];
		himax_mcu_register_read(
			addr_HX8530_Slave34_tx_rx_Addr, DATA_LEN_4, data);
		ic_data->HX8530_IC_RX_NUM[3] = data[0];
		ic_data->HX8530_IC_TX_NUM[3] = data[1];
		ic_data->HX8530_IC_RX_NUM[4] = data[2];
		ic_data->HX8530_IC_TX_NUM[4] = data[3];
		himax_mcu_register_read(
			addr_HX8530_Slave56_tx_rx_Addr, DATA_LEN_4, data);
		ic_data->HX8530_IC_RX_NUM[5] = data[0];
		ic_data->HX8530_IC_TX_NUM[5] = data[1];
		ic_data->HX8530_IC_RX_NUM[6] = data[2];
		ic_data->HX8530_IC_TX_NUM[6] = data[3];
		himax_mcu_register_read(
			addr_HX8530_Slave7_tx_rx_Addr, DATA_LEN_4, data);
		ic_data->HX8530_IC_RX_NUM[7] = data[0];
		ic_data->HX8530_IC_TX_NUM[7] = data[1];
	
		for (IC_index = 0; IC_index < ic_data->HX_RX_IC_NUM; IC_index++) {
			if (ic_data->HX8530_MAX_TX_NUM < ic_data->HX8530_IC_TX_NUM[IC_index]) {
				ic_data->HX8530_MAX_TX_NUM = ic_data->HX8530_IC_TX_NUM[IC_index];
			}
			if (ic_data->HX8530_MAX_RX_NUM < ic_data->HX8530_IC_RX_NUM[IC_index]) {
				ic_data->HX8530_MAX_RX_NUM = ic_data->HX8530_IC_RX_NUM[IC_index];
			}
		}
	} else {
		ic_data->HX8530_IC_order = 0U;
		ic_data->HX8530_is_distributed = 0U;
		ic_data->HX8530_MAX_TX_NUM = 0;
		ic_data->HX8530_MAX_RX_NUM = 0;
	}

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
	I("%s:HX_RX_IC_NUM =%d\n", __func__,
		ic_data->HX_RX_IC_NUM);
	if (private_ts->LTDI_product == 1U) {
		I("%s:HX_CHIP_RX_MAX =%d,HX_CHIP_TX_MAX =%d\n", __func__,
			ic_data->HX_CHIP_RX_MAX,
			ic_data->HX_CHIP_TX_MAX);
		I("%s:HX8530_TOTAL_RX_NUM =%d,HX8530_TOTAL_TX_NUM =%d\n", __func__,
			ic_data->HX8530_TOTAL_RX_NUM,
			ic_data->HX8530_TOTAL_TX_NUM);
		for(i=0; i<8; i++) {
			I("%s:[IC,%d] HX8530_TOTAL_RX_NUM =%d,HX8530_TOTAL_TX_NUM =%d\n", __func__,
				i,
				ic_data->HX8530_IC_RX_NUM[i],
				ic_data->HX8530_IC_TX_NUM[i]);
		}
		I("%s:HX8530_MAX_RX_NUM =%d,HX8530_MAX_TX_NUM =%d\n", __func__,
			ic_data->HX8530_MAX_RX_NUM,
			ic_data->HX8530_MAX_TX_NUM);
		I("%s:HX8530_is_distributed =%d\n", __func__,
			ic_data->HX8530_is_distributed);
		I("%s:HX8530_IC_order =%d\n", __func__,
			ic_data->HX8530_IC_order);
	}
}

bool hx8530_mcu_get_DSRAM_data(uint8_t *tmp_rawdata)
{
	uint32_t i = 0;
	uint32_t j = 0;
	uint32_t k = 1;
	int32_t IC_index = 0;
	unsigned char tmp_data[DATA_LEN_4];
	uint32_t max_i2c_size = MAX_I2C_TRANS_SZ;
	uint8_t chip_id_sel = 0;
	uint8_t total_ic_num = ic_data->HX_RX_IC_NUM;
	uint8_t chip_rx_num = 0;
	uint8_t chip_tx_num = 0;
	uint16_t x_num = ic_data->HX8530_MAX_RX_NUM;
	uint16_t y_num = ic_data->HX8530_MAX_TX_NUM;
	uint16_t chip_frame_size = ic_data->HX8530_MAX_RX_NUM * ic_data->HX8530_MAX_TX_NUM * 2;
	unsigned int rawdata_index = 0;
	unsigned int ptr_index = 0;
	uint32_t x_num_times_y_num = (uint32_t)x_num * (uint32_t)y_num;
	uint32_t x_num_plus_y_num = (uint32_t)x_num + (uint32_t)y_num + (uint32_t)y_num;
	uint32_t total_size = (uint32_t)((x_num_times_y_num + x_num_plus_y_num) * 2U) + 4U;
	uint32_t total_size_tmp = total_size;
	uint8_t *rawdata_buffer = NULL;
	uint8_t **mutual_buffer = NULL;
	uint8_t **self_RX_buffer = NULL;
	uint8_t **self_TX_buffer = NULL;
	uint16_t check_sum_cal = 0;
	uint8_t retry = 0;
	uint16_t tmp = 0;


	rawdata_buffer = kcalloc((total_size + 8U), sizeof(uint8_t), GFP_KERNEL);
	if (rawdata_buffer == NULL) {
		E("%s, Failed to allocate memory\n", __func__);
		return false;
	}

	mutual_buffer = (uint8_t **)kcalloc(total_ic_num, sizeof(uint8_t *), GFP_KERNEL);
	if (mutual_buffer == NULL) {
		E("%s, Failed to allocate memory\n", __func__);
		return false;
	}
	for (i = 0; i < total_ic_num; i++) {
		mutual_buffer[i] = (uint8_t *)kcalloc(chip_frame_size , sizeof(uint8_t), GFP_KERNEL);
		if (mutual_buffer[i] == NULL) {
			E("%s, Failed to allocate memory\n", __func__);
			for (j = 0; j < i; j++) {
				kfree(mutual_buffer[j]);
			}
			kfree(mutual_buffer);
			return false;
		}
	}

	self_RX_buffer = (uint8_t **)kcalloc(total_ic_num, sizeof(uint8_t *), GFP_KERNEL);
	if (self_RX_buffer == NULL) {
		E("%s, Failed to allocate memory\n", __func__);
		return false;
	}
	for (i = 0; i < total_ic_num; i++) {
		self_RX_buffer[i] = (uint8_t *)kcalloc((ic_data->HX8530_MAX_RX_NUM * 2) , sizeof(uint8_t), GFP_KERNEL);
		if (self_RX_buffer[i] == NULL) {
			E("%s, Failed to allocate memory\n", __func__);
			for (j = 0; j < i; j++) {
				kfree(self_RX_buffer[j]);
			}
			kfree(self_RX_buffer);
			return false;
		}
	}

	self_TX_buffer = (uint8_t **)kcalloc(total_ic_num, sizeof(uint8_t *), GFP_KERNEL);
	if (self_TX_buffer == NULL) {
		E("%s, Failed to allocate memory\n", __func__);
		return false;
	}
	for (i = 0; i < total_ic_num; i++) {
		self_TX_buffer[i] = kcalloc((ic_data->HX8530_MAX_TX_NUM * 2) , sizeof(uint8_t), GFP_KERNEL);
		if (self_TX_buffer[i] == NULL) {
			E("%s, Failed to allocate memory\n", __func__);
			for (j = 0; j < i; j++) {
				kfree(self_TX_buffer[j]);
			}
			kfree(self_TX_buffer);
			return false;
		}
	}

	for (chip_id_sel = 0; chip_id_sel < total_ic_num; chip_id_sel++) {
		for (retry = 0; retry < 3; retry++) {
			(void)memset(rawdata_buffer, 0x00U, total_size_tmp * sizeof(uint8_t));
			
			k=0;
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

			chip_frame_size = ic_data->HX8530_IC_RX_NUM[chip_id_sel] * ic_data->HX8530_IC_TX_NUM[chip_id_sel] * 2U;
			x_num = ic_data->HX8530_IC_RX_NUM[chip_id_sel];
			y_num = ic_data->HX8530_IC_TX_NUM[chip_id_sel];
			total_size = ic_data->HX8530_IC_RX_NUM[chip_id_sel] * ic_data->HX8530_IC_TX_NUM[chip_id_sel];
			total_size += ic_data->HX8530_IC_RX_NUM[chip_id_sel] + ic_data->HX8530_IC_TX_NUM[chip_id_sel];
			total_size += ic_data->HX8530_IC_TX_NUM[chip_id_sel];
			total_size = (total_size * 2U) + 4U;

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
				/*if (chip_id_sel != rawdata_buffer[total_size - 17U]) {
					E("%s chip_id_sel not match FW = %d, go retry\n", __func__,
						rawdata_buffer[total_size - 17U]);
					if (retry == 2) {
						E("%s reach retry limit, goto fail \n", __func__);
						goto FAIL_Lable;
					}
					continue;
				}*/
				/*I("raw_out_sel in FW = %d\n", rawdata_buffer[total_size - 18]);*/
				chip_rx_num = rawdata_buffer[total_size - 15U];
				if (chip_rx_num != ic_data->HX8530_IC_RX_NUM[chip_id_sel]) {
					W("chip_rx_num not match FW = %d\n", chip_rx_num);
					W("HX_CHIP_RX_MAX not match FW = %d\n", ic_data->HX8530_IC_RX_NUM[chip_id_sel]);
				}
				chip_tx_num = rawdata_buffer[total_size - 16U];
				if (chip_tx_num != ic_data->HX8530_IC_TX_NUM[chip_id_sel]) {
					W("chip_tx_num not match FW = %d\n", chip_tx_num);
					W("HX_CHIP_TX_MAX not match FW = %d\n", ic_data->HX8530_IC_TX_NUM[chip_id_sel]);
				}

				(void)memcpy(&mutual_buffer[chip_id_sel][0], &rawdata_buffer[4],
					chip_frame_size * sizeof(uint8_t));
				(void)memcpy(&self_RX_buffer[chip_id_sel][0], &rawdata_buffer[4 + chip_frame_size],
					(x_num * 2) * sizeof(uint8_t));
				(void)memcpy(&self_TX_buffer[chip_id_sel][0], &rawdata_buffer[4 + chip_frame_size + (x_num * 2)],
					(y_num * 2) * sizeof(uint8_t));

			}
			break;

		}
	}

	x_num = ic_data->HX8530_MAX_RX_NUM;
	y_num = ic_data->HX8530_MAX_TX_NUM;

	if (ic_data->HX8530_IC_order == 0) {
		for (i = 0; i < y_num ; i++) {
			for (IC_index = 0; IC_index < total_ic_num; IC_index++) {
				ptr_index = (i * (ic_data->HX8530_IC_RX_NUM[IC_index] * 2));
				for (j = 0; j < (x_num * 2); j++) {
					if (j > (ic_data->HX8530_IC_RX_NUM[IC_index] * 2 - 1)) {
						tmp_rawdata[rawdata_index] = 0;
					} else if (i > (ic_data->HX8530_IC_TX_NUM[IC_index] - 1)) {
						tmp_rawdata[rawdata_index] = 0;
					} else {
						tmp_rawdata[rawdata_index] = mutual_buffer[IC_index][ptr_index];
					}
					ptr_index++;
					rawdata_index++;
				}
			}
		}
	} else {
		for (i = 0; i < y_num ; i++) {
			for (IC_index = (total_ic_num - 1); IC_index >= 0; IC_index--) {
				ptr_index = (i * (ic_data->HX8530_IC_RX_NUM[IC_index] * 2));
				for (j = 0; j < (x_num * 2); j++) {
					if (j > (ic_data->HX8530_IC_RX_NUM[IC_index] * 2 - 1)) {
						if (ic_data->HX8530_IC_RX_NUM[IC_index] < x_num) {
							tmp_rawdata[rawdata_index] = 0;
						} else {
							tmp_rawdata[rawdata_index] = mutual_buffer[IC_index][ptr_index];
							ptr_index++;
						}
					} else if (i > (ic_data->HX8530_IC_TX_NUM[IC_index] - 1)) {
						if (ic_data->HX8530_IC_TX_NUM[IC_index] < y_num) {
							tmp_rawdata[rawdata_index] = 0;
						} else {
							tmp_rawdata[rawdata_index] = mutual_buffer[IC_index][ptr_index];
							ptr_index++;
						}
					} else {
						tmp_rawdata[rawdata_index] = mutual_buffer[IC_index][ptr_index];
						ptr_index++;
					}
					rawdata_index++;
				}
			}
		}
	}

	if (ic_data->HX8530_IC_order == 0) {
		for (IC_index = 0; IC_index < total_ic_num; IC_index++) {
			for (j = 0; j < (x_num * 2); j++) {
				if (j > (ic_data->HX8530_IC_RX_NUM[IC_index] * 2)) {
					tmp_rawdata[rawdata_index] = 0;
				} else {
					tmp_rawdata[rawdata_index] = self_RX_buffer[IC_index][j];
				}
				rawdata_index++;
			}
		}
	} else {
		for (IC_index = (total_ic_num - 1); IC_index >= 0; IC_index--) {
			for (j = 0; j < (x_num * 2); j++) {
				if (j > (ic_data->HX8530_IC_RX_NUM[IC_index] * 2)) {
					tmp_rawdata[rawdata_index] = 0;
				} else {
					tmp_rawdata[rawdata_index] = self_RX_buffer[IC_index][j];
				}
				rawdata_index++;
			}
		}
	}

	if (ic_data->HX8530_IC_order == 0) {
		for (IC_index = 0; IC_index < total_ic_num; IC_index++) {
			for (j = 0; j < (y_num * 2); j++) {
				if (j > (ic_data->HX8530_IC_TX_NUM[IC_index] * 2)) {
					tmp_rawdata[rawdata_index] = 0;
				} else {
					tmp_rawdata[rawdata_index] = self_TX_buffer[IC_index][j];
				}
				rawdata_index++;
			}
		}
	} else {
		for (IC_index = (total_ic_num - 1); IC_index >= 0; IC_index--) {
			for (j = 0; j < (y_num * 2); j++) {
				if (j > (ic_data->HX8530_IC_TX_NUM[IC_index] * 2)) {
					tmp_rawdata[rawdata_index] = 0;
				} else {
					tmp_rawdata[rawdata_index] = self_TX_buffer[IC_index][j];
				}
				rawdata_index++;
			}
		}
	}


	kfree(rawdata_buffer);
	rawdata_buffer = NULL;

	for (i = 0; i < total_ic_num; i++) {
		kfree(mutual_buffer[i]);
	}
	kfree(mutual_buffer);
	mutual_buffer = NULL;
	for (i = 0; i < total_ic_num; i++) {
		kfree(self_RX_buffer[i]);
	}
	kfree(self_RX_buffer);
	self_RX_buffer = NULL;
	for (i = 0; i < total_ic_num; i++) {
		kfree(self_TX_buffer[i]);
	}
	kfree(self_TX_buffer);
	self_TX_buffer = NULL;
	return true;
FAIL_Lable:
	for (i = 0; i < total_ic_num; i++) {
		kfree(mutual_buffer[i]);
	}
	kfree(mutual_buffer);
	mutual_buffer = NULL;
	for (i = 0; i < total_ic_num; i++) {
		kfree(self_RX_buffer[i]);
	}
	kfree(self_RX_buffer);
	self_RX_buffer = NULL;
	for (i = 0; i < total_ic_num; i++) {
		kfree(self_TX_buffer[i]);
	}
	kfree(self_TX_buffer);
	self_TX_buffer = NULL;
	return false;
}

bool hx8530_mcu_program_to_slave_flash(uint8_t input_byte)
{
	uint8_t tmp_data[DATA_LEN_4] = { 0U };
	uint32_t counter = 0U;
	bool 	ret = false;
	uint8_t data_record = 0U;
	uint8_t byte_47_40 = 0U;

#if (HX_BOOT_UPGRADE == 0x01)
	if (hxfw != NULL) {
		byte_47_40 = input_byte;
		if ((byte_47_40 & 0x08U) == 0x08U) {
			I("%s: hxfw check at 0x00000800U passed (bits 47:40 & 0x08 == 0x08)\n", __func__);
		} else {
			E("%s: hxfw check at 0x00000800U failed (bits 47:40 & 0x08 != 0x08)\n", __func__);
			ret = true;
			return ret;
		}
	} else {
		ret = true;
		return ret;
	}
#else
	byte_47_40 = input_byte;
	if ((byte_47_40 & 0x08U) == 0x08U) {
		I("%s: hxfw check at 0x00000800U passed (bits 47:40 & 0x08 == 0x08)\n", __func__);
	} else {
		E("%s: hxfw check at 0x00000800U failed (bits 47:40 & 0x08 != 0x08)\n", __func__);
		ret = true;
		return ret;
	}
#endif

	tmp_data[0] = 0x99U;
	tmp_data[1] = 0x99U;
	himax_mcu_register_write(addr_ctrl_fw, DATA_LEN_4, tmp_data);

	do {
		himax_mcu_register_read(addr_slave_flash_check, DATA_LEN_4, tmp_data);
		if (tmp_data[0] == 0x64U) {
			I("%s: Cascade IC Program pass!\n", __func__);
			ret = true;
			return ret;
		} else if (tmp_data[0] == 0xFFU) {
			E("%s: Cascade IC Program fail!\n", __func__);
			ret = false;
			return ret;
		} else if (tmp_data[0] == data_record) {
			counter++;
		} else {
			counter = 0U;
			data_record = tmp_data[0];
		}
		usleep_range(1000, 1100);
	} while (counter < 5000U);

	E("%s: Cascade IC Program timeout fail!\n", __func__);
	return ret;
}

bool hx8530_mcu_fts_ctpm_fw_upgrade(const u8 *fw_data, unsigned int bin_size)
{
	uint8_t counter = 0U;
	struct time_var timeStart;
	struct time_var timeEnd;
	struct time_var timeDelta;
	uint32_t start_addr = 0U;
	uint32_t process_size = 0U;
	u8 *hybrid_fw = NULL;

	hybrid_fw = kzalloc(sizeof(uint8_t) * FW_SIZE_255k, GFP_KERNEL);
	if (hybrid_fw == NULL) {
		E("%s: Memory allocation falied!\n", __func__);
		return false;
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
		} else {
            E("%s: Undefined behavior\n", __func__);
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

void hx8530_set_DSRAM_data_func(void)
{
	if (private_ts->LTDI_product == 1U) {
		g_core_fp.fp_get_DSRAM_data = hx8530_mcu_get_DSRAM_data;
	} else {
		g_core_fp.fp_get_DSRAM_data = himax_mcu_get_DSRAM_data;
	}
}

void hx8530_distributed_arch_check(void)
{
	uint8_t tmp_data[DATA_LEN_4] = { 0 };

	I("%s Enter\n", __func__);

	himax_mcu_register_read(addr_HX8530_fw_distributed_bit, DATA_LEN_4, tmp_data);

	if (((tmp_data[3] & 0x01) == 0x01)) {
		ic_data->HX8530_is_distributed = 1U;
		I("%s Support Distributed Arch\n", __func__);
	} else {
		ic_data->HX8530_is_distributed = 0U;
		I("%s Not Support Distributed Arch\n", __func__);
	}
}

bool hx8530_cascaded_arch_check(void)
{
	uint8_t tmp_data[DATA_LEN_4] = { 0 };
	bool ret = false;

	I("%s Enter\n", __func__);

	himax_mcu_register_read(addr_HX8530_fw_cascaded_bit, DATA_LEN_4, tmp_data);

	
#if (HX_FIX_TOUCH_INFO == 0x01)
	if (((tmp_data[1] & 0x08) == 0x08) && (g_fix_info->FIX_HX_RX_IC_NUM > 1)) {
#else
	if (((tmp_data[1] & 0x08) == 0x08)) {
#endif
		private_ts->LTDI_product = 1U;
		ret = true;
		I("%s Support Cascaded Arch\n", __func__);
	} else {
		private_ts->LTDI_product = 0U;
		ret = false;
		I("%s Not Support Cascaded Arch\n", __func__);
	}

	return ret;
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

	g_core_fp.fp_burst_mode_enable = hx8530_mcu_burst_mode_enable;
	g_core_fp.fp_fts_ctpm_fw_upgrade = hx8530_mcu_fts_ctpm_fw_upgrade;

}

static bool HX8530_chip_detect(void)
{
	bool ret_data = false;
	uint8_t tmp_data[DATA_LEN_4] = { 0 };
	uint8_t i = 0U;
	int ret = 0;

	ret = himax_bus_read(addr_AHB_continous, tmp_data,
				 1U, HIMAX_I2C_RETRY_TIMES);
	if (ret < 0) {
		E("%s: bus access fail!\n", __func__);
	} else {

		hx8530_sense_off();

		for (i = 0U; i < 5U; i++) {
			himax_mcu_register_read(addr_icid_addr, DATA_LEN_4, tmp_data);
			I("%s:Read driver IC ID = HX%2X%2X%02X\n", __func__, tmp_data[3],
			tmp_data[2], tmp_data[1]);

			if ((tmp_data[3] == 0x85U) && (tmp_data[2] == 0x30U) &&
				((tmp_data[1] == 0x0AU) || (tmp_data[1] == 0x0BU) || (tmp_data[1] == 0x0CU))) {

				strscpy(private_ts->chip_name, HX_8530_PWON, 30);
				ic_data->HX_FW_SIZE = FW_SIZE_255k;
				private_ts->LTDI_product = 0U;
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
			I("[Welcome] to Automotive Project \n");
			hx8530_distributed_arch_check();
			hx8530_Automotive_Project_init();
		}
	}
	return ret_data;
}

bool hx8530_init(void)
{
	bool ret = false;

#if !defined(HIMAX_I2C_PLATFORM)
	private_ts->spi_id = 0xF2U;
    I("SPI ID: 0x%2X\n", private_ts->spi_id);
#endif

	I("%s\n", __func__);
	ret = HX8530_chip_detect();
	return ret;
}
