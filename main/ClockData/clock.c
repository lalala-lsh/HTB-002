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

/*!
 * \brief 同步SNTP时间到RTC
 * 读取同步完成标志位，等待完成
 * 完成后比较RTC时间与当前同步后的系统时间，相差较大则进行同步
 */

/*!
 * \brief 获取SNTP网路时间功能
 * sntp_init()开始申请服务器返回网络时间
 * setSNTP()通过读取同步完成标志位结束等待
 */

/*!
 * \brief 从RTC中读取时间，将当前的时间设置到系统中去
 * 设备代码运行之初调用该功能
 * 时间源优先级：1. RTC硬件时间（最优先） 2. NVS备份时间 3. 编译时间
 */

/*!
 * \brief 初始化RTC时间
 * 在设备时间未被设定的情况下，初始化一个设备时间
 */

/*!
 * \brief 从RTC获取当前精确时间戳
 * 用于需要精确时间戳的场景，比如记录训练开始/结束时间
 * \return 当前RTC的时间戳
 */

/*!
 * \brief 检查系统时间是否与RTC同步，若不同步则重新同步
 * 这个函数应该定期调用，以确保系统时间准确
 * \return 如果需要重新同步并成功同步返回true，否则返回false
 */

/*!
 * \brief 检查当前时间是否有效
 * 优先信任系统时间（如果已通过SNTP同步），其次才检查RTC
 * \return true: 时间有效，false: 时间无效
 */

/*!
 * \brief 尝试时间同步
 * 检查时间是否有效，如果无效则尝试同步
 * \param timeout_seconds 参数保留兼容性（未使用）
 * \return true: 同步成功，false: 同步失败
 */

/*!
 * \brief 获取安全的时间戳
 * 确保返回的时间戳是有效的，如果时间无效则强制同步
 * \return 有效的时间戳，如果同步失败返回0
 */
