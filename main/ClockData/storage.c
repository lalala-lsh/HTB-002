#include "storage.h"

#include "protocal.h"
#include "clock.h"

#include "feed.h"
#include "heating.h"
#include "voice.h"
#include "camera.h"
#include "cJSON.h"
extern uint8_t user_data[24];

// static char* key_device_parameters [] = {   
//      "switch_bgm", 
//     "volume", 
//     "feed_power", 
//     "feed_time", 
//     "ruoshi_time",
//     "heating_time",
//     "camera",
//     "hertz"
// };

/** 
    0	DEVICE_PARA_BGM	"music"	"switch_bgm"	数组[0]
    1	DEVICE_PARA_VOL	"volume"	"volume"	数组[1]
    2	DEVICE_PARA_FEED_P	"feeding_power"	"feed_power"	数组[2]
    3	DEVICE_PARA_FEED_T	"feeding_duration"	"feed_time"	数组[3]
    4	DEVICE_PARA_RUOSHI_T	"amblyopia_duration"	"ruoshi_time"	数组[4]
    5	DEVICE_PARA_HEAT_T	"hot_duration"	"heating_time"	数组[5]
    6	DEVICE_PARA_CAMERA	"camera"	"camera"	数组[6]
    7	DEVICE_PARA_40HZ	"hertz"	"hertz"	数组[7]
    8	DEVICE_PARA_UDP_IP	"udp_ip"	"udp_ip"	独立存储
    9	DEVICE_PARA_PICTURE	"ucode"	"unicode" 
    10	DEVICE_PARA_FEEDING_HEAT	"feeding_heat"	"feeding_heat"	数组[8]
    11	DEVICE_PARA_COUNT_LIMIT	"count_limit"	"count_limit"	数组[9]
 */

static char* key_device_parameters [] = {   
    "switch_bgm", 
    "volume", 
    "feed_power", 
    "feed_time", 
    "ruoshi_time",
    "heating_time",
    "camera",
    "hertz",
    "udp_ip",
    "unicode",
    "feeding_heat",
    "count_limit"
};

// 移除UDP IP地址和picture参数，因为它们需要特殊处理
int initialData_device_parameters [DEVICE_PARA_MAX-2] = {1, 5, 80, 3, 5, 15, 1, 0, 0, -1};
int actualData_device_parameters [DEVICE_PARA_MAX-2] = {1, 5, 80, 3, 5, 15, 1, 0, 0, -1};

// 单独存储UDP IP地址
static char udp_ip[16] = "192.168.1.100";

/*
本地存储设计
分区表增加nvs属性的新分区，NVS存储原理是按空间和键值来，空间里可以对应不同的键值
                         分区
      空间1 参数空间               空间2 记录空间                空间n 记录空间              
   键值1      键值2……            键值1       键值2……11       键值1       键值2……11    
 初始化标志   是否有记录标志     数据条数；  对应10条存储     数据条数；  对应10条存储
 
 哺光数据一个分区
 弱视训练一个分区

*/
#define FD_STORAGE_PARTITION    "fd_data"  //哺光记录分区名
#define RS_STORAGE_PARTITION    "rs_data"  //弱视训练分区名
#define PROJECT_STORAGE_PARTITION  "project_data"  // 项目数据分区名

#define PARA_SPACENAME       "space_para" //参数空间
#define PARA_INITKEY         "para_init"  //参数键值 是否初始化 1个 int
#define INITED               (0x5A5A5A5A)
#define PARA_DAYKEY         "para_day" //参数键值 有存储数据的日期 按位表 
static char* s_num[32] = {"0","1","2","3","4","5","6","7","8","9","10",
                          "11","12","13","14","15","16","17","18","19","20",
                          "21","22","23","24","25","26","27","28","29","30","31"};
//#define DATA_SPACENAME    "space_data"  //数据空间  -- 31个循环进行拼接
static char s_spacename_data[16] = "space_data"; //数据空间  -- 需要拼接成space_data1、space_data2……space_data31
#define DATA_NUMS                "data_nums"//对应键值 数据存储的条数 1个int型 
static char s_data_name[16]      = "rd_data"; //对应键值,需要拼接成 rd_data0、rd_data2……rd_data9
//然后就是每个对应键值的空间是DATA_CNTS个char
static char s_data[DATA_CNTS]  = {0};

//位于PROJECT_STORAGE_PARTITION这个分区中
#define ACTIVE_SPACENAME    "space_active"   //激活参数空间
#define ACTIVE_KEY          "active_value"   //键值对--激活与否的标志
#define ACTIVE_DONE         (0xA5A5A5A5)

#define ACTIVED      (1)
#define NOT_ACTIVE   (0)
static bool  s_is_active = NOT_ACTIVE;
bool RTCTimeSet_flag = false;

#define LOG_SPACENAME "log_space"
#define LOG_KEY "device_logs"
#define LOG_MAX_ENTRIES 40

