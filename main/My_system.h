/**
*文件说明：该文件为系统板子配置、头文件应用及相关参数定义头文件
*
*
*/
#ifndef _MY_SYSTEM_H_
#define _MY_SYSTEM_H_

/*******************************************头文件****************************************************/
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c.h"
#include "driver/rtc_io.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_bt.h"
#include "esp_blufi_api.h"
#include "esp_blufi.h"
#include "esp_crc.h"
#include "esp_random.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "esp_rom_sys.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_chip_info.h"
#include "esp_netif.h"
#include "esp_efuse.h"
#include "esp_task_wdt.h"
#include "esp_sntp.h"
#include "esp_spiffs.h"

#include "spi_flash_mmap.h"

#include "mbedtls/aes.h"
#include "mbedtls/dhm.h"
#include "mbedtls/md5.h"

#include "time.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"
#include "cJSON.h"
#include "sys/unistd.h"

#include "mqtt_client.h"

#include "nvs.h"
#include "nvs_flash.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "stdarg.h"
#include <stddef.h>
#include <sys/time.h>
#include <inttypes.h>
#include <dirent.h>
/*******************************************头文件****************************************************/


/***************************************板载相关宏定义*************************************************/
/**
 * @brief 其他定义
 */
#define SNTP_SYNC_FLAG                      (1)                     /*!< SNTP同步标志位 */
#define TEST_PRINT                          (0)                     /*!< 测试日志打印标志位 */

#define TIME_FIRST_POWEROFF                 (3*60)                 // 首次进入低功耗时长计数：3分钟
#define TIME_NORMAL_POWEROFF                (3*60)                 // 普通进入低功耗时长计数：1分钟
#define INTERVA_HEARTBEAT                   (30*1000)               /*!< MQTT上传 */
#define LOW_POWER                           (1600)                  /*!< 低电量值 - 单位：mV (ADC电压，对应20%电量) */
#define DATA_CNTS                           (240)                   /*!< 每条存储数据的个数 */
#define MODE_COUNT                          6                        /*!<总共5种模式加关闭功能，一共六种*/
#define DEBUG_                              1
#define HAVE_HEATING_MODE_KEY               1                        /*!< 1为有加热按键，0为无加热按键*/

#define ON_LINE_SERVER                      1                        /*!< 0为线下测试，1为线上*/

#define EN_RED_SPARK                        0                        /*!<红光呼吸开关 1表示开启 0 表示关闭*/

/**
 * @brief 功能时长计数定义
 */
#define FEED_TIME                           (180)                   /*!< 哺光训练时长计数 */
#define TRAIN_TIME                          (300)                   /*!< 彩光模式训练时长计数 */
#define HEAT_TIME                           (15*60)                /*!< 加热模式训练时长计数 */

/**
 * @brief 哺光LEDC相关定义
 */
#define FEED_PWM_TIMER                     LEDC_TIMER_0            /*!< LEDC定时器0 */
#define FEED_PWM_MODE                      LEDC_LOW_SPEED_MODE     /*!< LEDC低速模式 */
#define FEED_PWM_DUTY_RES                  LEDC_TIMER_8_BIT        /*!< LEDC分辨率 */
#define FEED_PWM_FREQUENCY                 (5000)                  /*!< LEDC频率 */

#define FEEDL_PWM_OUTPUT_IO                 GPIO_NUM_11             /*!< 哺光左眼IO端口 */
#define FEEDR_PWM_OUTPUT_IO                 GPIO_NUM_47             /*!< 哺光右眼IO端口 */
#define FEED_POWER_OUTPUT_IO                GPIO_NUM_5              /*!< 哺光电源IO端口 */

#define FEEDL_PWM_CHANNEL                   LEDC_CHANNEL_0          /*!< 哺光左眼通道 */
#define FEEDR_PWM_CHANNEL                   LEDC_CHANNEL_1          /*!< 哺光右眼通道 */

/**
 * @brief 加热相关定义  linjun
 */
#define HEAT_PWM_OUTPUT_IO                 GPIO_NUM_45

/**
 * @brief 哺光模式等级及其对应占空比定义
 */
