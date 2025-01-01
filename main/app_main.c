/**
 * @file main.c
 * @brief 主程序入口文件，包含系统初始化和状态LED处理。
 *
 * @details 本文件实现了主函数和状态LED任务，用于根据系统状态（WiFi、MQTT、电源）控制LED的显示状态，
 *          并处理OTA状态和RTC时间设置等功能。
 */

#include "My_system.h"

#include "mqtt.h"
#include "power_manager.h"
#include "blufi.h"
#include "ds1302.h"
#include "led_status.h"
#include "ota.h"
#include "clock.h"
#include "key.h"
#include "camera.h"
#include "imgfile_save.h"

// 定义主模块的静态日志标签
static const char *TAG = "MAIN";

// 外部设备和标志位定义
extern DS1302_Dev ds1302_dev;      // 实时时钟设备结构体
extern bool readFlag;              // 读取操作的标志位
extern bool RTCTimeSet_flag;       // 检查RTC时间是否已设置的标志位
extern QueueHandle_t voice_evt_queue;  // 语音事件队列

/**
 * @brief 状态 LED 任务
 *
 * @param arg 任务参数（未使用）
 *
 * @details 根据系统的 WiFi、MQTT、电源状态更新 LED 的颜色和闪烁状态，
    *          处理 OTA 状态指示，并在需要时设置 RTC 时间或重启系统。
 */

/**
 * @brief 主应用入口点
 *
 * @details 该函数用于初始化系统并创建状态LED任务。
 */
