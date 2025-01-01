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
void clear_nvs_para_day(int day, RECORD_TYPE_E type)
{
    nvs_handle_t my_handle;
    esp_err_t err;  
    int32_t para_value;
    char* partition = NULL;

    switch (type) {
        case RECORD_FD:
        partition = FD_STORAGE_PARTITION;
        break;
        case RECORD_RS:
        partition = RS_STORAGE_PARTITION;
        break;
        default:
        return;
    }

    ESP_LOGI("STORAGE", "clear_nvs_para_day()函数执行");      
    err = nvs_open_from_partition(partition, PARA_SPACENAME, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE("STORAGE", "ERROR...update_nvs_para_day..open_from_partition!!!");
    } else {
        //int32_t temp = ~(1 << (day - 1));//存储位从0开始，day从1开始，所以需要注意这点
        int32_t temp = ~(1 << day);//存储位改成从1开始，day从1开始，所以需要注意这点
        err = nvs_get_i32(my_handle, PARA_DAYKEY, &para_value); 
        if(err == ESP_OK){ ESP_LOGI("STORAGE", "nvs_get_i32 PARA_DAYKEY Done");} 
            else {ESP_LOGE("STORAGE", "nvs_get_i32 PARA_DAYKEY Failed!");}
        ESP_LOGI("STORAGE", "old para_value = %lx, temp = %lx", para_value, temp);
        para_value = (para_value & temp);
        ESP_LOGI("STORAGE", "day = %d, new para_value = %lx\n",day, para_value);
        err = nvs_set_i32(my_handle, PARA_DAYKEY, para_value); 
        if(err == ESP_OK){ ESP_LOGI("STORAGE", "nvs_set_i32 PARA_DAYKEY Done");} 
            else {ESP_LOGE("STORAGE", "nvs_set_i32 PARA_DAYKEY Failed!");}
        err = nvs_commit(my_handle);
        if(err == ESP_OK){ ESP_LOGI("STORAGE", "nvs_commit Done");} 
            else {ESP_LOGE("STORAGE", "nvs_commit Failed!");}
    }
	ESP_LOGI("STORAGE", "clear_nvs_para_day()函数执行");  
    nvs_close(my_handle);
}
 
/*
update_nvs_para_day
该函数应用时要注意，这里的day输入必须是实际日期的比如1号开始，比如1号、31号，
在应用过程中如果是在按位存储中要注意，按位存储是从0位开始表示1号，则不能输入0要表示1号，而应该+1
*/
static void update_nvs_para_day(int day, RECORD_TYPE_E type)
{
    nvs_handle_t my_handle;
    esp_err_t err;  
    int32_t para_value;
    char* partition = NULL;

    switch (type) {
        case RECORD_FD:
        partition = FD_STORAGE_PARTITION;
        break;
        case RECORD_RS:
        partition = RS_STORAGE_PARTITION;
        break;
        default:
        return;
    }

    // printf("enter update_nvs_para_day\n");

    err = nvs_open_from_partition(partition, PARA_SPACENAME, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "ERROR...update_nvs_para_day..open_from_partition!!!");
    } else {
        ESP_LOGI("NVS", "opening NVS handle Done");
        // Read
        ESP_LOGI("NVS", "Get PARA_DAYKEY from NVS ... ");
        //先获取当前参数区数值
        err = nvs_get_i32(my_handle, PARA_DAYKEY, &para_value); 
        if(err == ESP_OK){ ESP_LOGI("STORAGE", "nvs_get_i32 PARA_DAYKEY Done");} 
            else {ESP_LOGE("STORAGE", "nvs_get_i32 PARA_DAYKEY Failed!");}
        //把新增要存储的数据更新到参数区中，用异或运算
        //每天对应一个位
        if (day == 0) {   //当day为0时，表示把所有天数的记录都清掉
            err = nvs_set_i32(my_handle, PARA_DAYKEY, 0); 
            if(err == ESP_OK){ ESP_LOGI("STORAGE", "nvs_set_i32 PARA_DAYKEY 0 Done");} 
                else { ESP_LOGE("STORAGE", "nvs_set_i32 PARA_DAYKEY 0 Failed!"); }
        } else {
            //int32_t temp = (1 << (day - 1));//存储位从0开始，day从1开始，所以需要注意这点
            int32_t temp = (1 << day);//存储位改成从1开始，day从1开始，所以需要注意这点
            ESP_LOGI("STORAGE", "old para_value = %lx, temp = %lx", para_value, temp);
            para_value = (para_value | temp);
            ESP_LOGI("STORAGE", "day = %d, new para_value = %lx",day, para_value);
            err = nvs_set_i32(my_handle, PARA_DAYKEY, para_value); 
            if(err == ESP_OK){ ESP_LOGI("STORAGE", "nvs_set_i32 PARA_DAYKEY Done");} 
                else { ESP_LOGE("STORAGE", "nvs_set_i32 PARA_DAYKEY Failed!"); }
        }
        err = nvs_commit(my_handle);
        if(err == ESP_OK){ ESP_LOGI("STORAGE", "nvs_commit Done");} 
            else { ESP_LOGE("STORAGE", "nvs_commit Failed!"); }
    }
    nvs_close(my_handle);
}


/*
检查存储数据
返回:有存储数据的天数标示，比如0x0003 表示第1、2天有存储数据
*/
int32_t check_record_data(RECORD_TYPE_E type)
{
    nvs_handle_t my_handle;
    esp_err_t err;  
    int32_t para_value;
    char* partition = NULL;

    switch (type) {
        case RECORD_FD:
        partition = FD_STORAGE_PARTITION;
        break;
        case RECORD_RS:
        partition = RS_STORAGE_PARTITION;
        break;
        default:
        return 0;
    }

    err = nvs_open_from_partition(partition, PARA_SPACENAME, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE("STORAGE", "ERROR...check_record_data..open_from_partition!!!");
        return 0;
    } else {
        ESP_LOGI("STORAGE", "opening NVS handle Done1");
        // Read
        ESP_LOGI("STORAGE", "Get PARA_DAYKEY from NVS ... ");
        // 先获取当前参数区数值
        err = nvs_get_i32(my_handle, PARA_DAYKEY, &para_value); 
        if(err == ESP_OK){ ESP_LOGI("STORAGE", "nvs_get_i32 PARA_DAYKEY Done");} 
            else { ESP_LOGE("STORAGE", "nvs_get_i32 PARA_DAYKEY Failed!"); }
        if (para_value != 0) {
            ESP_LOGI("STORAGE", "para_value = %lx", para_value);
        }

        err = nvs_commit(my_handle);
        if(err == ESP_OK){ ESP_LOGI("STORAGE", "nvs_commit Done");} 
            else { ESP_LOGE("STORAGE", "nvs_commit Failed!"); }
        nvs_close(my_handle);
        return para_value;  //返回的参数是按具有存储数据天的标示 比如0x0003 表示第1、2天有存储数据
    }    
}

