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
static uint8_t map_ota_error_to_code(esp_err_t err)
{
    switch(err) {
        case ESP_ERR_INVALID_ARG: return 1;
        case ESP_ERR_NOT_SUPPORTED: return 2;
        case ESP_ERR_NOT_FOUND: return 3;
        case ESP_ERR_HTTPS_OTA_IN_PROGRESS: return 4;
        case ESP_ERR_OTA_VALIDATE_FAILED: return 5;
        case ESP_ERR_NO_MEM: return 6;
#ifdef ESP_ERR_FLASH_OP_FAIL
        case ESP_ERR_FLASH_OP_FAIL: return 7;
#endif
        case ESP_ERR_FLASH_OP_TIMEOUT: return 7;
        case ESP_ERR_INVALID_VERSION: return 8;
        default:
            // 检查是否是HTTP/网络相关错误（错误码通常在特定范围内）
            // HTTP错误码通常在ESP_ERR_HTTP_BASE (0x7000) 范围内
            // 或者transport错误（负数，通常在-60000到-50000范围）
            if ((err < 0 && err > -70000) || (err >= 0x7000 && err < 0x8000)) {
                return 9;  // 网络连接失败
            }
            return 0; // 正常或未知错误，不记录
    }
}

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
static void advanced_ota_example_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Starting Advanced OTA example");

    // 配置HTTP客户端信息（优化内存使用）
    esp_err_t ota_finish_err = ESP_OK;
    esp_http_client_config_t config = {
        .url = adress,
        .timeout_ms = 15000,  // 15秒超时
        .keep_alive_enable = true, 
        .buffer_size = 2048,  // 减小HTTP接收缓冲区至2KB
        .buffer_size_tx = 1024,  // 减小HTTP发送缓冲区至1KB
        .disable_auto_redirect = false,  // 允许重定向
        .max_redirection_count = 3,  // 最大重定向次数
    };
    
    // HTTP连接不需要证书校验
    
    // OTA参数配置
    ESP_LOGI(TAG, "OTA固件下载地址:%s", config.url);
    esp_https_ota_config_t ota_config = {
        .http_config = &config,
        .http_client_init_cb = _http_client_init_cb, // 注册HTTP客户端初始化回调
#ifdef CONFIG_EXAMPLE_ENABLE_PARTIAL_HTTP_DOWNLOAD  // 是否启用HTTP部分文件下载
        .partial_http_download = true,
        .max_http_request_size = CONFIG_EXAMPLE_HTTP_REQUEST_SIZE,
#endif
    };

    // 开始OTA升级流程
    esp_https_ota_handle_t https_ota_handle = NULL;
    
    // 将HTTPS地址转换为HTTP地址
    if (strncmp(adress, "https://", 8) == 0) {
        char http_url[256];
        snprintf(http_url, sizeof(http_url), "http://%s", adress + 8);
        ESP_LOGI(TAG, "将HTTPS地址转换为HTTP: %s", http_url);
        config.url = http_url;
    } else {
        ESP_LOGI(TAG, "使用原地址: %s", adress);
        config.url = adress;
    }
    
    ESP_LOGI(TAG, "正在连接到OTA服务器...");
    
    // 增强的HTTP连接重试机制
    esp_err_t err = ESP_FAIL;
    int retry_count = 0;
    const int max_retries = 3;
    const int retry_delay_ms[] = {2000, 5000, 10000}; // 递增延迟
    
    for (retry_count = 0; retry_count <= max_retries; retry_count++) {
        err = esp_https_ota_begin(&ota_config, &https_ota_handle);
        
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "HTTP OTA连接建立成功 (第%d次尝试)", retry_count + 1);
            break;
        }
        
        ESP_LOGE(TAG, "HTTP OTA连接失败 (第%d次尝试), error: 0x%x (%s)", 
                 retry_count + 1, err, esp_err_to_name(err));
        
        if (retry_count < max_retries) {
            ESP_LOGI(TAG, "等待%d秒后重试...", retry_delay_ms[retry_count] / 1000);
            vTaskDelay(retry_delay_ms[retry_count] / portTICK_PERIOD_MS);
        }
    }
    
    // 如果所有重试都失败
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP OTA连接失败，已尝试%d次", max_retries + 1);
        
        // 保存OTA失败日志
        uint8_t ota_error = map_ota_error_to_code(err);
        time_t ota_error_time = time(NULL);
        save_device_logs_to_nvs(ota_error, 0, ota_error_time, 0);
        
        ota_status = OTA_STATUS_FAIL;
        vTaskDelete(NULL);
    }

    // 获取固件描述信息
    esp_app_desc_t app_desc;
    err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_read_img_desc failed");
        goto ota_end;
    }
    
    // 验证固件头部信息
    err = validate_image_header(&app_desc);  
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "image header verification failed");
        goto ota_end;
    }

    // 执行固件下载与升级流程
    while (1) {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            // 如果返回值不为ESP_ERR_HTTPS_OTA_IN_PROGRESS，表示下载完成或出错
            break;
        }
        ESP_LOGD(TAG, "Image bytes read: %d", esp_https_ota_get_image_len_read(https_ota_handle));
    }

    // 检查固件数据是否完整接收
    if (esp_https_ota_is_complete_data_received(https_ota_handle) != true) {
        ESP_LOGE(TAG, "Complete data was not received.");
        goto ota_end;
    } else {
        ESP_LOGI(TAG, "拉取bin文件成功");
        ota_finish_err = esp_https_ota_finish(https_ota_handle); // 完成升级过程
        
        // 判断升级结果并处理
        if ((err == ESP_OK) && (ota_finish_err == ESP_OK)) {
            ESP_LOGI(TAG, "ESP_HTTPS_OTA upgrade successful. Rebooting ...");
            play_voice(OTA_SUCCESS); // 播放升级成功提示音
            vTaskDelay(3000 / portTICK_PERIOD_MS);
            esp_restart(); // 重启设备应用新固件
        } else {
            if (ota_finish_err == ESP_ERR_OTA_VALIDATE_FAILED) {
                ESP_LOGE(TAG, "Image validation failed, image is corrupted");
            }
            ESP_LOGE(TAG, "ESP_HTTPS_OTA upgrade failed 0x%x", ota_finish_err);
            
            // 保存OTA失败日志
            uint8_t ota_error = map_ota_error_to_code(ota_finish_err);
            time_t ota_error_time = time(NULL);
            save_device_logs_to_nvs(ota_error, 0, ota_error_time, 0);
            
            vTaskDelete(NULL);
        }
    }

    // 如果升级失败，执行清理
