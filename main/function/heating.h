#ifndef HEADING_H
#define HEADING_H

#include "My_system.h"
void update_heating_time();
uint8_t heat_set_status();
void heating_init(void);
void heating_control(uint8_t);
void save_heat_only_record(void);  // 保存单独加热模式的历史记录

#endif