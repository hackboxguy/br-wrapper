// SPDX-License-Identifier: GPL-2.0
/*  Himax Android Driver Sample Code for self_test functions
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

#include "himax_self_test.h"
#include "himax_common.h"

/*------------------------- define block -------------------------------------*/
/*------------------------- define block -------------------------------------*/
/*------------------------- parameter block ----------------------------------*/

static uint32_t g_dc_max;
static unsigned int g_1kind_raw_size;
static int NOISEMAX;
static int g_recal_thx;
static int arraydata_max1;
static int arraydata_max2;
static int arraydata_max3;
static int arraydata_min1;
static int arraydata_min2;
static int arraydata_min3;

char *g_rslt_data;
static uint32_t g_rslt_data_len;
static int16_t **g_self_test_criteria;
static int *g_inspt_crtra_flag;
static bool *g_test_item_flag;
static unsigned int HX_CRITERIA_ITEM;
static unsigned int HX_CRITERIA_SIZE;
static bool file_w_flag;
static char *g_file_path;
static char *g_start_log;
static uint32_t block_num;
static uint32_t mutual_block_num;
static uint32_t self_block_num;

/*Need to map THP_self_test_ENUM*/
static char *g_himax_self_test_mode[] = { "HIMAX_OPEN",	"HIMAX_MICRO_OPEN",
				   "HIMAX_SHORT",	"HIMAX_WEIGHT_NOISE", "HIMAX_NOISE",
				   "HIMAX_RAWDATA",	"HIMAX_BPN_RAWDATA",
				   "HIMAX_SBP_RAWDATA",	"HIMAX_SC",
				   "HIMAX_SORTING",	"HIMAX_GAPTEST_RAW",
				   "HIMAX_BACK_NORMAL",	NULL };

/* for criteria */
static char *g_hx_inspt_crtra_name[] = {
	"CRITERIA_RAW_MIN",	"CRITERIA_RAW_MAX",
	"CRITERIA_RAW_BPN_MIN",	"CRITERIA_RAW_BPN_MAX",
	"CRITERIA_RAW_SBP_MIN",	"CRITERIA_RAW_SBP_MAX",
	"CRITERIA_SC_MIN",	"CRITERIA_SC_MAX",
	"CRITERIA_SC_GOLDEN",	"CRITERIA_SHORT_MIN",
	"CRITERIA_SHORT_MAX",	"CRITERIA_OPEN_MIN",
	"CRITERIA_OPEN_MAX",	"CRITERIA_MICRO_OPEN_MIN",
	"CRITERIA_MICRO_OPEN_MAX",	"CRITERIA_NOISE_WT_MIN",
	"CRITERIA_NOISE_WT_MAX",	"CRITERIA_NOISE_MIN",
	"CRITERIA_NOISE_MAX",	"CRITERIA_SORT_MIN",
	"CRITERIA_SORT_MAX",	NULL
};
#if defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX8530)
static char *default_item_and_criteria[] = {
	"CRITERIA_RAW_MIN:NULL",	"CRITERIA_RAW_MAX:NULL",
	"CRITERIA_RAW_BPN_MIN:NULL",	"CRITERIA_RAW_BPN_MAX:NULL",
	"CRITERIA_RAW_SBP_MIN:NULL",	"CRITERIA_RAW_SBP_MAX:NULL",
	"CRITERIA_SC_MIN:NULL",	"CRITERIA_SC_MAX:NULL",
	"CRITERIA_SC_GOLDEN:NULL",
	"CRITERIA_SHORT_MIN:0",	"CRITERIA_SHORT_MAX:100",
	"CRITERIA_OPEN_MIN:50",	"CRITERIA_OPEN_MAX:500",
	"CRITERIA_MICRO_OPEN_MIN:0",	"CRITERIA_MICRO_OPEN_MAX:100",
	"CRITERIA_NOISE_WT_MIN:NULL",	"CRITERIA_NOISE_WT_MAX:NULL",
	"CRITERIA_NOISE_MIN:NULL",	"CRITERIA_NOISE_MAX:NULL",
	"CRITERIA_SORT_MIN:NULL",	"CRITERIA_SORT_MAX:NULL",
	NULL
};
#else
static char *default_item_and_criteria[] = {
	"CRITERIA_RAW_MIN:NULL",	"CRITERIA_RAW_MAX:NULL",
	"CRITERIA_RAW_BPN_MIN:5",	"CRITERIA_RAW_BPN_MAX:65",
	"CRITERIA_RAW_SBP_MIN:20",	"CRITERIA_RAW_SBP_MAX:90",
	"CRITERIA_SC_MIN:NULL",	"CRITERIA_SC_MAX:NULL",
	"CRITERIA_SC_GOLDEN:NULL",
	"CRITERIA_SHORT_MIN:0",	"CRITERIA_SHORT_MAX:100",
	"CRITERIA_OPEN_MIN:50",	"CRITERIA_OPEN_MAX:500",
	"CRITERIA_MICRO_OPEN_MIN:0",	"CRITERIA_MICRO_OPEN_MAX:100",
	"CRITERIA_NOISE_WT_MIN:NULL",	"CRITERIA_NOISE_WT_MAX:NULL",
	"CRITERIA_NOISE_MIN:-200",	"CRITERIA_NOISE_MAX:255",
	"CRITERIA_SORT_MIN:NULL",	"CRITERIA_SORT_MAX:NULL",
	NULL
};
#endif
/*------------------------- parameter block ----------------------------------*/
/*------------------------- structure block ----------------------------------*/
/*------------------------- structure block ----------------------------------*/
static void himax_get_arraydata_edge(int16_t *RAW)
{
	int temp;
	int *ArrayData;
	uint32_t i;
	uint32_t j;
	uint32_t len = (uint32_t)ic_data->HX_RX_NUM
					* (uint32_t)ic_data->HX_TX_NUM;

	ArrayData = kcalloc(len, sizeof(int), GFP_KERNEL);
	if (ArrayData == NULL) {
		E("%s: allocate ArrayData failed\n", __func__);
		return;
	}

	for (i = 0; i < len; i++) {
		ArrayData[i] = RAW[i];
	}
	for (j = len - 1U; j > 0U; j--) { /*min to max*/
		for (i = 0; i < j; i++) {
			if (ArrayData[i] > ArrayData[i + 1U]) {
				temp = ArrayData[i];
				ArrayData[i] = ArrayData[i + 1U];
				ArrayData[i + 1U] = temp;
			}
		}
	}

	arraydata_min1 = ArrayData[0];
	arraydata_min2 = ArrayData[1];
	arraydata_min3 = ArrayData[2];
	arraydata_max1 = ArrayData[len - 3U];
	arraydata_max2 = ArrayData[len - 2U];
	arraydata_max3 = ArrayData[len - 1U];

	kfree(ArrayData);
	ArrayData = NULL;
}

static void hx_test_data_get(int16_t RAW[], char *start_log, char *result,
			    int now_item)
{
	int len = 0;
	char *testdata = NULL;
	int SZ_SIZE = (int) g_1kind_raw_size;
	uint32_t i;

	I("%s: Entering, Now type=%s!\n", __func__,
	  g_himax_self_test_mode[now_item]);

	testdata = kzalloc(sizeof(char) * g_1kind_raw_size, GFP_KERNEL);
	if (testdata == NULL) {
		E("%s: Memory allocation falied!\n", __func__);
		return;
	}

	len += snprintf(&testdata[len], SZ_SIZE - len, "%s", start_log);

	for (i = 0; i < block_num; i++) {
		if (i < mutual_block_num) {
			if ((i > 1U) && (((i + 1U) % (uint32_t)ic_data->HX_RX_NUM) == 0U)) {
				len += snprintf(&testdata[len], SZ_SIZE - len,
						"%5d,\n", RAW[i]);
			} else {
				len += snprintf(&testdata[len], SZ_SIZE - len, "%5d,",
						RAW[i]);
			}
		} else {
			if (i == mutual_block_num) {
				len += snprintf(&testdata[len], SZ_SIZE - len, "Self_RX:");
			} else if (i == (mutual_block_num + (uint32_t)ic_data->HX_RX_NUM)) {
				len += snprintf(&testdata[len], SZ_SIZE - len, "\nSelf_TX:");
			}  else {
				/* do nothing*/
			}
			len += snprintf(&testdata[len], SZ_SIZE - len, "%5d,",
						RAW[i]);
		}
	}

	len += snprintf(&testdata[len], SZ_SIZE - len, "\n%s", result);
	(void)memcpy(&g_rslt_data[0], testdata, len);
	g_rslt_data_len = (uint32_t)len;
	I("%s: g_rslt_data_len=%d!\n", __func__, g_rslt_data_len);

	/* dbg */
	/* for(i = 0; i < SZ_SIZE; i++)
	 * {
	 *	I("0x%04X, ", g_rslt_data[i + (now_item * SZ_SIZE)]);
	 *	if(i > 0 && (i % 16 == 15))
	 *		PI("\n");
	 * }
	 */

	kfree(testdata);
	testdata = NULL;

	return;
}

