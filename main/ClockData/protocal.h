#ifndef _PROTOCAL_H_
#define _PROTOCAL_H_

#include "cJSON.h"
#include "My_system.h"
/*
心跳
{
	"CMD": 0,
	"VERSION":"V1.00",
	"DEVICE_TYPE":"设备类型",
	"DEVICE_CODE":"设备序列号",
	"DEVICE_TIME":1678679106,//unix时间戳
	"RID":"11"
}

存储数据
{
	"CMD": 1,
	"VERSION": "V1.00",
	"DEVICE_TYPE": "Type",
	"DEVICE_CODE": "SN000000000000",
	"HISTORY":
	[
		{
		    "CMD": 3,
			"DEVICE_MODE": "Feeding1", //Feeding1、Feeding2、Feeding3、Bates、Amblyopia
		    "DEVICE_START_TIME": 1678679106,
		    "DEVICE_END_TIME": 1678679106,
		    "DEVICE_WORK_TIME": 3,
		},
		{
		    "CMD": 3,
			"DEVICE_MODE": "Bates", //Feeding1、Feeding2、Feeding3、Bates、Amblyopia
		    "DEVICE_START_TIME": 1678679106,
		    "DEVICE_END_TIME": 1678679106,
		    "DEVICE_WORK_TIME": 10,
		},
	],
	"RID": "11"
}

哺光上报，每次哺光训练相当于一次开关机
{
    "CMD": 3,
    "VERSION": "V1.00",
    "DEVICE_TYPE": "Type",
    "DEVICE_CODE": "SN000000000000",
    "DEVICE_MODE": "Feeding1", //Feeding1、Feeding2、Feeding3、Bates、Amblyopia
	"DEVICE_START_TIME": 1678679106,
	"DEVICE_END_TIME": 1678679106,
	"DEVICE_WORK_TIME": 3,
    "RID": "11"
}

设备按键模式切换：哺光训练1档/2档/3档、贝茨训练、弱视训练
{
	"CMD": 4,
	"VERSION": "V1.00",
    "DEVICE_TYPE": "Type",
    "DEVICE_CODE": "SN000000000000",
	"DEVICE_MODE":"Amblyopia",//Feeding1、Feeding2、Feeding3、Bates、Amblyopia
	"DEVICE_TIME":1678679106,//unix时间戳
	"RID": "11"
}

设备解绑
{
	"CMD": 5,
	"VERSION": "V1.00",
    "DEVICE_TYPE": "Type",
    "DEVICE_CODE": "SN000000000000",
	"DEVICE_TIME":1678679106,
	"RID": "11"
}

开机同步协议版本号
{
	"CMD": 6,
	"VERSION": "V1.00",
    "DEVICE_TYPE": "Type",
    "DEVICE_CODE": "SN000000000000",
	"DEVICE_TIME":1678679106,
	"RID": "11"
}

服务端回复：

res存储数据回复
{
	"CMD": 101,
	"VERSION": "V1.00",
    "DEVICE_TYPE": "Type",
    "DEVICE_CODE": "SN000000000000",
	"RES_CODE": "SUCCESS",
	"RID": "11"
}

哺光上报回复(暂不实现)
{
	"CMD": 103,
    "VERSION": "V1.00",
    "DEVICE_TYPE": "Type",
    "DEVICE_CODE": "SN000000000000",
	"RES_CODE": "SUCCESS",
    "RID": "11"
}

res设备解绑回复
{
	"CMD":105,
	"VERSION": "V1.00",
    "DEVICE_TYPE": "Type",
   	"DEVICE_CODE": "SN000000000000",
	"RES_CODE": "SUCCESS",
	"RID": "11"
}

服务端上报：

获取设备状态
{
	"CMD": 200,
	"VERSION": "V1.00",
    "DEVICE_TYPE": "Type",
    "DEVICE_CODE": "SN000000000000",
	"RID": "11"
}

设备端回复：

res：获取设备状态回复
{
	"CMD": 300,
	"VERSION": "V1.00",
    "DEVICE_TYPE": "Type",
    "DEVICE_CODE": "SN000000000000",
	"DEVICE_MODE":"Amblyopia",//Feeding1、Feeding2、Feeding3、Bates、Amblyopia
	"RES_CODE": "SUCCESS",
	"RID": "11"
}
*/

#include "mqtt_client.h"

#define CMD_HEARTBEAT 0
#define CMD_FEEDREPORT 3
#define CMD_CHANGEMODE 4
#define CMD_UNBIND 5
#define CMD_PROTOCAL_VERSION 6
#define CMD_STORAGE_MESSAGE 1
#define CMD_DEVICE_PARAMETERS_REPLY 309
#define DEVICE_TYPE "LAMP"
#define TOPIC "/topic/bgy/"

void build_heartbeat(esp_mqtt_client_handle_t client);
void build_feedreport(esp_mqtt_client_handle_t client, int level, int start_tm, int end_tm, int work_tm, int rcg_code, uint8_t tcount, uint8_t ccount, uint8_t ecount, uint8_t is_heat);
void build_changemode(esp_mqtt_client_handle_t client, int level, char * rid);
void build_unbind(esp_mqtt_client_handle_t client);
void build_protocalversion(esp_mqtt_client_handle_t client);
void build_storagemessage(esp_mqtt_client_handle_t client);
void build_versionmessage(esp_mqtt_client_handle_t client);
void process_msg(esp_mqtt_client_handle_t client, char * msg, int msg_len);
uint8_t * get_mac();
void read_mac();
void build_pp_topic();
char * get_pub_tpc();
char * get_sub_tpc();

void get_status_storagemessage();
char * get_device_str();
char * g_get_ble_mac();
void clear_storage();

#endif
