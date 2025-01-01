/* 整机电量相关：包括电量相关检测、充电检测及状态指示；进入低功耗调用
 * 
 *
 * 
 */
#include "power_manager.h"
#include "led_status.h"
#include "voice.h"
#include "key.h"
#include "xl9535.h"
#include "image_transfer.h"  // 添加头文件
#include "esp_blufi_api.h"
#include "esp_system.h"  // 添加系统头文件，包含reset reason API
#include <inttypes.h>  // 添加用于PRIu32宏
#include "nvs_flash.h"
#include "nvs.h"

/*---------------------------------------------------------------
        ADC General Macros
---------------------------------------------------------------*/
//ADC1 Channels

#define CHARGE_ADC1_CHAN0          ADC_CHANNEL_3
// 间隔45s进行一次低电量告警 (45秒 / 5秒检查间隔 = 9次)
#define POWER_WARNTIME				9

static int adc_raw;
static int voltage;
int power = -1;

static adc_oneshot_unit_handle_t adc1_handle;
bool do_calibration1 = 0;
static adc_cali_handle_t adc1_cali_handle = NULL;

static power_state_t power_state = POWER_STATE_NORMAL; 

// ADC处理相关变量 - 简化版本，无滤波
bool flag_low_battery = false;
uint32_t power_timecnt = 0; 

// 动态电量校准相关变量
static int max_recorded_voltage = 0;     // 记录的最高电压
static int calibrated_full_voltage = 2000; // 动态校准的满电电压
static bool is_calibration_done = false;   // 是否完成校准

// 移除开机低电量警告延迟控制相关变量

extern QueueHandle_t voice_evt_queue;


// 移除重复的校准函数，校准逻辑整合到 voltage_to_battery_percentage() 中

// 使用动态校准的电压-电量映射函数（仅使用ADC电压）
static int voltage_to_battery_percentage(int adc_voltage_mv)
{
    // 对ADC电压进行动态校准
    if(adc_voltage_mv > max_recorded_voltage) {
        max_recorded_voltage = adc_voltage_mv;
        
        // 如果检测到的ADC电压超过1900mV，进行校准
        if(max_recorded_voltage >= 1900) {
            calibrated_full_voltage = (max_recorded_voltage > 2000) ? 2000 : max_recorded_voltage;
            is_calibration_done = true;
            
            // 保存校准数据到NVS
            nvs_handle_t handle;
            esp_err_t err = nvs_open("battery_cal", NVS_READWRITE, &handle);
            if (err == ESP_OK) {
                nvs_set_i32(handle, "max_voltage", max_recorded_voltage);
                nvs_set_i32(handle, "full_voltage", calibrated_full_voltage);
                uint8_t cal_done = is_calibration_done ? 1 : 0;
                nvs_set_u8(handle, "cal_done", cal_done);
                nvs_commit(handle);
                nvs_close(handle);
            }
            
#if DEBUG_
            ESP_LOGI("BATTERY", "电池校准完成，满电ADC电压设为:%dmV，已保存到NVS", calibrated_full_voltage);
#endif
        }
    }
    
    // 使用校准后的电压范围
    const int ADC_MIN_VOLTAGE = 1500;
    int ADC_MAX_VOLTAGE = is_calibration_done ? calibrated_full_voltage : 2000;

    if (adc_voltage_mv <= ADC_MIN_VOLTAGE) {
        return 0;
    } else if (adc_voltage_mv >= ADC_MAX_VOLTAGE) {
        return 100;
    } else {
        return ((adc_voltage_mv - ADC_MIN_VOLTAGE) * 100) / (ADC_MAX_VOLTAGE - ADC_MIN_VOLTAGE);
    }
}



power_state_t get_power_state()
{
    return power_state;
} 

int get_battery_power()
{
    return power; // 在返回-1的情况下证明未获取到电量信息
}

// 重置电池校准（如果需要的话）
void reset_battery_calibration(void)
{
    max_recorded_voltage = 0;
    calibrated_full_voltage = 2000;  // 重置为默认ADC满电电压
    is_calibration_done = false;
    
    // 删除NVS中的校准数据
    nvs_handle_t handle;
    esp_err_t err = nvs_open("battery_cal", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_erase_key(handle, "max_voltage");
        nvs_erase_key(handle, "full_voltage");
        nvs_erase_key(handle, "cal_done");
        nvs_commit(handle);
        nvs_close(handle);
    }
    
    ESP_LOGI("BATTERY", "电池校准已重置，NVS数据已清除");
}