void clear_day_record_data(int day, RECORD_TYPE_E type)
{
    nvs_handle_t my_handle;
    esp_err_t err;  
    char* partition = NULL;
    char spacename[32] = "0";

    switch (type) {
        case RECORD_FD:
        partition = FD_STORAGE_PARTITION;
        break;
        case RECORD_RS:
        partition = RS_STORAGE_PARTITION;
        break;
        default:
        return;
    }

    ESP_LOGI("STORAGE", "clear_day_record_data()函数被调用");
    strcpy(spacename, s_spacename_data);
    strcat(spacename, s_num[day]);
    ESP_LOGI("STORAGE", "store_data...spacename = %s", spacename);

    err = nvs_open_from_partition(partition, spacename, NVS_READWRITE, &my_handle);
    err = nvs_set_i32(my_handle, DATA_NUMS, 0);   //清掉存储
    if(err == ESP_OK){ ESP_LOGI("STORAGE", "space...set_nvs_i32 Done");} 
        else { ESP_LOGE("STORAGE", "space..set_nvs_i32 Failed!"); }
	ESP_LOGI("STORAGE", "历史数据-日期数据清除成功");
    nvs_close(my_handle);
}

int32_t get_day_record_data(int32_t day, RECORD_TYPE_E type, char databuf[][DATA_CNTS])
{
    nvs_handle_t my_handle;
    esp_err_t err;
    int32_t datanums = 0;
    char datakeyname[32] = "0";
    char spacename[32] = "0";
    size_t len = 0;
    char* partition = NULL;

    switch (type) {
        case RECORD_FD:
        partition = FD_STORAGE_PARTITION;
        break;
        case RECORD_RS:
        partition = RS_STORAGE_PARTITION;
        break;
        default:
        return datanums;
    }
    
    // printf("enter get_day_record_data\n");
    strcpy(spacename, s_spacename_data);
    strcat(spacename, s_num[day]);
    // ("datakeyname = %s\n", datakeyname);
    ESP_LOGI("STORAGE", "store_data...spacename = %s", spacename);
    err = nvs_open_from_partition(partition, spacename, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE("STORAGE", "store_data..ERROR...nvs_open_from_partition...... !!!");
    } else {
        err = nvs_get_i32(my_handle, DATA_NUMS, &datanums);  //获取当前有存储的数据条数
        ESP_LOGI("STORAGE", "datanums = %ld\n", datanums);
        if (err != ESP_OK) {
            ESP_LOGE("STORAGE", "ERROR...nvs_get_i32(my_handle, DATA_NUMS, &datanums)");
        } else {
            for(int32_t i = 0; i < datanums; i++) {
                // printf("****get record data i = [%ld]***\n", i);
                strcpy(datakeyname, s_data_name);
                strcat(datakeyname, s_num[i]);
                // printf("datakeyname = %s\n", datakeyname);
                len = sizeof(databuf[i]);
                err = nvs_get_str(my_handle, datakeyname, databuf[i], &len);
                if(err == ESP_OK){ ESP_LOGI("STORAGE", "nvs_get_str Done");} 
                    else { ESP_LOGE("STORAGE", "nvs_get_str Failed!"); }
                if(err == ESP_OK){ ESP_LOGI("STORAGE", "space...set_nvs_i32 Done");} 
                    else { ESP_LOGE("STORAGE", "space..set_nvs_i32 Failed!"); }
                // printf("data len = %d, databuf[%ld] = %s\n", strlen(databuf[i]),i, databuf[i]);

            }           
            err = nvs_commit(my_handle);
            if(err == ESP_OK){ ESP_LOGI("STORAGE", "nvs_commit Done"); } 
                else { ESP_LOGE("STORAGE", "nvs_commit Failed!"); }
        }
    }
    // err = nvs_set_i32(my_handle, DATA_NUMS, 0);   //清掉存储
    // printf((err != ESP_OK) ? "space..set_nvs_i32 Failed!\n" : "space...set_nvs_i32 Done\n");
    nvs_close(my_handle);  //2023-11-21开启
    return datanums;
}


static void write_nvs_recorddata(int day, char *data, RECORD_TYPE_E type)
{
    nvs_handle_t my_handle;
    esp_err_t err;
    int32_t datanums = 0;
    int32_t datastoredest = 0;  //数据要存储的具体位置
    char datakeyname[32] = "0";
    char spacename[32] = "0";
    char* partition = NULL;

    // 1. 参数检查
    if (day < 0 || day > 31 || data == NULL) {
        ESP_LOGE("STORAGE", "Invalid parameters: day=%d, data=%p", day, data);
        return;
    }

    // 2. 确定分区
    switch (type) {
        case RECORD_FD:
            partition = FD_STORAGE_PARTITION;
            break;
        case RECORD_RS:
            partition = RS_STORAGE_PARTITION;
            break;
        default:
            ESP_LOGE("STORAGE", "Invalid record type");
            return;
    }

    // 3. 构建空间名称 (每天一个独立空间)
    strcpy(spacename, s_spacename_data);
    strcat(spacename, s_num[day]);
    ESP_LOGI("STORAGE", "Storing data to space: %s", spacename);

    // 4. 打开NVS句柄
    err = nvs_open_from_partition(partition, spacename, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE("STORAGE", "Failed to open NVS partition: %s", esp_err_to_name(err));
        return;
    }

    // 5. 获取当前存储的记录数
    err = nvs_get_i32(my_handle, DATA_NUMS, &datanums);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // 如果是新的一天，初始化记录数为0
        datanums = 0;
        ESP_LOGI("STORAGE", "New day, initializing record count");
    } else if (err != ESP_OK) {
        ESP_LOGE("STORAGE", "Failed to read record count: %s", esp_err_to_name(err));
        nvs_close(my_handle);
        return;
    }

    // 6. 检查记录数限制
    if (datanums >= 10) {
        ESP_LOGW("STORAGE", "Maximum records reached for day %d", day);
        nvs_close(my_handle);
        return;  // 达到最大记录数，不再存储
    }

    // 7. 存储新记录
    datastoredest = datanums;  // 新记录存储在当前记录数位置
    strcpy(datakeyname, s_data_name);
    strcat(datakeyname, s_num[datastoredest]);
    
    err = nvs_set_str(my_handle, datakeyname, data);
    if (err != ESP_OK) {
        ESP_LOGE("STORAGE", "Failed to store record: %s", esp_err_to_name(err));
        nvs_close(my_handle);
        return;
    }

    // 8. 更新记录数
    datanums++;
    err = nvs_set_i32(my_handle, DATA_NUMS, datanums);
    if (err != ESP_OK) {
        ESP_LOGE("STORAGE", "Failed to update record count: %s", esp_err_to_name(err));
        nvs_close(my_handle);
        return;
    }

    // 9. 提交更改
    err = nvs_commit(my_handle);
    if (err != ESP_OK) {
        ESP_LOGE("STORAGE", "Failed to commit changes: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI("STORAGE", "Successfully stored record %ld for day %d", datastoredest + 1, day);
    }

    nvs_close(my_handle);
}
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
void store_record(int level, int start_tm, int end_tm, int work_tm, int rcg_code, int day, uint8_t tcount, uint8_t ccount, uint8_t ecount, uint8_t is_heat, RECORD_TYPE_E type)
{
	char * levelstr = NULL;
    // int daypara = day;
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
				levelstr = "ERROR01";
				break;
		}

		cJSON *root = NULL;

		root = cJSON_CreateObject();
		cJSON_AddNumberToObject(root, "CMD", CMD_FEEDREPORT);
		cJSON_AddStringToObject(root, "DEVICE_MODE", levelstr);

		cJSON_AddNumberToObject(root, "DEVICE_START_TIME", start_tm);
		cJSON_AddNumberToObject(root, "DEVICE_END_TIME", end_tm);
		cJSON_AddNumberToObject(root, "DEVICE_WORK_TIME", work_tm);
        cJSON_AddNumberToObject(root, "RCG_CODE", rcg_code);
        cJSON_AddNumberToObject(root, "RCG_TCOUNT", tcount);
        cJSON_AddNumberToObject(root, "RCG_CCOUNT", ccount);
        cJSON_AddNumberToObject(root, "RCG_ECOUNT", ecount);
        cJSON_AddNumberToObject(root, "IS_HEAT", is_heat);  //是否加热
        // cJSON_AddNumberToObject(root, "feeding_heat", get_device_para(DEVICE_PARA_FEEDING_HEAT));  //红光加热开关配置


		// cJSON_AddStringToObject(root, "RID", rid);

		char * String = NULL;
		String = cJSON_PrintUnformatted(root);   //生成的使用记录数据

        ESP_LOGI("STORAGE", "generate_feedrecord, sizeof(String) = %d, report msg:%s", strlen(String), String);
       
        write_nvs_recorddata(day, String, type);
        update_nvs_para_day(day, type);

		cJSON_free(String);
		cJSON_Delete(root);
}


