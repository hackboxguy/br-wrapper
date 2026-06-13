// SPDX-License-Identifier: GPL-2.0
/*  Himax Android Driver Sample Code for QCT platform
 *
 *  Copyright (C) 2021 Himax Corporation.
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

#include <linux/version.h>
#include "himax_platform.h"
#include "himax_common.h"

/*------------------------- define block -------------------------------------*/
/*------------------------- define block -------------------------------------*/
/*------------------------- parameter block ----------------------------------*/

static uint8_t *gp_rw_buf;
static int g_tp_ext_rstn;

/*------------------------- parameter block ----------------------------------*/
/*------------------------- structure block ----------------------------------*/
/*------------------------- structure block ----------------------------------*/
int himax_bus_read(uint8_t command, uint8_t *data, uint32_t length,
		   uint8_t toRetry);
int himax_bus_write(uint8_t command, uint8_t *data, uint32_t length,
		    uint8_t toRetry);
int himax_bus_read_slave(uint8_t device, uint8_t command, uint8_t *data, uint32_t length,
		   uint8_t toRetry);
int himax_bus_write_slave(uint8_t device, uint8_t command, uint8_t *data, uint32_t length,
		    uint8_t toRetry);
void himax_int_enable(int enable);
void himax_fail_det_enable(int enable);
void himax_gpio_set(int pinnum, uint8_t value);
uint8_t himax_int_gpio_read(int pinnum);
int himax_interrupt_gpio_config(struct himax_i2c_platform_data *pdata);
int himax_gpio_power_config(struct himax_i2c_platform_data *pdata);
void himax_gpio_power_deconfig(struct himax_i2c_platform_data *pdata);
irqreturn_t himax_ts_thread(int irq, void *ptr);
irqreturn_t himax_fail_det_thread(int irq, void *ptr);
int himax_int_en_set(void);
int himax_fail_det_register_interrupt(void);
int himax_ts_register_interrupt(void);
int himax_ts_unregister_interrupt(void);

#if (HX_RST_PIN_FUNC == 0x01)
static void himax_tp_ext_rstn_reset(void)
{
	if (g_tp_ext_rstn > 0) {
		gpio_direction_output(g_tp_ext_rstn, 0);
		msleep(20);
		gpio_direction_output(g_tp_ext_rstn, 1);
		msleep(50);
	} else {
		E("%s: Invalid TP RST pin control\n", __func__);
	}
}
#endif
int himax_dev_set(struct himax_ts_data *ts)
{
	int ret = 0;

	ts->input_dev = input_allocate_device();

	if (ts->input_dev == NULL) {
		ret = -ENOMEM;
		E("%s: Failed to allocate input device-input_dev\n", __func__);
		return ret;
	}

	ts->input_dev->name = "himax-touchscreen";

	return ret;
}
int himax_input_register_device(struct input_dev *input_dev)
{
	return input_register_device(input_dev);
}

int himax_parse_dt(struct himax_ts_data *ts,
		   struct himax_i2c_platform_data *pdata)
{
	struct device_node *dt = private_ts->client->dev.of_node;
	u32 data = 0;
	uint32_t coords[4] = { 0 };
	int rc = 0;
	int coords_size = 0;
	int ret = 0;
	struct property *prop;

	UNUSED(ts);

	prop = of_find_property(dt, "himax,panel-coords", NULL);
	if (prop != NULL) {
		coords_size = prop->length / sizeof(u32);
		if (coords_size != 4) {
			D(" %s:Invalid panel coords size %d\n", __func__,
			  coords_size);
		}
	}
	ret = of_property_read_u32_array(dt, "himax,panel-coords", coords,
					 coords_size);
	if (ret == 0) {
		pdata->abs_x_min = coords[0];
		pdata->abs_x_max = coords[1];
		pdata->abs_y_min = coords[2];
		pdata->abs_y_max = coords[3];
		I(" DT:panel-coords = %d, %d, %d, %d\n", pdata->abs_x_min,
		  pdata->abs_x_max, pdata->abs_y_min, pdata->abs_y_max);
	}
	prop = of_find_property(dt, "himax,display-coords", NULL);
	if (prop != NULL) {
		coords_size = prop->length / sizeof(u32);
		if (coords_size != 4) {
			D(" %s:Invalid display coords size %d\n", __func__,
			  coords_size);
		}
	}
	rc = of_property_read_u32_array(dt, "himax,display-coords", coords,
					coords_size);
	if ((rc != 0) && (rc != -EINVAL)) {
		D(" %s:Fail to read display-coords %d\n", __func__, rc);
		return rc;
	}
	pdata->screenWidth = (uint16_t)coords[1];
	pdata->screenHeight = (uint16_t)coords[3];
	I(" DT:display-coords = (%d, %d)\n", pdata->screenWidth,
	  pdata->screenHeight);

	pdata->TSIX = of_get_named_gpio(dt, "himax,TSIX", 0);

	if (!gpio_is_valid(pdata->TSIX)) {
		I(" DT:TSIX value is not valid\n");
	}

	pdata->fail_det = of_get_named_gpio(dt, "himax,FAIL-DET", 0);

	if (!gpio_is_valid(pdata->fail_det)) {
		I(" DT:FAIL-DET value is not valid\n");
	}

	pdata->tp_ext_rstn = of_get_named_gpio(dt, "himax,TP-EXT-RSTN", 0);
	g_tp_ext_rstn = pdata->tp_ext_rstn;
	if (!gpio_is_valid(pdata->tp_ext_rstn)) {
		I(" DT:TP-EXT-RSTN value is not valid\n");
	}