/*
clear_nvs_para_day
该函数应用时要注意，这里的day输入必须是实际日期的比如1号开始，比如1号、31号，
在应用过程中如果是在按位存储中要注意，按位存储是从0位开始表示1号，则不能输入0要表示1号，而应该+1
*/
 
/*
update_nvs_para_day
该函数应用时要注意，这里的day输入必须是实际日期的比如1号开始，比如1号、31号，
在应用过程中如果是在按位存储中要注意，按位存储是从0位开始表示1号，则不能输入0要表示1号，而应该+1
*/


/*
检查存储数据
返回:有存储数据的天数标示，比如0x0003 表示第1、2天有存储数据
*/




/*存储数据存储的是以下几项，其他的读出后在外面进行拼接后上报
输入参数：
level：对应的按键的功能，拼凑在"DEVICE_MODE"字段
start_tm：拼凑在"DEVICE_START_TIME"字段
end_tm：拼凑在 "DEVICE_END_TIME" 字段
work_tm：拼凑在 "DEVICE_WORK_TIME"字段
day：当前日期，存储在数据分区中对应的位置
type：哺光数据RECORD_FD还是弱视训练数据RECORD_RS

存储数据内容介绍：
CMD
DEVICE_MODE
DEVICE_START_TIME
DEVICE_END_TIME
DEVICE_WORK_TIME
*/





static UsageRecords_t arrUsageRecords[30][10];


/*
获取设备是否被激活的标示,在红光、彩光功能开启之前调用
*/
/*
该函数用于对激活与取消激活标志设置，及对NVS里的标志进行设置
激活设置在连接网络且收到系统告知已经激活时调用
取消激活在双按键被同时按下情况下使用
*/





// 获取 i32 类型的数据


/** 
 * \brief 函数用于同步 NVS 数据和全局变量数据
 * 
 * - NVS 中不存在相关键值对的情况下，设置默认值
 * 
 * - NVS 中存在相关键值对，将读出数据设置到全局变量中
 **/

/** 
 * \brief 设置参数功能，包括写入和读出测试两个部分
 * \param index_key_device_parameters: 设备参数值对应索引
 * \param data：设置值
 * @return esp_err_t 
 **/
/** 
 * \brief 获取设备的参数
 * 
 * 返回设备参数给到各个模块进行配置
 * 
 **/

/** 
 * \brief 设备参数初始化（遍历所有设备参数实现）
 * 
 * 设备参数初始化包括：
 * 
 * - 从 NVS 中读出数据
 * 
 * - 同步到全局变量
 * 
 * - 调用相关函数使全局变量设置生效
 * 
 **/




// RCG_CODE 相关实现
#define RCG_CODE_SPACENAME "rcg_code_space"
#define DAILY_COUNT_KEY "daily_count"
#define LAST_DATE_KEY "last_date"

// 静态变量存储当前日期的识别次数
static uint32_t current_daily_count = 0;
static int current_date = 0;  // 格式：YYYYMMDD

/**
 * @brief 检查是否是新的一天，如果是则重置计数
 */

/**
 * @brief 递增每日识别次数
 */

/**
 * @brief 获取当前每日识别次数
 */

/**
 * @brief 生成RCG_CODE
 * @return 格式为YYYYMMDDNN的RCG_CODE
 */

/**
 * @brief 获取剩余使用次数
 * @return 剩余次数，-1表示不限
 */

/**
 * @brief 设置剩余使用次数
 * @param count 剩余次数，-1表示不限
 * @return ESP_OK 成功，ESP_FAIL 失败
 */

// ============= 时间备份功能 =============

#define TIME_BACKUP_SPACENAME "space_time"
#define TIME_BACKUP_KEY "last_valid_time"

/**
 * @brief 保存有效时间到NVS
 * @param valid_time 有效的时间戳
 */

/**
 * @brief 从NVS读取上次有效时间
 * @return 上次保存的有效时间戳，如果不存在返回0
 */

// ==================== 设备日志相关函数 ====================

/**
 * @brief 将重启原因映射到编号
 * @param reason 重启原因枚举值
 * @return uint8_t 编号（0-14），0表示未知也记录
 */

/**
 * @brief 保存设备日志到NVS
 * @param ota_error OTA失败原因编号（0-9，0表示未知也记录）
 * @param reboot_reason 重启原因编号（0-15，0表示未知也记录）
 * @param ota_error_time OTA失败时间戳（0表示未发生）
 * @param reboot_time 重启时间戳（0表示未发生异常重启）
 * @return esp_err_t ESP_OK表示成功，其他表示失败
 */

/**
 * @brief 从NVS读取设备日志
 * @param logs_json 用于存储日志JSON字符串的缓冲区
 * @param max_len 缓冲区最大长度
 * @return esp_err_t ESP_OK表示成功，ESP_ERR_NVS_NOT_FOUND表示没有日志，其他表示失败
 */


/**
 * @brief 清除NVS中的设备日志
 */
