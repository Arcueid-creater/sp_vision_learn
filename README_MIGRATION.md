# 🎉 CAN → 串口通信迁移完成

恭喜！你的项目已成功从 CAN 通信迁移到串口通信。

---

## ✅ 迁移完成情况

### 已修改文件：25 个

- ✅ 主程序文件：12 个
- ✅ 测试文件：2 个  
- ✅ 头文件：1 个
- ✅ 实现文件：1 个
- ✅ 配置文件：9 个

**详细清单请查看：** `MIGRATION_COMPLETED.md`

---

## 🚀 快速开始

### 1. 编译项目

```bash
cd build
cmake ..
make
```

### 2. 测试串口通信

```bash
# 检查串口设备
ls -l /dev/ttyACM*

# 设置权限（如果需要）
sudo chmod 666 /dev/ttyACM0

# 运行测试程序
./cboard_test ../configs/standard_serial.yaml
```

**预期输出：**
```
[INFO] CBoardSerial initialized on /dev/ttyACM0
[INFO] VirtualSerial opened: /dev/ttyACM0
[INFO] Bullet speed: 15.50 m/s, Mode: auto_aim
```

### 3. 运行主程序

```bash
# 标准模式
./standard ../configs/standard3.yaml

# 多线程标准模式
./mt_standard ../configs/standard3.yaml

# 哨兵模式
./sentry ../configs/sentry.yaml

# 无人机模式
./uav ../configs/uav.yaml
```

---

## 📋 关键变更

### 代码变更

```cpp
// 修改前
#include "io/cboard.hpp"
io::CBoard cboard(config_path);

// 修改后
#include "io/cboard_serial.hpp"
io::CBoardSerial cboard(config_path);
```

### 配置变更

```yaml
# 修改前（CAN 配置）
quaternion_canid: 0x100
bullet_speed_canid: 0x101
send_canid: 0xff
can_interface: "can0"

# 修改后（串口配置）
serial_device: "/dev/ttyACM0"  # 串口设备路径
serial_baudrate: 115200         # 波特率
```

---

## 🎯 接口兼容性

**好消息：** `CBoard` 和 `CBoardSerial` 提供完全相同的接口！

```cpp
// 使用方式完全相同
auto q = cboard.imu_at(timestamp);      // 获取IMU数据
auto speed = cboard.bullet_speed;       // 获取弹速
auto mode = cboard.mode;                // 获取工作模式
cboard.send(command);                   // 发送控制命令
```

**因此：** 主程序逻辑无需任何修改！

---

## 🧵 线程模型

### 串口通信的独立线程（自动创建）

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

**重要：** 你不必使用 `mt_standard.cpp`！串口通信的独立线程与视觉处理的多线程是两回事。

---

## ⚠️ 重要提示

### 1. 下位机配置

确保下位机（STM32）使用相同的协议：

**协议格式：**
```
[0xFF][0xAA][长度(2)][数据(N)][CRC16(2)]
```

**消息 ID：**
- `0x01` - STM32 状态数据（上传）
- `0x02` - 视觉控制命令（下发）

**CRC 算法：** CRC16-CCITT

**详细说明：** 请参考 `docs/serial_protocol.md`

---

### 2. 串口设备路径

常见串口设备：
- `/dev/ttyACM0` - USB CDC 设备（STM32 虚拟串口）✅ 推荐
- `/dev/ttyUSB0` - USB 转串口模块
- `/dev/ttyS0` - 硬件串口

**如果设备路径不同，请修改配置文件中的 `serial_device` 参数。**

---

### 3. 权限问题

如果遇到 "Permission denied" 错误：

```bash
# 方法一：临时解决
sudo chmod 666 /dev/ttyACM0

# 方法二：永久解决（推荐）
sudo usermod -aG dialout $USER
# 注销后重新登录生效
```

---

### 4. 波特率设置

默认波特率：**115200**

**确保上位机和下位机波特率一致！**

如需修改，请同时修改：
- 配置文件中的 `serial_baudrate`
- 下位机代码中的波特率设置

---

## 🐛 常见问题

### 问题 1：串口打开失败

```
Failed to open serial port: /dev/ttyACM0
```