#define FEED_LEVEL1                         (1)                     /*!< 哺光模式等级 - 1级 */
#define FEED_LEVEL2                         (2)                     /*!< 哺光模式等级 - 2级 */
#define FEED_LEVEL3                         (3)                     /*!< 哺光模式等级 - 3级 */

/**
 * @brief DC EN IO定义
 */
#define DC_3V3_EN                           GPIO_NUM_7             /*!< 摄像头EN引脚IO端口 *///新版本修改为扩展IO电源端口    linjun

/**
 * @brief 按键IO定义
 */
#define KEY_1_INPUT_IO                      GPIO_NUM_18             /*!< 按键1 IO端口 */
#define KEY_2_INPUT_IO                      GPIO_NUM_8              /*!< 按键2 IO端口 */
#define KEY_ONOFF_INPUT_IO                  GPIO_NUM_18             /*!< 唤醒按键 IO端口 */
#define GPIO_INPUT_PIN_SEL                  ((1ULL<<KEY_1_INPUT_IO) | (1ULL<<KEY_2_INPUT_IO))   /*!< GPIO输入引脚掩码 */
#define ESP_INTR_FLAG_DEFAULT               (0)                     /*!< 中断标志位 */

/**
 * @brief LEDC相关配置以及IO定义
 */
#define LED_STATUS_RED_OUTPUT_IO            GPIO_NUM_3              /*!< 状态指示灯IO端口 - 红色 */          
#define LED_STATUS_GREEN_OUTPUT_IO          GPIO_NUM_9              /*!< 状态指示灯IO端口 - 绿色 */          
#define LED_STATUS_BLUE_OUTPUT_IO           GPIO_NUM_10             /*!< 状态指示灯IO端口 - 蓝色 */          

/**
 * @brief LEDC相关配置以及IO定义
 */
#define LED_FUNC_BLUE_OUTPUT_IO             GPIO_NUM_21             /*!< 功能指示灯IO端口 - 蓝色 */    
#define LED_FUNC_RED_OUTPUT_IO              GPIO_NUM_2              /*!< 功能指示灯IO端口 - 红色 */
#define LED_FUNC_GREEN_OUTPUT_IO            GPIO_NUM_1              /*!< 功能指示灯IO端口 - 绿色 */

#define LED_FUNC_LEDC_TIMER                 LEDC_TIMER_0            /*!< LEDC定时器 */
#define LED_FUNC_LEDC_MODE                  LEDC_LOW_SPEED_MODE     /*!< LEDC低速度模式 */
#define LED_FUNC_LEDC_FREQ_HZ               (5000)                  /*!< LEDC频率 */
#define LED_FUNC_LEDC_RESOLUTION            LEDC_TIMER_8_BIT        /*!< LEDC分辨率 */

/**
 * @brief 蜂鸣器相关定义  linjun
 */
#define BEEP_PWM_TIMER                      LEDC_TIMER_1            /*!< LEDC定时器1 */
#define BEEP_PWM_MODE                       LEDC_LOW_SPEED_MODE     /*!< LEDC低速模式 */
#define BEEP_PWM_DUTY_RES                   LEDC_TIMER_12_BIT        /*!< LEDC分辨率 */
#define BEEP_PWM_FREQUENCY                  (40)                     /*!< LEDC频率 */
#define BEEP_PWM_OUTPUT_IO                  GPIO_NUM_6             /*!< 蜂鸣器IO端口 */
#define BEEP_PWM_CHANNEL                    LEDC_CHANNEL_2          /*!< 蜂鸣器通道 */


/**
 * @brief DS1302 IO定义
 */
#define DS1302_CLK_PIN                      GPIO_NUM_39             /*!< DS1302 CLK引脚端口号 */
#define DS1302_IO_PIN                       GPIO_NUM_40             /*!< DS1302 IO引脚端口号*/
#define DS1302_CE_PIN                       GPIO_NUM_42             /*!< DS1302 CE引脚端口号*/

/**
 * @brief 语音芯片 IO定义
 */
#define IO_CLK                              GPIO_NUM_41             /*!< 语音芯片 - CLK IO端口号 */
#define IO_DATA                             GPIO_NUM_17             /*!< 语音芯片 - DATA IO端口号 */
#define IO_BUSY                             GPIO_NUM_48             /*!< 语音芯片 - BUSY IO端口号 */
#define IO_FREE                             GPIO_NUM_9              /*!< 语音芯片 - FREE IO端口号 */