// 移除启用低电量警告函数


// 检查是否可以使用加热功能（基于稳定的电量百分比判断）
bool can_use_heating_function(void)
{
    
    int current_power = get_battery_power();
    
    // 如果电量未初始化，读取一次ADC作为备用判断
    if(current_power == -1) {
        int current_adc_raw, current_voltage;
        if(adc_oneshot_read(adc1_handle, CHARGE_ADC1_CHAN0, &current_adc_raw) != ESP_OK ||
           adc_cali_raw_to_voltage(adc1_cali_handle, current_adc_raw, &current_voltage) != ESP_OK) {
            ESP_LOGE("POWER", "无法读取ADC电压，禁用加热功能");
            return false;
        }
        
        // 备用阈值：根据校准后的满电电压计算45%电量的ADC电压
        int current_max_voltage = is_calibration_done ? calibrated_full_voltage : 2000;
        const int HEATING_MIN_ADC_VOLTAGE = 1500 + ((current_max_voltage - 1500) * 50) / 100;
        bool can_heat = (current_voltage >= HEATING_MIN_ADC_VOLTAGE);
        
#if DEBUG_
        ESP_LOGI("POWER", "加热功能检查(备用ADC): ADC=%dmV, 阈值=%dmV, 可用=%s", 
                 current_voltage, HEATING_MIN_ADC_VOLTAGE, 
                 can_heat ? "是" : "否");
#endif
        return can_heat;
    }
    
    // 使用稳定的电量百分比进行判断，避免ADC读数波动
    const int HEATING_MIN_BATTERY_PERCENT = 50;  // 50%电量阈值
    bool can_heat = (current_power >= HEATING_MIN_BATTERY_PERCENT);
    
#if DEBUG_
    ESP_LOGI("POWER", "加热功能检查: 当前电量=%d%%, 阈值=%d%%, 可用=%s", 
             current_power, HEATING_MIN_BATTERY_PERCENT, 
             can_heat ? "是" : "否");
#endif
    
    return can_heat;
}


// 检查是否可以使用红光功能（基于稳定的电量百分比判断，10%阈值）
bool can_use_red_light_function(void)
{
    int current_power = get_battery_power();
    
    // 如果电量未初始化，读取一次ADC作为备用判断
    if(current_power == -1) {
        int current_adc_raw, current_voltage;
        if(adc_oneshot_read(adc1_handle, CHARGE_ADC1_CHAN0, &current_adc_raw) != ESP_OK ||
           adc_cali_raw_to_voltage(adc1_cali_handle, current_adc_raw, &current_voltage) != ESP_OK) {
            ESP_LOGE("POWER", "无法读取ADC电压，禁用红光功能");
            return false;
        }
        
        // 备用阈值：根据校准后的满电电压计算20%电量的ADC电压
        int current_max_voltage = is_calibration_done ? calibrated_full_voltage : 2000;
        const int RED_LIGHT_MIN_ADC_VOLTAGE = 1500 + ((current_max_voltage - 1500) * 20) / 100;
        bool can_use_red = (current_voltage >= RED_LIGHT_MIN_ADC_VOLTAGE);
        
#if DEBUG_
        ESP_LOGI("POWER", "红光功能检查(备用ADC): ADC=%dmV, 阈值=%dmV, 可用=%s", 
                 current_voltage, RED_LIGHT_MIN_ADC_VOLTAGE, 
                 can_use_red ? "是" : "否");
#endif
        return can_use_red;
    }
    
    // 使用稳定的电量百分比进行判断，避免ADC读数波动
    const int RED_LIGHT_MIN_BATTERY_PERCENT = 20;  // 20%电量阈值
    bool can_use_red = (current_power >= RED_LIGHT_MIN_BATTERY_PERCENT);
    
#if DEBUG_
    ESP_LOGI("POWER", "红光功能检查: 当前电量=%d%%, 阈值=%d%%, 可用=%s", 
             current_power, RED_LIGHT_MIN_BATTERY_PERCENT, 
             can_use_red ? "是" : "否");
#endif
    
    return can_use_red;
}

// ESP32示例代码
void send_battery_level(void) {
    const uint8_t header = 0xAA;
    uint8_t checksum = (header + get_battery_power()) % 256;
    uint8_t data[] = {header, get_battery_power(), checksum};
    
    // 假设使用Blufi自定义数据通道
    esp_blufi_send_custom_data(data, sizeof(data));
}