static void report_data_init(RECORD_TYPE_E type)
{
    nvs_handle_t my_handle;
    esp_err_t err;
    char spacename[32] = "0";
    char datakeyname[32] = "0";
    char* partition = NULL;

    switch (type) {
        case RECORD_FD:
        partition = FD_STORAGE_PARTITION;
        break;
        case RECORD_RS:
        partition = RS_STORAGE_PARTITION;
        break;
        default:
        return;
    }
    
    int i;
    for (i = 1; i <= 31; i++) {
        memset(spacename, 0, 32);
        strcpy(spacename, s_spacename_data);
        strcat(spacename, s_num[i]);
        ESP_LOGI("STORAGE", "spacename = %s", spacename);
        err = nvs_open_from_partition(partition, spacename, NVS_READWRITE, &my_handle);
        if (err != ESP_OK) {
            ESP_LOGE("STORAGE", "ERROR...nvs_open_from_partition...... i = %d!!!",i);
        } else {
            ESP_LOGI("STORAGE", "opening NVS handle Done i = %d", i);
            err = nvs_set_i32(my_handle, DATA_NUMS, 0);
            if(err == ESP_OK){ ESP_LOGI("STORAGE", "space...set_nvs_i32 Done"); } 
                else { ESP_LOGE("STORAGE", "space..set_nvs_i32 Failed!"); }
            for (int d = 0; d < 10; d++) {
                memset(datakeyname, 0, 32);
                strcpy(datakeyname, s_data_name);
                strcat(datakeyname, s_num[d]);
                ESP_LOGI("STORAGE", "datakeyname = %s", datakeyname);
                err = nvs_set_str(my_handle, datakeyname, s_data);
                if(err == ESP_OK){ ESP_LOGI("STORAGE", "space...set_nvs_str Done"); } 
                    else { ESP_LOGE("STORAGE", "space..set_nvs_str Failed!"); }
            }
        }
        err = nvs_commit(my_handle);
        if(err == ESP_OK){ ESP_LOGI("STORAGE", "nvs_commit Done"); } 
            else { ESP_LOGE("STORAGE", "nvs_commit Failed!"); }
        nvs_close(my_handle);
    }    
}

static void record_nvs_partition_init(RECORD_TYPE_E type)
{
    nvs_handle_t my_handle;
    int32_t inited = 0;
    char* partition = NULL;

    switch (type) {
        case RECORD_FD:
            partition = FD_STORAGE_PARTITION;
            break;
        case RECORD_RS:
            partition = RS_STORAGE_PARTITION;
            break;
        default:
            return;
    }
    esp_err_t err = nvs_flash_init_partition(partition);   //使用nvs api之前都需要先init
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init_partition
        ESP_ERROR_CHECK(nvs_flash_erase_partition(partition));
        err = nvs_flash_init_partition(partition);
    }
    ESP_ERROR_CHECK(err);
    // Open PARA_SPACENAME 包括 PARA_INITKEY、PARA_DAYKEY键值
    err = nvs_open_from_partition(partition, PARA_SPACENAME, NVS_READWRITE, &my_handle);

    if (err != ESP_OK) {
        ESP_LOGE("STORAGE", "ERROR...nvs_open_from_partition!!!");
    } else {
        ESP_LOGI("STORAGE", "opening NVS handle Done");
        // Read
        ESP_LOGI("STORAGE", "Get PARA_INITKEY from NVS ... ");
        err = nvs_get_i32(my_handle, PARA_INITKEY, &inited);
        if (err == ESP_OK) {   //键值获取成功
            ESP_LOGI("STORGAE", "record partition inited read success");
            if (inited == INITED) { //已初始化过
                ESP_LOGI("STORAGE", "record partition have inited, inited = %lx", inited);
            } else {    // 未初始化过
                err = nvs_set_i32(my_handle, PARA_INITKEY, INITED); //设置初始化键值参数
                if(err == ESP_OK) { ESP_LOGI("STORAGE", "1.PARA_INITKEY.set_nvs_i32 Done"); } 
                    else { ESP_LOGE("STORAGE", "1.PARA_INITKEY.set_nvs_i32 Failed!"); }
                err = nvs_set_i32(my_handle, PARA_DAYKEY, 0); //设置参数键值参数--上报与否
                if(err == ESP_OK) { ESP_LOGI("STORAGE", "1.PARA_DAYKEY.set_nvs_i32 Done"); } 
                    else { ESP_LOGE("STORAGE", "1.PARA_DAYKEY.set_nvs_i32 Failed!"); }
            }
        } else { //读取键值参数失败
            ESP_LOGE("STORAGE", "record partition inited read fail...");
            switch(err) {
                case ESP_FAIL:
                    ESP_LOGE("STORAGE", "inited read fail...ESP_FAIL");
                    break;
                case ESP_ERR_NVS_NOT_FOUND://未初始化过
                    ESP_LOGE("STORAGE", "inited read fail...ESP_ERR_NVS_NOT_FOUND");
                    err = nvs_set_i32(my_handle, PARA_INITKEY, INITED); //设置参数
                    if(err == ESP_OK) { ESP_LOGI("STORAGE", "2..PARA_INITKEY.set_nvs_i32 Done"); } 
                        else { ESP_LOGE("STORAGE", "2.PARA_INITKEY.set_nvs_i32 Failed!"); }
                    err = nvs_set_i32(my_handle, PARA_DAYKEY, 0); //设置参数
                    if(err == ESP_OK) { ESP_LOGI("STORAGE", "2..PARA_DAYKEY.set_nvs_i32 Done"); } 
                        else { ESP_LOGE("STORAGE", "2.PARA_DAYKEY.set_nvs_i32 Failed!"); }
                    break;
                case ESP_ERR_NVS_INVALID_HANDLE:
                    ESP_LOGE("STORAGE", "inited read fail...ESP_ERR_NVS_INVALID_HANDLE");              
                    break;
                case ESP_ERR_NVS_INVALID_NAME:
                    ESP_LOGE("STORAGE", "inited read fail...ESP_ERR_NVS_INVALID_NAME");              
                    break;
                case ESP_ERR_NVS_INVALID_LENGTH:
                    ESP_LOGE("STORAGE", "inited read fail...ESP_ERR_NVS_INVALID_LENGTH");
                    break;
                default:
                    ESP_LOGE("STORAGE", "inited read fail...ESP_FAIL2");
                    break;
            }
        }
        err = nvs_commit(my_handle);
        if(err == ESP_OK){ ESP_LOGI("STORAGE", "nvs_commit Done"); } 
            else { ESP_LOGE("STORAGE", "nvs_commit Failed"); }

        nvs_close(my_handle);
    }
    if (inited != INITED) {  //首次初始化
        report_data_init(type);
    }
}

