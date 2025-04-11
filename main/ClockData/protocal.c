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
static void periodic_timer_callback(void* arg)
{
    int64_t time_since_boot = esp_timer_get_time();
    ESP_LOGI("example", "关闭蓝牙广播，Periodic timer called, time since boot: %lld us", (long long)time_since_boot);
	esp_blufi_adv_stop();
	esp_timer_stop(periodic_timer);
}
 
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

void build_unbind(esp_mqtt_client_handle_t client)
{
	if (client != NULL)
	{
		char rid[30];
		time_t rawtime;
		uint64_t current_time_ms = esp_timer_get_time()/1000;
		int ms_pre = current_time_ms%1000;
		snprintf(rid, 30, "%s%ld%d", c_user_data, (long)time(&rawtime), ms_pre);


		cJSON *root = NULL;
		// 创建PARAMS对象
		// cJSON *params = cJSON_CreateObject();
		// cJSON_AddStringToObject(params, "VOICE", "ON");
		// cJSON_AddStringToObject(params, "CAMERA", "ON");

		root = cJSON_CreateObject();
		cJSON_AddNumberToObject(root, "CMD", CMD_UNBIND);
		cJSON_AddStringToObject(root, "VERSION", device_version);
		cJSON_AddStringToObject(root, "DEVICE_TYPE", DEVICE_TYPE);
		cJSON_AddStringToObject(root, "DEVICE_CODE", c_user_data);
		cJSON_AddNumberToObject(root, "DEVICE_TIME", time(NULL));

		cJSON_AddStringToObject(root, "RID", rid);

		char * String = NULL;
		String = cJSON_PrintUnformatted(root);
		int msg_id;
		(void)msg_id;
		msg_id = esp_mqtt_client_publish(client, push_topic, String, strlen(String), 0, 0);
		ESP_LOGI(TAG, "sent publish successful, unbind, msg_id=%d", msg_id);
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

void get_status_storagemessage(){
	int32_t fdcount = 0;
	int32_t rscount = 0;
	fdcount = check_record_data(RECORD_FD);
	rscount = check_record_data(RECORD_RS);

	ESP_LOGI("STORAGE", "=== RECORD_FD = %lx", fdcount);
	ESP_LOGI("STORAGE", "=== RECORD_RS = %lx", rscount);

	// uint64_t current_time_ms = esp_timer_get_time()/1000;
	// int ms_pre = current_time_ms%1000;
	// printf("current_time_ms:%llu\n", current_time_ms);
	// printf("ms_pre:%d\n", ms_pre);
}

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

void clear_storage()
{
	int32_t fdcount = 0;
	int32_t rscount = 0;
	fdcount = check_record_data(RECORD_FD);
	rscount = check_record_data(RECORD_RS);

	if (fdcount == 0 && rscount == 0) {
		ESP_LOGI("STORAGE", "无历史数据");
		return;
	}

	if(fdcount != 0){
		for (int32_t i = 0; i < 32; i++) {               
			if (fdcount & 0x0001) {  								// 后续的处理是针对当天的数据               
				int32_t nums = get_day_record_data(i, RECORD_FD, databuf); 	// 获取当前的数据条数
				# if TEST_PRINT
				printf("get_day_record_data RECORD_FD i =%ld, fdcount = %lx, nums = %ld\n", i, fdcount, nums);
				for(int j=0; j<nums; j++){ 							// 针对当天的每一条数据进行处理
					printf("databuf[.]:%s\n", databuf[j]);
				}
				# endif
				(void)nums; // 标记使用，避免警告
				memset(databuf, 0, sizeof(databuf)); 				// 清空databuf
				clear_nvs_para_day(i, RECORD_FD);					// 清空flash中当天的数据
				clear_day_record_data(i, RECORD_FD);				// 修改存储标识符
			}
			fdcount = fdcount >> 1;
		}
	}
	if(rscount != 0){
		for (int32_t i = 0; i < 32; i++) {               
			if (rscount & 0x0001) {  								// 后续的处理是针对当天的数据               
				int32_t nums = get_day_record_data(i, RECORD_RS, databuf); 	// 获取当前的数据条数
				# if TEST_PRINT
				printf("get_day_record_data RECORD_RS i =%ld, rscount = %lx, nums = %ld\n", i, rscount, nums);
				for(int j=0; j<nums; j++){ 							// 针对当天的每一条数据进行处理
					printf("databuf[.]:%s\n", databuf[j]);
				}
				# endif
				(void)nums; // 标记使用，避免警告
				memset(databuf, 0, sizeof(databuf)); 				// 清空databuf
				clear_nvs_para_day(i, RECORD_RS);					// 清空flash中当天的数据
				clear_day_record_data(i, RECORD_RS);				// 修改存储标识符
			}
			rscount = rscount >> 1;
		}
	}

	# if TEST_PRINT
	printf("flash存储数据删除完成\n");
	# endif
}

void build_storagemessage(esp_mqtt_client_handle_t client)
{
	int32_t fdcount = 0;
	int32_t rscount = 0;
	fdcount = check_record_data(RECORD_FD);
	rscount = check_record_data(RECORD_RS);

	ESP_LOGI("STORAGE", "fdcount = %ld", fdcount);
	ESP_LOGI("STORAGE", "rscount = %ld", rscount);

	if (fdcount == 0 && rscount == 0) {
		ESP_LOGI("STORAGE", "Flash中无历史数据");
		return;
	}

	ESP_LOGI("STORAGE", "&&& RECORD_FD havedata = %lx\n", fdcount);
	ESP_LOGI("STORAGE", "&&& RECORD_RS havedata = %lx\n", rscount);

	cJSON *root = NULL;
	root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "CMD", CMD_STORAGE_MESSAGE);
    cJSON_AddStringToObject(root, "VERSION", device_version);
    cJSON_AddStringToObject(root, "DEVICE_TYPE", DEVICE_TYPE);
    cJSON_AddStringToObject(root, "DEVICE_CODE", c_user_data);
	// cJSON_AddNumberToObject(root, "DEVICE_MODE_ID", RECORD_FD);
	cJSON *ArrayJson  = NULL;
	ArrayJson  = cJSON_CreateArray();
	cJSON *ArrayFlagJson  = NULL;
	ArrayFlagJson = cJSON_CreateArray();

	if(fdcount != 0){
		int32_t nums = 0;
		int total = 0;
		for (int32_t i = 0; i < 32; i++) {               
			if (fdcount & 0x0001) {                    
				nums = get_day_record_data(i, RECORD_FD, databuf);
				# if TEST_PRINT
				printf("get_day_record_data RECORD_FD i =%ld, fdcount = %lx, nums = %ld\n", i, fdcount, nums);
				# endif
				for(int j=0; j<nums; j++){
					// printf("databuf[.]:%s\n", databuf[j]);
					cJSON *item = cJSON_CreateString(databuf[j]);
					cJSON_AddItemToArray(ArrayJson, item);
				}
				memset(databuf, 0, sizeof(databuf)); // 清空databuf
				cJSON_AddItemToArray(ArrayFlagJson, cJSON_CreateNumber(i));
				// clear_nvs_para_day(i, RECORD_FD);
				// printf("clear fd nvs\n");
				total += nums;
			}

			fdcount = fdcount >> 1;
			if (total > 10) {
				break;
			}
		}
		cJSON_AddNumberToObject(root, "DEVICE_MODE_ID", RECORD_FD);
		cJSON_AddItemToObject(root, "DAY_FLAG", ArrayFlagJson);
		cJSON_AddItemToObject(root, "HISTORY", ArrayJson);
		if (cJSON_GetArraySize(ArrayJson) == 0) {
			ESP_LOGW("STORAGE", "HISTORY empty, skip upload");
			cJSON_Delete(root);
			return;
		}

		char rid[30];
		time_t rawtime;
		uint64_t current_time_ms = esp_timer_get_time()/1000;
		int ms_pre = current_time_ms%1000;
		snprintf(rid, 30, "%s%ld%d", c_user_data, (long)time(&rawtime), ms_pre);
		cJSON_AddStringToObject(root, "RID", rid);

		/* MQTT上传 */
		char * String = NULL;
		String = cJSON_PrintUnformatted(root); 
		ESP_LOGI("STORAGE", "哺光历史数据上传:%s\n", String);
		int msg_id = esp_mqtt_client_publish((esp_mqtt_client_handle_t )client, push_topic, String, strlen(String), 1, 0);
		ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
		cJSON_free(String);
		cJSON_Delete(root);
		return;
	}
	

	if(rscount != 0){
		int32_t nums = 0;
		int total = 0;
		for (int32_t i = 0; i < 32; i++) {               
			if (rscount & 0x0001) {                    
				nums = get_day_record_data(i, RECORD_RS, databuf);
				# if TEST_PRINT
				printf("get_day_record_data RECORD_FD i =%ld, rscount = %lx, nums = %ld\n", i, fdcount, nums);
				# endif
				for(int j=0; j<nums; j++){
					cJSON *item = cJSON_CreateString(databuf[j]);
					cJSON_AddItemToArray(ArrayJson, item);
				}
				memset(databuf, 0, sizeof(databuf)); // 清空databuf
				cJSON_AddItemToArray(ArrayFlagJson, cJSON_CreateNumber(i));
				// clear_nvs_para_day(i, RECORD_RS);
				// printf("clear rs nvs\n");
				total += nums;
			}
			
			rscount = rscount >> 1;
			if (total > 10) {
				break;
			}
		}
		cJSON_AddNumberToObject(root, "DEVICE_MODE_ID", RECORD_RS);
		cJSON_AddItemToObject(root, "DAY_FLAG", ArrayFlagJson);
		cJSON_AddItemToObject(root, "HISTORY", ArrayJson);
		if (cJSON_GetArraySize(ArrayJson) == 0) {
			ESP_LOGW("STORAGE", "HISTORY empty, skip upload");
			cJSON_Delete(root);
			return;
		}

		char rid[30];
		time_t rawtime;
		snprintf(rid, 30, "%s%ld", c_user_data, (long)time(&rawtime));
		cJSON_AddStringToObject(root, "RID", rid);

		/* MQTT上传 */
		char * String = NULL;
		String = cJSON_PrintUnformatted(root); 
		// printf("String:%s\n", String);
		int msg_id = esp_mqtt_client_publish((esp_mqtt_client_handle_t )client, push_topic, String, strlen(String), 0, 0);
		ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
		cJSON_free(String);
		cJSON_Delete(root);
		return;
	}
	cJSON_AddNumberToObject(root, "DEVICE_MODE_ID", RECORD_RS);
	cJSON_AddItemToObject(root, "DAY_FLAG", ArrayFlagJson);
	cJSON_AddItemToObject(root, "HISTORY", ArrayJson);

}

/* *********************** start：设备参数设置部分 *********************** */

/** 
 * 构造对服务器的回复
 * - 其他的 build 相关函数都在 mqtt.c 文件被调用，但是这个函数是在本文件被调用，所以不传入 client 参数
 * - 传入不是 key 本身，而是对应的索引
 **/
static void build_device_parameters_process_reply(int index, int value, const char* result, char* rid)
{
	esp_mqtt_client_handle_t client = get_mqtt_client();

	if (client != NULL){
		char* key = server_keys[index];

		cJSON *root = NULL;
		root = cJSON_CreateObject();
		cJSON_AddNumberToObject(root, "CMD", CMD_DEVICE_PARAMETERS_REPLY);
		cJSON_AddStringToObject(root, "VERSION", device_version);
		cJSON_AddStringToObject(root, "DEVICE_TYPE", DEVICE_TYPE);
		cJSON_AddStringToObject(root, "DEVICE_CODE", c_user_data);
		cJSON_AddNumberToObject(root, "DEVICE_TIME", time(NULL));
		cJSON_AddStringToObject(root, "SET_KEY", key);
		cJSON_AddNumberToObject(root, "SET_VALUE", value);
		cJSON_AddStringToObject(root, "RES_CODE", result);
		cJSON_AddStringToObject(root, "RID", rid);

		char * String = NULL;
		String = cJSON_PrintUnformatted(root);
		int msg_id;
		(void)msg_id;

		msg_id = esp_mqtt_client_publish(client, push_topic, String, strlen(String), 0, 0);
		ESP_LOGI(TAG, "参数设置功能回复成功, VersionMessage=%s, msg_id=%d", device_version, msg_id);
		cJSON_free(String);
		cJSON_Delete(root);
	}
}

/** 
 * 调用 Flash 中的读写函数，根据成功与否调用 *参数设置* 并 *回复*
 * - 因为服务器端 key 设置和 flash 中不同，所以函数传入的是 index 
 **/
void func_device_parameters_process(int index_key_device_parameters, int data, char* rid)
{
	ESP_LOGI("DEVICE_PARAMETERS", "参数 index%d 设置指令进入处理函数", index_key_device_parameters);
	
	char* result = "SUCCESS";

	// 对于UDP IP地址，需要特殊处理
	if (index_key_device_parameters == DEVICE_PARA_UDP_IP) {
		// UDP IP地址是字符串类型，这里应该不会被调用
		result = "FAIL";
	} else {
		// 正常的整型参数
		esp_err_t err = set_device_parameters(index_key_device_parameters, data);
		if(err != ESP_OK){
			result = "FAIL";
		}else{
			result = "SUCCESS";
			#if 1//(实时更新小程序设置参数linjun)
			switch(index_key_device_parameters){
				case DEVICE_PARA_BGM: // 背景音乐关闭自动生效
					music_cycle();
					break;
				case DEVICE_PARA_VOL:	
					update_volume();//设置音量大小
					break;
				case DEVICE_PARA_FEED_P:
					update_feed_duty();//设置红光光强
					break;
				case DEVICE_PARA_FEED_T:
					update_feed_time();//设置红光时长
					break;
				case DEVICE_PARA_RUOSHI_T:
					update_color_time();//设置彩光时长
					break;
				case DEVICE_PARA_HEAT_T:
					update_heating_time();//设置加热时长
					break;
				case DEVICE_PARA_CAMERA:
					update_camera_status();//设置摄像头开关
					break;
				case DEVICE_PARA_40HZ:
					update_beep_status();//设置蜂鸣器开关
					ESP_LOGI("40HZ", "40HZ OK");
					break;
				case DEVICE_PARA_FEEDING_HEAT:
					// 红光加热开关参数，实时生效
					update_feeding_heat();
					break;
				case DEVICE_PARA_COUNT_LIMIT:
					set_count_limit(data);//设置剩余次数
					break;
				default:
					ESP_LOGE(TAG,"存在超出限制的指令 index");
					break;
			}
			#endif
		}
	}

	// 发送对应的回复消息
	build_device_parameters_process_reply(index_key_device_parameters, data, result, rid);
}

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


void process_msg(esp_mqtt_client_handle_t client, char * msg, int msg_len)
{
	if (client != NULL)
	{
		cJSON* root = cJSON_Parse(msg);
		if (!root) {
			ESP_LOGE("STORAGE", "Error before: [%s]", cJSON_GetErrorPtr());
			return;
		}

		// 获取json对象的值
		cJSON* CMD = cJSON_GetObjectItem(root, "CMD");
		cJSON* RID = cJSON_GetObjectItem(root, "RID");
		
		//设备状态--标记 该需求是否需要  用于上传当前训练状态
		if (CMD->valueint == 200)
		{
			int level =  key_get_status();
			build_changemode(client, level, RID->valuestring);
		}
		else if (CMD->valueint == 105) //unbind ack
		{
			cJSON* CODE = cJSON_GetObjectItem(root, "RES_CODE");
			
			if (strcmp(CODE->valuestring, "SUCCESS") == 0)
			{
				//处理完全解绑 (RID)
			}
		}
		else if (CMD->valueint == 103) //哺光上报回复
		{
			ESP_LOGI("STORAGE", "收到哺光上报回复");
			cJSON* res_code = cJSON_GetObjectItem(root, "RES_CODE");
			
			char *out = cJSON_Print(root);
			cJSON_free(out);
			
			if (strcmp(res_code->valuestring, "SUCCESS") == 0)
			{
				ESP_LOGI("STORAGE", "哺光上报回复的结果是成功，RES_CODE = SUCCESS");
				
				// 检查是否有待上传的图片
				if (pending_rcg_code > 0) {
					ESP_LOGI("STORAGE", "开始上传RCG_CODE为 %lu 的图片", pending_rcg_code);
					
					// 检查是否有对应的图片文件
					char file_paths[10][272]; // MAX_TEMP_IMAGES = 10
					int image_count = get_images_by_rcg_code(pending_rcg_code, file_paths, 10);
					
					if (image_count > 0) {
						ESP_LOGI(TAG, "找到 %d 张图片，开始上传", image_count);
						esp_err_t ret = start_upload_pictures_async(pending_rcg_code);
						if (ret == ESP_OK) {
							ESP_LOGI(TAG, "图片上传任务已启动");
							// 任务已启动，清除待上传标记
							pending_rcg_code = 0;
						} else {
							ESP_LOGE(TAG, "图片上传任务启动失败，错误码: %d", ret);
						}
					} else {
						ESP_LOGI(TAG, "未找到对应的图片文件");
						// 没有图片可上传，清除待上传标记
						pending_rcg_code = 0;
					}
				} else {
					ESP_LOGI("STORAGE", "没有待上传的图片");
				}
			} else {
				ESP_LOGI("STORAGE", "哺光上报失败，不上传图片");
				// 清除待上传的RCG_CODE
				pending_rcg_code = 0;
			}
		}
		else if (CMD->valueint == 101) //批量上报回复
		{
			ESP_LOGI("STORAGE", "收到批量上报回复");
			cJSON* res_code = cJSON_GetObjectItem(root, "RES_CODE");
			
			char *out = cJSON_Print(root);
			free(out);
			
			if (strcmp(res_code->valuestring, "SUCCESS") == 0)
			{
				ESP_LOGI("STORAGE", "批量上报回复的结果是成功，RES_CODE = SUCCESS");
				
				cJSON* arr_day_flag = cJSON_GetObjectItem(root, "DAY_FLAG");
				cJSON* dev_mode_id = cJSON_GetObjectItem(root, "DEVICE_MODE_ID");
				
				// 如果是哺光训练数据(RECORD_FD=1)，需要在清理数据前先上传对应的图片
				if (dev_mode_id && dev_mode_id->valueint == 1) {
					ESP_LOGI("STORAGE", "检查哺光训练历史数据对应的图片");
					
					// 检查是否已有图片上传任务在运行
					if (image_upload_task_handle != NULL) {
						ESP_LOGW(TAG, "图片上传任务已在运行，跳过本次上传");
					} else {
						// 创建参数结构
						image_upload_params_t* params = malloc(sizeof(image_upload_params_t));
						if (params) {
							// 深拷贝day_flags JSON数组
							params->day_flags = cJSON_Duplicate(arr_day_flag, 1);
							params->device_mode_id = dev_mode_id->valueint;
							
							// 创建独立的图片上传任务，避免在MQTT任务中进行耗时操作
							BaseType_t ret = xTaskCreate(
								image_upload_task,
								"image_upload",
								8192,  // 栈大小
								params,
								5,     // 优先级
								&image_upload_task_handle
							);
							
							if (ret != pdPASS) {
								ESP_LOGE(TAG, "创建图片上传任务失败");
								if (params->day_flags) cJSON_Delete(params->day_flags);
								free(params);
							} else {
								ESP_LOGI(TAG, "图片上传任务创建成功");
							}
						} else {
							ESP_LOGE(TAG, "分配图片上传参数内存失败");
						}
					}
				}
				
				int day_flag_size = cJSON_GetArraySize(arr_day_flag);

				for (int i = 0; i < day_flag_size; i++) {
					// ESP_LOGI("STORAGE", "DAY_FLAG: %d", cJSON_GetArrayItem(arr_day_flag, i)->valueint);
					cJSON *item = cJSON_GetArrayItem(arr_day_flag, i);
					clear_nvs_para_day(item->valueint, dev_mode_id->valueint); // clear_nvs_para_day(i, RECORD_FD);
					clear_day_record_data(item->valueint, dev_mode_id->valueint);
				}
				ESP_LOGI("STORAGE", "DEVICE_MODE_ID: %d", dev_mode_id->valueint);

				build_storagemessage(client);
			}
		}

		// 106：版本更新回复，调用 OTA 拉取 bin 文件
		else if(CMD->valueint == 106){
			if (!is_boot_ota_reply(root)) {
				ESP_LOGI("OTA", "收到非开机版本同步的OTA回复，忽略本次升级消息");
				cJSON_Delete(root);
				return;
			}
			close_boot_ota_window();

			KEY_FUNC_E current_key_status = key_get_status();
			if (s_version_sync_key_status != KEY_FUNC_OFF || current_key_status != KEY_FUNC_OFF) {
				ESP_LOGI("OTA", "版本同步时或当前设备处于工作状态，跳过OTA升级(sync:%d, current:%d)",
						 s_version_sync_key_status, current_key_status);
				cJSON_Delete(root);
				return;
			}

			cJSON* res_code = cJSON_GetObjectItem(root, "RES_CODE");
			
			char *out = cJSON_Print(root);
			free(out);
			
			if (strcmp(res_code->valuestring, "SUCCESS") == 0)
			{
				ESP_LOGI("STORAGE", "RES_CODE = SUCCESS\n");
				
				cJSON* package = cJSON_GetObjectItem(root, "PACKAGE");
				char* str_package = package->valuestring;
				# if TEST_PRINT
				printf("PACKAGE: %s\n", str_package);
				printf("packpage_status:%d\n", strcmp(str_package, ""));
				# endif

				if (strcmp(str_package, "") != 0){
					ESP_LOGI("OTA", "执行OTA升级程序");

					// 发送语音指令
					
					play_voice(OTA_START); // 播放升级失败提示音
					vTaskDelay(1000 / portTICK_PERIOD_MS);

					OTA_function(str_package);
				} else {
					ESP_LOGI("OTA","当前已经是最新版本，无需升级");
				}
			}
		}

		// 201：设备蓝牙控制
		else if(CMD->valueint == 201){
			char *out = cJSON_Print(root);
			free(out);

			cJSON* JSON_duration = cJSON_GetObjectItem(root, "DURATION");
			int duration = JSON_duration->valueint;

			// 打开蓝牙广播
       	 	mine_esp_blufi_adv_start();    

			// 开一个软件定时器
			const esp_timer_create_args_t periodic_timer_args = {
				.callback = &periodic_timer_callback,
				.name = "periodic" /* name is optional, but may help identify the timer when debugging */
			};

			ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));/* The timer has been created but is not running yet */
			// ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer,1*1000*1000)); // 定时器初始单位为us
			
			ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer,duration*1000*1000)); // 定时器初始单位为us
		}

		// 299：设备自动解绑
		else if(CMD->valueint == 299){
			char *out = cJSON_Print(root);
			free(out);

			cJSON* JSON_unbind_mode = cJSON_GetObjectItem(root, "IS_RETAIN_DATA");
			int unbind_mode = JSON_unbind_mode->valueint;

			wifi_reset();
			
			if(unbind_mode == 0)
			{
				clear_storage();
			}
			
			esp_restart();
		}

		// 209：设备控制参数修改
		else if(CMD->valueint == 209){

			ESP_LOGI("DEVICE_PARAMETERS", "接收到设备参数设置指令");

			char *out = cJSON_Print(root);
			free(out);

			// 获取 key
			cJSON* JSON_set_key = cJSON_GetObjectItem(root, "SET_KEY");
			char* str_set_key = JSON_set_key->valuestring;
			// 获取 rid
			cJSON* JSON_rid = cJSON_GetObjectItem(root, "RID");
			char* str_rid = JSON_rid->valuestring;

			// 特殊处理logs参数（用于日志上报
			if (strcmp(str_set_key, "logs") == 0) {
				cJSON* JSON_set_data = cJSON_GetObjectItem(root, "SET_VALUE");
				int set_data = (JSON_set_data && cJSON_IsNumber(JSON_set_data)) ? JSON_set_data->valueint : 0;

				ESP_LOGI("DEVICE_PARAMETERS", "收到209日志请求，logs=%d", set_data);

				if (set_data != 1) {
					ESP_LOGW("DEVICE_PARAMETERS", "logs参数值不为1，忽略");
					return;
				}

				// 从NVS读取日志信息（直接回传，不再解析/格式化）
				char logs_json[2048] = {0};
				char logs_json_fmt[2048] = {0};
				esp_err_t ret = get_device_logs_from_nvs(logs_json, sizeof(logs_json));
				if (ret != ESP_OK || logs_json[0] == '\0') {
					return;
				}

				// 仅对字符串数组做换行美化（不改变JSON结构）
				size_t out_len = 0;
				for (size_t i = 0; logs_json[i] != '\0' && out_len + 1 < sizeof(logs_json_fmt); i++) {
					if (logs_json[i] == ',' && logs_json[i + 1] == '\"' &&
					    out_len + 2 < sizeof(logs_json_fmt)) {
						logs_json_fmt[out_len++] = ',';
						logs_json_fmt[out_len++] = '\n';
						continue;
					}
					logs_json_fmt[out_len++] = logs_json[i];
				}
				logs_json_fmt[out_len] = '\0';

				esp_mqtt_client_handle_t client = get_mqtt_client();
				if (client != NULL) {
					cJSON *reply_root = cJSON_CreateObject();
					cJSON_AddNumberToObject(reply_root, "CMD", CMD_DEVICE_PARAMETERS_REPLY);
					cJSON_AddStringToObject(reply_root, "VERSION", device_version);
					cJSON_AddStringToObject(reply_root, "DEVICE_TYPE", DEVICE_TYPE);
					cJSON_AddStringToObject(reply_root, "DEVICE_CODE", c_user_data);
					cJSON_AddNumberToObject(reply_root, "DEVICE_TIME", time(NULL));
					cJSON_AddStringToObject(reply_root, "SET_KEY", "logs");
					cJSON_AddStringToObject(reply_root, "SET_VALUE", logs_json_fmt[0] ? logs_json_fmt : logs_json);
					cJSON_AddStringToObject(reply_root, "RES_CODE", "SUCCESS");
					cJSON_AddStringToObject(reply_root, "RID", str_rid);

					char *reply_string = cJSON_PrintUnformatted(reply_root);
					if (reply_string) {
						ESP_LOGI(TAG, "日志上报回复消息: %s", reply_string);
						int msg_id = esp_mqtt_client_publish(client, push_topic, reply_string, strlen(reply_string), 0, 0);
						ESP_LOGI(TAG, "日志上报回复成功, msg_id=%d", msg_id);
						cJSON_free(reply_string);
						if (msg_id != -1) {
							clear_device_logs_from_nvs();
							ESP_LOGI("DEVICE_PARAMETERS", "日志已清除");
						}
					}
					cJSON_Delete(reply_root);
				}
				return;
			}
			
			// 特殊处理ucode参数（用于图片上传）
			if (strcmp(str_set_key, "ucode") == 0) {
				cJSON* JSON_set_data = cJSON_GetObjectItem(root, "SET_VALUE");
				int set_data = JSON_set_data->valueint;
				
				ESP_LOGI("DEVICE_PARAMETERS", "收到209上传请求，ucode=%d", set_data);
				
				// 检查是否已有服务器上传任务在运行
				if (server_upload_task_handle != NULL) {
					ESP_LOGW(TAG, "服务器上传任务已在运行，拒绝新请求");
					build_device_parameters_process_reply(9, set_data, "BUSY", str_rid);
					return;
				}
				
				// 创建参数结构
				server_upload_params_t* params = malloc(sizeof(server_upload_params_t));
				if (params) {
					params->ucode = set_data;
					strncpy(params->rid, str_rid, sizeof(params->rid) - 1);
					params->rid[sizeof(params->rid) - 1] = '\0';
					
					// 创建独立的服务器上传任务，避免在MQTT任务中进行耗时操作
					BaseType_t ret = xTaskCreate(
						server_request_upload_task,
						"server_upload",
						8192,  // 栈大小
						params,
						5,     // 优先级
						&server_upload_task_handle
					);
					
					if (ret != pdPASS) {
						ESP_LOGE(TAG, "创建服务器上传任务失败");
						build_device_parameters_process_reply(9, set_data, "FAIL", str_rid);
						free(params);
					} else {
						ESP_LOGI(TAG, "服务器上传任务创建成功");
						// 不在这里回复，在任务完成后回复
					}
				} else {
					ESP_LOGE(TAG, "分配服务器上传参数内存失败");
					build_device_parameters_process_reply(9, set_data, "FAIL", str_rid);
				}
				return;
			}
			
			// 特殊处理UDP IP地址
			if (strcmp(str_set_key, "udp_ip") == 0) {
				cJSON* JSON_set_data = cJSON_GetObjectItem(root, "SET_VALUE");
				if (JSON_set_data->type == cJSON_String) {
					esp_err_t ret = set_udp_ip(JSON_set_data->valuestring);
					const char* result = (ret == ESP_OK) ? "SUCCESS" : "FAIL";
					char ip_str[16];
					snprintf(ip_str, sizeof(ip_str), "%s", JSON_set_data->valuestring);
					ESP_LOGI(TAG, "设置UDP IP: %s, 结果: %s", ip_str, result);
					// 这里使用0作为data值，因为实际的IP是字符串
					build_device_parameters_process_reply(DEVICE_PARA_UDP_IP, 0, result, str_rid);
				} else {
					ESP_LOGE(TAG, "UDP IP必须是字符串类型");
					build_device_parameters_process_reply(DEVICE_PARA_UDP_IP, 0, "FAIL", str_rid);
				}
				return;
			}

			// 特殊处理count_limit租赁次数参数
			if (strcmp(str_set_key, "count_limit") == 0) {
				cJSON* JSON_set_value = cJSON_GetObjectItem(root, "SET_VALUE");
				if (JSON_set_value && cJSON_IsObject(JSON_set_value)) {
					cJSON* type_item = cJSON_GetObjectItem(JSON_set_value, "type");
					cJSON* count_item = cJSON_GetObjectItem(JSON_set_value, "count");
					
					if (type_item && count_item && 
						(cJSON_IsString(type_item) || cJSON_IsNumber(type_item)) &&
						(cJSON_IsString(count_item) || cJSON_IsNumber(count_item))) {
						
						int type = cJSON_IsString(type_item) ? atoi(type_item->valuestring) : type_item->valueint;
						int count = cJSON_IsString(count_item) ? atoi(count_item->valuestring) : count_item->valueint;
						int final_count = 0;
						
						if (type == 1) {
							// 累加模式：从NVS读取剩余次数，然后进行累加
							int current_count = get_count_limit();
							final_count = current_count + count;
							ESP_LOGI("DEVICE_PARAMETERS", "租赁次数累加: %d + %d = %d", current_count, count, final_count);
						} else if (type == 2) {
							// 覆盖模式：直接覆盖NVS中的剩余次数
							final_count = count;
							ESP_LOGI("DEVICE_PARAMETERS", "租赁次数覆盖: %d", final_count);
						} else {
							ESP_LOGE("DEVICE_PARAMETERS", "无效的租赁次数设置类型: %d", type);
							build_device_parameters_process_reply(DEVICE_PARA_COUNT_LIMIT, 0, "FAIL", str_rid);
							return;
						}
						
						// 设置最终的次数
						esp_err_t ret = set_count_limit(final_count);
						const char* result = (ret == ESP_OK) ? "SUCCESS" : "FAIL";
						
						// 构造CMD 309响应消息
						build_device_parameters_process_reply(DEVICE_PARA_COUNT_LIMIT, final_count, result, str_rid);
						
						ESP_LOGI("DEVICE_PARAMETERS", "租赁次数设置完成，结果: %s, 最终次数: %d", result, final_count);
					} else {
						ESP_LOGE("DEVICE_PARAMETERS", "count_limit参数格式错误");
						build_device_parameters_process_reply(DEVICE_PARA_COUNT_LIMIT, 0, "FAIL", str_rid);
					}
				} else {
					ESP_LOGE("DEVICE_PARAMETERS", "count_limit的SET_VALUE必须是对象类型");
					build_device_parameters_process_reply(DEVICE_PARA_COUNT_LIMIT, 0, "FAIL", str_rid);
				}
				return;
			}
			
			// 处理普通数值型参数
			cJSON* JSON_set_data = cJSON_GetObjectItem(root, "SET_VALUE");
			int set_data = JSON_set_data->valueint;
			
			printf("key:%s, data:%d\n", str_set_key, set_data);
			// 同步 server 和 Flash 的 key
			for(int i=0; i<DEVICE_PARA_MAX; i++){  // 只比较普通参数
				if((strcmp(str_set_key, server_keys[i])) == 0){
					func_device_parameters_process(i, set_data, str_rid);
					break;
				}
			}
		}

		else {
			ESP_LOGI(TAG, "NO PROCESS CMD %d", CMD->valueint);
		}
		
		// 释放内存
		cJSON_Delete(root);
	}
}



