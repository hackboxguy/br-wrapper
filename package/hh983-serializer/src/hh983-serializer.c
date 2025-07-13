// hh983_serializer.c
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/delay.h>

/* Register definitions */
#define HH983_I2C_PASSTHROUGH_REG    0x07
#define HH983_ENABLE_PASSTHROUGH     0xD8

/* REM_INTB configuration registers */
#define HH983_INTERRUPT_CTRL_REG     0xC6
#define HH983_GPIO4_CONFIG_REG       0x1B
#define HH983_GLOBAL_INT_REG         0x51

/* REM_INTB configuration values */
#define HH983_ENABLE_REM_INT         0x21  /* Set bits [5] and [0] */
#define HH983_GPIO4_PORT0_REM_INT    0x88  /* Configure GPIO4 for Port 0 REM_INT */
#define HH983_GPIO4_PORT1_REM_INT    0x98  /* Configure GPIO4 for Port 1 REM_INT */
#define HH983_ENABLE_GLOBAL_INT      0x83  /* Set bit [7] and bits [1:0] = 0b11 */

struct hh983_data {
    struct i2c_client *client;
    bool rem_intb_enabled;
};

static int hh983_write_deserializer_reg(struct i2c_client *client, u8 deser_addr, u8 reg, u8 value)
{
    int ret;
    struct i2c_msg msgs[1];
    u8 buf[2];

    /* Prepare I2C message for deserializer */
    buf[0] = reg;
    buf[1] = value;

    msgs[0].addr = deser_addr >> 1;  /* Convert to 7-bit address */
    msgs[0].flags = 0;               /* Write operation */
    msgs[0].len = 2;
    msgs[0].buf = buf;

    /* Send via back-channel through serializer */
    ret = i2c_transfer(client->adapter, msgs, 1);
    if (ret != 1) {
        dev_err(&client->dev, "Failed to write deserializer reg 0x%02X: %d\n", reg, ret);
        return ret < 0 ? ret : -EIO;
    }

    dev_info(&client->dev, "Wrote 0x%02X to deserializer[0x%02X] reg 0x%02X\n",
             value, deser_addr, reg);
    return 0;
}

static int hh983_configure_deserializer_intb(struct i2c_client *client)
{
    int ret;

    dev_info(&client->dev, "Configuring deserializer INTB functionality\n");

    /* Enable INTB on DS90UH984 deserializer */
    ret = hh983_write_deserializer_reg(client, 0x58, 0x44, 0x81);
    if (ret < 0) {
        dev_err(&client->dev, "Failed to enable deserializer INTB: %d\n", ret);
        return ret;
    }

    dev_info(&client->dev, "Deserializer INTB enabled successfully\n");
    return 0;
}

static int hh983_configure_rem_intb(struct i2c_client *client)
{
    int ret;

    dev_info(&client->dev, "Configuring REM_INTB functionality\n");

    /* Step 1: Enable REM_INT in FPD-Link Transmitter interrupt
     * Set Main Page Reg 0xC6[5] = 1, 0xC6[0] = 1
     */
    ret = i2c_smbus_write_byte_data(client, HH983_INTERRUPT_CTRL_REG,
                                   HH983_ENABLE_REM_INT);
    if (ret < 0) {
        dev_err(&client->dev, "Failed to enable REM_INT: %d\n", ret);
        return ret;
    }
    dev_dbg(&client->dev, "REM_INT enabled in interrupt control register\n");

    /* Small delay between register writes */
    usleep_range(1000, 2000);

    /* Step 2: Configure GPIO4 Output and forward REM_INT
     * Writing 0x1B = 0x88 for Port 0 (standard configuration)
     * Use 0x98 for Port 1 if needed in dual-port configurations
     */
    ret = i2c_smbus_write_byte_data(client, HH983_GPIO4_CONFIG_REG,
                                   HH983_GPIO4_PORT0_REM_INT);
    if (ret < 0) {
        dev_err(&client->dev, "Failed to configure GPIO4 for REM_INT: %d\n", ret);
        return ret;
    }
    dev_dbg(&client->dev, "GPIO4 configured for Port 0 REM_INT forwarding\n");

    /* Small delay between register writes */
    usleep_range(1000, 2000);

    /* Step 3: Enable Global INTB and FPD_TX Interrupts
     * Set Main Page Reg 0x51[7] = 1, 0x51[1:0] = 0b11
     */
    ret = i2c_smbus_write_byte_data(client, HH983_GLOBAL_INT_REG,
                                   HH983_ENABLE_GLOBAL_INT);
    if (ret < 0) {
        dev_err(&client->dev, "Failed to enable global interrupts: %d\n", ret);
        return ret;
    }
    dev_dbg(&client->dev, "Global INTB and FPD_TX interrupts enabled\n");

    dev_info(&client->dev, "REM_INTB configuration completed successfully\n");
    return 0;
}

