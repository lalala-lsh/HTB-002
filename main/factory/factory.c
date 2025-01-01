#include "factory.h"

#include "storage.h"
#include "protocal.h"
#include "blufi.h"

#include <string.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"

#define BUF_SIZE    1024
static bool input_flag = false;
extern uint8_t user_data[24];

// SNCode写入许可
const uint8_t set_allow[5]           = { 0x7E, 0x81, 0x01, 0x01, 0x7F };
// SNCode写入失败
const uint8_t set_SNCode_fail[5]     = { 0x7E, 0x83, 0x01, 0x00, 0x7F };
// SNCode写入成功
const uint8_t set_SNCode_success[5]  = { 0x7E, 0x83, 0x01, 0x01, 0x7F };
// SNCode写入无许可
const uint8_t set_SNCode_noAllow[5]  = { 0x7E, 0x83, 0x01, 0x02, 0x7F };

/*
SN Code: SN/001/0AA25/AA/0001
SN Code Hex: 0x53 0x4E/ 0x30 0x30 0x31/ 0x00 0x41 0x41 0x32 0x35/ 0x41 0x41/ 0x30 0x30 0x30 0x31
SN Code Serial Buffer:
 0    1  2    3  4    5  6  7    8  9  10 11 12   13 14   15 16 17 18   19
7E / 03 16 / 53 4E / 30 30 31 / 30 41 41 32 35 / 41 41 / 30 30 30 31 / 7F
*/

static void SNCode_recieve(void *arg)
{
    uint8_t subbuffer[256];
    int len = 0;

    while(1) {
        // 从串口读取数据
        len = uart_read_bytes(UART_NUM_0, subbuffer, sizeof(subbuffer), 10 / portTICK_PERIOD_MS);
        if(len > 0) {
            # if TEST_PRINT
            printf("---------------------------------------------\n");
            printf("rx_task, len = %d\n", len);
            for (int i = 0; i < len; i++) {
                printf("Data[%d] in hexadecimal format: 0x%02X\n", i, subbuffer[i]);
            }
            printf("---------------------------------------------\n");
            # endif

            if(subbuffer[0] == 0x7E && subbuffer[subbuffer[2]+3] == 0x7F) {
                switch(subbuffer[1])
                {
                    case 0x01:
                        if(subbuffer[3] == 0x00)
                        {
                            esp_log_level_set("*", ESP_LOG_NONE);  // 关闭日志打印
                            input_flag = true;
                            uart_write_bytes(UART_NUM_0, &set_allow, 5); // 返回串口数据
                        }
                        break;
                    
                    case 0x02:
                        uint8_t get_SNCode_head[5]  = { 0x7E, 0x82, 0x11, 0x01 };
                        uint8_t get_SNCode_tail[2]  = { 0x7F };
                        uint8_t get_SNCode_success[35];
                       
                        if(subbuffer[3] == 0x00)
                        {
                            for(int i=0; i<20; i++)
                            {
                                if(i<4){
                                    get_SNCode_success[i] = get_SNCode_head[i];
                                } else if(i>=4 && i<20){
                                    get_SNCode_success[i] = user_data[i-4];
                                }
                            }
                            get_SNCode_success[20] = get_SNCode_tail[0];
                            uart_write_bytes(UART_NUM_0, &get_SNCode_success, 21); // 返回串口数据
                        }
                        break;

                    case 0x03:
                        if(input_flag) {
                            uint8_t uuid[16];       // 裁剪subbuffer
                            size_t start = 3;       // 要复制的起始位置（从0开始）
                            size_t count = 16;      // 要复制的字符数目
                            memcpy(uuid, subbuffer + start, count);
                            if(set_SNCode(uuid)){   // 存入flash中
                                get_SNCode();
                                uart_write_bytes(UART_NUM_0, (const char *)set_SNCode_success, sizeof(set_SNCode_success));
                            } else {
                                uart_write_bytes(UART_NUM_0, (const char *)set_SNCode_fail, sizeof(set_SNCode_success));
                            }
                            input_flag = false;
                        } else {
                            uart_write_bytes(UART_NUM_0, (const char *)set_SNCode_noAllow, sizeof(set_SNCode_noAllow));
                        }
                        break;
                    
                    case 0x04:
                        uint8_t get_mac_head[5]  = { 0x7E, 0x84, 0x0D, 0x01 };
                        uint8_t get_mac_tail[2]  = { 0x7F };
                        uint8_t get_mac_success[20];
                        
                        if(subbuffer[3] == 0x00)
                        {
                            char device_str[12];
                            strncpy(device_str, g_get_ble_mac(), 12);

                            for(int i=0; i<16; i++){
                                if(i<4){
                                    get_mac_success[i] = get_mac_head[i];
                                } else if(i>=4 && i<20){
                                    get_mac_success[i] = device_str[i-4];
                                }
                            }
                            get_mac_success[16] = get_mac_tail[0];
                            uart_write_bytes(UART_NUM_0, &get_mac_success, 17); // 返回串口数据
                        }
                        break;
                    
                    case 0x05:
                        if(subbuffer[3] == 0x00)
                        {
                           wifi_reset();
                           clear_storage();
                           esp_restart();
                        }
                        break;
                }
            }   
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void uart_init(void) {
    const uart_config_t uart_config = {
        .baud_rate  = 115200,                   // 波特率115200
        .data_bits  = UART_DATA_8_BITS,         // 数据位8位
        .parity     = UART_PARITY_DISABLE,      // 不开启奇偶校验
        .stop_bits  = UART_STOP_BITS_1,         // 停止位1位
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE, // 关闭硬件流控
        .source_clk = UART_SCLK_DEFAULT,        // 默认时钟源
    };
   
    uart_driver_install(UART_NUM_0, BUF_SIZE, BUF_SIZE*2, 0, NULL, 0);
    uart_param_config(UART_NUM_0, &uart_config);
    uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    // 接收数据
    xTaskCreate(SNCode_recieve, "SNCode_recieve", 4096, NULL, 8, NULL);
}

