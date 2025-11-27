/* SPDX-License-Identifier: GPL-2.0 */
/*  Himax Android Driver Sample Code for QCT platform
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

#ifndef HIMAX_PLATFORM_H
#define HIMAX_PLATFORM_H

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>

struct himax_i2c_platform_data {
	uint32_t abs_x_min;
	uint32_t abs_x_max;
	uint32_t abs_x_fuzz;
	uint32_t abs_y_min;
	uint32_t abs_y_max;
	uint32_t abs_y_fuzz;
	uint32_t abs_pressure_min;
	uint32_t abs_pressure_max;
	uint32_t abs_pressure_fuzz;
	uint32_t abs_width_min;
	uint32_t abs_width_max;
	uint16_t screenWidth;
	uint16_t screenHeight;
	uint8_t protocol_type;
	int TSIX;
	int fail_det;
	int tp_ext_rstn;
	int PON;
	int RESX;
	int (*power)(int on);
	void (*reset)(void);
	int g_customer_control_tp_reset;
};

#define HIMAX_I2C_PLATFORM
#define HIMAX_I2C_RETRY_TIMES 3U
#define BUS_RW_MAX_LEN 256

#if defined(CONFIG_TOUCHSCREEN_HIMAX_DEBUG)
#define D(x...) pr_info("[HXTP][DEBUG] " x)
#define I(x...) pr_info("[HXTP] " x)
#define W(x...) pr_warn("[HXTP][WARNING] " x)
#define E(x...) pr_err("[HXTP][ERROR] " x)
#define DIF(x...)                                                              \
	do {                                                                   \
		if (debug_flag)                                                \
			pr_debug("[HXTP][DEBUG] " x)                           \
	} while (0)
#else

#define D(x...)
#define I(x...)
#define W(x...)
#define E(x...)
#define DIF(x...)
#endif

#define HIMAX_common_NAME "himax_tp"
#define INPUT_DEV_NAME "himax-touchscreen"

extern int himax_bus_read(uint8_t command, uint8_t *data, uint32_t length,
			  uint8_t toRetry);
extern int himax_bus_write(uint8_t command, uint8_t *data, uint32_t length,
			   uint8_t toRetry);
extern int himax_bus_read_slave(uint8_t device, uint8_t command, uint8_t *data, uint32_t length,
			  uint8_t toRetry);
extern int himax_bus_write_slave(uint8_t device, uint8_t command, uint8_t *data, uint32_t length,
			   uint8_t toRetry);
extern void himax_int_enable(int enable);
extern void himax_fail_det_enable(int enable);
extern int himax_ts_register_interrupt(void);
extern int himax_fail_det_register_interrupt(void);
extern int himax_gpio_power_config(struct himax_i2c_platform_data *pdata);
extern int himax_interrupt_gpio_config(struct himax_i2c_platform_data *pdata);
#if defined(HX_CONFIG_FB)
extern int fb_notifier_callback(struct notifier_block *self,
				unsigned long event, void *data);
#elif defined(HX_CONFIG_DRM)
extern int drm_notifier_callback(struct notifier_block *self,
				 unsigned long event, void *data);
#endif
extern void himax_fail_det_work(void);
extern enum hrtimer_restart himax_ts_timer_func(struct hrtimer *timer);
extern int himax_chip_common_init(void);
extern void himax_chip_common_deinit(void);
extern void himax_gpio_set(int pinnum, uint8_t value);
extern int himax_int_en_set(void);
extern int himax_ts_unregister_interrupt(void);
uint8_t himax_int_gpio_read(int pinnum);
void himax_gpio_power_deconfig(struct himax_i2c_platform_data *pdata);
irqreturn_t himax_ts_thread(int irq, void *ptr);
irqreturn_t himax_fail_det_thread(int irq, void *ptr);


#endif