static void himax_switch_mode_self_test(uint8_t mode)
{
	uint8_t tmp_data[4] = { 0 };

	I("%s: Entering\n", __func__);

	/*Stop Handshaking*/
	himax_mcu_stop_DSRAM_output();

	/*Swtich Mode*/
	switch (mode) {
	case HX_SORTING:
		tmp_data[3] = 0x00;
		tmp_data[2] = 0x00;
		tmp_data[1] = PWD_SORTING_START;
		tmp_data[0] = PWD_SORTING_START;
		break;
	case HX_OPEN:
		tmp_data[3] = 0x00;
		tmp_data[2] = 0x00;
		tmp_data[1] = PWD_OPEN_START;
		tmp_data[0] = PWD_OPEN_START;
		break;
	case HX_MICRO_OPEN:
		tmp_data[3] = 0x00;
		tmp_data[2] = 0x00;
		tmp_data[1] = PWD_MICRO_OPEN_START;
		tmp_data[0] = PWD_MICRO_OPEN_START;
		break;
	case HX_SHORT:
		tmp_data[3] = 0x00;
		tmp_data[2] = 0x00;
		tmp_data[1] = PWD_SHORT_START;
		tmp_data[0] = PWD_SHORT_START;
		break;

	case HX_GAPTEST_RAW:
	case HX_RAWDATA:
	case HX_BPN_RAWDATA:
	case HX_SBP_RAWDATA:
	case HX_SC:
		tmp_data[3] = 0x00;
		tmp_data[2] = 0x00;
		tmp_data[1] = PWD_RAWDATA_START;
		tmp_data[0] = PWD_RAWDATA_START;
		break;

	case HX_WT_NOISE:
	case HX_NOISE:
		tmp_data[3] = 0x00;
		tmp_data[2] = 0x00;
		tmp_data[1] = PWD_NOISE_START;
		tmp_data[0] = PWD_NOISE_START;
		break;

	default:
		I("%s,Nothing to be done!\n", __func__);
		break;
	}

	himax_mcu_assign_sorting_mode(tmp_data);
	I("%s: End of setting!\n", __func__);

}

static uint32_t himax_get_rawdata(int16_t RAW[], uint32_t datalen)
{
	uint8_t *tmp_rawdata;
	bool get_raw_rlst;
	uint8_t retry = 0;
	uint16_t tmp_val = 0;
	uint32_t i = 0;
	uint32_t j = 0;
	uint32_t index = 0;
	int32_t Min_DATA = 0x7FFFFFFF;
	int32_t Max_DATA = 0x00000000;

	/* We use two bytes to combine a value of rawdata.*/
	tmp_rawdata = kzalloc(sizeof(uint8_t) * (datalen * 2U), GFP_KERNEL);
	if (tmp_rawdata == NULL) {
		E("%s: Memory allocation falied!\n", __func__);
		return HX_INSP_MEMALLCTFAIL;
	}

	while (retry < 200U) {
		get_raw_rlst = g_core_fp.fp_get_DSRAM_data(tmp_rawdata);
		if (get_raw_rlst) {
			break;
		}
		retry += 1U;
	}

	if (retry >= 200U) {
		goto DIRECT_END;
	}

	/* Copy Data*/
	for (i = 0; i < block_num; i++) {
		tmp_val = ((uint16_t)tmp_rawdata[(i * 2U) + 1U] * 256U)
					+ (uint16_t)tmp_rawdata[i * 2U];
		RAW[i + index] = (int16_t)tmp_val;
	}

	if ((private_ts->debug_log_level & BIT(3)) != 0U) {
		for (j = 0; j < ic_data->HX_RX_NUM; j++) {
			if (j == 0U) {
				I("      RX%2d", j + 1U);
			} else {
				PI("  RX%2d", j + 1U);
			}
		}
		I("\n");

		for (i = 0; i < (uint32_t)ic_data->HX_TX_NUM; i++) {
			if ((private_ts->debug_log_level & BIT(3)) != 0U) {
				I("TX%2d", i + 1U);
			}
			for (j = 0; j < (uint32_t)ic_data->HX_RX_NUM; j++) {
				if ((private_ts->debug_log_level & BIT(3)) != 0U) {
					PI("%5d ", RAW[index]);
				}
				if (RAW[index] > Max_DATA) {
					Max_DATA = RAW[index];
				}
				if (RAW[index] < Min_DATA) {
					Min_DATA = RAW[index];
				}

				index++;
			}
			if ((private_ts->debug_log_level & BIT(3)) != 0U) {
				PI("\n");
			}
		}
		I("Max = %5d, Min = %5d\n", Max_DATA, Min_DATA);
	}

DIRECT_END:
	kfree(tmp_rawdata);
	tmp_rawdata = NULL;

	if (get_raw_rlst) {
		return HX_INSP_OK;
	} else {
		return HX_INSP_EGETRAW;
	}
}

static void himax_switch_data_type(uint8_t checktype)
{
	uint8_t datatype = 0x00;

	switch (checktype) {
	case HX_SORTING:
		datatype = DATA_SORTING;
		break;
	case HX_OPEN:
		datatype = DATA_OPEN;
		break;
	case HX_MICRO_OPEN:
		datatype = DATA_MICRO_OPEN;
		break;
	case HX_SHORT:
		datatype = DATA_SHORT;
		break;
	case HX_RAWDATA:
	case HX_BPN_RAWDATA:
	case HX_SC:
	case HX_GAPTEST_RAW:
		datatype = DATA_RAWDATA;
		break;
	case HX_SBP_RAWDATA:
		datatype = DATA_Self_test_RAWDATA;
		break;
	case HX_WT_NOISE:
	case HX_NOISE:
		datatype = DATA_NOISE;
		break;
	case HX_BACK_NORMAL:
		datatype = DATA_BACK_NORMAL;
		break;
	default:
		E("Wrong type=%d\n", checktype);
		break;
	}
	himax_mcu_diag_register_set(datatype, IC_MASTER);
}

static void himax_neg_noise_sup(uint8_t *data)
{
	uint8_t tmp_data[4];

	/*0x10007FD8 Check support negative value or not */
	himax_mcu_register_read(addr_neg_noise_sup, DATA_LEN_4, tmp_data);

	if ((tmp_data[3] & 0x04U) == 0x04U) {
		himax_parse_assign_cmd(data_neg_noise, tmp_data,
				       sizeof(tmp_data));
		data[2] = tmp_data[2];
		data[3] = tmp_data[3];
	} else {
		I("%s Not support negative noise\n", __func__);
	}
}

static void himax_get_noise_base(uint8_t checktype) /*Normal Threshold*/
{
	uint8_t tmp_data[4];

	switch (checktype) {
	case HX_WT_NOISE:
		/*normal : 0x1000708F, LPWUG:0x10007093*/
		himax_mcu_register_read(addr_normal_noise_thx, DATA_LEN_4,
					tmp_data);
		break;

	default:
		I("%s Not support type\n", __func__);
		break;
	}

	NOISEMAX = (int)tmp_data[3];

	himax_mcu_register_read(addr_recal_thx, DATA_LEN_4, tmp_data);
	g_recal_thx = (int)tmp_data[2]; /*0x10007092*/
	I("%s: NOISEMAX=%d, g_recal_thx = %d\n", __func__, NOISEMAX,
	  g_recal_thx);
}

static void himax_set_N_frame(uint16_t Nframe, uint8_t checktype)
{
	uint8_t tmp_data[4] = { 0 };

	/*IIR MAX - 0x10007294*/
	tmp_data[3] = 0x00;
	tmp_data[2] = 0x00;
	tmp_data[1] = (uint8_t)((Nframe & 0xFF00U) >> 8U);
	tmp_data[0] = (uint8_t)(Nframe & 0x00FFU);

	if (checktype == (uint8_t)HX_NOISE) {
		himax_neg_noise_sup(tmp_data);
	}

	himax_mcu_register_write(addr_set_frame_addr, 4, tmp_data);

	himax_set_BS_UDT_frame(checktype);

	if ((checktype == (uint8_t)HX_WT_NOISE) || (checktype == (uint8_t)HX_NOISE)) {
		himax_get_noise_base(checktype);
	}

}

static uint16_t himax_get_palm_num(void) /*Palm Number*/
{
	uint8_t tmp_data[4];
	uint16_t palm_num;

	himax_mcu_register_read(addr_palm_num, DATA_LEN_4, tmp_data);
	palm_num = tmp_data[3]; /*0x100070AB*/
	I("%s: palm_num = %d ", __func__, palm_num);

	return palm_num;
}

static int himax_get_noise_weight_test(uint8_t checktype)
{
	uint8_t tmp_data[4];
	uint16_t weight = 0;
	uint16_t value = 0;

	/*0x100072C8 weighting value*/
	himax_mcu_register_read(addr_weight_sup, DATA_LEN_4, tmp_data);
	if ((tmp_data[3] != (uint8_t)((addr_weight_sup >> 8U) & 0x000000FFU))
		|| (tmp_data[2] != (uint8_t)(addr_weight_sup & 0x000000FFU))) {
		return FW_NOT_READY;
	}

	value = ((uint16_t)tmp_data[1] << 8) | (uint16_t)tmp_data[0];
	I("%s: value = %d, %d, %d ", __func__, value, tmp_data[2], tmp_data[3]);

	switch (checktype) {
	case HX_WT_NOISE:
		/*Normal:0x1000709C, LPWUG:0x100070A0 weighting threshold*/
		himax_mcu_register_read(addr_normal_weight_a, DATA_LEN_4,
					tmp_data);
		break;
	default:
		I("%s Not support type\n", __func__);
		break;
	}

	weight = (uint16_t)tmp_data[0];

	himax_mcu_register_read(addr_weight_b, DATA_LEN_4, tmp_data);
	weight = (uint16_t)tmp_data[1] * (uint16_t)weight; /*0x10007095 weighting threshold*/
	I("%s: weight = %d ", __func__, weight);

	if (value > weight) {
		return ERR_TEST_FAIL;
	} else {
		return 0;
	}
}

