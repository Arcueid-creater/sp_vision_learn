# CAN 通信迁移到串口通信完整指南

## 📌 概述

本指南帮助你将项目从 CAN 总线通信切换到虚拟串口通信（基于 CRC16 协议）。

---

## ✅ 接口兼容性

好消息：`CBoard` 和 `CBoardSerial` 提供**完全相同的公共接口**，迁移过程非常简单！

```cpp
// 两个类的接口完全一致
class CBoard / CBoardSerial {
public:
  double bullet_speed;      // 弹速
  Mode mode;                // 工作模式
  ShootMode shoot_mode;     // 射击模式（哨兵）
  double ft_angle;          // FT角度（无人机）
  
  Eigen::Quaterniond imu_at(timestamp);  // 获取IMU数据
  void send(Command command);             // 发送控制命令
};
```

---

## 🔧 迁移方案

### **方案一：完全切换到串口（推荐）**

适用于：确定只使用串口通信的场景

#### **步骤 1：修改主程序文件**

需要修改以下 12 个文件：

**标准模式：**
- `src/standard.cpp`
- `src/mt_standard.cpp`

**哨兵模式：**
- `src/sentry.cpp`
- `src/sentry_bp.cpp`
- `src/sentry_debug.cpp`
- `src/sentry_multithread.cpp`

**无人机模式：**
- `src/uav.cpp`
- `src/uav_debug.cpp`

**调试程序：**
- `src/auto_buff_debug.cpp`
- `src/mt_auto_aim_debug.cpp`

**工具程序：**
- `calibration/capture.cpp`
- `tests/cboard_test.cpp`

**修改内容：**

```cpp
// 修改前
#include "io/cboard.hpp"
// ...
io::CBoard cboard(config_path);

// 修改后
#include "io/cboard_serial.hpp"
// ...
io::CBoardSerial cboard(config_path);
```

#### **步骤 2：修改多线程命令生成器**

**文件：** `tasks/auto_aim/multithread/commandgener.hpp`

```cpp
// 修改前
#include "io/cboard.hpp"
// ...
class CommandGener {
public:
  CommandGener(
    auto_aim::Shooter & shooter, 
    auto_aim::Aimer & aimer, 
    io::CBoard & cboard,  // ← 修改这里
    tools::Plotter & plotter, 
    bool debug = false);
private:
  io::CBoard & cboard_;  // ← 修改这里
};

// 修改后
#include "io/cboard_serial.hpp"
// ...
class CommandGener {
public:
  CommandGener(
    auto_aim::Shooter & shooter, 
    auto_aim::Aimer & aimer, 
    io::CBoardSerial & cboard,  // ← 修改这里
    tools::Plotter & plotter, 
    bool debug = false);
private:
  io::CBoardSerial & cboard_;  // ← 修改这里
};
```

**文件：** `tasks/auto_aim/multithread/commandgener.cpp`

```cpp
// 修改前
CommandGener::CommandGener(
  auto_aim::Shooter & shooter, 
  auto_aim::Aimer & aimer, 
  io::CBoard & cboard,  // ← 修改这里
  tools::Plotter & plotter, 
  bool debug)
: shooter_(shooter), aimer_(aimer), cboard_(cboard), plotter_(plotter), 
  stop_(false), debug_(debug)
{ /* ... */ }

// 修改后
CommandGener::CommandGener(
  auto_aim::Shooter & shooter, 
  auto_aim::Aimer & aimer, 
  io::CBoardSerial & cboard,  // ← 修改这里
  tools::Plotter & plotter, 
  bool debug)
: shooter_(shooter), aimer_(aimer), cboard_(cboard), plotter_(plotter), 
  stop_(false), debug_(debug)
{ /* ... */ }
```

#### **步骤 3：修改配置文件**

需要修改所有 `configs/*.yaml` 文件：

**删除 CAN 配置：**
```yaml
# 删除这些行
quaternion_canid: 0x100
bullet_speed_canid: 0x101
send_canid: 0xff
can_interface: "can0"
```

