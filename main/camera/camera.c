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

static esp_err_t esp_jpeg_encoder_one_picture(uint8_t *input_buf, int input_len, uint8_t *output_buf, int output_len, int* output_size)
{
    esp_err_t ret = ESP_OK;

    jpeg_enc_info_t config = DEFAULT_JPEG_ENC_CONFIG();
    void* jpeg_enc = jpeg_enc_open(&config);
    if (jpeg_enc == NULL) {
        return ESP_FAIL;
    }

    // 开始编码JPEG原始数据到输出缓冲区
    ret = jpeg_enc_process(jpeg_enc, input_buf, input_len, output_buf, output_len, output_size);
    if (ret != JPEG_ERR_OK) {
        goto _cleanup;
    }else{
        printf("图像处理编码完成，编码完成后的大小是:%d \n", *output_size);
    }

_cleanup:
    // 释放编码器资源
    if (jpeg_enc) {
        jpeg_enc_close(jpeg_enc);
    }

    return ret == JPEG_ERR_OK ? ESP_OK : ESP_FAIL;
}

static int esp_jpeg_decoder_one_picture(uint8_t *input_buf, int len, uint8_t *output_buf)
{
#if DEBUG_
    // printf("len: %d\n", len);
#endif
    
    esp_err_t ret = ESP_OK;
    
    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG(); // 注意这里选用的是RGB888

    jpeg_dec_handle_t jpeg_dec = NULL;
    jpeg_dec = jpeg_dec_open(&config); // Create jpeg_dec
    
    jpeg_dec_io_t *jpeg_io = calloc(1, sizeof(jpeg_dec_io_t)); // Create io_callback handle
    if (jpeg_io == NULL) {
        return ESP_FAIL;
    }
    jpeg_dec_header_info_t *out_info = calloc(1, sizeof(jpeg_dec_header_info_t)); // Create out_info handle
    if (out_info == NULL) {
        return ESP_FAIL;
    }
    
    // Set input buffer and buffer len to io_callback
    jpeg_io->inbuf = input_buf;
    jpeg_io->inbuf_len = len;

    // 解析JPG图像头，并获取用户和解码器的图片
    ret = jpeg_dec_parse_header(jpeg_dec, jpeg_io, out_info);
    if (ret < 0) {
        goto _exit;
    }

    jpeg_io->outbuf = output_buf;
    int inbuf_consumed = jpeg_io->inbuf_len - jpeg_io->inbuf_remain;
    jpeg_io->inbuf = input_buf + inbuf_consumed;
    jpeg_io->inbuf_len = jpeg_io->inbuf_remain;

    // 开始解码jpg原始数据
    ret = jpeg_dec_process(jpeg_dec, jpeg_io);
    if (ret < 0) {
        goto _exit;
    }

_exit:
    // 解码器逆初始化
    jpeg_dec_close(jpeg_dec);
    free(out_info);
    free(jpeg_io);
    return ret;
}

static uint8_t bilinearInterpolate(uint8_t *image, int srcWidth, int srcHeight, double x, double y, int channel)
{
    int x0 = (int)x;
    int x1 = x0 + 1;
    int y0 = (int)y;
    int y1 = y0 + 1;

    // 防止越界
    x0 = x0 < 0 ? 0 : (x0 >= srcWidth ? srcWidth - 1 : x0);
    x1 = x1 < 0 ? 0 : (x1 >= srcWidth ? srcWidth - 1 : x1);
    y0 = y0 < 0 ? 0 : (y0 >= srcHeight ? srcHeight - 1 : y0);
    y1 = y1 < 0 ? 0 : (y1 >= srcHeight ? srcHeight - 1 : y1);

    double dx = x - x0;
    double dy = y - y0;

    int index00 = (y0 * srcWidth + x0) * 3 + channel;
    int index01 = (y0 * srcWidth + x1) * 3 + channel;
    int index10 = (y1 * srcWidth + x0) * 3 + channel;
    int index11 = (y1 * srcWidth + x1) * 3 + channel;

    uint8_t c00 = image[index00];
    uint8_t c01 = image[index01];
    uint8_t c10 = image[index10];
    uint8_t c11 = image[index11];

    uint8_t c0 = (uint8_t)((1 - dx) * c00 + dx * c01);
    uint8_t c1 = (uint8_t)((1 - dx) * c10 + dx * c11);

    return (uint8_t)((1 - dy) * c0 + dy * c1);
}