static uint32_t himax_check_mode(uint8_t checktype)
{
	uint8_t tmp_data[4] = { 0 };
	uint8_t wait_pwd[2] = { 0 };

	switch (checktype) {
	case HX_SORTING:
		wait_pwd[0] = PWD_SORTING_END;
		wait_pwd[1] = PWD_SORTING_END;
		break;
	case HX_OPEN:
		wait_pwd[0] = PWD_OPEN_END;
		wait_pwd[1] = PWD_OPEN_END;
		break;
	case HX_MICRO_OPEN:
		wait_pwd[0] = PWD_MICRO_OPEN_END;
		wait_pwd[1] = PWD_MICRO_OPEN_END;
		break;
	case HX_SHORT:
		wait_pwd[0] = PWD_SHORT_END;
		wait_pwd[1] = PWD_SHORT_END;
		break;
	case HX_RAWDATA:
	case HX_BPN_RAWDATA:
	case HX_SBP_RAWDATA:
	case HX_SC:
	case HX_GAPTEST_RAW:
		wait_pwd[0] = PWD_RAWDATA_END;
		wait_pwd[1] = PWD_RAWDATA_END;
		break;

	case HX_WT_NOISE:
	case HX_NOISE:
		wait_pwd[0] = PWD_NOISE_END;
		wait_pwd[1] = PWD_NOISE_END;
		break;

	default:
		E("Wrong type=%d\n", checktype);
		break;
	}

	if ((checktype == (uint8_t)HX_NOISE) || (checktype == (uint8_t)HX_WT_NOISE)) {
		I("%s: NOISE check n frame\n", __func__);
		himax_mcu_check_N_frame(tmp_data);
		if (tmp_data[0] != NOISEFRAME) {
			tmp_data[0] = NOISEFRAME;
			himax_mcu_register_write(addr_set_frame_addr, DATA_LEN_4, tmp_data);
			return 1;
		}
	}

	himax_mcu_check_sorting_mode(tmp_data);

	if (wait_pwd[0] == tmp_data[0]) {
		if (wait_pwd[1] == tmp_data[1]) {
			I("%s: Already in %s mode\n", __func__,
				g_himax_self_test_mode[checktype]);
		}
		return 0;
	} else {
		return 1;
	}
}

static uint32_t himax_wait_sorting_mode(uint8_t checktype)
{
	uint8_t tmp_data[4] = { 0 };
	uint8_t wait_pwd[2] = { 0 };
	uint16_t count = 0;

	switch (checktype) {
	case HX_SORTING:
		wait_pwd[0] = PWD_SORTING_END;
		wait_pwd[1] = PWD_SORTING_END;
		break;
	case HX_OPEN:
		wait_pwd[0] = PWD_OPEN_END;
		wait_pwd[1] = PWD_OPEN_END;
		break;
	case HX_MICRO_OPEN:
		wait_pwd[0] = PWD_MICRO_OPEN_END;
		wait_pwd[1] = PWD_MICRO_OPEN_END;
		break;
	case HX_SHORT:
		wait_pwd[0] = PWD_SHORT_END;
		wait_pwd[1] = PWD_SHORT_END;
		break;
	case HX_RAWDATA:
	case HX_BPN_RAWDATA:
	case HX_SBP_RAWDATA:
	case HX_SC:
	case HX_GAPTEST_RAW:
		wait_pwd[0] = PWD_RAWDATA_END;
		wait_pwd[1] = PWD_RAWDATA_END;
		break;
	case HX_WT_NOISE:
	case HX_NOISE:
		wait_pwd[0] = PWD_NOISE_END;
		wait_pwd[1] = PWD_NOISE_END;
		break;
	default:
		I("No Change Mode and now type=%d\n", checktype);
		break;
	}

	do {
		himax_mcu_check_sorting_mode(tmp_data);
		if ((wait_pwd[0] == tmp_data[0]) &&
		    (wait_pwd[1] == tmp_data[1])) {
			I("Retry %d times!\n", count);
			return HX_INSP_OK;
		}
		if ((count % 10U) == 0U) {
			himax_mcu_register_read(addr_cs_central_state, DATA_LEN_4,
						tmp_data);
			I(TEMP_LOG, __func__, "0x900000A8", tmp_data[0], tmp_data[1],
			  tmp_data[2], tmp_data[3]);

			himax_mcu_register_read(addr_flag_reset_event, DATA_LEN_4,
						tmp_data);
			I(TEMP_LOG, __func__, "0x900000E4", tmp_data[0], tmp_data[1],
			  tmp_data[2], tmp_data[3]);

			himax_mcu_register_read(addr_fw_dbg_msg_addr, DATA_LEN_4,
						tmp_data);
			I(TEMP_LOG, __func__, "0x10007F40", tmp_data[0], tmp_data[1],
			  tmp_data[2], tmp_data[3]);
		}
		msleep(50);
		count += 1U;
	} while (count < 200U);

	return HX_INSP_ESWITCHMODE;
}

static void himax_data_compare(uint8_t checktype, int16_t *RAW,
				   uint32_t *ret_val)
{
	uint32_t i = 0;
	uint32_t j = 0;
	uint32_t index = 0;
	uint8_t idx_max = 0;
	uint8_t idx_min = 0;
	int16_t Min_DATA = 0x7FFF;
	int16_t Max_DATA = 0x0000;
	uint16_t palm_num = 0;
	uint16_t noise_count = 0;
	int16_t tmp_RAW = 0;
	int16_t tmp_criteria = 0;
	uint32_t tmp_BP = 0U;
	uint32_t shifted_value = 0U;

	switch (checktype) {
	case HX_SORTING:
		idx_min = IDX_SORTMIN;
		break;
	case HX_OPEN:
		idx_max = IDX_OPENMAX;
		idx_min = IDX_OPENMIN;
		break;
	case HX_MICRO_OPEN:
		idx_max = IDX_M_OPENMAX;
		idx_min = IDX_M_OPENMIN;
		break;
	case HX_SHORT:
		idx_max = IDX_SHORTMAX;
		idx_min = IDX_SHORTMIN;
		break;
	case HX_RAWDATA:
		idx_max = IDX_RAWMAX;
		idx_min = IDX_RAWMIN;
		break;
	case HX_BPN_RAWDATA:
		idx_max = IDX_BPN_RAWMAX;
		idx_min = IDX_BPN_RAWMIN;
		break;
	case HX_SBP_RAWDATA:
		idx_max = IDX_SBP_RAWMAX;
		idx_min = IDX_SBP_RAWMIN;
		break;
	case HX_SC:
		idx_max = IDX_SCMAX;
		idx_min = IDX_SCMIN;
		break;
	case HX_WT_NOISE:
		idx_max = IDX_WT_NOISEMAX;
		idx_min = IDX_WT_NOISEMIN;
		break;
	case HX_NOISE:
		idx_max = IDX_NOISEMAX;
		idx_min = IDX_NOISEMIN;
		break;
	case HX_GAPTEST_RAW:
		break;
	default:
		E("Wrong type=%d\n", checktype);
		break;
	}

	/*data process*/
	switch (checktype) {
	case HX_SORTING:
		for (i = 0; i < block_num; i++) {
			g_self_test_criteria[idx_max][i] = 0x7FFF;
		}
		break;
	case HX_BPN_RAWDATA:
	case HX_SBP_RAWDATA:
		for (i = 0; i < block_num; i++) {
			tmp_BP = (uint32_t)RAW[i];
			tmp_BP = (tmp_BP * 100U / g_dc_max);
			RAW[i] = (int16_t)tmp_BP;
		}
		break;
	case HX_SC:
		for (i = 0; i < block_num; i++) {
			tmp_RAW = RAW[i];
			tmp_criteria =
				g_self_test_criteria[IDX_SC_GOLDEN][i];
			RAW[i] = tmp_RAW - tmp_criteria;
			RAW[i] =  RAW[i] * 100 / tmp_criteria;
		}
		break;
	default:
		/*do nothing*/
		break;
	}

	/*data compare*/
	switch (checktype) {
	case HX_WT_NOISE:
		noise_count = 0;
		himax_get_noise_base(checktype);
		palm_num = himax_get_palm_num();
		for (i = 0; i < block_num; i++) {
			if (RAW[i] > NOISEMAX) {
				noise_count++;
			}
		}
		I("noise_count=%d\n", noise_count);
		if (noise_count > palm_num) {
			E("%s: noise test FAIL\n", __func__);
			shifted_value = (uint32_t)0x01U << ((uint32_t)checktype + ERR_SFT);
			*ret_val |= shifted_value;
			break;
		}
		(void)snprintf(g_start_log, 256U * sizeof(char), "\n Threshold = %d\n",
			NOISEMAX);
		/*Check weightingt*/
		if (himax_get_noise_weight_test(checktype) < 0) {
			I("%s: %s FAIL 0x%02X\n", __func__,
			g_himax_self_test_mode[checktype], *ret_val);
			shifted_value = (uint32_t)0x01U << ((uint32_t)checktype + ERR_SFT);
			*ret_val |= shifted_value;
			break;
		}
		/*Check negative side noise*/
		for (i = 0; i < block_num; i++) {
			if ((RAW[i] > (g_self_test_criteria[idx_max][i] *
					(NOISEMAX / 100))) ||
				(RAW[i] < (g_self_test_criteria[idx_min][i] *
					(g_recal_thx / 100)))
			) {
				E(FAIL_IN_INDEX, __func__,
				g_himax_self_test_mode[checktype], i);
				shifted_value = (uint32_t)0x01U << ((uint32_t)checktype + ERR_SFT);
				*ret_val |= shifted_value;
				break;
			}
		}
		break;
	case HX_SORTING:
	case HX_OPEN:
	case HX_MICRO_OPEN:
	case HX_SHORT:
	case HX_RAWDATA:
	case HX_BPN_RAWDATA:
	case HX_SBP_RAWDATA:
	case HX_SC:
	case HX_NOISE:
		for (i = 0; i < block_num; i++) {
			if ((RAW[i] > g_self_test_criteria[idx_max][i]) ||
				(RAW[i] < g_self_test_criteria[idx_min][i])) {
				E(FAIL_IN_INDEX, __func__,
				g_himax_self_test_mode[checktype], i);
				shifted_value = (uint32_t)0x01U << ((uint32_t)checktype + ERR_SFT);
				*ret_val |= shifted_value;
				break;
			}
		}
		break;
	default:
		E("Wrong type=%d\n", checktype);
		break;
	}
	if ((private_ts->debug_log_level & BIT(3)) != 0U) {
		for (j = 0; j < ic_data->HX_RX_NUM; j++) {
			if (j == 0U) {
				I("      RX%2d", j + 1U);
			} else {
				PI("  RX%2d", j + 1U);
			}
		}
		PI("\n");
	}
	if ((private_ts->debug_log_level & BIT(3)) != 0U) {
		for (i = 0; i < (uint32_t)ic_data->HX_TX_NUM; i++) {
			I("TX%2d", i + 1U);
			for (j = 0; j < (uint32_t)ic_data->HX_RX_NUM; j++) {
				if (RAW[index] > g_self_test_criteria[idx_max][index]) {
					PI("%5d>", RAW[index]);
				} else if (RAW[index] < g_self_test_criteria[idx_min][index]) {
					PI("%5d<", RAW[index]);
				} else {
					PI("%5d ", RAW[index]);
				}
				if (RAW[index] > Max_DATA) {
					Max_DATA = RAW[index];
				}
				if (RAW[index] < Min_DATA) {
					Min_DATA = RAW[index];
				}

				index++;
			}
			PI("\n");
		}
		I("Max = %5d, Min = %5d\n", Max_DATA, Min_DATA);
	}
	I("%s: %s %s\n", __func__, g_himax_self_test_mode[checktype],
	  (*ret_val == (uint32_t)HX_INSP_OK) ? "PASS" : "FAIL");

}

