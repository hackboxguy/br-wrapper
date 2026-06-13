/* SPDX-License-Identifier: GPL-2.0 */
/*	Himax Android Driver Sample Code for ic core functions
 *
 *	Copyright (C) 2024 Himax Corporation.
 *
 *	This software is licensed under the terms of the GNU General Public
 *	License version 2,	as published by the Free Software Foundation,  and
 *	may be copied,	distributed,  and modified under those terms.
 *
 *	This program is distributed in the hope that it will be useful,
 *	 but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 */

#ifndef HIMAX_IC_CORE_H__
#define HIMAX_IC_CORE_H__

#include "himax_platform.h"
#include "himax_common.h"
#include <linux/slab.h>

/* CORE_INIT */

struct himax_core_fp {
	void (*fp_sense_on)(void);
	void (*fp_sense_off)(void);
	bool (*fp_flash_programming)(const u8 *FW_content, unsigned int start_addr, unsigned int FW_Size);
	void (*fp_flash_dump_func)(uint32_t start_addr, unsigned int Flash_Size, uint8_t *flash_buffer);
	uint32_t (*fp_check_CRC)(uint8_t *start_addr, unsigned int reload_length);
	bool (*fp_calculateChecksum)(uint32_t size);
	void (*fp_touch_information)(void);
	bool (*fp_get_DSRAM_data)(uint8_t *tmp_rawdata);
	void (*fp_burst_mode_enable)(void);
	bool (*fp_fts_ctpm_fw_upgrade)(const u8 *fw_data, unsigned int bin_size);
	void (*fp_slave_AHB_reg_broadcast_write)(uint8_t host_id, uint32_t waddr, uint8_t *wdata, uint8_t wlen);
	void (*fp_slave_AHB_reg_broadcast_read)(uint8_t host_id, uint32_t raddr, uint8_t *rdata, uint8_t rlen);

};


/*AHB_Interface_Command_Table*/
#define addr_AHB_address_byte_0			0x00U
#define addr_AHB_rdata_byte_0			0x08U
#define addr_AHB_access_direction		0x0CU
#define addr_AHB_continous				0x13U
#define addr_AHB_INC4					0x0DU
#define addr_sense_on_off_0				0x31U
#define addr_sense_on_off_1				0x32U
#define addr_read_event_stack			0x30U
#define para_AHB_access_direction_read	0x00U
#define para_AHB_continous				0x31U
#define para_AHB_INC4					0x10U
#define para_sense_off_0				0x27U
#define para_sense_off_1				0x95U
#define addr_CONV_I2C_cmd				0x80U
/*AHB_Interface_Command_Table*/

/*bin_desc_map_table*/
#define	TP_CONFIG_TABLE			0x0000000AU
#define	FW_CID					0x10000000U
#define	FW_VER					0x10000100U
#define	CFG_VER					0x30000000U
/*bin_desc_map_table*/

#define DATA_LEN_8				8U
#define DATA_LEN_4				4U
#define ADDR_LEN_4				4U
#define FLASH_RW_MAX_LEN		256U
#define MAX_I2C_TRANS_SZ		128U
#define HIMAX_TOUCH_DATA_SIZE	128U
#define FW_PAGE_SZ				128U
#define HX1K					0x400U
#define HX4K					0x1000U
#define HX8K					0x2000U
#define HX64K					0x10000U
#define HX247K					0x3DC00U
#define HX256K					0x40000U
#define HX512K					0x80000U
#define FW_SIZE_128k			131072U
#define FW_SIZE_192k			196608U
#define FW_SIZE_255k			261120U
#define HX_RW_REG_FAIL			(-1)
#define INT16_MAX				(0x7FFFU)

#if (HX_WPBP_ENABLE == 0x01)
/*Customer define for different Flash IC*/
#define FLASH_LOCK_CODE			0x9C
#endif

/* CORE_FW */
#if defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX8530)
	#define addr_CFG_base						0X03000000U
	#define addr_rawdata		         		0x13000100U
#else
	#define addr_CFG_base						0X00000000U
	#define addr_rawdata						0x10000000U
