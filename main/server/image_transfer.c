/**
 * @file image_transfer.c
 * @brief 图像传输模块实现文件
 * 
 * 实现了UDP实时图像传输和HTTP图像批量上传功能。
 * 包括UDP数据包组装与发送、HTTP多部分表单提交和SPIFFS图像文件管理。
 */

#include "image_transfer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_heap_caps.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "storage.h"
#include "mqtt.h"          // 添加MQTT头文件，用于暂停心跳

/************************** UDP图像传输 **************************/
#define HOST_IP_ADDR "192.168.2.221"  /**< UDP目标主机IP地址 */
#define PORT 3333                      /**< UDP目标端口 */

/* 全局变量定义 */
static const char *TAG = "IMAGE_TRANSFER";  /**< 日志标签 */


extern uint8_t user_data[24];  /**< 设备SN码 */

QueueHandle_t udp_evt_queue;   /**< UDP消息队列句柄 */

struct sockaddr_in dest_addr;  /**< UDP目标地址结构 */
int addr_family;               /**< 地址协议族 */
int ip_protocol;               /**< IP协议类型 */

const TickType_t xOneSecond = pdMS_TO_TICKS(3000); /**< 3秒超时时间 */

/**
 * @brief UDP数据包头部结构
 */
typedef struct {
    uint8_t seq_num;         /**< 序列号 */
    uint8_t total_packets;   /**< 总包数 */
    uint8_t payload_length;  /**< 数据长度 */
} packet_header_t;

static void print_memory_info(void);

/**
 * @brief 重启套接字连接
 * 
 * 关闭并释放现有套接字资源
 * 
 * @param sock 需要重启的套接字句柄
 */
void restart_sock(int sock)
{
    ESP_LOGI(TAG, "Shutting down socket and restarting...");
    shutdown(sock, 0);
    close(sock);
}

/**
 * @brief UDP客户端发送任务
 * 
 * 从队列接收图像数据，将其分包后通过UDP发送
 * 包括图像数据和识别结果
 * 
 * @param arg 任务参数（未使用）
 */
