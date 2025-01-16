/**
 * @file xl9535.c
 * @brief XL9535 I/O扩展芯片驱动实现
 * @version 1.0
 * @date 2024-8-14
 */

#include "xl9535.h"

/* 模块标识符 */
static const char *TAG = "Xl9535";

/**
 * @brief 初始化XL9535使用的GPIO
 * 
 * 配置并启用扩展IO电源控制引脚
 */
static void X19535_GPIO_init(void)
{
    gpio_config_t en_io_conf = {
        .pin_bit_mask = (1ULL << DC_3V3_EN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = 0,
        .pull_down_en = 0,
        .intr_type    = GPIO_INTR_DISABLE
    };
    gpio_config(&en_io_conf);

    gpio_set_level(DC_3V3_EN, 1);   /* 开启扩展IO电源、摄像头电源 */
}

/**
 * @brief 初始化XL9535的I2C通信
 * 
 * 配置I2C总线并安装驱动
 * 
 * @return esp_err_t ESP_OK: 成功; 其他: 错误代码
 */
static esp_err_t Xl9535_I2C_Init(void)
{
    int i2c_master_port = XL9535_I2C_MASTER_NUM;
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = XL9535_I2C_MASTER_SDA_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = XL9535_I2C_MASTER_SCL_IO,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = XL9535_I2C_MASTER_FREQ_HZ,
    };
    
    /* 配置I2C参数 */
    esp_err_t err = i2c_param_config(i2c_master_port, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config failed: %s", esp_err_to_name(err));
        return err;
    }
    
    /* 安装I2C驱动 */
    err = i2c_driver_install(i2c_master_port, conf.mode, 
                            XL9535_I2C_MASTER_RX_BUF_DISABLE, 
                            XL9535_I2C_MASTER_TX_BUF_DISABLE, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_driver_install failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "xl9535 init finish");
    }
    
    /* 验证与XL9535的通信 */
    uint8_t current_state;
    err = Xl9535_Read_Reg(XL9535_I2C_SLAVE_ADDR, XL9535_INPUT_PORT0, &current_state, 2);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Xl9535_Read_Reg failed: %s", esp_err_to_name(err));
    }
    
    return err;
}

/**
 * @brief 初始化XL9535芯片
 * 
 * 初始化GPIO和I2C，并将所有IO引脚设置为低电平
 */
void Xl9535_Init(void)
{
    /* 定义所有扩展IO引脚 */
    snPinName_t pins[] = {
        PIN_P00, PIN_P01, PIN_P02, PIN_P03, PIN_P04, PIN_P05, PIN_P06, PIN_P07, 
        PIN_P10, PIN_P11, PIN_P12, PIN_P13, PIN_P14, PIN_P15, PIN_P16, PIN_P17
    };
    
    /* 初始化GPIO和I2C */
    X19535_GPIO_init();
    Xl9535_I2C_Init();
    
    /* 将所有引脚设置为低电平 */
    Xl9535_Set_Io_Status_Multiple(pins, sizeof(pins) / sizeof(pins[0]), IO_HIGH);
    Xl9535_Set_Io_Status(PIN_P06, IO_LOW);  // 关闭绿色

}

/**
 * @brief 向XL9535写入寄存器
 * 
 * @param u8I2cSlaveAddr I2C从机地址
 * @param u8Cmd 寄存器地址
 * @param u8Value 写入的值
 * @return esp_err_t ESP_OK: 成功; 其他: 错误代码
 */
esp_err_t Xl9535_Write_Reg(uint8_t u8I2cSlaveAddr, uint8_t u8Cmd, uint8_t u8Value)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (u8I2cSlaveAddr << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, u8Cmd, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, u8Value, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(XL9535_I2C_MASTER_NUM, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C Write Reg Error: %s, Addr: 0x%02X, Cmd: 0x%02X, Value: 0x%02X", 
                esp_err_to_name(ret), u8I2cSlaveAddr, u8Cmd, u8Value);
    }
    
    return ret;
}

/**
 * @brief 从XL9535读取寄存器
 * 
 * @param u8I2cSlaveAddr I2C从机地址
 * @param u8Cmd 寄存器地址
 * @param pBuff 数据接收缓冲区
 * @param u8Cnt 读取的字节数
 * @return esp_err_t ESP_OK: 成功; 其他: 错误代码
 */
