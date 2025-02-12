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

static void update_time_anchor_if_valid(time_t ts)
{
    if (ts >= 1704067200) {
        s_last_good_wall_ts = ts;
        s_last_good_wall_mono_us = esp_timer_get_time();
    }
}

static time_t estimate_wall_time_from_anchor(void)
{
    if (s_last_good_wall_ts <= 0 || s_last_good_wall_mono_us <= 0) {
        return 0;
    }
    int64_t now_mono_us = esp_timer_get_time();
    int64_t delta_sec = (now_mono_us - s_last_good_wall_mono_us) / 1000000;
    return s_last_good_wall_ts + (time_t)delta_sec;
}

static void resolve_training_timestamps(time_t *start_time, time_t *end_time, uint32_t work_time_sec)
{
    // ============ 时间源可信度分析 ============
    //
    // ESP32系统时钟 time() 特性：
    //   - 开机时由 RTCTimeToSystemTime() 从 DS1302 或 NVS 设置初始值
    //   - 设置后走时准确（基于ESP32内部晶振，不受DS1302影响）
    //   - SNTP同步后绝对时间最精准
    //
    // DS1302 外部RTC 特性：
    //   - 走时可能严重偏慢（实测180秒真实时间只走86秒）
    //   - 每次调用 get_rtc_timestamp() 读到的是偏慢的时间
    //   - 绝对不能用两次RTC读数之差来计算时长
    //
    // Monotonic时钟 esp_timer_get_time() 特性：
    //   - 时长绝对准确，但没有绝对时间含义
    //
    // ============ 解算策略 ============
    //
    // end_time: 优先取系统时间 time()（无论是否SNTP同步过，走时都准）
    // start_time: end_time - work_time_sec（monotonic时长反推）
    // 这样保证: END - START == WORK_TIME 恒成立
    //
    // 场景覆盖:
    //   A. 联网+SNTP同步: end取SNTP校准后的time() → 绝对时间精准
    //   B. 开机按键后才联网: 训练中途SNTP同步 → end取校准后time() → 精准
    //   C. 全程断网: end取开机时从RTC/NVS初始化的time() → 走时准，绝对值可能有偏
    //   D. RTC坏+断网+NVS无备份: time()可能是默认2025年 → 走时准但绝对值不对
    //   以上所有场景 END-START==WORK_TIME 均成立

    time_t sys_time = time(NULL);

    // 优先级1: 系统时间有效（>=2024年）—— 无论是否SNTP同步，走时都准
    // SNTP同步过则绝对时间精准；未同步则绝对值可能有偏但时长正确
    if (sys_time >= 1704067200) {
        *end_time = sys_time;
        *start_time = sys_time - (time_t)work_time_sec;

        extern bool RTCTimeSet_flag;
        ESP_LOGI("RESOLVE_TS", "系统时间有效(SNTP=%s): end=%ld, start=%ld, work=%lu",
                 RTCTimeSet_flag ? "已同步" : "未同步",
                 (long)*end_time, (long)*start_time, (unsigned long)work_time_sec);
        return;
    }

    // 优先级2: 系统时间无效，用锚点(上次有效时间 + monotonic增量)估算
    time_t estimated_now = estimate_wall_time_from_anchor();
    if (estimated_now >= 1704067200) {
        *end_time = estimated_now;
        *start_time = estimated_now - (time_t)work_time_sec;
        ESP_LOGI("RESOLVE_TS", "使用锚点估算: end=%ld, start=%ld, work=%lu",
                 (long)*end_time, (long)*start_time, (unsigned long)work_time_sec);
        return;
    }

    // 优先级3: 开始时记录的时间戳有效，用它正推end
    // 注意：这个start来自训练开始时的 get_safe_timestamp()，可能是RTC读数
    // 虽然RTC走时偏慢，但至少start那一刻的绝对时间是当时最佳估计
    if (*start_time >= 1704067200) {
        *end_time = *start_time + (time_t)work_time_sec;
        ESP_LOGW("RESOLVE_TS", "系统时间无效，用start正推: start=%ld, end=%ld, work=%lu",
                 (long)*start_time, (long)*end_time, (unsigned long)work_time_sec);
        return;
    }

    // 所有时间源都不可用（极端情况：RTC坏+断网+NVS无备份+默认时间<2024年）
    // 仍然保存记录，时间戳为0，至少work_time是准的
    ESP_LOGW("RESOLVE_TS", "所有时间源不可用, start=0, end=0, work=%lu",
             (unsigned long)work_time_sec);
    *start_time = 0;
    *end_time = 0;
}

