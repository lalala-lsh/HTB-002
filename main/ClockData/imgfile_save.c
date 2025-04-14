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

void end_training_session(void)
{
    if (!temp_storage.training_active) {
        ESP_LOGW(TAG, "没有活动的训练会话需要结束");
        return;
    }

    ESP_LOGI(TAG, "结束训练会话:");
    ESP_LOGI(TAG, "  - RCG_CODE: %lu", temp_storage.current_rcg_code);
    ESP_LOGI(TAG, "  - 处理临时图片数量: %d", MAX_TEMP_IMAGES);

    int saved_count = 0;
    int skipped_count = 0;

    for (int i = 0; i < MAX_TEMP_IMAGES; i++) {
        if (!temp_storage.images[i].used || !temp_storage.images[i].data) {
            continue;
        }

        // 检查图片数量限制
        if (ensure_image_count_limit() != ESP_OK) {
            ESP_LOGE(TAG, "确保图片数量限制失败");
            skipped_count++;
            continue;
        }

        // 确保存储空间足够
        if (ensure_storage_space(temp_storage.images[i].size) != ESP_OK) {
            ESP_LOGE(TAG, "确保存储空间失败");
            skipped_count++;
            continue;
        }

        ESP_LOGI(TAG, "处理图片 %d:", i);
        ESP_LOGI(TAG, "  - 图片大小: %d 字节", temp_storage.images[i].size);

        // 使用RCG_CODE作为文件名前缀
        char filename[64];
        snprintf(filename, sizeof(filename), "/spiffs/%lu_%02d%02d%02d.jpg",
                temp_storage.current_rcg_code,
                temp_storage.images[i].timeinfo.tm_hour,
                temp_storage.images[i].timeinfo.tm_min,
                temp_storage.images[i].timeinfo.tm_sec);

        ESP_LOGI(TAG, "  - 正在保存到文件: %s", filename);

        FILE *f = fopen(filename, "w");
        if (f) {
            size_t written = fwrite(temp_storage.images[i].data, 1, temp_storage.images[i].size, f);
            fclose(f);
            if (written == temp_storage.images[i].size) {
                ESP_LOGI(TAG, "  - 成功写入 %d 字节", written);
                saved_count++;
            } else {
                ESP_LOGE(TAG, "  - 写入不完整: %d/%d 字节", written, temp_storage.images[i].size);
                skipped_count++;
            }
        } else {
            ESP_LOGE(TAG, "  - 打开文件失败");
            skipped_count++;
        }

        free(temp_storage.images[i].data);
        temp_storage.images[i].data = NULL;
        temp_storage.images[i].used = false;
    }

    ESP_LOGI(TAG, "训练会话结束:");
    ESP_LOGI(TAG, "  - 成功保存: %d 张图片", saved_count);
    ESP_LOGI(TAG, "  - 跳过/失败: %d 张图片", skipped_count);
    
    // 最后检查存储空间状态
    check_storage_space();

    temp_storage.training_active = false;
    temp_storage.current_index = 0;
    temp_storage.current_rcg_code = 0;
}

/**
 * @brief 清理临时存储（用于训练意外中断时）
 */
void cleanup_temp_storage(void)
{
    if (!temp_storage.training_active) {
        ESP_LOGW(TAG, "没有活动的训练会话需要清理");
        return;
    }

    ESP_LOGW(TAG, "训练会话意外中断，清理临时存储:");
    ESP_LOGW(TAG, "  - RCG_CODE: %lu", temp_storage.current_rcg_code);

    int cleaned_count = 0;
    for (int i = 0; i < MAX_TEMP_IMAGES; i++) {
        if (temp_storage.images[i].used && temp_storage.images[i].data) {
            ESP_LOGW(TAG, "  - 清理临时图片 %d (大小: %d 字节)", i, temp_storage.images[i].size);
            free(temp_storage.images[i].data);
            temp_storage.images[i].data = NULL;
            temp_storage.images[i].used = false;
            cleaned_count++;
        }
    }

    ESP_LOGW(TAG, "临时存储清理完成:");
    ESP_LOGW(TAG, "  - 清理图片数量: %d", cleaned_count);
    ESP_LOGW(TAG, "  - 注意：这些图片未保存到SPIFFS，对应的检测计数应为0");

    temp_storage.training_active = false;
    temp_storage.current_index = 0;
    temp_storage.current_rcg_code = 0;
}

