#ifndef _BIAS_H_
#define _BIAS_H_

#define LCM_I2C_ID_NAME "lcd_bias"

struct _lcm_i2c_dev {
    struct i2c_client *client;
};

static struct i2c_client *_lcm_i2c_client;
#endif