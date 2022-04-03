#include "ztacx.h"
#include <lorawan/lorawan.h>
#include "ztacx_settings.h"

#define DEFAULT_RADIO_NODE DT_ALIAS(lora0)
BUILD_ASSERT(DT_NODE_HAS_STATUS(DEFAULT_RADIO_NODE, okay),
	     "No default LoRa radio specified in DT");
#define DEFAULT_RADIO DT_LABEL(DEFAULT_RADIO_NODE)

static const struct device *lora_dev;
static struct lorawan_join_config *join_cfg;
static uint8_t dev_eui[8];
static uint8_t join_eui[8];
static uint8_t app_key[16];
static uint8_t dev_addr[4];
static uint8_t nwk_skey[16];
static uint8_t app_skey[16];

enum lorawan_setting_index {
	SETTING_AUTH_ABP = 0,
	SETTING_AUTH_OTAA,

	SETTING_DEV_EUI,
	SETTING_APP_KEY,
	SETTING_JOIN_EUI,
	SETTING_DEV_ADDR,
	SETTING_NWK_SKEY,
	SETTING_APP_SKEY,
	SETTING_CHANNEL_MASK,
	SETTING_ADR,
	SETTING_SEND_RETRIES,
	SETTING_JOIN_RETRIES,
	SETTING_ENABLE,
};

static struct ztacx_variable lorawan_settings[] = {
	{"lorawan_auth_abp", ZTACX_VALUE_BOOL},
	{"lorawan_auth_otaa", ZTACX_VALUE_BOOL},
	{"lorawan_dev_eui", ZTACX_VALUE_STRING},
	{"lorawan_app_key", ZTACX_VALUE_STRING},
	{"lorawan_join_eui", ZTACX_VALUE_STRING},
	{"lorawan_dev_addr", ZTACX_VALUE_STRING},
	{"lorawan_nwk_skey", ZTACX_VALUE_STRING},
	{"lorawan_app_skey", ZTACX_VALUE_STRING},
	{"lorawan_channel_mask", ZTACX_VALUE_STRING},
	{"lorawan_adr", ZTACX_VALUE_BOOL},
	{"lorawan_send_retries", ZTACX_VALUE_UINT16},
	{"lorawan_join_retries", ZTACX_VALUE_UINT16, {.val_uint16=10}},
	{"lorawan_enable", ZTACX_VALUE_BOOL, {.val_bool=true}},
};

static void dl_callback(uint8_t port, bool data_pending,

			int16_t rssi, int8_t snr,
			uint8_t len, const uint8_t *data)
{
	LOG_INF("Port %d, Pending %d, RSSI %ddB, SNR %ddBm", port, data_pending, rssi, snr);
	if (data) {
		LOG_HEXDUMP_INF(data, len, "Payload: ");
	}
}

static void lorwan_datarate_changed(enum lorawan_datarate dr)
{
	uint8_t unused, max_size;

	lorawan_get_payload_sizes(&unused, &max_size);
	LOG_INF("New Datarate: DR_%d, Max Payload %d", dr, max_size);
}

int ztacx_lorawan_init(struct ztacx_leaf *leaf)
{
	int rc = 0;

	lora_dev = device_get_binding(DEFAULT_RADIO);
	if (!lora_dev) {
		LOG_ERR("%s Device not found", DEFAULT_RADIO);
		return -ENODEV;
	}

	ztacx_settings_register(lorawan_settings, ARRAY_SIZE(lorawan_settings));

	return rc;
}

#define LORAWAN_SETTING(index,kind) (lorawan_settings[SETTING_ ## index].value.val_ ## kind)

static int parse_hex_setting_words(struct ztacx_variable *settings, int index, int min, int max,uint16_t *words_r, int *count_r)
{
	struct ztacx_variable *setting = &settings[index];
	const char *name = setting->name;
	const char *s = setting->value.val_string;
	int len = s?strlen(s):0;
	LOG_DBG("%s s=[%s] min=%d max=%d", log_strdup(name), s?log_strdup(s):"[empty]", min, max);
	if ((min==0) && (len==0)) {
		if (count_r) *count_r = 0;
		return 0;
	}

	int words = len >> 2;
	if ((words < min) || (words > max)) {
		LOG_ERR("Size of setting %s (%d words) is outside limits [%d,%d]",
			name, words, min, max);
		return -EMSGSIZE;
	}

	for (int word=0; word<words;word++) {
		char word_buf[5] = {s[word*4],s[word*4+1],s[word*4+2],s[word*4+3],0};
		if (words_r) {
			words_r[word]=strtoul(word_buf, NULL, 16);
		}
	}
	if (count_r) *count_r = words;
	return 0;
}

static int parse_hex_setting(struct ztacx_variable *settings, int index, int min, int max,uint8_t *bytes_r, int *count_r)
{
	struct ztacx_variable *setting = &settings[index];
	const char *name = setting->name;
	const char *s = setting->value.val_string;
	int len = s?strlen(s):0;
	LOG_DBG("%s s=[%s] min=%d max=%d", log_strdup(name), s?log_strdup(s):"[empty]", min, max);

	if ((min==0) && (len==0)) {
		if (count_r) *count_r = 0;
		return 0;
	}

	int bytes = len >> 1;
	if ((bytes < min) || (bytes > max)) {
		LOG_ERR("Size of setting %s (%d bytes) is outside limits [%d,%d]",
			log_strdup(name), bytes, min, max);
		return -EMSGSIZE;
	}

	for (int byte=0; byte<bytes;byte++) {
		char byte_buf[3] = {s[byte*2],s[byte*2+1],0};
		//LOG_INF("Parsing at [%s] [%s]", log_strdup(s+(byte*2)), log_strdup(byte_buf));
		bytes_r[byte]=strtoul(byte_buf, NULL, 16);
		//LOG_INF("Parsed byte %02x", (int)bytes_r[byte]);
	}
	if (count_r) *count_r = bytes;
	return 0;
}

