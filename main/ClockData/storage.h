#ifndef _STORAGE_H_
#define _STORAGE_H_

#include "My_system.h"

typedef enum {
    RECORD_FD = 0x01,
    RECORD_RS = 0x02,
} RECORD_TYPE_E;


typedef struct HTB_UsageRecords
{
    uint8_t Mode;
    uint32_t AverageLumen;
    uint64_t StartTime;
    uint64_t EndTime;
    uint32_t ExtValue;
} UsageRecords_t;


UsageRecords_t* getusagerecord(void);
uint16_t* getusagerecordcount(void);

void record_nvs_module_init(void);
void store_record(int level, int start_tm, int end_tm, int work_tm, int rcg_code, int day, uint8_t tcount, uint8_t ccount, uint8_t ecount, uint8_t is_heat, RECORD_TYPE_E type);
int32_t check_record_data(RECORD_TYPE_E type);

//void get_day_record_data(int32_t day, RECORD_TYPE_E type);
int32_t get_day_record_data(int32_t day, RECORD_TYPE_E type, char databuf[][DATA_CNTS]);

void clear_nvs_para_day(int day, RECORD_TYPE_E type);

void clear_day_record_data(int day, RECORD_TYPE_E type);

void project_data_partition_init(void);
void RTC_NVS_init(void);
void get_SNCode(void);
int set_SNCode(uint8_t* sn_code);

bool get_actived(void);
void set_active(bool value);

void func_device_parameters_init(void);
esp_err_t set_device_parameters(int, int);

int get_device_para(int para);

void clear_device_parameters(void);

// UDP IP地址相关函数
char* get_udp_ip(void);
esp_err_t set_udp_ip(const char* ip);

// RCG_CODE 相关函数
uint32_t generate_rcg_code(void);
esp_err_t increment_daily_count(void);
uint32_t get_daily_count(void);
void reset_daily_count_if_new_day(void);

// count_limit 相关函数
int get_count_limit(void);
esp_err_t set_count_limit(int count);

// 时间备份相关函数
void save_valid_time_to_nvs(time_t valid_time);
time_t load_last_valid_time_from_nvs(void);

// 设备日志相关函数
esp_err_t save_device_logs_to_nvs(uint8_t ota_error, uint8_t reboot_reason, time_t ota_error_time, time_t reboot_time);
esp_err_t get_device_logs_from_nvs(char* logs_json, size_t max_len);
void clear_device_logs_from_nvs(void);
uint8_t map_reset_reason_to_code(esp_reset_reason_t reason);

#endif