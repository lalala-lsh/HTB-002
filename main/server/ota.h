/**
 * @file ota.h
 * @brief OTA升级模块头文件
 * 
 * 该模块负责ESP32设备的空中升级(OTA)功能，包括固件版本获取、
 * OTA升级状态管理以及通过HTTPS下载并安装新固件。
 */

#ifndef OTA_H
#define OTA_H

#include "My_system.h"

/**
 * @brief 获取当前运行的固件版本
 * 
 * 从当前运行的分区获取固件版本信息
 * 
 * @param version 用于存储版本信息的字符数组
 */
void getVersion(char* version);

/**
 * @brief 执行OTA升级函数
 * 
 * 根据指定的URL地址下载新固件并执行升级
 * 
 * @param adr 固件下载地址
 */
void OTA_function(char* adr);

/**
 * @brief 获取当前OTA状态
 * 
 * @return OTA_STATUS 当前OTA状态枚举值
 */
OTA_STATUS get_ota_status(void);

/**
 * @brief 设置OTA状态
 * 
 * @param set_ota_status 要设置的OTA状态
 */
void set_ota_status(OTA_STATUS set_ota_status);

#endif /* OTA_H */