void record_nvs_module_init(void)
{
    record_nvs_partition_init(RECORD_FD);
    record_nvs_partition_init(RECORD_RS);
    
    project_data_partition_init();
}

static UsageRecords_t arrUsageRecords[30][10];

UsageRecords_t* getusagerecord(void)
{
    return &arrUsageRecords[0][0];
}

uint16_t* getusagerecordcounts(void)
{
    return 0;
}
/*
获取设备是否被激活的标示,在红光、彩光功能开启之前调用
*/
bool get_actived(void)
{
    return s_is_active;
}
/*
该函数用于对激活与取消激活标志设置，及对NVS里的标志进行设置
激活设置在连接网络且收到系统告知已经激活时调用
取消激活在双按键被同时按下情况下使用
*/
void set_active(bool value)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open_from_partition(PROJECT_STORAGE_PARTITION, ACTIVE_SPACENAME, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE("STORAGE", "ERROR...nvs_open_from_partition!!!PROJECT_STORAGE_PARTITION");
    } else {
        if (value == 0x01) {  //激活
            //先写NVS
            err = nvs_set_i32(my_handle, ACTIVE_KEY, ACTIVE_DONE); //设置参数，激活
            if(err == ESP_OK) { ESP_LOGI("STORAGE", "2..ACTIVE_KEY.set_nvs_i32 Done"); } 
                else { ESP_LOGE("STORAGE", "2.ACTIVE_KEY.set_nvs_i32 Failed!"); }
            s_is_active = ACTIVED;
        } else {              //取消激活
             //先写NVS
            err = nvs_set_i32(my_handle, ACTIVE_KEY, 0x00000000); //设置参数，未激活
            if(err == ESP_OK) { ESP_LOGI("STORAGE", "2..ACTIVE_KEY.set_nvs_i32 Done"); } 
                else { ESP_LOGE("STORAGE", "2.ACTIVE_KEY.set_nvs_i32 Failed!"); }
            s_is_active = NOT_ACTIVE; //置标志位
        }      
    }
}

void project_data_partition_init(void)
{
    nvs_handle_t my_handle;
    int32_t actived = 0;
    esp_err_t err = nvs_flash_init_partition(PROJECT_STORAGE_PARTITION);   //使用nvs api之前都需要先init
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase_partition(PROJECT_STORAGE_PARTITION));
        err = nvs_flash_init_partition(PROJECT_STORAGE_PARTITION);
    }
    ESP_ERROR_CHECK(err);

    err = nvs_open_from_partition(PROJECT_STORAGE_PARTITION, ACTIVE_SPACENAME, NVS_READWRITE, &my_handle);

    if (err != ESP_OK) {
        ESP_LOGE("STORAGE", "ERROR...nvs_open_from_partition!!!PROJECT_STORAGE_PARTITION");
    } else {
        ESP_LOGI("STORAGE", "project...opening NVS handle Done");
        // Read
        err = nvs_get_i32(my_handle, ACTIVE_KEY, &actived);  //读取激活与否参数的键值对
        if (err == ESP_OK) {   // 键值获取成功，表示不是第一次进来
            ESP_LOGI("STORGAE", "ACTIVE_KEY read success");
            if (actived == ACTIVE_DONE) { //已激活过
                ESP_LOGI("STORAGE", "device is actived = %lx", actived);
                s_is_active = ACTIVED;
            } else {    // 未激活过
                s_is_active = NOT_ACTIVE;
            }
        } else { // 读取键值参数失败
            ESP_LOGE("STORAGE", "atcive read fail...");
            switch(err) {
                case ESP_FAIL:
                    ESP_LOGE("STORAGE", "atcive read fail...ESP_FAIL");
                    break;
                case ESP_ERR_NVS_NOT_FOUND://未初始化过
                    ESP_LOGE("STORAGE", "atcive read fail...ESP_ERR_NVS_NOT_FOUND");
                    err = nvs_set_i32(my_handle, ACTIVE_KEY, 0x00000000); //设置参数--首次创建，未激活
                    if(err == ESP_OK) { ESP_LOGI("STORAGE", "2..ACTIVE_KEY.set_nvs_i32 Done"); } 
                        else { ESP_LOGE("STORAGE", "2.ACTIVE_KEY.set_nvs_i32 Failed!"); }
                    break;
                case ESP_ERR_NVS_INVALID_HANDLE:
                    ESP_LOGE("STORAGE", "atcive read fail...ESP_ERR_NVS_INVALID_HANDLE");              
                    break;
                case ESP_ERR_NVS_INVALID_NAME:
                    ESP_LOGE("STORAGE", "atcive read fail...ESP_ERR_NVS_INVALID_NAME");              
                    break;
                case ESP_ERR_NVS_INVALID_LENGTH:
                    ESP_LOGE("STORAGE", "atcive read fail...ESP_ERR_NVS_INVALID_LENGTH");
                    break;
                default:
                    ESP_LOGE("STORAGE", "atcive read fail...ESP_FAIL2");
                    break;
            }
        }
        err = nvs_commit(my_handle);
        if(err == ESP_OK){ ESP_LOGI("STORAGE", "nvs_commit Done"); } 
            else { ESP_LOGE("STORAGE", "nvs_commit Failed"); }

        nvs_close(my_handle);
    }
}

