/**
 * @file voice.c
 * @brief 语音模块实现文件
 * 
 * 该模块负责WT588F语音芯片的控制，提供语音播放、音量控制和背景音乐循环等功能。
 * 使用2线制（IO_CLK和IO_DATA）与芯片通信。
 */

#include "voice.h"
#include "storage.h"
#include "xl9535.h"
#include "feed.h"
#include "power_manager.h"
#include "heating.h"
#include "key.h"

/* 全局变量定义 */
bool training_flag = false;     /**< 表示设备是否处于训练状态，用于低电量语音提醒判断 */
bool VOICE_CHECK_FLAG = false;  /**< 语音播报后操作允许标志位 */
bool WELCOME_flag = false;      /**< 欢迎语音播放标志位 */
bool power_warn_flag = false;   /**< 电量警告标志位 */
bool bgm_playing = false;       /**< 背景音乐播放状态标志位 */
QueueHandle_t voice_evt_queue = NULL; /**< 语音命令消息队列句柄 */

const static char* TAG = "VOICE"; /**< 日志标签 */

/**
 * @brief 检查命令是否在指定的命令组中
 * 
 * @param cmd 要检查的命令
 * @param cmd_group 命令组数组
 * @param group_size 命令组大小（字节数）
 * @return bool 如果命令在组中返回true，否则返回false
 */
static bool is_cmd_in_group(uint8_t cmd, const uint8_t* cmd_group, uint8_t group_size)
{
    for (uint8_t i = 0; i < group_size; i++) {
        if (cmd == cmd_group[i]) {
            return true;
        }
    }
    return false;
}

/**
 * @brief WT588F芯片2线制通信函数
 * 
 * 实现与WT588F芯片的2线通信协议，发送单字节命令
 * 
 * @param data 要发送的命令字节
 */
static void Line_2A_WT588F(uint8_t data)
{
    uint8_t b_data;
    b_data = data & 0x01; // 获取待发送数据的最低位

    // 数据发送完成之后恢复高电平
    gpio_set_level(IO_CLK, 1);
    gpio_set_level(IO_DATA, 1);

    // 时钟线拉低，延时5ms
    gpio_set_level(IO_CLK, 0);
    esp_rom_delay_us(5000);

    // 进入数据发送的循环中
    for (int i = 0; i < 8; i++)
    {
        gpio_set_level(IO_CLK, 0); // 时钟线拉低
        gpio_set_level(IO_DATA, b_data);
        esp_rom_delay_us(1000);

        gpio_set_level(IO_CLK, 1);
        esp_rom_delay_us(1000);

        data = data >> 1;     // data右移1位，相当于删除原本最低位的数据
        b_data = data & 0x01; // 取处理后data的最后一位
    }

    // 数据发送完成之后恢复高电平
    gpio_set_level(IO_CLK, 1);
    gpio_set_level(IO_DATA, 1);
}

/**
 * @brief 循环播放当前背景音乐
 * 
 * 发送循环播放命令到WT588F芯片
 * 注意：确保MUSIC地址和循环命令0xF2的原子性，避免被其他语音命令打断
 */
static void drv_music_cycle_commd(void)
{
    ESP_LOGI(TAG, "发送背景音乐循环命令: MUSIC(0x%x) + 0xF2", MUSIC);
    
    // 暂停任务调度，确保背景音乐命令的原子性
    vTaskSuspendAll();
    
    // 先发送停止命令，确保芯片处于干净状态
    Line_2A_WT588F(0xFE);
    esp_rom_delay_us(2000); // 等待停止命令生效
    
    // 发送音乐地址
    Line_2A_WT588F(MUSIC);
    esp_rom_delay_us(10000); // 增加延时到10ms，确保地址命令被完全处理
    
    // 发送循环播放命令
    Line_2A_WT588F(0xF2); // 循环播放当前BGM
    esp_rom_delay_us(2000); // 等待循环命令生效
    
    // 恢复任务调度
    xTaskResumeAll();
    
    // WT588F芯片停止命令可能重置音量，需要重新设置（在任务调度恢复后）
    update_volume();
    esp_rom_delay_us(5000); // 等待音量设置生效
    
    ESP_LOGI(TAG, "背景音乐循环命令发送完成");
}

