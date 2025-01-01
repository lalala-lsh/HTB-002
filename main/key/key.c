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
static void doublePress_timer_callback(void *arg)
{
    if (gpio_get_level(KEY_1_INPUT_IO) == 0 && gpio_get_level(KEY_2_INPUT_IO) == 0)
    {
        if (doublePress_count < 3)
        {
            doublePress_count++;
        }
        else if (doublePress_count >= 3)
        {
            doublePress_count = 0;
            ESP_LOGI("DOUBLE PRESS JUDGE", "restore factory settings");
            
            // 1. 先停止所有功能
            heating_control(FUNC_OFF);  // 关闭加热
            beici_func_set(FUNC_OFF);   // 关闭彩光
            feed_func_set(FUNC_OFF);    // 关闭红光
            dal_40hz_beep_control_switch(0);//关闭蜂鸣器

            
            // 2. 等待一段时间确保所有功能都已经停止
            vTaskDelay(pdMS_TO_TICKS(500));
            
            // 3. 重置所有设备参数到默认值（只重置前8个参数，跳过UDP_IP和PICTURE）
            for(int i = 0; i < DEVICE_PARA_MAX-3 ; i++) 
            {
                set_device_parameters(i, initialData_device_parameters[i]);
            }
            
            // 4. 更新所有功能的参数
            update_feed_duty();    // 更新红光强度
            update_volume();       // 更新音量
            update_feed_time();    // 更新红光时间
            update_color_time();   // 更新彩光时间
            update_heating_time(); // 更新加热时间
            update_beep_status();  // 更新蜂鸣器状态   
             
            
            // 5. 清除存储数据
            clear_device_parameters();
            clear_storage();  // 清除其他存储数据
            wifi_reset();     // 清除WiFi配置
            
            // 6. 设置重置标志
            reset_flag = 1;
            
            // 7. 延时后重启
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        }
    }
    else
    {
        doublePress_count = 0;
    }
}

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

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL); // 向指定的队列发送数据
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
                // 检查电量是否足够使用红光功能（20%阈值）
                if(!can_use_red_light_function() && (key_status == KEY_FUNC_OFF || key_status == KEY_FUNC_HEAT)) {
                    ESP_LOGW("KEY", "电量不足20%%，禁用红光并关闭功能");
                    play_voice(POWER_WARNING);  // 播放低电量警告语音
                    if (key_status == KEY_FUNC_HEAT) {
                        save_heat_only_record();
                    }
                    key_status = KEY_FUNC_OFF;
                    feed_func_set(FUNC_OFF);
                    beici_func_set(FUNC_OFF);//关闭贝茨模
                    heating_control(FUNC_OFF);//关闭加热模式
                    set_func_led(KEY_FUNC_OFF, FUNC_OFF);
                    continue;  // 跳过后续处理
                }
                
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
                    case KEY_FUNC_MODE_1:
                        key_status++;
                        //发送语音"彩光模式"
                        set_func_led(KEY_FUNC_MODE_2, FUNC_ON);//修复彩光指示灯亮起慢的问题 linjun
                        play_voice(COLOR_LIGHT);
                        feed_func_set(FUNC_OFF); //关闭红光模式
                        beici_func_set(FUNC_ON);//开启贝茨模式  
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
                    default:
                        key_status = KEY_FUNC_OFF;
                        feed_func_set(FUNC_OFF);
                        beici_func_set(FUNC_OFF);//关闭贝茨模
                        save_heat_only_record();  // 保存加热历史记录（如果有）
                        heating_control(FUNC_OFF);//关闭加热模式
                        set_func_led(KEY_FUNC_OFF, FUNC_OFF);
                        //发送语音停止
                        play_voice(STOP);
                        ESP_LOGI("KEY", "key_1_status =KEY_FUNC_OFF--2");
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
                    case KEY_FUNC_MODE_2:
                        play_voice(HEATING);
                        beici_func_set(FUNC_OFF); //关闭贝茨模式
                        heating_control(FUNC_ON);//开启加热模式
                        set_func_led(KEY_FUNC_HEAT, FUNC_ON);
                        key_status = KEY_FUNC_HEAT;
                        //发送语音"开启加热"
                        break;
                    case KEY_FUNC_HEAT:
                        play_voice(HEATING_END);
                        save_heat_only_record();  // 保存加热历史记录
                        heating_control(FUNC_OFF);//关闭加热模式
                        set_func_led(KEY_FUNC_OFF, FUNC_OFF);
                        key_status = KEY_FUNC_OFF;
                        //发送语音"关闭加热"
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

KEY_FUNC_E key_get_status(void)
{
    return key_status;
}

void key_set_status(KEY_FUNC_E keyfunc)
{
    key_status = keyfunc;
}

void key_func_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = GPIO_INPUT_PIN_SEL,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    
    gpio_config(&io_conf);

    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));                         // create a queue to handle gpio event from isr
    xTaskCreate(key_interrupt_task, "key_interrupt_task", 4096, NULL, PRIORITY_KEY_INTERRUPT_TASK, NULL); // start gpio task

    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT); // install gpio isr service

    gpio_isr_handler_add(KEY_1_INPUT_IO, gpio_isr_handler, (void *)KEY_1_INPUT_IO); // 添加中断处理函数
    gpio_isr_handler_add(KEY_2_INPUT_IO, gpio_isr_handler, (void *)KEY_2_INPUT_IO);

    double_press_judge();
}
