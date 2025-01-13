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



power_state_t get_power_state()
{
    return power_state;
} 

int get_battery_power()
{
    return power; // 在返回-1的情况下证明未获取到电量信息
}

// 重置电池校准（如果需要的话）

// 移除启用低电量警告函数


// 检查是否可以使用加热功能（基于稳定的电量百分比判断）


// 检查是否可以使用红光功能（基于稳定的电量百分比判断，10%阈值）

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




static uint32_t s_timeout = 0;
// 移除之前添加的 s_first_sleep 变量
static bool s_is_poweron = false;  // 标识是否为正常开机