/**
 * @brief XL9535拓展IO IIC协议相关配置
 */
#define XL9535_I2C_MASTER_SCL_IO            GPIO_NUM_14             /*!< 拓展IO芯片IIC协议时钟线IO */
#define XL9535_I2C_MASTER_SDA_IO            GPIO_NUM_13             /*!< 拓展IO芯片IIC协议数据线IO */
#define XL9535_I2C_MASTER_FREQ_HZ           (400000)                /*!< 拓展IO芯片IIC协议频率 */
#define XL9535_I2C_MASTER_TX_BUF_DISABLE    (0)                 
#define XL9535_I2C_MASTER_RX_BUF_DISABLE    (0)
#define XL9535_I2C_SLAVE_ADDR               (0x20)                  /*!< 拓展IO芯片IIC协议地址 */
#define XL9535_I2C_MASTER_NUM               I2C_NUM_1               /*!< 拓展IO芯片IIC协议端口号 */
#define XL9535_INT_PIN                      GPIO_NUM_12             /*!< 拓展IO芯片中断引脚 */

/***************************************板载相关宏定义*************************************************/

/*****************************************相关枚举定义*************************************************/
/**
 * @brief 设备可设置参数
 */
typedef enum {
    DEVICE_PARA_BGM = 0,       //背景音乐
    DEVICE_PARA_VOL,           //音量
    DEVICE_PARA_FEED_P,        //红光能量
    DEVICE_PARA_FEED_T,        //红光时间
    DEVICE_PARA_RUOSHI_T,      //彩光时间
    DEVICE_PARA_HEAT_T,        //加热时间
    DEVICE_PARA_CAMERA,        //摄像头开关
    DEVICE_PARA_40HZ,           //蜂鸣器开关 LINJUN
    DEVICE_PARA_UDP_IP,        //UDP IP
    DEVICE_PARA_PICTURE,        //图片 
    DEVICE_PARA_FEEDING_HEAT,  //红光加热开关
    DEVICE_PARA_COUNT_LIMIT,   //剩余次数
    DEVICE_PARA_MAX
} DEVICE_PARA_E;

/**
 * @brief 按键相关枚举
 */
typedef enum {
    KEY_FUNC_OFF        = 0x00,         // 按键功能关闭
    KEY_FUNC_MODE_1     = 0x01,         // 模式一，红光，加热
    KEY_FUNC_MODE_2     = 0x02,         // 模式二，彩光，不加热
    KEY_FUNC_HEAT       = 0x03,         // 加热
    KEY_FUNC_OTA_ING    = 0x04,         // OTA升级
}KEY_FUNC_E;

/**
 * @brief 贝茨模式相关枚举
 */
typedef enum {
    BEICI_MODE_1      = 0x00,         // 模式1
    BEICI_MODE_2      = 0x01,         // 模式2
    BEICI_MODE_3      = 0x02,         // 模式3
    BEICI_MODE_4      = 0x03,         // 模式4
    BEICI_MODE_5      = 0x04,         // 模式5
    BEICI_MODE_MAX    = 0x05          //最大值
}E_BEICI_MODE;
/**
 * @brief OTA相关枚举
 */
typedef enum {
    OTA_STATUS_OFF      = 0x00,         // OTA关闭
    OTA_STATUS_ING      = 0x01,         // OTA进行中
    OTA_STATUS_SUCCESS  = 0x02,         // OTA升级成功
    OTA_STATUS_FAIL     = 0x03,         // OTA升级失败
}OTA_STATUS;

/**
 * @brief WiFi相关枚举
 */
typedef enum {
    WIFI_STATE_IDLE,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_DISCONNECTED,
} wifi_state_t;

/**
 * @brief 电量相关枚举
 */
typedef enum {
   POWER_STATE_LOW      = 0x00,
   POWER_STATE_NORMAL   = 0x01,
} power_state_t;

/**
 * @brief 拓展IO芯片IO口定义
 */
