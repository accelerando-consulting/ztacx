#pragma once

extern bool pwmled_value;
extern uint16_t pwmled_duty_value;
extern uint16_t pwmled_duration_value;

extern int pwmled_setup();
extern int pwmled_pre_sleep();
extern int cmd_test_pwmled(const struct shell *shell, size_t argc, char **argv);
extern int pwmled_pre_sleep();
extern int turn_pwmled_on(int duty, int duration_ms);
extern int turn_pwmled_off();
extern int pwmled_adjust_duty(int delta);
extern int pwmled_set_duty(int duty);