void RTC_NVS_init(void)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open_from_partition(PROJECT_STORAGE_PARTITION, "RTC_NVS", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE("RTC", "Error (%s) opening NVS handle", esp_err_to_name(err));
    } else {
        ESP_LOGI("RTC", "Open NVS handle done");

        int RTC_flag = 0; 
        err = nvs_get_i32(my_handle, "RTC_flag", &RTC_flag); 

        switch (err) {
            case ESP_OK:
                ESP_LOGI("RTC", "RTC_flag = %d", RTC_flag);
                if(RTC_flag != 1) {
                    RTCTime_init();
                    RTC_flag = 1;
                    err = nvs_set_i32(my_handle, "RTC_flag", RTC_flag);
                    if(err == ESP_OK){ ESP_LOGI("STORAGE", "Done");} 
                        else {ESP_LOGE("STORAGE", "Failed");}
                    err = nvs_commit(my_handle);
                    if(err == ESP_OK){ ESP_LOGI("STORAGE", "Done");} 
                        else{ESP_LOGE("STORAGE", "Failed");}
                } else {
                    // 即使已经初始化过，也要确保时间同步是最新的
                    bool sync_result = RTCTimeToSystemTime();
                    
                    // 记录同步时间，用于校验时间是否正确
                    time_t rtc_time = get_rtc_timestamp();
                    struct tm timeinfo;
                    localtime_r(&rtc_time, &timeinfo);
                    ESP_LOGI("RTC", "RTC时间已同步: %04d-%02d-%02d %02d:%02d:%02d", 
                             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
                    
                    // 检查时间是否合理，如果年份小于2024可能是默认时间，需要网络同步
                    if (timeinfo.tm_year + 1900 < 2024) {
                        ESP_LOGW("RTC", "RTC时间不是网络同步时间 (%d年)，RTCTimeSet_flag设为false", 
                                timeinfo.tm_year + 1900);
                        RTCTimeSet_flag = false;  // 标记为需要网络同步
                    } else {
                        RTCTimeSet_flag = sync_result;  // 只有同步成功且时间合理才设置为true
                        ESP_LOGI("RTC", "RTC时间合理，RTCTimeSet_flag = %s", 
                                RTCTimeSet_flag ? "true" : "false");
                    }
                }
                break;

            case ESP_ERR_NVS_NOT_FOUND:
                ESP_LOGI("RTC", "The value is not initialized yet");
                RTCTime_init();
                RTC_flag = 1;
                // 注意：RTCTime_init只是设置默认时间(2023年)，不是真正有效的时间
                // RTCTimeSet_flag保持false，只有网络时间同步后才设置为true
                ESP_LOGW("RTC", "RTC初始化完成 (默认时间2023年)，RTCTimeSet_flag保持false，需要网络同步");
                err = nvs_set_i32(my_handle, "RTC_flag", RTC_flag);
                if(err == ESP_OK){ ESP_LOGI("STORAGE", "Done");} 
                    else {ESP_LOGE("STORAGE", "Failed");}
                err = nvs_commit(my_handle);
                if(err == ESP_OK){ ESP_LOGI("STORAGE", "Done");} 
                    else {ESP_LOGE("STORAGE", "Failed");}
                break;

            default :
                ESP_LOGE("RTC", "Error (%s) reading NVS handle", esp_err_to_name(err));
        }
    }
    nvs_close(my_handle); // Close
    
    // 确保RTC时间与系统时间同步
    sync_system_with_rtc_if_needed();
}

int set_SNCode(uint8_t* sn_code)
{
    // open NVS
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open_from_partition(PROJECT_STORAGE_PARTITION, "SNCode_NVS", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE("SNCode", "Error (%s) opening NVS handle", esp_err_to_name(err));
        nvs_close(my_handle); // Close
        return 0;
    } else {
        ESP_LOGI("SNCode", "Open NVS handle done");
        err = nvs_set_blob(my_handle, "SNCode", sn_code, 16);
        err = nvs_commit(my_handle);
        ESP_LOGI("SNCode", "SNCode has been set");
        nvs_close(my_handle); // Close
        return 1;
    }     
}

void get_SNCode()
{
    // open NVS
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open_from_partition(PROJECT_STORAGE_PARTITION, "SNCode_NVS", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE("RTC", "Error (%s) opening NVS handle", esp_err_to_name(err));
    } else {
        ESP_LOGI("SNCode", "Open NVS handle done");
        // uint8_t sn_code[16];
        size_t blob_length = 16;
        // ESP_LOGI("SNCode", "blob_length:%d", blob_length);
        err = nvs_get_blob(my_handle, "SNCode", user_data, &blob_length);
        switch (err) {
            case ESP_OK:
                ESP_LOGI("SNCode", "get SNCode successfully");
                ESP_LOGI("SNCode", "%s", user_data);
                break;

            case ESP_ERR_NVS_NOT_FOUND:
                ESP_LOGW("SNCode", "The value is not initialized yet");
                break;

            default :
                ESP_LOGE("SNCode", "Error (%s) reading NVS handle", esp_err_to_name(err));
        }
    }
    nvs_close(my_handle); // Close
}

// 获取 i32 类型的数据
static esp_err_t get_i32_value_from_nvs(char* space_name, char *key, int32_t *result)
{
    // 检查空指针
    if (!space_name || !key || !result) {
        ESP_LOGE("NVS", "get_i32_value: Invalid arguments passed to get_i32_value_from_nvs.");
        return ESP_ERR_INVALID_ARG;
    }
    
    nvs_handle_t my_handle;

    // 打开对应的分区
    esp_err_t err = nvs_open_from_partition(PROJECT_STORAGE_PARTITION, space_name, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        goto lab_err_return;
    }

    // 获取对应的键值对数据
    err = nvs_get_i32(my_handle, key, result); 
    if(err != ESP_OK){
        goto lab_err_return;
    }

    nvs_close(my_handle);
    return ESP_OK;

    // 打开分区失败、读取失败的情况下返回错误
lab_err_return:
    nvs_close(my_handle);
    return err;
}

static esp_err_t set_i32_value_to_nvs(char* space_name, char *key, int data)
{
    // 检查空指针
    if (!space_name || !key) {
        ESP_LOGE("NVS", "set_i32_value: Invalid arguments passed to set_i32_value_to_nvs.");
        return ESP_ERR_INVALID_ARG;
    }
    
    nvs_handle_t my_handle;

    // 打开对应的分区
    esp_err_t err = nvs_open_from_partition(PROJECT_STORAGE_PARTITION, space_name, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        goto lab_err_return;
    }

    // 设置键值对数据
    err = nvs_set_i32(my_handle, key, data); 
    if(err != ESP_OK){
        goto lab_err_return;
    }

    nvs_close(my_handle);
    return ESP_OK;

// 打开分区失败、读取失败的情况下返回错误
lab_err_return:
    nvs_close(my_handle);
    return err;
}

/** 
 * \brief 函数用于同步 NVS 数据和全局变量数据
 * 
 * - NVS 中不存在相关键值对的情况下，设置默认值
 * 
 * - NVS 中存在相关键值对，将读出数据设置到全局变量中
 **/
static void nvs_value_init(char* space_name, char *key, int initial_data, int *actual_data)
{
    int32_t storing_data = -1;
    esp_err_t err = get_i32_value_from_nvs(space_name, key, &storing_data);

    switch(err){
        case ESP_OK:
            ESP_LOGI("NVS", "nvs_value_init %s: 曾初始化", key);
            *actual_data = storing_data;
            break;
        case ESP_ERR_NVS_NOT_FOUND:
            ESP_LOGI("NVS", "nvs_value_init %s: 开始执行初始化", key);
            err = set_i32_value_to_nvs(space_name, key, initial_data);
            if(err == ESP_OK){
                ESP_LOGI("NVS", "nvs_value_init: 初始化成功");
            }else{
                ESP_LOGE("NVS", "nvs_value_init: 初始化失败(%s)", esp_err_to_name(err));
            }
            *actual_data = initial_data; // 无论是否成功存入 Flash，都将实际值设置为默认值
            break;
        default:
            ESP_LOGE("NVS", "nvs_value_init: Error (%s) reading NVS handle", esp_err_to_name(err));
            break;
    }
}

