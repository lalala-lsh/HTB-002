#ifndef _CLOCK_H_
#define _CLOCK_H_
#include "My_system.h"

// 标志位，用于指示已经连接到Wi-Fi网络和同步了时间
// static EventGroupHandle_t s_wifi_event_group;

void obtain_time();

void initialize_sntp(void);

bool RTCTimeToSystemTime(void);
void RTCTime_init(void);

// 新增函数 - 获取RTC的精确时间戳
time_t get_rtc_timestamp(void);

// 新增函数 - 检查并同步系统时间与RTC
bool sync_system_with_rtc_if_needed(void);

// 新增函数 - 检查时间是否有效（用于训练前验证）
bool is_time_valid(void);

// 新增函数 - 强制等待时间同步完成（最多等待指定秒数）
bool wait_for_time_sync(uint32_t timeout_seconds);

// 新增函数 - 获取安全的时间戳（确保时间有效）
time_t get_safe_timestamp(void);

// NVS时间备份相关函数
void save_valid_time_to_nvs(time_t valid_time);
time_t load_last_valid_time_from_nvs(void);

#endif /* _CLOCK_H_ */