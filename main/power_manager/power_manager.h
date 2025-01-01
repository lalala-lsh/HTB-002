#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include "My_system.h"

void power_manager_init(void);
int get_battery_power(void);
power_state_t get_power_state(void);
void send_battery_level(void);
void reset_battery_calibration(void);
bool can_use_heating_function(void);          // 检查是否可以使用加热功能
bool can_use_red_light_function(void);        // 检查是否可以使用红光功能（20%电量阈值）

#endif /* POWER_MANAGER_H */