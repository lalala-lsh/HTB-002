#include "protocal.h"

#include "clock.h"
#include "blufi.h"
#include "ota.h"
#include "storage.h"
#include "key.h"
#include "led_status.h"
#include "power_manager.h"
#include "voice.h"
#include "mqtt.h"
#include "feed.h"
#include "heating.h"
#include "camera.h"
#include "image_transfer.h"
#include "imgfile_save.h"

extern QueueHandle_t voice_evt_queue;

static const char *TAG = "MESSAGE";
char push_topic[64] = {0};
char pull_topic[64] = {0};
uint8_t base_mac_[6];

char devicestr[20] = {0};
static char ble_mac[20] = {0};
char device_version[20]  = {0};


// 用于存储等待上传图片的RCG_CODE
static uint32_t pending_rcg_code = 0;
static bool s_boot_version_sent = false;
static bool s_boot_ota_waiting = false;
static char s_boot_version_rid[30] = {0};
static int64_t s_boot_version_send_time_ms = 0;
static KEY_FUNC_E s_version_sync_key_status = KEY_FUNC_OFF;

#define BOOT_OTA_REPLY_WINDOW_MS (120 * 1000)

extern uint8_t user_data[24];
char c_user_data[24];

extern int power;

extern uint32_t feed_power;

#include <esp_timer.h>
/*Hash 取模操作*/
#include <stdio.h>
#include "mbedtls/md.h"

#define HASH_MD5    MBEDTLS_MD_MD5
#define BUFFER_SIZE 1024

// extern char databuf[10][240];
static char databuf[10][DATA_CNTS] = {0};


// 服务器端的 key List
// static char* server_keys [] = {
// 	"music", 					//音乐，开关：1, 0
	// "volume", 					//音量：1-5
	// "feeding_power", 			//哺光强度：30-100，默认80
	// "feeding_duration",			//哺光时长：1-10，默认3
	// "amblyopia_duration",		//彩光时长：1-5，默认5
	// "hot_duration",				//加热时长：1-15，默认15
	// "camera",					//摄像头开关：1, 0
	// "udp_ip",					//UDP IP地址
	// "hertz"
// };
// 服务器端的 key List  linjun
static char* server_keys [] = {
	"music", 					//音乐，开关：1, 0
	"volume", 					//音量：1-5
	"feeding_power", 			//哺光强度：30-100，默认80
	"feeding_duration",			//哺光时长：1-10，默认3
	"amblyopia_duration",		//彩光时长：1-5，默认5
	"hot_duration",				//加热时长：1-15，默认15
	"camera",					//摄像头开关：1, 0
	"hertz",					//蜂鸣器开关 (对应DEVICE_PARA_40HZ)
	"udp_ip",					//UDP IP地址 (对应DEVICE_PARA_UDP_IP)
	"ucode",					//图片参数 (对应DEVICE_PARA_PICTURE)
	"feeding_heat",				//红光加热开关 (对应DEVICE_PARA_FEEDING_HEAT)
	"count_limit"				//剩余次数 (对应DEVICE_PARA_COUNT_LIMIT)
};


/* 蓝牙控制的软定时器中断函数 */
esp_timer_handle_t periodic_timer;
 
void hash(unsigned char *output, const unsigned char *input, size_t input_len, mbedtls_md_type_t md_type) // 使用MD5哈希算法
{
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);
    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, input, input_len);
    mbedtls_md_finish(&ctx, output);
    mbedtls_md_free(&ctx);
}

unsigned int mod(unsigned char *hash_value, unsigned int mod_num) // 取模运算
{
    unsigned int result = 0;
    for (int i = 0; i < 16; i++) {
        result <<= 8;            // 每次将结果左移8位
        result += hash_value[i]; // 加上当前字节
        result %= mod_num;       // 取模
    }
	if(result == 0) result = mod_num;
    return result;
}
uint8_t * get_mac()
{
	return base_mac_;
}

