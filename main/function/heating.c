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