esp_err_t Xl9535_Read_Reg(uint8_t u8I2cSlaveAddr, uint8_t u8Cmd, uint8_t *pBuff, uint8_t u8Cnt)
{
    esp_err_t ret;

    /* 发送寄存器地址 */
    i2c_cmd_handle_t wr_cmd = i2c_cmd_link_create();
    i2c_master_start(wr_cmd);
    i2c_master_write_byte(wr_cmd, (u8I2cSlaveAddr << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
    i2c_master_write_byte(wr_cmd, u8Cmd, ACK_CHECK_EN);
    i2c_master_stop(wr_cmd);
    ret = i2c_master_cmd_begin(XL9535_I2C_MASTER_NUM, wr_cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(wr_cmd);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C Write Cmd Error: %s, Addr: 0x%02X, Cmd: 0x%02X", 
                esp_err_to_name(ret), u8I2cSlaveAddr, u8Cmd);
        return ret;
    }

    /* 读取数据 */
    i2c_cmd_handle_t rd_cmd = i2c_cmd_link_create();
    i2c_master_start(rd_cmd);
    i2c_master_write_byte(rd_cmd, (u8I2cSlaveAddr << 1) | I2C_MASTER_READ, ACK_CHECK_EN);
    i2c_master_read(rd_cmd, pBuff, u8Cnt, I2C_MASTER_LAST_NACK);
    i2c_master_stop(rd_cmd);
    ret = i2c_master_cmd_begin(XL9535_I2C_MASTER_NUM, rd_cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(rd_cmd);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C Read Error: %s, Addr: 0x%02X, Cmd: 0x%02X", 
                esp_err_to_name(ret), u8I2cSlaveAddr, u8Cmd);
    }

    return ret;
}

/**
 * @brief 设置XL9535引脚方向
 * 
 * @param pinx 引脚名称
 * @param newMode 引脚模式(输入/输出)
 * @return esp_err_t ESP_OK: 成功; 其他: 错误代码
 */
esp_err_t Xl9535_Set_Io_Direction(snPinName_t pinx, snPinMode_t newMode)
{
    esp_err_t ret;
    uint8_t current_mode[2];

    /* 读取当前配置 */
    ret = Xl9535_Read_Reg(XL9535_I2C_SLAVE_ADDR, XL9535_CONFIG_PORT0, current_mode, 2);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read config registers: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 根据模式设置对应位 (低电平为输出，高电平为输入) */
    if (newMode == IO_OUTPUT) {
        if (pinx <= PIN_P07) {
            current_mode[0] &= ~(1 << pinx);
        } else {
            current_mode[1] &= ~(1 << (pinx - 8));
        }
    } else {
        if (pinx <= PIN_P07) {
            current_mode[0] |= (1 << pinx);
        } else {
            current_mode[1] |= (1 << (pinx - 8));
        }
    }

    /* 写入配置寄存器0 */
    ret = Xl9535_Write_Reg(XL9535_I2C_SLAVE_ADDR, XL9535_CONFIG_PORT0, current_mode[0]);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write config register 0: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 写入配置寄存器1 */
    ret = Xl9535_Write_Reg(XL9535_I2C_SLAVE_ADDR, XL9535_CONFIG_PORT1, current_mode[1]);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write config register 1: %s", esp_err_to_name(ret));
    }

    return ret;
}

/**
 * @brief 设置XL9535引脚输出状态
 * 
 * @param pinx 引脚名称
 * @param newState 引脚状态(高/低)
 * @return esp_err_t ESP_OK: 成功; 其他: 错误代码
 */
esp_err_t Xl9535_Set_Io_Status(snPinName_t pinx, snPinState_t newState)
{
    esp_err_t ret;
    uint8_t current_state[2];

    /* 读取当前输出状态 */
    ret = Xl9535_Read_Reg(XL9535_I2C_SLAVE_ADDR, XL9535_OUTPUT_PORT0, current_state, 2);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read output registers: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 根据目标状态设置对应位 */
    if (pinx <= PIN_P07) {
        if (newState == IO_LOW) {
            current_state[0] &= ~(1 << pinx);
        } else {
            current_state[0] |= (1 << pinx);
        }
    } else {
        if (newState == IO_LOW) {
            current_state[1] &= ~(1 << (pinx - 8));
        } else {
            current_state[1] |= (1 << (pinx - 8));
        }
    }

    /* 写入输出寄存器0 */
    ret = Xl9535_Write_Reg(XL9535_I2C_SLAVE_ADDR, XL9535_OUTPUT_PORT0, current_state[0]);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write output register 0: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 写入输出寄存器1 */
    ret = Xl9535_Write_Reg(XL9535_I2C_SLAVE_ADDR, XL9535_OUTPUT_PORT1, current_state[1]);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write output register 1: %s", esp_err_to_name(ret));
    }

    return ret;
}