void read_mac()
{
	esp_err_t ret = ESP_OK;
	ret = esp_efuse_mac_get_default(base_mac_);
	
	if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get base MAC address from EFUSE BLK0. (%s)", esp_err_to_name(ret));
        ESP_LOGE(TAG, "Aborting");
        abort();
    } else {
        ESP_LOGI(TAG, "Base MAC Address read from EFUSE BLK0");
        ESP_LOGI(TAG, "Using \"0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x\" as base MAC address",
            base_mac_[0], base_mac_[1], base_mac_[2], base_mac_[3], base_mac_[4], base_mac_[5]);
    }
	build_pp_topic();
}

void build_pp_topic()
{
	snprintf(devicestr, 20, "%02x%02x%02x%02x%02x%02x", base_mac_[0], base_mac_[1], base_mac_[2], base_mac_[3], base_mac_[4], base_mac_[5]);
	snprintf(ble_mac, 20, "%02x%02x%02x%02x%02x%02x", base_mac_[0], base_mac_[1], base_mac_[2], base_mac_[3], base_mac_[4], base_mac_[5]+2);
	// printf("蓝牙MAC地址：%s\n", ble_mac);
	/*对于设备码进行哈希取模处理*/
	unsigned char hash_value[16]; 
	hash(hash_value, (unsigned char*)devicestr, sizeof(devicestr), HASH_MD5);   // 转为无符号指针形式 / 对buf中的数据进行哈希运算
	unsigned int result = mod(hash_value, 2);									// 对哈希值进行取模运算

	memcpy(c_user_data, user_data, sizeof(user_data)); // 构造user_data的字符数组形式
	ESP_LOGI("BUILD_TOPIC", "%s", c_user_data);

    snprintf(push_topic, 64, "%s%u/%s/htb_pub", TOPIC, result, c_user_data);
	snprintf(pull_topic, 64, "%s%s/htb_sub", TOPIC, c_user_data);
}

char * get_pub_tpc()
{
	return push_topic;
}

char * get_sub_tpc()
{
	return pull_topic;
}

char * get_device_str()
{
	return devicestr;
}

char * g_get_ble_mac()
{
	return ble_mac;
}

void build_heartbeat(esp_mqtt_client_handle_t client){

	if(get_mqtt_status() ==  MQTT_EVENT_CONNECTED_)
	{
		cJSON *root = NULL;
		
		char rid[30];
		time_t rawtime;
		uint64_t current_time_ms = esp_timer_get_time()/1000;
		int ms_pre = current_time_ms%1000;
		int power_up = power;

		snprintf(rid, 30, "%s%ld%d", c_user_data, (long)time(&rawtime), ms_pre);
		
		// ESP_LOGI(TAG, "sent publish successful, heartbeat, topic:%s, devid:%s, rid:%s", get_pub_tpc(), c_user_data, rid);
		root = cJSON_CreateObject();
		cJSON_AddNumberToObject(root, "CMD", CMD_HEARTBEAT);
		cJSON_AddStringToObject(root, "VERSION", device_version);
		cJSON_AddStringToObject(root, "DEVICE_TYPE", DEVICE_TYPE);
		cJSON_AddStringToObject(root, "DEVICE_CODE", c_user_data);
		cJSON_AddNumberToObject(root, "DEVICE_TIME", time(NULL));
		cJSON_AddNumberToObject(root, "DEVICE_POWER", power_up);
		cJSON_AddStringToObject(root, "RID", rid);

		char * String = NULL;
		String = cJSON_PrintUnformatted(root);
		int msg_id;
		(void)msg_id;
		msg_id = esp_mqtt_client_publish(client, get_pub_tpc(), String, strlen(String), 0, 0);
		ESP_LOGI(TAG, "sent publish successful, heartbeat, msg_id=%d", msg_id);
		cJSON_free(String);
		cJSON_Delete(root);

		ESP_LOGI(TAG, "心跳发送成功");
	}
}

