# USB虚拟串口通信快速参考（极简版本）

## 消息ID速查表

| ID | 名称 | 方向 | 内容 | 数据长度 | 完整帧长度 |
|----|------|------|------|----------|------------|
| 0x01 | STM32状态 | STM32→PC | IMU四元数 + 弹速和模式 | 23字节 | 29字节 |
| 0x02 | 视觉命令 | PC→STM32 | 控制 + 射击 + Yaw + Pitch | 11字节 | 17字节 |

**完整帧格式**：`[0xFF][0xAA][长度(2)][数据(N)][CRC16(2)]`

---

## 下位机（STM32）快速使用

### 初始化
```c
#include "trans_task.h"

// 在main()中调用一次
trans_task_init();

// 在主循环或RTOS任务中周期性调用
void trans_task(void) {
    trans_control_task();  // 50-100Hz
}
```

### 发送数据（自动批量发送）
```c
// 在trans_control_task()中自动调用send_stm32_status()
// 会自动批量上传：IMU四元数 + 弹速和模式
// 无需手动调用
```

### 修改数据源
```c
// 在 trans_task.c 的 send_stm32_status() 函数中修改：

// 1. 修改IMU数据源
float imu_qx = gim_ins.quat.x;  // 替换为你的IMU数据
float imu_qy = gim_ins.quat.y;
float imu_qz = gim_ins.quat.z;
float imu_qw = gim_ins.quat.w;

// 2. 修改弹速和模式数据源
float bullet_speed = get_bullet_speed();  // 替换为你的弹速获取函数
uint8_t robot_mode = get_robot_mode();    // 替换为你的模式获取函数
uint8_t shoot_mode = get_shoot_mode();    // 替换为你的射击模式获取函数
```

### 接收视觉控制命令
```c
// 在 trans_task.c 的 process_usb_bytes() 函数中添加：
case TRANS_MSG_ID_VISION_COMMAND:
{
    uint8_t control_flag = data[offset++];
    uint8_t shoot_flag = data[offset++];
    float yaw, pitch;
    memcpy(&yaw, data + offset, sizeof(float)); offset += sizeof(float);
    memcpy(&pitch, data + offset, sizeof(float)); offset += sizeof(float);
    
    // 添加你的云台控制逻辑
    if (control_flag) {
        gimbal_set_target_angle(yaw, pitch);
    }
    if (shoot_flag) {
        shooter_trigger();
    }
    break;
}
```

---

## 上位机（C++）快速使用

### 初始化
```cpp
#include "io/cboard_serial.hpp"

// 创建通信对象
io::CBoardSerial cboard("configs/standard_serial.yaml");
```

### 接收数据（自动批量接收）
```cpp
// 1. 获取IMU数据
auto timestamp = std::chrono::steady_clock::now();
Eigen::Quaterniond q = cboard.imu_at(timestamp);

// 2. 获取弹速和模式
double bullet_speed = cboard.bullet_speed;
io::Mode mode = cboard.mode;  // idle, auto_aim, small_buff, big_buff, outpost
io::ShootMode shoot_mode = cboard.shoot_mode;  // left_shoot, right_shoot, both_shoot
```

### 发送视觉控制命令
```cpp
io::Command cmd;
cmd.control = true;   // 是否控制云台
cmd.shoot = false;    // 是否射击
cmd.yaw = 10.5;       // Yaw角度（度）
cmd.pitch = -5.2;     // Pitch角度（度）

cboard.send(cmd);
```

---

## 数据格式速查

### 0x01: STM32完整状态（91字节）
```
[0x01]
[IMU: qx(4) qy(4) qz(4) qw(4)]                    // 16字节
[弹速模式: speed(4) mode(1) shoot_mode(1)]        // 6字节
[遥控器: ch1-4(8) sw1-2(2) mouse(6) kb(4)]       // 20字节
[电机×4: id(1) rpm(2) angle(4) ecd(2) cur(2) temp(1)] // 48字节
```

### 0x02: 视觉控制命令（11字节）
```
[0x02][control(1)][shoot(1)][yaw(4)][pitch(4)]
```

---

## 配置文件

### configs/standard_serial.yaml
```yaml
serial_device: "/dev/ttyACM0"  # Linux设备路径
serial_baudrate: 115200         # 波特率
```

**查看设备**：
```bash
ls /dev/ttyACM*
# 或
dmesg | grep tty
```

