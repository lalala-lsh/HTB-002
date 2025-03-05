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


/**
 * @brief 获取当前图像上传状态
 * 
 * @return bool true表示正在上传，false表示空闲
 */

/**
 * @brief 异步上传指定RCG_CODE的图片
 */


/**
 * @brief 批量上传SPIFFS中的图像文件
 * 
 * 分批读取SPIFFS中的图像文件并上传到服务器
 * 上传成功后会删除文件以释放空间
 * 
 * @param ucode 用户码
 * @return esp_err_t ESP_OK表示上传成功，ESP_FAIL表示上传失败
 */

/**
 * @brief 专门为服务器请求上传所有图像文件（209命令专用）
 * 
 * 上传SPIFFS中的所有图像文件到服务器，上传成功后删除所有图片
 * 这个函数专门用于处理服务器的209命令请求
 * 
 * @param ucode 服务器提供的用户码
 * @return esp_err_t ESP_OK表示上传成功，ESP_FAIL表示上传失败
 */

/**
 * @brief 内存系统诊断函数
 * 
 * 打印系统的内存配置信息，用于调试内存相关问题
 */
