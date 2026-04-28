# 上下位机虚拟串口通信协议文档

## 协议概述

本协议用于上位机（视觉系统）与下位机（STM32）之间通过虚拟串口进行通信。

### 协议格式

```
[帧头1][帧头2][数据长度][数据][CRC16校验]
```

- **帧头1**: `0xFF` (1字节)
- **帧头2**: `0xAA` (1字节)
- **数据长度**: 数据段的字节数 (2字节，小端序)
- **数据**: 实际传输的数据 (N字节，最大512字节)
- **CRC16**: CRC16-CCITT校验码 (2字节，小端序)

### CRC16 计算

- **算法**: CRC16-CCITT
- **多项式**: 0x1021
- **初始值**: 0xFFFF
- **计算范围**: 从帧头到数据结束（不包括CRC16本身）

---

## 消息类型定义

### 消息ID

| ID | 名称 | 方向 | 说明 |
|----|------|------|------|
| 0x01 | MSG_ID_IMU_DATA | 下位机→上位机 | IMU数据（四元数） |
| 0x02 | MSG_ID_BULLET_SPEED | 下位机→上位机 | 弹速和模式信息 |
| 0x03 | MSG_ID_GIMBAL_COMMAND | 上位机→下位机 | 云台控制命令 |
| 0x10 | MSG_ID_RC_DBUS | 下位机→上位机 | 遥控器数据 |
| 0x20 | MSG_ID_CHASSIS_CMD | 上位机→下位机 | 底盘速度命令 |

---

## 数据包格式详解

### 1. IMU数据 (0x01)

**方向**: 下位机 → 上位机

**数据格式**:
```
[消息ID(1)] [x(4)] [y(4)] [z(4)] [w(4)]
```

**字段说明**:
- `消息ID`: 0x01
- `x, y, z, w`: 四元数分量，float类型（IEEE 754单精度浮点数）

**总长度**: 17字节

**示例代码（下位机发送）**:
```c
void send_imu_data(float x, float y, float z, float w)
{
    uint8_t data[17];
    size_t offset = 0;
    
    data[offset++] = 0x01;  // 消息ID
    
    memcpy(data + offset, &x, sizeof(float)); offset += sizeof(float);
    memcpy(data + offset, &y, sizeof(float)); offset += sizeof(float);
    memcpy(data + offset, &z, sizeof(float)); offset += sizeof(float);
    memcpy(data + offset, &w, sizeof(float)); offset += sizeof(float);
    
    send_custom_data(data, offset);  // 使用trans_task.c中的函数
}
```

---

### 2. 弹速和模式 (0x02)

**方向**: 下位机 → 上位机

**数据格式**:
```
[消息ID(1)] [弹速(4)] [模式(1)] [射击模式(1)]
```

**字段说明**:
- `消息ID`: 0x02
- `弹速`: float类型，单位 m/s
- `模式`: uint8_t，0=idle, 1=auto_aim, 2=small_buff, 3=big_buff, 4=outpost
- `射击模式`: uint8_t，0=left_shoot, 1=right_shoot, 2=both_shoot（哨兵专用）

**总长度**: 7字节

**示例代码（下位机发送）**:
```c
void send_bullet_speed(float speed, uint8_t mode, uint8_t shoot_mode)
{
    uint8_t data[7];
    size_t offset = 0;
    
    data[offset++] = 0x02;  // 消息ID
    
    memcpy(data + offset, &speed, sizeof(float)); offset += sizeof(float);
    data[offset++] = mode;
    data[offset++] = shoot_mode;
    
    send_custom_data(data, offset);
}
```

---

### 3. 云台控制命令 (0x03)

**方向**: 上位机 → 下位机

**数据格式**:
```
[消息ID(1)] [控制标志(1)] [射击标志(1)] [Yaw角度(4)] [Pitch角度(4)]
```

**字段说明**:
- `消息ID`: 0x03
- `控制标志`: uint8_t，0=不控制，1=控制
- `射击标志`: uint8_t，0=不射击，1=射击
- `Yaw角度`: float类型，单位：度
- `Pitch角度`: float类型，单位：度

**总长度**: 11字节

**示例代码（下位机接收）**:
```c
// 在 process_usb_bytes() 的 switch 语句中添加：
case 0x03:  // MSG_ID_GIMBAL_COMMAND
{
    if (expected_data_length >= 1 + 1 + 1 + 4 + 4)
    {
        uint8_t control_flag = data[offset++];
        uint8_t shoot_flag = data[offset++];
        
        float yaw, pitch;
        memcpy(&yaw, data + offset, sizeof(float)); offset += sizeof(float);
        memcpy(&pitch, data + offset, sizeof(float)); offset += sizeof(float);
        
        // 处理云台控制命令
        // TODO: 添加你的云台控制逻辑
    }
    break;
}
```

---

### 4. 遥控器数据 (0x10)

**方向**: 下位机 → 上位机

**数据格式**:
```
[消息ID(1)] [ch1(2)] [ch2(2)] [ch3(2)] [ch4(2)] [sw1(1)] [sw2(1)] 
[mouse_x(2)] [mouse_y(2)] [mouse_l(1)] [mouse_r(1)] [key_code(2)] [wheel(2)]
[电机1数据(12)] [电机2数据(12)] [电机3数据(12)] [电机4数据(12)]
```

