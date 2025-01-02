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

uint8_t get_reset_flag()
{
    return reset_flag;
}

void set_reset_flag(uint8_t flag)
{
    reset_flag = flag;
}

void double_press_judge()
{
    // 检查定时器是否已经创建，避免重复创建
    if (doublePress_timer != NULL) {
        ESP_LOGW("KEY", "doublePress_timer已存在，跳过创建");
        return;
    }
    
    const esp_timer_create_args_t doublePress_timer_args = {
        .callback = &doublePress_timer_callback,
        .name = "doublePress" /* name is optional, but may help identify the timer when debugging */
    };
    
    esp_err_t ret = esp_timer_create(&doublePress_timer_args, &doublePress_timer);
    if (ret != ESP_OK) {
        ESP_LOGE("KEY", "创建doublePress_timer失败: %s", esp_err_to_name(ret));
        return;
    }
    
    ret = esp_timer_start_periodic(doublePress_timer, 1 * 1000 * 1000);
    if (ret != ESP_OK) {
        ESP_LOGE("KEY", "启动doublePress_timer失败: %s", esp_err_to_name(ret));
        esp_timer_delete(doublePress_timer);
        doublePress_timer = NULL;
    }
}


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

static void key_interrupt_task(void *arg)
{
    uint32_t io_num;
    uint8_t command = STOP;

    for (;;)
    {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY))
        {
            if (get_ota_status() == OTA_STATUS_ING)
            {
                continue;
            }
            // 干扰处理及按键滤波处理
            if (gpio_get_level(io_num) != 0)
            {
                ESP_LOGW("KEY", "滤波处理前按键电平不正确");
                continue;
            }
            else
            {
                sys_delay_ms(50);
                if (gpio_get_level(io_num) != 0)
                {
                    ESP_LOGW("KEY", "滤波处理后按键电平不正确");
                    continue;
                }
            }
            // 如果是按键1进入中断
            if (io_num == KEY_1_INPUT_IO)
            {    
                // 检查使用次数限制
                int count_limit = get_count_limit();
                ESP_LOGI("KEY", "当前剩余次数: %d", count_limit);
                
                // 如果次数为0，则不允许开始新的训练周期，播放结束语音
                if (count_limit == 0) {
                    ESP_LOGW("KEY", "使用次数已用完，不允许操作");
                    play_voice(FEEDTRAIN_END);
                    feed_func_set(FUNC_OFF);
                    beici_func_set(FUNC_OFF);//关闭贝茨模
                    heating_control(FUNC_OFF);//关闭加热模式
                    set_func_led(KEY_FUNC_OFF, FUNC_OFF);
                    key_status = KEY_FUNC_OFF;
                    continue;  // 跳过后续处理
                }
                
                // 移除立即扣减次数的逻辑，改为在红光模式开启30秒后扣减
                // 次数扣减逻辑已移至feed.c中的30秒延时后处理
                
#if HAVE_HEATING_MODE_KEY        
                switch (key_status)
                {
                    case KEY_FUNC_OFF:
                        key_status++;
                        feed_func_set(FUNC_ON);    // 开启红光模式
                        play_voice(FEED_LIGHT);
                        set_func_led(KEY_FUNC_MODE_1, FUNC_ON); // 设置红光指示灯亮起
                        break;
                    case KEY_FUNC_MODE_2:
                        key_status = KEY_FUNC_OFF;
                        beici_func_set(FUNC_OFF);//关闭贝茨模式
                        //发送语音"彩光模式训练完毕"
                        play_voice(COLORTRAIN_END);
                        set_func_led(KEY_FUNC_OFF, FUNC_OFF);
                        ESP_LOGI("KEY", "key_1_status =KEY_FUNC_OFF--1");
                        break;
                    case KEY_FUNC_HEAT:
                        key_status = KEY_FUNC_MODE_1;
                        save_heat_only_record();  // 保存加热历史记录
                        heating_control(FUNC_OFF);//关闭加热模式
                        feed_func_set(FUNC_ON); //开启红光模式
                        set_func_led(KEY_FUNC_MODE_1, FUNC_ON);
                        //发送语音"红光模式"
                        play_voice(FEED_LIGHT);
                        break;
                }
#else
                if (key_status == KEY_FUNC_MODE_1) // 如果当前是红光模式，则关闭
                {
                    key_status = KEY_FUNC_OFF;
                    play_voice(STOP);
                    feed_func_set(FUNC_OFF);
                    beici_func_set(FUNC_OFF);
                    set_func_led(KEY_FUNC_OFF, FUNC_OFF);
                }
                else if (key_status == KEY_FUNC_MODE_2) // 如果当前是彩光模式，则切换到红光
                {
                    play_voice(FEED_LIGHT);
                    beici_func_set(FUNC_OFF);
                    feed_func_set(FUNC_ON);    // 开启红光模式
                    set_func_led(KEY_FUNC_MODE_1, FUNC_ON);
                    key_status = KEY_FUNC_MODE_1;
                }
                else // 如果当前是关闭状态，则开启红光
                {
                    play_voice(FEED_LIGHT);
                    feed_func_set(FUNC_ON);
                    set_func_led(KEY_FUNC_MODE_1, FUNC_ON);
                    key_status = KEY_FUNC_MODE_1;
                }
#endif
            }
            // 如果是按键2进入中断
            if (io_num == KEY_2_INPUT_IO)
            {
                // 检查使用次数限制
                int count_limit = get_count_limit();
                ESP_LOGI("KEY", "按键2 - 当前剩余次数: %d", count_limit);
                
                // 如果次数为0，则不允许操作，播放彩光结束语音
                if (count_limit == 0) {
                    ESP_LOGW("KEY", "使用次数已用完，不允许加热操作");
                    play_voice(HEATING_END);
                    continue;  // 跳过后续处理
                }
                
#if HAVE_HEATING_MODE_KEY
                // 先检查红光最低电量（20%），不足则只提示语音，不切换状态
                if(!can_use_red_light_function() && (key_status == KEY_FUNC_OFF || key_status == KEY_FUNC_MODE_1 || key_status == KEY_FUNC_MODE_2 || key_status == KEY_FUNC_HEAT))
                {
                    ESP_LOGW("KEY", "电量不足20%%，禁用红光并关闭功能");
                    play_voice(POWER_WARNING);  // 播放低电量警告语音
                    save_heat_only_record();  // 低电量强制关闭时也保存加热记录
                    key_status = KEY_FUNC_OFF;
                    feed_func_set(FUNC_OFF);
                    beici_func_set(FUNC_OFF);//关闭贝茨模式
                    heating_control(FUNC_OFF);//关闭加热模式
                    set_func_led(KEY_FUNC_OFF, FUNC_OFF);
                    continue;  // 跳过后续处理，阻止功能开启
                }
                // 再检查加热电量（50%），不足则提示加热不可用
                else if(!can_use_heating_function() && (key_status == KEY_FUNC_OFF || key_status == KEY_FUNC_MODE_1 || key_status == KEY_FUNC_MODE_2))
                {
                    play_voice(DISABLE_HEAT);
                    key_status = KEY_FUNC_OFF;
                    feed_func_set(FUNC_OFF);
                    beici_func_set(FUNC_OFF);//关闭贝茨模式
                    heating_control(0);//关闭加热模式
                    set_func_led(KEY_FUNC_OFF, FUNC_OFF);
                }
                else{
                    switch (key_status)
                    {
                    case KEY_FUNC_OFF:
                        play_voice(HEATING);
                        heating_control(FUNC_ON);//开启加热模式
                        key_status = KEY_FUNC_HEAT;
                        //发送语音"开启加热"
                        set_func_led(KEY_FUNC_HEAT, FUNC_ON);
                        break;
                    case KEY_FUNC_MODE_1:   
                        play_voice(HEATING);                 
                        feed_func_set(FUNC_OFF); //关闭红光模式
                        heating_control(FUNC_ON);//开启加热模式
                        set_func_led(KEY_FUNC_HEAT, FUNC_ON);
                        key_status = KEY_FUNC_HEAT;
                        //发送语音"开启加热"
                        break;
                    default:
                        key_status = KEY_FUNC_OFF;
                        feed_func_set(FUNC_OFF);
                        beici_func_set(FUNC_OFF);//关闭贝茨模式
                        save_heat_only_record();  // 保存加热历史记录（如果有）
                        heating_control(0);//关闭加热模式
                        set_func_led(KEY_FUNC_OFF, FUNC_OFF);
                        //发送语音停止
                        play_voice(STOP);
                        ESP_LOGI("KEY", "key_1_status =KEY_FUNC_OFF--2");
                        break;
                    }
                }
#else
                if (key_status == KEY_FUNC_MODE_2) // 如果当前是彩光模式，则关闭
                {
                    key_status = KEY_FUNC_OFF;
                    play_voice(STOP);
                    feed_func_set(FUNC_OFF);
                    beici_func_set(FUNC_OFF);
                    set_func_led(KEY_FUNC_OFF, FUNC_OFF);
                }
                else if (key_status == KEY_FUNC_MODE_1) // 如果当前是红光模式，则切换到彩光
                {
                    play_voice(COLOR_LIGHT);
                    feed_func_set(FUNC_OFF);
                    beici_func_set(FUNC_ON);
                    set_func_led(KEY_FUNC_MODE_2, FUNC_ON);
                    key_status = KEY_FUNC_MODE_2;
                }
                else // 如果当前是关闭状态，则开启彩光
                {
                    play_voice(COLOR_LIGHT);
                    beici_func_set(FUNC_ON);
                    set_func_led(KEY_FUNC_MODE_2, FUNC_ON);
                    key_status = KEY_FUNC_MODE_2;
                }
#endif
            }
        }
    }
}