	pdata->PON = of_get_named_gpio(dt, "himax,PON", 0);

	if (!gpio_is_valid(pdata->PON)) {
		I(" DT:PON value is not valid\n");
	}

	pdata->RESX = of_get_named_gpio(dt, "himax,RESX", 0);

	if (!gpio_is_valid(pdata->RESX)) {
		I(" DT:RESX value is not valid\n");
	}

	I(" DT:PON=%d, RESX=%d\n", pdata->PON, pdata->RESX);
	I(" DT:TSIX=%d, TP-EXT-RSTN=%d\n", pdata->TSIX, pdata->tp_ext_rstn);
	I(" DT:FAIL-DET=%d", pdata->fail_det);

	if (of_property_read_u32(dt, "report_type", &data) == 0) {
		pdata->protocol_type = (uint8_t) data;
		I(" DT:protocol_type=%d\n", pdata->protocol_type);
	}

	return 0;
}
EXPORT_SYMBOL(himax_parse_dt);

#if (HIMAX_LTDI_CONFIG == 0x01) && \
    ((HIMAX_PRODUCT_TYPE == 0x83194B) || (HIMAX_PRODUCT_TYPE == 0x83195B))
int himax_bus_write(uint8_t command, uint8_t *data, uint32_t length,
		    uint8_t toRetry)
{
	uint8_t retry;
	int ret = 0;
	struct i2c_client *client;
	struct i2c_msg msg[] = { { 0 } };

	if (private_ts == NULL || private_ts->client == NULL || gp_rw_buf == NULL) {
		E("%s: not initialized\n", __func__);
		return -EIO;
	}
	client = private_ts->client;
	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = length + 2U;
	msg[0].buf = gp_rw_buf;

	mutex_lock(&private_ts->rw_lock);
	gp_rw_buf[0] = 0xC0U;
	gp_rw_buf[1] = command;
	if (data != NULL) {
		(void)memcpy(&gp_rw_buf[2], data, length);
	}

	for (retry = 0; retry < toRetry; retry++) {
		ret = i2c_transfer(client->adapter, msg, 1);
		if (ret == 1) {
			break;
		}
		msleep(20);
	}

	if (retry == toRetry) {
		E("%s: i2c_write_block retry over %d\n", __func__, toRetry);
		mutex_unlock(&private_ts->rw_lock);
#if (HX_RST_PIN_FUNC == 0x01)
		himax_tp_ext_rstn_reset();
#endif
		return -EIO;
	}

	mutex_unlock(&private_ts->rw_lock);
	return 0;
}
EXPORT_SYMBOL(himax_bus_write);

int himax_bus_write_slave(uint8_t device, uint8_t command, uint8_t *data, uint32_t length,
		    uint8_t toRetry)
{
	uint8_t retry;
	int ret = 0;
	struct i2c_client *client;
	struct i2c_msg msg[] = { { 0 } };

	if (private_ts == NULL || private_ts->client == NULL || gp_rw_buf == NULL) {
		E("%s: not initialized\n", __func__);
		return -EIO;
	}
	client = private_ts->client;
	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = length + 2U;
	msg[0].buf = gp_rw_buf;

	mutex_lock(&private_ts->rw_lock);
	gp_rw_buf[0] = (0xC0U | device);
	gp_rw_buf[1] = command;
	if (data != NULL) {
		(void)memcpy(&gp_rw_buf[2], data, length);
	}

	for (retry = 0; retry < toRetry; retry++) {
		ret = i2c_transfer(client->adapter, msg, 1);
		if (ret == 1) {
			break;
		}
		/*msleep(20);*/
	}

	if (retry == toRetry) {
		E("%s: i2c_write_block retry over %d\n", __func__, toRetry);
		mutex_unlock(&private_ts->rw_lock);
#if (HX_RST_PIN_FUNC == 0x01)
		himax_tp_ext_rstn_reset();
#endif
		return -EIO;
	}

	mutex_unlock(&private_ts->rw_lock);
	return 0;
}
EXPORT_SYMBOL(himax_bus_write_slave);

int himax_bus_read(uint8_t command, uint8_t *data, uint32_t length,
		   uint8_t toRetry)
{
	uint8_t retry;
	int ret = 0;
	uint8_t SID_cmd[2] = {0xC0U, command};
	struct i2c_client *client;
	struct i2c_msg msg[2];

	if (private_ts == NULL || private_ts->client == NULL || gp_rw_buf == NULL) {
		E("%s: not initialized\n", __func__);
		return -EIO;
	}
	client = private_ts->client;
	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 2U;
	msg[0].buf = SID_cmd;
	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = length;
	msg[1].buf = gp_rw_buf;

	mutex_lock(&private_ts->rw_lock);

	for (retry = 0; retry < toRetry; retry++) {
		ret = i2c_transfer(client->adapter, msg, 2);
		if (ret == 2) {
			(void)memcpy(data, gp_rw_buf, length);
			break;
		}
		/*msleep(20);*/
	}

	if (retry == toRetry) {
		E("%s: i2c_read_block retry over %d\n", __func__, toRetry);
		mutex_unlock(&private_ts->rw_lock);
#if (HX_RST_PIN_FUNC == 0x01)
		himax_tp_ext_rstn_reset();
#endif
		return -EIO;
	}

	mutex_unlock(&private_ts->rw_lock);
	return 0;
}
EXPORT_SYMBOL(himax_bus_read);

