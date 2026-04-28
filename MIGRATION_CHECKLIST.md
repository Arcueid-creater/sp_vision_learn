# CAN → 串口通信迁移检查清单

## 📋 需要修改的文件总览

### ✅ 已完成准备工作
- [x] 创建 `io/cboard_wrapper.hpp` - 通信方式包装器
- [x] 创建 `docs/migration_can_to_serial_guide.md` - 详细迁移指南
- [x] 创建 `migrate_to_serial.sh` - 自动化迁移脚本

---

## 🔧 需要手动修改的文件（共 23 个）

### 1️⃣ 主程序文件（12 个）

#### 标准模式
- [ ] `src/standard.cpp`
  - 修改: `#include "io/cboard.hpp"` → `#include "io/cboard_serial.hpp"`
  - 修改: `io::CBoard cboard` → `io::CBoardSerial cboard`

- [ ] `src/mt_standard.cpp`
  - 修改: `#include "io/cboard.hpp"` → `#include "io/cboard_serial.hpp"`
  - 修改: `io::CBoard cboard` → `io::CBoardSerial cboard`

#### 哨兵模式
- [ ] `src/sentry.cpp`
  - 修改: `#include "io/cboard.hpp"` → `#include "io/cboard_serial.hpp"`
  - 修改: `io::CBoard cboard` → `io::CBoardSerial cboard`

- [ ] `src/sentry_bp.cpp`
  - 修改: `#include "io/cboard.hpp"` → `#include "io/cboard_serial.hpp"`
  - 修改: `io::CBoard cboard` → `io::CBoardSerial cboard`

- [ ] `src/sentry_debug.cpp`
  - 修改: `#include "io/cboard.hpp"` → `#include "io/cboard_serial.hpp"`
  - 修改: `io::CBoard cboard` → `io::CBoardSerial cboard`

- [ ] `src/sentry_multithread.cpp`
  - 修改: `#include "io/cboard.hpp"` → `#include "io/cboard_serial.hpp"`
  - 修改: `io::CBoard cboard` → `io::CBoardSerial cboard`

#### 无人机模式
- [ ] `src/uav.cpp`
  - 修改: `#include "io/cboard.hpp"` → `#include "io/cboard_serial.hpp"`
  - 修改: `io::CBoard cboard` → `io::CBoardSerial cboard`

- [ ] `src/uav_debug.cpp`
  - 修改: `#include "io/cboard.hpp"` → `#include "io/cboard_serial.hpp"`
  - 修改: `io::CBoard cboard` → `io::CBoardSerial cboard`

#### 调试程序
- [ ] `src/auto_buff_debug.cpp`
  - 修改: `#include "io/cboard.hpp"` → `#include "io/cboard_serial.hpp"`
  - 修改: `io::CBoard cboard` → `io::CBoardSerial cboard`

- [ ] `src/mt_auto_aim_debug.cpp`
  - 修改: `#include "io/cboard.hpp"` → `#include "io/cboard_serial.hpp"`
  - 修改: `io::CBoard cboard` → `io::CBoardSerial cboard`

#### 工具程序
- [ ] `calibration/capture.cpp`
  - 修改: `#include "io/cboard.hpp"` → `#include "io/cboard_serial.hpp"`
  - 修改: `io::CBoard cboard` → `io::CBoardSerial cboard`

- [ ] `tests/cboard_test.cpp`
  - 修改: `#include "io/cboard.hpp"` → `#include "io/cboard_serial.hpp"`
  - 修改: `io::CBoard cboard` → `io::CBoardSerial cboard`

---

### 2️⃣ 头文件和实现（2 个）

- [ ] `tasks/auto_aim/multithread/commandgener.hpp`
  - 修改: `#include "io/cboard.hpp"` → `#include "io/cboard_serial.hpp"`
  - 修改: `io::CBoard & cboard` → `io::CBoardSerial & cboard`
  - 修改: `io::CBoard & cboard_` → `io::CBoardSerial & cboard_`

- [ ] `tasks/auto_aim/multithread/commandgener.cpp`
  - 修改: 构造函数参数 `io::CBoard & cboard` → `io::CBoardSerial & cboard`

---

### 3️⃣ 配置文件（9 个）

所有配置文件需要：
1. **删除** CAN 配置
2. **添加** 串口配置

#### 删除的内容：
```yaml
quaternion_canid: 0x100
bullet_speed_canid: 0x101
send_canid: 0xff
can_interface: "can0"
```

