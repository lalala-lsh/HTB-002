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
static void Pwm_40hz_init(void)
{
    // 配置PWM定时器
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = BEEP_PWM_MODE,
        .timer_num        = BEEP_PWM_TIMER,
        .duty_resolution  = BEEP_PWM_DUTY_RES,
        .freq_hz          = BEEP_PWM_FREQUENCY,  // 设置输出频率为40Hz
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // 配置蜂鸣器通道
    ledc_channel_config_t beep_ledc_channel = {
        .speed_mode     = BEEP_PWM_MODE,
        .channel        = BEEP_PWM_CHANNEL,
        .timer_sel      = BEEP_PWM_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = BEEP_PWM_OUTPUT_IO,
        .duty           = 0, // 初始占空比为0%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&beep_ledc_channel));

}

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
void dal_40hz_beep_control_switch(uint8_t onoff)
{
    switch(onoff)
    {
        case 0:
            // turn off light
            ESP_ERROR_CHECK(ledc_set_duty(BEEP_PWM_MODE, BEEP_PWM_CHANNEL, 0));
            ESP_ERROR_CHECK(ledc_update_duty(BEEP_PWM_MODE, BEEP_PWM_CHANNEL));
            break;
        case 1:
            // turn on light    10%
            if(beep_status==1)
            {
                ESP_ERROR_CHECK(ledc_set_duty(BEEP_PWM_MODE, BEEP_PWM_CHANNEL, 410));
                ESP_ERROR_CHECK(ledc_update_duty(BEEP_PWM_MODE, BEEP_PWM_CHANNEL));
            }else{
                ESP_ERROR_CHECK(ledc_set_duty(BEEP_PWM_MODE, BEEP_PWM_CHANNEL, 0));
                ESP_ERROR_CHECK(ledc_update_duty(BEEP_PWM_MODE, BEEP_PWM_CHANNEL));
            }
            
            break;
        default:
            break;
    }
}

/*
哺光功能控制
开启: 开启红光功能
关闭: 关闭红光PWM输出
*/
void feed_func_set(uint32_t onoff)
{
    if (onoff) {
        // 开始训练并初始化
        start_feed_training();
    } else {
        // 停止训练并收尾
        stop_feed_training();
    }
}

// 开始哺光训练
static void start_feed_training(void)
{
    // 检查电量是否足够开启红光功能
    if(!can_use_red_light_function()) {
        play_voice(DISABLE_HEAT);  // 复用低电量语音提示
        ESP_LOGI("FEED", "电量不足，禁用红光功能");
        return;
    }
    
    // 生成RCG_CODE并保存到全局变量
    current_rcg_code = generate_rcg_code();
    
    // 开始训练时初始化图片存储并传入RCG_CODE
    start_training_session_with_code(current_rcg_code);
    
    if (feed_status != FEED_ING) { 
        // 关键修改：训练开始前强制验证时间有效性
        ESP_LOGI(TAG, "训练开始前检查时间有效性...");
        
        // if (!is_time_valid()) {
        //     ESP_LOGW(TAG, "时间未同步，训练无法开始！等待时间同步...");
            
        //     // 强制等待时间同步，最多等待30秒
        //     if (!wait_for_time_sync(30)) {
        //         ESP_LOGE(TAG, "时间同步失败，训练被中止");
        //         return; // 直接返回，不开始训练
        //     }
            
        //     ESP_LOGI(TAG, "时间同步成功，可以开始训练");
        // }
        
        feed_status = FEED_ING;
        feed_timecnt = 0;
        // 重置30秒计时器和扣减标志
        feed_30s_counter = 0;
        count_deducted = false;
        // 重置加热状态，后面会根据实际情况设置
        training_heat_status = 0;
        
        // // 使用安全的时间戳函数，确保时间有效
        // feed_st = get_safe_timestamp();
        
        // // 验证获取的时间戳是否有效
        // if (feed_st == 0) {
        //     ESP_LOGE(TAG, "无法获取有效时间戳，训练被中止");
        //     feed_status = FEED_NONE; // 恢复状态
        //     return;
        // }

        // 尝试获取开始时间戳，失败不阻塞功能
        time_t start_ts = get_safe_timestamp();
        feed_st = (int)start_ts;
        feed_start_mono_us = esp_timer_get_time();
        update_time_anchor_if_valid(start_ts);
        
        ESP_LOGI(TAG, "开始哺光训练，start_ts: %ld, mono_us: %lld, RCG_CODE: %ld", (long)feed_st,
                 (long long)feed_start_mono_us, current_rcg_code);
    }
    
    // 初始化电源和红光控制
    Xl9535_Set_Io_Status(PIN_P06, IO_LOW);  // 彩光使能脚拉低
    drv_feed_powerIO_init(SOC_GPIO_OUTPUT_ON); // 红光电源引脚使能
    uint32_t duty = FEED_DUTY;
#if HAVE_HEATING_MODE_KEY
    // 检查feeding_heat参数，决定是否在红光模式下开启加热
    int feeding_heat = get_device_para(DEVICE_PARA_FEEDING_HEAT);
    if (feeding_heat == 1) {
        // feeding_heat=1时，根据电量决定是否开启加热
        #if LOW_POWER_TURN_OFF_HEAT
        if(can_use_heating_function()) {
            // 电量足够，开启加热
            heating_control(FUNC_ON);
            ESP_LOGI("FEED", "feeding_heat=1, 电量足够，加热功能正常工作");
        } else {
            play_voice(DISABLE_HEAT);
            heating_control(FUNC_OFF);
            ESP_LOGI("FEED", "feeding_heat=1, 电量不足，禁用加热功能");
        }
        #else
        heating_control(FUNC_ON);
        ESP_LOGI("FEED", "feeding_heat=1, 开启加热");
        #endif
    } else {
        // feeding_heat=0时，红光模式下不开启加热
        heating_control(FUNC_OFF);
        ESP_LOGI("FEED", "feeding_heat=0, 红光模式下不开启加热");
    }
#endif

    // 记录加热状态（在加热控制之后）
    training_heat_status = heat_set_status();

    //开启蜂鸣器    LINJUN
    dal_40hz_beep_control_switch(1);

    // 设置PWM占空比
    ESP_ERROR_CHECK(ledc_set_duty(FEED_PWM_MODE, FEEDL_PWM_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_set_duty(FEED_PWM_MODE, FEEDR_PWM_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(FEED_PWM_MODE, FEEDL_PWM_CHANNEL));
    ESP_ERROR_CHECK(ledc_update_duty(FEED_PWM_MODE, FEEDR_PWM_CHANNEL));
    
#if EN_RED_SPARK
    s_red_status_cnt = 0;
    s_red_status = RED_HIGH;
#endif
}

// 停止哺光训练
static void stop_feed_training(void)
{
    // 重置RCG_CODE
    current_rcg_code = 0;
    ESP_LOGI(TAG, "关闭红光");

    //关闭蜂鸣器    LINJUN
    dal_40hz_beep_control_switch(0);
    
    // 关闭加热和状态复位
    heating_control(FUNC_OFF);
    feed_timecnt = 0;
    feed_status = FEED_NONE;
    feed_start_mono_us = 0;
    
    // 重置租赁模式防误触标志
    count_deducted = false;
    feed_30s_counter = 0;
    
    // 平滑关闭红光
    ESP_ERROR_CHECK(ledc_set_fade_with_time(FEED_PWM_MODE, FEEDL_PWM_CHANNEL, 0, 1000));
    ESP_ERROR_CHECK(ledc_set_fade_with_time(FEED_PWM_MODE, FEEDR_PWM_CHANNEL, 0, 1000));
    ESP_ERROR_CHECK(ledc_fade_start(FEED_PWM_MODE, FEEDL_PWM_CHANNEL, LEDC_FADE_NO_WAIT));
    ESP_ERROR_CHECK(ledc_fade_start(FEED_PWM_MODE, FEEDR_PWM_CHANNEL, LEDC_FADE_NO_WAIT));
    drv_feed_powerIO_init(SOC_GPIO_OUTPUT_ON); // 红光电源引脚关闭
    
    // 停止摄像头
    // camera_stop();
    // ESP_LOGI(TAG, "关闭摄像头");
    
    vTaskDelay(pdMS_TO_TICKS(200));
#if EN_RED_SPARK
    s_red_status_cnt = 0;
    s_red_status = RED_OFF;
#endif
}

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
static void stop_beici_training(void)
{
    //关闭蜂鸣器    LINJUN
    dal_40hz_beep_control_switch(0);
    set_func_led(KEY_FUNC_OFF, FUNC_OFF);
    feed_status = FEED_NONE;
    beici_start_mono_us = 0;
    stop_running_light();
}

// 关闭当前运行的功能
static void stop_running_light() 
{
    current_mode = MODE_COUNT - 1;
    color_light_status = COLOR_LIGHT_NONE;
    
    // 关闭所有LED灯
    static const snPinName_t all_pins[] = {
        PIN_P00, PIN_P14, PIN_P05, PIN_P12, PIN_P04, PIN_P13, 
        PIN_P03, PIN_P17, PIN_P02, PIN_P16, PIN_P01, PIN_P15
    };
    
    for (int i = 0; i < sizeof(all_pins)/sizeof(all_pins[0]); i++) {
        Xl9535_Set_Io_Status(all_pins[i], IO_HIGH);
    }
    
    // 关闭所有颜色
    drv_feed_powerIO_init(SOC_GPIO_OUTPUT_ON); // 关闭红色
    Xl9535_Set_Io_Status(PIN_P06, IO_LOW);     // 关闭绿色
    current_color = COLOR_RED;                 // 重置颜色状态
}

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
            
            // 租赁模式防误触逻辑：红光开启30秒后扣减次数
            if (!count_deducted && feed_timecnt >= 30) {
                int count_limit = get_count_limit();
                if (count_limit > 0) {
                    count_limit--;
                    set_count_limit(count_limit);
                    count_deducted = true;  // 标记已经扣减过次数
                    ESP_LOGI(TAG, "红光模式运行30秒后，扣减使用次数，剩余: %d", count_limit);
                }
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
static void handle_red_light_breathing(int *duty) {
    if (s_red_status == RED_HIGH) {
        if(++s_red_status_cnt >= 2) {
            ESP_LOGI(TAG, "从高降到低, feed_timecnt = %ld, s_red_status_cnt = %d", feed_timecnt, s_red_status_cnt);
            s_red_status = RED_LOW;
            s_red_status_cnt = 0;
            //从高降到低
            *duty = ((FEED_DUTY_MAX) * 20) / 100;
            ESP_ERROR_CHECK(ledc_set_fade_with_time(FEEDL_PWM_MODE, FEEDL_PWM_CHANNEL, *duty, 2000));
            ESP_ERROR_CHECK(ledc_set_fade_with_time(FEEDR_PWM_MODE, FEEDR_PWM_CHANNEL, *duty, 2000));
            ESP_ERROR_CHECK(ledc_fade_start(FEEDL_PWM_MODE, FEEDL_PWM_CHANNEL, LEDC_FADE_NO_WAIT));
            ESP_ERROR_CHECK(ledc_fade_start(FEEDR_PWM_MODE, FEEDR_PWM_CHANNEL, LEDC_FADE_NO_WAIT));
            ESP_LOGI(TAG, "feed_status == FEED_ING, feed_timecnt = %ld", feed_timecnt);
        }
    } else if (s_red_status == RED_LOW) {
        if(++s_red_status_cnt >= 3) {
            ESP_LOGI(TAG, "从低升到高, feed_timecnt = %ld, s_red_status_cnt = %d", feed_timecnt, s_red_status_cnt);
            s_red_status = RED_RISING;
            s_red_status_cnt = 0;
            //从低升到高
            *duty = FEED_DUTY;
            ESP_ERROR_CHECK(ledc_set_fade_with_time(FEEDL_PWM_MODE, FEEDL_PWM_CHANNEL, *duty, 3000));
            ESP_ERROR_CHECK(ledc_set_fade_with_time(FEEDR_PWM_MODE, FEEDR_PWM_CHANNEL, *duty, 3000));
            ESP_ERROR_CHECK(ledc_fade_start(FEEDL_PWM_MODE, FEEDL_PWM_CHANNEL, LEDC_FADE_NO_WAIT));
            ESP_ERROR_CHECK(ledc_fade_start(FEEDR_PWM_MODE, FEEDR_PWM_CHANNEL, LEDC_FADE_NO_WAIT));
            ESP_LOGI(TAG, "feed_status == FEED_ING, feed_timecnt = %ld", feed_timecnt);
        }
    } else if (s_red_status == RED_RISING) {
        if(++s_red_status_cnt >= 2) {
            ESP_LOGI(TAG, "维持高, feed_timecnt = %ld, s_red_status_cnt = %d", feed_timecnt, s_red_status_cnt);
            s_red_status = RED_HIGH;
            s_red_status_cnt = 0;
            //维持高
        }
    }
}
#endif

// 初始化彩光模式
static void initialize_beici_mode(void) {
    current_mode = 0; // 灯珠切换自动切换任务启动
    beici_first = 1;  // 用于首次计时
    
    // build_changemode(get_mqtt_client(), 4, NULL);
    ESP_LOGI("RUOSHI", "彩光模式开始，保留开始时间戳:%ld", (long)beici_st);
}

// 处理哺光完成
static void handle_feed_completion(uint8_t *tcount, uint8_t *ccount, uint8_t *ecount) {
    // 1. 用 monotonic 计算真实运行时长（绝对可信）
    int64_t end_mono_us = esp_timer_get_time();
    uint32_t work_time_sec = feed_timecnt;
    if (feed_start_mono_us > 0 && end_mono_us > feed_start_mono_us) {
        work_time_sec = (uint32_t)((end_mono_us - feed_start_mono_us) / 1000000);
    }

    // 自动到时完成时，工作时长按配置值上报，避免任务调度抖动导致180秒上报成183秒
    if (work_time_sec > (uint32_t)feed_timecontrol) {
        ESP_LOGW(TAG, "自动完成时长修正: measured=%lu, config=%d",
                 (unsigned long)work_time_sec, feed_timecontrol);
        work_time_sec = (uint32_t)feed_timecontrol;
    }

    // 2. 解算 start/end 时间戳（优先SNTP，反推start）
    time_t resolved_start = (time_t)feed_st;
    time_t resolved_end = 0;
    resolve_training_timestamps(&resolved_start, &resolved_end, work_time_sec);
    feed_st = (int)resolved_start;
    feed_et = (int)resolved_end;

    int level = key_get_status();
    
    // 3. 用解算后的 end_time 获取日期（不再依赖RTC读数）
    time_t ts_for_day = (resolved_end > 0) ? resolved_end : time(NULL);
    struct tm timeinfo;
    localtime_r(&ts_for_day, &timeinfo);
    
    get_rcg_cnt(tcount, ccount, ecount);
    ESP_LOGI(TAG, "FEED finish! day=%d, work=%lu, start=%ld, end=%ld, tcount=%d, ccount=%d, ecount=%d",
             timeinfo.tm_mday, (unsigned long)work_time_sec, (long)feed_st, (long)feed_et, *tcount, *ccount, *ecount);
    
    // 网络上报或本地存储，传入当前训练的RCG_CODE
    report_or_store_data(level, feed_st, feed_et, work_time_sec, timeinfo.tm_mday, *tcount, *ccount, *ecount, RECORD_FD, current_rcg_code);
    *tcount = 0;
    *ccount = 0;
    
    feed_func_set(FUNC_OFF);            // 停止哺光
    key_set_status(KEY_FUNC_OFF);       // 按键状态恢复
    set_func_led(KEY_FUNC_OFF, FUNC_OFF); // 关闭状态指示灯

    xQueueReset(voice_evt_queue);       // 清空队列，防止残留命令（如WEAR_REMIND）干扰结束语音
    play_voice(FEEDTRAIN_END);
    
    ESP_LOGI(TAG, "FEEDING NORMAL END");
}

// 处理彩光完成
static void handle_beici_completion(uint8_t *tcount, uint8_t *ccount, uint8_t *bcount) {
    // 1. 用 monotonic 计算真实运行时长（绝对可信）
    int64_t end_mono_us = esp_timer_get_time();
    uint32_t work_time_sec = beici_timecnt;
    if (beici_start_mono_us > 0 && end_mono_us > beici_start_mono_us) {
        work_time_sec = (uint32_t)((end_mono_us - beici_start_mono_us) / 1000000);
    }

    // 自动到时完成时，工作时长按配置值上报，避免任务调度抖动导致上报偏大
    if (work_time_sec > (uint32_t)color_timecontrol) {
        ESP_LOGW(TAG, "自动完成时长修正: measured=%lu, config=%d",
                 (unsigned long)work_time_sec, color_timecontrol);
        work_time_sec = (uint32_t)color_timecontrol;
    }

    // 2. 解算 start/end 时间戳（优先SNTP，反推start）
    time_t resolved_start = (time_t)beici_st;
    time_t resolved_end = 0;
    resolve_training_timestamps(&resolved_start, &resolved_end, work_time_sec);
    beici_st = (int)resolved_start;
    beici_et = (int)resolved_end;
    
    int level = key_get_status();
    
    // 3. 用解算后的 end_time 获取日期
    time_t ts_for_day = (resolved_end > 0) ? resolved_end : time(NULL);
    struct tm timeinfo;
    localtime_r(&ts_for_day, &timeinfo);
    
    // 彩光模式没有睁闭眼检测，所有计数应为0
    *tcount = 0;
    *ccount = 0;
    *bcount = 0;
    ESP_LOGI(TAG, "RUOSHI finish!!---timeinfo.tm_mday = %d, tcount = %d, ccount = %d", timeinfo.tm_mday, *tcount, *ccount);

    // 网络上报或本地存储，彩光训练不需要RCG_CODE
    report_or_store_data(level, beici_st, beici_et, work_time_sec, timeinfo.tm_mday, *tcount, *ccount, *bcount, RECORD_RS, 0);
    
    beici_first = 0;

    xQueueReset(voice_evt_queue);
    play_voice(COLORTRAIN_END);

    beici_func_set(FUNC_OFF);      // 停止训练
    key_set_status(KEY_FUNC_OFF);  // 按键状态恢复
}

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

static void running_light_task(void* arg) 
{
    // 预定义所有灯光模式的PIN配置
    typedef struct {
        const snPinName_t* pins;
        size_t length;
    } PinGroup;

    // 定义灯光模式的PIN配置
    static const snPinName_t pin_sequence_mode_0[] = {PIN_P05, PIN_P04, PIN_P03, PIN_P02, PIN_P01, PIN_P00, PIN_P14, PIN_P12, PIN_P13, PIN_P17, PIN_P16, PIN_P15, PIN_P14, PIN_P00};
    static const snPinName_t pin_sequence_mode_1[] = {PIN_P01, PIN_P02, PIN_P03, PIN_P04, PIN_P05, PIN_P00, PIN_P14, PIN_P15, PIN_P16, PIN_P17, PIN_P13, PIN_P12, PIN_P14, PIN_P00};
    static const snPinName_t step1[] = {PIN_P00, PIN_P14};
    static const snPinName_t step2[] = {PIN_P01, PIN_P05, PIN_P15, PIN_P12};
    static const snPinName_t step3[] = {PIN_P02, PIN_P04, PIN_P16, PIN_P13};
    static const snPinName_t step4[] = {PIN_P03, PIN_P17};
    static const snPinName_t steps[][2] = {
        {PIN_P00, PIN_P14}, {PIN_P05, PIN_P12}, {PIN_P04, PIN_P13},
        {PIN_P03, PIN_P17}, {PIN_P02, PIN_P16}, {PIN_P01, PIN_P15}
    };
    static const snPinName_t all_pins[] = {PIN_P00, PIN_P14, PIN_P05, PIN_P12, PIN_P04, PIN_P13, 
                                         PIN_P03, PIN_P17, PIN_P02, PIN_P16, PIN_P01, PIN_P15};

    // 预定义步骤组，用于模式3
    static const PinGroup step_groups[] = {
        {step1, sizeof(step1)/sizeof(step1[0])},
        {step2, sizeof(step2)/sizeof(step2[0])},
        {step3, sizeof(step3)/sizeof(step3[0])},
        {step4, sizeof(step4)/sizeof(step4[0])}
    };

    // 定义延迟常量
    const int SEQUENCE_DELAY = 200;
    const int ALL_FLASH_DELAY = 1000;

    for(;;) {
        if (feed_status == BEICI_ING) {
            // 根据当前模式执行相应的灯光效果
            switch (current_mode) {
                case BEICI_MODE_1:  // 模式：顺序点亮
                    for (int i = 0; i < sizeof(pin_sequence_mode_0)/sizeof(pin_sequence_mode_0[0]); i++) {
                        flash_pins(&pin_sequence_mode_0[i], 1, SEQUENCE_DELAY);
                    }                    
                    switch_led_color();                    
                    break;

                case BEICI_MODE_2:  // 模式：反向顺序点亮
                    for (int i = 0; i < sizeof(pin_sequence_mode_1)/sizeof(pin_sequence_mode_1[0]); i++) {
                        flash_pins(&pin_sequence_mode_1[i], 1, SEQUENCE_DELAY);
                    }
                    switch_led_color();
                    break;

                case BEICI_MODE_3:  // 模式：分组闪烁
                    for (int i = 0; i < sizeof(step_groups)/sizeof(step_groups[0]); i++) {
                        flash_pins(step_groups[i].pins, step_groups[i].length, SEQUENCE_DELAY);
                    }
                    switch_led_color();
                    break;

                case BEICI_MODE_4:  // 模式：对称闪烁
                    for (int i = 0; i < sizeof(steps)/sizeof(steps[0]); i++) {
                        flash_pins(steps[i], 2, SEQUENCE_DELAY);
                    }
                    switch_led_color();
                    break;

                case BEICI_MODE_5:  // 模式：全体闪烁
                    flash_pins(all_pins, sizeof(all_pins)/sizeof(all_pins[0]), ALL_FLASH_DELAY);
                    switch_led_color();
                    break;

                default:
                    vTaskDelay(100 / portTICK_PERIOD_MS);
                    break;
            }
            
            // 检查是否需要切换到下一个模式
            if (beici_timecnt >= ((current_mode + 1) * s_beici_time_everymode) && current_mode < BEICI_MODE_MAX - 1) {
                current_mode++;
                ESP_LOGI(TAG, "彩光模式切换至 = %d", current_mode);
            }
        }       
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}
/** 
 * \brief 更新哺光档位占空比
 **/
void update_feed_duty(void){
    int feed_p;

    feed_p = get_device_para(DEVICE_PARA_FEED_P);

    FEED_DUTY = ((FEED_DUTY_MAX) * feed_p) / 100;
}

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
void update_feeding_heat(void)
{
#if HAVE_HEATING_MODE_KEY
    // 检查当前是否在红光模式下运行
    uint8_t current_feed_status = feed_set_status();
    
    if (current_feed_status == FEED_ING) {
        // 当前正在红光模式下运行，立即应用新的feeding_heat参数
        int feeding_heat = get_device_para(DEVICE_PARA_FEEDING_HEAT);
        
        if (feeding_heat == 1) {
            // feeding_heat=1时，根据电量决定是否开启加热
            #if LOW_POWER_TURN_OFF_HEAT
            if(can_use_heating_function()) {
                // 电量足够，开启加热
                heating_control(FUNC_ON);
                ESP_LOGI("FEED", "feeding_heat参数更新为1，当前红光模式运行中，开启加热");
            } else {
                play_voice(DISABLE_HEAT);
                heating_control(FUNC_OFF);
                ESP_LOGI("FEED", "feeding_heat参数更新为1，但电量不足，禁用加热功能");
            }
            #else
            heating_control(FUNC_ON);
            ESP_LOGI("FEED", "feeding_heat参数更新为1，当前红光模式运行中，开启加热");
            #endif
        } else {
            // feeding_heat=0时，关闭加热
            heating_control(FUNC_OFF);
            ESP_LOGI("FEED", "feeding_heat参数更新为0，当前红光模式运行中，关闭加热");
        }
    } else {
        // 当前不在红光模式下运行，无需操作
        ESP_LOGI("FEED", "feeding_heat参数已更新，当前不在红光模式下运行，无需立即生效");
    }
#endif
}
/** 
 * \brief 更新彩光时间  linjun
 **/
void update_color_time(void)
{
    int color_t = 0;

    color_t = get_device_para(DEVICE_PARA_RUOSHI_T);
    color_timecontrol = color_t * 60;
    s_beici_time_everymode = color_timecontrol / BEICI_MODE_MAX;
}

/** 
 * \brief 更新蜂鸣器状态  linjun
 **/
bool update_beep_status()
{
    beep_status = get_device_para(DEVICE_PARA_40HZ);
    if(beep_status == 1 && (key_get_status() == KEY_FUNC_MODE_1 || key_get_status() == KEY_FUNC_MODE_2 || key_get_status() == KEY_FUNC_HEAT)) 
    {
        dal_40hz_beep_control_switch(1);
    }else{
        dal_40hz_beep_control_switch(0);
    }

    return beep_status;
}

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
