/**
 * @file ota.c
 * @brief OTA升级模块实现文件
 * 
 * 该模块实现了基于HTTP的OTA固件升级功能。
 * 包括固件版本获取、固件下载、校验和安装过程。
 * 使用HTTP连接避免SSL内存不足问题。
 */

#include "ota.h"
#include "esp_wifi.h"
#include "voice.h"
#include "storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* 全局变量定义 */
static OTA_STATUS ota_status = OTA_STATUS_OFF;  /**< 当前OTA状态 */
static const char *TAG = "OTA";                 /**< 日志标签 */
char adress[100] = {0};                         /**< OTA固件下载地址 */

/* 外部引用声明 */
extern QueueHandle_t voice_evt_queue;

#define OTA_URL_SIZE 256

/**
 * @brief 将OTA错误码映射到编号
 * @param err 错误码
 * @return uint8_t 编号（1-9），0表示正常或未知错误，不记录
 */

/**
 * @brief 获取当前OTA状态
 * 
 * @return OTA_STATUS 当前OTA状态枚举值
 */

/**
 * @brief 设置OTA状态
 * 
 * @param set_ota_status 要设置的OTA状态
 */

/**
 * @brief 获取当前运行的固件版本
 * 
 * 从当前运行的分区获取固件版本信息
 * 
 * @param version 用于存储版本信息的字符数组
 */

/**
 * @brief OTA事件处理回调函数
 * 
 * 处理OTA升级过程中的各种事件，并更新OTA状态
 * 
 * @param arg 回调参数
 * @param event_base 事件基础类型
 * @param event_id 事件ID
 * @param event_data 事件数据
 */

/**
 * @brief 验证固件头部信息
 * 
 * 检查新固件的版本是否与当前运行版本相同，避免重复升级
 * 
 * @param new_app_info 新固件的应用信息描述符
 * @return esp_err_t ESP_OK表示验证通过，ESP_FAIL表示验证失败
 */

/**
 * @brief HTTP客户端初始化回调函数
 * 
 * 可以在此函数中添加自定义HTTP请求头
 * 
 * @param http_client HTTP客户端句柄
 * @return esp_err_t ESP_OK表示初始化成功
 */

/**
 * @brief OTA升级任务函数
 * 
 * 执行完整的OTA升级流程，包括：
 * 1. 配置HTTP客户端
 * 2. 初始化OTA句柄
 * 3. 下载和验证固件
 * 4. 完成升级并根据结果重启或报错
 * 
 * @param pvParameter 任务参数（未使用）
 */

/**
 * @brief 启动OTA升级流程
 * 
 * 初始化OTA相关组件，注册事件处理函数，创建OTA任务
 * 
 * @param adr OTA固件下载地址
 */