/**
 * @brief 从JSON数据中提取RCG_CODE并上传对应图片
 * @param json_data JSON格式的历史数据字符串
 */
static void extract_and_upload_rcg_images(const char* json_data)
{
	if (!json_data) {
		ESP_LOGW(TAG, "历史数据为空，跳过图片上传");
		return;
	}
	
	cJSON* data_root = cJSON_Parse(json_data);
	if (!data_root) {
		ESP_LOGE(TAG, "解析历史数据JSON失败: %s", json_data);
		return;
	}
	
	cJSON* rcg_code_item = cJSON_GetObjectItem(data_root, "RCG_CODE");
	if (rcg_code_item && cJSON_IsNumber(rcg_code_item)) {
		uint32_t rcg_code = (uint32_t)rcg_code_item->valueint;
		
		// 跳过无效的RCG_CODE
		if (rcg_code == 0) {
			ESP_LOGI(TAG, "RCG_CODE为0，跳过图片上传");
			cJSON_Delete(data_root);
			return;
		}
		
		ESP_LOGI(TAG, "提取到RCG_CODE: %lu", rcg_code);
		
		// 检查是否有对应的图片
		char file_paths[MAX_TEMP_IMAGES][272];
		int image_count = get_images_by_rcg_code(rcg_code, file_paths, MAX_TEMP_IMAGES);
		
		if (image_count > 0) {
			ESP_LOGI(TAG, "找到 %d 张RCG_CODE为 %lu 的历史图片，开始上传", image_count, rcg_code);
			
			// 带重试的图片上传
			esp_err_t ret = ESP_FAIL;
			for (int retry = 0; retry < MAX_IMAGE_UPLOAD_RETRIES; retry++) {
				ret = upload_pictures(rcg_code);
				if (ret == ESP_OK) {
					ESP_LOGI(TAG, "RCG_CODE %lu 的历史图片上传成功", rcg_code);
					break;
				} else {
					ESP_LOGW(TAG, "RCG_CODE %lu 的历史图片上传失败，重试 %d/%d，错误码: %d", 
							rcg_code, retry + 1, MAX_IMAGE_UPLOAD_RETRIES, ret);
					
					if (retry < MAX_IMAGE_UPLOAD_RETRIES - 1) {
						// 等待后再重试
						vTaskDelay(pdMS_TO_TICKS(IMAGE_UPLOAD_RETRY_DELAY_MS));
						
						// 重新检查网络状态
						if (get_wifi_status() != WIFI_STATE_CONNECTED) {
							ESP_LOGW(TAG, "网络连接丢失，停止重试RCG_CODE %lu", rcg_code);
							break;
						}
					}
				}
			}
			
			if (ret != ESP_OK) {
				ESP_LOGE(TAG, "RCG_CODE %lu 的历史图片上传最终失败，已重试 %d 次", rcg_code, MAX_IMAGE_UPLOAD_RETRIES);
			}
		} else {
			ESP_LOGI(TAG, "未找到RCG_CODE为 %lu 的历史图片文件", rcg_code);
		}
	} else {
		ESP_LOGW(TAG, "历史数据中未找到有效的RCG_CODE字段: %s", json_data);
	}
	
	cJSON_Delete(data_root);
}

