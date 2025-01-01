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

/**
 * @brief WT588F芯片2线制通信函数
 * 
 * 实现与WT588F芯片的2线通信协议，发送单字节命令
 * 
 * @param data 要发送的命令字节
 */

/**
 * @brief 循环播放当前背景音乐
 * 
 * 发送循环播放命令到WT588F芯片
 * 注意：确保MUSIC地址和循环命令0xF2的原子性，避免被其他语音命令打断
 */

/**
 * @brief 控制背景音乐循环播放
 * 
 * 根据设备参数决定是否启用背景音乐
 */

/**
 * @brief 强制停止背景音乐
 */

/**
 * @brief 统一处理语音播放后的音乐恢复逻辑
 * 
 * 该函数确保在训练模式下（FEED_ING或BEICI_ING）任何语音播放后都能正确恢复背景音乐
 * @param voice_cmd 刚播放完的语音命令
 * @param delay_ms 语音播放延时（毫秒），用于等待语音播放完成
 */

/**
 * @brief 更新音量设置
 * 
 * 根据存储的音量参数设置WT588F芯片音量
 * 音量范围：1-5级
 */

/**
 * @brief 控制语音芯片电源
 * 
 * @param power_on true为开启电源，false为关闭电源
 */

/**
 * @brief 播放语音命令
 * 
 * 将语音命令发送到消息队列，由语音任务处理
 * 
 * @param command 要播放的语音命令码
 */

/**
 * @brief 停止当前语音播放
 * 
 * 立即停止当前正在播放的语音
 */

/**
 * @brief 控制语音芯片电源（外部接口）
 * 
 * @param power_on true为开启电源，false为关闭电源
 */

/**
 * @brief 语音命令处理任务
 * 
 * 从消息队列接收语音命令并播放，处理不同命令后的延时和音乐循环控制
 * 
 * @param arg 任务参数（未使用）
 */

/**
 * @brief 语音模块测试功能
 * 
 * 测试语音芯片的基本功能，包括电源控制
 */

/**
 * @brief 语音模块初始化
 * 
 * 配置IO、创建任务和队列、设置初始音量
 */