int himax_bus_read_slave(uint8_t device, uint8_t command, uint8_t *data, uint32_t length,
		   uint8_t toRetry)
{
	uint8_t retry;
	int ret = 0;
	uint8_t SID_cmd[2] = {(0xC0U | device), command};
	struct i2c_client *client;
	struct i2c_msg msg[2];

	if (private_ts == NULL || private_ts->client == NULL || gp_rw_buf == NULL) {
		E("%s: not initialized\n", __func__);
		return -EIO;
	}
	client = private_ts->client;
	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = SID_cmd;
	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = length;
	msg[1].buf = gp_rw_buf;

	mutex_lock(&private_ts->rw_lock);

	for (retry = 0; retry < toRetry; retry++) {
		ret = i2c_transfer(client->adapter, msg, 2);
		if (ret == 2) {
			(void)memcpy(data, gp_rw_buf, length);
			break;
		}
		/*msleep(20);*/
	}

	if (retry == toRetry) {
		if (toRetry == HIMAX_I2C_RETRY_TIMES) {
			E("%s: i2c_read_block retry over %d\n", __func__, toRetry);
			mutex_unlock(&private_ts->rw_lock);
#if (HX_RST_PIN_FUNC == 0x01)
			himax_tp_ext_rstn_reset();
#endif
		} else {
			mutex_unlock(&private_ts->rw_lock);
		}
		return -EIO;
	}

	mutex_unlock(&private_ts->rw_lock);
	return 0;
}
EXPORT_SYMBOL(himax_bus_read_slave);

#else
int himax_bus_write(uint8_t command, uint8_t *data, uint32_t length,
		    uint8_t toRetry)
{
	uint8_t retry;
	int ret = 0;
	struct i2c_client *client;
	struct i2c_msg msg[] = { { 0 } };

	if (private_ts == NULL || private_ts->client == NULL || gp_rw_buf == NULL) {
		E("%s: not initialized\n", __func__);
		return -EIO;
	}
	client = private_ts->client;
	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = length + 1U;
	msg[0].buf = gp_rw_buf;

	mutex_lock(&private_ts->rw_lock);
	gp_rw_buf[0] = command;
	if (data != NULL) {
		(void)memcpy(&gp_rw_buf[1], data, length);
	}

	for (retry = 0; retry < toRetry; retry++) {
		ret = i2c_transfer(client->adapter, msg, 1);
		if (ret == 1) {
			break;
		}
		msleep(20);
	}

	if (retry == toRetry) {
		E("%s: i2c_write_block retry over %d\n", __func__, toRetry);
		mutex_unlock(&private_ts->rw_lock);
#if (HX_RST_PIN_FUNC == 0x01)
		himax_tp_ext_rstn_reset();
#endif
		return -EIO;
	}

	mutex_unlock(&private_ts->rw_lock);
	return 0;
}
EXPORT_SYMBOL(himax_bus_write);

int himax_bus_write_slave(uint8_t device, uint8_t command, uint8_t *data, uint32_t length,
		    uint8_t toRetry)
{
	uint8_t retry;
	int ret = 0;
	struct i2c_client *client;
	struct i2c_msg msg[] = { { 0 } };

	if (private_ts == NULL || private_ts->client == NULL || gp_rw_buf == NULL) {
		E("%s: not initialized\n", __func__);
		return -EIO;
	}
	client = private_ts->client;
	msg[0].addr = (client->addr+device);
	msg[0].flags = 0;
	msg[0].len = length + 1U;
	msg[0].buf = gp_rw_buf;

	mutex_lock(&private_ts->rw_lock);
	gp_rw_buf[0] = command;
	if (data != NULL) {
		(void)memcpy(&gp_rw_buf[1], data, length);
	}

	for (retry = 0; retry < toRetry; retry++) {
		ret = i2c_transfer(client->adapter, msg, 1);
		if (ret == 1) {
			break;
		}
		/*msleep(20);*/
	}

	if (retry == toRetry) {
		E("%s: i2c_write_block retry over %d\n", __func__, toRetry);
		mutex_unlock(&private_ts->rw_lock);
#if (HX_RST_PIN_FUNC == 0x01)
		himax_tp_ext_rstn_reset();
#endif
		return -EIO;
	}

	mutex_unlock(&private_ts->rw_lock);
	return 0;
}
EXPORT_SYMBOL(himax_bus_write_slave);

int himax_bus_read(uint8_t command, uint8_t *data, uint32_t length,
		   uint8_t toRetry)
{
	uint8_t retry;
	int ret = 0;
	struct i2c_client *client;
	struct i2c_msg msg[2];

	if (private_ts == NULL || private_ts->client == NULL || gp_rw_buf == NULL) {
		E("%s: not initialized\n", __func__);
		return -EIO;
	}
	client = private_ts->client;
	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &command;
	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = length;
	msg[1].buf = gp_rw_buf;

	mutex_lock(&private_ts->rw_lock);

	for (retry = 0; retry < toRetry; retry++) {
		ret = i2c_transfer(client->adapter, msg, 2);
		if (ret == 2) {
			(void)memcpy(data, gp_rw_buf, length);
			break;
		}
		/*msleep(20);*/
	}

	if (retry == toRetry) {
		E("%s: i2c_read_block retry over %d\n", __func__, toRetry);
		mutex_unlock(&private_ts->rw_lock);
#if (HX_RST_PIN_FUNC == 0x01)
		himax_tp_ext_rstn_reset();
#endif
		return -EIO;
	}

	mutex_unlock(&private_ts->rw_lock);
	return 0;
}
EXPORT_SYMBOL(himax_bus_read);

