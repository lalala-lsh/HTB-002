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
static void status_led_task(void *arg) 
{
    // 定义WiFi、MQTT和电源状态的本地变量
    wifi_state_t wifi_status = WIFI_STATE_IDLE;
    mqtt_state_t mqtt_status = MQTT_EVENT_IDLE_;
    power_state_t power_state = POWER_STATE_NORMAL;

    uint8_t work_led = 0;             // LED状态变量（用于切换LED闪烁）
    uint8_t reset_flag_count = 0;     // 重置标志处理的计数器
    uint32_t time_sync_counter = 0;   // 时间同步检查计数器

    // 无限循环，根据系统状态更新LED状态
    while (1)
    {
        // 定期检查并同步RTC和系统时间(每30秒检查一次)
        if (++time_sync_counter >= 30) {
            time_sync_counter = 0;
            sync_system_with_rtc_if_needed();
        }
        
        // 获取当前的MQTT、WiFi和电源状态
        mqtt_status = get_mqtt_status();
        wifi_status = get_wifi_status();
        power_state = get_power_state();

        // 切换LED状态以产生闪烁效果
        work_led = !work_led;

        // 根据系统状态设置LED颜色和闪烁模式
        if(power_state == POWER_STATE_LOW)
        {
            // 如果电源电压低，设置LED为橙色闪烁
            set_status_led_color(1, 1, 0, work_led);//linjun
        }
        else
        {
            if (mqtt_status == MQTT_EVENT_CONNECTED_)      // MQTT连接成功
            {    
                set_status_led_color(0, 1, 0, work_led); // 绿色LED闪烁     linjun
            }
            else
            {
                if (wifi_status == WIFI_STATE_CONNECTED) // WiFi连接成功
                {     
                    set_status_led_color(1, 0, 0, work_led); // 红色LED闪烁     linjun
                } 
                else // WiFi未连接（MQTT未连接）
                { 
                    set_status_led_color(0, 0, 1, work_led); // 蓝色LED闪烁     linjun
                }
            }
        }

        // 处理 OTA（空中下载）更新的 LED 状态
        OTA_STATUS ota_status = get_ota_status();
        switch(ota_status){
            case OTA_STATUS_OFF:
                // 无 OTA 操作，不做任何处理
                break;
            case OTA_STATUS_ING:
                // OTA 进行中，打开功能 LED
                key_set_status(KEY_FUNC_OTA_ING);
                set_func_led(KEY_FUNC_OTA_ING, FUNC_ON);
                break;
            case OTA_STATUS_FAIL:
                // OTA 失败，关闭功能 LED 并重置状态
                key_set_status(KEY_FUNC_OFF);
                set_func_led(KEY_FUNC_OFF, FUNC_OFF);
                set_ota_status(OTA_STATUS_OFF);
                break;
            case OTA_STATUS_SUCCESS:
                // OTA 成功，关闭功能 LED
                key_set_status(KEY_FUNC_OFF);
                set_func_led(KEY_FUNC_OFF, FUNC_OFF);
                break;
        }

        // 如果 RTC 时间尚未设置，则设置 RTC 时间
        if(!RTCTimeSet_flag){
            RTCTimeSet_flag = RTCTimeToSystemTime();
        }

        // 处理复位条件，通过计数并重启系统
        if(get_reset_flag() == 1){ 
            if(reset_flag_count < 10){
                reset_flag_count++;
            } else {
                reset_flag_count = 0;
                set_reset_flag(0);
                esp_restart();  // 重启 ESP 系统
            }
            vTaskDelay(200 / portTICK_PERIOD_MS);
        } else{
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }
}

/**
 * @brief 主应用入口点
 *
 * @details 该函数用于初始化系统并创建状态LED任务。
 */
void app_main(void)
{   
    // 系统初始化函数
    SystemInit();

    // 创建用于处理状态LED的任务
    xTaskCreate(status_led_task, "status_led_task", 4096, NULL, PRIORITY_STATUS_LED_TASK, NULL);

    // 主循环以防止应用退出
    while (1)
    {
        // 延时以让出控制并减少CPU负载
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
