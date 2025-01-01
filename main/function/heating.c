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

static void heat_rf_shutdown(void)
{
    if (heat_rf_managed) {
        return;
    }

    esp_err_t err = esp_wifi_stop();
    if (err == ESP_OK) {
        heat_wifi_was_started = true;
        ESP_LOGI(TAG, "加热模式：Wi-Fi已停止");
    } else if (err == ESP_ERR_WIFI_NOT_STARTED || err == ESP_ERR_WIFI_NOT_INIT) {
        heat_wifi_was_started = false;
    } else {
        ESP_LOGW(TAG, "加热模式：esp_wifi_stop失败: %s", esp_err_to_name(err));
    }

    esp_bt_controller_status_t bt_status = esp_bt_controller_get_status();
    if (bt_status == ESP_BT_CONTROLLER_STATUS_ENABLED) {
        err = esp_bt_controller_disable();
        if (err == ESP_OK) {
            heat_bt_was_enabled = true;
            ESP_LOGI(TAG, "加热模式：BT已关闭");
        } else {
            ESP_LOGW(TAG, "加热模式：esp_bt_controller_disable失败: %s", esp_err_to_name(err));
        }
    } else {
        heat_bt_was_enabled = false;
    }

    heat_rf_managed = true;
    heat_rf_reopened = false;
}

static void heat_rf_restore(void)
{
    if (!heat_rf_managed) {
        return;
    }

    if (heat_wifi_was_started) {
        esp_err_t err = esp_wifi_start();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "加热模式：Wi-Fi已恢复");
        } else {
            ESP_LOGW(TAG, "加热模式：esp_wifi_start失败: %s", esp_err_to_name(err));
        }
    }

    if (heat_bt_was_enabled) {
        esp_bt_controller_status_t bt_status = esp_bt_controller_get_status();
        if (bt_status != ESP_BT_CONTROLLER_STATUS_ENABLED) {
            esp_err_t err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "加热模式：BT已恢复");
            } else if (err != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(TAG, "加热模式：esp_bt_controller_enable失败: %s", esp_err_to_name(err));
            }
        }
    }

    heat_rf_managed = false;
    heat_rf_reopened = true;
    heat_wifi_was_started = false;
    heat_bt_was_enabled = false;
}

//linjun
void update_heating_time(void)
{
    int heat_t = 0;

    heat_t = get_device_para(DEVICE_PARA_HEAT_T);
    heat_timecontrol = heat_t * 60;
}

uint8_t heat_set_status()
{
    return heat_status;
}


// 内部工具：立刻打断加热通道上的任何渐变，并把占空比拉成0
static inline void stop_heating_fade_immediately(void)
{
    // 停止任何渐变并关闭通道输出，避免下次开启异常
    ledc_fade_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3);
    // 直接停止通道输出，关闭加热PWM，占空比归零
    ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3, 0);
}

void heating_control(uint8_t en_flag)
{
    if(en_flag == 0){
#if 0
        gpio_set_level(HEAT_PWM_OUTPUT_IO, 0);
#else
        // 关闭加热时，先强制打断任何正在运行的硬件渐变，确保不会拖尾
        stop_heating_fade_immediately();
#endif
        s_heat_cnt = 0;
        heat_status = 0;//关闭加热，状态为0
        heat_start_mono_us = 0;
        dal_40hz_beep_control_switch(0);//LINJUN
        ESP_LOGI(TAG, "关闭加热功能");

        if (heat_rf_managed) {
            heat_rf_restore();
        }
    }else if(en_flag == 1){
        dal_40hz_beep_control_switch(1);//LINJUN
        heat_rf_reopened = false; // 每次开启加热都允许重新关射频
#if 0
        gpio_set_level(HEAT_PWM_OUTPUT_IO, 1);
#else
        // 初始PWM设置为10%（26），然后使用硬件渐变功能在60秒内升到75%（191）
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3, 26); // 10% 占空比
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3);
        // 设置硬件渐变：从当前值（26）渐变到191（75%），耗时60000毫秒（60秒）
        esp_err_t err = ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3, 191, 60000);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "设置加热渐变失败: %s", esp_err_to_name(err));
        }
        // 启动硬件渐变，不等待完成
        err = ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3, LEDC_FADE_NO_WAIT);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "启动加热渐变失败: %s，回退到固定占空比", esp_err_to_name(err));
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3, 191);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3);
        }
#endif
        s_heat_cnt = 0;
        heat_status = 1;//开启加热，状态为1
        heat_start_time = get_safe_timestamp();//记录加热开始时间
        heat_start_mono_us = esp_timer_get_time();
        // ESP_LOGI(TAG, "开启加热功能, 开始时间: %lld, PWM硬件渐变启动(10%%->75%%, 60秒)", heat_start_time);
    }
}