/**
 * @brief 保存闭眼图片到临时存储
 * 
 * @param image_data 图片数据
 * @param image_size 图片大小
 */
esp_err_t save_closed_eye_image(const uint8_t *image_data, size_t image_size)
{
    if (!temp_storage.training_active) {
        ESP_LOGW(TAG, "训练未激活，图片未保存");
        return ESP_FAIL;
    }

    // 检查当前内存状况
    size_t free_heap = esp_get_free_heap_size();
    size_t min_free_heap = esp_get_minimum_free_heap_size();
    
    ESP_LOGI(TAG, "保存新图片到临时存储:");
    ESP_LOGI(TAG, "  - 图片大小: %d 字节 (%.2f KB)", image_size, image_size/1024.0);
    ESP_LOGI(TAG, "  - 存储位置: %d/%d", temp_storage.current_index, MAX_TEMP_IMAGES);
    ESP_LOGI(TAG, "  - 当前可用堆: %zu 字节, 最小可用堆: %zu 字节", free_heap, min_free_heap);
    
    // 检查内存是否足够，如果内存不足则跳过保存
    if (free_heap < (image_size + 50000)) {  // 预留50KB安全边际
        ESP_LOGW(TAG, "内存不足，跳过图片保存 (需要: %zu, 可用: %zu)", image_size, free_heap);
        return ESP_FAIL;
    }

    // 获取当前时间并打印
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    ESP_LOGI(TAG, "  - 捕获时间: %04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    // 如果当前位置已有图片，打印释放信息
    if (temp_storage.images[temp_storage.current_index].data) {
        ESP_LOGI(TAG, "  - 释放位置 %d 的旧图片", temp_storage.current_index);
        free(temp_storage.images[temp_storage.current_index].data);
        temp_storage.images[temp_storage.current_index].data = NULL;
    }

    // 分配内存并保存新图片，优先使用PSRAM
    temp_storage.images[temp_storage.current_index].data = NULL;
#if CONFIG_SPIRAM_BOOT_INIT
    temp_storage.images[temp_storage.current_index].data = heap_caps_malloc(image_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
    if (!temp_storage.images[temp_storage.current_index].data) {
        // PSRAM分配失败时回退到内部内存
        temp_storage.images[temp_storage.current_index].data = malloc(image_size);
    }
    
    if (temp_storage.images[temp_storage.current_index].data) {
        memcpy(temp_storage.images[temp_storage.current_index].data, image_data, image_size);
        temp_storage.images[temp_storage.current_index].size = image_size;
        temp_storage.images[temp_storage.current_index].used = true;
        localtime_r(&now, &temp_storage.images[temp_storage.current_index].timeinfo);

        ESP_LOGI(TAG, "  - 图片保存到临时存储成功");
        ESP_LOGI(TAG, "  - 已分配内存: %d 字节", image_size);
        
        // 更新索引
        int prev_index = temp_storage.current_index;
        temp_storage.current_index = (temp_storage.current_index + 1) % MAX_TEMP_IMAGES;
        ESP_LOGI(TAG, "  - 存储位置更新: %d -> %d", prev_index, temp_storage.current_index);
        
        return ESP_OK;  // 保存成功
    } else {
        ESP_LOGE(TAG, "图片内存分配失败!");
        ESP_LOGE(TAG, "  - 请求内存大小: %d 字节", image_size);
        ESP_LOGE(TAG, "  - 当前内存信息:");
        ESP_LOGE(TAG, "    * 可用堆内存: %ld 字节", esp_get_free_heap_size());
        ESP_LOGE(TAG, "    * 最小可用堆内存: %ld 字节", esp_get_minimum_free_heap_size());
        
        return ESP_FAIL;  // 保存失败
    }
}

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
int test_storage_capacity(void)
{
    ESP_LOGI(TAG, "开始存储容量测试");
    
    // 获取初始空间状态
    size_t total = 0, used = 0;
    esp_err_t ret = esp_spiffs_info(spiffs_conf.partition_label, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "获取存储信息失败");
        return -1;
    }
    
    ESP_LOGI(TAG, "测试前存储状态:");
    check_storage_space();

    // 读取所有现有图片信息
    DIR *dir = opendir("/spiffs");
    if (!dir) {
        ESP_LOGE(TAG, "打开目录失败");
        return -1;
    }

    // 存储现有图片信息
    typedef struct {
        char name[MAX_FILENAME_LEN];  // 增加文件名缓冲区大小
        size_t size;
    } image_info_t;
    
    image_info_t *images = malloc(30 * sizeof(image_info_t));  // 预留30张图片的空间
    int image_count = 0;
    size_t total_original_size = 0;
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && image_count < 30) {
        if (strlen(entry->d_name) == 19 && strstr(entry->d_name, ".jpg")) {
            char filepath[MAX_FILEPATH_LEN];
            snprintf(filepath, sizeof(filepath), "/spiffs/%s", entry->d_name);
            
            // 获取文件大小
            struct stat st;
            if (stat(filepath, &st) == 0) {
                strncpy(images[image_count].name, entry->d_name, sizeof(images[image_count].name)-1);
                images[image_count].size = st.st_size;
                total_original_size += st.st_size;
                image_count++;
            }
        }
    }
    closedir(dir);
    
    ESP_LOGI(TAG, "找到 %d 张原始图片, 总大小: %.2f KB", image_count, total_original_size/1024.0);
    
    // 开始复制图片
    int copies_made = 0;
    size_t safe_limit = total * 0.95;  // 使用95%的空间作为安全限制
    
    while (used < safe_limit) {
        for (int i = 0; i < image_count && used < safe_limit; i++) {
            char new_filename[MAX_FILEPATH_LEN];
            
            // 直接使用一个格式化字符串，限制复制的文件名长度
            // 确保总长度不会超过缓冲区大小
            snprintf(new_filename, sizeof(new_filename), 
                    "/spiffs/%.*s_copy_%04d.jpg", 
                    (int)(strrchr(images[i].name, '.') - images[i].name), // 主文件名长度
                    images[i].name,  // 原始文件名（char*）
                    copies_made);    // 副本编号（int）
            
            // 读取源文件
            char src_path[MAX_FILEPATH_LEN];
            snprintf(src_path, sizeof(src_path), "/spiffs/%.*s", 
                    MAX_FILENAME_LEN - 16, // 预留空间给路径前缀
                    images[i].name);
            
            FILE *src = fopen(src_path, "r");
            if (!src) continue;
            
            uint8_t *buffer = malloc(images[i].size);
            if (!buffer) {
                fclose(src);
                continue;
            }
            
            size_t read_size = fread(buffer, 1, images[i].size, src);
            fclose(src);
            
            if (read_size == images[i].size) {
                FILE *dst = fopen(new_filename, "w");
                if (dst) {
                    if (fwrite(buffer, 1, images[i].size, dst) == images[i].size) {
                        copies_made++;
                        ESP_LOGI(TAG, "复制第 %d 张图片: %s (%.2f KB)", 
                                copies_made, new_filename, images[i].size/1024.0);
                    }
                    fclose(dst);
                }
            }
            
            free(buffer);
            
            // 更新已使用空间
            ret = esp_spiffs_info(spiffs_conf.partition_label, &total, &used);
            if (ret != ESP_OK || used >= safe_limit) break;
        }
    }
    
    free(images);
    
    // 打印最终状态
    ESP_LOGI(TAG, "\n测试结果:");
    ESP_LOGI(TAG, "------------------------");
    ESP_LOGI(TAG, "原始图片数量: %d", image_count);
    ESP_LOGI(TAG, "成功复制数量: %d", copies_made);
    ESP_LOGI(TAG, "总图片数量: %d", image_count + copies_made);
    ESP_LOGI(TAG, "------------------------");
    check_storage_space();
    
    return copies_made;
}
#endif
/**
 * @brief 统计SPIFFS中的所有jpg图片
 * 
 * @param total_size 返回所有图片的总大小(字节)
 * @return int 返回图片总数，失败返回-1
 */
