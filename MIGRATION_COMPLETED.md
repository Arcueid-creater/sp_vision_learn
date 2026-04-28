# ✅ CAN → 串口通信迁移完成报告

**迁移时间：** 2026-04-28  
**迁移方式：** 完全切换到串口通信

---

## 📋 已修改文件清单

### ✅ 主程序文件（12 个）

#### 标准模式
- [x] `src/standard.cpp`
  - 修改头文件: `io/cboard.hpp` → `io/cboard_serial.hpp`
  - 修改类名: `io::CBoard` → `io::CBoardSerial`

- [x] `src/mt_standard.cpp`
  - 修改头文件: `io/cboard.hpp` → `io/cboard_serial.hpp`
  - 修改类名: `io::CBoard` → `io::CBoardSerial`

#### 哨兵模式
- [x] `src/sentry.cpp`
- [x] `src/sentry_bp.cpp`
- [x] `src/sentry_debug.cpp`
- [x] `src/sentry_multithread.cpp`

#### 无人机模式
- [x] `src/uav.cpp` (已经是 CBoardSerial)
- [x] `src/uav_debug.cpp`

#### 调试程序
- [x] `src/auto_buff_debug.cpp`
- [x] `src/mt_auto_aim_debug.cpp`

#### 工具程序
- [x] `calibration/capture.cpp`
- [x] `tests/cboard_test.cpp`

---

### ✅ 测试文件（2 个）

- [x] `tests/handeye_test.cpp`
- [x] `tests/gimbal_response_test.cpp`

---

### ✅ 头文件和实现（2 个）

- [x] `tasks/auto_aim/multithread/commandgener.hpp`
  - 修改头文件包含
  - 修改构造函数参数: `io::CBoard &` → `io::CBoardSerial &`
  - 修改成员变量: `io::CBoard & cboard_` → `io::CBoardSerial & cboard_`

- [x] `tasks/auto_aim/multithread/commandgener.cpp`
  - 修改构造函数参数: `io::CBoard &` → `io::CBoardSerial &`

---

### ✅ 配置文件（9 个）

所有配置文件已从 CAN 配置切换到串口配置：

- [x] `configs/standard3.yaml`
- [x] `configs/standard4.yaml`
- [x] `configs/sentry.yaml`
- [x] `configs/uav.yaml`
- [x] `configs/mvs.yaml`
- [x] `configs/example.yaml`
- [x] `configs/demo.yaml`
- [x] `configs/calibration.yaml`
- [x] `configs/ascento.yaml`

**配置变更：**
```yaml
# 删除的 CAN 配置
quaternion_canid: 0x100
bullet_speed_canid: 0x101
send_canid: 0xff
can_interface: "can0"

# 新增的串口配置
serial_device: "/dev/ttyACM0"  # 虚拟串口设备路径
serial_baudrate: 115200         # 波特率
```

---

## 📊 修改统计

| 类型 | 数量 | 状态 |
|------|------|------|
| 主程序文件 | 12 | ✅ 完成 |
| 测试文件 | 2 | ✅ 完成 |
| 头文件 | 1 | ✅ 完成 |
| 实现文件 | 1 | ✅ 完成 |
| 配置文件 | 9 | ✅ 完成 |
| **总计** | **25** | **✅ 全部完成** |

---

## 🔍 验证检查

### 代码检查
```bash
# 检查是否还有 CBoard 引用（应该没有结果）
grep -r "io::CBoard[^S]" src/ tests/ tasks/ calibration/

# 检查是否所有文件都使用 CBoardSerial
grep -r "io::CBoardSerial" src/ tests/ tasks/ calibration/
```

**结果：** ✅ 所有文件已正确迁移

---

## 🧪 后续测试步骤

### 1. 编译测试

```bash
cd build
cmake ..
make
```

**预期结果：** 编译成功，无错误

---

### 2. 串口连接测试

```bash
# 检查串口设备
ls -l /dev/ttyACM*

# 设置串口权限
sudo chmod 666 /dev/ttyACM0

# 或添加用户到 dialout 组（推荐）
sudo usermod -aG dialout $USER
# 注销后重新登录生效
```

---

### 3. 运行测试程序

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

---

### 4. 运行主程序

```bash
# 标准模式
./standard configs/standard3.yaml

# 多线程标准模式
./mt_standard configs/standard3.yaml

# 哨兵模式
./sentry configs/sentry.yaml

# 无人机模式
./uav configs/uav.yaml
```

---

## 🎯 关键变更说明

### 1. 通信方式变更

| 项目 | CAN 通信 | 串口通信 |
|------|---------|---------|
| 硬件接口 | CAN 收发器 | USB 虚拟串口 |
| 设备路径 | can0 | /dev/ttyACM0 |
| 协议格式 | CAN 帧（8字节） | 自定义帧（CRC16） |
| 数据校验 | CAN 硬件CRC | CRC16-CCITT |
| 配置复杂度 | 高 | 低 |