/**
 * @brief 控制背景音乐循环播放
 * 
 * 根据设备参数决定是否启用背景音乐
 */
void music_cycle(void)
{
    int bgm_onoff = FUNC_ON;
    uint8_t current_feed_status = feed_set_status();
    uint8_t current_key_status = key_get_status();

    bgm_onoff = get_device_para(DEVICE_PARA_BGM);
    
    ESP_LOGI(TAG, "music_cycle调用: bgm_onoff=%d, key_status=%d, feed_status=%d, bgm_playing=%d", 
             bgm_onoff, current_key_status, current_feed_status, bgm_playing);
    
    // 修复条件判断：增加训练状态检查，即使按键状态为OFF，如果仍在训练中也应该播放音乐
    bool should_play_bgm = false;
    
    if(bgm_onoff == FUNC_ON && (current_key_status == KEY_FUNC_MODE_1 || current_key_status == KEY_FUNC_MODE_2 || current_key_status == KEY_FUNC_HEAT || current_feed_status != FEED_NONE)) {
        should_play_bgm = true;
        ESP_LOGI(TAG, "满足播放条件: bgm_onoff=%d, key_status=%d, feed_status=%d", 
                 bgm_onoff, current_key_status, current_feed_status);
    }
    
    if(should_play_bgm) {
        if(!bgm_playing) {
            drv_music_cycle_commd();
            bgm_playing = true;
            ESP_LOGI(TAG, "启动背景音乐循环播放");
        } else {
            ESP_LOGI(TAG, "背景音乐状态为播放中，强制重启确保正常播放");
            drv_music_cycle_commd(); // 强制重新发送命令
            ESP_LOGI(TAG, "强制重启背景音乐完成");
        }
    } else {
        ESP_LOGI(TAG, "不满足播放条件或已关闭BGM");
        if(bgm_playing) {
            Line_2A_WT588F(0xFE); // 关闭BGM
            esp_rom_delay_us(2000); // 等待停止命令生效
            // WT588F芯片停止命令可能重置音量，需要重新设置
            update_volume();
            bgm_playing = false;
            ESP_LOGI(TAG, "停止背景音乐播放，已重新设置音量");
        }
    }
}   

/**
 * @brief 强制停止背景音乐
 */
void stop_background_music(void)
{
    Line_2A_WT588F(0xFE);
    esp_rom_delay_us(2000); // 等待停止命令生效
    // WT588F芯片停止命令可能重置音量，需要重新设置
    update_volume();
    bgm_playing = false;
    ESP_LOGI(TAG, "强制停止背景音乐，已重新设置音量");
}

/**
 * @brief 统一处理语音播放后的音乐恢复逻辑
 * 
 * 该函数确保在训练模式下（FEED_ING或BEICI_ING）任何语音播放后都能正确恢复背景音乐
 * @param voice_cmd 刚播放完的语音命令
 * @param delay_ms 语音播放延时（毫秒），用于等待语音播放完成
 */
static void handle_voice_music_recovery(uint8_t voice_cmd, uint32_t delay_ms)
{
    // 等待语音播放完成
    vTaskDelay(delay_ms / portTICK_PERIOD_MS);
    
    uint8_t current_feed_status = feed_set_status();
    uint8_t current_heat_status = heat_set_status();
    
    // 在训练模式下，任何语音播放后都需要恢复背景音乐
    if(current_feed_status == FEED_ING || current_feed_status == BEICI_ING || current_heat_status == 1) {
        ESP_LOGI(TAG, "训练模式下语音播放后恢复音乐: cmd=0x%x, feed_status=%d, delay=%lums", 
                 voice_cmd, current_feed_status, delay_ms);
        music_cycle(); // 使用统一的音乐控制函数
        return;
    }
    
    // 非训练模式下，确保停止播放
    ESP_LOGI(TAG, "非训练模式下语音播放后停止: cmd=0x%x, feed_status=%d", 
             voice_cmd, current_feed_status);
    Line_2A_WT588F(0xFE); // 停止播放
}