/*
用于设置训练倒计时  linjun
*/
static void feed_count_down_remind(int timecnt){
    // 只在有效的倒计时时间播放语音，避免在开始时播放
    if(timecnt > 0) {
        switch(timecnt/60){
            case 1:
                play_voice(COUNT_DOWN_1);
                break;
            case 2:
                play_voice(COUNT_DOWN_2);
                break;
            default:
                break;  
        }
    }
}
//linjun
static void color_count_down_remind(int timecnt){
    // 只在有效的倒计时时间播放语音，避免在开始时播放
    if(timecnt > 0) {
        switch(timecnt/60){
            case 1:
                play_voice(COUNT_DOWN_1);
                break;
            case 2:
                play_voice(COUNT_DOWN_2);
                break;
            case 3:
                play_voice(COUNT_DOWN_3);
                break;
            case 4:
                play_voice(COUNT_DOWN_4);
                break;
            default:
                break;  
        }
    }
}

/*
* 摄像头供电控制
*/
void camera_power_control(uint8_t en_flag)
{
    Xl9535_Set_Io_Direction(PIN_P11, IO_OUTPUT);
    
    if (en_flag) {
        Xl9535_Set_Io_Status(PIN_P11, IO_HIGH);
#if DEBUG_
        ESP_LOGI(TAG, "开启摄像头供电");
#endif
    } else {
        Xl9535_Set_Io_Status(PIN_P11, IO_LOW);
#if DEBUG_
        ESP_LOGI(TAG, "断开摄像头供电");
#endif
    }
}

/*
哺光电源管脚初始化，可对红光控制进行关断
*/
void drv_feed_powerIO_init(SOC_GPIO_ONOFF onoff)
{
    static bool is_initialized = false;
    
    // 只在第一次调用时进行初始化
    if (!is_initialized) {
        gpio_config_t io_conf = {
            .pin_bit_mask = 1ULL << FEED_POWER_OUTPUT_IO,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = 0,
            .pull_down_en = 0,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&io_conf);
        is_initialized = true;
    }

    // 设置GPIO电平
    gpio_set_level(FEED_POWER_OUTPUT_IO, (onoff == SOC_GPIO_OUTPUT_ON) ? 1 : 0);
}

