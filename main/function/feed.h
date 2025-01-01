#ifndef FEED_H
#define FEED_H

#include "My_system.h"
#include "image_transfer.h"  // 添加图片传输头文件

typedef enum {                     //定义哺光状态枚举
    FEED_NONE = 0x00,
    FEED_ING  = 0x01,
    BEICI_ING = 0x02
} FEED_STATUS_E;

typedef enum {                     //定义弱视训练状态枚举
    COLOR_LIGHT_NONE = 0x00,
    COLOR_LIGHT_ING  = 0x01,
    COLOR_LIGHT_END  = 0x02
} COLOR_LIGHT_STATUS_E;

// 添加颜色控制相关的定义
typedef enum {
    COLOR_RED = 0,
    COLOR_GREEN
} LED_COLOR_E;

void dal_40hz_beep_control_switch(uint8_t onoff); // 40hz蜂鸣器控制开关

void feed_func_set(uint32_t onoff);
void feed_ruoshi_moudle_init(void);
// void feed_set_status(FEED_STATUS_E status);

void beici_func_set(uint32_t onoff);


void update_feed_duty(void);
void update_feed_time(void);
void update_color_time(void);
bool update_beep_status();
void update_feeding_heat(void);

uint8_t feed_set_status();


#endif /* FEED_H */