void build_feedreport(esp_mqtt_client_handle_t client, int level, int start_tm, int end_tm, int work_tm, int rcg_code, uint8_t tcount, uint8_t ccount, uint8_t ecount, uint8_t is_heat){
	if (client != NULL)
	{
		// 存储RCG_CODE用于后续图片上传
		pending_rcg_code = rcg_code;
		char * levelstr = NULL;
		switch(level) 
		{
			case 0x01:
				levelstr = "Feeding";
				break;
			case 0x02:
				levelstr = "Amblyopia";
				break;
			case 0x03:
				levelstr = "Heat";//加热模式
				break;
			default:
				levelstr = "ERROR01";
				break; 
		}

		char rid[30];
		time_t rawtime;
		uint64_t current_time_ms = esp_timer_get_time()/1000;
		int ms_pre = current_time_ms%1000;
		snprintf(rid, 30, "%s%ld%d", c_user_data, (long)time(&rawtime), ms_pre);

		cJSON *root = NULL;

		root = cJSON_CreateObject();
		cJSON_AddNumberToObject(root, "CMD", CMD_FEEDREPORT);
		cJSON_AddStringToObject(root, "VERSION", device_version);
		cJSON_AddStringToObject(root, "DEVICE_TYPE", DEVICE_TYPE);
		cJSON_AddStringToObject(root, "DEVICE_CODE", c_user_data);
		cJSON_AddStringToObject(root, "DEVICE_MODE", levelstr);

		cJSON_AddNumberToObject(root, "DEVICE_START_TIME", start_tm);
		cJSON_AddNumberToObject(root, "DEVICE_END_TIME", end_tm);
		cJSON_AddNumberToObject(root, "DEVICE_WORK_TIME", work_tm);
        cJSON_AddNumberToObject(root, "RCG_CODE", rcg_code);
		cJSON_AddNumberToObject(root, "RCG_TCOUNT", tcount);  //总识别
		cJSON_AddNumberToObject(root, "RCG_CCOUNT", ccount);  //闭眼次数
		cJSON_AddNumberToObject(root, "RCG_ECOUNT", ecount);  //异常次数
		cJSON_AddNumberToObject(root, "IS_HEAT", is_heat);    //是否加热
		// cJSON_AddNumberToObject(root, "feeding_heat", get_device_para(DEVICE_PARA_FEEDING_HEAT));  //红光加热开关配置

		cJSON_AddStringToObject(root, "RID", rid);

		char * String = NULL;
		String = cJSON_PrintUnformatted(root);
		int msg_id;
		(void)msg_id;
		ESP_LOGI(TAG, "feedreport JSON: %s", String);  //打印完整JSON
		msg_id = esp_mqtt_client_publish(client, push_topic, String, strlen(String), 1, 0);
		ESP_LOGI(TAG, "sent publish successful, report msg:%s, msg_id=%d", levelstr, msg_id);
		cJSON_free(String);
		cJSON_Delete(root);
	}
}
//是否需要该函数，用于上传当前训练状态--linjun
void build_changemode(esp_mqtt_client_handle_t client, int level, char * rid)
{
	if (client != NULL)
	{
		char * levelstr = NULL;
		switch(level)
		{
			case 0x01:
				levelstr = "Feeding";//linjun
				break;
			case 0x02:
				levelstr = "Amblyopia";//linjun
				break;
			case 0x03:
				levelstr = "Heat";//加热模式
				break;
			default:
				levelstr = "Feeding";
				break;
		}
		
		char Rid[30];
		time_t rawtime;
		uint64_t current_time_ms = esp_timer_get_time()/1000;
		int ms_pre = current_time_ms%1000;
		snprintf(Rid, 30, "%s%ld%d", c_user_data, (long)time(&rawtime), ms_pre);
		
		cJSON *root = NULL;
		root = cJSON_CreateObject();
		cJSON_AddNumberToObject(root, "CMD", CMD_CHANGEMODE);
		cJSON_AddStringToObject(root, "VERSION", device_version);
		cJSON_AddStringToObject(root, "DEVICE_TYPE", DEVICE_TYPE);
		cJSON_AddStringToObject(root, "DEVICE_CODE", c_user_data);
		cJSON_AddNumberToObject(root, "DEVICE_TIME", time(NULL));
		cJSON_AddStringToObject(root, "DEVICE_MODE", levelstr);
		cJSON_AddStringToObject(root, "RID", rid==NULL?Rid:rid);

		char * String = NULL;
		String = cJSON_PrintUnformatted(root);
		int msg_id;
		(void)msg_id;
		msg_id = esp_mqtt_client_publish(client, push_topic, String, strlen(String), 0, 0);
		ESP_LOGI(TAG, "sent publish successful, change mode msg:%s, msg_id=%d", levelstr, msg_id);
		cJSON_free(String);
		cJSON_Delete(root);
	}
}