/** 
 * \brief 设置参数功能，包括写入和读出测试两个部分
 * \param index_key_device_parameters: 设备参数值对应索引
 * \param data：设置值
 * @return esp_err_t 
 **/
esp_err_t set_device_parameters(int index_key_device_parameters, int data)
{
     // udp_ip 和 picture 不存储到 NVS
    if (index_key_device_parameters == DEVICE_PARA_UDP_IP || index_key_device_parameters == DEVICE_PARA_PICTURE) {
        return ESP_FAIL;
    }

    // 先设置参数
    esp_err_t err = set_i32_value_to_nvs(
        PROJECT_STORAGE_PARTITION, 
        key_device_parameters[index_key_device_parameters],
        data
    );

            // 参数设置成功的情况下读出参数
        if(err == ESP_OK){
            int32_t value = -1;

        esp_err_t err = get_i32_value_from_nvs(
            PROJECT_STORAGE_PARTITION, 
            key_device_parameters[index_key_device_parameters],
            &value
        );

        // 还有一种情况是 读出的参数 与 设置的参数 和当前的参数都不一致 我就需要重新设置参数

        if(err == ESP_OK){
            if(value == data){ // 判断读出的参数与设置的参数是否一致
                // 计算实际数组索引，跳过UDP IP和picture参数
                int array_index = index_key_device_parameters;
                if (index_key_device_parameters > DEVICE_PARA_UDP_IP) {
                    array_index--;  // 跳过UDP IP (索引8)
                    if (index_key_device_parameters > DEVICE_PARA_PICTURE) {
                        array_index--;  // 跳过picture (索引9)
                    }
                }
                actualData_device_parameters[array_index] = value;
            }else{ // 即便读出成功但是参数不一致依旧返回错误
                err = ESP_FAIL;
            }
        }
    }

    return err;
}
/** 
 * \brief 获取设备的参数
 * 
 * 返回设备参数给到各个模块进行配置
 * 
 **/
int get_device_para(int para)
{
    if (para >= DEVICE_PARA_MAX) {
        return -1;
    }
    // 跳过UDP IP和picture参数
    if(para == DEVICE_PARA_UDP_IP || para == DEVICE_PARA_PICTURE) {
        return -1;  // 这些参数不是整数类型
    }
    
    // 计算实际数组索引，跳过UDP IP和picture参数
    int array_index = para;
    if (para > DEVICE_PARA_UDP_IP) {
        array_index--;  // 跳过UDP IP (索引8)
        if (para > DEVICE_PARA_PICTURE) {
            array_index--;  // 跳过picture (索引9)
        }
    }
    
    return actualData_device_parameters[array_index];
}

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
void func_device_parameters_init(void)
{
    int array_index = 0;  // 数组中的实际索引
    for(int i=0; i<DEVICE_PARA_MAX; i++){  
        if(i==DEVICE_PARA_UDP_IP || i==DEVICE_PARA_PICTURE)
        {
            continue;  // 跳过UDP IP和picture参数
        }else{
            nvs_value_init(
                        PROJECT_STORAGE_PARTITION, 
                        key_device_parameters[i], 
                        initialData_device_parameters[array_index], 
                        &actualData_device_parameters[array_index]);
            array_index++;  // 只有在实际存储时才增加数组索引
        }
           
    }

    update_feed_duty();
    update_volume();
    //linjun
    update_feed_time();
    update_color_time();
    update_heating_time();
    update_camera_status();
    update_beep_status();
#if 0
    for(int i=0; i<DEVICE_PARA_40HZ + 1; i++)
    {
        printf("%s的值：%d\n", key_device_parameters[i], actualData_device_parameters[i]);
    }
    printf("--------------------------------\n");
#endif
}

void clear_device_parameters(void)
{
    nvs_handle_t my_handle;
    esp_err_t err;

    // 打开 NVS 句柄
    err = nvs_open_from_partition(PROJECT_STORAGE_PARTITION, PARA_SPACENAME, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE("STORAGE", "Error opening NVS handle");
        return;
    }

    // 清除所有设备参数
    int array_index = 0;
    for(int i = 0; i < DEVICE_PARA_MAX; i++) {
        if(i == DEVICE_PARA_UDP_IP || i == DEVICE_PARA_PICTURE)
        {
            continue;  // 跳过UDP IP和picture参数
        }else{
            err = nvs_set_i32(my_handle, key_device_parameters[i], initialData_device_parameters[array_index]);
            if (err != ESP_OK) {
                ESP_LOGE("STORAGE", "Error resetting parameter %s", key_device_parameters[i]);
            }
            array_index++;
        }
        
    }

    // 提交更改
    err = nvs_commit(my_handle);
    if (err != ESP_OK) {
        ESP_LOGE("STORAGE", "Error committing NVS changes");
    }

    // 关闭句柄
    nvs_close(my_handle);

    // 重置实际参数数组
    memcpy(actualData_device_parameters, initialData_device_parameters, sizeof(actualData_device_parameters));
}

char* get_udp_ip(void)
{
    return udp_ip;
}

esp_err_t set_udp_ip(const char* ip)
{
    if (ip == NULL || strlen(ip) >= sizeof(udp_ip)) {
        return ESP_ERR_INVALID_ARG;
    }
    strcpy(udp_ip, ip);
    return ESP_OK;
}

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
void reset_daily_count_if_new_day(void)
{
    time_t now = get_rtc_timestamp();
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    int today = (timeinfo.tm_year + 1900) * 10000 + (timeinfo.tm_mon + 1) * 100 + timeinfo.tm_mday;
    
    if (current_date != today) {
        // 新的一天，重置计数
        current_date = today;
        current_daily_count = 0;
        
        // 从NVS读取今天的计数
        nvs_handle_t my_handle;
        esp_err_t err = nvs_open_from_partition(PROJECT_STORAGE_PARTITION, RCG_CODE_SPACENAME, NVS_READWRITE, &my_handle);
        if (err == ESP_OK) {
            int32_t stored_date = 0;
            uint32_t stored_count = 0;
            
            // 读取上次存储的日期
            err = nvs_get_i32(my_handle, LAST_DATE_KEY, &stored_date);
            if (err == ESP_OK && stored_date == today) {
                // 同一天，读取计数
                err = nvs_get_u32(my_handle, DAILY_COUNT_KEY, &stored_count);
                if (err == ESP_OK) {
                    current_daily_count = stored_count;
                }
            } else {
                // 新的一天或第一次，重置计数为0
                current_daily_count = 0;
                nvs_set_i32(my_handle, LAST_DATE_KEY, today);
                nvs_set_u32(my_handle, DAILY_COUNT_KEY, 0);
                nvs_commit(my_handle);
            }
            nvs_close(my_handle);
        }
        
        ESP_LOGI("RCG_CODE", "日期检查: %d, 当前计数: %lu", today, current_daily_count);
    }
}

/**
 * @brief 递增每日识别次数
 */