/**
 * @brief 根据DAY_FLAG数组上传历史图片
 * @param day_flags DAY_FLAG JSON数组
 * @param device_mode_id 设备模式ID（1=哺光训练，2=弱视训练）
 */
static void upload_historical_images_by_day_flags(cJSON* day_flags, int device_mode_id)
{
	if (!day_flags || !cJSON_IsArray(day_flags)) {
		ESP_LOGW(TAG, "DAY_FLAG数组无效，跳过历史图片上传");
		return;
	}
	
	// 检查网络状态
	if (get_wifi_status() != WIFI_STATE_CONNECTED) {
		ESP_LOGW(TAG, "网络未连接，跳过历史图片上传");
		return;
	}
	
	// 只处理哺光训练数据
	if (device_mode_id != RECORD_FD) {
		ESP_LOGI(TAG, "不是哺光训练数据，无需上传图片");
		return;
	}
	
	int day_flag_size = cJSON_GetArraySize(day_flags);
	ESP_LOGI(TAG, "准备处理 %d 天的历史图片上传", day_flag_size);
	
	// 遍历每一天的数据
	for (int i = 0; i < day_flag_size; i++) {
		cJSON* day_item = cJSON_GetArrayItem(day_flags, i);
		if (!day_item || !cJSON_IsNumber(day_item)) {
			continue;
		}
		
		int day = day_item->valueint;
		ESP_LOGI(TAG, "处理第 %d 天的历史数据", day);
		
		// 动态分配内存避免栈溢出
		char (*temp_databuf)[DATA_CNTS] = malloc(10 * DATA_CNTS);
		if (!temp_databuf) {
			ESP_LOGE(TAG, "第 %d 天分配内存失败", day);
			continue;
		}
		
		// 重新读取这一天的历史数据
		int32_t nums = get_day_record_data(day, RECORD_FD, temp_databuf);
		
		if (nums <= 0) {
			ESP_LOGW(TAG, "第 %d 天没有找到历史数据", day);
			free(temp_databuf);
			continue;
		}
		
		ESP_LOGI(TAG, "第 %d 天找到 %ld 条历史记录", day, nums);
		
		// 处理这一天的每条记录
		for (int j = 0; j < nums; j++) {
			ESP_LOGI(TAG, "处理历史记录 %d/%ld: %s", j+1, nums, temp_databuf[j]);
			extract_and_upload_rcg_images(temp_databuf[j]);
		}
		
		// 释放内存
		free(temp_databuf);
	}
	
	ESP_LOGI(TAG, "历史图片上传处理完成");
}