int himax_bus_read_slave(uint8_t device, uint8_t command, uint8_t *data, uint32_t length,
		   uint8_t toRetry)
{
	uint8_t retry;
	int ret = 0;
	struct i2c_client *client;
	struct i2c_msg msg[2];

	if (private_ts == NULL || private_ts->client == NULL || gp_rw_buf == NULL) {
		E("%s: not initialized\n", __func__);
		return -EIO;
	}
	client = private_ts->client;
	msg[0].addr = (client->addr+device);
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &command;
	msg[1].addr = (client->addr+device);
	msg[1].flags = I2C_M_RD;
	msg[1].len = length;
	msg[1].buf = gp_rw_buf;

	mutex_lock(&private_ts->rw_lock);

	for (retry = 0; retry < toRetry; retry++) {
		ret = i2c_transfer(client->adapter, msg, 2);
		if (ret == 2) {
			(void)memcpy(data, gp_rw_buf, length);
			break;
		}
		/*msleep(20);*/
	}

	if (retry == toRetry) {
		if (toRetry == HIMAX_I2C_RETRY_TIMES) {
			E("%s: i2c_read_block retry over %d\n", __func__, toRetry);
			mutex_unlock(&private_ts->rw_lock);
#if (HX_RST_PIN_FUNC == 0x01)
			himax_tp_ext_rstn_reset();
#endif
		} else {
			mutex_unlock(&private_ts->rw_lock);
		}
		return -EIO;
	}

	mutex_unlock(&private_ts->rw_lock);
	return 0;
}
EXPORT_SYMBOL(himax_bus_read_slave);

#endif

#if defined(Tp_inspect_mode_patch)
static void himax_mcu_clear_inspect_mode(void)
{
	uint8_t cmd_byte[8] = {0x54, 0x74, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00};
	uint8_t cmd_byte_2[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00};
	uint8_t data_byte[4] = {0xFF, 0xFF, 0xFF, 0xFF};
	uint8_t i = 0;
	int ret = 0;

	cmd_byte_2[0] = addr_Tp_inspect_mode_cmd_current & 0xFFU;
	cmd_byte_2[1] = (addr_Tp_inspect_mode_cmd_current >> 8U) & 0xFFU;
	cmd_byte_2[2] = (addr_Tp_inspect_mode_cmd_current >> 16U) & 0xFFU;
	cmd_byte_2[3] = (addr_Tp_inspect_mode_cmd_current >> 24U) & 0xFFU;

	ret = himax_bus_read(0x80, data_byte, 4,
		HIMAX_I2C_RETRY_TIMES);

	/*inspect mode initial*/
	do {
		ret = himax_bus_write(0x00, cmd_byte, 8,
			HIMAX_I2C_RETRY_TIMES);
		if (ret < 0) {
			E("%s: i2c access fail!\n", __func__);
		}
		usleep_range(1000, 1100);

		ret = himax_bus_write(0x00, cmd_byte, 4,
			HIMAX_I2C_RETRY_TIMES);
		data_byte[0] = 0x00;
		ret = himax_bus_write(0x0C, data_byte, 1,
			HIMAX_I2C_RETRY_TIMES);
		ret = himax_bus_read(0x08, data_byte, 4,
			HIMAX_I2C_RETRY_TIMES);

		I("%s: addr 10007454: %02X%02X%02X%02X\n", __func__,
			data_byte[3], data_byte[2], data_byte[1], data_byte[0]);

	} while ((i++ < 28U) && (data_byte[3] != 0U));

	(void)memset(data_byte, 0xFF, 4);
	i = 0;
	/*clear addr_Tp_inspect_mode_cmd_current*/
	do {
		ret = himax_bus_write(0x00, cmd_byte_2, 8,
			HIMAX_I2C_RETRY_TIMES);
		if (ret < 0) {
			E("%s: i2c access fail!\n", __func__);
		}
		usleep_range(1000, 1100);

		ret = himax_bus_write(0x00, cmd_byte_2, 4,
			HIMAX_I2C_RETRY_TIMES);
		data_byte[0] = 0x00;
		ret = himax_bus_write(0x0C, data_byte, 1,
			HIMAX_I2C_RETRY_TIMES);
		ret = himax_bus_read(0x08, data_byte, 4,
			HIMAX_I2C_RETRY_TIMES);

		I("%s: addr %08X: %02X%02X%02X%02X\n", __func__, addr_Tp_inspect_mode_cmd_current,
			data_byte[3], data_byte[2], data_byte[1], data_byte[0]);

	} while ((i++ < 28U) && (data_byte[3] != 0U));

	I("%s: DONE!\n", __func__);
}
#endif
void himax_int_enable(int enable)
{
	struct himax_ts_data *ts = private_ts;
	unsigned long irqflags = 0;
	int irqnum = ts->client->irq;

	if (enable == 1) {
		himax_mcu_clear_event_stack();
	}

	spin_lock_irqsave(&ts->irq_lock, irqflags);
	I("%s: Entering!\n", __func__);
	if ((enable == 1) && (atomic_read(&ts->irq_state) == 0)) {
		atomic_set(&ts->irq_state, 1);
		enable_irq(irqnum);
		private_ts->irq_enabled = true;
	} else if ((enable == 0) && (atomic_read(&ts->irq_state) == 1)) {
		atomic_set(&ts->irq_state, 0);
		disable_irq_nosync(irqnum);
		private_ts->irq_enabled = false;
	} else {
		/* do nothing*/
	}

	I("enable = %d\n", enable);
	spin_unlock_irqrestore(&ts->irq_lock, irqflags);
}
EXPORT_SYMBOL(himax_int_enable);