// void build_protocalversion(esp_mqtt_client_handle_t client)
// {
// 	if (client != NULL)
// 	{	
// 		char rid[30];
// 		time_t rawtime;
// 		uint64_t current_time_ms = esp_timer_get_time()/1000;
// 		int ms_pre = current_time_ms%1000;
// 		snprintf(rid, 30, "%s%lld%d", c_user_data, time(&rawtime), ms_pre);

// 		cJSON *root = NULL;
		
// 		root = cJSON_CreateObject();
// 		cJSON_AddNumberToObject(root, "CMD", CMD_PROTOCAL_VERSION);
// 		cJSON_AddStringToObject(root, "VERSION", device_version);
// 		cJSON_AddStringToObject(root, "DEVICE_TYPE", DEVICE_TYPE);
// 		cJSON_AddStringToObject(root, "DEVICE_CODE", c_user_data);
// 		cJSON_AddNumberToObject(root, "DEVICE_TIME", time(NULL));
// 		cJSON_AddStringToObject(root, "RID", rid);

// 		char * String = NULL;
// 		String = cJSON_PrintUnformatted(root);
// 		int msg_id;
// 		(void)msg_id;
// 		msg_id = esp_mqtt_client_publish(client, push_topic, String, strlen(String), 1, 0);
// 		ESP_LOGI(TAG, "sent publish successful, protocal version, msg_id=%d", msg_id);
// 		cJSON_free(String);
// 		cJSON_Delete(root);
// 	}
// }


static bool is_boot_ota_reply(cJSON *root)
{
	if (!s_boot_ota_waiting) {
		return false;
	}

	cJSON *rid = cJSON_GetObjectItem(root, "RID");
	if (rid && rid->valuestring && s_boot_version_rid[0] != '\0') {
		return strcmp(rid->valuestring, s_boot_version_rid) == 0;
	}

	int64_t now_ms = esp_timer_get_time() / 1000;
	return (now_ms - s_boot_version_send_time_ms) <= BOOT_OTA_REPLY_WINDOW_MS;
}

static void close_boot_ota_window(void)
{
	s_boot_ota_waiting = false;
	s_boot_version_rid[0] = '\0';
	s_boot_version_send_time_ms = 0;
}

