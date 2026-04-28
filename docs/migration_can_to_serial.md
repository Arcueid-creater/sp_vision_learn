# CAN 通信迁移到虚拟串口通信指南

## 概述

本文档说明如何将自瞄项目从 CAN 通信迁移到虚拟串口通信（USB CDC）。

---

## 迁移原因

1. **简化硬件**: 无需额外的 CAN 收发器
2. **提高带宽**: USB 带宽远高于 CAN（480Mbps vs 1Mbps）
3. **易于调试**: 可使用普通串口工具调试
4. **降低成本**: 减少硬件成本
5. **更大数据包**: 单帧最大 512 字节 vs CAN 的 8 字节

---

## 架构对比

### 原架构（CAN 通信）

```
┌─────────────┐                    ┌─────────────┐
│             │   CAN Bus (1Mbps)  │             │
│  上位机     │◄──────────────────►│  下位机     │
│  (视觉)     │   8字节/帧          │  (STM32)    │
│             │   CAN协议           │             │
└─────────────┘                    └─────────────┘
     ↓                                    ↓
  CBoard.cpp                         CAN收发器
  SocketCAN                          硬件CRC
```

### 新架构（虚拟串口通信）

```
┌─────────────┐                    ┌─────────────┐
│             │  USB CDC (480Mbps) │             │
│  上位机     │◄──────────────────►│  下位机     │
│  (视觉)     │  512字节/帧         │  (STM32)    │
│             │  CRC16协议          │             │
└─────────────┘                    └─────────────┘
     ↓                                    ↓
CBoardSerial.cpp                   trans_task.c
VirtualSerial.hpp                  USB CDC
```

---

## 文件清单

### 新增文件

#### 上位机（C++）
- `io/virtual_serial.hpp` - 虚拟串口通信类（CRC16协议）
- `io/cboard_serial.hpp` - 基于虚拟串口的 CBoard 实现
- `configs/standard_serial.yaml` - 虚拟串口配置示例

#### 下位机（C）
- `trans_task.c` - 已修改为 CRC16 协议
- `trans_task.h` - 已修改为 CRC16 协议

#### 文档
- `docs/serial_protocol.md` - 通信协议详细说明
- `docs/stm32_integration_guide.md` - STM32 集成指南
- `docs/migration_can_to_serial.md` - 本文档

### 需要修改的文件

#### 上位机
- `src/standard.cpp` - 将 `CBoard` 替换为 `CBoardSerial`
- `src/sentry.cpp` - 将 `CBoard` 替换为 `CBoardSerial`
- 其他使用 `CBoard` 的源文件

---

## 迁移步骤

### 第一步：下位机修改

#### 1.1 确认文件已更新

确保 `trans_task.c` 和 `trans_task.h` 已经修改为 CRC16 协议。

#### 1.2 添加发送函数

在 `trans_task.c` 中添加以下函数（参考 `docs/stm32_integration_guide.md`）：

```c
void send_imu_data(float qx, float qy, float qz, float qw);
void send_bullet_speed_mode(float bullet_speed, uint8_t mode, uint8_t shoot_mode);
```

#### 1.3 添加接收处理

在 `process_usb_bytes()` 中添加云台命令的处理逻辑。

#### 1.4 配置 USB CDC

在 STM32CubeMX 中：
1. 启用 USB Device
2. 选择 CDC 类
3. 配置端点大小

#### 1.5 修改 usbd_cdc_if.c

在 `CDC_Receive_HS()` 中调用 `process_usb_data()`。

#### 1.6 在主循环中调用

```c
void main_loop(void)
{
    trans_task_init();
    
    while (1)
    {
        trans_control_task();
        
        // 定期发送 IMU 数据
        send_imu_data(...);
        
        // 定期发送弹速和模式
        send_bullet_speed_mode(...);
    }
}
```

---

### 第二步：上位机修改

#### 2.1 修改源文件

以 `src/standard.cpp` 为例：

**修改前：**
```cpp
#include "io/cboard.hpp"

int main()
{
    io::CBoard cboard("configs/standard3.yaml");
    
    // ...
}
```

**修改后：**
```cpp
#include "io/cboard_serial.hpp"

int main()
{
    io::CBoardSerial cboard("configs/standard_serial.yaml");
    
    // 其他代码保持不变
    // ...
}
```

#### 2.2 更新配置文件

复制 `configs/standard3.yaml` 为 `configs/standard_serial.yaml`，并修改：

**删除 CAN 相关配置：**
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
serial_device: "/dev/ttyACM0"
serial_baudrate: 115200
```

#### 2.3 修改 CMakeLists.txt（如果需要）

如果项目使用 CMake，确保包含新的头文件：

```cmake
# 添加头文件路径
include_directories(
    ${CMAKE_SOURCE_DIR}/io
)
```

---

### 第三步：测试

#### 3.1 连接硬件

1. 使用 USB 线连接上位机和下位机
2. 确认设备被识别：
   ```bash
   ls /dev/ttyACM*
   # 或
   dmesg | grep tty
   ```

#### 3.2 权限设置（Linux）

```bash
# 添加用户到 dialout 组
sudo usermod -a -G dialout $USER

# 或直接修改权限
sudo chmod 666 /dev/ttyACM0
```

#### 3.3 运行测试

```bash
# 编译
mkdir build && cd build
cmake ..
make

