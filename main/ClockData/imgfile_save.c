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
static esp_err_t check_storage_space(void)
{
    size_t total = 0, used = 0;
    esp_err_t ret = esp_spiffs_info(spiffs_conf.partition_label, &total, &used);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS info: %s", esp_err_to_name(ret));
        return ret;
    }

    float used_percent = (used * 100.0) / total;
    ESP_LOGI(TAG, "存储空间状态:");
    ESP_LOGI(TAG, "  - 总容量: %d 字节 (%.2f KB)", total, total/1024.0);
    ESP_LOGI(TAG, "  - 已使用: %d 字节 (%.2f KB)", used, used/1024.0);
    ESP_LOGI(TAG, "  - 可用空间: %d 字节 (%.2f KB)", total - used, (total-used)/1024.0);
    ESP_LOGI(TAG, "  - 使用率: %.1f%%", used_percent);

    if (used > total) {
        ESP_LOGW(TAG, "存储空间信息不一致，执行SPIFFS检查");
        ret = esp_spiffs_check(spiffs_conf.partition_label);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SPIFFS check failed: %s", esp_err_to_name(ret));
        }
    }
    
    return ret;
}

/**
 * @brief 删除指定文件
 */
static esp_err_t delete_file(const char* filepath)
{
    if (remove(filepath) != 0) {
        ESP_LOGE(TAG, "Failed to delete file: %s", filepath);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Deleted file: %s", filepath);
    return ESP_OK;
}

/**
 * @brief 获取并删除最早的图片
 */
static esp_err_t delete_oldest_image(void)
{
    DIR *dir = opendir("/spiffs");
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open SPIFFS directory");
        return ESP_FAIL;
    }

    struct dirent *entry;
    time_t oldest_time = time(NULL);
    char oldest_file[MAX_FILENAME_LEN] = {0};  // 修改这里

    while ((entry = readdir(dir)) != NULL) {
        if (strlen(entry->d_name) == 19 && strstr(entry->d_name, ".jpg")) {
            struct tm file_time = {0};
            char year_str[5] = {0}, month_str[3] = {0}, day_str[3] = {0};
            
            strncpy(year_str, entry->d_name, 4);
            strncpy(month_str, entry->d_name + 4, 2);
            strncpy(day_str, entry->d_name + 6, 2);
            
            file_time.tm_year = atoi(year_str) - 1900;
            file_time.tm_mon = atoi(month_str) - 1;
            file_time.tm_mday = atoi(day_str);
            
            time_t file_time_t = mktime(&file_time);
            if (file_time_t < oldest_time) {
                oldest_time = file_time_t;
                strncpy(oldest_file, entry->d_name, MAX_FILENAME_LEN - 1);  // 确保不会溢出
            }
        }
    }
    closedir(dir);

    if (strlen(oldest_file) > 0) {
        char file_path[MAX_FILEPATH_LEN];  // 修改这里
        snprintf(file_path, MAX_FILEPATH_LEN, "/spiffs/%s", oldest_file);  // 现在安全了
        return delete_file(file_path);
    }

    return ESP_FAIL;
}

/**
 * @brief 确保有足够的存储空间
 */
static esp_err_t ensure_storage_space(size_t required_size)
{
    size_t total = 0, used = 0;
    esp_err_t ret = esp_spiffs_info(spiffs_conf.partition_label, &total, &used);
    if (ret != ESP_OK) return ret;

    size_t available = total - used;
    while (available < required_size) {
        if (delete_oldest_image() != ESP_OK) {
            return ESP_FAIL;
        }
        
        ret = esp_spiffs_info(spiffs_conf.partition_label, &total, &used);
        if (ret != ESP_OK) return ret;
        
        available = total - used;
        ESP_LOGI(TAG, "After deletion - Available: %d", available);
    }

    return ESP_OK;
}

/**
 * @brief 确保图片数量不超过限制
 * 如果超过限制，删除最早的图片
 * 
 * @return esp_err_t 
 */
static esp_err_t ensure_image_count_limit(void)
{
    size_t total_size;
    int count = count_spiffs_images(&total_size);
    
    if (count < 0) {
        ESP_LOGE(TAG, "获取图片数量失败");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "当前图片数量: %d, 限制: %d", count, MAX_IMAGES_COUNT);
    
    // 如果超过限制，删除最早的图片
    while (count >= MAX_IMAGES_COUNT) {
        ESP_LOGI(TAG, "图片数量超过限制，删除最早的图片");
        if (delete_oldest_image() != ESP_OK) {
            ESP_LOGE(TAG, "删除最早图片失败");
            return ESP_FAIL;
        }
        count--;
    }
    
    return ESP_OK;
}

/**
 * @brief 清理超过90天的图片
 */
esp_err_t clean_expired_images(void)
{
    time_t now = time(NULL);
    DIR *dir = opendir("/spiffs");
    if (!dir) return ESP_FAIL;

    esp_err_t ret = ESP_OK;
    int deleted_count = 0, checked_count = 0;
    struct dirent *entry;
    
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".jpg")) {
            checked_count++;
            
            struct tm file_time = {0};
            char year_str[5] = {0}, month_str[3] = {0}, day_str[3] = {0};
            
            strncpy(year_str, entry->d_name, 4);
            strncpy(month_str, entry->d_name + 4, 2);
            strncpy(day_str, entry->d_name + 6, 2);
            
            file_time.tm_year = atoi(year_str) - 1900;
            file_time.tm_mon = atoi(month_str) - 1;
            file_time.tm_mday = atoi(day_str);
            
            double diff_days = difftime(now, mktime(&file_time)) / (24 * 3600);
            
            if (diff_days > MAX_IMAGE_DAYS) {
                char file_path[MAX_FILEPATH_LEN];
                snprintf(file_path, MAX_FILEPATH_LEN, "/spiffs/%s", entry->d_name);
                if (delete_file(file_path) == ESP_OK) {
                    deleted_count++;
                    ESP_LOGI(TAG, "删除过期图片(%.1f天): %s", diff_days, entry->d_name);
                } else {
                    ret = ESP_FAIL;
                }
            }
        }
    }
    
    closedir(dir);
    check_storage_space();
    
    ESP_LOGI(TAG, "Cleanup completed - Checked: %d, Deleted: %d files", 
             checked_count, deleted_count);
             
    return ret;
}

void init_temp_storage(void)
{
    memset(&temp_storage, 0, sizeof(temp_storage));
}

void start_training_session_with_code(uint32_t rcg_code)
{
    for (int i = 0; i < MAX_TEMP_IMAGES; i++) {
        if (temp_storage.images[i].data) {
            free(temp_storage.images[i].data);
            temp_storage.images[i].data = NULL;
        }
        temp_storage.images[i].used = false;
    }
    temp_storage.current_index = 0;
    temp_storage.training_active = true;
    temp_storage.current_rcg_code = rcg_code;  // 保存RCG_CODE
    
    ESP_LOGI(TAG, "开始训练会话，RCG_CODE: %lu", rcg_code);
}


/**
 * @brief 清理临时存储（用于训练意外中断时）
 */

/**
 * @brief 保存闭眼图片到临时存储
 * 
 * @param image_data 图片数据
 * @param image_size 图片大小
 */

esp_err_t spiffs_init(void)
{
    ESP_LOGI(TAG, "初始化SPIFFS文件系统");
    esp_err_t ret = esp_vfs_spiffs_register(&spiffs_conf);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 检查存储空间
    check_storage_space();
    
    // 清理过期文件
    clean_expired_images();
    
    // 确保图片数量限制
    ensure_image_count_limit();
    
    return ESP_OK;
}
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