/*
哺光管脚初始化，采用LEDCPWM方式初始化，目的是为了进行功率调节
*/
void feed_gpio_init(void)
{
    feed_timecnt = 0;
    feed_status = FEED_NONE;
    
    // 配置PWM定时器
    ledc_timer_config_t feed_ledc_timer = {
        .speed_mode       = FEED_PWM_MODE,
        .timer_num        = FEED_PWM_TIMER,
        .duty_resolution  = FEED_PWM_DUTY_RES,
        .freq_hz          = FEED_PWM_FREQUENCY,  // 设置输出频率为5 kHz
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&feed_ledc_timer));

    // 配置左侧PWM通道
    ledc_channel_config_t feed_ledc_channel_l = {
        .speed_mode     = FEED_PWM_MODE,
        .channel        = FEEDL_PWM_CHANNEL,
        .timer_sel      = FEED_PWM_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = FEEDL_PWM_OUTPUT_IO,
        .duty           = 0, // 初始占空比为0%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&feed_ledc_channel_l));

    // 配置右侧PWM通道
    ledc_channel_config_t feed_ledc_channel_r= {
        .speed_mode     = FEED_PWM_MODE,
        .channel        = FEEDR_PWM_CHANNEL,
        .timer_sel      = FEED_PWM_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = FEEDR_PWM_OUTPUT_IO,
        .duty           = 0, // 初始占空比为0%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&feed_ledc_channel_r));

    // 使能摄像头电源
    camera_power_control(1);
    
    // 安装淡入淡出功能
    ledc_fade_func_install(0);
}

/**
 * @Author LINJUN
 * @brief 蜂鸣器初始化
 */

static void color_light_gpio_init(void)
{
    // 设置彩光控制GPIO
    Xl9535_Set_Io_Direction(PIN_P06, IO_OUTPUT);
    
    // 定义所有灯光PIN
    static const snPinName_t all_led_pins[] = {
        PIN_P05, PIN_P04, PIN_P03, PIN_P02, PIN_P01, PIN_P00, 
        PIN_P14, PIN_P12, PIN_P13, PIN_P17, PIN_P16, PIN_P15
    };
    
    // 设置所有灯光PIN为输出且默认关闭状态(高电平关闭)
    for (int i = 0; i < sizeof(all_led_pins) / sizeof(all_led_pins[0]); i++) {
        Xl9535_Set_Io_Direction(all_led_pins[i], IO_OUTPUT);
        Xl9535_Set_Io_Status(all_led_pins[i], IO_HIGH);
    }
}

uint8_t feed_set_status()
{
    return feed_status;
}


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
void beici_func_set(uint32_t onoff)
{
    if (FUNC_ON == onoff) {
        start_beici_training();
    } else {
        stop_beici_training();
    }
}

// 开始彩光训练
static void start_beici_training(void)
{
    set_func_led(KEY_FUNC_MODE_2, FUNC_ON);
    
    if (feed_status != BEICI_ING) {
        // 贝西训练开始前也要检查时间有效性
        ESP_LOGI(TAG, "彩光训练开始前检查时间有效性...");
        
        // if (!is_time_valid()) {
        //     ESP_LOGW(TAG, "时间未同步，彩光训练无法开始！等待时间同步...");
            
        //     if (!wait_for_time_sync(30)) {
        //         ESP_LOGE(TAG, "时间同步失败，彩光训练被中止");
        //         return;
        //     }
            
        //     ESP_LOGI(TAG, "时间同步成功，可以开始彩光训练");
        // }
        
        feed_status = BEICI_ING;
        beici_timecnt = 0;
        
        // 尝试获取开始时间戳，失败不阻塞功能
        time_t start_ts = get_safe_timestamp();
        beici_st = (int)start_ts;
        beici_start_mono_us = esp_timer_get_time();
        update_time_anchor_if_valid(start_ts);
        
        ESP_LOGI(TAG, "开始彩光训练，start_ts: %ld, mono_us: %lld", (long)beici_st,
                 (long long)beici_start_mono_us);
        
        // 立即禁用图像识别
        set_decode_flag(false);
        ESP_LOGI(TAG, "进入彩光模式，禁用图像识别");
    }
    
    current_mode = 0;
    drv_feed_powerIO_init(SOC_GPIO_OUTPUT_OFF);
    Xl9535_Set_Io_Status(PIN_P06, IO_LOW);
    //开启蜂鸣器    LINJUN
    dal_40hz_beep_control_switch(1);
}

// 停止彩光训练

// 关闭当前运行的功能

static void feed_task(void* arg)
// 哺光任务有两种结束情况：
// 1、时间到，正常结束—— 通过计数器和状态连同判断，结束后，数据需要上传（有网络）或者本地存储（无网络）
// 2、时间未到，被切换或者关闭—— 先允许这种情况，如果后面有问题再做成3分钟内允许切换成其他训练也不能关闭
{
#if EN_RED_SPARK
    int duty = 0;
#endif
    uint8_t tcount = 0;
    uint8_t ccount = 0;
    uint8_t bcount = 0;
    
    for(;;) {
        // 定期同步系统时间与RTC时间
        if (++time_sync_check_counter >= TIME_SYNC_CHECK_INTERVAL) {
            sync_system_with_rtc_if_needed();
            time_sync_check_counter = 0;
        }
        
        // 处理哺光状态
        if (feed_status == FEED_ING) {
#if DEBUG_
            ESP_LOGI(TAG, "feed_status == FEED_ING, feed_timecnt = %ld", feed_timecnt);
#endif
            // 处理红光呼吸效果
#if EN_RED_SPARK
            handle_red_light_breathing(&duty);
#endif
            // 红光训练倒计时提醒
            if(feed_timecnt % 60 == 0) {
                feed_count_down_remind(feed_timecnt);
            }
            
            // 检查哺光是否完成
            if (feed_timecnt >= feed_timecontrol) {
                handle_feed_completion(&tcount, &ccount, &bcount);
            }     
            feed_timecnt++;                  
        } 
        
        // 处理彩光状态
        if (feed_status == BEICI_ING) {
            ESP_LOGI("BEICI", "%ld, 进入beici_task", beici_timecnt);
            // 彩光首次初始化
            if (beici_first == 0) {
                initialize_beici_mode();
            }
            
            // 彩光倒计时提醒
            if(beici_timecnt % 60 == 0) {
                color_count_down_remind(beici_timecnt);
            }
            
            // 检查彩光是否完成
            if (beici_timecnt >= color_timecontrol) {
                handle_beici_completion(&tcount, &ccount, &bcount);
            }
            beici_timecnt++;
        }
        
        vTaskDelay(1000 / portTICK_PERIOD_MS);  // 任务延时，防止看门狗复位
    }
}

// 处理红光呼吸灯效果
#if EN_RED_SPARK
#endif

// 初始化彩光模式

// 处理哺光完成

// 处理彩光完成

// 网络上报或本地存储数据
static void report_or_store_data(int level, time_t start_time, time_t end_time, uint32_t time_count,
                                int day, uint8_t t_count, uint8_t c_count, uint8_t ecount, uint8_t record_type, uint32_t rcg_code) {
    // 放宽保存条件：只要时间有效就保存，不再强制要求RTCTimeSet_flag
    // 这样即使RTC硬件损坏，只要SNTP同步过或有NVS备份时间，就可以保存数据
    if (!is_time_valid()) {
        ESP_LOGW(TAG, "时间无效，使用降级时间策略继续保存训练数据");
    }

    // 检查时间戳是否合理（2024年之后）
    time_t sys_time = time(NULL);
    if (sys_time < 1704067200) {  // 2024-01-01 00:00:00 UTC
        ESP_LOGW(TAG, "系统时间戳过小 (%ld)，继续保存训练数据（降级）", (long)sys_time);
    }

    ESP_LOGI(TAG, "时间有效，系统时间戳: %ld，开始保存训练数据", (long)sys_time);
    
    // 使用传入的rcg_code，不再重新生成
    
    // 如果是哺光训练（有图片识别），先保存图片到SPIFFS
    if (level == 0x01) {  // 哺光训练
        // 先结束训练会话，将临时图片保存到SPIFFS
        end_training_session();
        ESP_LOGI(TAG, "哺光训练结束，图片已保存到SPIFFS，RCG_CODE: %lu", rcg_code);
    }
    
    if (get_wifi_status() == WIFI_STATE_CONNECTED && get_mqtt_status() == MQTT_EVENT_CONNECTED_) {
        ESP_LOGI(TAG, "report to mqtt, level:%d, st:%lld, et:%lld, rcg_code:%lu", level, start_time, end_time, rcg_code);

        // 使用训练开始时记录的加热状态
        build_feedreport(get_mqtt_client(), level, start_time, end_time, time_count, rcg_code, t_count, c_count, ecount, training_heat_status);

        // 注意：图片上传已改为等待CMD:103回复后再执行
        ESP_LOGI(TAG, "训练记录已发送，等待服务器回复后再上传图片");
    } else {
        // 使用训练开始时记录的加热状态，写本地flash
        store_record(level, start_time, end_time, time_count, rcg_code, day, t_count, c_count, ecount, training_heat_status, record_type);
        ESP_LOGI(TAG, "generate record & store to flash, rcg_code: %lu", rcg_code);
    }
}

// 添加灯光控制的辅助函数
static void set_pins_status(const snPinName_t* pins, int length, uint8_t status) {
    for (int i = 0; i < length; i++) {
        Xl9535_Set_Io_Status(pins[i], status);
    }
}

static void flash_pins(const snPinName_t* pins, int length, int delay_ms) {
    set_pins_status(pins, length, IO_LOW);
    vTaskDelay(delay_ms / portTICK_PERIOD_MS);
    set_pins_status(pins, length, IO_HIGH);
}

// 修改颜色切换函数
static void switch_led_color(void) {
    if (current_color == COLOR_RED) {
        drv_feed_powerIO_init(SOC_GPIO_OUTPUT_ON); // 关闭红色
        Xl9535_Set_Io_Status(PIN_P06, IO_LOW);     // 打开绿色
        current_color = COLOR_GREEN;
    } else {
        Xl9535_Set_Io_Status(PIN_P06, IO_HIGH);    // 关闭绿色
        drv_feed_powerIO_init(SOC_GPIO_OUTPUT_OFF); // 打开红色
        current_color = COLOR_RED;
    }
}

/** 
 * \brief 更新哺光档位占空比
 **/

/** 
 * \brief 更新哺光时间  linjun
 **/
void update_feed_time(void)
{
    int feed_t = 0;

    feed_t = get_device_para(DEVICE_PARA_FEED_T);
    feed_timecontrol = feed_t * 60;//linjun
}
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

static void camera_task(void* arg){
    static bool first_feed_detect = true;  // 添加静态变量跟踪第一次检测

    for(;;) {
        // 只在红光模式下进行摄像头检测
        if (feed_status == FEED_ING) { 
            if (first_feed_detect) {
                first_feed_detect = false;
            } else {
#if DEBUG_
                ESP_LOGI(TAG, "0. 使能解码标志位");
#endif
                set_decode_flag(true);
            }
            vTaskDelay(9 * 1000 / portTICK_PERIOD_MS); 
        }
        else {
            set_decode_flag(false);

            first_feed_detect = true;
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void feed_ruoshi_moudle_init(void)
{
    // 在初始化时确保系统时间与RTC同步
    sync_system_with_rtc_if_needed();
    
    // 初始化配置参数
    update_feed_duty();
    update_feed_time();
    update_color_time();
    update_camera_status();
    

    // 初始化GPIO和任务
    feed_gpio_init();
    color_light_gpio_init();
    Pwm_40hz_init();//20250611--LINJUN
    update_beep_status();//linjun

    // 创建任务
    xTaskCreate(feed_task, "feed_task", 4096 * 2, NULL, 9, NULL);
    xTaskCreate(running_light_task, "running_light_task", 4096, NULL, 10, NULL);
    xTaskCreate(camera_task, "camera_task", 4096, NULL, 11, &cameraTaskHandle);
    
    ESP_LOGI(TAG, "哺光和彩光模块初始化完成");
}