**添加串口配置：**
```yaml
# 添加这些行
serial_device: "/dev/ttyACM0"  # 串口设备路径
serial_baudrate: 115200         # 波特率
```

**需要修改的配置文件列表：**
- `configs/standard3.yaml`
- `configs/standard4.yaml`
- `configs/sentry.yaml`
- `configs/uav.yaml`
- `configs/mvs.yaml`
- `configs/example.yaml`
- `configs/demo.yaml`
- `configs/calibration.yaml`
- `configs/ascento.yaml`

---

### **方案二：使用包装器（保留两种方式）**

适用于：需要在 CAN 和串口之间灵活切换的场景

#### **步骤 1：使用包装器头文件**

项目已提供 `io/cboard_wrapper.hpp`，它会根据编译选项自动选择通信方式。

**在所有主程序中：**

```cpp
// 修改前
#include "io/cboard.hpp"

// 修改后
#include "io/cboard_wrapper.hpp"

// 使用方式不变
io::CBoard cboard(config_path);
```

#### **步骤 2：修改 CMakeLists.txt**

```cmake
# 添加编译选项
option(USE_SERIAL_COMM "Use serial communication instead of CAN" ON)

if(USE_SERIAL_COMM)
  add_definitions(-DUSE_SERIAL_COMM)
  message(STATUS "Using Serial Communication")
else()
  message(STATUS "Using CAN Communication")
endif()
```

#### **步骤 3：切换通信方式**

```bash
# 使用串口通信（默认）
cmake -DUSE_SERIAL_COMM=ON ..
make

# 使用 CAN 通信
cmake -DUSE_SERIAL_COMM=OFF ..
make
```

---

## 📊 协议对比

### **CAN 协议**

| 数据类型 | CAN ID | 数据格式 | 字节数 |
|---------|--------|---------|--------|
| 四元数 | 0x100 | int16 × 4 | 8 |
| 弹速+模式 | 0x101 | int16 + uint8 × 2 | 6 |
| 控制命令 | 0xFF | int16 × 4 | 8 |

### **串口协议**

**帧格式：** `[0xFF][0xAA][长度(2)][数据(N)][CRC16(2)]`

| 消息ID | 名称 | 数据内容 | 字节数 |
|-------|------|---------|--------|
| 0x01 | STM32状态 | IMU(16) + 弹速(4) + 模式(2) | 23 |
| 0x02 | 视觉命令 | 控制标志(2) + Yaw(4) + Pitch(4) | 11 |

**优势：**
- ✅ CRC16 校验，更可靠
- ✅ 自动重连机制
- ✅ 状态机解析，抗干扰
- ✅ 批量传输，效率更高

---

## 🧪 测试步骤

### **1. 编译测试**

```bash
cd build
cmake ..
make cboard_test
```

### **2. 运行测试程序**

```bash
# 测试串口通信
./cboard_test configs/standard_serial.yaml
```

**预期输出：**
```
[INFO] CBoardSerial initialized on /dev/ttyACM0
[INFO] VirtualSerial opened: /dev/ttyACM0
[INFO] Bullet speed: 15.50 m/s, Mode: auto_aim, Shoot mode: both_shoot
```

### **3. 检查串口连接**

```bash
# 查看可用串口
ls /dev/ttyACM* /dev/ttyUSB*

# 测试串口通信
sudo minicom -D /dev/ttyACM0 -b 115200
```

### **4. 调试工具**

```bash
# 监控串口数据
sudo cat /dev/ttyACM0 | hexdump -C

# 查看串口权限
ls -l /dev/ttyACM0

# 添加用户到 dialout 组（避免 sudo）
sudo usermod -aG dialout $USER
```

---

## ⚠️ 常见问题

### **问题 1：串口打开失败**

**错误信息：**
```
Failed to open serial port: /dev/ttyACM0
```

**解决方案：**
```bash
# 检查设备是否存在
ls -l /dev/ttyACM0

# 检查权限
sudo chmod 666 /dev/ttyACM0

# 或添加用户到 dialout 组
sudo usermod -aG dialout $USER
# 注销后重新登录生效
```