static void resizeImageBilinear(uint8_t * imgOriginal, uint8_t * imgResized)
{
    for (int y = 0; y < HEIGHT_NEW; y++) {
        for (int x = 0; x < WIDTH_NEW; x++) {
            double sx = (double)x / WIDTH_NEW * WIDTH_ORI;
            double sy = (double)y / HEIGHT_NEW * HEIGHT_ORI;

            int destIndex = (y * WIDTH_NEW + x) * 3;

            // 分别计算R、G、B三个通道的值
            imgResized[destIndex + 0] = bilinearInterpolate(imgOriginal, WIDTH_ORI, HEIGHT_ORI, sx, sy, 0); // R
            imgResized[destIndex + 1] = bilinearInterpolate(imgOriginal, WIDTH_ORI, HEIGHT_ORI, sx, sy, 1); // G
            imgResized[destIndex + 2] = bilinearInterpolate(imgOriginal, WIDTH_ORI, HEIGHT_ORI, sx, sy, 2); // B
        }
    }
}

static void camera_frame_cb(uvc_frame_t *frame, void *ptr)
{
    if(frame->frame_format == UVC_FRAME_FORMAT_MJPEG)
    {
        // 添加对模式的判断，只在红光模式下执行识别
        if(decode_flag && feed_set_status() == FEED_ING && camera_status)
        {
            set_decode_flag(false); // 直到下次触发的时候开放
            
            static void *jpeg_buffer = NULL;
            static void *jpeg_new_buffer = NULL;
            static void *new_jpeg = NULL;
            
            int init_len = 0;
            int *output_size = &init_len;

            // printf("----------------------------------------------SPIRAM: %zu----------------------------------------------\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

            if( heap_caps_get_free_size(MALLOC_CAP_SPIRAM) > WIDTH_ORI * HEIGHT_ORI * CHANNELS){ 
                size_t length  = frame->width * frame->height * CHANNELS;
                jpeg_buffer = (uint8_t *)heap_caps_aligned_alloc(16, length, MALLOC_CAP_SPIRAM); // 在SPIRAM留出足够的缓冲区
                
                if(jpeg_buffer != NULL){
                    esp_jpeg_decoder_one_picture((uint8_t *)frame->data, frame->data_bytes, jpeg_buffer); // 解析图像到缓冲区
#if DEBUG_
                    ESP_LOGI(TAG, "1. 解析图像到缓冲区完成");
#endif
                    
                    if( heap_caps_get_free_size(MALLOC_CAP_SPIRAM) > WIDTH_NEW*WIDTH_NEW*CHANNELS){         
                        size_t length  = WIDTH_NEW*WIDTH_NEW*CHANNELS;
                        jpeg_new_buffer = (uint8_t *)heap_caps_aligned_alloc(16, length, MALLOC_CAP_SPIRAM); 
                        if(jpeg_new_buffer != NULL){
                            // resizeImageRGB(jpeg_buffer, jpeg_new_buffer); // 图像resize到96*96*3
                            resizeImageBilinear(jpeg_buffer, jpeg_new_buffer);
#if DEBUG_
                            ESP_LOGI(TAG, "2. 图像缩放预处理完成");
#endif

                            // 执行图像压缩操作

                            new_jpeg = (uint8_t *)heap_caps_aligned_alloc(16, length/4, MALLOC_CAP_SPIRAM); 
                            esp_jpeg_encoder_one_picture((uint8_t *)jpeg_new_buffer, length, new_jpeg, length/4, output_size);

                            // new_jpeg = (uint8_t *)heap_caps_aligned_alloc(16, 320*240*3/8, MALLOC_CAP_SPIRAM); 
                            // esp_jpeg_encoder_one_picture((uint8_t *)jpeg_buffer, length, new_jpeg, 320*240*3/8, output_size);
                        }
                    }

                }else{
                    ESP_LOGE(TAG, "jpeg_buffer == NULL");
                }
            }else{
                ESP_LOGE(TAG, "SPIRAM剩余空间小于图像所需要的存储空间");
            }

            // 调用模型推理函数
#if DEBUG_
            ESP_LOGI(TAG, "3. 进入模型推理环节");
#endif
            // 让出一次调度，避免长时间占用CPU触发WDT
            vTaskDelay(1);
            float result = loop((uint8_t*)jpeg_new_buffer);    

            // 语音播报识别结果
            if(result == 0) {  // 闭眼
                play_voice(OPEN_EYES);
                
                // 先保存图片，只有保存成功才增加计数
                esp_err_t save_ret = save_closed_eye_image(new_jpeg, *output_size);
                if (save_ret == ESP_OK) {
                    s_rcg_ccount++;
#if DEBUG_
                    ESP_LOGI(TAG, "4. 模型推理结果为: %f, 识别结果为闭眼，图片保存成功 s_rcg_ccount = %d", result, s_rcg_ccount);
#endif
                } else {
#if DEBUG_
                    ESP_LOGW(TAG, "4. 模型推理结果为: %f, 识别结果为闭眼，但图片保存失败，不增加计数", result);
#endif
                }
                
            } else if(result == 1) {  // 睁眼
                s_rcg_ocount++;
#if DEBUG_
                ESP_LOGI(TAG, "4. 模型推理结果为: %f, 识别结果为睁眼 s_rcg_ocount = %d", result, s_rcg_ocount);
#endif
            } else if(result == 2) {  // 背景
                play_voice(WEAR_REMIND);//背景识别语音提醒--linjun
                s_rcg_bcount++;
#if DEBUG_
                ESP_LOGI(TAG, "4. 模型推理结果为: %f, 识别结果为背景 s_rcg_bcount = %d", result, s_rcg_bcount);
#endif
            } else {  // 错误情况
                ESP_LOGE(TAG, "4. 模型推理出错,返回值: %f", result);
            }

            // UDP 同步图像数据
            
            // if(update_udp_ip() != NULL){
            //     QueueMessage_t message;
            //     message.buffer = jpeg_new_buffer;
            //     message.result = result;
            //     xQueueSendFromISR(udp_evt_queue, &message, NULL); // UDP发送数据
            // }

            // 任务执行完毕后：处理动态空间内存、删除任务
            free(jpeg_buffer);
            free(jpeg_new_buffer);
            free(new_jpeg);

            // 避免野指针
            jpeg_buffer = NULL;                 
            jpeg_new_buffer = NULL;
            new_jpeg = NULL;
#if DEBUG_
            // printf("----------------------------------------------SPIRAM: %zu----------------------------------------------\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
            ESP_LOGI(TAG, "5. 清理分配的动态空间和指针");
#endif

            // 让出一次调度，避免连续处理帧导致WDT
            vTaskDelay(1);
        }
        vTaskDelay(1 * 1000 / portTICK_PERIOD_MS);
    } else {
        ESP_LOGW(TAG, "Format not supported");
        assert(0);
    }
}

