#include "clock.h"

#include "ds1302.h"
#include "storage.h"

// NTP服务器列表
static const char* NTP_SERVERS[] = {"cn.pool.ntp.org", "time.nist.gov", "time.google.com"};

DS1302_Dev ds1302_dev;
bool readFlag = false;

#define TIMEZONE_HOURS 0  // 时区偏移量，以小时为单位

// 写、读地址，初始化时间
unsigned char write_addr[7] = {0x80, 0x82, 0x84, 0x86, 0x88, 0x8A, 0x8C};
unsigned char read_addr[7] 	= {0x81, 0x83, 0x85, 0x87, 0x89, 0x8B, 0x8D};
unsigned char TIME[7] 		= {0x00, 0x30, 0x08, 0x22, 0x08, 0x02, 0x23}; // 秒分时日月周年
// 其他控制地址
#define DS1302_CONTROL_REG_ADDR 0x8E // 写保护控制地址
// #define DS1302_RAM_REG_ADDR 	0xC0
// 反向
#define high 0
#define low 1

#define DS1302_CMD_WRITE_CLOCK_BURST    (DS1302_ACB | DS1302_ACB_CLOCK | 0x3E | DS1302_ACB_WRITE)

// static int time_nums = 100;

static const char *TAG = "clock";

// 添加一个标志，表示系统时间是否已经与RTC同步
static bool system_time_synced = false;

/*
esp_http_client_config_t config = {
    .url = "http://www.baidu.com",
};

void check_network() {
	ESP_LOGI(TAG, "执行网络检查程序");

	esp_http_client_handle_t client = esp_http_client_init(&config);
	esp_err_t err = esp_http_client_perform(client);

	if (err == ESP_OK) {
		int status_code = esp_http_client_get_status_code(client);
		ESP_LOGI(TAG, "HTTP status code: %d", status_code);
	} else {
		ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
	}

	esp_http_client_cleanup(client);
}
*/

/*!
 * \brief 初始化SNTP功能
 * 包括设置SNTP运行模式，设置服务器地址，打印服务器地址
 */
void initialize_sntp(void) {
    ESP_LOGI(TAG, "初始化SNTP");

	ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);

	sntp_setservername(0, NTP_SERVERS[0]);
	sntp_setservername(1, NTP_SERVERS[1]);
	sntp_setservername(2, NTP_SERVERS[2]);

	for (uint8_t i = 0; i < SNTP_MAX_SERVERS; ++i){
        if (sntp_getservername(i)){
            ESP_LOGI(TAG, "server %d: %s", i, sntp_getservername(i));
        } else {
			ESP_LOGI(TAG, "if(sntp_getservername(i))判断为负");
            char buff[INET6_ADDRSTRLEN];
            ip_addr_t const *ip = sntp_getserver(i);
            if (ipaddr_ntoa_r(ip, buff, INET6_ADDRSTRLEN) != NULL)
                ESP_LOGI(TAG, "server %d: %s", i, buff);
        }
    }
}

/*!
 * \brief 同步SNTP时间到RTC
 * 读取同步完成标志位，等待完成
 * 完成后比较RTC时间与当前同步后的系统时间，相差较大则进行同步
 */
