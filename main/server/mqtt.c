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
static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            client_now = client;
            mqtt_state = MQTT_EVENT_CONNECTED_;
            ESP_LOGI(TAG, "MQTT Event connected");
            esp_mqtt_client_subscribe(client, get_sub_tpc(), 1);   // 订阅主题
            build_versionmessage(client);                          // 版本校验信息上传
            build_storagemessage(client);                          // 存储数据上传
            xTaskCreate(mqtt_send_task, 
                            "mqtt_hb", 
                            8192, NULL,  // 增加栈大小到8KB，防止SSL传输时栈溢出
                            PRIORITY_MQTT_SEND_TASK, 
                            NULL);    // 创建心跳任务
            ESP_LOGI(TAG, "心跳的循环事件开启");
            break;

        case MQTT_EVENT_DISCONNECTED:
            mqtt_state = MQTT_EVENT_DISCONNECTED;
            ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "sent publish successful");
            break;

        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_DATA:
            // 如果正在上传图片，延迟处理MQTT数据避免SSL冲突
            if (get_upload_status()) {
                ESP_LOGD(TAG, "图片上传中，延迟处理MQTT数据");
                return ESP_OK;  // 暂时跳过处理
            }
            
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            // 只打印有效JSON部分
            char *end_of_json = strstr(event->data, "}");
            if (end_of_json) {
                ESP_LOGI(TAG, "DATA=%.*s",
                        (int)(end_of_json - event->data + 1), event->data);
            }
            process_msg(client, event->data, event->data_len);
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
            break;

        case MQTT_EVENT_BEFORE_CONNECT: 
            break;

        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
    return ESP_OK;
}

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
static void mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRId32, base, event_id);
    mqtt_event_handler_cb(event_data);
}

/**
 * @brief 初始化MQTT客户端
 * 
 * 配置MQTT连接参数，初始化客户端并启动连接
 */
extern char device_version[20];
void mqtt_func_init(void)
{
    // 幂等性检查：如果MQTT已连接，直接返回
    if (client_now != NULL && mqtt_state == MQTT_EVENT_CONNECTED_) {
        ESP_LOGI(TAG, "MQTT already connected, skip initialization");
        return;
    }
    // 上传期间避免重建MQTT，防止SSL/transport被破坏
    if (get_upload_status()) {
        ESP_LOGW(TAG, "上传中，跳过mqtt_func_init");
        return;
    }
    
    ESP_LOGI("GOT_IP", "网络连接创建成功，调用MQTT初始化功能");
    
    // 准备客户端ID和设备信息
    memcpy(c_user_data, user_data, sizeof(user_data));
    snprintf(c_client_id, sizeof(c_client_id), "htb-%s", c_user_data);

    // 获取当前固件版本
    getVersion(device_version);

    // 重置MQTT状态和客户端
    mqtt_state = MQTT_EVENT_IDLE_;
    if(client_now != NULL) {
        esp_err_t stop_ret = esp_mqtt_client_stop(client_now);
        if (stop_ret != ESP_OK) {
            ESP_LOGW(TAG, "MQTT stop failed: %s", esp_err_to_name(stop_ret));
        }
        esp_mqtt_client_destroy(client_now);
        client_now = NULL;
    }
    
    // 配置MQTT客户端
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = URI,
        .credentials.username = "htb_device",
        .credentials.authentication.password = password,
        .credentials.client_id = c_client_id,
        .network.timeout_ms = 10 * 1000,
        .task = {
            .stack_size = 8192,  // 增加MQTT主任务栈大小到8KB，防止栈溢出
            .priority = 5        // 设置任务优先级  
        }
    }; 
    
    // 初始化并启动MQTT客户端
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    
    // 安全启动，不使用 ESP_ERROR_CHECK
    esp_err_t ret = esp_mqtt_client_start(client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MQTT start failed: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "mqtt server was started successfully");
}

/**
 * @brief MQTT心跳发送任务
 * 
 * 定期向服务器发送心跳消息，保持连接活跃
 * 
 * @param arg 任务参数（未使用）
 */
void mqtt_send_task(void *arg)
{
    while (1) {
        // 检查是否正在上传图片或手动暂停，避免SSL冲突
        if (!get_upload_status() && !heartbeat_paused) {
            build_heartbeat(get_mqtt_client());
        } else {
            ESP_LOGD(TAG, "心跳已暂停（上传状态:%d, 手动暂停:%d）", get_upload_status(), heartbeat_paused);
        }
        vTaskDelay(pdMS_TO_TICKS(INTERVA_HEARTBEAT));  // 心跳30s发送一次
    }
}

void set_mqtt_heartbeat_pause(bool pause)
{
    heartbeat_paused = pause;
    ESP_LOGI(TAG, "MQTT心跳%s", pause ? "已暂停" : "已恢复");
}