int ztacx_lorawan_start(struct ztacx_leaf *leaf)
{
	int rc=0, err=0;
	uint16_t channel_mask[6]={0x0000,0x0000,0x0000,0x0000,0x0000,0x0000};
	int channel_mask_len = 0;

	struct lorawan_downlink_cb downlink_cb = {
		.port = LW_RECV_PORT_ANY,
		.cb = dl_callback
	};

	struct lorawan_join_config parsed_join_cfg;

	if (!LORAWAN_SETTING(ENABLE,bool)) {
		LOG_INF("Lorawan is disabled by settings");
		return -ENOTSUP;
	}

	if (!join_cfg && LORAWAN_SETTING(AUTH_ABP,bool)) {
		// ABP mode requires dev_eui, dev_addr, nwk_key, app_key
		parsed_join_cfg.mode = LORAWAN_ACT_ABP;

		err = parse_hex_setting(lorawan_settings, SETTING_DEV_EUI, 8, 8, dev_eui, NULL);
		if (err) return err;
		parsed_join_cfg.dev_eui = dev_eui;

		err = parse_hex_setting(lorawan_settings, SETTING_DEV_ADDR, 4, 4, (uint8_t *)&dev_addr, NULL);
		if (err) return err;
		parsed_join_cfg.abp.dev_addr = (dev_addr[0]<<24)|(dev_addr[1]<<16)|(dev_addr[2]<<8)|dev_addr[3];

		err = parse_hex_setting(lorawan_settings, SETTING_APP_SKEY, 16, 16, app_skey, NULL);
		if (err) return err;
		parsed_join_cfg.abp.app_skey = app_skey;

		err = parse_hex_setting(lorawan_settings, SETTING_NWK_SKEY, 16, 16, nwk_skey, NULL);
		if (err) return err;
		parsed_join_cfg.abp.nwk_skey = nwk_skey;

		join_cfg = calloc(1, sizeof(struct lorawan_join_config));
		if (!join_cfg) {
			return -ENOMEM;
		}
		memcpy(join_cfg, &parsed_join_cfg, sizeof(parsed_join_cfg));
		LOG_INF("LoRaWAN ABP configured");
	}
	else if (!join_cfg && LORAWAN_SETTING(AUTH_OTAA,bool)) {
		// OTAA mode requires dev_eui, app_key, join_eui
		parsed_join_cfg.mode = LORAWAN_ACT_OTAA;

		err = parse_hex_setting(lorawan_settings, SETTING_DEV_EUI, 8, 8, dev_eui, NULL);
		if (err) return err;
		parsed_join_cfg.dev_eui = dev_eui;

		err = parse_hex_setting(lorawan_settings, SETTING_APP_KEY, 16, 16, app_key, NULL);
		if (err) return err;
		parsed_join_cfg.otaa.app_key = app_key;
		parsed_join_cfg.otaa.nwk_key = app_key;

		err = parse_hex_setting(lorawan_settings, SETTING_JOIN_EUI, 8, 8, join_eui, NULL);
		if (err) return err;
		parsed_join_cfg.otaa.join_eui = join_eui;

		join_cfg = calloc(1, sizeof(struct lorawan_join_config));
		if (!join_cfg) {
			LOG_ERR("Allocation failed");
			return -ENOMEM;
		}
		memcpy(join_cfg, &parsed_join_cfg, sizeof(parsed_join_cfg));
		LOG_INF("LoRaWAN OTAA configured");
	}
	else if (!join_cfg) {
		LOG_WRN("LoRaWAN Authentication not (yet) configured");
		return -EINVAL;
	}

	if (parse_hex_setting_words(lorawan_settings, SETTING_CHANNEL_MASK, 0, 6, channel_mask, &channel_mask_len) != 0) {
		LOG_ERR("LoRaWAN channel mask could not be parsed");
	}

	rc = lorawan_start();
	if (rc < 0) {
		LOG_ERR("lorawan_start failed: %d", rc);
		return rc;
	}
	if (lorawan_settings[SETTING_ADR].value.val_bool) {
		lorawan_enable_adr(true);
	}
	if (lorawan_settings[SETTING_SEND_RETRIES].value.val_uint16) {
		lorawan_set_conf_msg_tries(lorawan_settings[SETTING_SEND_RETRIES].value.val_uint16);
	}

	if (channel_mask_len) {
		if (lorawan_set_channel_mask(channel_mask, channel_mask_len*16)) {
			LOG_INF("Channel mask updated for selected channels");
		}
		else {
			LOG_ERR("Channel mask update failed");
		}
	}

	lorawan_register_downlink_callback(&downlink_cb);
	lorawan_register_dr_changed_callback(lorwan_datarate_changed);

	// FIXME: join on demand only for OTAA
	// FIXME: refactor this as a k_delayed_work

	uint16_t try = 0;
	uint16_t try_max = LORAWAN_SETTING(JOIN_RETRIES, uint16);
	bool joined = false;
	do {
		++try;
		LOG_INF("Join attempt %d", try);

		rc = lorawan_join(join_cfg);

		if (rc == 0) {
			joined = true;
			break;
		}
		LOG_ERR("lorawan_join_network failed: %d", rc);
		k_sleep(K_SECONDS(5));
	} while (try < try_max);

	if (!joined) {
		LOG_ERR("failed to join, aborting");
		return -ENETUNREACH;
	}
	LOG_INF("LoRaWAN Joined rc=%d", rc);
	return rc;
}