static uint32_t himax_get_max_dc(void)
{
	uint8_t tmp_data[DATA_LEN_4];
	uint32_t dc_max = 0;

	himax_mcu_register_read(addr_max_dc, DATA_LEN_4, tmp_data);
	/*I("%s: tmp_data[0]=0x%02X,tmp_data[1]=0x%02X\n", __func__, tmp_data[0],
	 * tmp_data[1]);
	 */

	dc_max = ((uint32_t)tmp_data[3] << 24U)
			+ ((uint32_t)tmp_data[2] << 16U)
			+ ((uint32_t)tmp_data[1] << 8U)
			+ (uint32_t)tmp_data[0];

	I("%s: dc max = %d\n", __func__, dc_max);
	return dc_max;
}

/*	 HX_GAP END*/
static uint32_t mpTestFunc(uint8_t checktype, uint32_t datalen,
			   struct seq_file *s)
{
	char g_rslt_log[256];
	size_t len = 0;
	int16_t *RAW = NULL;
	uint16_t n_frame = 0;
	uint32_t ret_val = HX_INSP_OK;
	struct time_var timeStart;
	struct time_var timeEnd;
	struct time_var timeDelta;

	I("%s, Check type = %d\n", __func__, checktype);
	time_func(&timeStart);
	RAW = kcalloc(datalen, sizeof(int16_t), GFP_KERNEL);

	if (RAW == NULL) {
		E("%s, Failed to allocate memory\n", __func__);
		return HX_INSP_MEMALLCTFAIL;
	}

	if (himax_check_mode(checktype) != 0U) {
		/*himax_check_mode(checktype);*/

		I("Need Change Mode ,target=%s\n",
		  g_himax_self_test_mode[checktype]);

		g_core_fp.fp_sense_off();

		himax_mcu_config_reload_disable();

		himax_switch_mode_self_test(checktype);

		switch (checktype) {
		case HX_WT_NOISE:
		case HX_NOISE:
			n_frame = (uint16_t)NOISEFRAME;
			break;
		default:
			n_frame = (uint16_t)OTHERSFRAME;
			break;
		}
		himax_set_N_frame(n_frame, checktype);
#if !defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83194)
		himax_mcu_rawdata_normalize_disable(1);
#endif
		g_core_fp.fp_sense_on();

	}
	ret_val |= himax_wait_sorting_mode(checktype);
	if (ret_val != 0U) {
		E("%s: himax_wait_sorting_mode FAIL\n", __func__);
		ret_val |= (1U << ((uint32_t)checktype + ERR_SFT));
		goto fail_wait_sorting_mode;
	}
	himax_switch_data_type(checktype);

	usleep_range(40000, 41000);

	ret_val |= himax_get_rawdata(RAW, datalen);
	if (ret_val != 0U) {
		E("%s: himax_get_rawdata FAIL\n", __func__);
		ret_val |= (1U << ((uint32_t)checktype + ERR_SFT));
		goto fail_get_rawdata;
	}
	time_func(&timeEnd);
	timeDelta = time_diff(timeStart, timeEnd);
#if defined(KERNEL_VER_5_10)
	seq_printf(s, "\t%s :  %lld.%ld s\n", g_himax_self_test_mode[checktype], timeDelta.tv_sec, timeDelta.tv_nsec);
#else
	seq_printf(s, "\t%s :  %ld.%ld s\n", g_himax_self_test_mode[checktype], timeDelta.tv_sec, timeDelta.tv_nsec);
#endif

	/*get Max DC from FW*/
	g_dc_max = himax_get_max_dc();

	/* back to normal */
	himax_switch_data_type(HX_BACK_NORMAL);

	len += (size_t)snprintf(&g_start_log[len], sizeof(g_rslt_log), "\n%s%s\n",
			g_himax_self_test_mode[checktype],
			": data as follow!\n");

	himax_data_compare(checktype, RAW, &ret_val);

	himax_get_arraydata_edge(RAW);

	len += (size_t)snprintf(&g_start_log[len], sizeof(g_rslt_log) - len,
			"\n arraydata_min1 = %d,", arraydata_min1);
	len += (size_t)snprintf(&g_start_log[len], sizeof(g_rslt_log) - len,
			"  arraydata_min2 = %d,", arraydata_min2);
	len += (size_t)snprintf(&g_start_log[len], sizeof(g_rslt_log) - len,
			"  arraydata_min3 = %d,", arraydata_min3);
	len += (size_t)snprintf(&g_start_log[len], sizeof(g_rslt_log) - len,
			"\n arraydata_max1 = %d,", arraydata_max1);
	len += (size_t)snprintf(&g_start_log[len], sizeof(g_rslt_log) - len,
			"  arraydata_max2 = %d,", arraydata_max2);
	len += (size_t)snprintf(&g_start_log[len], sizeof(g_rslt_log) - len,
			"  arraydata_max3 = %d\n", arraydata_max3);

	if (ret_val == HX_INSP_OK) { /*PASS*/
		len += (size_t)snprintf(g_rslt_log, sizeof(g_rslt_log), "\n%s%s\n",
			 g_himax_self_test_mode[checktype], ":Test Pass!");
	} else { /*FAIL*/
		len += (size_t)snprintf(g_rslt_log, sizeof(g_rslt_log), "\n%s%s\n",
			 g_himax_self_test_mode[checktype], ":Test Fail!");
	}

	hx_test_data_get(RAW, g_start_log, g_rslt_log, (int)checktype);
fail_get_rawdata:
fail_wait_sorting_mode:
	kfree(RAW);
	RAW = NULL;
	return ret_val;
}

/* get idx of criteria whe parsing file */
static int hx_find_crtra_id(char *input)
{
	unsigned int i = 0;
	int result = 0;

	for (i = 0; i < HX_CRITERIA_SIZE; i++) {
		if (strcmp(g_hx_inspt_crtra_name[i], input) == 0) {
			result = (int)i;
			I("find the str=%s,idx=%d\n", g_hx_inspt_crtra_name[i],
			  i);
			break;
		}
	}
	if (i > (HX_CRITERIA_SIZE - 1U)) {
		E("%s: find Fail!\n", __func__);
		result = LENGTH_FAIL;
	}

	return result;
}

static void hx_print_crtra_after_parsing(void)
{
	uint32_t i = 0;
	uint32_t j = 0;

	for (i = 0; i < HX_CRITERIA_SIZE; i++) {
		I("Now is %s\n", g_hx_inspt_crtra_name[i]);
		if (g_inspt_crtra_flag[i] == 1) {
			for (j = 0; j < block_num; j++) {
				I("%d, ", g_self_test_criteria[i][j]);
				if ((j % 16U) == 15U) {
					PI("\n");
				}
			}
		} else {
			I("No this Item in this criteria file!\n");
		}
		PI("\n");
	}

}