void udp_client_send_task(void* arg)
{
    const int size = 96*3*8;         // 每个数据包的大小
    uint8_t* payload = (uint8_t*)malloc((size + sizeof(packet_header_t)) * sizeof(uint8_t));

    if (!payload) {
        ESP_LOGE(TAG, "Memory allocation failed");
        return;
    }

    // 初始化数据包头部
    packet_header_t header = {0};
    header.seq_num = 0;
    header.total_packets = 13;
    header.payload_length = (size > 255) ? 255 : (uint8_t)size;  // 限制在uint8_t范围内

    QueueMessage_t message;
    while(true) {
        if(xQueueReceive(udp_evt_queue, &message, xOneSecond)) {
            uint8_t* jpeg_new_buffer = message.buffer;

            // 创建UDP套接字
            int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
            if (sock < 0) {
                ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
                restart_sock(sock);
            } else {
                // 分包发送图像数据
                for(int i=0; i<12; i++) {
                    header.seq_num = i + 1;

                    // 组装数据包：头部+数据
                    memcpy(payload, &header, sizeof(packet_header_t));
                    memcpy(&payload[sizeof(packet_header_t)], &jpeg_new_buffer[i*size], size);

                    // 发送数据包
                    int err = sendto(sock, payload, size+sizeof(packet_header_t), 0, 
                                     (struct sockaddr *)&dest_addr, sizeof(dest_addr));
                    if (err < 0) {
                        ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                        restart_sock(sock);
                    } else {
                        ESP_LOGI(TAG, "Packet %d of %d sent", header.seq_num, header.total_packets);
                    }
                    vTaskDelay(10 / portTICK_PERIOD_MS);
                }

                // 发送识别结果包
                uint8_t result = (uint8_t)(message.result * 100);
                ESP_LOGI(TAG, "result = %u", result);
                
                // 识别结果作为第13个包发送
                header.seq_num = 13;
                uint8_t result_pac[4] = {header.seq_num, header.total_packets, 0, result};
                int err = sendto(sock, result_pac, 4, 0, 
                                 (struct sockaddr *)&dest_addr, sizeof(dest_addr));
                if (err < 0) {
                    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                    restart_sock(sock);
                } else {
                    ESP_LOGI(TAG, "Packet %d of %d sent", header.seq_num, header.total_packets);
                }

                restart_sock(sock);
            }
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    
    free(payload);
}

char* update_udp_ip()
{
    return get_udp_ip();
}

/**
 * @brief 初始化UDP客户端
 * 
 * 配置UDP目标地址和参数，创建消息队列和发送任务
 */
void udp_client_init()
{
    if (update_udp_ip() == NULL){
        dest_addr.sin_addr.s_addr = inet_addr(HOST_IP_ADDR);
    }else{
        dest_addr.sin_addr.s_addr = inet_addr(update_udp_ip());
    }
    // 配置UDP目标地址
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(PORT);
    addr_family = AF_INET;
    ip_protocol = IPPROTO_IP;

    // 创建UDP消息队列和发送任务
    udp_evt_queue = xQueueCreate(10, sizeof(QueueMessage_t));
    xTaskCreate(udp_client_send_task, "udp_client_send_task", 4096, NULL, 1, NULL);
}


/*************************************UDP图像传输************************************************ */

/************************** HTTP图像上传 **************************/
#include "esp_tls.h"

// HTTP缓冲区大小配置 - 根据批处理大小动态调整
#define MAX_HTTP_RECV_BUFFER 512            /**< HTTP接收缓冲区大小 */
#define MAX_HTTP_OUTPUT_BUFFER 1024*128     /**< HTTP发送缓冲区大小，128KB以支持100张图片批处理 */

/**
 * @brief 添加表单数据字段到HTTP请求体
 * 
 * @param buffer 请求缓冲区
 * @param buffer_len 当前缓冲区已用长度（会更新）
 * @param boundary 表单边界字符串
 * @param name 表单字段名
 * @param value 表单字段值
 */
static void add_form_data(char *buffer, size_t *buffer_len, const char *boundary, 
                          const char *name, const char *value) 
{
    // 构建表单字段部分
    int len = snprintf(NULL, 0, 
                      "--%s\r\nContent-Disposition: form-data; name=\"%s\"\r\n\r\n%s\r\n", 
                      boundary, name, value);
    if (*buffer_len + len >= MAX_HTTP_OUTPUT_BUFFER) {
        ESP_LOGE(TAG, "Buffer overflow when adding form data");
        return;
    }
    snprintf(buffer + *buffer_len, len + 1,
             "--%s\r\nContent-Disposition: form-data; name=\"%s\"\r\n\r\n%s\r\n", 
             boundary, name, value);
    *buffer_len += len;
}

/**
 * @brief 添加文件数据到HTTP多部分表单请求体
 * 
 * @param buffer 请求缓冲区
 * @param buffer_len 当前缓冲区已用长度（会更新）
 * @param boundary 表单边界字符串
 * @param name 表单字段名
 * @param filename 文件名
 * @param file_data 文件数据
 * @param file_size 文件大小
 * @return esp_err_t ESP_OK表示添加成功，ESP_ERR_NO_MEM表示缓冲区不足
 */
static esp_err_t add_file_to_multipart(char *buffer, size_t *buffer_len, const char *boundary, 
                                       const char *name, const char *filename, 
                                       const uint8_t *file_data, size_t file_size) 
{
    // 计算头部长度
    int header_len = snprintf(NULL, 0, 
        "--%s\r\nContent-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\n"
        "Content-Type: image/jpeg\r\n\r\n", 
        boundary, name, filename);
    
    // 计算尾部长度
    const int tail_len = 2;  // \r\n
    
    // 计算总需要的长度
    size_t total_needed = header_len + file_size + tail_len;
    
    // 检查缓冲区是否足够
    if (*buffer_len + total_needed >= MAX_HTTP_OUTPUT_BUFFER) {
        ESP_LOGE(TAG, "Buffer overflow when adding file: %s (need %zu bytes, available %zu bytes)", 
                 filename, total_needed, MAX_HTTP_OUTPUT_BUFFER - *buffer_len);
        return ESP_ERR_NO_MEM;
    }

    // 添加头部
    *buffer_len += snprintf(buffer + *buffer_len, header_len + 1,
        "--%s\r\nContent-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\n"
        "Content-Type: image/jpeg\r\n\r\n",
        boundary, name, filename);

    // 添加文件数据
    memcpy(buffer + *buffer_len, file_data, file_size);
    *buffer_len += file_size;

    // 添加尾部
    memcpy(buffer + *buffer_len, "\r\n", tail_len);
    *buffer_len += tail_len;

    ESP_LOGI(TAG, "Successfully added file: %s, size: %zu, current buffer length: %zu", 
             filename, file_size, *buffer_len);
    
    return ESP_OK;
}

/**
 * @brief HTTP客户端事件处理回调
 * 
 * 处理HTTP请求的各种事件，如连接、数据接收等
 * 
 * @param evt HTTP客户端事件
 * @return esp_err_t ESP_OK表示处理成功
 */
static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            // 当执行过程中出现任何错误时，会发生此事件 
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            // 一旦 HTTP 连接到服务器，就不会执行任何数据交换
            break;
        case HTTP_EVENT_HEADER_SENT:
            // 将所有标头发送到服务器后发生此事件
            break;
        case HTTP_EVENT_ON_HEADER:
            // 在接收从服务器发送的每个标头时发生
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            // 在此处理HTTP请求返回的数据
            break;
        case HTTP_EVENT_ON_FINISH:
            // 完成 HTTP 会话时发生
            break;
        case HTTP_EVENT_DISCONNECTED:
            // 连接断开时发生此事件
            break;
        default:
            break;
    }
    return ESP_OK;
}

/**
 * @brief 发送HTTP multipart/form-data请求上传多个图像文件
 * 
 * @param url 服务器URL
 * @param device_sn 设备序列号
 * @param ucode 用户码
 * @param img_files 图像数据数组
 * @param filenames 文件名数组
 * @param img_count 图像数量
 * @param file_sizes 文件大小数组
 * @return esp_err_t ESP_OK表示上传成功，ESP_FAIL表示上传失败
 */
