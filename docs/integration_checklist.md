# USB虚拟串口通信集成检查清单

## 使用说明

本检查清单帮助您逐步完成USB虚拟串口通信系统的集成和测试。请按顺序完成每一项，并在完成后打勾。

---

## 一、准备工作

### 硬件准备

- [ ] STM32开发板（支持USB Device功能）
- [ ] USB数据线（确保支持数据传输，不是仅充电线）
- [ ] 上位机（Linux系统，或Windows系统需要额外配置）
- [ ] 调试器（ST-Link / J-Link，可选）

### 软件准备

- [ ] STM32CubeIDE / Keil MDK / IAR（下位机开发环境）
- [ ] GCC / Clang / Visual Studio（上位机开发环境）
- [ ] 串口调试工具（screen / minicom / picocom）
- [ ] Git（版本控制）

---

## 二、下位机端（STM32）集成

### 2.1 文件集成

- [ ] 已将 `trans_task.c` 添加到项目
- [ ] 已将 `trans_task.h` 添加到项目
- [ ] 已在项目中包含必要的头文件路径
- [ ] 已在编译配置中添加源文件

### 2.2 USB CDC配置

- [ ] 已在STM32CubeMX中启用USB Device功能
- [ ] 已选择USB CDC类
- [ ] 已配置USB引脚（通常是PA11/PA12或PB14/PB15）
- [ ] 已生成代码
- [ ] 已在 `usbd_cdc_if.c` 中调用 `process_usb_data()`

**验证代码**：
```c
// 在 usbd_cdc_if.c 的 CDC_Receive_HS() 或 CDC_Receive_FS() 中
static int8_t CDC_Receive_HS(uint8_t* Buf, uint32_t *Len)
{
    process_usb_data(Buf, Len);  // ✓ 确认已添加此行
    USBD_CDC_SetRxBuffer(&hUsbDeviceHS, &Buf[0]);
    USBD_CDC_ReceivePacket(&hUsbDeviceHS);
    return (USBD_OK);
}
```

### 2.3 初始化代码

- [ ] 已在 `main()` 函数中调用 `trans_task_init()`
- [ ] 已在主循环或RTOS任务中调用 `trans_control_task()`
- [ ] 调用频率设置为100-500Hz

**验证代码**：
```c
// 在 main.c 中
int main(void)
{
    // ... 其他初始化代码 ...
    
    trans_task_init();  // ✓ 确认已添加此行
    
    while (1)
    {
        trans_control_task();  // ✓ 确认已添加此行
        // ... 其他任务 ...
    }
}
```

### 2.4 数据发送实现

- [ ] 已实现IMU数据发送
- [ ] 已实现弹速和模式发送
- [ ] 遥控器数据自动发送（无需额外代码）

**示例代码**：
```c
// 在你的IMU任务中
void imu_task(void)
{
    // 获取IMU数据
    float qx = imu.quat.x;
    float qy = imu.quat.y;
    float qz = imu.quat.z;
    float qw = imu.quat.w;
    
    // 发送IMU数据
    send_imu_data(qx, qy, qz, qw);  // ✓ 确认已添加此行
}

// 在你的状态任务中
void status_task(void)
{
    float bullet_speed = get_bullet_speed();
    uint8_t mode = get_robot_mode();
    uint8_t shoot_mode = get_shoot_mode();
    
    send_bullet_speed(bullet_speed, mode, shoot_mode);  // ✓ 确认已添加此行
}
```

### 2.5 数据接收实现

- [ ] 已实现云台命令接收处理
- [ ] 已实现底盘速度命令接收处理

**云台命令处理**：
```c
// 在 trans_task.c 的 process_usb_bytes() 中
case TRANS_MSG_ID_GIMBAL_COMMAND:
{
    if (expected_data_length >= 1 + 1 + 1 + 4 + 4)
    {
        uint8_t control_flag = data[offset++];
        uint8_t shoot_flag = data[offset++];
        
        float yaw, pitch;
        memcpy(&yaw, data + offset, sizeof(float)); offset += sizeof(float);
        memcpy(&pitch, data + offset, sizeof(float)); offset += sizeof(float);
        
        // ✓ 在这里添加你的云台控制逻辑
        gimbal_set_target_angle(yaw, pitch);
        if (shoot_flag) {
            shooter_trigger();
        }
    }
    break;
}
```

**底盘命令处理**：
```c
// 在你的底盘任务中
void chassis_task(void)
{
    chassis_cmd_received_t* cmd = get_chassis_cmd_received();
    if (cmd->updated)
    {
        // ✓ 在这里添加你的底盘控制逻辑
        chassis_set_velocity(cmd->linear_x, cmd->linear_y, cmd->angular_z);
        cmd->updated = 0;
    }
}
```

### 2.6 编译和烧录

- [ ] 代码编译无错误
- [ ] 代码编译无警告（或已确认警告无害）
- [ ] 已成功烧录到STM32
- [ ] 程序正常运行

---

## 三、上位机端（C++）集成

### 3.1 文件更新