/**
 * @brief 更新音量设置
 * 
 * 根据存储的音量参数设置WT588F芯片音量
 * 音量范围：1-5级
 */
void update_volume(void)
{
    int volume = 1; // 默认音量值

    volume = get_device_para(DEVICE_PARA_VOL);
    uint8_t command = VOLUME_LEVEL_0;
    switch(volume) {
        case 1:
            command = VOLUME_LEVEL_1;
            break;
        case 2:
            command = VOLUME_LEVEL_2;
            break;
        case 3:
            command = VOLUME_LEVEL_3;
            break;
        case 4:
            command = VOLUME_LEVEL_4;
            break;
        case 5:
            command = VOLUME_LEVEL_5;
            break;
        default:
            ESP_LOGE(TAG, "音量设置超出限制");
            break;
    }
    Line_2A_WT588F(command); 
    ESP_LOGI(TAG, "已经更新音量到 %d", volume);
}

/**
 * @brief 控制语音芯片电源
 * 
 * @param power_on true为开启电源，false为关闭电源
 */
static void voice_power_control(bool power_on)
{
    if(power_on) {
        // 开启语音芯片电源
        Xl9535_Set_Io_Status(PIN_P07, IO_HIGH);
        vTaskDelay(100 / portTICK_PERIOD_MS); // 等待电源稳定
        
        // 重新初始化时钟线和数据线
        gpio_set_level(IO_CLK, 1);
        gpio_set_level(IO_DATA, 1);
        
        // 重新设置音量
        update_volume();
        
        ESP_LOGI(TAG, "语音芯片电源已开启");
    } else {
        // 关闭语音芯片电源
        Xl9535_Set_Io_Status(PIN_P07, IO_LOW);
        ESP_LOGI(TAG, "语音芯片电源已关闭");
    }
}

/**
 * @brief 播放语音命令
 * 
 * 将语音命令发送到消息队列，由语音任务处理
 * 
 * @param command 要播放的语音命令码
 */
void play_voice(uint8_t command)
{
    xQueueSend(voice_evt_queue, &command, 0);
}

/**
 * @brief 停止当前语音播放
 * 
 * 立即停止当前正在播放的语音
 */
void stop_voice(void)
{
    uint8_t stop_cmd = STOP;
    xQueueSend(voice_evt_queue, &stop_cmd, 0);
}

/**
 * @brief 控制语音芯片电源（外部接口）
 * 
 * @param power_on true为开启电源，false为关闭电源
 */
void voice_power_control_ext(bool power_on)
{
    voice_power_control(power_on);
}

/**
 * @brief 语音命令处理任务
 * 
 * 从消息队列接收语音命令并播放，处理不同命令后的延时和音乐循环控制
 * 
 * @param arg 任务参数（未使用）
 */
