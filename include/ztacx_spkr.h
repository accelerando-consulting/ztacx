#pragma once

extern uint16_t spkr_value;
extern uint16_t spkr_freq_value;
extern uint16_t spkr_duration_value;

extern int spkr_setup();
extern int spkr_pre_sleep();
extern int spkr_beep(void);
extern int spkr_tone(int freq, int duration_ms);
extern int cmd_test_spkr(const struct shell *shell, size_t argc, char **argv);