#endif
	#define addr_fw_state						0x800204DCU
	#define addr_fw_state2						0x80020048U
	#define addr_scu_reload_control				0x90000080U
	#define addr_psl							0x900000A0U
	#define addr_cs_central_state				0x900000A8U
	#define addr_flag_reset_event				0x900000E4U
	#define addr_chk_dd_status					0x900000E8U
	#define addr_chk_tp_status					0x900000ECU
	#define addr_osc_en							0x9000009CU
	#define addr_osc_pw							0x90000280U
	#define addr_system_reset					0x90000018U
	#define addr_ctrl_fw						0x9000005CU
	#define addr_icid_addr						0x900000D0U
	#define addr_tp_dmy_config					0x900000B0U
	#define addr_scu_dd_version					0x900001D4U
	#define addr_product_id						0x900001D8U
	#define addr_retry_wrapper_clr_pw			0x900002A0U
	#define addr_program_reload_from			0x00000000U
	#define addr_reload_status					0x80050000U
	#define addr_reload_crc32_result			0x80050018U
	#define addr_reload_addr_from				0x80050020U
	#define addr_reload_addr_cmd_beat			0x80050028U
	#define addr_83181_dd_reg_flow_broadcast	0x80070084U
	#define addr_83181_dd_reg_flow_high_addr	0x80070060U
	#define addr_83181_dd_reg_flow_low_addr		0xC000005CU
	#define data_system_reset					0x00000055U
	#define data_clear							0x00000000U
	#define addr_FW_baseline_ready				0x800204DCU
	#define data_fw_define_flash_reload_dis		0x0000a55aU
	#define data_fw_define_flash_reload_en		0x00000000U
	#define addr_LTDI_fw_state					(addr_CFG_base + 0x10007410U)
	#define addr_raw_out_sel					(addr_CFG_base + 0x100072ECU)
	#define addr_set_frame_addr					(addr_CFG_base + 0x10007294U)
	#define addr_inspect_mode					(addr_CFG_base + 0x10007454U)
	#define addr_sorting_mode_en				(addr_CFG_base + 0x10007F04U)
	#define addr_fw_architecture_version		(addr_CFG_base + 0x10007004U)
	#define addr_fw_config_date					(addr_CFG_base + 0x10007038U)
	#define addr_fw_config_version				(addr_CFG_base + 0x10007084U)
	#define addr_fw_CID							(addr_CFG_base + 0x10007000U)
	#define addr_fw_customer					(addr_CFG_base + 0x10007008U)
	#define addr_fw_project_name				(addr_CFG_base + 0x10007014U)
	#define addr_fw_remark1						(addr_CFG_base + 0x10007020U)
	#define addr_fw_remark2						(addr_CFG_base + 0x1000702CU)
	#define addr_fw_ticket						(addr_CFG_base + 0x10007050U)
	#define addr_fw_HX_ID_EN					(addr_CFG_base + 0x10007134U)
	#define addr_fw_define_flash_reload			(addr_CFG_base + 0x10007F00U)
	#define addr_fw_define_rawdata_normalize	(addr_CFG_base + 0x10007130U)
	#define addr_fw_dbg_msg_addr				(addr_CFG_base + 0x10007F40U)
	#define addr_chk_fw_reload2					(addr_CFG_base + 0x100072C0U)
	#define addr_fw_setting_start				(addr_CFG_base + 0x10007084U)
	#define addr_fw_define_int_is_edge			(addr_CFG_base + 0x10007088U)
	#define addr_fw_define_chip_rx_tx_num		(addr_CFG_base + 0x100070F4U)
	#define addr_fw_define_maxpt				(addr_CFG_base + 0x100070F8U)
	#define addr_fw_define_total_rx_tx_rxic_num	(addr_CFG_base + 0x100071FCU)
	#define addr_fw_define_total_txic_num		(addr_CFG_base + 0x10007200U)
	#define addr_fw_define_xy_res				(addr_CFG_base + 0x100070FCU)
	#define addr_fw_setting_end					(addr_CFG_base + 0x10007204U)
	#define addr_fail_det_GPIO1_msg				(addr_CFG_base + 0x100074C0U)
	#define addr_fail_det_MainItem_checksum		0x10007500U
	#define addr_fail_det_IC_summary_status		0x10007504U
	#define addr_fail_det_MainItem_DD_Master	0x10007508U
	#define addr_fail_det_SubItem_checksum		0x100075D0U
	#define addr_fail_det_SubItem_DD_Master		0x100075D4U
	#define addr_SRAM_start_test				0x800204D8U
	#define addr_SRAM_set_voltage				0x900000F0U
	#define addr_SRAM_stress_num				0x900000F1U
	#define addr_SRAM_check_result				0x800204DCU
	#define addr_SRAM_fail_result				0x900000F4U
	#define addr_SRAM_amba_bus_reset			0x9000000CU
	#define addr_SRAM_safe_mode_release			0x90000098U
	#define addr_SRAM_WDTDIS					0x9000800cU
	#define addr_SRAM_reload_done				0x90000048U
	#define addr_SRAM_voltage_set				0x800204E0U
	#define addr_SRAM_test_option				0x900000FCU
	#define addr_SRAM_ilm_set					0x90000094U
	#define addr_SRAM_default_ivb_set			0x90000090U
