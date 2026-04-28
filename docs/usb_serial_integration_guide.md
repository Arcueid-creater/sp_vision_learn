# USB虚拟串口通信集成指南

## 概述

本文档说明如何将原来基于CAN通信的系统迁移到USB虚拟串口通信，以及如何在上下位机之间正确使用新的通信协议。

---

## 一、协议对比

### CAN通信 vs USB虚拟串口通信

| 特性 | CAN通信 | USB虚拟串口 |
|------|---------|-------------|
| 物理接口 | CAN总线 | USB (虚拟串口) |
| 数据帧格式 | CAN帧 (ID + 8字节数据) | 自定义帧 (帧头 + 长度 + 数据 + CRC16) |
| 数据长度限制 | 8字节/帧 | 最大512字节/帧 |
| 校验方式 | CAN硬件CRC | CRC16-CCITT软件校验 |
| 传输速率 | 1Mbps (典型) | 12Mbps (USB 2.0 Full Speed) |
| 可靠性 | 硬件保证 | 软件校验 + 重连机制 |

---

## 二、下位机端（STM32）集成

### 2.1 文件结构

```
├── trans_task.c          # 通信任务实现
├── trans_task.h          # 通信任务头文件
└── usbd_cdc_if.c         # USB CDC接口（STM32 CubeMX生成）
```

### 2.2 初始化流程

在你的主程序中调用：

```c
// 在系统初始化时调用一次
trans_task_init();

// 在主循环或RTOS任务中周期性调用
void trans_task(void)
{
    trans_control_task();  // 建议调用频率：100-500Hz
}
```

### 2.3 发送数据到上位机

#### 2.3.1 发送IMU数据（四元数）

```c
#include "trans_task.h"

// 假设你有IMU数据结构
typedef struct {
    float x, y, z, w;  // 四元数
} quaternion_t;

quaternion_t imu_quat;

// 在IMU数据更新后发送（建议100-200Hz）
void send_imu_to_pc(void)
{
    send_imu_data(imu_quat.x, imu_quat.y, imu_quat.z, imu_quat.w);
}
```

#### 2.3.2 发送弹速和模式信息

```c
// 模式定义（与上位机保持一致）
typedef enum {
    MODE_IDLE = 0,
    MODE_AUTO_AIM = 1,
    MODE_SMALL_BUFF = 2,
    MODE_BIG_BUFF = 3,
    MODE_OUTPOST = 4
} robot_mode_e;

typedef enum {
    SHOOT_LEFT = 0,
    SHOOT_RIGHT = 1,
    SHOOT_BOTH = 2
} shoot_mode_e;

// 发送弹速和模式（建议50Hz）
void send_status_to_pc(void)
{
    float bullet_speed = 15.0f;  // 从传感器获取
    uint8_t mode = MODE_AUTO_AIM;
    uint8_t shoot_mode = SHOOT_BOTH;
    
    send_bullet_speed(bullet_speed, mode, shoot_mode);
}
```

#### 2.3.3 发送遥控器和电机数据

```c
// 这个函数已经在trans_task.c中实现
// 会自动发送遥控器数据和4个底盘电机的反馈数据
// 在trans_control_task()中自动调用，无需手动调用
```

### 2.4 接收上位机数据

#### 2.4.1 接收云台控制命令

在 `trans_task.c` 的 `process_usb_bytes()` 函数中已经预留了接收云台命令的代码框架：

```c
case TRANS_MSG_ID_GIMBAL_COMMAND:
{
    if (expected_data_length >= 1 + 1 + 1 + 4 + 4)
    {
        uint8_t control_flag = data[offset++];
        uint8_t shoot_flag = data[offset++];
        
        float yaw, pitch;
        memcpy(&yaw, data + offset, sizeof(float)); offset += sizeof(float);
        memcpy(&pitch, data + offset, sizeof(float)); offset += sizeof(float);
        
        // TODO: 在这里添加你的云台控制逻辑
        // 例如：
        // gimbal_set_target_angle(yaw, pitch);
        // if (shoot_flag) {
        //     shooter_trigger();
        // }
    }
    break;
}
```

你需要在这里添加你的云台控制逻辑。

#### 2.4.2 接收底盘速度命令

底盘速度命令已经自动解析并存储，你可以通过以下方式获取：

```c
#include "trans_task.h"

void chassis_control_task(void)
{
    // 获取接收到的底盘命令
    chassis_cmd_received_t* cmd = get_chassis_cmd_received();
    
    if (cmd->updated)
    {
        // 使用接收到的速度命令
        float vx = cmd->linear_x;   // 前后速度 (m/s)
        float vy = cmd->linear_y;   // 左右速度 (m/s)
        float wz = cmd->angular_z;  // 旋转角速度 (rad/s)
        
        // 将速度命令转换为电机控制
        // chassis_set_velocity(vx, vy, wz);
        
        cmd->updated = 0;  // 清除更新标志
    }
}
```