void himax_fail_det_enable(int enable)
{
	struct himax_ts_data *ts = private_ts;
	unsigned long faildet_flags = 0;

	spin_lock_irqsave(&ts->fail_det_lock, faildet_flags);
	I("%s: Entering!\n", __func__);
	if ((enable == 1) && (atomic_read(&ts->fail_det_state) == 0)) {
		atomic_set(&ts->fail_det_state, 1);
		enable_irq(ts->hx_fail_det);
	} else if ((enable == 0) && (atomic_read(&ts->fail_det_state) == 1)) {
		atomic_set(&ts->fail_det_state, 0);
		disable_irq_nosync(ts->hx_fail_det);
	} else {
		/* do nothing*/
	}

	I("enable = %d\n", enable);
	spin_unlock_irqrestore(&ts->fail_det_lock, faildet_flags);
}
EXPORT_SYMBOL(himax_fail_det_enable);

void himax_gpio_set(int pinnum, uint8_t value)
{
	gpio_direction_output(pinnum, value);
}
EXPORT_SYMBOL(himax_gpio_set);


uint8_t himax_int_gpio_read(int pinnum)
{
	return gpio_get_value(pinnum);
}

int himax_interrupt_gpio_config(struct himax_i2c_platform_data *pdata)
{
	int ret = 0;
	struct i2c_client *client = private_ts->client;

	if (gpio_is_valid(pdata->TSIX) != 0) {
		ret = gpio_request(pdata->TSIX, "TSIX");
		if (ret != 0) {
			E("unable to request TSIX [%d]\n", pdata->TSIX);
			goto err_TSIX_req;
		}
		ret = gpio_direction_input(pdata->TSIX);
		if (ret != 0) {
			E("unable to set direction for TSIX [%d]\n",
				pdata->TSIX);
			goto err_TSIX_set_input;
		}
		client->irq = gpio_to_irq(pdata->TSIX);
		if (client->irq < 0) {
			E("fail to request IRQ: %d\n", client->irq);
		}
		private_ts->hx_irq = client->irq;
	} else {
		E("TSIX gpio not provided\n");
		goto err_TSIX_req;
	}
	if (gpio_is_valid(pdata->fail_det) != 0) {
		ret = gpio_request(pdata->fail_det, "FAIL-DET");
		if (ret != 0) {
			E("unable to request FAIL-DET [%d]\n", pdata->fail_det);
		}
		ret = gpio_direction_input(pdata->fail_det);
		if (ret != 0) {
			E("unable to set direction for FAIL-DET [%d]\n",
			  pdata->fail_det);
		}
		private_ts->hx_fail_det = gpio_to_irq(pdata->fail_det);
	} else {
		I("FAIL-DET not provided\n");
	}
	return ret;

err_TSIX_set_input:
	if (gpio_is_valid(pdata->TSIX) != 0) {
		gpio_free(pdata->TSIX);
	}
err_TSIX_req:
	return ret;
}