/* WP control */
	#define addr_WP_pin_base					0x90028000U
	#define addr_WP_gpio0_cmd_04				(addr_WP_pin_base + 0x04U)
	#define addr_WP_gpio0_cmd_0C				(addr_WP_pin_base + 0x0CU)
	#define addr_WP_gpio0_cmd_B4				0x900880B4U
	#define data_WP_gpio0_cmd_00				0x00000000U
	#define data_WP_gpio0_cmd_01				0x00000001U
	#define addr_WP_gpio4_cmd_04				(addr_WP_pin_base + 0x04U)
	#define addr_WP_gpio4_cmd_1C				(addr_WP_pin_base + 0x1CU)
	#define addr_WP_gpio4_cmd_B4				0x900880B4U
	#define data_WP_gpio4_cmd_00				0x00000000U
	#define data_WP_gpio4_cmd_01				0x00000001U
	#define data_WP_gpio4_cmd_10				0x00000010U
	#define addr_WP_pin_HX83192D				0x90000230U
	#define data_WP_disable_HX83192D			0x00000003U
	#define data_WP_enable_HX83192D				0x00000002U
	#define addr_WP_pin_HX83193					0x900880B8U
	#define data_WP_disable_HX83193				0x00000001U
	#define data_WP_enable_HX83193				0x00000000U
	#define addr_WP_pin_HX83195					0x90000230U
	#define data_WP_disable_HX83195				0x00000003U
	#define data_WP_enable_HX83195				0x00000001U
	#define addr_WP_pin_HX83180					0x80000014U

/* MPAP test items usage */
	#define addr_mpap_rst_test					0x900000F0U
	#define addr_mpap_int_test_base				0x90028060U
	#define addr_mpap_int_test_cmd_1			(addr_mpap_int_test_base + 0x04U)
	#define addr_mpap_int_test_cmd_2			(addr_mpap_int_test_base + 0x08U)
	#define addr_mpap_fail_det_test				0x13007234U
	#define data_mpap_fail_det_test_L			0xA55A0000U
	#define data_mpap_fail_det_test_H			0xA55A0001U

/* BP control */
	#define addr_BP_lock_base					0x80000000U
	#define addr_BP_lock_cmd_10					(addr_BP_lock_base + 0x10U)
	#define addr_BP_lock_cmd_20					(addr_BP_lock_base + 0x20U)
	#define addr_BP_lock_cmd_24					(addr_BP_lock_base + 0x24U)
	#define addr_BP_lock_cmd_2C					(addr_BP_lock_base + 0x2CU)
	#define data_BP_lock_cmd_1					0x00020780U
	#define data_BP_lock_cmd_2					0x47000000U
	#define data_BP_lock_cmd_3					0x00000006U
	#define data_BP_lock_cmd_4					0x41000000U
	#define data_BP_lock_cmd_5					0x00000000U
	#define data_BP_lock_cmd_6					0x00000001U
	#define data_BP_lock_cmd_7					0x0000009CU
	#define data_BP_check_cmd_1					0x42000000U
	#define data_BP_check_cmd_2					0x00000005U
	#define data_BP_check_cmd_3					0x42000002U
	#define data_BP_check_cmd_4					0x0000009FU