### 2.5 USB CDC接口配置

确保在 `usbd_cdc_if.c` 中正确调用接收处理函数：

```c
static int8_t CDC_Receive_HS(uint8_t* Buf, uint32_t *Len)
{
    // 调用trans_task的接收处理函数
    process_usb_data(Buf, Len);
    
    // 准备接收下一包数据
    USBD_CDC_SetRxBuffer(&hUsbDeviceHS, &Buf[0]);
    USBD_CDC_ReceivePacket(&hUsbDeviceHS);
    return (USBD_OK);
}
```

---

## 三、上位机端（C++）集成

### 3.1 文件结构

```
├── io/
│   ├── cboard_serial.hpp      # 串口通信类
│   ├── virtual_serial.hpp     # 虚拟串口底层实现
│   └── command.hpp            # 命令结构定义
└── configs/
    └── standard_serial.yaml   # 配置文件
```

### 3.2 配置文件

创建或修改 `configs/standard_serial.yaml`：

```yaml
# 虚拟串口配置
serial_device: "/dev/ttyACM0"  # Linux下的设备路径
serial_baudrate: 115200         # 波特率

# 其他配置...
```

**注意**：
- Linux: 设备通常是 `/dev/ttyACM0` 或 `/dev/ttyUSB0`
- Windows: 设备是 `COM3`, `COM4` 等（需要修改代码支持）
- 查看设备：`ls /dev/ttyACM*` 或 `dmesg | grep tty`

### 3.3 初始化通信

```cpp
#include "io/cboard_serial.hpp"

// 创建通信对象
io::CBoardSerial cboard("configs/standard_serial.yaml");
```

### 3.4 接收数据

#### 3.4.1 获取IMU数据

```cpp
// 获取指定时间戳的IMU数据（自动插值）
auto timestamp = std::chrono::steady_clock::now();
Eigen::Quaterniond q = cboard.imu_at(timestamp);

// 使用四元数
double w = q.w();
double x = q.x();
double y = q.y();
double z = q.z();
```

#### 3.4.2 获取弹速和模式

```cpp
// 直接访问成员变量（自动更新）
double bullet_speed = cboard.bullet_speed;
io::Mode mode = cboard.mode;
io::ShootMode shoot_mode = cboard.shoot_mode;

// 模式枚举
// Mode: idle, auto_aim, small_buff, big_buff, outpost
// ShootMode: left_shoot, right_shoot, both_shoot
```

### 3.5 发送数据

#### 3.5.1 发送云台控制命令

```cpp
#include "io/command.hpp"

// 创建命令
io::Command cmd;
cmd.control = true;   // 是否控制云台
cmd.shoot = false;    // 是否射击
cmd.yaw = 10.5;       // Yaw角度（度）
cmd.pitch = -5.2;     // Pitch角度（度）

// 发送
cboard.send(cmd);
```

#### 3.5.2 发送底盘速度命令

目前上位机端的 `cboard_serial.hpp` 还没有实现发送底盘速度命令的函数。你需要添加：

```cpp
// 在 CBoardSerial 类中添加方法
void send_chassis_cmd(float linear_x, float linear_y, float angular_z) const
{
    uint8_t data[13];
    size_t offset = 0;

    // 消息 ID
    data[offset++] = MSG_ID_CHASSIS_CMD;

    // 速度命令
    std::memcpy(data + offset, &linear_x, sizeof(float));
    offset += sizeof(float);
    std::memcpy(data + offset, &linear_y, sizeof(float));
    offset += sizeof(float);
    std::memcpy(data + offset, &angular_z, sizeof(float));
    offset += sizeof(float);

    // 发送
    try {
        serial_->write(data, offset);
    } catch (const std::exception & e) {
        tools::logger()->warn("Failed to send chassis command: {}", e.what());
    }
}
```

使用：

```cpp
// 发送底盘速度命令
cboard.send_chassis_cmd(1.0f, 0.5f, 0.2f);  // vx=1.0m/s, vy=0.5m/s, wz=0.2rad/s
```

---

## 四、调试和测试

### 4.1 检查USB连接

```bash
# Linux
ls /dev/ttyACM*
# 或
dmesg | grep tty

# 查看设备信息
lsusb
```

### 4.2 使用串口调试工具

```bash
# 使用 screen
screen /dev/ttyACM0 115200

# 使用 minicom
minicom -D /dev/ttyACM0 -b 115200

# 使用 picocom
picocom /dev/ttyACM0 -b 115200
```

### 4.3 查看日志

上位机会自动输出日志：

```
[info] CBoardSerial initialized on /dev/ttyACM0
[info] VirtualSerial opened: /dev/ttyACM0
```

如果出现错误：

```
[warn] VirtualSerial::open() failed: Failed to open serial port: /dev/ttyACM0
[warn] CRC16 mismatch! Received: 0x1234, Calculated: 0x5678
[warn] Unknown message ID: 0xFF
```

