#!/bin/bash

# HTB-Z01 版本编译脚本（最终版）
# 使用 -DVERSION_2_X=1 参数控制版本

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

# 显示使用说明
show_usage() {
    echo "HTB-Z01 版本编译脚本"
    echo ""
    echo "使用方法："
    echo "  $0 [version]"
    echo ""
    echo "参数："
    echo "  version - 可选，1、2 或 all（默认为 all）"
    echo "           1: 只编译版本1.x（旧语音枚举 - MUSIC=0x00）"
    echo "           2: 只编译版本2.x（新语音枚举 - WELCOME=0x00）"
    echo "           all: 编译两个版本（默认）"
    echo ""
    echo "示例："
    echo "  $0        # 编译两个版本"
    echo "  $0 all    # 编译两个版本"
    echo "  $0 1      # 只编译版本1.x"
    echo "  $0 2      # 只编译版本2.x"
}

# 检查参数
VERSION=${1:-all}

if [ "$VERSION" != "1" ] && [ "$VERSION" != "2" ] && [ "$VERSION" != "all" ]; then
    log_error "无效的参数: $VERSION"
    show_usage
    exit 1
fi

# 检查ESP-IDF环境
if [ -z "$IDF_PATH" ]; then
    log_error "ESP-IDF环境未设置！请先运行: . \$IDF_PATH/export.sh"
    exit 1
fi

# 获取版本信息函数
get_version_info() {
    VERSION_MAJOR=$(grep 'set(VERSION_MAJOR' CMakeLists.txt | sed 's/.*"\(.*\)".*/\1/')
    VERSION_MINOR=$(grep 'set(VERSION_MINOR' CMakeLists.txt | sed 's/.*"\(.*\)".*/\1/')
    VERSION_PATCH=$(grep 'set(VERSION_PATCH' CMakeLists.txt | sed 's/.*"\(.*\)".*/\1/')
    FULL_VERSION="${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}"
    MAJOR_VERSION="${VERSION_MAJOR}"
}

# 获取版本信息
get_version_info

# 创建输出目录
OUTPUT_DIR="firmware_output"
mkdir -p "$OUTPUT_DIR/v1x_old_voice"
mkdir -p "$OUTPUT_DIR/v2x_new_voice"