- [ ] 已更新 `io/cboard_serial.hpp`（添加 `send_chassis_cmd()` 方法）
- [ ] 已确认 `io/virtual_serial.hpp` 存在且无需修改
- [ ] 已确认 `io/command.hpp` 存在

### 3.2 配置文件

- [ ] 已创建或更新 `configs/standard_serial.yaml`
- [ ] 已设置正确的设备路径（如 `/dev/ttyACM0`）
- [ ] 已设置正确的波特率（通常是115200）

**配置文件示例**：
```yaml
serial_device: "/dev/ttyACM0"  # ✓ 确认设备路径正确
serial_baudrate: 115200         # ✓ 确认波特率正确
```

### 3.3 权限设置（Linux）

- [ ] 已检查串口设备是否存在
- [ ] 已设置串口设备权限
- [ ] 已将用户添加到dialout组（永久解决方案）

**权限设置命令**：
```bash
# 检查设备
ls /dev/ttyACM*  # ✓ 确认设备存在

# 临时设置权限
sudo chmod 666 /dev/ttyACM0

# 永久设置权限
sudo usermod -a -G dialout $USER
# 然后注销重新登录
```

### 3.4 代码集成

- [ ] 已在代码中创建 `CBoardSerial` 对象
- [ ] 已实现IMU数据接收
- [ ] 已实现弹速和模式接收
- [ ] 已实现云台命令发送
- [ ] 已实现底盘速度命令发送

**示例代码**：
```cpp
#include "io/cboard_serial.hpp"

// 创建通信对象
io::CBoardSerial cboard("configs/standard_serial.yaml");  // ✓ 确认已添加

// 接收IMU数据
auto timestamp = std::chrono::steady_clock::now();
Eigen::Quaterniond q = cboard.imu_at(timestamp);  // ✓ 确认已添加

// 发送云台命令
io::Command cmd;
cmd.control = true;
cmd.yaw = 0.0;
cmd.pitch = 0.0;
cboard.send(cmd);  // ✓ 确认已添加

// 发送底盘速度命令
cboard.send_chassis_cmd(1.0f, 0.5f, 0.2f);  // ✓ 确认已添加
```

### 3.5 编译和运行

- [ ] 代码编译无错误
- [ ] 代码编译无警告（或已确认警告无害）
- [ ] 程序正常运行
- [ ] 日志输出正常

---

## 四、功能测试

### 4.1 连接测试

- [ ] USB设备已连接
- [ ] 设备在系统中可见（`ls /dev/ttyACM*`）
- [ ] 上位机成功打开串口
- [ ] 日志显示 "VirtualSerial opened"

**预期日志**：
```
[info] CBoardSerial initialized on /dev/ttyACM0
[info] VirtualSerial opened: /dev/ttyACM0
```

### 4.2 IMU数据测试

- [ ] 下位机正常发送IMU数据
- [ ] 上位机正常接收IMU数据
- [ ] 四元数数值合理（模长接近1）
- [ ] 数据更新频率正常（100-200Hz）

**测试代码**：
```cpp
// 上位机测试代码
auto q = cboard.imu_at(std::chrono::steady_clock::now());
double norm = std::sqrt(q.w()*q.w() + q.x()*q.x() + q.y()*q.y() + q.z()*q.z());
std::cout << "Quaternion norm: " << norm << std::endl;  // 应该接近1.0
```

### 4.3 弹速和模式测试

- [ ] 下位机正常发送弹速和模式
- [ ] 上位机正常接收弹速和模式
- [ ] 数值合理
- [ ] 数据更新频率正常（10-50Hz）

**测试代码**：
```cpp
// 上位机测试代码
std::cout << "Bullet Speed: " << cboard.bullet_speed << " m/s" << std::endl;
std::cout << "Mode: " << io::MODES[cboard.mode] << std::endl;
std::cout << "Shoot Mode: " << io::SHOOT_MODES[cboard.shoot_mode] << std::endl;
```

### 4.4 云台命令测试

- [ ] 上位机正常发送云台命令
- [ ] 下位机正常接收云台命令
- [ ] 云台响应正常
- [ ] 射击功能正常

**测试代码**：
```cpp
// 上位机测试代码
io::Command cmd;
cmd.control = true;
cmd.shoot = false;
cmd.yaw = 10.0;
cmd.pitch = -5.0;
cboard.send(cmd);
```

### 4.5 底盘速度命令测试

- [ ] 上位机正常发送底盘速度命令
- [ ] 下位机正常接收底盘速度命令
- [ ] 底盘响应正常
- [ ] 速度控制准确

**测试代码**：
```cpp
// 上位机测试代码
cboard.send_chassis_cmd(1.0f, 0.0f, 0.0f);  // 前进1m/s
std::this_thread::sleep_for(std::chrono::seconds(2));
cboard.send_chassis_cmd(0.0f, 0.0f, 0.0f);  // 停止
```

### 4.6 遥控器数据测试

- [ ] 下位机正常发送遥控器数据
- [ ] 上位机正常接收遥控器数据（如果需要的话）
- [ ] 电机反馈数据正常
- [ ] 数据更新频率正常（50Hz）

---

## 五、性能测试

### 5.1 延迟测试