/*---------------------------------------------------------------
        ADC Calibration
---------------------------------------------------------------*/
static bool charge_adc_calibration_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

    if (!calibrated) {
        ESP_LOGI("POWER", "calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI("POWER", "Calibration Success");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        ESP_LOGW("POWER", "eFuse not burnt, skip software calibration");
    } else {
        ESP_LOGE("POWER", "Invalid arg or no memory");
    }

    return calibrated;
}
static void power_adc_init(void)
{
    //-------------ADC1 Init---------------//
    
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    //-------------ADC1 Config---------------//
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,     //Default ADC output bits, max supported width will be selected.
        .atten = ADC_ATTEN_DB_11,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, CHARGE_ADC1_CHAN0, &config));  

    
    do_calibration1 = charge_adc_calibration_init(ADC_UNIT_1, ADC_ATTEN_DB_11, &adc1_cali_handle);  
}

// 初始化电量管理
static void power_init(void)
{
    // 尝试从NVS读取之前保存的校准数据
    nvs_handle_t handle;
    esp_err_t err = nvs_open("battery_cal", NVS_READONLY, &handle);
    
    if (err == ESP_OK) {
        // 读取保存的校准数据
        size_t required_size = sizeof(int32_t);
        int32_t stored_max_voltage = 0;
        int32_t stored_full_voltage = 2000;
        uint8_t stored_cal_done = 0;
        
        nvs_get_i32(handle, "max_voltage", &stored_max_voltage);
        nvs_get_i32(handle, "full_voltage", &stored_full_voltage);
        nvs_get_u8(handle, "cal_done", &stored_cal_done);
        
        nvs_close(handle);
        
        // 验证读取的数据是否合理
        if (stored_full_voltage >= 1800 && stored_full_voltage <= 2000 && stored_max_voltage > 0) {
            max_recorded_voltage = stored_max_voltage;
            calibrated_full_voltage = stored_full_voltage;
            is_calibration_done = (stored_cal_done == 1);
            
#if DEBUG_
            ESP_LOGI("POWER", "从NVS恢复校准数据: 最高=%dmV, 满电=%dmV, 已校准=%s", 
                     max_recorded_voltage, calibrated_full_voltage, 
                     is_calibration_done ? "是" : "否");
#endif
        } else {
            // 数据无效，使用默认值
            max_recorded_voltage = 0;
            calibrated_full_voltage = 2000;
            is_calibration_done = false;
            ESP_LOGI("POWER", "NVS校准数据无效，使用默认值");
        }
    } else {
        // 首次运行或NVS错误，使用默认值
        max_recorded_voltage = 0;
        calibrated_full_voltage = 2000;
        is_calibration_done = false;
        ESP_LOGI("POWER", "未找到NVS校准数据，使用默认值");
    }
    
    ESP_LOGI("POWER", "电量管理初始化完成");
}

static void power_check()
{
    // adc_raw是ADC转换结果的原始值
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, CHARGE_ADC1_CHAN0, &adc_raw));
    // adc_cali_raw_to_voltage用于校准转换结果
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_handle, adc_raw, &voltage));

    // 使用简化的电压-电量映射函数
    power = voltage_to_battery_percentage(voltage);
    
    // 设置电量状态
    if (voltage > LOW_POWER){
#if DEBUG_
         ESP_LOGI("BATTERY", "电量正常");
         ESP_LOGI("电量", "ADC原始值 = %d, 百分比 = %d%%, ADC电压 = %dmV", 
                  adc_raw, power, voltage);
#endif
         power_timecnt = 0;
         power_state = POWER_STATE_NORMAL;
    } else {
        // 设置低电量状态
        if(power_state != POWER_STATE_LOW) {
            power_state = POWER_STATE_LOW;
            power_timecnt = 0; // 重置计数器，开始新的45秒倒计时
#if DEBUG_
            ESP_LOGI("BATTERY", "设置为低电量状态，ADC原始值:%d, 百分比:%d%%, ADC电压:%dmV", 
                     adc_raw, power, voltage);   
#endif
        }
    }   
    
#if DEBUG_
    ESP_LOGI("BATTERY", "电量显示:%d%%, ADC原始值:%d, ADC电压:%dmV", 
             power, adc_raw, voltage);
#endif
}