### **问题 2：CRC 校验失败**

**错误信息：**
```
CRC16 mismatch! Received: 0x1234, Calculated: 0x5678
```

**原因：**
- 下位机和上位机的 CRC 算法不一致
- 数据传输过程中出现错误

**解决方案：**
- 确认下位机使用 CRC16-CCITT 算法
- 检查波特率设置是否一致
- 检查串口线缆质量

### **问题 3：接收不到数据**

**症状：**
- 程序运行正常，但 `bullet_speed` 始终为 0
- 日志中没有接收到数据的提示

**解决方案：**
```bash
# 1. 检查下位机是否正在发送数据
sudo cat /dev/ttyACM0 | hexdump -C

# 2. 检查波特率是否匹配
# 在配置文件中确认：
serial_baudrate: 115200  # 必须与下位机一致

# 3. 检查消息 ID 是否正确
# 下位机发送的消息 ID 必须是 0x01
```

### **问题 4：编译错误**

**错误信息：**
```
error: 'CBoardSerial' was not declared in this scope
```

**解决方案：**
- 确认已包含正确的头文件：`#include "io/cboard_serial.hpp"`
- 检查命名空间：使用 `io::CBoardSerial`
- 清理并重新编译：`rm -rf build && mkdir build && cd build && cmake .. && make`

---

## 📝 下位机配置要求

### **协议格式**

下位机必须按照以下格式发送数据：

```c
// 发送帧格式
typedef struct {
  uint8_t header1;      // 0xFF
  uint8_t header2;      // 0xAA
  uint16_t length;      // 数据长度（小端序）
  uint8_t data[N];      // 数据内容
  uint16_t crc16;       // CRC16 校验（小端序）
} SerialFrame;

// STM32 状态数据（消息 ID: 0x01）
typedef struct {
  uint8_t msg_id;       // 0x01
  float quat_x;         // 四元数 x
  float quat_y;         // 四元数 y
  float quat_z;         // 四元数 z
  float quat_w;         // 四元数 w
  float bullet_speed;   // 弹速 (m/s)
  uint8_t mode;         // 工作模式
  uint8_t shoot_mode;   // 射击模式
} __attribute__((packed)) STM32StatusData;
```

### **CRC16 计算**

下位机必须使用 **CRC16-CCITT** 算法：

```c
// CRC16-CCITT 计算函数（与上位机一致）
uint16_t calculate_crc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    uint8_t index = (crc >> 8) ^ data[i];
    crc = (crc << 8) ^ CRC16_TABLE[index];
  }
  return crc;
}
```

### **发送频率建议**

- IMU 数据：100-200 Hz
- 弹速和模式：10-20 Hz（变化时立即发送）

---

## 🎯 迁移检查清单

- [ ] 修改所有主程序文件（12 个）
- [ ] 修改 `commandgener.hpp` 和 `commandgener.cpp`
- [ ] 修改所有配置文件（9 个）
- [ ] 编译通过，无错误
- [ ] 运行测试程序，能接收到数据
- [ ] 检查 IMU 数据是否正常
- [ ] 检查弹速和模式是否正常
- [ ] 测试控制命令发送
- [ ] 验证实际运行效果

---

## 📚 参考文档

- [串口协议详细说明](serial_protocol.md)
- [STM32 集成指南](stm32_integration_guide.md)
- [USB 串口集成指南](usb_serial_integration_guide.md)
- [快速参考](quick_reference.md)

---

## 💡 建议

1. **先在测试程序中验证**：使用 `tests/cboard_test.cpp` 验证串口通信正常
2. **逐步迁移**：先迁移一个主程序，测试通过后再迁移其他
3. **保留备份**：迁移前备份原始代码
4. **使用版本控制**：使用 git 管理代码变更

---

## 🆘 获取帮助

如果遇到问题，请检查：
1. 串口设备路径是否正确
2. 波特率是否匹配
3. 下位机是否正在发送数据
4. CRC 算法是否一致
5. 消息 ID 是否正确

祝迁移顺利！🎉