void setSNTP()
{
	ESP_LOGI(TAG, "setSNTP函数被调用\n");
	readFlag = true; // 表示资源被占用

	// 通过SNTP获取网络时间
    time_t now = 0;
    struct tm timeinfo_sntp = {0};
	int count_retry = 0;

	while (1)
	{
		int state = sntp_get_sync_status();
		ESP_LOGI(TAG, "the first state:%d", state);

		if(state != SNTP_SYNC_STATUS_COMPLETED)
		{
			if(count_retry < 5){
				vTaskDelay(pdMS_TO_TICKS(2000));
				time(&now);
				localtime_r(&now, &timeinfo_sntp);
				ESP_LOGI(TAG, "System time: %s", asctime(&timeinfo_sntp));
				count_retry++;
			} else{
				count_retry = 0;
				ESP_LOGE(TAG, "达到最大尝试次数");
				readFlag = false; // 释放资源
				return;
			}
		} else if(state == SNTP_SYNC_STATUS_COMPLETED){
			time(&now);
			localtime_r(&now, &timeinfo_sntp);
			ESP_LOGI(TAG, "同步完成，System time is set to %s", asctime(&timeinfo_sntp));
			break;
		} 
    }

	// 判断SNTP所获取的timeinfo与ds1302当前时间之间的差值
	DS1302_DateTime dtr;
	if (!DS1302_getDateTime(&ds1302_dev, &dtr)) {
		ESP_LOGE("SNTP", "Error: DS1302 read failed");
	} else{
		ESP_LOGI("SNTP", "同步之前的RTC时间：%d %02d-%02d-%d %d:%02d:%02d",
			dtr.dayWeek, 
			dtr.dayMonth, dtr.month, dtr.year, 
			dtr.hour, dtr.minute, dtr.second);

		if((dtr.year != (timeinfo_sntp.tm_year+1900)) | 
		   (dtr.month != (timeinfo_sntp.tm_mon+1)) | 
		   (dtr.dayMonth != timeinfo_sntp.tm_mday) |
		   (dtr.dayWeek != timeinfo_sntp.tm_wday) |
		   (dtr.hour != timeinfo_sntp.tm_hour) |
		   (dtr.minute != timeinfo_sntp.tm_min) |
		   (dtr.second - timeinfo_sntp.tm_sec > 15) | 
		   (timeinfo_sntp.tm_sec - dtr.second > 15))
		{
			DS1302_DateTime dtw;
			dtw.second 		= timeinfo_sntp.tm_sec;
			dtw.minute 		= timeinfo_sntp.tm_min;
			dtw.hour   		= timeinfo_sntp.tm_hour;
			dtw.dayWeek 	= timeinfo_sntp.tm_wday;
			dtw.dayMonth 	= timeinfo_sntp.tm_mday;
			dtw.month 		= (timeinfo_sntp.tm_mon + 1);
			dtw.year 		= (timeinfo_sntp.tm_year + 1900);
			DS1302_setDateTime(&ds1302_dev, &dtw);
			ESP_LOGI("SNTP", "DS1302 RTC time is set\n");

			if (!DS1302_getDateTime(&ds1302_dev, &dtr)) {
				ESP_LOGE("SNTP", "Error: DS1302 read failed");
			} else{
				ESP_LOGI("SNTP", "同步之后的RTC时间：%d %02d-%02d-%d %d:%02d:%02d",
					dtr.dayWeek, 
					dtr.dayMonth, dtr.month, dtr.year, 
					dtr.hour, dtr.minute, dtr.second);
			}
		} else{
			ESP_LOGI("SNTP", "DS1302 RTC time no need to be set\n");
		}
	}

	// 标记系统时间已同步
	system_time_synced = true;

	// 网络时间同步成功后，设置RTC初始化标志
	extern bool RTCTimeSet_flag;
	RTCTimeSet_flag = true;
	ESP_LOGI("SNTP", "网络时间同步完成，设置RTCTimeSet_flag = true");

	// 保存当前有效时间到NVS，作为备份
	time(&now);
	save_valid_time_to_nvs(now);
	ESP_LOGI("SNTP", "已保存SNTP时间到NVS备份，时间戳: %ld", (long)now);

	readFlag = false;
}

/*!
 * \brief 获取SNTP网路时间功能
 * sntp_init()开始申请服务器返回网络时间
 * setSNTP()通过读取同步完成标志位结束等待
 */
void obtain_time()
{
	while(readFlag == true){
		vTaskDelay(100 / portTICK_PERIOD_MS);
	}
	if(readFlag == false){
		sntp_init(); 
		setSNTP();
	} 
}

/*!
 * \brief 从RTC中读取时间，将当前的时间设置到系统中去
 * 设备代码运行之初调用该功能
 * 时间源优先级：1. RTC硬件时间（最优先） 2. NVS备份时间 3. 编译时间
 */
