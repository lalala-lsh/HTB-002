#ifndef __IMGFILE_SAVE_H__
#define __IMGFILE_SAVE_H__

#include "My_system.h"

// 常量定义
#define MAX_TEMP_IMAGES 3  // 每次训练最多保存3张图片

/**
 * @brief 初始化SPIFFS文件系统
 * 
 * @return esp_err_t ESP_OK: 成功, ESP_FAIL: 失败
 */
esp_err_t spiffs_init(void);

/**
 * @brief 清理超过90天的图片文件
 * 
 * @return esp_err_t ESP_OK: 成功, ESP_FAIL: 失败
 */
esp_err_t clean_expired_images(void);

/**
 * @brief 初始化临时存储区
 */
void init_temp_storage(void);

/**
 * @brief 开始新的训练session，设置RCG_CODE
 * 
 * @param rcg_code 本次训练的RCG_CODE
 */
void start_training_session_with_code(uint32_t rcg_code);

/**
 * @brief 结束训练session，将临时图片保存到SPIFFS
 */
void end_training_session(void);

/**
 * @brief 清理临时存储（用于训练意外中断时）
 */
void cleanup_temp_storage(void);

/**
 * @brief 保存闭眼图片到临时存储
 * 
 * @param image_data 图片数据
 * @param image_size 图片大小
 * @return esp_err_t ESP_OK表示保存成功，ESP_FAIL表示保存失败
 */
esp_err_t save_closed_eye_image(const uint8_t *image_data, size_t image_size);

/**
 * @brief 根据RCG_CODE获取对应的图片文件路径列表
 * 
 * @param rcg_code RCG_CODE
 * @param file_paths 输出的文件路径数组
 * @param max_files 最大文件数量
 * @return int 返回找到的文件数量
 */
int get_images_by_rcg_code(uint32_t rcg_code, char file_paths[][272], int max_files);

/**
 * @brief 测试存储容量极限
 * @return int 成功复制的图片数量
 */
int test_storage_capacity(void);

/**
 * @brief 统计SPIFFS中的图片数量和总大小
 * 
 * @param total_size 返回所有图片的总大小(字节)
 * @return int 返回图片总数，失败返回-1
 */
int count_spiffs_images(size_t *total_size);

#endif // __IMGFILE_SAVE_H__