static int hh983_verify_configuration(struct i2c_client *client)
{
    int ret;
    u8 reg_val;

    /* Verify interrupt control register */
    ret = i2c_smbus_read_byte_data(client, HH983_INTERRUPT_CTRL_REG);
    if (ret < 0) {
        dev_err(&client->dev, "Failed to read interrupt control register: %d\n", ret);
        return ret;
    }
    reg_val = (u8)ret;
    dev_info(&client->dev, "Interrupt control reg (0xC6): 0x%02X\n", reg_val);

    /* Verify GPIO4 configuration */
    ret = i2c_smbus_read_byte_data(client, HH983_GPIO4_CONFIG_REG);
    if (ret < 0) {
        dev_err(&client->dev, "Failed to read GPIO4 config register: %d\n", ret);
        return ret;
    }
    reg_val = (u8)ret;
    dev_info(&client->dev, "GPIO4 config reg (0x1B): 0x%02X\n", reg_val);

    /* Verify global interrupt register */
    ret = i2c_smbus_read_byte_data(client, HH983_GLOBAL_INT_REG);
    if (ret < 0) {
        dev_err(&client->dev, "Failed to read global interrupt register: %d\n", ret);
        return ret;
    }
    reg_val = (u8)ret;
    dev_info(&client->dev, "Global interrupt reg (0x51): 0x%02X\n", reg_val);

    return 0;
}

static int hh983_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct hh983_data *data;
    int ret;

    dev_info(&client->dev, "Initializing HH983 FPDLink serializer\n");

    /* Allocate driver data */
    data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    data->client = client;
    i2c_set_clientdata(client, data);

    /* Step 1: Enable I2C passthrough */
    ret = i2c_smbus_write_byte_data(client, HH983_I2C_PASSTHROUGH_REG,
                                   HH983_ENABLE_PASSTHROUGH);
    if (ret < 0) {
        dev_err(&client->dev, "Failed to enable I2C passthrough: %d\n", ret);
        return ret;
    }
    dev_info(&client->dev, "HH983 I2C passthrough enabled\n");

    /* Allow some time for passthrough to stabilize */
    msleep(10);

    /* Step 2: Configure REM_INTB functionality */
    ret = hh983_configure_rem_intb(client);
    if (ret < 0) {
        dev_err(&client->dev, "Failed to configure REM_INTB: %d\n", ret);
        return ret;
    }
    data->rem_intb_enabled = true;

    /* Step 3: Configure deserializer INTB */
    ret = hh983_configure_deserializer_intb(client);
    if (ret < 0) {
        dev_err(&client->dev, "Failed to configure deserializer INTB: %d\n", ret);
        return ret;
    }

    /* Step 4: Verify configuration (optional, for debugging) */
    ret = hh983_verify_configuration(client);
    if (ret < 0) {
        dev_warn(&client->dev, "Configuration verification failed: %d\n", ret);
        /* Don't fail probe on verification failure */
    }

    dev_info(&client->dev, "HH983 initialization completed successfully\n");
    dev_info(&client->dev, "REM_INTB pin ready to mirror deserializer INTB_IN\n");

    return 0;
}

static void hh983_remove(struct i2c_client *client)
{
    struct hh983_data *data = i2c_get_clientdata(client);

    dev_info(&client->dev, "Removing HH983 FPDLink serializer driver\n");

    /* Optional: Disable REM_INTB on removal */
    if (data && data->rem_intb_enabled) {
        i2c_smbus_write_byte_data(client, HH983_GPIO4_CONFIG_REG, 0x00);
        dev_info(&client->dev, "REM_INTB functionality disabled\n");
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

MODULE_DESCRIPTION("HH983 FPDLink Serializer Driver with REM_INTB support");
MODULE_AUTHOR("Albert David");
MODULE_LICENSE("GPL");
