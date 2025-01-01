/**
 * @file xl9535.h
 * @brief XL9535 I/O扩展芯片驱动头文件
 * @version 1.0
 * @date 2024-8-14
 */

#ifndef BGY001_101_XL9535_H
#define BGY001_101_XL9535_H

#include "My_system.h"

/**
 * @brief I2C通信应答控制宏定义
 */
#define ACK_CHECK_EN    0x1     /* 启用ACK检查 */
#define ACK_CHECK_DIS   0x0     /* 禁用ACK检查 */
#define ACK_VAL         0x0     /* ACK值 */
#define NACK_VAL        0x1     /* NACK值 */

/**
 * @brief XL9535寄存器地址定义
 */
#define XL9535_INPUT_PORT0               0x00    /* 输入端口寄存器0 */
#define XL9535_INPUT_PORT1               0x01    /* 输入端口寄存器1 */
#define XL9535_OUTPUT_PORT0              0x02    /* 输出端口寄存器0 */
#define XL9535_OUTPUT_PORT1              0x03    /* 输出端口寄存器1 */
#define XL9535_POLARITY_INVERSION_PORT0  0x04    /* 极性反转寄存器0 */
#define XL9535_POLARITY_INVERSION_PORT1  0x05    /* 极性反转寄存器1 */
#define XL9535_CONFIG_PORT0              0x06    /* 配置寄存器0 */
#define XL9535_CONFIG_PORT1              0x07    /* 配置寄存器1 */

/**
 * @brief 函数声明
 */

/**
 * @brief 初始化XL9535芯片
 */
void Xl9535_Init(void);

/**
 * @brief 设置XL9535引脚方向
 * @param pinx 引脚名称
 * @param newMode 引脚模式(输入/输出)
 * @return esp_err_t 操作结果
 */
esp_err_t Xl9535_Set_Io_Direction(snPinName_t pinx, snPinMode_t newMode);

/**
 * @brief 设置XL9535引脚状态
 * @param pinx 引脚名称
 * @param newState 引脚状态(高/低)
 * @return esp_err_t 操作结果
 */
esp_err_t Xl9535_Set_Io_Status(snPinName_t pinx, snPinState_t newState);

/**
 * @brief 设置XL9535输入引脚极性
 * @param pinx 引脚名称
 * @param newPolarity 极性(正常/反转)
 * @return esp_err_t 操作结果
 */
esp_err_t Xl9535_Set_Input_Polarity(snPinName_t pinx, snPinPolarity_t newPolarity);

/**
 * @brief 获取XL9535引脚状态
 * @param pinx 引脚名称
 * @return snPinState_t 引脚状态
 */
snPinState_t Xl9535_Get_Io_Status(snPinName_t pinx);

/**
 * @brief 读取XL9535寄存器
 * @param u8I2cSlaveAddr I2C从机地址
 * @param u8Cmd 寄存器命令
 * @param pBuff 读取数据缓冲区
 * @param u8Cnt 读取字节数
 * @return esp_err_t 操作结果
 */
esp_err_t Xl9535_Read_Reg(uint8_t u8I2cSlaveAddr, uint8_t u8Cmd, uint8_t *pBuff, uint8_t u8Cnt);

/**
 * @brief 批量设置多个引脚状态
 * @param pins 引脚数组
 * @param num_pins 引脚数量
 * @param newState 设置的状态
 */
void Xl9535_Set_Io_Status_Multiple(snPinName_t pins[], int num_pins, snPinState_t newState);

/**
 * @brief 使扩展IO进入低功耗模式
 */
void extend_IO_sleep(void);

#endif /* BGY001_101_XL9535_H */
