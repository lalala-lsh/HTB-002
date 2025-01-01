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