bool RTCTimeToSystemTime()
{
	/* 设置时区 */
	setenv("TZ", "CST-8", 1);
	tzset();
	/* 初始化 */
	struct timeval tv = {0};
	struct tm tm_time = {0};
	bool time_source_valid = false;

	/* 尝试从RTC获取时间 */
	DS1302_DateTime dtr;
	if (DS1302_getDateTime(&ds1302_dev, &dtr) && dtr.year >= 2020 && dtr.year <= 2050) {
		/* RTC时间获取成功且合理，设置到系统时间中去 */
		ESP_LOGI("RTC", "RTC时间有效：%d %02d-%02d-%d %d:%02d:%02d",
			dtr.dayWeek,
			dtr.dayMonth, dtr.month, dtr.year,
			dtr.hour, dtr.minute, dtr.second);
		/* 转换结构体格式 */
		tm_time.tm_wday = dtr.dayWeek;
		tm_time.tm_mday = dtr.dayMonth;
		tm_time.tm_mon	= dtr.month - 1;
		tm_time.tm_year	= dtr.year - 1900;
		tm_time.tm_hour	= dtr.hour;
		tm_time.tm_min	= dtr.minute;
		tm_time.tm_sec	= dtr.second;
		/* 设置到系统时间 */
		tv.tv_sec = mktime(&tm_time) + TIMEZONE_HOURS * 3600;
		settimeofday(&tv, NULL);
		time_source_valid = true;
		ESP_LOGI("RTC", "系统时间已从RTC同步，时间戳：%ld", (long)tv.tv_sec);
	} else {
		ESP_LOGW("RTC", "DS1302读取失败或时间不合理，尝试使用NVS备份时间");

		/* 尝试从NVS读取上次有效时间 */
		time_t last_valid_time = load_last_valid_time_from_nvs();

		if (last_valid_time > 1577836800) {  // 检查是否大于2020-01-01
			/* 使用NVS备份时间 */
			tv.tv_sec = last_valid_time;
			settimeofday(&tv, NULL);
			time_source_valid = true;
			ESP_LOGI("RTC", "系统时间已从NVS备份同步，时间戳：%ld", (long)tv.tv_sec);
		} else {
			/* NVS也没有有效时间，使用2025年作为初始时间 */
			/* 这种情况只在第一次初始化或NVS被清空时发生 */
			static bool default_time_set = false;
			if (!default_time_set) {
				ESP_LOGW("RTC", "NVS中无有效时间，使用2025年作为初始时间");
				tm_time.tm_year = 2025 - 1900;
				tm_time.tm_mon = 0;  // 1月
				tm_time.tm_mday = 1;
				tm_time.tm_hour = 0;
				tm_time.tm_min = 0;
				tm_time.tm_sec = 0;
				tv.tv_sec = mktime(&tm_time);
				settimeofday(&tv, NULL);
				ESP_LOGW("RTC", "系统时间已设置为默认值：2025-01-01，时间戳：%ld", (long)tv.tv_sec);
				default_time_set = true;
				// 虽然是默认时间，但也标记为已同步，避免频繁重试
				time_source_valid = true;
			} else {
				// 已经设置过默认时间，不再重复设置
				ESP_LOGD("RTC", "默认时间已设置，跳过重复初始化");
				time_source_valid = false;
			}
		}
	}

	// 标记同步完成，避免频繁重试
	if (time_source_valid) {
		system_time_synced = true;
		return true;
	}

	return false;
}

/*!
 * \brief 初始化RTC时间
 * 在设备时间未被设定的情况下，初始化一个设备时间
 */
void RTCTime_init()
{
    if (DS1302_isWriteProtected(&ds1302_dev)) {
        ESP_LOGE("RTC", "Error: DS1302 write protected");
    }
    DS1302_DateTime dt;
    dt.second 	= 0;
    dt.minute 	= 0;
    dt.hour   	= 20;
    dt.dayWeek 	= 3;
    dt.dayMonth = 30;
    dt.month 	= 8;
    dt.year 	= 2023;
    DS1302_setDateTime(&ds1302_dev, &dt);
    
    // 初始化后立即同步到系统时间
    RTCTimeToSystemTime();
}

/*!
 * \brief 从RTC获取当前精确时间戳
 * 用于需要精确时间戳的场景，比如记录训练开始/结束时间
 * \return 当前RTC的时间戳
 */