int count_spiffs_images(size_t *total_size)
{
    ESP_LOGI(TAG, "开始统计SPIFFS中的图片");
    
    DIR *dir = opendir("/spiffs");
    if (!dir) {
        ESP_LOGE(TAG, "打开SPIFFS目录失败");
        return -1;
    }

    int image_count = 0;
    size_t total_image_size = 0;
    struct dirent *entry;
    
    while ((entry = readdir(dir)) != NULL) {
        // 只检查是否是jpg文件
        if (strstr(entry->d_name, ".jpg")) {
            char filepath[MAX_FILEPATH_LEN];
            snprintf(filepath, sizeof(filepath), "/spiffs/%s", entry->d_name);
            
            // 获取文件信息
            struct stat st;
            if (stat(filepath, &st) == 0) {
                image_count++;
                total_image_size += st.st_size;
                
                // // 打印每个图片的信息
                // ESP_LOGI(TAG, "图片 %d: %s (%.2f KB)", 
                //         image_count, entry->d_name, st.st_size/1024.0);
            }
        }
    }
    
    closedir(dir);
    
    if (total_size) {
        *total_size = total_image_size;
    }
    
    // 打印统计结果
    ESP_LOGI(TAG, "\n统计结果:");
    ESP_LOGI(TAG, "------------------------");
    ESP_LOGI(TAG, "图片总数: %d", image_count);
    ESP_LOGI(TAG, "总大小: %.2f KB", total_image_size/1024.0);
    ESP_LOGI(TAG, "平均大小: %.2f KB", image_count > 0 ? (total_image_size/image_count)/1024.0 : 0);
    ESP_LOGI(TAG, "------------------------");
    
    // 打印存储空间状态
    check_storage_space();
    
    return image_count;
}