static void power_check_task(void* arg)
{
    for(;;) {
        // 始终进行电量检测
        power_check();
        
        power_timecnt++;
        // 发出低电量警告
        if(power_timecnt >= POWER_WARNTIME && power_state == POWER_STATE_LOW){
            // 检查是否处于训练状态，训练状态下不发送低电量语音警告
            extern bool training_flag;
            if(!training_flag) {
                uint8_t command = POWER_WARNING;
                xQueueSendFromISR(voice_evt_queue, &command, NULL);
                ESP_LOGI("POWER", "非训练状态，发送低电量警告");
            } else {
                ESP_LOGI("POWER", "训练状态下跳过低电量语音警告");
            }
            power_timecnt = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(5000));  // 5秒刷新一次
    }
}

static void power_check_first()
{
    ESP_LOGI("POWER", "开始首次电量检测...");
    
    // 等待ADC稳定后进行一次检测
    vTaskDelay(pdMS_TO_TICKS(100));
    power_check();
    
    ESP_LOGI("POWER", "首次电量检测完成，当前电量: %d%%, 电源状态: %s", 
             get_battery_power(), 
             (get_power_state() == POWER_STATE_LOW) ? "低电量" : "正常");
    
    xTaskCreate(power_check_task, "power_check_task", 4096, NULL, PRIORITY_POWER_CHECK_TASK, NULL);
}

static uint32_t s_timeout = 0;
// 移除之前添加的 s_first_sleep 变量
static bool s_is_poweron = false;  // 标识是否为正常开机

static void enter_deepsleep(void)
{
    //进入低功耗后关闭音乐  linjun
    uint8_t command = STOP;
    xQueueSendFromISR(voice_evt_queue, &command, NULL);
    
    // 拓展 IO 拉低
    extend_IO_sleep();
    
    // ESP32S3 低功耗运行
    const uint64_t ext_wakeup_pin_1_mask = 1ULL << KEY_ONOFF_INPUT_IO;
    esp_sleep_enable_ext1_wakeup(ext_wakeup_pin_1_mask, ESP_EXT1_WAKEUP_ALL_LOW);////ESP_EXT1_WAKEUP_ANY_HIGH
    esp_deep_sleep_start();
}

static void deepsleep_task(void* arg)
{   
    for (;;) {
#if DEBUG_
        // 进入低功耗的情况：无操作、无OTA、无处于联网过程中、无图片上传
        printf("低功耗检测任务中按键状态：%d\n", key_get_status());
#endif
        if (KEY_FUNC_OFF == key_get_status() && !get_upload_status()) {  // 使用函数获取状态
            s_timeout++;
        } else {
            s_timeout = 0;
        }

        // 根据唤醒原因选择不同的超时时间
        uint32_t poweroff_timeout = s_is_poweron ? TIME_FIRST_POWEROFF : TIME_NORMAL_POWEROFF;
        
        if (s_timeout >= poweroff_timeout) {
            s_timeout = 0;
            ESP_LOGI("DEEPSLEEP", "timeout....Entering deep sleep");
            enter_deepsleep();
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }  
}

void power_manager_init(void)
{
    // 获取复位原因
    esp_reset_reason_t reset_reason = esp_reset_reason();
    // 检查唤醒原因
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    
    // 只有在正常上电复位且不是从睡眠唤醒的情况下才使用3分钟超时
    if (reset_reason == ESP_RST_POWERON && wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED) {
        // 如果是正常开机
        s_is_poweron = true;
        ESP_LOGI("DEEPSLEEP", "正常开机唤醒(POWERON)，3分钟后进入低功耗");
    } else {
        // 如果是其他唤醒原因（睡眠唤醒或其他复位原因如brownout）
        s_is_poweron = false;
        
        // 输出详细的唤醒和复位信息，便于调试
        if (wakeup_reason != ESP_SLEEP_WAKEUP_UNDEFINED) {
            ESP_LOGI("DEEPSLEEP", "从睡眠状态唤醒，唤醒原因:%d，1分钟后进入低功耗", wakeup_reason);
        } else {
            const char* reset_reasons[] = {
                "正常上电复位", "未知原因复位", "硬件看门狗复位", "软件看门狗复位", 
                "软件触发复位", "深度睡眠唤醒", "掉电复位(Brownout)"
            };
            ESP_LOGI("DEEPSLEEP", "系统复位，原因:%s，1分钟后进入低功耗", 
                     (reset_reason < 7) ? reset_reasons[reset_reason] : "其他原因");
        }
    }

    power_adc_init();  // 电池电量ADC检测初始化
    power_init();  // 电量管理初始化

    xTaskCreate(deepsleep_task, "deepsleep_task", 4096, NULL, PRIORITY_DEEPSLEEP_TASK, NULL);
    
    power_check_first();
}