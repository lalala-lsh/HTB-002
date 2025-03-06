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
OTA_STATUS get_ota_status()
{
    return ota_status; 
}

/**
 * @brief 设置OTA状态
 * 
 * @param set_ota_status 要设置的OTA状态
 */
void set_ota_status(OTA_STATUS set_ota_status)
{
    ota_status = set_ota_status;
}

/**
 * @brief 获取当前运行的固件版本
 * 
 * 从当前运行的分区获取固件版本信息
 * 
 * @param version 用于存储版本信息的字符数组
 */
void getVersion(char* version)
{
    const esp_partition_t* running_partition = esp_ota_get_running_partition();

    if (running_partition) {
        esp_app_desc_t app_desc;
        esp_err_t err = esp_ota_get_partition_description(running_partition, &app_desc);

        if (err == ESP_OK) {
            strcpy(version, app_desc.version);
        } else {
            ESP_LOGE(TAG, "Error getting partition description: %d", err);
            strcpy(version, "wrong version");
        }
    } else {
        strcpy(version, "no partition");
    }
}

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
static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == ESP_HTTPS_OTA_EVENT) {
        switch (event_id) {
            case ESP_HTTPS_OTA_START:
                ESP_LOGI(TAG, "OTA started");
                ota_status = OTA_STATUS_ING;
               
                break;
            case ESP_HTTPS_OTA_CONNECTED:
                ESP_LOGI(TAG, "Connected to server");
                break;
            case ESP_HTTPS_OTA_GET_IMG_DESC:
                ESP_LOGI(TAG, "Reading Image Description");
                break;
            case ESP_HTTPS_OTA_VERIFY_CHIP_ID:
                ESP_LOGI(TAG, "Verifying chip id of new image: %d", *(esp_chip_id_t *)event_data);
                break;
            case ESP_HTTPS_OTA_DECRYPT_CB:
                ESP_LOGI(TAG, "Callback to decrypt function");
                break;
            case ESP_HTTPS_OTA_WRITE_FLASH:
                ESP_LOGD(TAG, "Writing to flash: %d written", *(int *)event_data);
                break;
            case ESP_HTTPS_OTA_UPDATE_BOOT_PARTITION:
                ESP_LOGI(TAG, "Boot partition updated. Next Partition: %d", *(esp_partition_subtype_t *)event_data);
                break;
            case ESP_HTTPS_OTA_FINISH:
                ESP_LOGI(TAG, "OTA finish");
                ota_status = OTA_STATUS_SUCCESS;
                break;
            case ESP_HTTPS_OTA_ABORT: // OTA升级过程中断
                ESP_LOGI(TAG, "OTA abort");
                ota_status = OTA_STATUS_FAIL;
                break;
        }
    }
}

/**
 * @brief 验证固件头部信息
 * 
 * 检查新固件的版本是否与当前运行版本相同，避免重复升级
 * 
 * @param new_app_info 新固件的应用信息描述符
 * @return esp_err_t ESP_OK表示验证通过，ESP_FAIL表示验证失败
 */
static esp_err_t validate_image_header(esp_app_desc_t *new_app_info)
{
    if (new_app_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // 获取当前运行的固件版本信息
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
    }

    // 版本检查：如果新固件版本与当前运行版本相同或更旧，则不升级
#ifdef CONFIG_EXAMPLE_SKIP_VERSION_CHECK
    ESP_LOGI(TAG, "获取的固件版本:%s; 设备的运行版本：%s", new_app_info->version, running_app_info.version);
    if (memcmp(new_app_info->version, running_app_info.version, sizeof(new_app_info->version)) == 0) {
        ESP_LOGW(TAG, "Current running version is the same as a new. We will not continue the update.");
        return ESP_FAIL;
    }
    
    // 简单的版本比较检查（防止版本回退）
    if (strcmp(new_app_info->version, running_app_info.version) < 0) {
        ESP_LOGW(TAG, "New version (%s) is older than running version (%s). Rollback not allowed.", 
                 new_app_info->version, running_app_info.version);
        return ESP_FAIL;
    }
#endif

    // 安全版本检查，防止固件回滚
#ifdef CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK
    /**
     * Secure version check from firmware image header prevents subsequent download and flash write of
     * entire firmware image. However this is optional because it is also taken care in API
     * esp_https_ota_finish at the end of OTA update procedure.
     */
    const uint32_t hw_sec_version = esp_efuse_read_secure_version();
    if (new_app_info->secure_version < hw_sec_version) {
        ESP_LOGW(TAG, "New firmware security version is less than eFuse programmed, %"PRIu32" < %"PRIu32, 
                new_app_info->secure_version, hw_sec_version);
        return ESP_FAIL;
    }
#endif

    return ESP_OK;
}

/**
 * @brief HTTP客户端初始化回调函数
 * 
 * 可以在此函数中添加自定义HTTP请求头
 * 
 * @param http_client HTTP客户端句柄
 * @return esp_err_t ESP_OK表示初始化成功
 */
static esp_err_t _http_client_init_cb(esp_http_client_handle_t http_client)
{
    esp_err_t err = ESP_OK;
    /* 如需添加自定义HTTP请求头，可取消下面的注释 */
    // err = esp_http_client_set_header(http_client, "Custom-Header", "Value");
    
    // 添加User-Agent头，某些服务器可能需要
    esp_http_client_set_header(http_client, "User-Agent", "ESP32-OTA/1.0");
    
    // 设置连接保持活跃
    esp_http_client_set_header(http_client, "Connection", "keep-alive");
    
    ESP_LOGI(TAG, "HTTP客户端初始化完成");
    return err;
}

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


