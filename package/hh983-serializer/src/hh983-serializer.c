// SPDX-License-Identifier: GPL-2.0
/*
 * HH983 FPDLink Serializer Driver
 *
 * Supports two configurations:
 *   Mode 0: DS90UH983 + DS90UH984 (REM_INTB forwarding)
 *   Mode 1: DS90UH983 + DS90UH988 (I2C passthrough for TDDI + REM_INTB)
 *
 * Author: Albert David
 */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/delay.h>

/* Configuration mode: 0=983+984, 1=983+988 */
static int config_mode = 0;
module_param(config_mode, int, 0444);
MODULE_PARM_DESC(config_mode, "Configuration mode: 0=983+984, 1=983+988 (default: 0)");

/* Common serializer registers */
#define SER_I2C_CONTROL          0x07
#define SER_GPIO4_CONFIG         0x1B
#define SER_GLOBAL_INT           0x51
#define SER_TARGET_ID0           0x70
#define SER_TARGET_ID1           0x71
#define SER_TARGET_ALIAS0        0x78
#define SER_TARGET_ALIAS1        0x79
#define SER_TARGET_DEST0         0x88
#define SER_TARGET_DEST1         0x89
#define SER_INTERRUPT_CTRL       0xC6

/* Serializer configuration values */
#define SER_ENABLE_PASSTHROUGH   0xD8
#define SER_ENABLE_REM_INT       0x21
#define SER_GPIO4_PORT0_REM_INT  0x88
#define SER_GPIO4_PORT1_REM_INT  0x98
#define SER_ENABLE_GLOBAL_INT    0x83

/* 984 Deserializer registers */
#define DES984_INTB_ENABLE       0x44
#define DES984_INTB_VALUE        0x81

/* 988 Deserializer registers */
#define DES988_I2C_CONTROL       0x04
#define DES988_RX_LOCK_STATUS    0x53
#define DES988_RX_INTN_CTL       0x44  /* INTB_IN enable register (datasheet 7.3.9) */

/* 988 Deserializer configuration values */
#define DES988_ENABLE_PASSTHROUGH 0xD9
#define DES988_INTB_IN_ENABLE    0x81  /* 0x81 required, not 0x80! Enables INTB_IN -> REM_INTB forwarding */

/* TDDI I2C targets (7-bit addresses) */
#define TDDI_ADDR_1              0x48
#define TDDI_ADDR_2              0x49

/* TARGET_ID/ALIAS format: (7-bit addr << 1) */
#define MAKE_TARGET_ID(addr)     ((addr) << 1)

/* TARGET_DEST format: [7:5]=port, [1:0]=depth */
#define TARGET_DEST_PORT0        0x00
#define TARGET_DEST_PORT1        0x20

struct hh983_data {
	struct i2c_client *client;
	u8 deser_addr;
	int mode;
	bool initialized;
};

static int hh983_write_reg(struct i2c_client *client, u8 reg, u8 value)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, reg, value);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to write reg 0x%02X: %d\n", reg, ret);
		return ret;
	}
	dev_dbg(&client->dev, "SER 0x%02X <- 0x%02X\n", reg, value);
	return 0;
}

static int hh983_read_reg(struct i2c_client *client, u8 reg)
{
	int ret;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0)
		dev_err(&client->dev, "Failed to read reg 0x%02X: %d\n", reg, ret);
	else
		dev_dbg(&client->dev, "SER 0x%02X = 0x%02X\n", reg, ret);
	return ret;
}

static int hh983_write_deser_reg(struct i2c_client *client, u8 deser_addr, u8 reg, u8 value)
{
	struct i2c_msg msg;
	u8 buf[2];
	int ret;

	buf[0] = reg;
	buf[1] = value;

	msg.addr = deser_addr;
	msg.flags = 0;
	msg.len = 2;
	msg.buf = buf;

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret != 1) {
		dev_err(&client->dev, "Failed to write DES[0x%02X] reg 0x%02X: %d\n",
			deser_addr, reg, ret);
		return ret < 0 ? ret : -EIO;
	}
	dev_dbg(&client->dev, "DES[0x%02X] 0x%02X <- 0x%02X\n", deser_addr, reg, value);
	return 0;
}

static int hh983_read_deser_reg(struct i2c_client *client, u8 deser_addr, u8 reg)
{
	struct i2c_msg msgs[2];
	u8 reg_buf = reg;
	u8 val_buf;
	int ret;

	msgs[0].addr = deser_addr;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = &reg_buf;

	msgs[1].addr = deser_addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = 1;
	msgs[1].buf = &val_buf;

	ret = i2c_transfer(client->adapter, msgs, 2);
	if (ret != 2) {
		dev_err(&client->dev, "Failed to read DES[0x%02X] reg 0x%02X: %d\n",
			deser_addr, reg, ret);
		return ret < 0 ? ret : -EIO;
	}
	dev_dbg(&client->dev, "DES[0x%02X] 0x%02X = 0x%02X\n", deser_addr, reg, val_buf);
	return val_buf;
}