int himax_gpio_power_config(struct himax_i2c_platform_data *pdata)
{
	int error = 0;

#if (HX_RST_PIN_FUNC == 0x01)
	if (pdata->tp_ext_rstn >= 0) {
		error = gpio_request(pdata->tp_ext_rstn, "tp_ext_rstn");

		if (error < 0) {
			E("%s: request tp-reset pin failed\n", __func__);
			goto err_tp_ext_rstn_req;
		}

		error = gpio_direction_output(pdata->tp_ext_rstn, 0);

		if (error != 0) {
			E("unable to set direction for tp-reset [%d]\n",
			  pdata->tp_ext_rstn);
			goto err_tp_ext_rstn_dir;
		}
		usleep_range(10000, 11000);
		if (gpio_get_value(pdata->tp_ext_rstn) == 1) {
			E("unable control TP_EXT_RSTN, please check it\n");
			pdata->g_customer_control_tp_reset = 1;
		} else {
			pdata->g_customer_control_tp_reset = 0;
		}
	}
#elif (HX_RST_PIN_FUNC == 0x02)
	/* Need Customer pull TP reset pin low */
#endif

	if (pdata->RESX >= 0) {
		error = gpio_request(pdata->RESX, "RESX");

		if (error < 0) {
			E("%s: request RESX pin failed\n", __func__);
			goto err_RESX_req;
		}

		error = gpio_direction_output(pdata->RESX, 0);
		if (error != 0) {
			E("unable to set direction for RESX [%d]\n",
			  pdata->RESX);
			goto err_RESX_dir;
		}
	}

	if (gpio_is_valid(pdata->PON) != 0) {
		error = gpio_request(pdata->PON, "PON");

		if (error != 0) {
			E("unable to request PON [%d]\n", pdata->PON);
			goto err_PON_req;
		}

		error = gpio_direction_output(pdata->PON, 0);

		if (error != 0) {
			E("unable to set direction for PON [%d]\n", pdata->PON);
			goto err_PON_dir;
		}
	}

	usleep_range(6000, 6100);

	if (pdata->RESX >= 0) {
		error = gpio_direction_output(pdata->RESX, 1);

		if (error != 0) {
			E("RESX unable to set direction for RESX [%d]\n",
			  pdata->RESX);
			goto err_lcm_reset_set_high;
		}
	}
	usleep_range(1000, 1100);

#if (HX_RST_PIN_FUNC == 0x01)

	if (pdata->tp_ext_rstn >= 0) {
		error = gpio_direction_output(pdata->tp_ext_rstn, 1);

		if (error != 0) {
			E("unable to set direction for tp_ext_rstn [%d]\n",
			  pdata->tp_ext_rstn);
			goto err_tp_ext_rstn_set_high;
		}
	}
#elif (HX_RST_PIN_FUNC == 0x02)
	/* Need Customer pull TP reset pin high */
#endif
#if defined(Tp_inspect_mode_patch)
	himax_mcu_clear_inspect_mode();
#endif
	msleep(95);

	if (gpio_is_valid(pdata->PON) != 0) {
		error = gpio_direction_output(pdata->PON, 1);

		if (error != 0) {
			E("PON unable to set direction for PON [%d]\n",
			  pdata->PON);
			goto err_PON_set_high;
		}
	}
	I("%s: DONE!\n", __func__);
	return error;

err_PON_set_high:
#if (HX_RST_PIN_FUNC == 0x01)
err_tp_ext_rstn_set_high:
#endif
err_lcm_reset_set_high:
err_PON_dir:
	if (gpio_is_valid(pdata->PON) != 0) {
		gpio_free(pdata->PON);
	}
err_PON_req:
err_RESX_dir:
	if (gpio_is_valid(pdata->RESX) != 0) {
		gpio_free(pdata->RESX);
	}
err_RESX_req:

#if (HX_RST_PIN_FUNC == 0x01)
err_tp_ext_rstn_dir:
	if (pdata->tp_ext_rstn >= 0) {
		gpio_free(pdata->tp_ext_rstn);
	}
err_tp_ext_rstn_req:
#endif
	return error;
}

void himax_gpio_power_deconfig(struct himax_i2c_platform_data *pdata)
{
	int error = 0;

	if (gpio_is_valid(pdata->PON) != 0) {
		error = gpio_direction_output(pdata->PON, 0);

		if (error != 0) {
			E("unable to set direction for PON [%d]\n", pdata->PON);
		}
	}

#if (HX_RST_PIN_FUNC == 0x01)
	if (pdata->tp_ext_rstn >= 0) {
		error = gpio_direction_output(pdata->tp_ext_rstn, 0);

		if (error != 0) {
			E("unable to set direction for tp-reset [%d]\n",
			  pdata->tp_ext_rstn);
		}
	}
#elif (HX_RST_PIN_FUNC == 0x02)
	/* Need Customer pull TP reset pin low */
#endif

	if (pdata->RESX >= 0) {
		error = gpio_direction_output(pdata->RESX, 0);
		if (error != 0) {
			E("unable to set direction for RESX [%d]\n",
			  pdata->RESX);
		}
	}

	if (gpio_is_valid(pdata->TSIX) != 0) {
		gpio_free(pdata->TSIX);
	}
	if (gpio_is_valid(pdata->fail_det) != 0) {
		gpio_free(pdata->fail_det);
	}
#if (HX_RST_PIN_FUNC == 0x01)
	if (gpio_is_valid(pdata->tp_ext_rstn) != 0) {
		gpio_free(pdata->tp_ext_rstn);
	}
#endif
	if (gpio_is_valid(pdata->PON) != 0) {
		gpio_free(pdata->PON);
	}
	if (gpio_is_valid(pdata->RESX) != 0) {
		gpio_free(pdata->RESX);
	}
}

static void himax_ts_isr_func(struct himax_ts_data *ts)
{
	himax_ts_work(ts);
}

irqreturn_t himax_ts_thread(int irq, void *ptr)
{
	UNUSED(irq);

	himax_ts_isr_func((struct himax_ts_data *)ptr);

	return IRQ_HANDLED;
}

void himax_ts_work_func(struct work_struct *work)
{
	struct himax_ts_data *ts =
		container_of(work, struct himax_ts_data, work);

	himax_ts_work(ts);
}

irqreturn_t himax_fail_det_thread(int irq, void *ptr)
{
	UNUSED(irq);
	UNUSED(ptr);

	himax_fail_det_work();

	return IRQ_HANDLED;
}

static int himax_int_register_trigger(void)
{
	int ret = 0;
	struct himax_ts_data *ts = private_ts;
	struct i2c_client *client = private_ts->client;

	if (ic_data->HX_INT_IS_EDGE) {
		/* FPDLink REM_INTB forwarding degrades edge signals — noisy edges
		 * cause false triggers, checksum errors, and polling fallback.
		 * Force level trigger for reliable operation through FPDLink.
		 */
		I("%s TDDI reports edge trigger, overriding to level trigger low (FPDLink)\n", __func__);
		ic_data->HX_INT_IS_EDGE = false;
	}

	I("%s level trigger low\n", __func__);
	ret = request_threaded_irq(client->irq, NULL, himax_ts_thread,
				   IRQF_TRIGGER_LOW | IRQF_ONESHOT,
				   client->name, ts);

	return ret;
}