static uint32_t hx_criteria_collect(char *critera_str, uint16_t himax_count_type, int comprae_data)
{
	int result = 0;
	uint32_t ret = HX_INSP_OK;

	if (kstrtoint(critera_str, 10, &result) == 0) {
		g_self_test_criteria[himax_count_type][comprae_data] = (int16_t)result;
	} else {
		I("Failed to convert string '%s' to integer\n", critera_str);
		I("himax_count_type = %d, comprae_data index = %d \n", himax_count_type, comprae_data);
		ret = HX_INSP_EFILE;
		if ((private_ts->debug_log_level & BIT(4)) != 0U) {
			/* dbg:print all of criteria from parsing file */
			hx_print_crtra_after_parsing();
		} else {
			/*do nothing*/
		}
	}

	return ret;
}

static uint32_t hx_check_criteria_in_csv(const uint8_t *end_ptr, char *curr_byte, int crtra_id)
{
	uint32_t ret = HX_INSP_OK;
	int rx_count = 0;
	int tx_count = 0;
	size_t i = 0;
	char str_buff[8] = { 0 };
	size_t str_len = 0;
	uint8_t *curr_ptr;

	do {
		if (curr_byte[i] == '-') {
			str_len++;
		} else if (isdigit(curr_byte[i]) != 0) {
			str_len++;
		} else if (curr_byte[i] == ',') {
			(void)memset(str_buff, '\0', sizeof(str_buff));
			(void)memcpy(&str_buff[0], &curr_byte[i - str_len], str_len);
			if (str_len > 0U) {
				ret = hx_criteria_collect(str_buff, (uint16_t)crtra_id, rx_count);
			}
			rx_count++;
			str_len = 0;
		} else if (curr_byte[i] == '\n') {
			/* do nothing */
		} else if (curr_byte[i] == '\r') {
			(void)memset(str_buff, '\0', sizeof(str_buff));
			(void)memcpy(&str_buff[0], &curr_byte[i - str_len], str_len);
			if (str_len > 0U) {
				ret = hx_criteria_collect(str_buff, (uint16_t)crtra_id, rx_count);
			}
			tx_count++;
			str_len = 0;
		} else {
			I("%s, character: %c\n", __func__,
			  curr_byte[i]);
			break;
		}
		i++;
		curr_ptr = (uint8_t*)&curr_byte[i];
	} while (curr_ptr < end_ptr);

	rx_count++;
	if (tx_count != 0) {
		rx_count = (rx_count / tx_count);
	}
	if ((uint8_t)rx_count != ic_data->HX_RX_NUM) {
		E("%s,Parsing RX: %d, it should be %d\n", __func__,
		  rx_count, ic_data->HX_RX_NUM);
		ret = HX_INSP_EFILE;
	} else if ((uint8_t)tx_count != ic_data->HX_TX_NUM) {
		E("%s,Parsing TX: %d, it should be %d\n", __func__,
			  tx_count, ic_data->HX_TX_NUM);
		ret = HX_INSP_EFILE;
	} else {
		/* Parsing */
		I("%s:Parsed TX: %d, RX: %d\n", __func__, tx_count, rx_count);
	}

	return ret;
}

static uint32_t himax_parse_criteria_str(char *str_ptr, uint32_t hx_str_len,
				    const struct firmware *csv_file)
{
	uint32_t ret = HX_INSP_OK;
	char str_rslt[100] = { 0 };
	int crtra_id = 0;
	char *curr_ptr = NULL;
	const uint8_t *csv_end = csv_file->data + csv_file->size - 1U;
	const uint8_t *end_ptr = &csv_end[0];

	I("%s,Entering\n", __func__);

	(void)memcpy(&str_rslt[0], str_ptr, hx_str_len);

	crtra_id = hx_find_crtra_id(str_rslt);
	if (crtra_id == LENGTH_FAIL) {
		E("Please check criteria file again!\n");
		ret = HX_INSP_EFILE;
	} else {
		g_inspt_crtra_flag[crtra_id] = 1;
		curr_ptr = &str_ptr[hx_str_len + 1U];
		ret = hx_check_criteria_in_csv(end_ptr, curr_ptr, crtra_id);
	}
	return ret;
}

static uint32_t himax_parse_criteria(const struct firmware *csv_file)
{
	uint32_t ret = HX_INSP_OK;
	int i = 0;
	char *str_ptr = NULL;

	I("%s: enter, %d\n", __func__, __LINE__);

	while (g_hx_inspt_crtra_name[i] != NULL) {
		str_ptr = strnstr(csv_file->data, g_hx_inspt_crtra_name[i],
				    csv_file->size);
		if (str_ptr != NULL) {
			I("g_hx_inspt_crtra_name[%d] = %s\n", i,
			  g_hx_inspt_crtra_name[i]);
			ret |= himax_parse_criteria_str(str_ptr,
							(uint32_t)strlen(g_hx_inspt_crtra_name[i]),
							csv_file);
			if (ret >= HX_INSP_EFILE) {
				break;
			}
		}
		i++;
	}
	return ret;
}

static void himax_test_item_chk(void)
{
	unsigned int i = 0;

	for (i = 0; i < (HX_CRITERIA_ITEM - 1U); i++) {
		g_test_item_flag[i] = false;
	}

	g_test_item_flag[HX_OPEN] =
		((g_inspt_crtra_flag[IDX_OPENMIN] == 1) &&
		(g_inspt_crtra_flag[IDX_OPENMAX] == 1)) ?
		true : false;

	g_test_item_flag[HX_MICRO_OPEN] =
		((g_inspt_crtra_flag[IDX_M_OPENMIN] == 1) &&
		 (g_inspt_crtra_flag[IDX_M_OPENMAX] == 1)) ?
		true : false;

	g_test_item_flag[HX_SHORT] =
		((g_inspt_crtra_flag[IDX_SHORTMIN] == 1) &&
		(g_inspt_crtra_flag[IDX_SHORTMAX] == 1)) ?
		true : false;

	g_test_item_flag[HX_RAWDATA] =
		((g_inspt_crtra_flag[IDX_RAWMIN] == 1) &&
		(g_inspt_crtra_flag[IDX_RAWMAX] == 1)) ?
		true : false;

	g_test_item_flag[HX_BPN_RAWDATA] =
		((g_inspt_crtra_flag[IDX_BPN_RAWMIN] == 1) &&
		 (g_inspt_crtra_flag[IDX_BPN_RAWMAX] == 1)) ?
		true : false;

	g_test_item_flag[HX_SBP_RAWDATA] =
		((g_inspt_crtra_flag[IDX_SBP_RAWMIN] == 1) &&
		 (g_inspt_crtra_flag[IDX_SBP_RAWMAX] == 1)) ?
		true : false;

	g_test_item_flag[HX_SC] = ((g_inspt_crtra_flag[IDX_SCMIN] == 1) &&
		(g_inspt_crtra_flag[IDX_SCMAX] == 1) &&
		(g_inspt_crtra_flag[IDX_SC_GOLDEN] == 1)) ?
		true : false;

	g_test_item_flag[HX_WT_NOISE] =
		((g_inspt_crtra_flag[IDX_WT_NOISEMIN] == 1) &&
		 (g_inspt_crtra_flag[IDX_WT_NOISEMAX] == 1)) ?
		true : false;

	g_test_item_flag[HX_NOISE] =
		((g_inspt_crtra_flag[IDX_NOISEMIN] == 1) &&
		 (g_inspt_crtra_flag[IDX_NOISEMAX] == 1)) ?
		true : false;

	g_test_item_flag[HX_SORTING] =
		((g_inspt_crtra_flag[IDX_SORTMIN] == 1) &&
		(g_inspt_crtra_flag[IDX_SORTMAX] == 1)) ?
		true : false;

	g_test_item_flag[HX_GAPTEST_RAW] =
		((g_inspt_crtra_flag[IDX_GAP_HOR_RAWMAX] == 1) &&
		 (g_inspt_crtra_flag[IDX_GAP_HOR_RAWMIN] == 1) &&
		 (g_inspt_crtra_flag[IDX_GAP_VER_RAWMAX] == 1) &&
		 (g_inspt_crtra_flag[IDX_GAP_VER_RAWMIN] == 1)) ?
		true : false;

	for (i = 0; i < (HX_CRITERIA_ITEM - 1U); i++) {
		I("g_test_item_flag[%d] = %d\n", i, g_test_item_flag[i]);
	}
}

static unsigned int hx_get_size_str_arr(char **input)
{
	unsigned int i = 0;

	while (input[i] != NULL) {
		i++;
	}

	if ((private_ts->debug_log_level & BIT(4)) != 0U) {
		I("There is %d in [0]=%s\n", i, input[0]);
	}

	return i;
}