/* CORE_FLASH */
	#define addr_ctrl_base						0x80000000U
	#define addr_spi200_trans_fmt				(addr_ctrl_base + 0x10U)
	#define addr_spi200_trans_ctrl				(addr_ctrl_base + 0x20U)
	#define addr_spi200_cmd						(addr_ctrl_base + 0x24U)
	#define addr_spi200_addr					(addr_ctrl_base + 0x28U)
	#define addr_spi200_data					(addr_ctrl_base + 0x2CU)
	#define addr_spi200_fifo_rst				(addr_ctrl_base + 0x30U)
	#define addr_spi200_flash_speed				(addr_ctrl_base + 0x40U)
	#define data_spi200_txfifo_rst				0x00000004U
	#define data_spi200_trans_fmt				0x00020780U
	#define data_spi200_trans_ctrl_1			0x42000003U
	#define data_spi200_trans_ctrl_2			0x47000000U
	#define data_spi200_trans_ctrl_3			0x67000000U
	#define data_spi200_trans_ctrl_4			0x610ff000U
	#define data_spi200_trans_ctrl_6			0x42000000U
	#define data_spi200_cmd_1					0x00000005U
	#define data_spi200_cmd_2					0x00000006U
	#define data_spi200_cmd_4					0x000000D8U
	#define data_spi200_cmd_6					0x00000002U
	#define data_set_flash_speed				0x00000201U

#define	Flash_list	{{0xEF, 0x30, 0x12}, {0xEF, 0x60, 0x12}, {0xEF, 0x70, 0x17},\
					{0xC8, 0x40, 0x12}, {0xC8, 0x40, 0x13}, {0xC8, 0x40, 0x14},\
					{0xC8, 0x60, 0x13}, {0xC8, 0x60, 0x12}, {0xC2, 0x20, 0x12},\
					{0xC2, 0x23, 0x12}, {0xC2, 0x23, 0x13}, {0xC2, 0x25, 0x32},\
					{0xC2, 0x28, 0x11}, {0xC2, 0x28, 0x12}, {0x85, 0x60, 0x13},\
					{0x85, 0x60, 0x12}, {0x85, 0x40, 0x12}, {0x7F, 0x11, 0x52},\
					{0x5E, 0x60, 0x13}, {0x1C, 0x38, 0x13}, {0x1C, 0x38, 0x12},\
					{0x9D, 0x40, 0x12}, {0x9D, 0x40, 0x13}, {0x9D, 0x60, 0x15} }

#if (HX_BOOT_UPGRADE == 0x01)
	extern uint16_t g_i_FW_VER;
	extern uint16_t g_i_CFG_VER;
	extern uint8_t g_i_CID_MAJ;
	extern uint8_t g_i_CID_MIN;
#endif

extern uint32_t dbg_reg_ary[10];


#if (HX_BOOT_UPGRADE == 0x01)
	extern const struct firmware *hxfw;
#endif

#if defined(KERNEL_VER_5_10)
	struct timespec64 time_diff(struct timespec64 start, struct timespec64 end);
#else
	struct timespec time_diff(struct timespec start, struct timespec end);
#endif

void himax_gpio_set(int pinnum, uint8_t value);