**解决方案：**
1. 检查设备是否存在：`ls -l /dev/ttyACM*`
2. 检查权限：`sudo chmod 666 /dev/ttyACM0`
3. 检查是否被占用：`lsof /dev/ttyACM0`

---

### 问题 2：CRC 校验失败

```
CRC16 mismatch! Received: 0x1234, Calculated: 0x5678
```

**解决方案：**
1. 确认下位机使用 CRC16-CCITT 算法
2. 检查波特率是否匹配
3. 检查串口线缆质量

---

### 问题 3：接收不到数据

**解决方案：**
```bash
# 检查下位机是否正在发送数据
sudo cat /dev/ttyACM0 | hexdump -C

# 检查波特率是否匹配
# 在配置文件中确认 serial_baudrate: 115200

# 检查消息 ID 是否正确
# 下位机发送的消息 ID 必须是 0x01
```

---

## 📚 文档索引

### 迁移相关
- **[MIGRATION_COMPLETED.md](MIGRATION_COMPLETED.md)** - 迁移完成报告
- **[MIGRATION_CHECKLIST.md](MIGRATION_CHECKLIST.md)** - 迁移检查清单
- **[docs/migration_can_to_serial_guide.md](docs/migration_can_to_serial_guide.md)** - 详细迁移指南

### 技术文档
- **[docs/serial_protocol.md](docs/serial_protocol.md)** - 串口协议说明
- **[docs/stm32_integration_guide.md](docs/stm32_integration_guide.md)** - STM32 集成指南
- **[docs/standard_vs_mt_standard.md](docs/standard_vs_mt_standard.md)** - 单线程 vs 多线程对比
- **[docs/communication_architecture.md](docs/communication_architecture.md)** - 通信架构对比

### 快速参考
- **[docs/quick_reference.md](docs/quick_reference.md)** - 快速参考手册

---

## 🛠️ 工具脚本

### 自动迁移脚本（已完成，仅供参考）

```bash
./migrate_to_serial.sh
```

### 验证脚本

```bash
./verify_migration.sh
```

**功能：**
- ✅ 检查代码迁移是否完整
- ✅ 检查配置文件是否正确
- ✅ 检查串口设备是否可用
- ✅ 尝试编译项目
- ✅ 生成验证报告

---

## 📊 性能对比

| 指标 | CAN 通信 | 串口通信 |
|------|---------|---------|
| 延迟 | ~1ms | ~2-3ms |
| 吞吐量 | 1 Mbps | 115200 bps |
| 配置复杂度 | 高 | 低 |
| 硬件成本 | 高 | 低 |
| 可靠性 | 高 | 高 |

**结论：** 对于机器人视觉系统（相机帧率 < 200 Hz），串口通信的性能完全满足需求。

---

## ✅ 验证清单

迁移完成后，请确认以下项目：

- [ ] 编译通过，无错误
- [ ] 串口设备可访问
- [ ] 测试程序能接收到数据
- [ ] IMU 数据正常
- [ ] 弹速和模式正常
- [ ] 控制命令能正常发送
- [ ] 主程序运行正常
- [ ] 实际机器人测试通过

---

## 🎓 学习资源

### 串口通信原理
- Linux termios 编程
- CRC16-CCITT 算法
- 状态机解析

### 多线程编程
- C++ std::thread
- 线程安全队列
- 回调函数机制

### 机器人视觉
- 相机标定
- 姿态估计
- 目标跟踪

---

## 🆘 获取帮助

如果遇到问题：

1. **查看日志输出** - 程序会输出详细的调试信息
2. **参考文档** - 查看上面列出的相关文档
3. **检查硬件连接** - 确认串口线缆和设备正常
4. **验证下位机** - 确认下位机正在发送数据

---

## 🎉 总结

**迁移完成！** 你的项目现在使用串口通信，具有以下优势：

- ✅ 配置简单（只需设备路径和波特率）
- ✅ 硬件简单（USB 即可）
- ✅ 可靠性高（CRC16 校验 + 自动重连）
- ✅ 接口兼容（主程序逻辑无需修改）

**下一步：**
1. 编译并测试
2. 验证串口通信
3. 运行实际程序
4. 享受简化的通信方式！

祝你使用愉快！🚀

---

**有问题？** 请查看 `MIGRATION_COMPLETED.md` 中的常见问题部分。