/* Configure REM_INTB on serializer (common to both modes) */
static int hh983_configure_rem_intb(struct i2c_client *client, int port)
{
	int ret, readback;
	u8 gpio4_val = (port == 0) ? SER_GPIO4_PORT0_REM_INT : SER_GPIO4_PORT1_REM_INT;

	dev_info(&client->dev, "Configuring REM_INTB for Port %d (GPIO4=0x%02X)\n", port, gpio4_val);

	/* Enable REM_INT in interrupt control */
	ret = hh983_write_reg(client, SER_INTERRUPT_CTRL, SER_ENABLE_REM_INT);
	if (ret < 0)
		return ret;
	usleep_range(1000, 2000);

	/* Configure GPIO4 for REM_INT forwarding */
	dev_info(&client->dev, "Writing GPIO4_CONFIG (0x%02X) = 0x%02X\n", SER_GPIO4_CONFIG, gpio4_val);
	ret = hh983_write_reg(client, SER_GPIO4_CONFIG, gpio4_val);
	if (ret < 0)
		return ret;
	usleep_range(1000, 2000);

	/* Verify write */
	readback = hh983_read_reg(client, SER_GPIO4_CONFIG);
	if (readback != gpio4_val)
		dev_warn(&client->dev, "GPIO4_CONFIG readback mismatch: wrote 0x%02X, read 0x%02X\n",
			 gpio4_val, readback);
	else
		dev_info(&client->dev, "GPIO4_CONFIG verified: 0x%02X\n", readback);

	/* Enable global INTB */
	ret = hh983_write_reg(client, SER_GLOBAL_INT, SER_ENABLE_GLOBAL_INT);
	if (ret < 0)
		return ret;

	return 0;
}

/* Mode 0: 983 + 984 configuration */
static int hh983_init_mode_984(struct hh983_data *data)
{
	struct i2c_client *client = data->client;
	int ret;

	dev_info(&client->dev, "Initializing Mode 0: 983 + 984\n");

	/* Enable I2C passthrough on serializer */
	ret = hh983_write_reg(client, SER_I2C_CONTROL, SER_ENABLE_PASSTHROUGH);
	if (ret < 0)
		return ret;
	msleep(10);

	/* Configure REM_INTB for Port 0 */
	ret = hh983_configure_rem_intb(client, 0);
	if (ret < 0)
		return ret;

	/* Enable INTB on 984 deserializer */
	ret = hh983_write_deser_reg(client, data->deser_addr, DES984_INTB_ENABLE, DES984_INTB_VALUE);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to configure 984 INTB\n");
		return ret;
	}

	dev_info(&client->dev, "Mode 0 (983+984) initialization complete\n");
	return 0;
}

