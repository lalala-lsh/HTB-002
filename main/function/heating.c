#include "heating.h"
#include "key.h"
#include "led_status.h"
#include "power_manager.h"
#include "storage.h"
#include "voice.h"
#include "feed.h"
#include "driver/ledc.h"
#include "protocal.h"
#include "mqtt.h"
#include "blufi.h"
#include "clock.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_err.h"
#include "esp_wifi.h"

#define TAG                     "HEAT"

static uint32_t s_heat_cnt = 0;
static uint32_t heat_timecontrol = 15*60;//linjun
static uint8_t heat_status = 0;
static int64_t heat_start_time = 0;  // 加热开始时间戳
static int64_t heat_start_mono_us = 0;
static bool heat_rf_managed = false;
static bool heat_rf_reopened = false;
static bool heat_wifi_was_started = false;
static bool heat_bt_was_enabled = false;
// 外部引用当前设备参数值



//linjun



// 内部工具：立刻打断加热通道上的任何渐变，并把占空比拉成0


// 保存单独加热模式的历史记录（供外部调用）