time_t get_rtc_timestamp()
{
    DS1302_DateTime dtr;
    struct tm tm_time = {0};
    
    // 如果无法从RTC读取时间，返回系统时间
    if (!DS1302_getDateTime(&ds1302_dev, &dtr)) {
        ESP_LOGW("RTC", "无法读取RTC时间，使用系统时间");
        return time(NULL);
    }
    
    // 修复年份问题：如果年份不合理（如2000），使用系统时间
    if (dtr.year < 2020 || dtr.year > 2050) {
        ESP_LOGW("RTC", "RTC年份不合理 (%d)，使用系统时间", dtr.year);
        ESP_LOGW("RTC", "原始RTC数据: %04d-%02d-%02d %02d:%02d:%02d", 
                 dtr.year, dtr.month, dtr.dayMonth, dtr.hour, dtr.minute, dtr.second);
        return time(NULL);
    }
    
    // 转换RTC时间为时间戳格式
    tm_time.tm_wday = dtr.dayWeek;
    tm_time.tm_mday = dtr.dayMonth;
    tm_time.tm_mon = dtr.month - 1;  
    tm_time.tm_year = dtr.year - 1900;
    tm_time.tm_hour = dtr.hour;
    tm_time.tm_min = dtr.minute;
    tm_time.tm_sec = dtr.second;
    
    // 调试日志
    ESP_LOGI("RTC", "RTC时间有效: %04d-%02d-%02d %02d:%02d:%02d (tm_year=%d)", 
             dtr.year, dtr.month, dtr.dayMonth, dtr.hour, dtr.minute, dtr.second, tm_time.tm_year);
    
    // 转换为时间戳并添加时区偏移
    return mktime(&tm_time) + TIMEZONE_HOURS * 3600;
}

/*!
 * \brief 检查系统时间是否与RTC同步，若不同步则重新同步
 * 这个函数应该定期调用，以确保系统时间准确
 * \return 如果需要重新同步并成功同步返回true，否则返回false
 */
bool sync_system_with_rtc_if_needed()
{
    static bool rtc_read_failed = false;  // 记录RTC是否损坏

    // 如果系统时间未同步，尝试同步一次
    if (!system_time_synced) {
        bool result = RTCTimeToSystemTime();
        if (!result) {
            rtc_read_failed = true;  // 标记RTC损坏
        }
        return result;
    }

    // 如果之前检测到RTC损坏，就不再频繁尝试读取
    if (rtc_read_failed) {
        return false;
    }

    // 获取系统时间与RTC时间
    time_t sys_time = time(NULL);
    time_t rtc_time = get_rtc_timestamp();

    // 如果RTC时间为0或1970年，说明RTC读取失败，标记为损坏
    if (rtc_time < 1577836800) {  // 2020-01-01之前
        ESP_LOGW("RTC", "RTC读取失败或时间不合理，标记为损坏状态");
        rtc_read_failed = true;
        return false;
    }

    // 如果差异超过5秒，重新同步
    if (llabs(sys_time - rtc_time) > 5) {
        ESP_LOGW("RTC", "系统时间与RTC时间不同步，差值为%ld秒，重新同步", (long)(sys_time - rtc_time));
        return RTCTimeToSystemTime();
    }

    return false;
}

/*!
 * \brief 检查当前时间是否有效
 * 优先信任系统时间（如果已通过SNTP同步），其次才检查RTC
 * \return true: 时间有效，false: 时间无效
 */