void usb_connect_task(void *arg) {
    ESP_LOGI(TAG, "============================创建重连任务============================");
    while (true) {
        esp_err_t ret = usb_streaming_connect_wait(portMAX_DELAY);  // 10 秒
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "USB Device successfully connected");
            break;  // 连接成功后退出循环
        }
    }
    vTaskDelete(NULL);  // 任务结束后自删除
}

// UVC数据流状态改变调用该回调函数
static void stream_state_changed_cb(usb_stream_state_t event, void *arg)
{
    switch (event) {
        case STREAM_CONNECTED: {
            size_t frame_size = 0;
            size_t frame_index = 0;
            uvc_frame_size_list_get(NULL, &frame_size, &frame_index);
            if (frame_size) {
                ESP_LOGI(TAG, "UVC: get frame list size = %u, current = %u", frame_size, frame_index);
                uvc_frame_size_t *uvc_frame_list = (uvc_frame_size_t *)malloc(frame_size * sizeof(uvc_frame_size_t));
                uvc_frame_size_list_get(uvc_frame_list, NULL, NULL);
                for (size_t i = 0; i < frame_size; i++) {
                    ESP_LOGI(TAG, "\tframe[%u] = %ux%u", i, uvc_frame_list[i].width, uvc_frame_list[i].height);
                }
                free(uvc_frame_list);
            } else {
                ESP_LOGW(TAG, "UVC: get frame list size = %u", frame_size);
            }
            ESP_LOGI(TAG, "Device connected");
            break;
        }
        case STREAM_DISCONNECTED:
            ESP_LOGI(TAG, "Device disconnected");
            // xTaskCreate(usb_connect_task, "usb_connect_task", 4096, NULL, PRIORITY_USB_CONNECT_TASK, NULL);
            break;
        default:
            ESP_LOGE(TAG, "Unknown event");
            break;
    }
}

/**
 * @brief 获取识别总次数及闭眼次数
 * 
 */