### 4.4 常见问题

#### 问题1：无法打开串口

**原因**：权限不足

**解决**：
```bash
# 临时解决
sudo chmod 666 /dev/ttyACM0

# 永久解决（将用户添加到dialout组）
sudo usermod -a -G dialout $USER
# 然后注销重新登录
```

#### 问题2：CRC校验失败

**原因**：
- 上下位机CRC算法不一致
- 数据传输过程中出现错误
- 字节序不一致

**解决**：
- 确保使用相同的CRC16-CCITT算法
- 确保初始值为0xFFFF
- 检查数据长度字段是否正确

#### 问题3：数据丢失

**原因**：
- 发送频率过高
- 缓冲区溢出

**解决**：
- 降低发送频率（IMU: 100-200Hz, 其他: 50Hz）
- 增加缓冲区大小
- 使用DMA传输

---

## 五、性能优化

### 5.1 下位机端

1. **使用DMA传输**：减少CPU占用
   ```c
   // 在STM32 CubeMX中启用USB CDC的DMA
   ```

2. **降低发送频率**：
   - IMU数据：100-200Hz
   - 遥控器数据：50Hz
   - 弹速和模式：10Hz

3. **批量发送**：将多个小数据包合并

### 5.2 上位机端

1. **使用线程池**：并行处理数据

2. **优化队列大小**：
   ```cpp
   queue_(5000)  // 根据实际需求调整
   ```

3. **减少日志输出**：只在调试时输出详细日志

---

## 六、迁移检查清单

### 下位机端

- [ ] 已集成 `trans_task.c` 和 `trans_task.h`
- [ ] 已在主程序中调用 `trans_task_init()`
- [ ] 已在主循环中调用 `trans_control_task()`
- [ ] 已在 `usbd_cdc_if.c` 中调用 `process_usb_data()`
- [ ] 已实现云台控制逻辑
- [ ] 已实现底盘速度控制逻辑
- [ ] 已测试IMU数据发送
- [ ] 已测试弹速和模式发送
- [ ] 已测试遥控器数据发送

### 上位机端

- [ ] 已更新 `cboard_serial.hpp`
- [ ] 已配置 `standard_serial.yaml`
- [ ] 已测试IMU数据接收
- [ ] 已测试弹速和模式接收
- [ ] 已测试云台命令发送
- [ ] 已添加底盘速度命令发送函数
- [ ] 已测试底盘速度命令发送
- [ ] 已验证数据完整性

---

## 七、示例代码

### 完整的下位机发送示例

```c
#include "trans_task.h"

// 在RTOS任务或主循环中
void communication_task(void)
{
    static uint32_t imu_last_time = 0;
    static uint32_t status_last_time = 0;
    
    uint32_t now = HAL_GetTick();
    
    // 100Hz发送IMU数据
    if (now - imu_last_time >= 10)
    {
        imu_last_time = now;
        
        // 获取IMU数据
        float qx = imu.quat.x;
        float qy = imu.quat.y;
        float qz = imu.quat.z;
        float qw = imu.quat.w;
        
        send_imu_data(qx, qy, qz, qw);
    }
    
    // 10Hz发送状态信息
    if (now - status_last_time >= 100)
    {
        status_last_time = now;
        
        float bullet_speed = get_bullet_speed();
        uint8_t mode = get_robot_mode();
        uint8_t shoot_mode = get_shoot_mode();
        
        send_bullet_speed(bullet_speed, mode, shoot_mode);
    }
    
    // 处理接收和发送（必须调用）
    trans_control_task();
}
```

### 完整的上位机接收示例

```cpp
#include "io/cboard_serial.hpp"
#include <iostream>

int main()
{
    // 初始化通信
    io::CBoardSerial cboard("configs/standard_serial.yaml");
    
    while (true)
    {
        // 获取IMU数据
        auto timestamp = std::chrono::steady_clock::now();
        Eigen::Quaterniond q = cboard.imu_at(timestamp);
        
        // 获取弹速和模式
        double bullet_speed = cboard.bullet_speed;
        io::Mode mode = cboard.mode;
        
        // 打印信息
        std::cout << "IMU: " << q.w() << ", " << q.x() << ", " 
                  << q.y() << ", " << q.z() << std::endl;
        std::cout << "Bullet Speed: " << bullet_speed << " m/s" << std::endl;
        std::cout << "Mode: " << io::MODES[mode] << std::endl;
        
        // 发送云台命令
        io::Command cmd;
        cmd.control = true;
        cmd.shoot = false;
        cmd.yaw = 0.0;
        cmd.pitch = 0.0;
        cboard.send(cmd);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    return 0;
}
```

---

## 八、参考文档

- [serial_protocol.md](serial_protocol.md) - 详细的协议格式说明
- [stm32_integration_guide.md](stm32_integration_guide.md) - STM32集成指南

---

## 联系方式

如有问题，请联系开发团队。