esp_err_t http_post_multipart(const char *url, const char *device_sn, int ucode, 
                              const uint8_t **img_files, const char **filenames, 
                              size_t img_count, const size_t *file_sizes) 
{
    if (!img_files || !filenames || !file_sizes || img_count == 0) {
        ESP_LOGE(TAG, "Invalid parameters for HTTP post");
        return ESP_FAIL;
    }

    // 初始化HTTP客户端配置
    esp_http_client_config_t config = {
        .url = url,
#if ON_LINE_SERVER
        .transport_type = HTTP_TRANSPORT_OVER_SSL, 
#else
        .transport_type = HTTP_TRANSPORT_OVER_TCP,  
#endif
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return ESP_FAIL;
    }

    // 设置HTTP方法为POST
    esp_http_client_set_method(client, HTTP_METHOD_POST);

    // 创建boundary
    char boundary[32];
    snprintf(boundary, sizeof(boundary), "------------------------%lx", esp_random());

    // 设置Content-Type header
    char content_type[64];
    snprintf(content_type, sizeof(content_type), "multipart/form-data; boundary=%s", boundary);
    esp_http_client_set_header(client, "Content-Type", content_type);

    // 优先使用PSRAM分配请求体缓冲区以节省内部内存
    char* buffer = NULL;
#if CONFIG_SPIRAM_BOOT_INIT
    buffer = heap_caps_malloc(MAX_HTTP_OUTPUT_BUFFER, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
    if (!buffer) {
        // PSRAM分配失败时回退到内部内存
        buffer = malloc(MAX_HTTP_OUTPUT_BUFFER);
    }
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate memory for buffer (both PSRAM and internal RAM)");
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Allocated %d bytes for HTTP buffer (%s)", MAX_HTTP_OUTPUT_BUFFER,
             heap_caps_get_allocated_size(buffer) ? "PSRAM" : "Internal RAM");
    size_t buffer_len = 0;

    // 添加设备序列号字段
    add_form_data(buffer, &buffer_len, boundary, "device_sn", device_sn);

    // 添加用户码字段（转换为字符串）
    char ucode_str[16];
    snprintf(ucode_str, sizeof(ucode_str), "%d", ucode);
    add_form_data(buffer, &buffer_len, boundary, "ucode", ucode_str);

    // 预估所需缓冲区大小并智能调整文件数量
    size_t estimated_size = buffer_len;  // 当前已用空间（表单字段）
    size_t actual_files_to_upload = 0;
    
    // 预估每个文件需要的空间（文件数据 + multipart头部，约150字节开销）
    for (size_t i = 0; i < img_count; i++) {
        if (img_files[i] && filenames[i]) {
            size_t file_overhead = 150 + strlen(filenames[i]);  // multipart头部开销
            size_t file_total_size = file_sizes[i] + file_overhead;
            
            if (estimated_size + file_total_size + 100 < MAX_HTTP_OUTPUT_BUFFER) {  // 预留100字节安全边际
                estimated_size += file_total_size;
                actual_files_to_upload++;
            } else {
                ESP_LOGW(TAG, "达到缓冲区容量限制，实际上传文件数: %zu/%zu", actual_files_to_upload, img_count);
                break;
            }
        }
    }
    
    ESP_LOGI(TAG, "预估缓冲区使用: %zu/%d 字节，计划上传文件: %zu", 
             estimated_size, MAX_HTTP_OUTPUT_BUFFER, actual_files_to_upload);

    // 添加图像文件（仅添加预估范围内的文件）
    int successful_files = 0;
    for (size_t i = 0; i < actual_files_to_upload && i < img_count; i++) {
        if (img_files[i] && filenames[i]) {
            ESP_LOGI(TAG, "Adding file %zu/%zu: %s, size: %zu", 
                     i+1, actual_files_to_upload, filenames[i], file_sizes[i]);
            
            esp_err_t ret = add_file_to_multipart(buffer, &buffer_len, boundary, "imgs[]", 
                                                filenames[i], img_files[i], file_sizes[i]);
            if (ret == ESP_OK) {
                successful_files++;
            } else {
                ESP_LOGW(TAG, "缓冲区空间不足，停止添加文件。当前已添加: %d/%zu", successful_files, actual_files_to_upload);
                break;  // 如果缓冲区已满，停止添加更多文件
            }
        }
    }

    ESP_LOGI(TAG, "Successfully added %d/%zu files to request", successful_files, img_count);

    // 添加结束边界
    char end_boundary[64];
    snprintf(end_boundary, sizeof(end_boundary), "\r\n--%s--\r\n", boundary);
    size_t end_len = strlen(end_boundary);
    
    if (buffer_len + end_len < MAX_HTTP_OUTPUT_BUFFER) {
        memcpy(buffer + buffer_len, end_boundary, end_len);
        buffer_len += end_len;

        // 设置POST字段
        esp_http_client_set_post_field(client, buffer, buffer_len);

        // 执行HTTP请求
        esp_err_t err = esp_http_client_perform(client);
        esp_err_t result = ESP_FAIL;  // 默认为失败
        
        if (err == ESP_OK) {
            int status_code = esp_http_client_get_status_code(client);
            if (status_code == 200) {
                ESP_LOGI(TAG, "Files uploaded successfully");
                result = ESP_OK;  // 只有在HTTP状态码为200时才认为成功
            } else {
                ESP_LOGE(TAG, "Server returned status code: %d", status_code);
                result = ESP_FAIL;
            }
        } else {
            ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
            result = ESP_FAIL;
        }
        
        // 清理资源
        free(buffer);
        esp_http_client_cleanup(client);
        
        // 添加更长延迟确保HTTP客户端完全清理，防止SSL与MQTT冲突
        ESP_LOGW(TAG, "HTTP客户端清理完成，等待SSL上下文完全释放...");
        vTaskDelay(pdMS_TO_TICKS(500));  // 增加到500ms
        return result;
    } else {
        ESP_LOGE(TAG, "Buffer overflow when adding end boundary");
        // 清理资源
        free(buffer);
        esp_http_client_cleanup(client);
        
        // 添加更长延迟确保HTTP客户端完全清理，防止SSL与MQTT冲突
        ESP_LOGW(TAG, "HTTP客户端清理完成，等待SSL上下文完全释放...");
        vTaskDelay(pdMS_TO_TICKS(500));  // 增加到500ms
        return ESP_FAIL;
    }
}

