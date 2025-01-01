/**
 * @file image_transfer.h
 * @brief 图像传输模块头文件
 * 
 * 该模块负责设备采集的图像数据传输功能，支持UDP实时图像传输和HTTP批量图像上传。
 * 包括图像文件管理、网络传输和上传状态监控。
 */

#ifndef IMAGE_TRANSFER_H
#define IMAGE_TRANSFER_H
#include "My_system.h"

/**
 * @brief 图像上传服务器URL配置
 */
#if ON_LINE_SERVER
#define CFG_IMG_API_URL "http://www.eyenice.cn/api/common/imgs"    /**< 在线服务器图像上传地址 */
#else
#define CFG_IMG_API_URL "http://www.eyenice.cn:8080/api/common/imgs" /**< 本地服务器图像上传地址 */
#endif

/**
 * @brief UDP图像消息结构体
 * 
 * 包含图像数据和识别结果
 */
typedef struct {
    uint8_t *buffer;    /**< 图像数据缓冲区 */
    float result;       /**< 图像识别结果 */
} QueueMessage_t;

/**
 * @brief 初始化UDP客户端
 * 
 * 配置UDP连接参数，创建发送任务和消息队列
 */
void udp_client_init(void);

/**
 * @brief HTTP多部分表单图像上传
 * 
 * 使用multipart/form-data格式上传多个图像文件到服务器
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
                              size_t img_count, const size_t *file_sizes);

/**
 * @brief 批量上传存储在SPIFFS中的图像
 * 
 * 分批读取SPIFFS中的图像文件并上传到服务器，上传成功后可选择删除文件
 * 
 * @param ucode 用户码
 * @return esp_err_t ESP_OK表示上传成功，ESP_FAIL表示上传失败
 */
esp_err_t upload_pictures(int ucode);

/**
 * @brief 异步上传指定RCG_CODE的图片（避免在MQTT任务中执行）
 *
 * @param ucode 用户码
 * @return esp_err_t ESP_OK表示任务创建成功，ESP_ERR_INVALID_STATE表示已有上传任务
 */
esp_err_t start_upload_pictures_async(int ucode);

/**
 * @brief 专门为服务器请求上传所有图像文件（209命令专用）
 * 
 * 上传SPIFFS中的所有图像文件到服务器，上传成功后删除所有图片
 * 这个函数专门用于处理服务器的209命令请求，不做rcg_code过滤
 * 
 * @param ucode 服务器提供的用户码
 * @return esp_err_t ESP_OK表示上传成功，ESP_FAIL表示上传失败
 */
esp_err_t upload_all_pictures_for_server(int ucode);

/**
 * @brief 获取当前图像上传状态
 * 
 * @return bool true表示正在上传，false表示空闲
 */
bool get_upload_status(void);

/**
 * @brief 更新UDP IP
 * 
 * @param ip 新的UDP IP地址
 */
char* update_udp_ip(void);

#endif 