# 编译单个版本的函数
build_version() {
    local ver=$1
    local VERSION_DIR=""
    local VERSION_DESC=""
    local VERSION_INFO=""
    local TARGET_BIN=""
    local DISPLAY_VERSION=""
    
    if [ "$ver" == "1" ]; then
        VERSION_DIR="$OUTPUT_DIR/v1x_old_voice"
        VERSION_DESC="版本 1.x（旧语音枚举）"
        VERSION_INFO="语音顺序: MUSIC(0x00), WELCOME, FEED_LIGHT..."
        TARGET_BIN="HTB-002_${FULL_VERSION}_v1x_voice.bin"
        DISPLAY_VERSION="${FULL_VERSION}"
    else
        # 为版本2.x创建新的版本号
        VERSION_DIR="$OUTPUT_DIR/v2x_new_voice"
        VERSION_DESC="版本 2.x（新语音枚举）"
        VERSION_INFO="语音顺序: WELCOME(0x00), FEED_LIGHT, COLOR_LIGHT..."
        # 修改版本号：将V1改为V2
        local V2_VERSION=$(echo "$FULL_VERSION" | sed 's/^V1/V2/')
        TARGET_BIN="HTB-002_${V2_VERSION}_v2x_voice.bin"
        DISPLAY_VERSION="${V2_VERSION}"
        
        # 临时修改CMakeLists.txt中的版本号
        log_info "临时修改版本号为 V2.x.x..."
        sed -i.bak 's/set(VERSION_MAJOR "V1")/set(VERSION_MAJOR "V2")/' CMakeLists.txt
    fi

    log_info "========================================="
    log_info "开始编译 $VERSION_DESC"
    log_info "固件版本: $DISPLAY_VERSION"
    log_info "$VERSION_INFO"
    log_info "========================================="

    # 清理构建
    log_info "清理构建目录..."
    idf.py fullclean
    
    # 额外清理，确保CMake缓存完全清除
    if [ -d "build" ]; then
        log_info "删除build目录以确保完全清理..."
        rm -rf build
    fi
    if [ -f "sdkconfig" ]; then
        log_info "删除sdkconfig..."
        rm -f sdkconfig
    fi

    # 设置目标
    log_info "设置目标设备为 ESP32S3..."
    idf.py set-target esp32s3

    # 编译
    if [ "$ver" == "1" ]; then
        log_info "编译版本 1.x..."
        idf.py build
    else
        log_info "编译版本 2.x（使用 -DVERSION_2_X=1）..."
        idf.py -DVERSION_2_X=1 build
    fi

    # 检查编译结果
    if [ $? -ne 0 ]; then
        log_error "编译失败！"
        return 1
    fi

    # 复制文件
    log_info "复制固件文件..."

    # 主固件文件
    if [ "$ver" == "2" ]; then
        # 版本2使用V2作为主版本号
        SOURCE_BIN="build/HTB-002-V2.bin"
    else
        SOURCE_BIN="build/HTB-002-${MAJOR_VERSION}.bin"
    fi
    
    if [ ! -f "$SOURCE_BIN" ]; then
        log_error "找不到固件文件: $SOURCE_BIN"
        return 1
    fi

    cp "$SOURCE_BIN" "$VERSION_DIR/$TARGET_BIN"
    log_info "固件已保存: $VERSION_DIR/$TARGET_BIN"

    # 复制其他文件
    cp build/partition_table/partition-table.bin "$VERSION_DIR/"
    cp build/bootloader/bootloader.bin "$VERSION_DIR/"
    if [ -f "build/ota_data_initial.bin" ]; then
        cp build/ota_data_initial.bin "$VERSION_DIR/"
    fi

    # 生成烧录说明
    cat > "$VERSION_DIR/README.txt" << EOF
HTB-002 固件说明 - $VERSION_DESC
=====================================
版本: $FULL_VERSION
生成时间: $(date)

版本特点：
$VERSION_INFO

文件说明：
- $TARGET_BIN: 主程序固件
- bootloader.bin: 引导程序
- partition-table.bin: 分区表
- ota_data_initial.bin: OTA数据初始化

烧录命令（在本目录下执行）：
========================================
esptool.py -p <串口> -b 460800 --before default_reset --after hard_reset --chip esp32s3 \\
    write_flash --flash_mode dio --flash_size 8MB --flash_freq 80m \\
    0x0 bootloader.bin \\
    0x8000 partition-table.bin \\
    0x10000 $TARGET_BIN \\
    0x524000 ota_data_initial.bin

注意：将 <串口> 替换为实际的串口号（如 /dev/ttyUSB0 或 COM3）

验证版本：
烧录后，设备启动时会播放不同的语音，可以通过首个语音判断版本：
- 版本1.x: 首先播放背景音乐
- 版本2.x: 首先播放欢迎语音
EOF

    log_info "版本 $ver 编译完成！"
    
    # 如果是版本2，恢复CMakeLists.txt
    if [ "$ver" == "2" ]; then
        log_info "恢复原始版本号..."
        mv CMakeLists.txt.bak CMakeLists.txt
    fi
    
    echo ""
    return 0
}

# 根据参数决定编译哪些版本
if [ "$VERSION" == "all" ]; then
    log_info "将编译两个版本..."
    
    # 编译版本1
    build_version 1
    if [ $? -ne 0 ]; then
        log_error "版本1编译失败！"
        exit 1
    fi
    
    # 编译版本2
    build_version 2
    if [ $? -ne 0 ]; then
        log_error "版本2编译失败！"
        exit 1
    fi
    
    log_info "========================================="
    log_info "所有版本编译完成！"
    log_info "========================================="
    echo ""
    log_info "版本 1.x 文件："
    ls -la "$OUTPUT_DIR/v1x_old_voice/"
    echo ""
    log_info "版本 2.x 文件："
    ls -la "$OUTPUT_DIR/v2x_new_voice/"
else
    # 只编译指定版本
    build_version $VERSION
    if [ $? -ne 0 ]; then
        log_error "编译失败！"
        exit 1
    fi
    
    if [ "$VERSION" == "1" ]; then
        log_info "输出目录: $OUTPUT_DIR/v1x_old_voice"
        ls -la "$OUTPUT_DIR/v1x_old_voice/"
    else
        log_info "输出目录: $OUTPUT_DIR/v2x_new_voice"
        ls -la "$OUTPUT_DIR/v2x_new_voice/"
    fi
fi