/**
 * @brief 图片上传任务，在独立任务中执行图片上传避免栈溢出
 * @param parameter 传入的参数结构指针
 */
static void image_upload_task(void* parameter)
{
	image_upload_params_t* params = (image_upload_params_t*)parameter;
	
	if (!params) {
		ESP_LOGE(TAG, "图片上传任务参数为空");
		vTaskDelete(NULL);
		return;
	}
	
	ESP_LOGI(TAG, "图片上传任务开始执行");
	
	// 执行图片上传
	upload_historical_images_by_day_flags(params->day_flags, params->device_mode_id);
	
	// 清理资源
	if (params->day_flags) {
		cJSON_Delete(params->day_flags);
	}
	free(params);
	
	// 清除任务句柄
	image_upload_task_handle = NULL;
	
	ESP_LOGI(TAG, "图片上传任务完成，任务即将结束");
	
	// 删除自己
	vTaskDelete(NULL);
}

/**
 * @brief 服务器请求上传任务（209命令专用），在独立任务中执行图片上传避免MQTT任务栈溢出
 * @param parameter 传入的参数结构指针
 */
static void server_request_upload_task(void* parameter)
{
	server_upload_params_t* params = (server_upload_params_t*)parameter;
	
	if (!params) {
		ESP_LOGE(TAG, "服务器上传任务参数为空");
		vTaskDelete(NULL);
		return;
	}
	
	ESP_LOGI(TAG, "服务器请求上传任务开始执行，ucode=%d", params->ucode);
	
	// 执行图片上传
	esp_err_t ret = upload_all_pictures_for_server(params->ucode);
	
	// 发送回复消息
	const char* result = (ret == ESP_OK) ? "SUCCESS" : "FAIL";
	build_device_parameters_process_reply(9, params->ucode, result, params->rid);
	
	ESP_LOGI(TAG, "服务器请求上传任务完成，结果: %s", result);
	
	// 清理资源
	free(params);
	
	// 清除任务句柄
	server_upload_task_handle = NULL;
	
	// 删除自己
	vTaskDelete(NULL);
}
