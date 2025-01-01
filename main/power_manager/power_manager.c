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





// 重置电池校准（如果需要的话）

// 移除启用低电量警告函数


// 检查是否可以使用加热功能（基于稳定的电量百分比判断）


// 检查是否可以使用红光功能（基于稳定的电量百分比判断，10%阈值）

// ESP32示例代码

/*---------------------------------------------------------------
        ADC Calibration
---------------------------------------------------------------*/

// 初始化电量管理




static uint32_t s_timeout = 0;
// 移除之前添加的 s_first_sleep 变量
static bool s_is_poweron = false;  // 标识是否为正常开机



