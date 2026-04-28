#!/bin/bash

# 迁移验证脚本
# 使用方法: ./verify_migration.sh

set -e

echo "=========================================="
echo "  串口通信迁移验证脚本"
echo "=========================================="
echo ""

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 检查是否在项目根目录
if [ ! -f "CMakeLists.txt" ]; then
    echo -e "${RED}错误: 请在项目根目录运行此脚本${NC}"
    exit 1
fi

echo -e "${BLUE}[1/5] 检查代码迁移...${NC}"

# 检查是否还有 CBoard 引用
echo "  检查是否还有 CBoard 引用..."
if grep -r "io::CBoard[^S]" src/ tests/ tasks/ calibration/ 2>/dev/null; then
    echo -e "${RED}  ✗ 发现未迁移的 CBoard 引用！${NC}"
    exit 1
else
    echo -e "${GREEN}  ✓ 所有文件已迁移到 CBoardSerial${NC}"
fi

# 检查头文件包含
echo "  检查头文件包含..."
if grep -r '#include "io/cboard\.hpp"' src/ tests/ tasks/ calibration/ 2>/dev/null; then
    echo -e "${RED}  ✗ 发现未迁移的头文件包含！${NC}"
    exit 1
else
    echo -e "${GREEN}  ✓ 所有头文件已更新${NC}"
fi

echo ""
echo -e "${BLUE}[2/5] 检查配置文件...${NC}"

# 检查配置文件
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
        if grep -q "can_interface:" "$file"; then
            echo -e "${RED}  ✗ $file 仍包含 CAN 配置${NC}"
            exit 1
        fi
        
        if grep -q "serial_device:" "$file"; then
            echo -e "${GREEN}  ✓ $file 已更新为串口配置${NC}"
        else
            echo -e "${YELLOW}  ⚠ $file 缺少串口配置${NC}"
        fi
    fi
done

echo ""
echo -e "${BLUE}[3/5] 检查串口设备...${NC}"

# 检查串口设备
if ls /dev/ttyACM* >/dev/null 2>&1; then
    echo -e "${GREEN}  ✓ 找到串口设备:${NC}"
    ls -l /dev/ttyACM* | awk '{print "    " $0}'
    
    # 检查权限
    if [ -r /dev/ttyACM0 ] && [ -w /dev/ttyACM0 ]; then
        echo -e "${GREEN}  ✓ 串口设备权限正常${NC}"
    else
        echo -e "${YELLOW}  ⚠ 串口设备权限不足，建议执行:${NC}"
        echo "    sudo chmod 666 /dev/ttyACM0"
        echo "    或"
        echo "    sudo usermod -aG dialout \$USER"
    fi
elif ls /dev/ttyUSB* >/dev/null 2>&1; then
    echo -e "${GREEN}  ✓ 找到串口设备:${NC}"
    ls -l /dev/ttyUSB* | awk '{print "    " $0}'
    echo -e "${YELLOW}  ⚠ 请确认配置文件中的设备路径正确${NC}"
else
    echo -e "${YELLOW}  ⚠ 未找到串口设备 (/dev/ttyACM* 或 /dev/ttyUSB*)${NC}"
    echo "    请检查:"
    echo "    1. 下位机是否已连接"
    echo "    2. USB 线缆是否正常"
    echo "    3. 驱动是否已安装"
fi

echo ""
echo -e "${BLUE}[4/5] 检查编译环境...${NC}"

# 检查 build 目录
if [ -d "build" ]; then
    echo -e "${GREEN}  ✓ build 目录存在${NC}"
else
    echo -e "${YELLOW}  ⚠ build 目录不存在，创建中...${NC}"
    mkdir build
fi

# 尝试编译
echo "  尝试编译项目..."
cd build

if cmake .. >/dev/null 2>&1; then
    echo -e "${GREEN}  ✓ CMake 配置成功${NC}"
else
    echo -e "${RED}  ✗ CMake 配置失败${NC}"
    cd ..
    exit 1
fi

if make -j$(nproc) 2>&1 | tee /tmp/build.log | grep -q "error:"; then
    echo -e "${RED}  ✗ 编译失败，请查看错误信息:${NC}"
    tail -20 /tmp/build.log
    cd ..
    exit 1
else
    echo -e "${GREEN}  ✓ 编译成功${NC}"
fi

cd ..

echo ""
echo -e "${BLUE}[5/5] 生成测试报告...${NC}"

# 生成报告
REPORT_FILE="migration_verification_report.txt"
cat > "$REPORT_FILE" << EOF
========================================
串口通信迁移验证报告
========================================

验证时间: $(date)

1. 代码迁移检查
   ✓ 所有文件已迁移到 CBoardSerial
   ✓ 所有头文件已更新

2. 配置文件检查
EOF

for file in "${CONFIG_FILES[@]}"; do
    if [ -f "$file" ]; then
        if grep -q "serial_device:" "$file"; then
            echo "   ✓ $file" >> "$REPORT_FILE"
        else
            echo "   ⚠ $file (缺少串口配置)" >> "$REPORT_FILE"
        fi
    fi
done

cat >> "$REPORT_FILE" << EOF

3. 串口设备检查
EOF

if ls /dev/ttyACM* >/dev/null 2>&1; then
    echo "   ✓ 找到串口设备: $(ls /dev/ttyACM*)" >> "$REPORT_FILE"
elif ls /dev/ttyUSB* >/dev/null 2>&1; then
    echo "   ✓ 找到串口设备: $(ls /dev/ttyUSB*)" >> "$REPORT_FILE"
else
    echo "   ⚠ 未找到串口设备" >> "$REPORT_FILE"
fi

cat >> "$REPORT_FILE" << EOF

4. 编译检查
   ✓ CMake 配置成功
   ✓ 编译成功

========================================
后续步骤:
========================================

1. 测试串口通信:
   cd build
   ./cboard_test ../configs/standard_serial.yaml

2. 运行主程序:
   ./standard ../configs/standard3.yaml

3. 如遇问题，请参考:
   - MIGRATION_COMPLETED.md
   - docs/migration_can_to_serial_guide.md

========================================
EOF

echo -e "${GREEN}  ✓ 报告已生成: $REPORT_FILE${NC}"

echo ""
echo "=========================================="
echo -e "${GREEN}✓ 迁移验证完成！${NC}"
echo "=========================================="
echo ""
echo "验证结果:"
echo "  ✓ 代码迁移: 完成"
echo "  ✓ 配置文件: 完成"
echo "  ✓ 编译测试: 通过"
echo ""
echo "后续步骤:"
echo "  1. 测试串口通信:"
echo "     cd build"
echo "     ./cboard_test ../configs/standard_serial.yaml"
echo ""
echo "  2. 运行主程序:"
echo "     ./standard ../configs/standard3.yaml"
echo ""
echo "详细报告: $REPORT_FILE"
echo ""
