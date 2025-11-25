/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef H_HIMAX_SELF_TEST
#define H_HIMAX_SELF_TEST

#include "himax_platform.h"
#include "himax_common.h"
#include "himax_ic_core.h"

/*-------------------------------------- define block ---------------------------------------------*/

/* enum THP_self_test_ENUM */
#define	HX_OPEN				0x00U
#define	HX_MICRO_OPEN		0x01U
#define	HX_SHORT			0x02U
#define	HX_WT_NOISE			0x03U
#define	HX_NOISE			0x04U
#define	HX_RAWDATA			0x05U
#define	HX_BPN_RAWDATA		0x06U
#define	HX_SBP_RAWDATA		0x07U
#define	HX_SC				0x08U
#define	HX_SORTING			0x09U
#define	HX_GAPTEST_RAW		0x0AU
/*Must put in the end*/
#define	HX_BACK_NORMAL		0x0BU


/* enum HX_CRITERIA_ENUM */
#define	IDX_RAWMIN			0U
#define	IDX_RAWMAX			1U
#define	IDX_BPN_RAWMIN		2U
#define	IDX_BPN_RAWMAX		3U
#define	IDX_SBP_RAWMIN		4U
#define	IDX_SBP_RAWMAX		5U
#define	IDX_SCMIN			6U
#define	IDX_SCMAX			7U
#define	IDX_SC_GOLDEN		8U
#define	IDX_SHORTMIN		9U
#define	IDX_SHORTMAX		10U
#define	IDX_OPENMIN			11U
#define	IDX_OPENMAX			12U
#define	IDX_M_OPENMIN		13U
#define	IDX_M_OPENMAX		14U
#define	IDX_WT_NOISEMIN		15U
#define	IDX_WT_NOISEMAX		16U
#define	IDX_NOISEMIN		17U
#define	IDX_NOISEMAX		18U
#define	IDX_SORTMIN			19U
#define	IDX_SORTMAX			20U
#define	IDX_GAP_HOR_RAWMAX	21U
#define	IDX_GAP_HOR_RAWMIN	22U
#define	IDX_GAP_VER_RAWMAX	23U
#define	IDX_GAP_VER_RAWMIN	24U

#define ERR_SFT 5U
/* Error code of self_test */
	/* OK */
#define	HX_INSP_OK				0x00U
	/* Criteria file error*/
#define	HX_INSP_EFILE			0x01U
	/* Get raw data errors */
#define	HX_INSP_EGETRAW			0x02U
	/* Memory allocate errors */
#define	HX_INSP_MEMALLCTFAIL	0x03U
	/* Switch mode error*/
#define	HX_INSP_ESWITCHMODE		0x04U

#define	HX_RSLT_OUT_PATH	"/sdcard/"
#define	HX_SZ_ICID			1024
#define PI(x...) pr_cont(x)

#define	BS_RAWDATA			8U
#define	BS_NOISE			8U
#define	BS_OPENSHORT		1U
#define	BS_INSPECT_MODE		1U
#define	NOISEFRAME			200U
#define	OTHERSFRAME			1U

/*Himax MP Password*/
#define	PWD_OPEN_START			0x77U
#define	PWD_OPEN_END			0x88U
#define	PWD_MICRO_OPEN_START	0x6BU
#define	PWD_MICRO_OPEN_END		0x70U
#define	PWD_SHORT_START			0x11U
#define	PWD_SHORT_END			0x33U
#define	PWD_RAWDATA_START		0x00U
#define	PWD_RAWDATA_END			0x99U
#define	PWD_NOISE_START			0x00U
#define	PWD_NOISE_END			0x99U
#define	PWD_SORTING_START		0xAAU
#define	PWD_SORTING_END			0xCCU

/*Himax DataType*/
#define	DATA_SORTING			0x0AU
#define	DATA_OPEN				0x0BU
#define	DATA_MICRO_OPEN			0x0AU
#define	DATA_SHORT				0x0AU
#define	DATA_RAWDATA			0x0AU
#define	DATA_Self_test_RAWDATA	0x0BU
#define	DATA_NOISE				0x0FU
#define	DATA_BACK_NORMAL		0x00U

/*self_test register*/
#define	addr_normal_noise_thx	(addr_CFG_base + 0x1000708CU)
#define	addr_recal_thx			(addr_CFG_base + 0x10007090U)
#define	addr_palm_num			(addr_CFG_base + 0x100070A8U)
#define	addr_weight_sup			(addr_CFG_base + 0x100072C8U)
#define	addr_normal_weight_a	(addr_CFG_base + 0x1000709CU)
#define	addr_weight_b			(addr_CFG_base + 0x10007094U)
#define	addr_max_dc				(addr_CFG_base + 0x10007FC8U)
#define	addr_skip_frame			(addr_CFG_base + 0x100070F4U)
#define	addr_neg_noise_sup		(addr_CFG_base + 0x10007FD8U)
#define	data_neg_noise			0x7F0C0000U

#define	HX_INSPECT_MODE			0xFFU
/*Need to map *g_himax_self_test_mode[]*/

#define FAIL_IN_INDEX "%s: %s FAIL in index %d\n"
extern char *g_rslt_data;


void himax_self_test_data_clear(void);


#endif