---

### 2. 接口兼容性

**好消息：** `CBoard` 和 `CBoardSerial` 提供完全相同的公共接口！

```cpp
// 两个类的接口完全一致
class CBoard / CBoardSerial {
public:
  double bullet_speed;      // 弹速
  Mode mode;                // 工作模式
  ShootMode shoot_mode;     // 射击模式
  double ft_angle;          // FT角度
  
  Eigen::Quaterniond imu_at(timestamp);  // 获取IMU数据
  void send(Command command);             // 发送控制命令
};
```

**因此：** 主程序逻辑无需任何修改！

---

### 3. 线程模型

**串口通信的独立线程（自动创建）：**

```
VirtualSerial 对象
├─ read_thread_: 持续读取串口数据
│   └─ 状态机解析协议帧
│   └─ CRC16 校验
│   └─ 调用回调函数
│
└─ daemon_thread_: 监控连接状态
    └─ 断线自动重连
```

**特点：**
- ✅ 自动创建，无需手动管理
- ✅ 所有程序都有（standard.cpp、mt_standard.cpp 等）
- ✅ 透明运行，不影响主程序逻辑

---

## ⚠️ 注意事项

### 1. 下位机配置要求

确保下位机（STM32）使用相同的协议：

**协议格式：**
```
[0xFF][0xAA][长度(2)][数据(N)][CRC16(2)]
```

**消息格式：**
```c
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

**CRC16 算法：** 必须使用 CRC16-CCITT

---

### 2. 串口设备路径

常见串口设备：
- `/dev/ttyACM0` - USB CDC 设备（STM32 虚拟串口）✅ 推荐
- `/dev/ttyUSB0` - USB 转串口模块
- `/dev/ttyS0` - 硬件串口

**如果设备路径不同，请修改配置文件：**
```yaml
serial_device: "/dev/ttyUSB0"  # 根据实际情况修改
```

---

### 3. 波特率设置

默认波特率：**115200**

支持的波特率：
- 9600
- 19200
- 38400
- 57600
- 115200 ✅ 推荐
- 230400
- 460800
- 921600

**确保上位机和下位机波特率一致！**

---

### 4. 权限问题

如果遇到 "Permission denied" 错误：

```bash
# 方法一：临时解决（每次重启后需重新执行）
sudo chmod 666 /dev/ttyACM0

# 方法二：永久解决（推荐）
sudo usermod -aG dialout $USER
# 注销后重新登录生效
```

---

## 🐛 常见问题排查

### 问题 1：串口打开失败

**错误信息：**
```
Failed to open serial port: /dev/ttyACM0
```

**解决方案：**
1. 检查设备是否存在：`ls -l /dev/ttyACM*`
2. 检查权限：`sudo chmod 666 /dev/ttyACM0`
3. 检查设备是否被占用：`lsof /dev/ttyACM0`

---

### 问题 2：CRC 校验失败

**错误信息：**
```
CRC16 mismatch! Received: 0x1234, Calculated: 0x5678
```

**原因：**
- 下位机和上位机的 CRC 算法不一致
- 波特率设置不匹配
- 数据传输过程中出现错误

**解决方案：**
1. 确认下位机使用 CRC16-CCITT 算法
2. 检查波特率设置是否一致
3. 检查串口线缆质量

---

### 问题 3：接收不到数据

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

---

### 问题 4：编译错误

**错误信息：**
```
error: 'CBoardSerial' was not declared in this scope
```

**解决方案：**
```bash
# 清理并重新编译
rm -rf build
mkdir build
cd build
cmake ..
make
```

---

## 📚 相关文档

- [详细迁移指南](docs/migration_can_to_serial_guide.md)
- [串口协议说明](docs/serial_protocol.md)
- [STM32 集成指南](docs/stm32_integration_guide.md)
- [standard vs mt_standard 对比](docs/standard_vs_mt_standard.md)
- [通信架构对比](docs/communication_architecture.md)
- [快速参考](docs/quick_reference.md)

---

## ✅ 迁移完成检查清单

- [x] 所有主程序文件已修改
- [x] 所有测试文件已修改
- [x] commandgener 头文件和实现已修改
- [x] 所有配置文件已更新
- [x] 代码检查通过（无 CBoard 引用）
- [ ] 编译测试通过
- [ ] 串口连接测试通过
- [ ] 测试程序运行正常
- [ ] 主程序运行正常
- [ ] 实际机器人测试通过

---

## 🎉 迁移完成！

**下一步：**
1. 编译项目：`cd build && cmake .. && make`
2. 测试串口通信：`./cboard_test configs/standard_serial.yaml`
3. 运行主程序：`./standard configs/standard3.yaml`

**如有问题，请参考：**
- 常见问题排查部分
- 相关文档
- 或查看日志输出

祝运行顺利！🚀
