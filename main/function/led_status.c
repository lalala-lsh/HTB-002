/*
*文件说明：指示灯(状态指示灯、功能指示灯)模块代码
*/
#include "led_status.h"

static void led_gpio_init(void)
{
    //LED指示灯初始化   linjun
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_FUNC_RED_OUTPUT_IO) | (1ULL << LED_FUNC_GREEN_OUTPUT_IO) | 
                            (1ULL << LED_FUNC_BLUE_OUTPUT_IO)| (1ULL << LED_STATUS_RED_OUTPUT_IO)| 
                            (1ULL << LED_STATUS_GREEN_OUTPUT_IO)| (1ULL << LED_STATUS_BLUE_OUTPUT_IO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    
    gpio_config(&io_conf);

}

//功能指示灯控制
static void set_func_led_color(uint8_t red, uint8_t green, uint8_t orange) 
{
    //红灯  linjun
    gpio_set_level(LED_FUNC_RED_OUTPUT_IO, red);
    //绿灯  linjun
    gpio_set_level(LED_FUNC_GREEN_OUTPUT_IO, green);
    //蓝灯  linjun
    gpio_set_level(LED_FUNC_BLUE_OUTPUT_IO, orange);
}

// 状态指示灯相关控制   linjun
void set_status_led_color(uint8_t red, uint8_t green, uint8_t blue, uint8_t onoff)
{
    if (onoff == 1){
        gpio_set_level(LED_STATUS_RED_OUTPUT_IO, red);
        gpio_set_level(LED_STATUS_GREEN_OUTPUT_IO, green);
        gpio_set_level(LED_STATUS_BLUE_OUTPUT_IO, blue);
    } else{
        gpio_set_level(LED_STATUS_RED_OUTPUT_IO, 0);
        gpio_set_level(LED_STATUS_GREEN_OUTPUT_IO, 0);
        gpio_set_level(LED_STATUS_BLUE_OUTPUT_IO, 0);
    }
}

void set_func_led(KEY_FUNC_E key, uint32_t onoff)
{
    switch(key) {
        case KEY_FUNC_MODE_1:
            if (onoff == FUNC_ON) {
                set_func_led_color(1, 1, 0); // 点亮黄色
            } else {
                set_func_led_color(0, 0, 0); //关闭指示灯
            }
            break;
        case KEY_FUNC_MODE_2:
            if (onoff == FUNC_ON) {
                set_func_led_color(1, 0, 1); // 点亮紫色
            } else {
                set_func_led_color(0, 0, 0); //关闭指示灯
            }
            break;
        case KEY_FUNC_HEAT:
            if (onoff == FUNC_ON) {
                set_func_led_color(1, 0, 0); // 点亮红色
            } else {
                set_func_led_color(0, 0, 0); //关闭指示灯
            }
            break;  
        case KEY_FUNC_OTA_ING:  // OTA 状态指示灯
            if (onoff == FUNC_ON) {
                set_func_led_color(0, 0, 1); 
            } else {
                set_func_led_color(0, 0, 0); //关闭指示灯
            }
            break;
        case KEY_FUNC_OFF:
            set_func_led_color(0, 0, 0); //关闭指示灯
            break;
        default:
            break;
    }
}

void led_status_module_init(void)
{
    led_gpio_init();
    set_func_led(KEY_FUNC_OFF, FUNC_OFF);    
}