// 版本号上传
void build_versionmessage(esp_mqtt_client_handle_t client)
{
	if (s_boot_version_sent) {
		ESP_LOGI(TAG, "开机版本同步已发送过，跳过本次MQTT重连版本上报");
		return;
	}

    if (client != NULL)
    {
        char rid[30];
        time_t rawtime;
        uint64_t current_time_ms = esp_timer_get_time()/1000;
        int ms_pre = current_time_ms%1000;
        snprintf(rid, 30, "%s%lld%d", c_user_data, time(&rawtime), ms_pre);

        cJSON *root = NULL;
        root = cJSON_CreateObject();

        // 创建PARAMS对象
        cJSON *params = cJSON_CreateObject();
        
        // 添加普通整型参数
        for(int i=0; i<DEVICE_PARA_MAX; i++){
			if (i == DEVICE_PARA_UDP_IP) //UDP IP参数
			{
				char* ip = get_udp_ip();
				if (ip != NULL) {
					cJSON_AddStringToObject(params, server_keys[DEVICE_PARA_UDP_IP], ip);
				}
			}else if(i == DEVICE_PARA_PICTURE) continue; //跳过图片参数
			else{
				int device_para;
				device_para = get_device_para(i);
				cJSON_AddNumberToObject(params, server_keys[i], device_para);
			} 
            
        }
        
        // 单独处理UDP IP地址
        

        uint8_t base_mac_[6];
        char mac_str[18];
        esp_efuse_mac_get_default(base_mac_);
        base_mac_[5] += 2; // 最后一位加2
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X", 
            base_mac_[0], base_mac_[1], base_mac_[2], 
            base_mac_[3], base_mac_[4], base_mac_[5]);
        cJSON_AddNumberToObject(root, "CMD", CMD_PROTOCAL_VERSION);
        cJSON_AddStringToObject(root, "VERSION", device_version);
        cJSON_AddStringToObject(root, "DEVICE_TYPE", DEVICE_TYPE);
        cJSON_AddStringToObject(root, "DEVICE_CODE", c_user_data);
        cJSON_AddStringToObject(root, "DEVICE_MAC", mac_str);
        cJSON_AddNumberToObject(root, "DEVICE_TIME", time(NULL));
        cJSON_AddItemToObject(root, "PARAMS", params);
        cJSON_AddStringToObject(root, "RID", rid);

        char * String = NULL;
        String = cJSON_PrintUnformatted(root);
        
        // 打印JSON字符串
        ESP_LOGI(TAG, "JSON Message: %s", String);

        int msg_id;
		(void)msg_id;
		msg_id = esp_mqtt_client_publish(client, push_topic, String, strlen(String), 0, 0);
		ESP_LOGI(TAG, "版本号信息发送成功, VersionMessage=%s, msg_id=%d", device_version, msg_id);
		if (msg_id != -1) {
			s_boot_version_sent = true;
			s_boot_ota_waiting = true;
			strncpy(s_boot_version_rid, rid, sizeof(s_boot_version_rid) - 1);
			s_boot_version_rid[sizeof(s_boot_version_rid) - 1] = '\0';
			s_boot_version_send_time_ms = current_time_ms;
			s_version_sync_key_status = key_get_status();
		}
        cJSON_free(String);
        cJSON_Delete(root);
    }
}



/* *********************** start：设备参数设置部分 *********************** */

/** 
 * 构造对服务器的回复
 * - 其他的 build 相关函数都在 mqtt.c 文件被调用，但是这个函数是在本文件被调用，所以不传入 client 参数
 * - 传入不是 key 本身，而是对应的索引
 **/

/** 
 * 调用 Flash 中的读写函数，根据成功与否调用 *参数设置* 并 *回复*
 * - 因为服务器端 key 设置和 flash 中不同，所以函数传入的是 index 
 **/

/* *********************** end：设备参数设置部分 *********************** */

// 在文件开头添加新的辅助函数声明
static void extract_and_upload_rcg_images(const char* json_data);
static void upload_historical_images_by_day_flags(cJSON* day_flags, int device_mode_id);
static void image_upload_task(void* parameter);
static void server_request_upload_task(void* parameter);  // 新增：服务器请求上传任务

// 图片上传重试参数
#define MAX_IMAGE_UPLOAD_RETRIES 3
#define IMAGE_UPLOAD_RETRY_DELAY_MS 2000

// 图片上传任务相关
static TaskHandle_t image_upload_task_handle = NULL;
static TaskHandle_t server_upload_task_handle = NULL;  // 新增：服务器上传任务句柄
typedef struct {
	cJSON* day_flags;
	int device_mode_id;
} image_upload_params_t;

// 新增：服务器请求上传参数结构
typedef struct {
	int ucode;
	char rid[64];  // 存储RID用于回复
} server_upload_params_t;





/**
 * @brief 从JSON数据中提取RCG_CODE并上传对应图片
 * @param json_data JSON格式的历史数据字符串
 */

/**
 * @brief 根据DAY_FLAG数组上传历史图片
 * @param day_flags DAY_FLAG JSON数组
 * @param device_mode_id 设备模式ID（1=哺光训练，2=弱视训练）
 */

/**
 * @brief 图片上传任务，在独立任务中执行图片上传避免栈溢出
 * @param parameter 传入的参数结构指针
 */

/**
 * @brief 服务器请求上传任务（209命令专用），在独立任务中执行图片上传避免MQTT任务栈溢出
 * @param parameter 传入的参数结构指针
 */