// 全局上传状态标志
static bool g_is_uploading = false;
static TaskHandle_t g_upload_task_handle = NULL;

typedef struct {
    int ucode;
} upload_task_params_t;

static bool stop_mqtt_for_upload(void)
{
    bool stopped = false;
    set_mqtt_heartbeat_pause(true);

    esp_mqtt_client_handle_t client = get_mqtt_client();
    if (client != NULL && get_mqtt_status() == MQTT_EVENT_CONNECTED_) {
        esp_err_t ret = esp_mqtt_client_stop(client);
        if (ret == ESP_OK) {
            stopped = true;
            ESP_LOGI(TAG, "MQTT已停止，避免上传期间SSL冲突");
        } else {
            ESP_LOGW(TAG, "MQTT停止失败: %s", esp_err_to_name(ret));
        }
    }

    return stopped;
}

static void resume_mqtt_after_upload(bool mqtt_stopped)
{
    if (mqtt_stopped) {
        esp_mqtt_client_handle_t client = get_mqtt_client();
        if (client != NULL) {
            esp_err_t ret = esp_mqtt_client_start(client);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "MQTT重启失败: %s", esp_err_to_name(ret));
            } else {
                ESP_LOGI(TAG, "MQTT已重启");
            }
        }
    }
    set_mqtt_heartbeat_pause(false);
}