// 保存单独加热模式的历史记录（供外部调用）
void save_heat_only_record(void)
{
    if (heat_start_time == 0) {
        ESP_LOGW(TAG, "加热开始时间无效，跳过保存记录");
        return;
    }

    int64_t end_time = 0;
    int64_t end_mono_us = esp_timer_get_time();
    int work_time = (int)s_heat_cnt;

    if (heat_start_mono_us > 0 && end_mono_us > heat_start_mono_us) {
        work_time = (int)((end_mono_us - heat_start_mono_us) / 1000000);
    }

    // 时间源策略: 系统时间time()走时准确（无论是否SNTP），优先用它
    // end = time(), start = end - work_time, 保证 END-START==WORK_TIME
    time_t sys_time = time(NULL);
    if (sys_time >= 1704067200) {
        end_time = (int64_t)sys_time;
        heat_start_time = end_time - work_time;
    } else if (heat_start_time > 0) {
        // 系统时间无效，用start正推
        end_time = heat_start_time + work_time;
    } else {
        end_time = 0;
    }
    
    // 获取当前设定的加热时长（秒）
    int heat_t = get_device_para(DEVICE_PARA_HEAT_T);
    uint32_t heat_timecontrol_seconds = heat_t * 60;
    
    // 判断是否完整训练完成（达到设定时长，允许1秒误差）
    bool is_complete = (work_time >= (int)heat_timecontrol_seconds - 1);
    
    if (is_complete) {
        // 完整训练完成，无论时长多少都保存
        ESP_LOGI(TAG, "完整训练完成，保存记录: work_time=%d秒, 设定时长=%lu秒",
                 work_time, (unsigned long)heat_timecontrol_seconds);
    } else {
        // 中途停止，需要>3分钟才保存
        if (work_time < 180) {
            ESP_LOGI(TAG, "加热时间不足3分钟(%d秒)且未完整训练完成，跳过保存记录", work_time);
            heat_start_time = 0;
            return;
        }
        ESP_LOGI(TAG, "中途停止但时间>3分钟，保存记录: work_time=%d秒", work_time);
    }

    struct tm timeinfo;
    time_t now = (time_t)end_time;
    localtime_r(&now, &timeinfo);
    int day = timeinfo.tm_mday;
    uint32_t rcg_code = generate_rcg_code();

    ESP_LOGI(TAG, "保存加热记录: start=%lld, end=%lld, work_time=%d, 完整完成=%s", 
             heat_start_time, end_time, work_time, is_complete ? "是" : "否");

    if (get_wifi_status() == WIFI_STATE_CONNECTED && get_mqtt_status() == MQTT_EVENT_CONNECTED_) {
        build_feedreport(get_mqtt_client(), 0x03, heat_start_time, end_time, work_time, rcg_code, 0, 0, 0, 1);
    } else {
        store_record(0x03, heat_start_time, end_time, work_time, rcg_code, day, 0, 0, 0, 1, RECORD_FD);
    }

    heat_start_time = 0;  // 重置开始时间
    heat_start_mono_us = 0;
}

static void heat_task(void* arg){

    for(;;) {
        if (key_get_status() == KEY_FUNC_HEAT) {
            if (!heat_rf_reopened && !heat_rf_managed) {
                heat_rf_shutdown();
            }

            s_heat_cnt++;
            ESP_LOGI(TAG, "s_heating == FUNC_ON.....s_heat_cnt = %lu", s_heat_cnt);

            if (!heat_rf_reopened) {
                uint32_t reopen_at = (heat_timecontrol > 20) ? (heat_timecontrol - 20) : 0;
                if (s_heat_cnt >= reopen_at) {
                    heat_rf_restore();
                }
            }

            if (s_heat_cnt >= heat_timecontrol) {//linjun
                // 保存加热历史记录
                save_heat_only_record();

                set_func_led(KEY_FUNC_OFF, FUNC_OFF);//linjun  自动训练完后关闭状态指示灯
                heating_control(FUNC_OFF);
                key_set_status(KEY_FUNC_OFF);
                play_voice(HEATING_END);
                ESP_LOGI(TAG, "时间到-----关闭----加热功能");
            }
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);      // 每隔1s判断1次
    }
}

void heating_init(void)
{
    update_heating_time();//小程序更新加热时间    linjun
#if 0
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << HEAT_PWM_OUTPUT_IO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(HEAT_PWM_OUTPUT_IO, 0);
#else  
    
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_2,
        .duty_resolution  = LEDC_TIMER_8_BIT,
        .freq_hz          = 1000,  // 1 kHz
        .clk_cfg          = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_3,
        .timer_sel      = LEDC_TIMER_2,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = HEAT_PWM_OUTPUT_IO,
        .duty           = 0, // 初始占空比为0
        .hpoint         = 0,
    };
    ledc_channel_config(&ledc_channel);
    
    // 安装LEDC硬件渐变功能
    ledc_fade_func_install(0);
    ESP_LOGI(TAG, "LEDC硬件渐变功能已安装");
#endif

    xTaskCreate(heat_task, "heat_task", 4096, NULL, 11, NULL);
}
