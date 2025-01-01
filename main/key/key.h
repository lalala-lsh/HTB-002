#ifndef KEY_H
#define KEY_H

#include "My_system.h"

void key_func_init(void);
KEY_FUNC_E key_get_status(void);
void key_set_status(KEY_FUNC_E keyfunc);
void double_press_judge(void);

uint8_t get_reset_flag();
void set_reset_flag(uint8_t flag);

#endif /* KEY_H */