# 运行
./standard configs/standard_serial.yaml
```

#### 3.4 验证通信

观察日志输出：
```
[INFO] VirtualSerial opened: /dev/ttyACM0
[INFO] CBoardSerial initialized on /dev/ttyACM0
```

---

## API 对比

### CBoard (CAN) vs CBoardSerial (虚拟串口)

两者的公共接口完全相同，可以无缝替换：

```cpp
// 两者接口一致
class CBoard / CBoardSerial
{
public:
    double bullet_speed;
    Mode mode;
    ShootMode shoot_mode;
    
    CBoard/CBoardSerial(const std::string & config_path);
    
    Eigen::Quaterniond imu_at(std::chrono::steady_clock::time_point timestamp);
    
    void send(Command command) const;
};
```

**使用示例：**
```cpp
// 获取 IMU 数据
auto q = cboard.imu_at(timestamp);

// 发送云台命令
io::Command cmd;
cmd.control = true;
cmd.yaw = 10.5;
cmd.pitch = -5.2;
cboard.send(cmd);

// 读取弹速
double speed = cboard.bullet_speed;
```

---

## 性能对比

| 指标 | CAN 通信 | 虚拟串口通信 |
|------|---------|-------------|
| 最大带宽 | 1 Mbps | 480 Mbps (USB 2.0 HS) |
| 单帧数据量 | 8 字节 | 512 字节 |
| 延迟 | ~1ms | ~1ms |
| CPU 占用 | 低（硬件处理） | 中（软件 CRC） |
| 可靠性 | 高（CAN 硬件 CRC） | 高（CRC16 软件校验） |
| 调试难度 | 高（需要 CAN 分析仪） | 低（串口工具即可） |

---

## 故障排查

### 问题 1: 设备未识别

**症状**: `/dev/ttyACM0` 不存在

**解决方案**:
1. 检查 USB 连接
2. 确认 STM32 USB CDC 配置正确
3. 查看 `dmesg` 输出：
   ```bash
   dmesg | tail -20
   ```

---

### 问题 2: 权限不足

**症状**: `Failed to open serial port: Permission denied`

**解决方案**:
```bash
sudo chmod 666 /dev/ttyACM0
# 或
sudo usermod -a -G dialout $USER
# 然后重新登录
```

---

### 问题 3: CRC 校验失败

**症状**: 日志显示 `CRC16 mismatch`

**解决方案**:
1. 确认上下位机使用相同的 CRC16 算法
2. 检查字节序（应为小端序）
3. 验证数据长度字段是否正确
4. 使用串口工具查看原始数据：
   ```bash
   hexdump -C /dev/ttyACM0
   ```

---

### 问题 4: 数据丢包

**症状**: IMU 数据更新不及时

**解决方案**:
1. 降低发送频率
2. 增加队列深度（`USB_RX_MSG_COUNT`）
3. 检查 USB 带宽是否足够
4. 使用 DMA 传输

---

### 问题 5: 断线重连

**症状**: USB 断开后无法自动重连

**解决方案**:
- `VirtualSerial` 类已内置断线重连机制
- 守护线程每 100ms 检查连接状态
- 无需手动处理

---

## 回滚方案

如果需要回退到 CAN 通信：

1. 恢复源文件：
   ```cpp
   #include "io/cboard.hpp"  // 改回原来的
   io::CBoard cboard("configs/standard3.yaml");
   ```

2. 恢复配置文件：
   ```yaml
   # 使用原来的 CAN 配置
   quaternion_canid: 0x100
   bullet_speed_canid: 0x101
   send_canid: 0xff
   can_interface: "can0"
   ```

3. 重新编译

---

## 扩展功能

### 添加新的消息类型

#### 上位机（C++）

在 `io/cboard_serial.hpp` 中添加：

```cpp
enum MessageID : uint8_t
{
    // ... 现有消息
    MSG_ID_CUSTOM = 0x30,  // 新消息
};

// 在 callback() 中添加处理
case MSG_ID_CUSTOM:
{
    // 解析自定义数据
    break;
}
```

#### 下位机（C）

在 `trans_task.c` 中添加：

```c
#define TRANS_MSG_ID_CUSTOM 0x30

void send_custom_message(uint8_t *data, uint16_t len)
{
    uint8_t buffer[256];
    buffer[0] = TRANS_MSG_ID_CUSTOM;
    memcpy(buffer + 1, data, len);
    send_custom_data(buffer, len + 1);
}
```

---

## 总结

### 优势

✅ **简化硬件**: 无需 CAN 收发器  
✅ **提高带宽**: 480Mbps vs 1Mbps  
✅ **易于调试**: 普通串口工具即可  
✅ **降低成本**: 减少硬件成本  
✅ **更大数据包**: 512 字节 vs 8 字节  
✅ **自动重连**: 内置断线重连机制  

### 注意事项

⚠️ **USB 稳定性**: USB 连接可能不如 CAN 稳定（工业环境）  
⚠️ **CPU 占用**: 软件 CRC 计算会占用一定 CPU  
⚠️ **实时性**: USB 中断优先级需要合理配置  

---

## 参考文档

- [通信协议详细说明](serial_protocol.md)
- [STM32 集成指南](stm32_integration_guide.md)

---

## 联系方式

如有问题，请联系开发团队。

**版本**: v1.0  
**日期**: 2025-01-XX