void himax_parse_assign_cmd(uint32_t addr, uint8_t *cmd, uint32_t len);
void himax_disable_flash_protected_mode(void);
void himax_enable_flash_protected_mode(void);
int himax_mcu_WP_BP_status(void);
void himax_mcu_register_write(uint32_t write_addr, uint32_t write_length, uint8_t *write_data);
void himax_mcu_register_read(uint32_t read_addr, uint32_t read_length, uint8_t *read_data);
void himax_mcu_register_write_slave(uint8_t device, uint32_t write_addr, uint32_t write_length, uint8_t *write_data);
void himax_mcu_register_read_slave(uint8_t device, uint32_t read_addr, uint32_t read_length, uint8_t *read_data);
void himax_mcu_register_write_all_slave(uint32_t write_addr, uint32_t write_length,uint8_t *write_data);
bool himax_mcu_tp_lcm_pin_reset(void);
void himax_mcu_burst_mode_enable(void);
void himax_mcu_burst_mode_disable(void);
void himax_mcu_interface_on(void);
bool himax_mcu_wait_wip(int Timing);
void himax_mcu_power_on_init(void);
void himax_mcu_init_psl(void);
void himax_mcu_write_dd_reg_password(uint8_t ic_device);
void himax_mcu_clear_dd_reg_password(uint8_t ic_device);
void himax_mcu_write_dd_reg_password_sram_test(uint8_t ic_device);
void himax_mcu_clear_dd_reg_password_sram_test(uint8_t ic_device);
void himax_mcu_dd_reg_write(uint8_t addr, uint8_t pa_num, uint8_t len, uint8_t *data, uint8_t bank, uint8_t ic_device);
void himax_mcu_dd_reg_read(uint8_t addr, uint8_t pa_num, uint8_t len, uint8_t *data, uint8_t bank, uint8_t ic_device);
void himax_mcu_system_reset(void);
void himax_mcu_command_reset(void);
uint32_t himax_mcu_calculate_CRC32_by_AP(unsigned char *FW_content, uint32_t len);
uint32_t himax_mcu_check_CRC(uint8_t *start_addr, unsigned int reload_length);
void himax_mcu_diag_register_set(uint8_t diag_command, uint8_t chip_id_sel);
int himax_write_read_reg(uint32_t addr_32, uint8_t *data, uint8_t hb, uint8_t lb);
void himax_mcu_config_reload_disable(void);
void himax_mcu_config_reload_enable(void);
#if !defined(CONFIG_TOUCHSCREEN_HIMAX_IC_HX83194)
void himax_mcu_rawdata_normalize_disable(int disable);
#endif
void himax_set_BS_UDT_frame(uint8_t checktype);
void himax_mcu_read_FW_ver(void);
void himax_print_define_function(void);
bool himax_mcu_read_event_stack(uint8_t *buf, uint8_t length);
void himax_mcu_stop_DSRAM_output(void);
bool himax_mcu_calculateChecksum(uint32_t size);
void himax_mcu_assign_sorting_mode(uint8_t *tmp_data);
void himax_mcu_sector_erase(uint32_t start_addr, uint32_t length);
bool himax_mcu_fts_ctpm_fw_upgrade(const u8 *fw_data, unsigned int bin_size);
bool himax_mcu_flash_lastdata_check_with_bin(uint32_t size);
bool hx_mcu_bin_desc_get(unsigned char *fw, uint32_t max_sz);
bool himax_mcu_get_DSRAM_data(uint8_t *tmp_rawdata);
int himax_mcu_fw_ver_bin(void);
void himax_mcu_hw_reset(bool int_off);
void himax_mcu_toggle_rst_gpio(void);
void himax_mcu_tp_reset(void);
void himax_mcu_touch_information(void);
void himax_mcu_check_cascade_ic_num(void);
unsigned int himax_mcu_cal_data_len(unsigned int raw_cnt_rmd, uint8_t HX_MAX_PT, unsigned int raw_cnt_max);
bool himax_mcu_diag_check_sum(struct himax_report_data *hx_touch_data_tmp);
void himax_mcu_diag_parse_raw_data(struct himax_report_data *hx_touch_data_tmp,
				   uint16_t mul_num, uint16_t self_num, uint8_t diag_cmd,
				   int16_t *mutual_data, int16_t *self_data);
void himax_mcu_read_FW_status(void);
void himax_mcu_check_sorting_mode(uint8_t *tmp_data);
void himax_mcu_check_N_frame(uint8_t *tmp_data);
void himax_mcu_irq_switch(int switch_on);
void himax_mcu_block_erase(uint32_t start_addr, uint32_t length);
int himax_mcu_SRAM_test(unsigned char *fw, int len);
int himax_mcu_SRAM_run_test_DTSRAM_function(uint8_t *write_buffer, uint8_t *read_buffer, uint8_t IC_num, int max_loop);
int himax_mcu_SRAM_run_test_ISRAM_function(uint8_t *write_buffer, uint8_t *read_buffer, uint8_t IC_num, int max_loop);
void himax_mcu_SRAM_bus_reset_function(void);
/*------------------------- function block -----------------------------------*/

#endif
