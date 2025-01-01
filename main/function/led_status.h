#ifndef LED_STATUS_H
#define LED_STATUS_H

#include "My_system.h"

void led_status_module_init(void);
void set_func_led(KEY_FUNC_E key, uint32_t onoff);
void set_status_led_color(uint8_t red, uint8_t green, uint8_t blue, uint8_t onoff);

#endif