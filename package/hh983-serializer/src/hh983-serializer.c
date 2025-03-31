// hh983_serializer.c
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>

#define HH983_I2C_PASSTHROUGH_REG 0x07
#define HH983_ENABLE_PASSTHROUGH  0xD8

static int hh983_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int ret;
    
    dev_info(&client->dev, "Initializing HH983 FPDLink serializer\n");
    
    // Enable I2C passthrough
    ret = i2c_smbus_write_byte_data(client, HH983_I2C_PASSTHROUGH_REG, 
                                   HH983_ENABLE_PASSTHROUGH);
    if (ret < 0) {
        dev_err(&client->dev, "Failed to enable I2C passthrough: %d\n", ret);
        return ret;
    }
    
    dev_info(&client->dev, "HH983 I2C passthrough enabled\n");
    return 0;
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
    .id_table = hh983_id,
    .driver = {
        .name = "hh983-serializer",
        .of_match_table = hh983_of_match,
    },
};

module_i2c_driver(hh983_driver);

MODULE_DESCRIPTION("HH983 FPDLink Serializer Driver");
MODULE_AUTHOR("Albert David");
MODULE_LICENSE("GPL");