typedef enum __pinname {
    PIN_P00 = 0,
    PIN_P01,
    PIN_P02,
    PIN_P03,
    PIN_P04,
    PIN_P05,
    PIN_P06,
    PIN_P07,
    PIN_P10,
    PIN_P11,
    PIN_P12,
    PIN_P13,
    PIN_P14,
    PIN_P15,
    PIN_P16,
    PIN_P17
} snPinName_t;

/**
 * @brief 拓展IO芯片IO高低电平定义
 */
typedef enum __pinstate {
    IO_LOW = 0,
    IO_HIGH = 1,
    IO_UNKNOW,
} snPinState_t;

/**
 * @brief 拓展IO芯片IO输出模式定义
 */
typedef enum __pinmode {
    IO_OUTPUT = 0,
    IO_INPUT = 1
} snPinMode_t;

/**
 * @brief 拓展IO芯片IO中断定义
 */
typedef enum __pinpolarity {
    IO_NON_INVERTED = 0,
    IO_INVERTED = 1
} snPinPolarity_t;

/**
 * @brief 语音相关枚举
 */
#ifdef VERSION_2_X
// Version 2.x.x 语音枚举定义
typedef enum {
    WELCOME = 0x00,
    FEED_LIGHT, 
    COLOR_LIGHT,
    FEEDTRAIN_END,
    COLORTRAIN_END,
    HEATING, 
    HEATING_END, 
    COUNT_DOWN_1,
    COUNT_DOWN_2,
    COUNT_DOWN_3,
    COUNT_DOWN_4,
    COUNT_DOWN_5,
    OTA_START,
    OTA_SUCCESS,
    OTA_FAIL,
    OPEN_EYES,
    POWER_WARNING,
    DISABLE_HEAT,//低电量加热关闭提醒--linjun
    WEAR_REMIND,//背景识别提醒--linjun
    MUSIC,
    STOP
} SCENE;
#else
// Version 1.x.x 语音枚举定义 (原版本)
typedef enum {
    WELCOME = 0x00,
    OTA_START,
    OTA_SUCCESS,
    OTA_FAIL, 
    FEED_LIGHT, 
    HEATING, 
    HEATING_END, 
    COLOR_LIGHT, 
    MUSIC,
    COUNT_DOWN_1,
    COUNT_DOWN_2,
    COUNT_DOWN_3,
    COUNT_DOWN_4,
    COUNT_DOWN_5,
    OPEN_EYES,
    // NO_NETWORK,
    POWER_WARNING,
    FEEDTRAIN_END,
    COLORTRAIN_END,
    WEAR_REMIND,//背景识别提醒--linjun
    DISABLE_HEAT,//低电量加热关闭提醒--linjun
    STOP
} SCENE;
#endif

/**
 * @brief IO关断相关枚举
 */
typedef enum {
    SOC_GPIO_OUTPUT_OFF,
    SOC_GPIO_OUTPUT_ON
} SOC_GPIO_ONOFF;

/**
 * @brief 功能开关枚举
 */
typedef enum {
    FUNC_OFF = 0,                           /*!< 功能关闭 */
    FUNC_ON = 1                            /*!< 功能开启 */
} FUNC_STATUS;


/*****************************************相关枚举定义*************************************************/

// 系统任务优先级
#define PRIORITY_KEY_INTERRUPT_TASK     12  // 按键中断任务
#define PRIORITY_USB_CONNECT_TASK       4   // 摄像头重连任务
#define PRIORITY_VOICE_TASK             3   // 语音任务
#define PRIORITY_DEEPSLEEP_TASK         2   // 深度睡眠任务
#define PRIORITY_POWER_CHECK_TASK       5   // 电量检测任务
#define PRIORITY_OTA_TASK               6   // OTA升级任务
#define PRIORITY_MQTT_SEND_TASK         8   // MQTT心跳发送任务
#define PRIORITY_CAMERA_TASK            11  // 摄像头任务
#define PRIORITY_RUOSHI_TASK            9   // 彩光功能任务
#define PRIORITY_FEED_TASK              9   // 哺光任务
#define PRIORITY_STATUS_LED_TASK        7   // 指示灯任务

// 系统初始化函数
void SystemInit(void);

#endif // _MY_SYSTEM_H_