bool is_time_valid(void)
{
    time_t sys_time = time(NULL);

    // 优先级1：如果系统时间已同步（通过SNTP或RTC），直接信任系统时间
    if (system_time_synced) {
        if (sys_time >= 1704067200) {  // 2024-01-01 00:00:00 UTC
            ESP_LOGI("TIME_CHECK", "系统时间已同步且有效，时间戳: %ld", (long)sys_time);
            return true;
        } else {
            ESP_LOGW("TIME_CHECK", "系统时间已同步但时间戳过小: %ld", (long)sys_time);
        }
    }

    // 优先级2：检查RTCTimeSet_flag（表示SNTP成功写入过RTC）
    extern bool RTCTimeSet_flag;
    if (RTCTimeSet_flag) {
        // SNTP成功过，即使RTC读取有问题，也信任系统时间
        if (sys_time >= 1704067200) {
            ESP_LOGI("TIME_CHECK", "SNTP已同步标志有效，系统时间戳: %ld", (long)sys_time);
            return true;
        }
    }

    // 优先级3：即使标志位未设置，如果系统时间合理（>=2024年），也认为有效
    // 这是为了处理系统已正确初始化但标志位未设置的情况
    if (sys_time >= 1704067200) {  // 2024-01-01 00:00:00 UTC
        struct tm timeinfo;
        localtime_r(&sys_time, &timeinfo);
        int year = timeinfo.tm_year + 1900;

        // 如果年份在合理范围内（2024-2050），认为时间有效
        if (year >= 2024 && year <= 2050) {
            ESP_LOGI("TIME_CHECK", "系统时间合理（%d年），认为时间有效，时间戳: %ld",
                     year, (long)sys_time);
            // 自动设置同步标志
            system_time_synced = true;
            return true;
        }
    }

    // 优先级4：尝试读取RTC验证（作为最后的验证手段）
    DS1302_DateTime dtr;
    if (DS1302_getDateTime(&ds1302_dev, &dtr)) {
        // 检查RTC年份是否合理
        if (dtr.year >= 2024 && dtr.year <= 2050) {
            ESP_LOGI("TIME_CHECK", "RTC时间有效: %04d-%02d-%02d %02d:%02d:%02d",
                     dtr.year, dtr.month, dtr.dayMonth, dtr.hour, dtr.minute, dtr.second);
            return true;
        } else {
            ESP_LOGW("TIME_CHECK", "RTC年份不合理: %d", dtr.year);
        }
    } else {
        ESP_LOGW("TIME_CHECK", "无法读取RTC时间");
    }

    // 所有检查都失败
    ESP_LOGW("TIME_CHECK", "时间验证失败 - system_time_synced=%d, RTCTimeSet_flag=%d, sys_time=%ld",
             system_time_synced, RTCTimeSet_flag, (long)sys_time);
    return false;
}

/*!
 * \brief 尝试时间同步
 * 检查时间是否有效，如果无效则尝试同步
 * \param timeout_seconds 参数保留兼容性（未使用）
 * \return true: 同步成功，false: 同步失败
 */
bool wait_for_time_sync(uint32_t timeout_seconds)
{
    extern bool RTCTimeSet_flag;
    
    // 检查时间是否已经有效
    if (is_time_valid()) {
        ESP_LOGI("TIME_SYNC", "时间同步成功");
        return true;
    }
    
    // 尝试从RTC同步到系统时间
    if (!RTCTimeSet_flag) {
        ESP_LOGI("TIME_SYNC", "尝试从RTC同步时间到系统");
        RTCTimeSet_flag = RTCTimeToSystemTime();
    }
    
    return is_time_valid();
}

/*!
 * \brief 获取安全的时间戳
 * 确保返回的时间戳是有效的，如果时间无效则强制同步
 * \return 有效的时间戳，如果同步失败返回0
 */
time_t get_safe_timestamp(void)
{
    // 先检查时间是否有效
    if (!is_time_valid()) {
        ESP_LOGW("SAFE_TIME", "时间无效，尝试强制同步");
        
        // 尝试进行时间同步
        if (!wait_for_time_sync(0)) {
            ESP_LOGE("SAFE_TIME", "时间同步失败，无法获取有效时间戳");
            return 0;  // 返回0表示时间无效
        }
    }
    
    // 时间有效，返回RTC时间戳
    time_t timestamp = get_rtc_timestamp();
    
    // 再次验证时间戳的合理性
    if (timestamp < 1577836800) {  // 2020-01-01 00:00:00 UTC
        ESP_LOGE("SAFE_TIME", "获取的时间戳无效: %ld", (long)timestamp);
        return 0;
    }
    
    ESP_LOGI("SAFE_TIME", "获取安全时间戳: %ld", (long)timestamp);
    return timestamp;
}