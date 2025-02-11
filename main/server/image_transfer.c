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

/**
 * @brief HTTP客户端事件处理回调
 * 
 * 处理HTTP请求的各种事件，如连接、数据接收等
 * 
 * @param evt HTTP客户端事件
 * @return esp_err_t ESP_OK表示处理成功
 */

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

// 全局上传状态标志
static bool g_is_uploading = false;
static TaskHandle_t g_upload_task_handle = NULL;

typedef struct {
    int ucode;
} upload_task_params_t;




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