esp_err_t increment_daily_count(void)
{
    reset_daily_count_if_new_day();
    
    current_daily_count++;
    
    // 保存到NVS
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open_from_partition(PROJECT_STORAGE_PARTITION, RCG_CODE_SPACENAME, NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        err = nvs_set_u32(my_handle, DAILY_COUNT_KEY, current_daily_count);
        if (err == ESP_OK) {
            err = nvs_commit(my_handle);
        }
        nvs_close(my_handle);
    }
    
    ESP_LOGI("RCG_CODE", "递增识别次数: %lu", current_daily_count);
    return err;
}

/**
 * @brief 获取当前每日识别次数
 */
uint32_t get_daily_count(void)
{
    reset_daily_count_if_new_day();
    return current_daily_count;
}

/**
 * @brief 生成RCG_CODE
 * @return 格式为YYYYMMDDNN的RCG_CODE
 */
uint32_t generate_rcg_code(void)
{
    time_t now = get_rtc_timestamp();
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    // 添加详细调试信息
    ESP_LOGI("RCG_CODE", "调试时间信息:");
    ESP_LOGI("RCG_CODE", "  - time_t now = %ld", (long)now);
    ESP_LOGI("RCG_CODE", "  - tm_year = %d (原始值)", timeinfo.tm_year);
    ESP_LOGI("RCG_CODE", "  - tm_year + 1900 = %d (实际年份)", timeinfo.tm_year + 1900);
    ESP_LOGI("RCG_CODE", "  - tm_mon = %d (0-11)", timeinfo.tm_mon);
    ESP_LOGI("RCG_CODE", "  - tm_mon + 1 = %d (实际月份)", timeinfo.tm_mon + 1);
    ESP_LOGI("RCG_CODE", "  - tm_mday = %d (日期)", timeinfo.tm_mday);
    
    // 递增识别次数
    increment_daily_count();
    
    // 生成RCG_CODE: YYYYMMDDNN - 分步计算以便调试
    uint32_t year_part = (timeinfo.tm_year + 1900) * 1000000UL;
    uint32_t month_part = (timeinfo.tm_mon + 1) * 10000UL;
    uint32_t day_part = timeinfo.tm_mday * 100UL;
    uint32_t count_part = current_daily_count;
    
    ESP_LOGI("RCG_CODE", "RCG_CODE各部分计算:");
    ESP_LOGI("RCG_CODE", "  - 年份部分: %lu * 1000000 = %lu", (unsigned long)(timeinfo.tm_year + 1900), year_part);
    ESP_LOGI("RCG_CODE", "  - 月份部分: %lu * 10000 = %lu", (unsigned long)(timeinfo.tm_mon + 1), month_part);
    ESP_LOGI("RCG_CODE", "  - 日期部分: %lu * 100 = %lu", (unsigned long)timeinfo.tm_mday, day_part);
    ESP_LOGI("RCG_CODE", "  - 计数部分: %lu", count_part);
    
    uint32_t rcg_code = year_part + month_part + day_part + count_part;
    
    ESP_LOGI("RCG_CODE", "生成RCG_CODE: %lu (%04d%02d%02d%02lu)", 
             rcg_code, timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, 
             timeinfo.tm_mday, current_daily_count);
    
    return rcg_code;
}

/**
 * @brief 获取剩余使用次数
 * @return 剩余次数，-1表示不限
 */
int get_count_limit(void)
{
    return get_device_para(DEVICE_PARA_COUNT_LIMIT);
}

/**
 * @brief 设置剩余使用次数
 * @param count 剩余次数，-1表示不限
 * @return ESP_OK 成功，ESP_FAIL 失败
 */
esp_err_t set_count_limit(int count)
{
    return set_device_parameters(DEVICE_PARA_COUNT_LIMIT, count);
}

// ============= 时间备份功能 =============

#define TIME_BACKUP_SPACENAME "space_time"
#define TIME_BACKUP_KEY "last_valid_time"

/**
 * @brief 保存有效时间到NVS
 * @param valid_time 有效的时间戳
 */
void save_valid_time_to_nvs(time_t valid_time)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open_from_partition(PROJECT_STORAGE_PARTITION, TIME_BACKUP_SPACENAME, NVS_READWRITE, &my_handle);

    if (err == ESP_OK) {
        // 将time_t转换为int64_t以确保跨平台兼容性
        int64_t time_value = (int64_t)valid_time;
        err = nvs_set_i64(my_handle, TIME_BACKUP_KEY, time_value);

        if (err == ESP_OK) {
            err = nvs_commit(my_handle);
            ESP_LOGI("TIME_BACKUP", "保存有效时间到NVS: %lld", (long long)time_value);
        } else {
            ESP_LOGE("TIME_BACKUP", "保存时间失败: %s", esp_err_to_name(err));
        }
        nvs_close(my_handle);
    } else {
        ESP_LOGE("TIME_BACKUP", "打开NVS分区失败: %s", esp_err_to_name(err));
    }
}

/**
 * @brief 从NVS读取上次有效时间
 * @return 上次保存的有效时间戳，如果不存在返回0
 */
time_t load_last_valid_time_from_nvs(void)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open_from_partition(PROJECT_STORAGE_PARTITION, TIME_BACKUP_SPACENAME, NVS_READONLY, &my_handle);

    time_t last_time = 0;

    if (err == ESP_OK) {
        int64_t time_value = 0;
        err = nvs_get_i64(my_handle, TIME_BACKUP_KEY, &time_value);

        if (err == ESP_OK) {
            last_time = (time_t)time_value;
            ESP_LOGI("TIME_BACKUP", "从NVS读取上次有效时间: %lld", (long long)time_value);
        } else if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW("TIME_BACKUP", "NVS中没有保存的时间数据");
        } else {
            ESP_LOGE("TIME_BACKUP", "读取时间失败: %s", esp_err_to_name(err));
        }
        nvs_close(my_handle);
    } else {
        ESP_LOGW("TIME_BACKUP", "打开NVS分区失败: %s", esp_err_to_name(err));
    }

    return last_time;
}

// ==================== 设备日志相关函数 ====================

/**
 * @brief 将重启原因映射到编号
 * @param reason 重启原因枚举值
 * @return uint8_t 编号（0-14），0表示未知也记录
 */
uint8_t map_reset_reason_to_code(esp_reset_reason_t reason)
{
    switch(reason) {
        case ESP_RST_UNKNOWN: return 0;
        case ESP_RST_POWERON: return 1;
        case ESP_RST_EXT: return 2;
        case ESP_RST_SW: return 3;
        case ESP_RST_PANIC: return 4;
        case ESP_RST_INT_WDT: return 5;
        case ESP_RST_TASK_WDT: return 6;
        case ESP_RST_WDT: return 7;
        case ESP_RST_DEEPSLEEP: return 8;
        case ESP_RST_BROWNOUT: return 9;
        case ESP_RST_SDIO: return 10;
#ifdef ESP_RST_USB
        case ESP_RST_USB: return 11;
#endif
#ifdef ESP_RST_JTAG
        case ESP_RST_JTAG: return 12;
#endif
#ifdef ESP_RST_PWR_GLITCH
        case ESP_RST_PWR_GLITCH: return 13;
#endif
#ifdef ESP_RST_CPU_LOCKUP
        case ESP_RST_CPU_LOCKUP: return 14;
#endif
        default: return 0; // 未知
    }
}

