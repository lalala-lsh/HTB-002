#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include <time.h>
#include <dirent.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_log.h"

#include "usb_stream.h"
#include "esp_jpeg_dec.h"
#include "esp_jpeg_enc.h"
#include "image_transfer.h"
#include "voice.h"
#include "imgfile_save.h"
#include "feed.h"
#include "key.h"
#include "camera.h"

#include "model.h"

#include "storage.h"

#include "esp_task_wdt.h"

#define ENABLE_UVC_FRAME_RESOLUTION_ANY   1        /* 从相机获取的任何分辨率 */

#if (ENABLE_UVC_FRAME_RESOLUTION_ANY)
    #define DEMO_UVC_FRAME_WIDTH        FRAME_RESOLUTION_ANY
    #define DEMO_UVC_FRAME_HEIGHT       FRAME_RESOLUTION_ANY
#else
    #define DEMO_UVC_FRAME_WIDTH        320
    #define DEMO_UVC_FRAME_HEIGHT       240
#endif

#define WIDTH_ORI   320
#define HEIGHT_ORI  240
#define CHANNELS    3
#define WIDTH_NEW   96
#define HEIGHT_NEW  96

#define DEMO_UVC_XFER_BUFFER_SIZE (55 * 1024)

#define test_print_flag      1

static const char *TAG = "UVC_CAMERA";
static EventGroupHandle_t s_evt_handle;

bool decode_flag = false;
// bool decode_flag = true;
const char Camera_Nvs_Flag = 0;

extern QueueHandle_t udp_evt_queue;
extern QueueHandle_t voice_evt_queue;

bool CAMERA_MODE_FLAG = true;

static uint8_t s_rcg_ocount = 0;//睁眼次数
static uint8_t s_rcg_ccount = 0;//闭眼次数
static uint8_t s_rcg_bcount = 0;//背景次数
static uint8_t s_rcg_ecount = 0;//背景次数


// 声明全局变量
static uint8_t *xfer_buffer_a = NULL;
static uint8_t *xfer_buffer_b = NULL;
static uint8_t *frame_buffer = NULL;

static bool camera_status = true;







// UVC数据流状态改变调用该回调函数

/**
 * @brief 获取识别总次数及闭眼次数
 * 
 */

/**
 * @brief 设置解码标志位
 * 
 * - 只有在解码标志位被设置的情况下才会执行睁眼识别算法
 * 
 */