- [ ] 测量端到端延迟
- [ ] 延迟 < 5ms（正常）
- [ ] 延迟 < 10ms（可接受）

**测试方法**：
```cpp
// 上位机发送命令并记录时间戳
auto t1 = std::chrono::steady_clock::now();
cboard.send(cmd);

// 下位机接收并立即回传
// 上位机接收并计算延迟
auto t2 = std::chrono::steady_clock::now();
auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
std::cout << "Latency: " << latency << " ms" << std::endl;
```

### 5.2 吞吐量测试

- [ ] 测量数据传输速率
- [ ] IMU数据：100-200Hz
- [ ] 遥控器数据：50Hz
- [ ] 无数据丢失

**测试方法**：
```cpp
// 统计接收到的数据包数量
int count = 0;
auto start = std::chrono::steady_clock::now();

while (true) {
    auto q = cboard.imu_at(std::chrono::steady_clock::now());
    count++;
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
    if (elapsed >= 1) {
        std::cout << "IMU data rate: " << count << " Hz" << std::endl;
        count = 0;
        start = now;
    }
}
```

### 5.3 稳定性测试

- [ ] 长时间运行测试（1小时）
- [ ] 无崩溃
- [ ] 无内存泄漏
- [ ] 无数据丢失

### 5.4 断线重连测试

- [ ] 拔掉USB线
- [ ] 重新插入USB线
- [ ] 自动重连成功
- [ ] 数据传输恢复正常

---

## 六、错误处理测试

### 6.1 CRC校验测试

- [ ] 发送错误数据
- [ ] CRC校验失败
- [ ] 错误数据被丢弃
- [ ] 日志输出 "CRC16 mismatch"

### 6.2 超时测试

- [ ] 停止发送数据
- [ ] 检测到超时
- [ ] 触发重连机制

### 6.3 异常数据测试

- [ ] 发送超长数据
- [ ] 发送错误的消息ID
- [ ] 系统正常处理
- [ ] 无崩溃

---

## 七、文档检查

### 7.1 代码注释

- [ ] 关键函数有注释
- [ ] 复杂逻辑有说明
- [ ] 参数有说明
- [ ] 返回值有说明

### 7.2 使用文档

- [ ] 已阅读 [quick_reference.md](quick_reference.md)
- [ ] 已阅读 [serial_protocol.md](serial_protocol.md)
- [ ] 已阅读 [usb_serial_integration_guide.md](usb_serial_integration_guide.md)
- [ ] 已阅读 [migration_summary.md](migration_summary.md)

### 7.3 配置文件

- [ ] 配置文件有注释
- [ ] 配置项有说明
- [ ] 提供了示例配置

---

## 八、部署准备

### 8.1 版本控制

- [ ] 代码已提交到Git
- [ ] 提交信息清晰
- [ ] 已打标签（如 v1.0）

### 8.2 备份

- [ ] 已备份原始代码
- [ ] 已备份配置文件
- [ ] 已备份文档

### 8.3 发布

- [ ] 已更新版本号
- [ ] 已更新更新日志
- [ ] 已通知相关人员

---

## 九、常见问题排查

### 问题1：无法打开串口

**检查项**：
- [ ] 设备路径是否正确（`ls /dev/ttyACM*`）
- [ ] 权限是否足够（`sudo chmod 666 /dev/ttyACM0`）
- [ ] 设备是否被占用（`lsof /dev/ttyACM0`）
- [ ] USB线是否支持数据传输

### 问题2：CRC校验失败

**检查项**：
- [ ] 上下位机使用相同的CRC16算法
- [ ] 初始值为0xFFFF
- [ ] 字节序一致（小端序）
- [ ] 数据长度字段正确

### 问题3：数据丢失

**检查项**：
- [ ] 发送频率是否过高
- [ ] 缓冲区是否溢出
- [ ] USB带宽是否足够
- [ ] 是否使用了DMA

### 问题4：延迟过高

**检查项**：
- [ ] CPU占用率是否过高
- [ ] 是否有其他高优先级任务
- [ ] 是否使用了阻塞式IO
- [ ] 缓冲区大小是否合适

---

## 十、最终确认

### 10.1 功能完整性

- [ ] 所有消息类型都已实现
- [ ] 所有功能都已测试
- [ ] 所有错误都已修复

### 10.2 性能达标

- [ ] 延迟 < 10ms
- [ ] 吞吐量满足需求
- [ ] CPU占用率 < 50%
- [ ] 内存占用合理

### 10.3 可靠性

- [ ] 长时间运行稳定
- [ ] 断线重连正常
- [ ] 错误处理完善
- [ ] 无内存泄漏

### 10.4 文档完整

- [ ] 代码注释完整
- [ ] 使用文档完整
- [ ] 示例代码完整
- [ ] 故障排查指南完整

---

## 完成！

恭喜！如果您已经完成了以上所有检查项，说明USB虚拟串口通信系统已经成功集成并可以投入使用了。

如果遇到任何问题，请参考：
- [故障排查指南](README_SERIAL_COMM.md#-故障排查)
- [常见问题](usb_serial_integration_guide.md#四调试和测试)

---

**检查日期**：__________  
**检查人员**：__________  
**版本**：v1.0