static void hx_print_ic_id(void)
{
	int len = 0;
	char *prt_data = NULL;
	char data[12] = { 0 };
	uint16_t cid = 0;
	unsigned int i = 0;

	prt_data = kzalloc(sizeof(char) * HX1K, GFP_KERNEL);
	if (prt_data == NULL) {
		E("%s: Memory allocation falied!\n", __func__);
		return;
	}
	cid = ((uint16_t)ic_data->vendor_cid_maj_ver << 8);
	cid += ic_data->vendor_cid_min_ver;
	len += snprintf(&prt_data[len], HX_SZ_ICID - len, "IC = %s\n",
			private_ts->chip_name);

	len += snprintf(&prt_data[len], HX_SZ_ICID - len,
			"FW Architecture Version : %X\n",
			ic_data->vendor_arch_ver);
	len += snprintf(&prt_data[len], HX_SZ_ICID - len, "CID : %04X\n",
			cid);
#if defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX8530)
	len += snprintf(&prt_data[len], HX_SZ_ICID - len,
			"FW Algorithm Version : A%02X\n",
			ic_data->vendor_display_cfg_ver);
#else
	len += snprintf(&prt_data[len], HX_SZ_ICID - len,
			"FW Display Config Version : D%02X\n",
			ic_data->vendor_display_cfg_ver);
#endif
	len += snprintf(&prt_data[len], HX_SZ_ICID - len,
			"FW Touch Config Version : C%02X\n",
			ic_data->vendor_touch_cfg_ver);
	len += snprintf(&prt_data[len], HX_SZ_ICID - len,
			"Panel Version : 0x%02X\n", ic_data->vendor_panel_ver);

	if (ic_data->vendor_arch_ver >= 0x8098U) {
		(void)memcpy(data, (char *)ic_data->vendor_remark1, sizeof(data));
		len += snprintf(&prt_data[len], HX_SZ_ICID - len, "Remark 1 = %s\n",
			data);
		(void)memcpy(data, (char *)ic_data->vendor_remark2, sizeof(data));
		len += snprintf(&prt_data[len], HX_SZ_ICID - len, "Remark 2 = %s\n",
			data);
		(void)memcpy(data, (char *)ic_data->vendor_ticket, sizeof(data));
		len += snprintf(&prt_data[len], HX_SZ_ICID - len, "Himax Ticket = %s\n",
			data);
	}
	(void)memcpy(data, (char *)ic_data->vendor_config_date, sizeof(data));
	len += snprintf(&prt_data[len], HX_SZ_ICID - len, "FW Config Date = %s\n",
			data);
	(void)memcpy(data, (char *)ic_data->vendor_proj_info, sizeof(data));
	len += snprintf(&prt_data[len], HX_SZ_ICID - len, "Project = %s\n",
			data);
	(void)memcpy(data, (char *)ic_data->vendor_cus_info, sizeof(data));
	len += snprintf(&prt_data[len], HX_SZ_ICID - len, "Customer = %s\n",
			data);
	len += snprintf(&prt_data[len], HX_SZ_ICID - len,
			"Himax Touch Driver Version = %s\n", HIMAX_DRIVER_VER);

	len += snprintf(&prt_data[len], HX_SZ_ICID - len,
			"\nSelf test item critera:\n");
	for (i = 0; i < HX_CRITERIA_SIZE; i++) {
		if (g_inspt_crtra_flag[i] == 1) {
			len += snprintf(&prt_data[len], HX_SZ_ICID - len,
					"Item %s\n", g_hx_inspt_crtra_name[i]);
			len += snprintf(&prt_data[len], HX_SZ_ICID - len,
					"%d\n", g_self_test_criteria[i][0]);
		}
	}

	(void)memcpy(&g_rslt_data[0], prt_data, len);
	g_rslt_data_len = (uint32_t)len;
	I("%s: g_rslt_data_len=%d!\n", __func__, g_rslt_data_len);

	kfree(prt_data);
	prt_data = NULL;
}

static void hx_print_normalize_status(void)
{
	int len = 0;
	char *prt_data = NULL;
	uint8_t data[DATA_LEN_4] = { 0 };

	prt_data = kzalloc(sizeof(char) * 60U, GFP_KERNEL);
	if (prt_data == NULL) {
		E("%s: Memory allocation falied!\n", __func__);
		return;
	}
	himax_mcu_register_read(addr_fw_define_rawdata_normalize, DATA_LEN_4,
				data);
	if ((data[3] & (0x80U >> 7U)) != 0U) {/*normalize enable*/
		len += snprintf(&prt_data[len], 60 - len,
				"\n***Rawdata Normalize Status : Enable\n");
	} else { /*normalize disable*/
		len += snprintf(&prt_data[len], 60 - len,
				"\n***Rawdata Normalize Status : Disable\n");
	}

	(void)memcpy(&g_rslt_data[0], prt_data, len);
	g_rslt_data_len = (uint32_t)len;
	I("%s: g_rslt_data_len=%d!\n", __func__, g_rslt_data_len);

	kfree(prt_data);
	prt_data = NULL;
}

static uint32_t hx_create_criteria_array(char *critera_str, unsigned int id)
{
	uint32_t i = 0;
	int result = 0;
	uint32_t ret = HX_INSP_OK;

	if (kstrtoint(critera_str, 10, &result) == 0) {
		for (i = 0; i < block_num; i++) {
			g_self_test_criteria[id][i] = (int16_t)result;
		}
	} else {
		I("Failed to convert string '%s' to integer\n", critera_str);
		ret = HX_INSP_EFILE;
	}

	return ret;
}

static uint32_t himax_default_item_and_criteria(void)
{
	unsigned int i = 0;
	unsigned int j = 0;
	unsigned int size;
	char *buf;
	char str_buf[10] = { 0 };
	uint32_t ret = HX_INSP_OK;

	I("%s: enter\n", __func__);
	size = hx_get_size_str_arr(default_item_and_criteria);
	for (i = 0; i < size; i++) {
		for (j = 0; j < HX_CRITERIA_SIZE; j++) {
			buf = strnstr(default_item_and_criteria[i], g_hx_inspt_crtra_name[j], strlen(default_item_and_criteria[i]));
			if (buf != NULL) {
				buf = strstr(default_item_and_criteria[i], ":");
				if (strcmp(&buf[1], "NULL") != 0) {
					ret = hx_create_criteria_array(&buf[1], j);
					if (ret > 0U) {
						E("%s: please check %s\n", __func__, default_item_and_criteria[i]);
						return ret;
					}
					g_inspt_crtra_flag[j] = 1;
				}
			}
			(void)memset(str_buf, '\0', sizeof(str_buf));
		}
	}
	I("%s: end\n", __func__);
	return ret;
}

static uint32_t himax_self_test_data_init(void)
{
	const struct firmware *csv_file = NULL;
	struct himax_ts_data *ts = private_ts;
	char file_name_2[30];
	uint32_t ret = HX_INSP_OK;
	int err = 0;
	unsigned int i = 0;
	struct time_var timeStart;
	int size = 0;
	char out_file[100];

	mutual_block_num = (uint32_t)ic_data->HX_TX_NUM
					* (uint32_t)ic_data->HX_RX_NUM;
	self_block_num = (uint32_t)ic_data->HX_TX_NUM
					+ (uint32_t)ic_data->HX_RX_NUM;

#if defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX8530)
	block_num = mutual_block_num + self_block_num;
#else
	block_num = mutual_block_num;
#endif

	(void)strcpy(file_name_2, "hx_criteria.csv");

	if (ts->self_test_file_ch[0] != (char)0) {
		(void)memset(file_name_2, 0, sizeof(file_name_2));
		(void)memcpy(file_name_2, ts->self_test_file_ch, strlen(ts->self_test_file_ch));
		I("%s: hx_criteria file name change to |%s|\n", __func__, file_name_2);
	}

	/*
	 * 5: one value will not over than 99999, so get this size of string
	 * 2: get twice size
	 */
	g_1kind_raw_size = 5U * block_num * 2U;

	/* get test item and its items of criteria*/
	HX_CRITERIA_ITEM = hx_get_size_str_arr(g_himax_self_test_mode);
	HX_CRITERIA_SIZE = hx_get_size_str_arr(g_hx_inspt_crtra_name);
	I("There is %d HX_CRITERIA_ITEM and %d HX_CRITERIA_SIZE\n",
	  HX_CRITERIA_ITEM, HX_CRITERIA_SIZE);

	/* init criteria data*/
	g_test_item_flag = kcalloc(HX_CRITERIA_ITEM, sizeof(int), GFP_KERNEL);
	if (g_test_item_flag == NULL) {
		E("%s,%d: Memory allocation falied!\n", __func__, __LINE__);
		ret = HX_INSP_MEMALLCTFAIL;
		goto err_malloc_test_item_flag;
	}

	g_inspt_crtra_flag = kcalloc(HX_CRITERIA_SIZE, sizeof(int), GFP_KERNEL);
	if (g_inspt_crtra_flag == NULL) {
		E("%s,%d: Memory allocation falied!\n", __func__, __LINE__);
		ret = HX_INSP_MEMALLCTFAIL;
		goto err_malloc_inspt_crtra_flag;
	}

	g_self_test_criteria =
		kcalloc(HX_CRITERIA_SIZE, sizeof(int *), GFP_KERNEL);
	if (g_self_test_criteria == NULL) {
		E("%s,%d: Memory allocation falied!\n", __func__, __LINE__);
		ret = HX_INSP_MEMALLCTFAIL;
		goto err_malloc_self_test_criteria;
	}

	for (i = 0; i < HX_CRITERIA_SIZE; i++) {
		g_self_test_criteria[i] =
			kcalloc((ic_data->HX_TX_NUM * ic_data->HX_RX_NUM),
				sizeof(int), GFP_KERNEL);
		if (g_self_test_criteria[i] == NULL) {
			E("%s,%d: Memory allocation %d falied!\n", __func__,
			  __LINE__, i);
			ret = HX_INSP_MEMALLCTFAIL;
			goto err_malloc_self_test_criteria2;
		}
	}

	g_rslt_data_len = 0;
	if (g_rslt_data == NULL) {
		g_rslt_data =
			kcalloc(g_1kind_raw_size, sizeof(char), GFP_KERNEL);
		if (g_rslt_data == NULL) {
			E("%s,%d: Memory allocation falied!\n", __func__,
			  __LINE__);
			ret = HX_INSP_MEMALLCTFAIL;
			goto err_malloc_rslt_data;
		}
	}
	I("%s: initialize g_rslt_data, length = %d\n", __func__,
	  g_1kind_raw_size);
	(void)memset(g_rslt_data, 0x00, g_1kind_raw_size * sizeof(char));

	if (g_start_log == NULL) {
		g_start_log =
			kcalloc(512, sizeof(char), GFP_KERNEL);
		if (g_start_log == NULL) {
			E("%s,%d: Memory allocation falied!\n", __func__,
			  __LINE__);
			ret = HX_INSP_MEMALLCTFAIL;
			goto err_malloc_start_log;
		}
	}

	if (g_file_path == NULL) {
		g_file_path =
			kcalloc(256, sizeof(char), GFP_KERNEL);
		if (g_file_path == NULL) {
			E("%s,%d: Memory allocation falied!\n", __func__,
			  __LINE__);
			ret = HX_INSP_MEMALLCTFAIL;
			goto err_malloc_file_path;
		}
	}

	/* default path is /system/etc/firmware */
	/* request criteria file*/

	err = request_firmware(&csv_file, file_name_2, ts->dev);
	if (err < 0) {
		I("%s,Fail to get %s\n", __func__, file_name_2);
		I("%s: No criteria file\n", __func__);
		I("%s: Change to use default criteria(default_item_and_criteria)\n", __func__);
		ret = himax_default_item_and_criteria();
		if (ret > 0U) {
			goto err_open_criteria_file;
		}
	} else {
		I("%s,Success to get %s\n", __func__, file_name_2);
		/* parsing criteria from file .csv*/
		ret = himax_parse_criteria(csv_file);
		release_firmware(csv_file);
		if (ret > 0U) {
			I("%s,err_open_criteria_file\n", __func__);
			goto err_open_criteria_file;
		}
	}
	himax_test_item_chk();

	if ((private_ts->debug_log_level & BIT(4)) != 0U) {
		/* print get criteria string */
		for (i = 0; i < HX_CRITERIA_SIZE; i++) {
			if (g_inspt_crtra_flag[i] != 0) {
				I("%s: [%d]There is String=%s\n", __func__, i,
				  g_hx_inspt_crtra_name[i]);
			}
		}
	}

	time_func(&timeStart);
#if defined(KERNEL_VER_5_10)
	size = snprintf(out_file, sizeof(out_file), "hx_test_result_%lld.txt", timeStart.tv_sec);
#else
	size = snprintf(out_file, sizeof(out_file), "hx_test_result_%ld.txt", timeStart.tv_sec);
#endif

	(void)snprintf(g_file_path,
		 (strlen(HX_RSLT_OUT_PATH) + strlen(out_file) + 1U), "%s%s",
		 HX_RSLT_OUT_PATH, out_file);

	file_w_flag = true;
	for (i = 0; i < HX_CRITERIA_SIZE; i++) {
		if (g_inspt_crtra_flag[i] == 1) {
			I("[%s]\n", default_item_and_criteria[i]);
		}
	}
	return ret;

err_open_criteria_file:
	kfree(g_file_path);
	g_file_path = NULL;
err_malloc_file_path:
	kfree(g_start_log);
	g_start_log = NULL;
err_malloc_start_log:
	kfree(g_rslt_data);
	g_rslt_data = NULL;
err_malloc_rslt_data:

err_malloc_self_test_criteria2:
	for (i = 0; i < HX_CRITERIA_SIZE; i++) {
		if (g_self_test_criteria[i] != NULL) {
			kfree(g_self_test_criteria[i]);
			g_self_test_criteria[i] = NULL;
		}
	}
	kfree(g_self_test_criteria);
	g_self_test_criteria = NULL;
err_malloc_self_test_criteria:
	kfree(g_inspt_crtra_flag);
	g_inspt_crtra_flag = NULL;
err_malloc_inspt_crtra_flag:
	kfree(g_test_item_flag);
	g_test_item_flag = NULL;
err_malloc_test_item_flag:
	return ret;
}

