#include "My_system.h"      // 系统头文件

#include "led_status.h"     // 指示灯头文件            
#include "storage.h"        // 存储头文件              
#include "ds1302.h"         // DS1302头文件            
#include "xl9535.h"         // 拓展IO芯片xl9535头文件  
#include "feed.h"           // 功能头文件       
#include "key.h"            // 按键头文件
#include "blufi.h"          // 蓝牙配网头文件   
#include "factory.h"        //
#include "clock.h"
#include "power_manager.h"
#include "voice.h" 
#include "camera.h"
#include "model.h"
#include "image_transfer.h"
#include "heating.h"
#include "imgfile_save.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "clock.h"

static const char *TAG = "INIT";
extern bool WELCOME_flag;//开机语音标志位   linjun

static void log_boot_reset_reason(void)
{
    esp_reset_reason_t reset_reason = esp_reset_reason();
    uint8_t reboot_reason = map_reset_reason_to_code(reset_reason);
    time_t reboot_time = get_safe_timestamp();
    save_device_logs_to_nvs(0, reboot_reason, 0, reboot_time);
    ESP_LOGI("INIT", "保存重启原因日志，编号:%u", reboot_reason);
}

void SystemInit(void)
{  
    gpio_reset_pin(LED_STATUS_BLUE_OUTPUT_IO);
    gpio_set_level(LED_STATUS_BLUE_OUTPUT_IO, 1);

    /* Flash初始化 - RTC初始化判断、SN码读写、离线功能运行*/
    record_nvs_module_init();

    /* 获取SNCode */ 
    get_SNCode(); 

    /* 初始化RTC引脚及时钟 - 获取RTC时间必须 */
    DS1302_init();

    /* 初始化DS1302时间、获取DS1302时间值 - MQTT数据同步、离线功能运行*/
    RTC_NVS_init();

    /* 记录重启原因（RTC同步后，避免时间为0） */
    log_boot_reset_reason();

    spiffs_init();
    
    /* 拓展IO芯片初始化 */
    Xl9535_Init();

    /* 音频功能初始化 */
    voice_init();

    /* 电源管理功能初始化 */        //语音初始化后电量会突然掉很多  linjun
    power_manager_init();

    /* BLUFI功能初始化（包含蓝牙配网以及wifi连接）*/
    blufi_init();

    /* 指示灯引脚初始化 */
    led_status_module_init();

    set_status_led_color(0, 0, 1, 1); // 开机自亮蓝灯

    /* 哺光、弱视引脚定义、功能函数初始化 - 按键中断必须*/
    feed_ruoshi_moudle_init();

    /* 加热功能初始化 */
    heating_init();

    /* 初始化串口输入SNCode的功能 */
    uart_init();

    // 初始化并启动摄像头
    camera_init();
    ESP_LOGI(TAG, "摄像头初始化");
    camera_start();
    ESP_LOGI(TAG, "开启摄像头");

    /* TinyML模型初始化 */
    setup();

    /* 初始化 UDP 服务器 */
    udp_client_init();

    func_device_parameters_init();

    /* 欢迎语音 */
    play_voice(WELCOME);

    /* 按键中断初始化 linjun*/
    if(WELCOME_flag == true){//播完开机语音后才可按键操作   linjun
        key_func_init();
    }
    
}