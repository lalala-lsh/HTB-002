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