**字段说明**:
- `消息ID`: 0x10
- `ch1~ch4`: 遥控器通道值，int16_t
- `sw1, sw2`: 开关状态，uint8_t
- `mouse_x, mouse_y`: 鼠标移动，int16_t
- `mouse_l, mouse_r`: 鼠标按键，uint8_t
- `key_code`: 键盘按键码，uint16_t
- `wheel`: 滚轮值，int16_t
- `电机数据`: 4个电机的数据，每个电机12字节：
  - motor_id (1字节): 电机ID
  - speed_rpm (2字节): 转速 (RPM)
  - total_angle (4字节): 总角度
  - ecd (2字节): 编码器值
  - real_current (2字节): 实际电流
  - temperature (1字节): 温度

**总长度**: 69字节 (1 + 20 + 48)

**注意**: 此消息已在 `trans_task.c` 的 `send_rc_dbus_data()` 函数中实现。

**电机数据结构**:
```c
typedef struct {
    uint8_t motor_id;        // 电机ID
    int16_t speed_rpm;       // 转速 (RPM)
    float total_angle;       // 总角度
    uint16_t ecd;            // 编码器值
    int16_t real_current;    // 实际电流
    uint8_t temperature;     // 温度
} chassis_motor_trans;  // 12字节
```

---

### 5. 底盘速度命令 (0x20)

**方向**: 上位机 → 下位机

**数据格式**:
```
[消息ID(1)] [linear_x(4)] [linear_y(4)] [angular_z(4)]
```

**字段说明**:
- `消息ID`: 0x20
- `linear_x`: 前后速度，float类型，单位 m/s
- `linear_y`: 左右速度，float类型，单位 m/s
- `angular_z`: 旋转角速度，float类型，单位 rad/s

**总长度**: 13字节

**示例代码（下位机接收）**:
```c
// 已在 trans_task.c 中实现，参考 TRANS_MSG_ID_CHASSIS_CMD 部分
```

---

## 使用示例

### 下位机端（STM32）

#### 1. 初始化
```c
void trans_task_init(void)
{
    // 已在 trans_task.c 中实现
}
```

#### 2. 发送IMU数据（周期性调用）
```c
void send_imu_periodic(void)
{
    // 获取IMU四元数
    float qx = imu.quat.x;
    float qy = imu.quat.y;
    float qz = imu.quat.z;
    float qw = imu.quat.w;
    
    // 发送
    uint8_t data[17];
    size_t offset = 0;
    
    data[offset++] = 0x01;  // MSG_ID_IMU_DATA
    memcpy(data + offset, &qx, sizeof(float)); offset += sizeof(float);
    memcpy(data + offset, &qy, sizeof(float)); offset += sizeof(float);
    memcpy(data + offset, &qz, sizeof(float)); offset += sizeof(float);
    memcpy(data + offset, &qw, sizeof(float)); offset += sizeof(float);
    
    send_custom_data(data, offset);
}
```

#### 3. 接收云台命令
```c
// 在 trans_task.c 的 process_usb_bytes() 函数中添加处理逻辑
```

---

### 上位机端（C++）

#### 1. 初始化
```cpp
#include "io/cboard_serial.hpp"

// 创建通信对象
io::CBoardSerial cboard("configs/standard_serial.yaml");
```

#### 2. 获取IMU数据
```cpp
auto timestamp = std::chrono::steady_clock::now();
Eigen::Quaterniond q = cboard.imu_at(timestamp);
```

#### 3. 发送云台命令
```cpp
io::Command cmd;
cmd.control = true;
cmd.shoot = false;
cmd.yaw = 10.5;    // 度
cmd.pitch = -5.2;  // 度

cboard.send(cmd);
```

---

## 配置文件

### YAML 配置示例

```yaml
# 虚拟串口参数
serial_device: "/dev/ttyACM0"  # 串口设备路径
serial_baudrate: 115200         # 波特率（支持：9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600）
```

---

## 常见问题

### 1. 如何确定虚拟串口设备路径？

在Linux系统中，插入USB设备后，可以使用以下命令查看：
```bash
ls /dev/ttyACM*
# 或
ls /dev/ttyUSB*
```

### 2. CRC16校验失败怎么办？

- 确保上下位机使用相同的CRC16算法（CRC16-CCITT）
- 确保初始值为 0xFFFF
- 确保字节序一致（小端序）
- 检查数据长度字段是否正确

### 3. 如何调试通信？

可以使用串口调试工具（如 `minicom`, `screen`, `cutecom`）监控数据：
```bash
# 使用 screen
screen /dev/ttyACM0 115200

# 使用 minicom
minicom -D /dev/ttyACM0 -b 115200
```

### 4. 断线重连机制

虚拟串口类 `VirtualSerial` 内置了断线重连机制：
- 守护线程每100ms检查连接状态
- 连接断开时自动尝试重新打开
- 无需手动处理断线情况

---

## 性能优化建议

1. **降低发送频率**: IMU数据建议100-200Hz，遥控器数据50Hz即可
2. **批量发送**: 将多个小数据包合并成一个大包发送
3. **使用DMA**: 下位机端使用DMA进行串口收发，减少CPU占用
4. **缓冲区大小**: 根据实际数据量调整接收缓冲区大小

---

## 版本历史

- **v1.0** (2025-01-XX): 初始版本，支持基本通信功能
- 使用 CRC16-CCITT 校验
- 支持断线重连
- 支持多种消息类型

---

## 联系方式

如有问题，请联系开发团队。