/**
 * @brief 根据RCG_CODE获取对应的图片文件路径列表
 * 
 * @param rcg_code RCG_CODE
 * @param file_paths 输出的文件路径数组
 * @param max_files 最大文件数量
 * @return int 返回找到的文件数量
 */
int get_images_by_rcg_code(uint32_t rcg_code, char file_paths[][MAX_FILEPATH_LEN], int max_files)
{
    DIR *dir = opendir("/spiffs");
    if (!dir) {
        ESP_LOGE(TAG, "打开SPIFFS目录失败");
        return -1;
    }

    int found_count = 0;
    struct dirent *entry;
    char rcg_code_str[16];
    snprintf(rcg_code_str, sizeof(rcg_code_str), "%lu_", rcg_code);
    
    ESP_LOGI(TAG, "查找RCG_CODE为 %lu 的图片", rcg_code);
    
    while ((entry = readdir(dir)) != NULL && found_count < max_files) {
        // 检查是否是jpg文件且以RCG_CODE开头
        if (strstr(entry->d_name, ".jpg") && strncmp(entry->d_name, rcg_code_str, strlen(rcg_code_str)) == 0) {
            snprintf(file_paths[found_count], MAX_FILEPATH_LEN, "/spiffs/%s", entry->d_name);
            ESP_LOGI(TAG, "找到图片 %d: %s", found_count + 1, file_paths[found_count]);
            found_count++;
        }
    }
    
    closedir(dir);
    
    ESP_LOGI(TAG, "总共找到 %d 张RCG_CODE为 %lu 的图片", found_count, rcg_code);
    return found_count;
}