static void voice_task(void* arg)
{
    uint8_t cmd;
    
    // 重新定义命令类型分组，HEATING单独处理
    const uint8_t TRAINING_START_CMDS[] = {COLOR_LIGHT, FEED_LIGHT};  // 移除HEATING
    const uint8_t TRAINING_END_CMDS[] = {FEEDTRAIN_END, COLORTRAIN_END, HEATING_END};
    const uint8_t COUNTDOWN_CMDS[] = {COUNT_DOWN_1, COUNT_DOWN_2, COUNT_DOWN_3, COUNT_DOWN_4, COUNT_DOWN_5};
    const uint8_t REMIND_CMDS[] = {OPEN_EYES, WEAR_REMIND};
    const uint8_t OTA_CMDS[] = {OTA_START, OTA_SUCCESS, OTA_FAIL};
    // 定义需要停止背景音乐的命令
    const uint8_t STOP_BGM_CMDS[] = {WELCOME, OTA_START, OTA_SUCCESS, OTA_FAIL, FEEDTRAIN_END, COLORTRAIN_END, HEATING_END};
    
    while(true) {
        if(xQueueReceive(voice_evt_queue, &cmd, portMAX_DELAY)) {
            // 检查队列中是否有其他命令待处理
            int queue_length = uxQueueMessagesWaiting(voice_evt_queue);
            
            // OTA相关命令强制播放，不受队列长度限制
            bool is_ota_cmd = is_cmd_in_group(cmd, OTA_CMDS, sizeof(OTA_CMDS));
            
            if (queue_length != 0 && !is_ota_cmd) {
                ESP_LOGI(TAG, "消息队列剩余消息：%d, 跳过执行该条", queue_length);
                continue;
            } else if (is_ota_cmd) {
                ESP_LOGI(TAG, "OTA语音命令0x%x强制播放，队列剩余消息：%d", cmd, queue_length);
            }
            
            WELCOME_flag = true;
            
#if DEBUG_
            ESP_LOGI(TAG, "调用语音指令：0x%x", cmd);
#endif
            
            // STOP命令单独处理
            if(cmd == STOP) {
                training_flag = false;
                stop_background_music(); // 停止背景音乐
                Line_2A_WT588F(0xFE); // 停止播放
                continue;
            }
            
            // 只对特定命令停止背景音乐，避免重复启动
            if(is_cmd_in_group(cmd, STOP_BGM_CMDS, sizeof(STOP_BGM_CMDS))) {
                stop_background_music();
                vTaskDelay(100 / portTICK_PERIOD_MS); // 延时确保停止命令生效
            }
            
            // 在播放语音命令前，先确保停止当前播放状态
            // 这样避免语音命令与背景音乐命令冲突
            if(cmd != STOP) {
                Line_2A_WT588F(0xFE); // 先停止当前播放
                esp_rom_delay_us(2000); // 等待停止命令生效
                // WT588F芯片停止命令可能重置音量，需要重新设置
                update_volume();
                esp_rom_delay_us(1000); // 等待音量设置生效
            }
            
            // 播放指定命令
            Line_2A_WT588F(cmd);
            
            // 添加调试日志
            ESP_LOGI(TAG, "播放命令: 0x%x, training_flag=%d, feed_status=%d, key_status=%d", 
                     cmd, training_flag, feed_set_status(), key_get_status());
            
            // 特殊情况：在非训练状态下收到提醒命令，立即停止播放
            if(feed_set_status() == FEED_NONE && 
               is_cmd_in_group(cmd, REMIND_CMDS, sizeof(REMIND_CMDS))) {
                ESP_LOGI(TAG, "非训练状态下的提醒命令，停止播放");
                Line_2A_WT588F(0xFE); 
            }
            
            // 根据命令类型执行相应操作
            if(cmd == WELCOME) {
                // 欢迎语音，延时较长
                vTaskDelay(3500 / portTICK_PERIOD_MS);
                // 播放完成后停止，避免重复播放
                Line_2A_WT588F(0xFE);
            } 
            else if(is_cmd_in_group(cmd, TRAINING_START_CMDS, sizeof(TRAINING_START_CMDS))) {
                // 训练开始类命令（COLOR_LIGHT, FEED_1）
                training_flag = true; // 设置训练标志
                handle_voice_music_recovery(cmd, 1200); // 使用统一的恢复逻辑
            }
            else if(cmd == HEATING) {
                // 加热命令单独处理
                training_flag = true; // 设置训练标志
                handle_voice_music_recovery(cmd, 1200); // 使用统一的恢复逻辑
            }
            else if(cmd == DISABLE_HEAT) {
                // 关闭加热，不影响训练状态
                handle_voice_music_recovery(cmd, 2500); // 使用统一的恢复逻辑
            } 
            else if(is_cmd_in_group(cmd, TRAINING_END_CMDS, sizeof(TRAINING_END_CMDS))) {
                // 训练结束类命令
                training_flag = false;
                vTaskDelay(2500 / portTICK_PERIOD_MS);
                Line_2A_WT588F(0xFE); // 停止播放
            } 
            else if(is_cmd_in_group(cmd, COUNTDOWN_CMDS, sizeof(COUNTDOWN_CMDS))) {
                // 倒计时命令，在训练模式下需要恢复音乐
                handle_voice_music_recovery(cmd, 2000); // 使用统一的恢复逻辑
            } 
            else if(is_cmd_in_group(cmd, REMIND_CMDS, sizeof(REMIND_CMDS))) {
                // 提醒命令，在训练模式下需要恢复音乐
                ESP_LOGI(TAG, "REMIND命令处理: cmd=0x%x, feed_status=%d", cmd, feed_set_status());
                handle_voice_music_recovery(cmd, 2400); // 使用统一的恢复逻辑
            } 
            else if(cmd == POWER_WARNING && training_flag == true) {
                // 训练模式下跳过低电量语音播报，只记录日志
                ESP_LOGI(TAG, "训练模式下跳过低电量语音播报: cmd=0x%x", cmd);
                // 不播放语音，不执行任何音频操作
            } 
            else if(is_cmd_in_group(cmd, OTA_CMDS, sizeof(OTA_CMDS))) {
                // OTA相关命令
                vTaskDelay(2500 / portTICK_PERIOD_MS);
                // 播放完成后停止，避免重复播放
                Line_2A_WT588F(0xFE);
            }
            else {
                // 其他命令，播放完成后使用统一的音乐恢复逻辑
                handle_voice_music_recovery(cmd, 2500); // 使用统一的恢复逻辑
            }

            // 让出一次调度，避免长时间占用CPU触发WDT
            vTaskDelay(1);
        }
    }
}

