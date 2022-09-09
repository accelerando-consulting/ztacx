#pragma once
#if CONFIG_ZTACX_LEAF_LORA_MODEM

extern int ztacx_lora_modem_init(struct ztacx_leaf *leaf);
extern int ztacx_lora_modem_start(struct ztacx_leaf *leaf);

ZTACX_CLASS_DEFINE(lora_modem, ((struct ztacx_leaf_cb){
			   .init=&ztacx_lora_modem_init,
			   .start=&ztacx_lora_modem_start
		}));
ZTACX_LEAF_DEFINE(lora_modem, lora_modem, NULL);

#endif