static int himax_fail_det_int_register_trigger(void)
{
	int ret = 0;
	struct himax_ts_data *ts = private_ts;
	const char *hx_fail_det_name = "hx_fail_det";

	I("%s edge trigger rising\n", __func__);
	ret = request_threaded_irq(ts->hx_fail_det, NULL, himax_fail_det_thread,
				   IRQF_TRIGGER_RISING | IRQF_ONESHOT,
				   hx_fail_det_name, (void *)0);

	return ret;
}

int himax_int_en_set(void)
{
	int ret = NO_ERR;

	ret = himax_int_register_trigger();
	return ret;
}

int himax_fail_det_register_interrupt(void)
{
	int ret = 0;
	struct himax_ts_data *ts = private_ts;

	if (ts->hx_fail_det > 0) {
		ret = himax_fail_det_int_register_trigger();

		if (ret == 0) {
			atomic_set(&ts->fail_det_state, 1);
			I("%s: fail_det enabled at gpio: %d\n", __func__,
			  ts->hx_fail_det);
		} else {
			E("%s: request fail_det failed\n", __func__);
		}

	} else {
		I("%s: hx_fail_det is empty.\n", __func__);
	}

	return ret;
}

int himax_ts_register_interrupt(void)
{
	struct himax_ts_data *ts = private_ts;
	struct i2c_client *client = private_ts->client;
	int ret = 0;

	ts->irq_enabled = false;

	/* Work functon */
	if (client->irq && (private_ts->hx_irq > 0)) { /*INT mode*/
		ts->use_irq = true;
		ret = himax_int_register_trigger();

		if (ret == 0) {
			ts->irq_enabled = true;
			atomic_set(&ts->irq_state, 1);
			I("%s: irq enabled at gpio: %d\n", __func__,
			  client->irq);
		} else {
			ts->use_irq = false;
			E("%s: request_irq failed\n", __func__);
		}
	} else {
		I("%s: client->irq is empty, use polling mode.\n", __func__);
	}

	/*if use polling mode need to disable HX_ESD_RECOVERY function*/
	if (!ts->use_irq) {
		ts->himax_wq = create_singlethread_workqueue("himax_touch");
		INIT_WORK(&ts->work, himax_ts_work_func);
		hrtimer_init(&ts->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		ts->timer.function = himax_ts_timer_func;
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
		I("%s: polling mode enabled\n", __func__);
	}

	return ret;
}

int himax_ts_unregister_interrupt(void)
{
	struct himax_ts_data *ts = private_ts;
	int ret = 0;

	I("%s: entered.\n", __func__);

	/* Work functon */
	if ((private_ts->hx_irq > 0) && ts->use_irq) { /*INT mode*/

		free_irq(ts->hx_irq, ts);
		I("%s: irq disabled at qpio: %d\n", __func__,
		  private_ts->hx_irq);
	}
	if (ts->hx_fail_det > 0) {
		free_irq(ts->hx_fail_det, NULL);
		I("%s: irq disabled at qpio: %d\n", __func__, ts->hx_fail_det);
	}
	/*if use polling mode need to disable HX_ESD_RECOVERY function*/
	if (!ts->use_irq) {
		hrtimer_cancel(&ts->timer);
		cancel_work_sync(&ts->work);
		if (ts->himax_wq != NULL) {
			destroy_workqueue(ts->himax_wq);
		}
		I("%s: polling mode destroyed", __func__);
	}

	return ret;
}

void himax_common_suspend(struct device *dev)
{
	struct himax_ts_data *ts = dev_get_drvdata(dev);

	I("%s: enter\n", __func__);
#if defined(HX_CONFIG_DRM) && !defined(HX_CONFIG_FB)
	if (!ts->initialized) {
		return;
	}
#endif
	himax_chip_common_suspend(ts);
}

void himax_common_resume(struct device *dev)
{
	struct himax_ts_data *ts = dev_get_drvdata(dev);

	I("%s: enter\n", __func__);
#if defined(HX_CONFIG_DRM) && !defined(HX_CONFIG_FB)
	/*
	 *	wait until device resume for TDDI
	 *	TDDI: Touch and display Driver IC
	 */
	if (!ts->initialized) {
		if (himax_chip_common_init() != 0) {
			return;
		}
	}
#endif
	himax_chip_common_resume(ts);
}

#if defined(HX_CONFIG_FB)
int fb_notifier_callback(struct notifier_block *self, unsigned long event,
			 void *data)
{
	struct fb_event *evdata = data;
	int *blank;
	struct himax_ts_data *ts =
		container_of(self, struct himax_ts_data, fb_notif);

	I(" %s\n", __func__);

	if (evdata && evdata->data && (event == FB_EVENT_BLANK) && ts &&
	    ts->client) {
		blank = evdata->data;

		switch (*blank) {
		case FB_BLANK_UNBLANK:
			himax_common_resume(&ts->client->dev);
			break;
		case FB_BLANK_POWERDOWN:
		case FB_BLANK_HSYNC_SUSPEND:
		case FB_BLANK_VSYNC_SUSPEND:
		case FB_BLANK_NORMAL:
			himax_common_suspend(&ts->client->dev);
			break;
		default:
			break;
		}
	}

	return 0;
}
#elif defined(HX_CONFIG_DRM)
int drm_notifier_callback(struct notifier_block *self, unsigned long event,
			  void *data)
{
	struct msm_drm_notifier *evdata = data;
	int *blank;
	struct himax_ts_data *ts =
		container_of(self, struct himax_ts_data, fb_notif);

	if (!evdata || (evdata->id != 0)) {
		return 0;
	}

	D("DRM  %s\n", __func__);

	if (evdata->data && (event == MSM_DRM_EARLY_EVENT_BLANK) && ts &&
	    ts->client) {
		blank = evdata->data;
		switch (*blank) {
		case MSM_DRM_BLANK_POWERDOWN:
			if (!ts->initialized) {
				return -ECANCELED;
			}
			himax_common_suspend(&ts->client->dev);
			break;
		default:
			break;
		}
	}

	if (evdata->data && (event == MSM_DRM_EVENT_BLANK) && ts && ts->client) {
		blank = evdata->data;
		switch (*blank) {
		case MSM_DRM_BLANK_UNBLANK:
			himax_common_resume(&ts->client->dev);
			break;
		default:
			break;
		}
	}

	return 0;
}
#endif

/* Kernel 6.3+ changed I2C probe signature - handle both versions */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
int himax_chip_common_probe(struct i2c_client *client)
#else
int himax_chip_common_probe(struct i2c_client *client, const struct i2c_device_id *id)
#endif
{
	int ret = 0;
	struct himax_ts_data *ts;
	uint8_t test_buf[4];
	struct i2c_msg test_msg;

	/* Early I2C connectivity test - if FPDLink passthrough isn't ready,
	 * defer probe so kernel retries after hh983-serializer initializes.
	 * This is the standard Linux pattern for handling device dependencies.
	 */
	test_msg.addr = client->addr;
	test_msg.flags = I2C_M_RD;
	test_msg.len = 1;
	test_msg.buf = test_buf;
	ret = i2c_transfer(client->adapter, &test_msg, 1);
	if (ret < 0) {
		dev_info(&client->dev, "I2C not ready (FPDLink?), deferring probe\n");
		return -EPROBE_DEFER;
	}

	gp_rw_buf = kcalloc(BUS_RW_MAX_LEN, sizeof(uint8_t), GFP_KERNEL);
	if (gp_rw_buf == NULL) {
		E("Allocate I2C RW Buffer failed\n");
		ret = -ENODEV;
		goto err_alloc_rw_buf_failed;
	}

	/* Check I2C functionality */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		E("%s: i2c check functionality error\n", __func__);
		ret = -ENODEV;
		goto err_i2c_functionality_failed;
	}

	ts = kzalloc(sizeof(struct himax_ts_data), GFP_KERNEL);
	if (ts == NULL) {
		E("%s: allocate himax_ts_data failed\n", __func__);
		ret = -ENOMEM;
		goto err_alloc_data_failed;
	}

	i2c_set_clientdata(client, ts);
	ts->client = client;
	ts->dev = &client->dev;
	mutex_init(&ts->rw_lock);
	private_ts = ts;

	I("%s: %d-bit I2C address: 0x%02hx\n", __func__,
	  (client->flags & I2C_CLIENT_TEN) ? 10 : 7, client->addr);

	ts->initialized = false;
	ret = himax_chip_common_init();
	if (ret < 0) {
		goto err_common_init_failed;
	}

	return ret;

err_common_init_failed:
	kfree(ts);
	ts = NULL;
err_alloc_data_failed:
err_i2c_functionality_failed:
	kfree(gp_rw_buf);
	gp_rw_buf = NULL;
err_alloc_rw_buf_failed:

	return ret;
}