/**
 * @brief 设置XL9535输入引脚极性
 * 
 * @param pinx 引脚名称
 * @param newPolarity 极性(正常/反转)
 * @return esp_err_t ESP_OK: 成功; 其他: 错误代码
 */
esp_err_t Xl9535_Set_Input_Polarity(snPinName_t pinx, snPinPolarity_t newPolarity)
{
    esp_err_t ret;
    uint8_t current_state[2];
    
    /* 读取当前极性配置 */
    ret = Xl9535_Read_Reg(XL9535_I2C_SLAVE_ADDR, XL9535_POLARITY_INVERSION_PORT0, current_state, 2);
    if (ret != ESP_OK) {
        return ret;
    }

    /* 根据目标极性设置对应位 */
    if (pinx <= PIN_P07) {
        if (newPolarity == IO_NON_INVERTED) {
            current_state[0] &= ~(1 << pinx);
        } else {
            current_state[0] |= (1 << pinx);
        }
    } else {
        if (newPolarity == IO_NON_INVERTED) {
            current_state[1] &= ~(1 << (pinx - 8));
        } else {
            current_state[1] |= (1 << (pinx - 8));
        }
    }

    /* 写入极性反转寄存器 */
    ret = Xl9535_Write_Reg(XL9535_I2C_SLAVE_ADDR, XL9535_POLARITY_INVERSION_PORT0, current_state[0]);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = Xl9535_Write_Reg(XL9535_I2C_SLAVE_ADDR, XL9535_POLARITY_INVERSION_PORT1, current_state[1]);
    return ret;
}

/**
 * @brief 获取XL9535引脚状态
 * 
 * @param pinx 引脚名称
 * @return snPinState_t 引脚状态 (IO_HIGH/IO_LOW/IO_UNKNOW)
 */
snPinState_t Xl9535_Get_Io_Status(snPinName_t pinx)
{
    esp_err_t ret;
    uint8_t current_state[2];
    
    /* 读取当前输入状态 */
    ret = Xl9535_Read_Reg(XL9535_I2C_SLAVE_ADDR, XL9535_INPUT_PORT0, current_state, 2);

    if (ret == ESP_OK) {
        /* 根据引脚位置返回相应状态 */
        if (pinx <= PIN_P07) {
            if (current_state[0] & (1 << pinx)) {
                return IO_HIGH;
            } else {
                return IO_LOW;
            }
        } else {
            if (current_state[1] & (1 << (pinx - 8))) {
                return IO_HIGH;
            } else {
                return IO_LOW;
            }
        }
    } else {
        /* 读取失败返回未知状态 */
        return IO_UNKNOW;
    }
}

/**
 * @brief 批量设置多个引脚状态
 * 
 * @param pins 引脚数组
 * @param num_pins 引脚数量
 * @param newState 设置的状态
 */
void Xl9535_Set_Io_Status_Multiple(snPinName_t pins[], int num_pins, snPinState_t newState)
{
    /* 遍历每个引脚进行设置 */
    for (int i = 0; i < num_pins; i++) {
        Xl9535_Set_Io_Direction(pins[i], IO_OUTPUT);
        Xl9535_Set_Io_Status(pins[i], newState);
    }
}

/**
 * @brief 使扩展IO进入低功耗模式
 * 
 * 将所有扩展IO设置为低电平
 */
void extend_IO_sleep(void)
{
    /* 注释部分: 单独控制特定引脚 */
    // Xl9535_Set_Io_Status(PIN_P07, IO_LOW);  // 关闭红色
    // Xl9535_Set_Io_Status(PIN_P06, IO_LOW);  // 关闭绿色

    /* 定义所有扩展IO引脚 */
    snPinName_t pins[] = {
        PIN_P00, PIN_P01, PIN_P02, PIN_P03, PIN_P04, PIN_P05, PIN_P06, PIN_P07, 
        PIN_P10, PIN_P11, PIN_P12, PIN_P13, PIN_P14, PIN_P15, PIN_P16, PIN_P17
    };

    /* 将所有引脚设置为低电平 */
    Xl9535_Set_Io_Status_Multiple(pins, sizeof(pins) / sizeof(pins[0]), IO_HIGH);
    Xl9535_Set_Io_Status(PIN_P06, IO_LOW);  // 关闭绿色
}

