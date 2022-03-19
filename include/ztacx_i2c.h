#pragma once

extern int ztacx_i2c_init(struct ztacx_leaf *leaf);
extern int ztacx_i2c_start(struct ztacx_leaf *leaf);

ZTACX_CLASS_DEFINE(i2c, ((struct ztacx_leaf_cb){.init=&ztacx_i2c_init, .start=ztacx_i2c_start}));
#if CONFIG_ZTACX_LEAF_I2C0
ZTACX_LEAF_DEFINE(i2c, I2C_0, NULL);
#endif
#if CONFIG_ZTACX_LEAF_I2C1
ZTACX_LEAF_DEFINE(i2c, I2C_1, NULL);
#endif