/**
 * @brief 语音模块测试功能
 * 
 * 测试语音芯片的基本功能，包括电源控制
 */
void test_voice_module(void)
{
    ESP_LOGI(TAG, "开始语音模块测试");
    
    // 测试电源控制
    voice_power_control(false);
    vTaskDelay(500 / portTICK_PERIOD_MS);
    voice_power_control(true);
    vTaskDelay(500 / portTICK_PERIOD_MS);
    
    // 测试简单语音播放
    Line_2A_WT588F(WELCOME);
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    Line_2A_WT588F(0xFE); // 停止播放
    
    ESP_LOGI(TAG, "语音模块测试完成");
}

/**
 * @brief 语音模块初始化
 * 
 * 配置IO、创建任务和队列、设置初始音量
 */
void voice_init(void)
{
    gpio_config_t io_conf = {
#if BUM_MODE
        .pin_bit_mask = (1ULL << IO_CLK) | (1ULL << IO_DATA) | (1ULL << IO_BUSY) | (1ULL << IO_FREE),
#else
        .pin_bit_mask = (1ULL << IO_CLK) | (1ULL << IO_DATA),
#endif
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

#if !BUM_MODE
    // 音频芯片电源使能IO
    Xl9535_Set_Io_Direction(PIN_P07, IO_OUTPUT);
    Xl9535_Set_Io_Status(PIN_P07, IO_HIGH);
    
    // 时钟线和数据线默认高电平
    gpio_set_level(IO_CLK, 1);
    gpio_set_level(IO_DATA, 1);
    
    // 初始化音量大小
    update_volume();
    // 创建voice相关的消息队列
    voice_evt_queue = xQueueCreate(15, sizeof(uint8_t));
    // 创建任务
    xTaskCreate(voice_task, "voice_task", 4096, NULL, PRIORITY_VOICE_TASK, NULL);
    
    ESP_LOGI(TAG, "语音模块初始化完成");
    
    // 可选择在初始化后关闭电源以节省功耗
    // 取消注释下面的代码行来启用电源管理模式
    // voice_power_control(false);
    
    // 进行模块测试（可选）
    // test_voice_module();
#endif
}
