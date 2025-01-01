#include "blufi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_blufi_api.h"
#include "esp_timer.h"
#include "esp_blufi.h"
#include "esp_gap_ble_api.h"

#include "clock.h"
#include "mqtt.h"
#include "protocal.h"
#include "ds1302.h"
#include "factory.h"
#include "power_manager.h"
#include "image_transfer.h"
#include "camera.h"


#define EXAMPLE_WIFI_CONNECTION_MAXIMUM_RETRY 3
#define EXAMPLE_INVALID_REASON                255
#define EXAMPLE_INVALID_RSSI                  -128


static wifi_state_t wifi_state = WIFI_STATE_IDLE; 

static void example_event_callback(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param);

#define WIFI_LIST_NUM   10

static wifi_config_t sta_config;
static wifi_config_t ap_config;

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int CONNECTED_BIT = BIT0;

static uint8_t example_wifi_retry = 0;

/* store the station info for send back to phone */
static bool gl_sta_connected = false;
static bool gl_sta_got_ip = false;
static bool ble_is_connected = false;
static uint8_t gl_sta_bssid[6];
static uint8_t gl_sta_ssid[32];
static int gl_sta_ssid_len;
static wifi_sta_list_t gl_sta_list;
static bool gl_sta_is_connecting = false;
static esp_blufi_extra_info_t gl_sta_conn_info;


char DEVICE_NAME[16] = "HTB-002-";//蓝牙开放名称    linjun
uint8_t user_data[24] = {0x53, 0x4E, 0x46, 0x46, 0x46, 0x46, 0x46, 0x46, 0x46, 0x46, 0x46, 0x46, 0x46, 0x46, 0x46, 0x46};
//uint8_t init_check_data[2] = {0x43,0x44};//ds1302 camera
static uint8_t blufi_service_uuid128[16] = { // 数组是倒叙的
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00
};

/* blufi广播数据配置 */
static esp_ble_adv_data_t blufi_adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = false,
    .min_interval = 0x0006, //slave connection min interval, Time = min_interval * 1.25 msec
    .max_interval = 0x0010, //slave connection max interval, Time = max_interval * 1.25 msec
    .appearance = 0x00,
    .manufacturer_len = 0, 
    .p_manufacturer_data = NULL,      
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = 16,
    .p_service_uuid = blufi_service_uuid128,
    .flag = 0x6
};

/* blufi广播数据配置 */


/* 记录WIFI连接信息*/









static esp_blufi_callbacks_t example_callbacks = {
    .event_cb = example_event_callback,
    .negotiate_data_handler = blufi_dh_negotiate_data_handler,
    .encrypt_func = blufi_aes_encrypt,
    .decrypt_func = blufi_aes_decrypt,
    .checksum_func = blufi_crc_checksum,
};


