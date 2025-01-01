#ifndef CAMERA_H_
#define CAMERA_H_

#include "My_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_err.h"

esp_err_t camera_init(void);
esp_err_t camera_start(void);
void set_decode_flag(bool flag);
void get_rcg_cnt(uint8_t* tcount, uint8_t* ccount, uint8_t* bcount);
void camera_stop(void);

bool update_camera_status();


#endif