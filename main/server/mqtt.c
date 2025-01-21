/**
 * @file mqtt.c
 * @brief MQTT客户端模块实现文件
 * 
 * 实现了MQTT客户端的功能，包括连接管理、事件处理、消息收发等。
 * 支持设备状态上报、心跳维持和服务器命令处理。
 */

#include "mqtt.h"
#include "blufi.h"
#include "protocal.h"
#include "ota.h"
#include "image_transfer.h"  // 添加图片传输头文件，用于检查上传状态

/* 全局变量定义 */
static const char *TAG = "MQTT";                       /**< 日志标签 */
static mqtt_state_t mqtt_state = MQTT_EVENT_IDLE_;     /**< MQTT连接状态 */
static esp_mqtt_client_handle_t client_now = NULL;     /**< MQTT客户端句柄 */
static bool heartbeat_paused = false;                  /**< 心跳暂停标志 */

/* MQTT服务器配置 */
#if ON_LINE_SERVER
const char *URI = "mqtt://emqx.eyenice.cn:1883";       /**< 在线服务器地址 */
const char *password = "6g7JqYe1mRmq0ZY_";            /**< 在线服务器密码 */
#else 
const char *URI = "mqtt://110.87.103.170:6883";        /**< 本地服务器地址 */
const char *password = "Dj7Byhm!ZX3aE8V8";            /**< 本地服务器密码 */
#endif

/* 外部引用声明 */
extern uint8_t user_data[24];                         /**< 用户数据，包含设备SN码 */
static char c_user_data[24];                          /**< 用户数据字符串 */
static char c_client_id[30];                          /**< MQTT客户端ID */

/**
 * @brief 获取MQTT客户端句柄
 * 
 * @return esp_mqtt_client_handle_t MQTT客户端句柄
 */
esp_mqtt_client_handle_t get_mqtt_client()
{
    if (client_now == NULL) {
        ESP_LOGI(TAG, "GET MQTT CLIENT NULL");
    }
    return client_now;
}

/**
 * @brief 获取当前MQTT连接状态
 * 
 * @return mqtt_state_t 当前MQTT状态
 */
mqtt_state_t get_mqtt_status()
{
    return mqtt_state;
}

/**
 * @brief MQTT事件处理回调函数
 * 
 * 处理MQTT连接、订阅、数据接收等事件
 * 
 * @param event MQTT事件句柄
 * @return esp_err_t ESP_OK表示处理成功
 */

/**
 * @brief MQTT事件处理分发函数
 * 
 * 负责将MQTT事件分发到回调函数处理
 * 
 * @param handler_args 处理器参数
 * @param base 事件基础类型
 * @param event_id 事件ID
 * @param event_data 事件数据
 */

/**
 * @brief 初始化MQTT客户端
 * 
 * 配置MQTT连接参数，初始化客户端并启动连接
 */
extern char device_version[20];

/**
 * @brief MQTT心跳发送任务
 * 
 * 定期向服务器发送心跳消息，保持连接活跃
 * 
 * @param arg 任务参数（未使用）
 */