void get_rcg_cnt(uint8_t* tcount, uint8_t* ccount, uint8_t* ecount)
{
    *tcount = s_rcg_ocount + s_rcg_ccount + s_rcg_bcount;
    *ccount = s_rcg_ccount;
    *ecount = s_rcg_bcount + s_rcg_ccount;
    ESP_LOGI(TAG, "get_rcg_cnt s_rcg_tcount = %d, s_rcg_ccount = %d, s_rcg_bcount = %d", s_rcg_ocount, s_rcg_ccount, s_rcg_bcount);
    s_rcg_ocount = 0;
    s_rcg_ccount = 0;
    s_rcg_bcount = 0;
    s_rcg_ecount = 0;
}

/**
 * @brief 设置解码标志位
 * 
 * - 只有在解码标志位被设置的情况下才会执行睁眼识别算法
 * 
 */
void set_decode_flag(bool flag)
{
    decode_flag = flag;
}

esp_err_t camera_init(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("httpd_txrx", ESP_LOG_INFO);

    esp_err_t ret = ESP_FAIL;
    s_evt_handle = xEventGroupCreate();
    if (s_evt_handle == NULL) {
        ESP_LOGE(TAG, "line-%u event group create failed", __LINE__);
        assert(0);
    }

    /* 创建缓冲区*/
    // malloc双缓冲区用于USB负载， xfer_buffer_size >= frame_buffer_size
    xfer_buffer_a = (uint8_t *)malloc(DEMO_UVC_XFER_BUFFER_SIZE); 
    assert(xfer_buffer_a != NULL);
    xfer_buffer_b = (uint8_t *)malloc(DEMO_UVC_XFER_BUFFER_SIZE);
    assert(xfer_buffer_b != NULL);
    // malloc帧缓冲区用于jpeg图像帧
    frame_buffer = (uint8_t *)malloc(DEMO_UVC_XFER_BUFFER_SIZE); 
    assert(frame_buffer != NULL);

    /* UVC配置 */
    uvc_config_t uvc_config = {
        .frame_width         = DEMO_UVC_FRAME_WIDTH,
        .frame_height        = DEMO_UVC_FRAME_HEIGHT,
        .frame_interval      = FPS2INTERVAL(5),
        .xfer_buffer_size    = DEMO_UVC_XFER_BUFFER_SIZE,
        .xfer_buffer_a       = xfer_buffer_a,
        .xfer_buffer_b       = xfer_buffer_b,
        .frame_buffer_size   = DEMO_UVC_XFER_BUFFER_SIZE,
        .frame_buffer        = frame_buffer,
        .frame_cb            = &camera_frame_cb,
        .frame_cb_arg        = NULL,
    };
    ret = uvc_streaming_config(&uvc_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uvc streaming config failed");
        return ret;
    }
    ESP_LOGI(TAG, "uvc streaming config success");

    // 注册状态回调函数用于获取连接或断连状态
    ret = usb_streaming_state_register(&stream_state_changed_cb, NULL);
    if (ret != ESP_OK) {
        return ret;
    }

    // 主动发起 USB 连接
    ret = usb_streaming_connect_wait(5000);  // 5 秒
    if (ret != ESP_OK) {
        return ret;
    }

    return ESP_OK;
}

esp_err_t camera_start(void) 
{
    // 启动 USB 流式传输
    esp_err_t ret = usb_streaming_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "USB streaming start failed");
        return ret;
    }
    return ESP_OK;
}

void camera_stop(void)
{
    ESP_LOGI(TAG, "Stopping camera stream");
    
    // 先暂停数据流
    usb_streaming_control(STREAM_UVC, CTRL_SUSPEND, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // 停止数据流，但不使用 ESP_ERROR_CHECK
    esp_err_t ret = usb_streaming_stop();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "USB Streaming stop failed: %s", esp_err_to_name(ret));
    }
    
    vTaskDelay(pdMS_TO_TICKS(200));

    // 添加指针检查，避免重复释放
    if (xfer_buffer_a != NULL) {
        free(xfer_buffer_a);
        xfer_buffer_a = NULL;
    }
    
    if (xfer_buffer_b != NULL) {
        free(xfer_buffer_b);
        xfer_buffer_b = NULL;
    }
    
    if (frame_buffer != NULL) {
        free(frame_buffer);
        frame_buffer = NULL;
    }
}

bool update_camera_status()
{
    camera_status = get_device_para(DEVICE_PARA_CAMERA);

    return camera_status;
}
