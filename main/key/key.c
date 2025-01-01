#include "key.h"

#include "protocal.h"
#include "blufi.h"
#include "ota.h"
#include "feed.h"
#include "led_status.h"
#include "feed.h"
#include "heating.h"
#include "power_manager.h"
#include "storage.h"  // 添加这个头文件，包含设备参数相关函数
#include "voice.h"    // 添加这个头文件，包含音量更新函数


static QueueHandle_t gpio_evt_queue = NULL; // idf 5.0.1版本
int doublePress_count = 0;
// uint8_t doublePress_flag = 0;
static uint8_t reset_flag = 0; // 用于设置指示灯快闪
extern QueueHandle_t voice_evt_queue;
extern int initialData_device_parameters[];

bool mode_changed = false;
bool auto_switch = true;

/* 双按键同时按下的判定 */
esp_timer_handle_t doublePress_timer = NULL;





static KEY_FUNC_E key_status = KEY_FUNC_OFF;

/*
2024-12-05
按键一
概述：该按键为红光训练功能和彩光训练功能
1、短按1次，红光功能+加热功能，时间默认3分钟，红光强度默认80%，摄像头启动睁眼闭眼识别，语音提醒"模式一，红光功能"，再短按1次关闭功能
2、再短1次，红光功能关闭，彩光训练功能开启，默认5分钟，加热不开启，摄像头启动睁眼闭眼识别，语音提醒"模式二，彩光功能"，
3、再短按1次，关闭功能

按键二
概述：该按键为加热功能
1、短按一次，开启加热，默认15分钟，语音提醒"加热功能已打开"
2、再短按一次，关闭加热，语音提醒"加热功能已关闭"
*/