static void upload_pictures_task(void* parameter)
{
    upload_task_params_t* params = (upload_task_params_t*)parameter;
    if (!params) {
        ESP_LOGE(TAG, "上传任务参数为空");
        g_upload_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    int ucode = params->ucode;
    free(params);

    ESP_LOGI(TAG, "异步图片上传任务启动，ucode=%d", ucode);
    upload_pictures(ucode);

    g_upload_task_handle = NULL;
    vTaskDelete(NULL);
}

/**
 * @brief 获取当前图像上传状态
 * 
 * @return bool true表示正在上传，false表示空闲
 */
bool get_upload_status(void)
{
    return g_is_uploading;
}

/**
 * @brief 异步上传指定RCG_CODE的图片
 */
esp_err_t start_upload_pictures_async(int ucode)
{
    if (g_upload_task_handle != NULL || g_is_uploading) {
        ESP_LOGW(TAG, "已有图片上传任务在运行，忽略新的请求");
        return ESP_ERR_INVALID_STATE;
    }

    upload_task_params_t* params = (upload_task_params_t*)malloc(sizeof(upload_task_params_t));
    if (!params) {
        ESP_LOGE(TAG, "分配上传任务参数失败");
        return ESP_ERR_NO_MEM;
    }
    params->ucode = ucode;

    BaseType_t ret = xTaskCreate(
        upload_pictures_task,
        "img_upload",
        8192,
        params,
        5,
        &g_upload_task_handle
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建图片上传任务失败");
        free(params);
        g_upload_task_handle = NULL;
        return ESP_FAIL;
    }

    return ESP_OK;
}


/**
 * @brief 批量上传SPIFFS中的图像文件
 * 
 * 分批读取SPIFFS中的图像文件并上传到服务器
 * 上传成功后会删除文件以释放空间
 * 
 * @param ucode 用户码
 * @return esp_err_t ESP_OK表示上传成功，ESP_FAIL表示上传失败
 */
esp_err_t upload_pictures(int ucode)
{
    if (g_is_uploading) {
        ESP_LOGW(TAG, "已有上传任务在运行，跳过本次上传");
        return ESP_ERR_INVALID_STATE;
    }
    g_is_uploading = true;
    
    // 上传前暂停心跳并停止MQTT，避免SSL冲突
    ESP_LOGI(TAG, "暂停MQTT并停止客户端以避免SSL资源冲突");
    bool mqtt_stopped = stop_mqtt_for_upload();
    
    // 检查内存状态，如果可用内存太低则延迟上传
    size_t free_heap_start = esp_get_free_heap_size();
    if (free_heap_start < 300000) {  // 如果可用内存小于300KB
        ESP_LOGW(TAG, "可用内存不足 (%zu 字节)，延迟上传避免崩溃", free_heap_start);
        vTaskDelay(pdMS_TO_TICKS(2000));  // 等待2秒让系统释放内存
        free_heap_start = esp_get_free_heap_size();
        if (free_heap_start < 200000) {  // 如果仍然不足200KB
            ESP_LOGE(TAG, "内存严重不足 (%zu 字节)，取消上传", free_heap_start);
            // 恢复MQTT心跳
            resume_mqtt_after_upload(mqtt_stopped);
            g_is_uploading = false;
            return ESP_ERR_NO_MEM;
        }
    }
    
    ESP_LOGI(TAG, "Image upload started for RCG_CODE: %d", ucode);
    
    // 打印详细内存信息用于调试
    print_memory_info();
    
    // 检查当前可用内存
    size_t free_heap = esp_get_free_heap_size();
    size_t min_free_heap = esp_get_minimum_free_heap_size();
    ESP_LOGI(TAG, "开始上传前内存状态: 可用堆=%zu, 最小可用堆=%zu", free_heap, min_free_heap);
    
    // 根据内存状况动态调整批处理大小
    size_t BATCH_SIZE;

    BATCH_SIZE = 50;   // 中等内存时使用50张

    ESP_LOGI(TAG, "根据可用内存 %zu 字节，设置批处理大小为: %zu", free_heap, BATCH_SIZE);
    
    // 打开SPIFFS目录
    DIR *dir = opendir("/spiffs");
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open SPIFFS directory");
        resume_mqtt_after_upload(mqtt_stopped);
        g_is_uploading = false;
        return ESP_FAIL;
    }

    // 构建RCG_CODE前缀用于过滤文件
    char rcg_prefix[16];
    snprintf(rcg_prefix, sizeof(rcg_prefix), "%d_", ucode);
    ESP_LOGI(TAG, "只上传RCG_CODE前缀为 '%s' 的图片", rcg_prefix);

    // 首先计算匹配RCG_CODE的jpg文件数量
    int total_files = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".jpg") != NULL && 
            strncmp(entry->d_name, rcg_prefix, strlen(rcg_prefix)) == 0) {
            total_files++;
        }
    }

    if (total_files == 0) {
        ESP_LOGE(TAG, "No jpg files found in SPIFFS");
        closedir(dir);
        resume_mqtt_after_upload(mqtt_stopped);
        g_is_uploading = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Found total %d files, will process in batches of %d", total_files, BATCH_SIZE);

    // 计算实际需要的批次数
    int total_batches = (total_files + BATCH_SIZE - 1) / BATCH_SIZE;
    int processed_files = 0;

    // 上传结果标志
    esp_err_t final_result = ESP_OK;
    
    for (int current_batch = 1; current_batch <= total_batches; current_batch++) {
        ESP_LOGI(TAG, "Processing batch %d/%d", current_batch, total_batches);
        rewinddir(dir);  // 重置目录指针到开始位置

        // 为当前批次分配内存，优先使用PSRAM
        uint8_t **img_files = NULL;
        char **filenames = NULL;
        size_t *file_sizes = NULL;
        
#if CONFIG_SPIRAM_BOOT_INIT
        img_files = (uint8_t**)heap_caps_malloc(BATCH_SIZE * sizeof(uint8_t*), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        filenames = (char**)heap_caps_malloc(BATCH_SIZE * sizeof(char*), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        file_sizes = (size_t*)heap_caps_malloc(BATCH_SIZE * sizeof(size_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
        
        // 如果PSRAM分配失败，使用内部内存
        if (!img_files) img_files = (uint8_t**)malloc(BATCH_SIZE * sizeof(uint8_t*));
        if (!filenames) filenames = (char**)malloc(BATCH_SIZE * sizeof(char*));
        if (!file_sizes) file_sizes = (size_t*)malloc(BATCH_SIZE * sizeof(size_t));

        if (!img_files || !filenames || !file_sizes) {
            ESP_LOGE(TAG, "Failed to allocate memory for arrays");
            goto cleanup_batch;
        }

        // 初始化数组
        memset(img_files, 0, BATCH_SIZE * sizeof(uint8_t*));
        memset(filenames, 0, BATCH_SIZE * sizeof(char*));
        memset(file_sizes, 0, BATCH_SIZE * sizeof(size_t));

        // 跳过已处理的文件
        int files_to_skip = (current_batch - 1) * BATCH_SIZE;
        int jpg_files_seen = 0;  // 计数已经看到的jpg文件
        
        // 跳过之前批次的文件
        while ((entry = readdir(dir)) != NULL) {
            if (strstr(entry->d_name, ".jpg") != NULL && 
                strncmp(entry->d_name, rcg_prefix, strlen(rcg_prefix)) == 0) {
                if (jpg_files_seen >= files_to_skip) {
                    break;  // 找到了这个批次的起始位置
                }
                jpg_files_seen++;
            }
        }

        // 读取当前批次的文件
        int batch_files = 0;
        while (batch_files < BATCH_SIZE && processed_files < total_files && entry != NULL) {
            // 处理当前entry - 只处理匹配RCG_CODE的jpg文件
            if (strstr(entry->d_name, ".jpg") != NULL && 
                strncmp(entry->d_name, rcg_prefix, strlen(rcg_prefix)) == 0) {
                char file_path[512];
                snprintf(file_path, sizeof(file_path), "/spiffs/%s", entry->d_name);
                
                FILE *file = fopen(file_path, "rb");
                if (!file) {
                    ESP_LOGE(TAG, "Failed to open file: %s", file_path);
                    entry = readdir(dir);  // 获取下一个文件
                    continue;
                }

                // 获取文件大小
                fseek(file, 0, SEEK_END);
                size_t file_size = ftell(file);
                rewind(file);

                // 分配内存并读取文件内容，优先使用PSRAM
                img_files[batch_files] = NULL;
#if CONFIG_SPIRAM_BOOT_INIT
                img_files[batch_files] = (uint8_t*)heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
                if (!img_files[batch_files]) {
                    // PSRAM分配失败时回退到内部内存
                    img_files[batch_files] = (uint8_t*)malloc(file_size);
                }
                
                if (img_files[batch_files]) {
                    if (fread(img_files[batch_files], 1, file_size, file) == file_size) {
                        filenames[batch_files] = strdup(entry->d_name);
                        file_sizes[batch_files] = file_size;
                        if (filenames[batch_files]) {
                            ESP_LOGI(TAG, "Batch %d: Read file %d/%d: %s, size: %zu bytes", 
                                    current_batch, batch_files + 1, BATCH_SIZE, 
                                    entry->d_name, file_size);
                            batch_files++;
                            processed_files++;
                        } else {
                            free(img_files[batch_files]);
                            img_files[batch_files] = NULL;
                        }
                    } else {
                        free(img_files[batch_files]);
                        img_files[batch_files] = NULL;
                    }
                }
                fclose(file);
            }
            entry = readdir(dir);  // 获取下一个文件
        }

        // 上传当前批次的文件
        if (batch_files > 0) {
            // 上传前检查内存状况
            size_t free_before = esp_get_free_heap_size();
            ESP_LOGI(TAG, "上传前可用内存: %zu 字节", free_before);
            
            esp_err_t ret = http_post_multipart(CFG_IMG_API_URL, (char*)user_data, ucode, 
                              (const uint8_t**)img_files, 
                              (const char**)filenames, 
                              batch_files,
                              (const size_t*)file_sizes);
            if (ret != ESP_OK) {
                final_result = ESP_FAIL;  // 如果任何一批上传失败，标记最终结果为失败
            }
            
            // 上传后检查内存状况
            size_t free_after = esp_get_free_heap_size();
            ESP_LOGI(TAG, "Batch %d/%d: Uploaded %d files, 内存变化: %zu -> %zu (差值: %ld)", 
                    current_batch, total_batches, batch_files, free_before, free_after, 
                    (long)(free_after - free_before));
        }

cleanup_batch:
        // 清理当前批次的资源
        if (img_files) {
            for (int i = 0; i < BATCH_SIZE; i++) {
                if (img_files[i]) free(img_files[i]);
            }
            free(img_files);
        }
        if (filenames) {
            for (int i = 0; i < BATCH_SIZE; i++) {
                if (filenames[i]) free(filenames[i]);
            }
            free(filenames);
        }
        if (file_sizes) {
            free(file_sizes);
        }

        // 在批次之间添加延时，让系统回收内存
        vTaskDelay(pdMS_TO_TICKS(500));  // 延时500ms让内存有时间释放
        
        // 手动触发垃圾回收
        size_t free_heap_after_batch = esp_get_free_heap_size();
        ESP_LOGI(TAG, "批次 %d 完成后可用内存: %zu 字节", current_batch, free_heap_after_batch);
    }

    closedir(dir);
    ESP_LOGI(TAG, "Image upload completed - Processed %d files in %d batches", 
             processed_files, total_batches);
             
    // 上传完成后，如果上传成功则只删除对应RCG_CODE的图片
    if (final_result == ESP_OK) {
        ESP_LOGI(TAG, "RCG_CODE %d 的图片上传成功，开始删除对应的图片文件", ucode);
        
        // 删除指定RCG_CODE的图片
        DIR *del_dir = opendir("/spiffs");
        if (del_dir) {
            struct dirent *del_entry;
            int deleted_count = 0;
            
            while ((del_entry = readdir(del_dir)) != NULL) {
                if (strstr(del_entry->d_name, ".jpg") != NULL && 
                    strncmp(del_entry->d_name, rcg_prefix, strlen(rcg_prefix)) == 0) {
                    
                    char file_path[512];
                    snprintf(file_path, sizeof(file_path), "/spiffs/%s", del_entry->d_name);
                    
                    if (remove(file_path) == 0) {
                        ESP_LOGI(TAG, "已删除: %s", del_entry->d_name);
                        deleted_count++;
                    } else {
                        ESP_LOGE(TAG, "删除失败: %s", del_entry->d_name);
                    }
                }
            }
            closedir(del_dir);
            ESP_LOGI(TAG, "删除完成，共删除 %d 个RCG_CODE %d 的图片文件", deleted_count, ucode);
        } else {
            ESP_LOGE(TAG, "无法打开目录进行删除操作");
        }
    } else {
        ESP_LOGW(TAG, "由于上传失败，保留RCG_CODE %d 的图片文件", ucode);
    }
    
    // 上传任务完成后添加更长延迟，确保所有网络资源完全释放
    ESP_LOGI(TAG, "图片上传任务完成，等待网络资源释放...");
    
    // 强制清理SSL上下文和网络连接池
    ESP_LOGI(TAG, "强制清理SSL上下文...");
    
    // 等待更长时间确保所有SSL连接完全关闭
    vTaskDelay(pdMS_TO_TICKS(3000));  // 增加到3秒等待时间
    
    // 强制执行垃圾回收
    size_t final_free_heap = esp_get_free_heap_size();
    ESP_LOGI(TAG, "上传任务结束后可用内存: %zu 字节", final_free_heap);
    
    // 恢复MQTT心跳并重启MQTT
    ESP_LOGI(TAG, "恢复MQTT心跳");
    resume_mqtt_after_upload(mqtt_stopped);
    
    g_is_uploading = false;
    return final_result;
}

/**
 * @brief 专门为服务器请求上传所有图像文件（209命令专用）
 * 
 * 上传SPIFFS中的所有图像文件到服务器，上传成功后删除所有图片
 * 这个函数专门用于处理服务器的209命令请求
 * 
 * @param ucode 服务器提供的用户码
 * @return esp_err_t ESP_OK表示上传成功，ESP_FAIL表示上传失败
 */
esp_err_t upload_all_pictures_for_server(int ucode)
{
    if (g_is_uploading) {
        ESP_LOGW(TAG, "已有上传任务在运行，跳过服务器上传请求");
        return ESP_ERR_INVALID_STATE;
    }
    g_is_uploading = true;
    ESP_LOGI(TAG, "Server request image upload started for all pictures with ucode: %d", ucode);

    // 上传前暂停心跳并停止MQTT，避免SSL冲突
    ESP_LOGI(TAG, "暂停MQTT并停止客户端以避免SSL资源冲突");
    bool mqtt_stopped = stop_mqtt_for_upload();
    
    // 打印详细内存信息用于调试
    print_memory_info();
    
    // 检查当前可用内存
    size_t free_heap = esp_get_free_heap_size();
    size_t min_free_heap = esp_get_minimum_free_heap_size();
    ESP_LOGI(TAG, "服务器请求上传前内存状态: 可用堆=%zu, 最小可用堆=%zu", free_heap, min_free_heap);
    
    // 根据内存状况动态调整批处理大小
    size_t BATCH_SIZE = 50;   // 使用50张作为批处理大小

    ESP_LOGI(TAG, "根据可用内存 %zu 字节，设置批处理大小为: %zu", free_heap, BATCH_SIZE);
    
    // 打开SPIFFS目录
    DIR *dir = opendir("/spiffs");
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open SPIFFS directory");
        resume_mqtt_after_upload(mqtt_stopped);
        g_is_uploading = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "服务器请求：上传所有jpg图片（不做rcg_code过滤）");

    // 计算所有jpg文件数量（不做rcg_code过滤）
    int total_files = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".jpg") != NULL) {
            total_files++;
        }
    }

    if (total_files == 0) {
        ESP_LOGE(TAG, "No jpg files found in SPIFFS for server request");
        closedir(dir);
        resume_mqtt_after_upload(mqtt_stopped);
        g_is_uploading = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "服务器请求：找到总共 %d 个图片文件，将分批处理，每批 %zu 个", total_files, BATCH_SIZE);

    // 计算实际需要的批次数
    int total_batches = (total_files + BATCH_SIZE - 1) / BATCH_SIZE;
    int processed_files = 0;

    // 上传结果标志
    esp_err_t final_result = ESP_OK;
    
    for (int current_batch = 1; current_batch <= total_batches; current_batch++) {
        ESP_LOGI(TAG, "服务器请求：处理批次 %d/%d", current_batch, total_batches);
        rewinddir(dir);  // 重置目录指针到开始位置

        // 为当前批次分配内存，优先使用PSRAM
        uint8_t **img_files = NULL;
        char **filenames = NULL;
        size_t *file_sizes = NULL;
        
#if CONFIG_SPIRAM_BOOT_INIT
        img_files = (uint8_t**)heap_caps_malloc(BATCH_SIZE * sizeof(uint8_t*), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        filenames = (char**)heap_caps_malloc(BATCH_SIZE * sizeof(char*), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        file_sizes = (size_t*)heap_caps_malloc(BATCH_SIZE * sizeof(size_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
        
        // 如果PSRAM分配失败，使用内部内存
        if (!img_files) img_files = (uint8_t**)malloc(BATCH_SIZE * sizeof(uint8_t*));
        if (!filenames) filenames = (char**)malloc(BATCH_SIZE * sizeof(char*));
        if (!file_sizes) file_sizes = (size_t*)malloc(BATCH_SIZE * sizeof(size_t));

        if (!img_files || !filenames || !file_sizes) {
            ESP_LOGE(TAG, "Failed to allocate memory for arrays in server request");
            goto cleanup_batch_server;
        }

        // 初始化数组
        memset(img_files, 0, BATCH_SIZE * sizeof(uint8_t*));
        memset(filenames, 0, BATCH_SIZE * sizeof(char*));
        memset(file_sizes, 0, BATCH_SIZE * sizeof(size_t));

        // 跳过已处理的文件
        int files_to_skip = (current_batch - 1) * BATCH_SIZE;
        int jpg_files_seen = 0;  // 计数已经看到的jpg文件
        
        // 跳过之前批次的文件
        while ((entry = readdir(dir)) != NULL) {
            if (strstr(entry->d_name, ".jpg") != NULL) {  // 不做rcg_code过滤
                if (jpg_files_seen >= files_to_skip) {
                    break;  // 找到了这个批次的起始位置
                }
                jpg_files_seen++;
            }
        }

        // 读取当前批次的文件
        int batch_files = 0;
        while (batch_files < BATCH_SIZE && processed_files < total_files && entry != NULL) {
            // 处理当前entry - 处理所有jpg文件（不做rcg_code过滤）
            if (strstr(entry->d_name, ".jpg") != NULL) {
                char file_path[512];
                snprintf(file_path, sizeof(file_path), "/spiffs/%s", entry->d_name);
                
                FILE *file = fopen(file_path, "rb");
                if (!file) {
                    ESP_LOGE(TAG, "Failed to open file: %s", file_path);
                    entry = readdir(dir);  // 获取下一个文件
                    continue;
                }

                // 获取文件大小
                fseek(file, 0, SEEK_END);
                size_t file_size = ftell(file);
                rewind(file);

                // 分配内存并读取文件内容，优先使用PSRAM
                img_files[batch_files] = NULL;
#if CONFIG_SPIRAM_BOOT_INIT
                img_files[batch_files] = (uint8_t*)heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
                if (!img_files[batch_files]) {
                    // PSRAM分配失败时回退到内部内存
                    img_files[batch_files] = (uint8_t*)malloc(file_size);
                }
                
                if (img_files[batch_files]) {
                    if (fread(img_files[batch_files], 1, file_size, file) == file_size) {
                        filenames[batch_files] = strdup(entry->d_name);
                        file_sizes[batch_files] = file_size;
                        if (filenames[batch_files]) {
                            ESP_LOGI(TAG, "服务器请求批次 %d: 读取文件 %d/%zu: %s, 大小: %zu 字节", 
                                    current_batch, batch_files + 1, BATCH_SIZE, 
                                    entry->d_name, file_size);
                            batch_files++;
                            processed_files++;
                        } else {
                            free(img_files[batch_files]);
                            img_files[batch_files] = NULL;
                        }
                    } else {
                        free(img_files[batch_files]);
                        img_files[batch_files] = NULL;
                    }
                }
                fclose(file);
            }
            entry = readdir(dir);  // 获取下一个文件
        }

        // 上传当前批次的文件
        if (batch_files > 0) {
            // 上传前检查内存状况
            size_t free_before = esp_get_free_heap_size();
            ESP_LOGI(TAG, "服务器请求上传前可用内存: %zu 字节", free_before);
            
            esp_err_t ret = http_post_multipart(CFG_IMG_API_URL, (char*)user_data, ucode, 
                              (const uint8_t**)img_files, 
                              (const char**)filenames, 
                              batch_files,
                              (const size_t*)file_sizes);
            if (ret != ESP_OK) {
                final_result = ESP_FAIL;  // 如果任何一批上传失败，标记最终结果为失败
            }
            
            // 上传后检查内存状况
            size_t free_after = esp_get_free_heap_size();
            ESP_LOGI(TAG, "服务器请求批次 %d/%d: 上传 %d 个文件，内存变化: %zu -> %zu (差值: %ld)", 
                    current_batch, total_batches, batch_files, free_before, free_after, 
                    (long)(free_after - free_before));
        }

cleanup_batch_server:
        // 清理当前批次的资源
        if (img_files) {
            for (int i = 0; i < BATCH_SIZE; i++) {
                if (img_files[i]) free(img_files[i]);
            }
            free(img_files);
        }
        if (filenames) {
            for (int i = 0; i < BATCH_SIZE; i++) {
                if (filenames[i]) free(filenames[i]);
            }
            free(filenames);
        }
        if (file_sizes) {
            free(file_sizes);
        }

        // 在批次之间添加延时，让系统回收内存
        vTaskDelay(pdMS_TO_TICKS(500));  // 延时500ms让内存有时间释放
        
        // 手动触发垃圾回收
        size_t free_heap_after_batch = esp_get_free_heap_size();
        ESP_LOGI(TAG, "服务器请求批次 %d 完成后可用内存: %zu 字节", current_batch, free_heap_after_batch);
    }

    closedir(dir);
    ESP_LOGI(TAG, "服务器请求图片上传完成 - 处理了 %d 个文件，共 %d 批次", 
             processed_files, total_batches);
             
    // 服务器请求的上传完成后，如果上传成功则删除所有上传的图片
    if (final_result == ESP_OK) {
        ESP_LOGI(TAG, "服务器请求上传成功，开始删除所有上传的图片文件");
        
        // 删除所有jpg图片
        DIR *del_dir = opendir("/spiffs");
        if (del_dir) {
            struct dirent *del_entry;
            int deleted_count = 0;
            
            while ((del_entry = readdir(del_dir)) != NULL) {
                if (strstr(del_entry->d_name, ".jpg") != NULL) {  // 删除所有jpg文件
                    char file_path[512];
                    snprintf(file_path, sizeof(file_path), "/spiffs/%s", del_entry->d_name);
                    
                    if (remove(file_path) == 0) {
                        ESP_LOGI(TAG, "已删除: %s", del_entry->d_name);
                        deleted_count++;
                    } else {
                        ESP_LOGE(TAG, "删除失败: %s", del_entry->d_name);
                    }
                }
            }
            closedir(del_dir);
            ESP_LOGI(TAG, "服务器请求删除完成，共删除 %d 个图片文件", deleted_count);
        } else {
            ESP_LOGE(TAG, "无法打开目录进行删除操作");
        }
    } else {
        ESP_LOGW(TAG, "由于上传失败，保留所有图片文件");
    }
    
    resume_mqtt_after_upload(mqtt_stopped);
    g_is_uploading = false;
    return final_result;
}

/**
 * @brief 内存系统诊断函数
 * 
 * 打印系统的内存配置信息，用于调试内存相关问题
 */
static void print_memory_info(void)
{
    ESP_LOGI(TAG, "=== 内存系统诊断 ===");
    
    // 基本堆信息
    ESP_LOGI(TAG, "堆内存信息:");
    ESP_LOGI(TAG, "  - 总可用堆: %ld 字节 (%.2f KB)", 
             esp_get_free_heap_size(), esp_get_free_heap_size()/1024.0);
    ESP_LOGI(TAG, "  - 最小可用堆: %ld 字节 (%.2f KB)", 
             esp_get_minimum_free_heap_size(), esp_get_minimum_free_heap_size()/1024.0);
    
    // 各种内存类型的可用量
    ESP_LOGI(TAG, "分类内存信息:");
    ESP_LOGI(TAG, "  - 内部8位: %zu 字节", heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "  - 内部32位: %zu 字节", heap_caps_get_free_size(MALLOC_CAP_32BIT | MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "  - DMA内存: %zu 字节", heap_caps_get_free_size(MALLOC_CAP_DMA));
    
#if CONFIG_SPIRAM_BOOT_INIT
    ESP_LOGI(TAG, "  - PSRAM: %zu 字节 (%.2f KB)", 
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM), 
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM)/1024.0);
    ESP_LOGI(TAG, "  - PSRAM总大小: %zu 字节 (%.2f KB)", 
             heap_caps_get_total_size(MALLOC_CAP_SPIRAM),
             heap_caps_get_total_size(MALLOC_CAP_SPIRAM)/1024.0);
#endif
    
    ESP_LOGI(TAG, "====================");
}