/**
 * @brief 保存设备日志到NVS
 * @param ota_error OTA失败原因编号（0-9，0表示未知也记录）
 * @param reboot_reason 重启原因编号（0-15，0表示未知也记录）
 * @param ota_error_time OTA失败时间戳（0表示未发生）
 * @param reboot_time 重启时间戳（0表示未发生异常重启）
 * @return esp_err_t ESP_OK表示成功，其他表示失败
 */
esp_err_t save_device_logs_to_nvs(uint8_t ota_error, uint8_t reboot_reason, time_t ota_error_time, time_t reboot_time)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open_from_partition(PROJECT_STORAGE_PARTITION, LOG_SPACENAME, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE("DEVICE_LOGS", "打开NVS分区失败: %s", esp_err_to_name(err));
        return err;
    }

    cJSON *logs_array = NULL;
    char *existing = NULL;
    size_t required_size = 0;
    err = nvs_get_str(my_handle, LOG_KEY, NULL, &required_size);
    if (err == ESP_OK && required_size > 1) {
        existing = (char *)malloc(required_size);
        if (existing && nvs_get_str(my_handle, LOG_KEY, existing, &required_size) == ESP_OK) {
            cJSON *root = cJSON_Parse(existing);
            if (cJSON_IsArray(root)) {
                logs_array = root;
                root = NULL;
            } else if (cJSON_IsObject(root)) {
                logs_array = cJSON_CreateArray();
                if (logs_array) {
                    cJSON_AddItemToArray(logs_array, root);
                    root = NULL;
                }
            }
            if (root) {
                cJSON_Delete(root);
            }
        }
    }

    if (!logs_array || !cJSON_IsArray(logs_array)) {
        if (logs_array) {
            cJSON_Delete(logs_array);
        }
        logs_array = cJSON_CreateArray();
    }

    if (logs_array == NULL || !cJSON_IsArray(logs_array)) {
        ESP_LOGE("DEVICE_LOGS", "创建日志数组失败");
        if (existing) {
            free(existing);
        }
        nvs_close(my_handle);
        return ESP_ERR_NO_MEM;
    }

    while (cJSON_GetArraySize(logs_array) >= LOG_MAX_ENTRIES) {
        cJSON_DeleteItemFromArray(logs_array, 0);
    }

    char ota_error_time_str[24] = {0};
    char reboot_time_str[24] = {0};
    if (ota_error_time > 0) {
        struct tm tm_time;
        localtime_r(&ota_error_time, &tm_time);
        strftime(ota_error_time_str, sizeof(ota_error_time_str), "%Y-%m-%d %H:%M:%S", &tm_time);
    }
    if (reboot_time > 0) {
        struct tm tm_time;
        localtime_r(&reboot_time, &tm_time);
        strftime(reboot_time_str, sizeof(reboot_time_str), "%Y-%m-%d %H:%M:%S", &tm_time);
    }
    if (ota_error_time_str[0] != '\0') {
        char ota_line[40];
        snprintf(ota_line, sizeof(ota_line), "[%s]: %03u", ota_error_time_str, ota_error);
        cJSON *item = cJSON_CreateString(ota_line);
        if (item) {
            cJSON_AddItemToArray(logs_array, item);
        }
    }
    if (reboot_time_str[0] != '\0') {
        char reboot_line[40];
        snprintf(reboot_line, sizeof(reboot_line), "[%s]: %03u", reboot_time_str, reboot_reason + 100);
        cJSON *item = cJSON_CreateString(reboot_line);
        if (item) {
            cJSON_AddItemToArray(logs_array, item);
        }
    }

    char *logs_json = cJSON_PrintUnformatted(logs_array);
    if (!logs_json) {
        ESP_LOGE("DEVICE_LOGS", "序列化日志失败");
        cJSON_Delete(logs_array);
        if (existing) {
            free(existing);
        }
        nvs_close(my_handle);
        return ESP_ERR_NO_MEM;
    }

    // 存储日志
    err = nvs_set_str(my_handle, LOG_KEY, logs_json);
    if (err != ESP_OK) {
        ESP_LOGE("DEVICE_LOGS", "存储日志失败: %s", esp_err_to_name(err));
        cJSON_free(logs_json);
        cJSON_Delete(logs_array);
        if (existing) {
            free(existing);
        }
        nvs_close(my_handle);
        return err;
    }

    // 提交更改
    err = nvs_commit(my_handle);
    if (err != ESP_OK) {
        ESP_LOGE("DEVICE_LOGS", "提交日志失败: %s", esp_err_to_name(err));
        cJSON_free(logs_json);
        cJSON_Delete(logs_array);
        if (existing) {
            free(existing);
        }
        nvs_close(my_handle);
        return err;
    }

    ESP_LOGI("DEVICE_LOGS", "日志保存成功: %s", logs_json);
    cJSON_free(logs_json);
    cJSON_Delete(logs_array);
    if (existing) {
        free(existing);
    }
    nvs_close(my_handle);
    return ESP_OK;
}

/**
 * @brief 从NVS读取设备日志
 * @param logs_json 用于存储日志JSON字符串的缓冲区
 * @param max_len 缓冲区最大长度
 * @return esp_err_t ESP_OK表示成功，ESP_ERR_NVS_NOT_FOUND表示没有日志，其他表示失败
 */
esp_err_t get_device_logs_from_nvs(char *logs_json, size_t max_len)
{
    if (!logs_json || max_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open_from_partition(PROJECT_STORAGE_PARTITION, LOG_SPACENAME, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGE("DEVICE_LOGS", "打开NVS失败: %s", esp_err_to_name(err));
        return err;
    }

    size_t required_size = max_len;
    err = nvs_get_str(handle, LOG_KEY, logs_json, &required_size);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI("DEVICE_LOGS", "没有日志");
    } else if (err == ESP_ERR_NVS_INVALID_LENGTH) {
        ESP_LOGE("DEVICE_LOGS", "缓冲区太小，需要至少 %u 字节", (unsigned)required_size);
    } else if (err != ESP_OK) {
        ESP_LOGE("DEVICE_LOGS", "读取日志失败: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI("DEVICE_LOGS", "读取日志成功: %s", logs_json);
    }

    nvs_close(handle);
    return err;
}


/**
 * @brief 清除NVS中的设备日志
 */
void clear_device_logs_from_nvs(void)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open_from_partition(PROJECT_STORAGE_PARTITION, LOG_SPACENAME, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE("DEVICE_LOGS", "打开NVS分区失败: %s", esp_err_to_name(err));
        return;
    }

    // 删除日志key
    err = nvs_erase_key(my_handle, LOG_KEY);
    if (err == ESP_OK) {
        nvs_commit(my_handle);
        ESP_LOGI("DEVICE_LOGS", "日志清除成功");
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI("DEVICE_LOGS", "NVS中没有日志信息，无需清除");
    } else {
        ESP_LOGE("DEVICE_LOGS", "清除日志失败: %s", esp_err_to_name(err));
    }

    nvs_close(my_handle);
}