static void himax_self_test_data_deinit(void)
{
	unsigned int i = 0;

	/*dbg*/
	/* for (i = 0; i < HX_CRITERIA_ITEM; i++)
	 *	I("%s:[%d]%d\n", __func__, i, g_self_test_criteria[i]);
	 */

	I("%s: release allocated memory\n", __func__);

	for (i = 0; i < HX_CRITERIA_SIZE; i++) {
		if (g_self_test_criteria[i] != NULL) {
			kfree(g_self_test_criteria[i]);
			g_self_test_criteria[i] = NULL;
		}
	}
	kfree(g_self_test_criteria);
	g_self_test_criteria = NULL;

	kfree(g_inspt_crtra_flag);
	g_inspt_crtra_flag = NULL;

	kfree(g_test_item_flag);
	g_test_item_flag = NULL;
	I("%s: release finished\n", __func__);
}

#if (HX_RST_PIN_FUNC == 0x01)
/*
 *+------------+------------+--------+----------------------------+
 *|   State1   |   State2   | Result |            Info            |
 *+------------+------------+--------+----------------------------+
 *| 0x05       | != 0x3776  | OK     | Reset Pin OK               |
 *| Don’t Care | == 0x3776  | Fail   | Reset Pin Fail : Keep High |
 *| 0xFF       | Don’t Care | Fail   | Reset Pin Fail : Keep Low  |
 *| Others     | Others     | Fail   | Unknown Fail               |
 *+------------+------------+--------+----------------------------+
 */

static bool himax_mpap_test_tp_rst(void)
{
	uint8_t tmp_data[DATA_LEN_4] = { 0 };
	uint8_t count = 0;
	uint16_t State2 = 0;

	g_core_fp.fp_sense_off();

	tmp_data[3] = 0x00;
	tmp_data[2] = 0x00;
	tmp_data[1] = 0x37;
	tmp_data[0] = 0x76;

	himax_mcu_register_write(addr_mpap_rst_test, 4, tmp_data);

	himax_mcu_toggle_rst_gpio();

	do {

		himax_mcu_register_read(addr_cs_central_state, DATA_LEN_4,	tmp_data);
		usleep_range(1000, 1100);
		count++;

	} while ((tmp_data[0] == 0x04U) && (count < 100U));

	if (tmp_data[0] == 0x05U) {
		himax_mcu_register_read(addr_mpap_rst_test, DATA_LEN_4,	tmp_data);
		State2 = ((uint16_t)tmp_data[1] << 8U) + (uint16_t)tmp_data[0];
		if (State2 != 0x3776U) {
			I("%s:MPAP TP RST test pass\n", __func__);
			return true;
		}
	}

	return false;
}
#endif

static bool himax_mpap_test_int(void)
{
	uint8_t tmp_data[DATA_LEN_4] = { 0 };
	bool ret = true;
	uint8_t int_status = 0;

	g_core_fp.fp_sense_off();

	tmp_data[3] = 0x00;
	tmp_data[2] = 0x00;
	tmp_data[1] = 0x00;
	tmp_data[0] = 0x02;
	himax_mcu_register_write(addr_mpap_int_test_base, 4, tmp_data);

	tmp_data[0] = 0x01;
	himax_mcu_register_write(addr_mpap_int_test_cmd_1, 4, tmp_data);

	tmp_data[0] = 0x00;
	himax_mcu_register_write(addr_mpap_int_test_cmd_2, 4, tmp_data);

	/*MPAP test int pin low*/
	int_status = himax_int_gpio_read(private_ts->pdata->TSIX);
	if (int_status != 0U) {
		ret = false;
		E("%s:MPAP INT pin low test fail\n", __func__);
	}

	tmp_data[0] = 0x02;
	himax_mcu_register_write(addr_mpap_int_test_base, 4, tmp_data);

	tmp_data[0] = 0x01;
	himax_mcu_register_write(addr_mpap_int_test_cmd_1, 4, tmp_data);

	tmp_data[0] = 0x01;
	himax_mcu_register_write(addr_mpap_int_test_cmd_2, 4, tmp_data);

	/*MPAP test int pin high*/
	int_status = himax_int_gpio_read(private_ts->pdata->TSIX);
	if (int_status != 1U) {
		ret = false;
		E("%s:MPAP INT pin high test fail\n", __func__);
	}

	g_core_fp.fp_sense_on();

	if (ret) {
		I("%s:MPAP INT test pass\n", __func__);
	}

	return ret;
}
#if defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX8530)
static bool himax_mpap_test_fail_det(void)
{
	uint8_t tmp_data[DATA_LEN_4] = { 0 };
	uint8_t pswd[DATA_LEN_4] = { 0 };
	bool ret = false;
	uint8_t gpio_status = 0U;
	uint8_t counter = 0U;

	himax_mcu_register_read(addr_cs_central_state, DATA_LEN_4, tmp_data);

	if (tmp_data[0] != 0x05U) {
		E("%s: 9000005C data[0]=0x%02X, \n", __func__,
		  tmp_data[0]);
	} else {

		while (counter < 10U) {
			himax_mcu_register_read(addr_chk_fw_reload2,
					DATA_LEN_4, tmp_data);
			pswd[1] = (uint8_t)((addr_chk_fw_reload2 & 0xFF00U) >> 8U);
			pswd[0] = (uint8_t)(addr_chk_fw_reload2 & 0xFFU);
			if ((tmp_data[1] == pswd[1]) && (tmp_data[0] == pswd[0])) {
				I("%s: reload done takes %d times\n", __func__,
			 		counter);
				ret = true;
				break;
			} else {
				counter++;
				usleep_range(10000, 11000);
			}
		}

		if(ret) {
			himax_parse_assign_cmd(data_mpap_fail_det_test_L, tmp_data,
							sizeof(tmp_data));
			himax_mcu_register_write(addr_mpap_fail_det_test, 4, tmp_data);

			gpio_status = himax_int_gpio_read(private_ts->pdata->fail_det);
			if (gpio_status != 0U) {
				ret = false;
				E("%s:MPAP Fail_det pin Low test fail\n", __func__);
			}
		}

		if(ret) {
			himax_parse_assign_cmd(data_mpap_fail_det_test_H, tmp_data,
							sizeof(tmp_data));
			himax_mcu_register_write(addr_mpap_fail_det_test, 4, tmp_data);
			msleep(50);
			gpio_status = himax_int_gpio_read(private_ts->pdata->fail_det);
			if (gpio_status != 1U) {
				ret = false;
				E("%s:MPAP Fail_det pin High test fail\n", __func__);
			}
		}
	}

	if (ret) {
		I("%s:MPAP Fail_det pin test pass\n", __func__);
	}

	himax_parse_assign_cmd(0x00000000, tmp_data,
					sizeof(tmp_data));
	himax_mcu_register_write(addr_mpap_fail_det_test, 4, tmp_data);

	return ret;
}

