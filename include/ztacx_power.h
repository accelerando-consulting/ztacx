x#pragma once

extern int power_setup();
extern int power_off();
extern int cmd_test_power(const struct shell *shell, size_t argc, char **argv);
extern int cmd_test_poweroff(const struct shell *shell, size_t argc, char **argv);
extern int cmd_test_idleoff(const struct shell *shell, size_t argc, char **argv);