**权限设置**：
```bash
sudo chmod 666 /dev/ttyACM0
# 或永久设置
sudo usermod -a -G dialout $USER
```

---

## 调试命令

### 查看串口数据
```bash
# 使用screen
screen /dev/ttyACM0 115200

# 使用minicom
minicom -D /dev/ttyACM0 -b 115200

# 使用picocom
picocom /dev/ttyACM0 -b 115200
```

### 查看日志
```bash
# 上位机日志会自动输出到终端
# 查找错误信息
grep -i "error\|warn\|fail" log.txt
```

---

## 常见错误代码

| 错误信息 | 原因 | 解决方法 |
|---------|------|----------|
| `Failed to open serial port` | 设备不存在或权限不足 | 检查设备路径，设置权限 |
| `CRC16 mismatch` | 数据传输错误 | 检查连接，降低发送频率 |
| `Unknown message ID` | 消息ID不匹配 | 检查上下位机代码版本 |
| `Serial read error` | 串口读取失败 | 检查USB连接 |
| `RX buffer overflow` | 接收缓冲区溢出 | 降低发送频率，增加缓冲区 |

---

## 性能参数

| 参数 | 推荐值 | 最大值 |
|------|--------|--------|
| STM32状态发送频率 | 50-100Hz | 200Hz |
| 视觉命令发送频率 | 按需 | 100Hz |
| 单帧数据长度 | 91字节 | 512字节 |
| 波特率 | 115200 | 921600 |
| 延迟 | <5ms | <10ms |

---

## 模式枚举

### 机器人模式（Mode）
```cpp
enum Mode {
    idle = 0,        // 空闲
    auto_aim = 1,    // 自瞄
    small_buff = 2,  // 小符
    big_buff = 3,    // 大符
    outpost = 4      // 前哨站
};
```

### 射击模式（ShootMode）
```cpp
enum ShootMode {
    left_shoot = 0,   // 左侧射击
    right_shoot = 1,  // 右侧射击
    both_shoot = 2    // 双侧射击
};
```

---

## 完整示例

### 下位机完整示例
```c
#include "trans_task.h"

void communication_task(void)
{
    // 处理接收和发送（自动批量上传所有数据）
    trans_control_task();
    
    // 处理视觉控制命令（在process_usb_bytes中自动处理）
}
```

### 上位机完整示例
```cpp
#include "io/cboard_serial.hpp"

int main() {
    io::CBoardSerial cboard("configs/standard_serial.yaml");
    
    while (true) {
        // 接收IMU
        auto ts = std::chrono::steady_clock::now();
        Eigen::Quaterniond q = cboard.imu_at(ts);
        
        // 接收弹速和模式
        double speed = cboard.bullet_speed;
        io::Mode mode = cboard.mode;
        
        // 发送视觉控制命令
        io::Command cmd;
        cmd.control = true;
        cmd.yaw = 0.0;
        cmd.pitch = 0.0;
        cboard.send(cmd);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
```

---

## 检查清单

### 下位机
- [ ] 已调用 `trans_task_init()`
- [ ] 已周期性调用 `trans_control_task()`
- [ ] 已在 `usbd_cdc_if.c` 中调用 `process_usb_data()`
- [ ] 已修改IMU数据源
- [ ] 已修改弹速和模式数据源
- [ ] 已实现视觉控制命令处理逻辑

### 上位机
- [ ] 已配置 `standard_serial.yaml`
- [ ] 已设置串口设备权限
- [ ] 已测试IMU数据接收
- [ ] 已测试弹速和模式接收
- [ ] 已测试视觉控制命令发送

---

## 批量通信优势

### 对比原来的分次发送

**数据量**：
- 原来：111字节（分3次发送）
- 现在：97字节（1次发送）
- **节省**：14字节（12.6%）

**传输次数**：
- 原来：5次（IMU、弹速、遥控器、云台命令、底盘命令）
- 现在：2次（STM32状态、视觉命令）
- **减少**：3次（60%）

**优点**：
- ✅ 减少通信次数，降低协议开销
- ✅ 数据同步性更好（所有数据来自同一时刻）
- ✅ 简化代码逻辑
- ✅ 提高传输效率

---

## 更多信息

- 详细协议：[batch_protocol_summary.md](batch_protocol_summary.md)
- 集成指南：[usb_serial_integration_guide.md](usb_serial_integration_guide.md)
- 文档导航：[README_SERIAL_COMM.md](README_SERIAL_COMM.md)