static bool himax_mpap_check_Vsync(void) {
	uint8_t tmp_data[DATA_LEN_4] = { 0 };
	bool ret = false;
	uint8_t counter = 0U;
	uint32_t addr_Vsync_status = 0x130074F8U;

	himax_mcu_register_read(addr_cs_central_state, DATA_LEN_4, tmp_data);

	if (tmp_data[0] != 0x05U) {
		E("%s: 9000005C data[0]=0x%02X, \n", __func__,
		  tmp_data[0]);
	} else {
		while (counter < 50U) {
			himax_mcu_check_sorting_mode(tmp_data);
			if ((tmp_data[0] == 0x99U) &&
				(tmp_data[1] == 0x99U)) {

				msleep(50);
				himax_mcu_register_read(addr_Vsync_status,
					DATA_LEN_4, tmp_data);
				if ((tmp_data[0] & 0x01U) == 0x00U) {
					ret = true;
				}
				break;
			}
			counter += 1U;
			I("Retry %d times!\n", counter);
		}
	}

	if (ret) {
		I("%s:MPAP check_Vsync Pass\n", __func__);
	} else {
		I("%s:MPAP check_Vsync Fail\n", __func__);
	}

	return ret;
}
#endif

void himax_chip_self_test(struct seq_file *s, void *v)
{
	uint32_t ret = HX_INSP_OK;
	uint32_t i = 0;
	uint8_t tmp_data[DATA_LEN_4] = { 0x01, 0x00, 0x00, 0x00 };
	struct file *raw_file = NULL;
	/*struct filename *vts_name = NULL;*/
#if !defined(KERNEL_VER_5_18)
    mm_segment_t fs;
#endif
	loff_t pos = 0;
	uint32_t rslt = HX_INSP_OK;
	char write_buffer[512] = { 0 };
	int len = 0;

	UNUSED(v);

	private_ts->suspend_resume_done = 0;

	ret = himax_self_test_data_init();
	if (ret > 0U) {
		E("%s: initialize self test failed\n", __func__);
		goto END;
	}

	if (raw_file == NULL) {
		raw_file = filp_open(g_file_path, (O_TRUNC | O_CREAT | O_RDWR),
				     (S_IRGRP | S_IWGRP | S_IRUSR | S_IWUSR));

		if (IS_ERR(raw_file) != 0) {
			E("%s open file failed = %ld\n", __func__,
			  PTR_ERR(raw_file));
			file_w_flag = false;
		}
	}

#if !defined(KERNEL_VER_5_18)
    fs = get_fs();
    set_fs(KERNEL_DS);
#endif
	hx_print_ic_id();
	if (file_w_flag) {
#ifdef KERNEL_VER_ABOVE_4_14
		kernel_write(raw_file, g_rslt_data, g_rslt_data_len, &pos);
#else
		vfs_write(raw_file, g_rslt_data, g_rslt_data_len, &pos);
#endif
	}

	I("%s: enter, %d\n", __func__, __LINE__);
	seq_puts(s, "Self_Test Duration==>\n");
	/*Do normal test items*/
	for (i = 0; i < HX_CRITERIA_ITEM; i++) {
		if (g_test_item_flag[i]) {
			I("%d. %s Start\n", i,
				g_himax_self_test_mode[i]);
			rslt = mpTestFunc((uint8_t)i, (mutual_block_num + self_block_num), s);
			if (file_w_flag &&
				((rslt & HX_INSP_EGETRAW) == 0U) &&
				((rslt & HX_INSP_ESWITCHMODE) == 0U)) {
#ifdef KERNEL_VER_ABOVE_4_14
				kernel_write(raw_file, g_rslt_data,
							g_rslt_data_len, &pos);
#else
				vfs_write(raw_file, g_rslt_data,
						g_rslt_data_len, &pos);
#endif
			}
			ret |= rslt;

			I("%d. %s End, ret = %d\n", i,
				g_himax_self_test_mode[i], ret);
		}
	}
	I("%s: enter, %d\n", __func__, __LINE__);

	hx_print_normalize_status();
	if (file_w_flag) {
#ifdef KERNEL_VER_ABOVE_4_14
		kernel_write(raw_file, g_rslt_data, g_rslt_data_len, &pos);
#else
		vfs_write(raw_file, g_rslt_data, g_rslt_data_len, &pos);
#endif
	}

	g_core_fp.fp_sense_off();
	/*himax_set_N_frame(1, HX_self_test_WT_NOISE);*/
	/* set N frame back to default value 1*/
	himax_mcu_register_write(addr_set_frame_addr, 4, tmp_data);

	himax_mcu_config_reload_enable();
#if !defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83194)
	himax_mcu_rawdata_normalize_disable(0);
#endif

	if (himax_check_mode(HX_RAWDATA) != 0U) {
		I("%s:try to  Need to back to Normal!\n", __func__);
		himax_switch_mode_self_test(HX_RAWDATA);
		g_core_fp.fp_sense_on();
		(void)himax_wait_sorting_mode(HX_RAWDATA);
	} else {
		I("%s: It has been in Normal!\n", __func__);
		g_core_fp.fp_sense_on();
	}

	if (ret == HX_INSP_OK) {
		seq_puts(s, "Self_Test Pass==>\n");
	} else {
		seq_puts(s, "Self_Test Fail==>\n");
	}

	for (i = 0; i < (HX_CRITERIA_ITEM - 1U); i++) {
		if (g_test_item_flag[i]) {
			seq_printf(s, "\t%s : %s\n", g_himax_self_test_mode[i],
				   ((ret & (1U << (i + ERR_SFT))) ==
				    (1U << (i + ERR_SFT))) ?
					   "Fail" :
						 "OK");
		}
	}

	himax_self_test_data_deinit();


	if (private_ts->chip_test_mpap_flag) {
		len += snprintf(&write_buffer[len], 512 - len,
		"\n\n---------------------------MPAP items test start----------------------------\n\n");
#if (HX_RST_PIN_FUNC == 0x01)
		if (himax_mpap_test_tp_rst()) {
			seq_puts(s, "MPAP tp rst test Pass\n");
			len += snprintf(&write_buffer[len], 512 - len, "MPAP tp rst test Pass\n");
		} else {
			seq_puts(s, "MPAP tp rst test Fail\n");
			len += snprintf(&write_buffer[len], 512 - len, "MPAP tp rst test Fail\n");
		}
#else
		seq_puts(s, "Need config TP RST pin!!!\n");
		seq_puts(s, "Software tp reset not support MPAP tp rst test item\n");
#endif

		if (himax_mpap_test_int()) {
			seq_puts(s, "MPAP int test Pass\n");
			len += snprintf(&write_buffer[len], 512 - len, "MPAP int test Pass\n");
		} else {
			seq_puts(s, "MPAP int test Fail\n");
			len += snprintf(&write_buffer[len], 512 - len, "MPAP int test Fail\n");
		}
#if defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX8530)
		if (himax_mpap_test_fail_det()) {
			seq_puts(s, "MPAP fail_det test Pass\n");
			len += snprintf(&write_buffer[len], 512 - len, "MPAP fail_det test Pass\n");
		} else {
			seq_puts(s, "MPAP fail_det test Fail\n");
			len += snprintf(&write_buffer[len], 512 - len, "MPAP fail_det test Fail\n");
		}
		if (himax_mpap_check_Vsync()) {
			seq_puts(s, "MPAP check_Vsync Pass\n");
			len += snprintf(&write_buffer[len], 512 - len, "MPAP check_Vsync Pass\n");
		} else {
			seq_puts(s, "MPAP check_Vsync Fail\n");
			len += snprintf(&write_buffer[len], 512 - len, "MPAP check_Vsync Fail\n");
		}
#endif
		if (file_w_flag) {
#ifdef KERNEL_VER_ABOVE_4_14
			kernel_write(raw_file, write_buffer, len, &pos);
#else
			vfs_write(raw_file, write_buffer, len, &pos);
#endif
		}

	}

	if (file_w_flag) {
		filp_close(raw_file, NULL);
	}
#if !defined(KERNEL_VER_5_18)
    set_fs(fs);
#endif

END:
	I("running status = 0x%02X\n", ret);

	I("%s:output file to path : %s\n", __func__, g_file_path);
}

void himax_self_test_data_clear(void)
{
	if (g_rslt_data != NULL) {
		kfree(g_rslt_data);
		g_rslt_data = NULL;
	}
	if (g_start_log != NULL) {
		kfree(g_start_log);
		g_start_log = NULL;
	}
	if (g_file_path != NULL) {
		kfree(g_file_path);
		g_file_path = NULL;
	}
}