ota_end:
    esp_https_ota_abort(https_ota_handle);
    ESP_LOGE(TAG, "ESP_HTTPS_OTA upgrade failed");
    
    // 保存OTA失败日志（使用err作为错误码）
    uint8_t ota_error = map_ota_error_to_code(err);
    if (ota_error == 0 && ota_finish_err != ESP_OK) {
        // 如果err映射为0，尝试使用ota_finish_err
        ota_error = map_ota_error_to_code(ota_finish_err);
    }
    time_t ota_error_time = time(NULL);
    save_device_logs_to_nvs(ota_error, 0, ota_error_time, 0);
    
    play_voice(OTA_FAIL); // 播放升级失败提示音
    vTaskDelete(NULL);
}

/**
 * @brief 启动OTA升级流程
 * 
 * 初始化OTA相关组件，注册事件处理函数，创建OTA任务
 * 
 * @param adr OTA固件下载地址
 */
void OTA_function(char* adr)
{
    ESP_LOGI(TAG, "OTA_function 函数运行");
 
    /* 保存OTA固件下载地址 */
    strncpy(adress, adr, strlen(adr)); 

    /* NVS初始化 */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_LOGI(TAG, "NVS 初始化完成");

    /* 注册OTA事件处理函数 */
    // 默认事件循环只应该初始化1次，在wifi初始化时已经创建过一次
    ESP_ERROR_CHECK(esp_event_handler_register(ESP_HTTPS_OTA_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

    /* 取消固件回滚（如果处于待验证状态） */
#if 1
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
                ESP_LOGI(TAG, "App is valid, rollback cancelled successfully");
            } else {
                ESP_LOGE(TAG, "Failed to cancel rollback");
            }
        }
    }
#endif

    /* 启用WiFi省电模式，降低OTA期间功耗 */
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM); 

    /* 创建OTA任务（减少栈大小节约内存）*/
    if (xTaskCreate(advanced_ota_example_task, "ota_task", 1024 * 5, NULL, 9, NULL) == pdPASS) {
        ESP_LOGI(TAG, "ota_task 创建成功（5KB栈）");
    } else {
        ESP_LOGE(TAG, "ota_task 创建失败");
    }
}