#### 添加的内容：
```yaml
serial_device: "/dev/ttyACM0"  # 虚拟串口设备路径
serial_baudrate: 115200         # 波特率
```

#### 配置文件列表：

- [ ] `configs/standard3.yaml`
- [ ] `configs/standard4.yaml`
- [ ] `configs/sentry.yaml`
- [ ] `configs/uav.yaml`
- [ ] `configs/mvs.yaml`
- [ ] `configs/example.yaml`
- [ ] `configs/demo.yaml`
- [ ] `configs/calibration.yaml`
- [ ] `configs/ascento.yaml`

**注意：** `configs/standard_serial.yaml` 已经是串口配置，无需修改。

---

## 🚀 快速迁移方法

### 方法一：使用自动化脚本（推荐）

```bash
# 1. 赋予执行权限
chmod +x migrate_to_serial.sh

# 2. 运行脚本（会自动备份）
./migrate_to_serial.sh

# 3. 检查修改
git diff

# 4. 编译测试
cd build
cmake ..
make
```

### 方法二：手动修改

按照上面的清单逐个文件修改。

---

## 🧪 测试步骤

### 1. 编译测试
```bash
cd build
cmake ..
make
```

**预期结果：** 编译成功，无错误

### 2. 运行测试程序
```bash
./cboard_test configs/standard_serial.yaml
```

**预期输出：**
```
[INFO] CBoardSerial initialized on /dev/ttyACM0
[INFO] VirtualSerial opened: /dev/ttyACM0
[INFO] Bullet speed: 15.50 m/s, Mode: auto_aim
```

### 3. 检查串口连接
```bash
# 查看串口设备
ls -l /dev/ttyACM*

# 测试串口权限
sudo chmod 666 /dev/ttyACM0

# 或添加用户到 dialout 组
sudo usermod -aG dialout $USER
```

### 4. 运行实际程序
```bash
# 标准模式
./standard configs/standard_serial.yaml

# 多线程标准模式
./mt_standard configs/standard_serial.yaml
```

---

## ⚠️ 注意事项

### 1. 下位机配置
确保下位机：
- ✅ 使用相同的串口协议（帧头 0xFF 0xAA）
- ✅ 使用 CRC16-CCITT 算法
- ✅ 波特率设置为 115200
- ✅ 消息 ID 为 0x01（STM32状态）

### 2. 串口设备路径
常见串口设备：
- `/dev/ttyACM0` - USB CDC 设备（STM32 虚拟串口）
- `/dev/ttyUSB0` - USB 转串口模块
- `/dev/ttyS0` - 硬件串口

### 3. 权限问题
如果遇到权限错误：
```bash
# 临时解决
sudo chmod 666 /dev/ttyACM0

# 永久解决
sudo usermod -aG dialout $USER
# 注销后重新登录
```

### 4. 波特率选择
支持的波特率：
- 9600
- 19200
- 38400
- 57600
- 115200 ✅ 推荐
- 230400
- 460800
- 921600

---

## 🔄 回滚方法

如果迁移后出现问题，可以回滚：

```bash
# 方法一：使用备份
cp -r backup_can_YYYYMMDD_HHMMSS/* .

# 方法二：使用 git
git checkout -- src/ tasks/ configs/ calibration/ tests/
```

---

## 📊 修改统计

| 类型 | 数量 | 说明 |
|------|------|------|
| 主程序文件 | 12 | 需要修改头文件和类名 |
| 头文件 | 1 | commandgener.hpp |
| 实现文件 | 1 | commandgener.cpp |
| 配置文件 | 9 | 需要修改通信参数 |
| **总计** | **23** | |

---

## 📚 相关文档

- [详细迁移指南](docs/migration_can_to_serial_guide.md)
- [串口协议说明](docs/serial_protocol.md)
- [STM32 集成指南](docs/stm32_integration_guide.md)
- [快速参考](docs/quick_reference.md)

---

## ✅ 完成标志

当以下所有项都完成时，迁移成功：

- [ ] 所有文件修改完成
- [ ] 编译通过，无错误
- [ ] 测试程序能接收到数据
- [ ] IMU 数据正常
- [ ] 弹速和模式正常
- [ ] 控制命令能正常发送
- [ ] 实际运行效果正常

---

**祝迁移顺利！** 🎉

如有问题，请参考 `docs/migration_can_to_serial_guide.md` 中的常见问题部分。
