#pragma once

#include <lorawan/lorawan.h>

extern int ztacx_lorawan_init(struct ztacx_leaf *leaf);
extern int ztacx_lorawan_start(struct ztacx_leaf *leaf);

ZTACX_CLASS_DEFINE(lorawan, ((struct ztacx_leaf_cb){
			   .init=&ztacx_lorawan_init,
			   .start=&ztacx_lorawan_start
		}));
ZTACX_LEAF_DEFINE(lorawan, lorawan, NULL);
