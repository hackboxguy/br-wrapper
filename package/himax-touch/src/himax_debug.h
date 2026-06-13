/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef H_HIMAX_DEBUG
#define H_HIMAX_DEBUG

#include "himax_platform.h"
#include "himax_common.h"

/*------------------------- define block -------------------------------------*/

/* enum rawdata_process_type */
#define Tch_mot_flag 0U
#define Dsram_delivery 1U

/* enum flash_dump_prog */
#define Dump_Finished 0U
#define Dump_Ongoing 1U

#define HIMAX_PROC_VENDOR_FILE "vendor"
#define HIMAX_PROC_DIAG_FOLDER "diag"
#define HIMAX_PROC_STACK_FILE "stack"
#define HIMAX_PROC_DELTA_FILE "delta_s"
#define HIMAX_PROC_DC_FILE "dc_s"
#define HIMAX_PROC_BASELINE_FILE "baseline_s"
#define HIMAX_PROC_DEBUG_FILE	"debug"
#define HIMAX_PROC_FLASH_DUMP_FILE	"flash_dump"
#define HIMAX_PROC_PINTEST_FILE	"pintest"
#define HIMAX_PROC_SRAM_TEST_FILE	"sram_test"
#define CMD_NUM 16

/*------------------------- define block -------------------------------------*/
/*------------------------- parameter block ----------------------------------*/

extern uint8_t byte_length;
extern uint8_t register_command[4];
extern uint8_t cfg_flag;
extern uint8_t g_hx_state_info[2];
/*extern uint8_t diag_coor[128];*/
extern bool	fw_update_complete;
extern char debug_level_cmd;
extern uint8_t *g_flash_buffer;
extern uint8_t g_flash_cmd;
extern bool g_flash_dump_rst; /*Fail = false, Pass = true*/
extern char *dbg_cmd_par;
extern int g_max_mutual;
extern int g_min_mutual;
extern int g_max_self;
extern int g_min_self;
extern uint8_t reg_cmd[4];
extern uint8_t g_diag_arr_num;
extern bool diag_wq_alive;
extern uint8_t diag_max_cnt;
extern bool fw_update_going;
extern uint32_t g_page_prog_start;
extern uint8_t g_flash_progress;
extern uint8_t *reg_read_data;

uint8_t byte_length;
uint8_t cfg_flag;
int16_t *diag_mutual;
int16_t *diag_mutual_new;
int16_t *diag_mutual_old;
uint8_t g_hx_state_info[2];
/*uint8_t diag_coor[128];*/
int16_t *diag_self;
int16_t *diag_self_new;
int16_t *diag_self_old;
bool fw_update_complete;
char debug_level_cmd;
unsigned int g_Flash_Size = 0x80000U;
uint8_t *g_flash_buffer;
uint8_t g_flash_cmd;
bool g_flash_dump_rst; /*Fail = false, Pass = true*/

/*------------------------- parameter block ----------------------------------*/
/*------------------------- structure block ----------------------------------*/

extern struct time_var g_timeStart;
extern struct time_var g_timeEnd;
extern struct time_var g_timeDelta;

//extern struct himax_debug *debug_data;
extern struct proc_dir_entry *himax_proc_diag_dir;
extern struct proc_dir_entry *himax_proc_vendor_file;
extern struct proc_dir_entry *himax_proc_stack_file;
extern struct proc_dir_entry *himax_proc_delta_file;
extern struct proc_dir_entry *himax_proc_dc_file;
extern struct proc_dir_entry *himax_proc_baseline_file;
extern struct proc_dir_entry *himax_proc_debug_file;
extern struct proc_dir_entry *himax_proc_flash_dump_file;
extern struct proc_dir_entry *himax_proc_pintest_file;
extern struct proc_dir_entry *himax_proc_SRAM_test_file;

/*------------------------- structure block ----------------------------------*/
/*------------------------- function block -----------------------------------*/

extern int himax_int_en_set(void);
extern int himax_input_register(struct himax_ts_data *ts);
#if (HX_RST_PIN_FUNC == 0x01)
extern void himax_ic_reset(uint8_t loadconfig, uint8_t int_off);
#endif

int16_t *getMutualBuffer(void);
int16_t *getMutualNewBuffer(void);
int16_t *getMutualOldBuffer(void);
int16_t *getSelfBuffer(void);
int16_t *getSelfNewBuffer(void);
int16_t *getSelfOldBuffer(void);
bool himax_ts_diag_func(void);
int himax_debug_init(void);
void himax_debug_remove(void);


/*------------------------- function block -----------------------------------*/

#endif
