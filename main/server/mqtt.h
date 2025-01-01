/**
 * @file mqtt.h
 * @brief MQTT客户端模块头文件
 * 
 * 该模块负责与MQTT服务器通信，实现设备状态上报、命令接收和处理。
 * 包括MQTT连接状态管理、消息订阅和发布功能。
 */

#ifndef _MQTT_H_
#define _MQTT_H_

#include "My_system.h"

/**
 * @brief MQTT连接状态枚举
 */
typedef enum {
    MQTT_EVENT_IDLE_,            /**< 初始空闲状态 */
    MQTT_EVENT_CONNECTED_,       /**< 已连接到MQTT服务器 */
    MQTT_EVENT_DISCONNECTED_,    /**< 与MQTT服务器断开连接 */
    MQTT_EVENT_SUBSCRIBED_,      /**< 已成功订阅主题 */
    MQTT_EVENT_UNSUBSCRIBED_,    /**< 已取消订阅主题 */
    MQTT_EVENT_PUBLISHED_,       /**< 消息发布成功 */
    MQTT_EVENT_DATA_,            /**< 收到MQTT消息数据 */
    MQTT_EVENT_ERROR_,           /**< 发生MQTT错误 */
    MQTT_EVENT_BEFORE_CONNECT_,  /**< 连接前状态 */
} mqtt_state_t;

/**
 * @brief 初始化MQTT客户端
 * 
 * 配置并启动MQTT客户端，连接到服务器并注册事件处理
 */
void mqtt_func_init(void);

/**
 * @brief 获取当前MQTT连接状态
 * 
 * @return mqtt_state_t 当前MQTT状态
 */
mqtt_state_t get_mqtt_status(void);

/**
 * @brief 获取MQTT客户端句柄
 * 
 * @return esp_mqtt_client_handle_t MQTT客户端句柄，用于发布消息
 */
esp_mqtt_client_handle_t get_mqtt_client(void);

/**
 * @brief MQTT心跳发送任务
 * 
 * 定期向服务器发送心跳消息，维持连接并上报设备状态
 * 
 * @param arg 任务参数（未使用）
 */
void mqtt_send_task(void *arg);

/**
 * @brief 暂停MQTT心跳任务以避免SSL冲突
 * 
 * @param pause true暂停，false恢复
 */
void set_mqtt_heartbeat_pause(bool pause);

#endif /* _MQTT_H_ */