#if !defined(KERNEL_VER_6_01)
int himax_chip_common_remove(struct i2c_client *client)
#else
void himax_chip_common_remove(struct i2c_client *client)
#endif
{
    UNUSED(client);

    if (g_hx_chip_inited == true) {
        himax_chip_common_deinit();
    }

    kfree(gp_rw_buf);
    gp_rw_buf = NULL;
#if !defined(KERNEL_VER_6_01)
    return 0;
#endif
}

static const struct i2c_device_id himax_common_ts_id[] = { { HIMAX_common_NAME,
							     0 },
							   {} };

#if defined(CONFIG_OF)
static const struct of_device_id himax_match_table[] = {
	{ .compatible = "himax,hxcommon" },
	{},
};
#else
#define himax_match_table NULL
#endif

static struct i2c_driver himax_common_driver = {
	.id_table	= himax_common_ts_id,
	.probe		= himax_chip_common_probe,
	.remove		= himax_chip_common_remove,
	.driver		= {
		.name = HIMAX_common_NAME,
		.owner = THIS_MODULE,
		.of_match_table = himax_match_table,
	},
};

int __init himax_common_init(void)
{
	I("Himax common touch panel driver init\n");
	if (g_mmi_refcnt > 0U) {
		I("Himax driver has been loaded! ignoring....\n");
		return 0;
	} else {
		I("Himax driver loading...\n");
		g_mmi_refcnt = 1U;
	}
	i2c_add_driver(&himax_common_driver);

	return 0;
}

void __exit himax_common_exit(void)
{
	i2c_del_driver(&himax_common_driver);
}

module_init(himax_common_init);
module_exit(himax_common_exit);

MODULE_DESCRIPTION("Himax_common driver");
MODULE_LICENSE("GPL");
/* Note: If using with FPDLink (hh983-serializer), add to /etc/modprobe.d/:
 *   softdep himax_mmi pre: hh983-serializer
 * The driver handles probe ordering via -EPROBE_DEFER automatically.
 */