/* Mode 1: 983 + 988 configuration */
static int hh983_init_mode_988(struct hh983_data *data)
{
	struct i2c_client *client = data->client;
	int ret;

	dev_info(&client->dev, "Initializing Mode 1: 983 + 988 (TDDI passthrough)\n");

	/* Step 1: Enable I2C passthrough on serializer */
	ret = hh983_write_reg(client, SER_I2C_CONTROL, SER_ENABLE_PASSTHROUGH);
	if (ret < 0)
		return ret;
	msleep(10);

	/* Step 2: Enable I2C passthrough on 988 deserializer */
	ret = hh983_write_deser_reg(client, data->deser_addr, DES988_I2C_CONTROL, DES988_ENABLE_PASSTHROUGH);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to enable 988 passthrough\n");
		return ret;
	}
	usleep_range(5000, 10000);

	/* Step 3: Check link status */
	ret = hh983_read_deser_reg(client, data->deser_addr, DES988_RX_LOCK_STATUS);
	if (ret >= 0)
		dev_info(&client->dev, "988 RX Lock Status: 0x%02X\n", ret);

	/* Step 4: Configure TARGET_ID/ALIAS/DEST for TDDI 0x48 -> Port 1 */
	ret = hh983_write_reg(client, SER_TARGET_ID0, MAKE_TARGET_ID(TDDI_ADDR_1));
	if (ret < 0)
		return ret;
	ret = hh983_write_reg(client, SER_TARGET_ALIAS0, MAKE_TARGET_ID(TDDI_ADDR_1));
	if (ret < 0)
		return ret;
	ret = hh983_write_reg(client, SER_TARGET_DEST0, TARGET_DEST_PORT1);
	if (ret < 0)
		return ret;

	/* Step 5: Configure TARGET_ID/ALIAS/DEST for TDDI 0x49 -> Port 1 */
	ret = hh983_write_reg(client, SER_TARGET_ID1, MAKE_TARGET_ID(TDDI_ADDR_2));
	if (ret < 0)
		return ret;
	ret = hh983_write_reg(client, SER_TARGET_ALIAS1, MAKE_TARGET_ID(TDDI_ADDR_2));
	if (ret < 0)
		return ret;
	ret = hh983_write_reg(client, SER_TARGET_DEST1, TARGET_DEST_PORT1);
	if (ret < 0)
		return ret;

	/* Step 6: Configure REM_INTB for Port 0
	 * IMPORTANT: Use Port 0, NOT Port 1!
	 * - TARGET_DEST_PORT1 routes I2C to the 988's I2C Port 1 (where TDDI is connected)
	 * - But REM_INT comes over the single FPDLink link, which is always Port 0
	 * - The 983 has only one deserializer (988) connected = FPDLink Port 0
	 * Signal path: TDDI touch_int -> 988 INTB_IN -> BCC Port 0 -> 983 REM_INTB -> GPIO4
	 */
	ret = hh983_configure_rem_intb(client, 0);
	if (ret < 0)
		return ret;

	/* Step 7: Enable INTB_IN forwarding on 988 deserializer (datasheet 7.3.9)
	 * RX_INTN_CTL (0x44) bit 7 = 1 enables INTB_IN -> back channel -> REM_INTB
	 * Signal path: TDDI touch_int -> 988 INTB_IN (pin 45) -> BCC -> 983 REM_INTB -> Host GPIO
	 */
	ret = hh983_write_deser_reg(client, data->deser_addr, DES988_RX_INTN_CTL, DES988_INTB_IN_ENABLE);
	if (ret < 0) {
		dev_warn(&client->dev, "Failed to configure 988 INTB_IN forwarding\n");
		/* Don't fail - passthrough may still work */
	}

	dev_info(&client->dev, "Mode 1 (983+988) initialization complete\n");
	dev_info(&client->dev, "TDDI 0x%02X and 0x%02X should be visible on I2C bus\n",
		 TDDI_ADDR_1, TDDI_ADDR_2);

	/* Allow FPDLink I2C passthrough to fully stabilize before returning.
	 * The himax touch driver may probe during this delay via deferred probe.
	 * Initial I2C commands work but touch reporting can be degraded if the
	 * link isn't fully stable. 100ms provides adequate stabilization.
	 */
	msleep(100);

	return 0;
}

/* Kernel 6.3+ changed I2C probe signature - handle both versions */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
static int hh983_probe(struct i2c_client *client)
#else
static int hh983_probe(struct i2c_client *client, const struct i2c_device_id *id)
#endif
{
	struct hh983_data *data;
	int ret;

	dev_info(&client->dev, "HH983 FPDLink serializer probe (config_mode=%d)\n", config_mode);

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	data->deser_addr = 0x2C;  /* Default deserializer address */
	data->mode = config_mode;
	i2c_set_clientdata(client, data);

	switch (data->mode) {
	case 0:
		ret = hh983_init_mode_984(data);
		break;
	case 1:
		ret = hh983_init_mode_988(data);
		break;
	default:
		dev_err(&client->dev, "Invalid config_mode %d (use 0 or 1)\n", data->mode);
		return -EINVAL;
	}

	if (ret < 0) {
		dev_err(&client->dev, "Initialization failed: %d\n", ret);
		return ret;
	}

	data->initialized = true;
	dev_info(&client->dev, "HH983 initialization successful\n");
	return 0;
}

static void hh983_remove(struct i2c_client *client)
{
	struct hh983_data *data = i2c_get_clientdata(client);

	dev_info(&client->dev, "HH983 driver removed\n");

	if (data && data->initialized) {
		/* Disable GPIO4 REM_INT forwarding */
		hh983_write_reg(client, SER_GPIO4_CONFIG, 0x00);
	}
}

static const struct i2c_device_id hh983_id[] = {
	{ "hh983-serializer", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, hh983_id);

static const struct of_device_id hh983_of_match[] = {
	{ .compatible = "ti,hh983-serializer" },
	{ }
};
MODULE_DEVICE_TABLE(of, hh983_of_match);

static struct i2c_driver hh983_driver = {
	.probe = hh983_probe,
	.remove = hh983_remove,
	.id_table = hh983_id,
	.driver = {
		.name = "hh983-serializer",
		.of_match_table = hh983_of_match,
	},
};

module_i2c_driver(hh983_driver);

MODULE_DESCRIPTION("HH983 FPDLink Serializer Driver (983+984 / 983+988)");
MODULE_AUTHOR("Albert David");
MODULE_LICENSE("GPL");
