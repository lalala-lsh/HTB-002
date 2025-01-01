#include "feed.h"

#include "key.h"
#include "led_status.h"
#include "mqtt.h"
#include "clock.h"
#include "protocal.h"
#include "storage.h"
#include "blufi.h"
#include "voice.h"
#include "camera.h"
#include "xl9535.h"
#include "heating.h"
#include "imgfile_save.h"
#include "power_manager.h"
#include "image_transfer.h"


static const char *TAG = "FEED";
static uint32_t feed_timecnt = 0;
static uint32_t beici_timecnt = 0;
static FEED_STATUS_E feed_status = FEED_NONE;
static uint32_t current_rcg_code = 0;  // 当前训练的RCG_CODE
int feed_st = 0;
int feed_et = 0;
static int64_t feed_start_mono_us = 0;

// 租赁模式防误触相关变量
static bool count_deducted = false;      // 标记是否已经扣减过次数
static uint32_t feed_30s_counter = 0;   // 30秒计时器
static uint8_t training_heat_status = 0; // 记录训练开始时的加热状态

int beici_st = 0;
int beici_et = 0;
static int64_t beici_start_mono_us = 0;

//时间设置
static int feed_timecontrol = 3*60;//linjun
static int color_timecontrol = 5*60;//linjun

static uint8_t beici_first = 0;
static uint8_t s_beici_time_everymode = TRAIN_TIME / BEICI_MODE_MAX;//linjun
static LED_COLOR_E current_color = COLOR_RED;  // 当前颜色

static int FEED_DUTY_MAX = 256 - 1;
static int FEED_DUTY = 0; 
 
extern bool mode_changed;
extern bool auto_switch;
static  int current_mode;
static int beep_status;//linjun

static COLOR_LIGHT_STATUS_E color_light_status = COLOR_LIGHT_NONE;

extern QueueHandle_t voice_evt_queue;
TaskHandle_t cameraTaskHandle = NULL;
#define LOW_POWER_TURN_OFF_HEAT 1

#if EN_RED_SPARK
typedef enum {
    RED_OFF = 0,
    RED_HIGH = 1,
    RED_LOW = 2,
    RED_RISING = 3
} RED_STATUS_E;

static RED_STATUS_E s_red_status = RED_OFF;
static int s_red_status_cnt = 0;
#endif 

// 前向声明所有静态函数
static void feed_count_down_remind(int timecnt);
static void color_count_down_remind(int timecnt);
static void stop_running_light(void);
static void set_pins_status(const snPinName_t* pins, int length, uint8_t status);
static void flash_pins(const snPinName_t* pins, int length, int delay_ms);
static void switch_led_color(void);
static void start_feed_training(void);
static void stop_feed_training(void);
static void start_beici_training(void);
static void stop_beici_training(void);
static void initialize_beici_mode(void);
static void handle_feed_completion(uint8_t *tcount, uint8_t *ccount, uint8_t *bcount);
static void handle_beici_completion(uint8_t *tcount, uint8_t *ccount, uint8_t *bcount);
static void report_or_store_data(int level, time_t start_time, time_t end_time, uint32_t time_count, 
                                int day, uint8_t t_count, uint8_t c_count, uint8_t ecount, uint8_t record_type, uint32_t rcg_code);
static void resolve_training_timestamps(time_t *start_time, time_t *end_time, uint32_t work_time_sec);
static void feed_task(void* arg);
static void running_light_task(void* arg);
static void camera_task(void* arg);

// 添加时间同步检查计数器
static uint32_t time_sync_check_counter = 0;
#define TIME_SYNC_CHECK_INTERVAL 60  // 每60秒检查一次时间同步

// 最近一次可用的真实时间基准，用于降级估算
static time_t s_last_good_wall_ts = 0;
static int64_t s_last_good_wall_mono_us = 0;




/*
用于设置训练倒计时  linjun
*/
//linjun

/*
* 摄像头供电控制
*/

/*
哺光电源管脚初始化，可对红光控制进行关断
*/

/*
哺光管脚初始化，采用LEDCPWM方式初始化，目的是为了进行功率调节
*/

/**
 * @Author LINJUN
 * @brief 蜂鸣器初始化
 */




/*******************************************************************************
 * @brief       创建一个静态函数，用于处理40hz蜂鸣器控制
 * @return      none
 ******************************************************************************/

/*
哺光功能控制
开启: 开启红光功能
关闭: 关闭红光PWM输出
*/

// 开始哺光训练

// 停止哺光训练

/*
BEICI功能控制
开启: 开启灯光闪烁功能
关闭: 关闭灯光闪烁输出
*/

// 开始彩光训练

// 停止彩光训练

// 关闭当前运行的功能


// 处理红光呼吸灯效果
#if EN_RED_SPARK
#endif

// 初始化彩光模式

// 处理哺光完成

// 处理彩光完成

// 网络上报或本地存储数据

// 添加灯光控制的辅助函数


// 修改颜色切换函数

/** 
 * \brief 更新哺光档位占空比
 **/

/** 
 * \brief 更新哺光时间  linjun
 **/
/** 
 * \brief 更新红光模式下加热状态
 * 
 * 当feeding_heat参数更新时，如果当前正在红光模式下运行，
 * 立即根据新参数值开启或关闭加热
 **/
/** 
 * \brief 更新彩光时间  linjun
 **/

/** 
 * \brief 更新蜂鸣器状态  linjun
 **/


