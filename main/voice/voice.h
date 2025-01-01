/**
 * @file voice.h
 * @brief 语音模块头文件
 * 
 * 该文件包含语音模块的所有声明和相关宏定义，负责控制WT588F语音芯片。
 */

#ifndef _VOICE_H_
#define _VOICE_H_

#include "My_system.h"

typedef enum {
    VOLUME_LEVEL_0 = 0xE0,
    VOLUME_LEVEL_1 = 0xE1,
    VOLUME_LEVEL_2 = 0xE4,
    VOLUME_LEVEL_3 = 0xE7,
    VOLUME_LEVEL_4 = 0xEA,
    VOLUME_LEVEL_5 = 0xED,
    VOLUME_LEVEL_MAX = 0xFF,
};

/**
 * @brief 设备工作模式定义
 * 
 * BUM_MODE设置为false表示正常工作模式，此时会初始化语音芯片
 * 设置为true时为调试模式，不会初始化语音功能
 */
#define BUM_MODE false

/**
 * @brief 语音功能启用标志，根据BUM_MODE值自动设置
 */
#define VOICE !BUM_MODE

// #define VOICE_FLAG true

void music_cycle(void);

/**
 * @brief 强制停止背景音乐
 * 
 * 立即停止背景音乐播放并重置状态标志
 */
void stop_background_music(void);

/**
 * @brief 初始化语音模块
 * 
 * 配置相关GPIO、创建任务和队列、设置音量等
 */
void voice_init(void);

/**
 * @brief 更新音量设置
 * 
 * 根据NVS存储的音量设置值，更新WT588F芯片音量
 */
void update_volume(void);

/**
 * @brief 播放指定的语音命令
 * 
 * 将语音命令加入消息队列，由语音任务处理
 * 
 * @param command 语音命令代码，定义在My_system.h的SCENE枚举中
 */
void play_voice(uint8_t command);

/**
 * @brief 停止当前语音播放
 * 
 * 立即停止当前正在播放的语音
 */
void stop_voice(void);

/**
 * @brief 控制语音芯片电源
 * 
 * @param power_on true为开启电源，false为关闭电源
 */
void voice_power_control_ext(bool power_on);

/**
 * @brief 语音模块测试功能
 * 
 * 测试语音芯片的基本功能，包括电源控制
 */
void test_voice_module(void);

/**
 * @brief 训练状态标志
 * 
 * 外部变量声明，表示设备是否处于训练状态
 */
extern bool training_flag;

#endif
