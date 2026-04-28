#!/bin/bash

# CAN 通信迁移到串口通信自动化脚本
# 使用方法: ./migrate_to_serial.sh

set -e

echo "=========================================="
echo "  CAN → 串口通信自动迁移脚本"
echo "=========================================="
echo ""

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 检查是否在项目根目录
if [ ! -f "CMakeLists.txt" ]; then
    echo -e "${RED}错误: 请在项目根目录运行此脚本${NC}"
    exit 1
fi

# 创建备份
echo -e "${YELLOW}[1/4] 创建备份...${NC}"
BACKUP_DIR="backup_can_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$BACKUP_DIR"

# 备份需要修改的文件
echo "  备份主程序文件..."
cp -r src "$BACKUP_DIR/"
cp -r tasks/auto_aim/multithread "$BACKUP_DIR/"
cp -r configs "$BACKUP_DIR/"
cp -r calibration "$BACKUP_DIR/"
cp -r tests "$BACKUP_DIR/"

echo -e "${GREEN}  ✓ 备份完成: $BACKUP_DIR${NC}"
echo ""

# 修改主程序文件
echo -e "${YELLOW}[2/4] 修改主程序文件...${NC}"

FILES=(
    "src/standard.cpp"
    "src/mt_standard.cpp"
    "src/sentry.cpp"
    "src/sentry_bp.cpp"
    "src/sentry_debug.cpp"
    "src/sentry_multithread.cpp"
    "src/uav.cpp"
    "src/uav_debug.cpp"
    "src/auto_buff_debug.cpp"
    "src/mt_auto_aim_debug.cpp"
    "calibration/capture.cpp"
    "tests/cboard_test.cpp"
)

for file in "${FILES[@]}"; do
    if [ -f "$file" ]; then
        echo "  处理: $file"
        # 替换头文件包含
        sed -i 's/#include "io\/cboard\.hpp"/#include "io\/cboard_serial.hpp"/g' "$file"
        # 替换类名
        sed -i 's/io::CBoard /io::CBoardSerial /g' "$file"
        echo -e "${GREEN}    ✓ 完成${NC}"
    else
        echo -e "${RED}    ✗ 文件不存在: $file${NC}"
    fi
done
echo ""

# 修改 commandgener 文件
echo -e "${YELLOW}[3/4] 修改 commandgener 文件...${NC}"

COMMANDGENER_FILES=(
    "tasks/auto_aim/multithread/commandgener.hpp"
    "tasks/auto_aim/multithread/commandgener.cpp"
)

for file in "${COMMANDGENER_FILES[@]}"; do
    if [ -f "$file" ]; then
        echo "  处理: $file"
        # 替换头文件包含
        sed -i 's/#include "io\/cboard\.hpp"/#include "io\/cboard_serial.hpp"/g' "$file"
        # 替换类型引用
        sed -i 's/io::CBoard &/io::CBoardSerial \&/g' "$file"
        sed -i 's/io::CBoard\&/io::CBoardSerial\&/g' "$file"
        echo -e "${GREEN}    ✓ 完成${NC}"
    else
        echo -e "${RED}    ✗ 文件不存在: $file${NC}"
    fi
done
echo ""

# 修改配置文件
echo -e "${YELLOW}[4/4] 修改配置文件...${NC}"

CONFIG_FILES=(
    "configs/standard3.yaml"
    "configs/standard4.yaml"
    "configs/sentry.yaml"
    "configs/uav.yaml"
    "configs/mvs.yaml"
    "configs/example.yaml"
    "configs/demo.yaml"
    "configs/calibration.yaml"
    "configs/ascento.yaml"
)

for file in "${CONFIG_FILES[@]}"; do
    if [ -f "$file" ]; then
        echo "  处理: $file"
        
        # 检查是否已经有串口配置
        if grep -q "serial_device:" "$file"; then
            echo -e "${YELLOW}    ⚠ 已存在串口配置，跳过${NC}"
            continue
        fi
        
        # 删除 CAN 配置行
        sed -i '/^quaternion_canid:/d' "$file"
        sed -i '/^bullet_speed_canid:/d' "$file"
        sed -i '/^send_canid:/d' "$file"
        sed -i '/^can_interface:/d' "$file"
        
        # 在 cboard 参数部分后添加串口配置
        if grep -q "#####-----cboard参数-----#####" "$file"; then
            # 在 cboard 参数标题后插入串口配置
            sed -i '/#####-----cboard参数-----#####/a\
serial_device: "/dev/ttyACM0"  # 虚拟串口设备路径\
serial_baudrate: 115200         # 波特率\
' "$file"
            echo -e "${GREEN}    ✓ 完成${NC}"
        else
            echo -e "${YELLOW}    ⚠ 未找到 cboard 参数部分，请手动添加串口配置${NC}"
        fi
    else
        echo -e "${RED}    ✗ 文件不存在: $file${NC}"
    fi
done
echo ""

# 完成
echo "=========================================="
echo -e "${GREEN}✓ 迁移完成！${NC}"
echo "=========================================="
echo ""
echo "后续步骤："
echo "  1. 检查修改内容: git diff"
echo "  2. 编译项目: cd build && cmake .. && make"
echo "  3. 测试串口通信: ./cboard_test configs/standard_serial.yaml"
echo "  4. 如需回滚: cp -r $BACKUP_DIR/* ."
echo ""
echo "配置说明："
echo "  - 默认串口设备: /dev/ttyACM0"
echo "  - 默认波特率: 115200"
echo "  - 如需修改，请编辑 configs/*.yaml 文件"
echo ""
echo -e "${YELLOW}注意: 请确保下位机使用相同的串口协议和波特率！${NC}"
echo ""
echo "详细文档: docs/migration_can_to_serial_guide.md"
echo ""
