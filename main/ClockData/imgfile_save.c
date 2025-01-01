#include "imgfile_save.h"

// 在文件开头添加常量定义
#define MAX_IMAGES_COUNT 600  // 最大图片数量限制
#define MAX_IMAGE_DAYS 90    // 图片保存的最大天数

static const char *TAG = "IMGFILE_SAVE";

// SPIFFS配置
static const esp_vfs_spiffs_conf_t spiffs_conf = {
    .base_path = "/spiffs",
    .partition_label = "spiffs",
    .max_files = 5,
    .format_if_mount_failed = true
};

// 临时存储结构定义
typedef struct {
    uint8_t *data;
    size_t size;
    bool used;
    struct tm timeinfo;
} temp_image_t;

static struct {
    temp_image_t images[MAX_TEMP_IMAGES];
    int current_index;
    bool training_active;
    uint32_t current_rcg_code;  // 添加当前训练的RCG_CODE
} temp_storage = {0};

// 修改文件路径缓冲区的大小定义
#define MAX_FILENAME_LEN 256  // 文件名最大长度
#define MAX_FILEPATH_LEN (MAX_FILENAME_LEN + 16)  // /spiffs/ + 文件名 + 一些额外空间
#define COPY_PREFIX_LEN 32    // 用于复制文件前缀的长度

/**
 * @brief 检查并打印存储空间状态
 * @return esp_err_t 
 */

/**
 * @brief 删除指定文件
 */

/**
 * @brief 获取并删除最早的图片
 */

/**
 * @brief 确保有足够的存储空间
 */

/**
 * @brief 确保图片数量不超过限制
 * 如果超过限制，删除最早的图片
 * 
 * @return esp_err_t 
 */

/**
 * @brief 清理超过90天的图片
 */




/**
 * @brief 清理临时存储（用于训练意外中断时）
 */

/**
 * @brief 保存闭眼图片到临时存储
 * 
 * @param image_data 图片数据
 * @param image_size 图片大小
 */

#if 1
/**
 * @brief 测试存储容量极限
 * 通过复制现有图片直到空间接近满载
 * 
 * @return int 成功复制的图片数量
 */
#endif
/**
 * @brief 统计SPIFFS中的所有jpg图片
 * 
 * @param total_size 返回所有图片的总大小(字节)
 * @return int 返回图片总数，失败返回-1
 */

/**
 * @brief 根据RCG_CODE获取对应的图片文件路径列表
 * 
 * @param rcg_code RCG_CODE
 * @param file_paths 输出的文件路径数组
 * @param max_files 最大文件数量
 * @return int 返回找到的文件数量
 */


