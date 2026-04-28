# STM32 下位机集成指南

## 概述

本文档说明如何在 STM32 下位机中集成虚拟串口通信功能，替代原有的 CAN 通信。

---

## 1. 修改 trans_task.c

您已经有了 `trans_task.c` 和 `trans_task.h` 文件，这些文件已经实现了 CRC16 协议。现在需要添加发送 IMU 数据和弹速数据的功能。

### 1.1 在 trans_task.c 中添加发送函数

在 `trans_task.c` 文件的 `/* ==================== 上发数据类型定义 ==================== */` 部分添加新的消息 ID：

```c
typedef enum
{
    TRANS_MSG_ID_IMU_DATA = 0x01,      // IMU数据（四元数）
    TRANS_MSG_ID_BULLET_SPEED = 0x02,  // 弹速和模式
    TRANS_MSG_ID_GIMBAL_COMMAND = 0x03,// 云台控制命令（接收）
    TRANS_MSG_ID_RC_DBUS = 0x10,       // 遥控器数据
    TRANS_MSG_ID_CHASSIS_CMD = 0x20,   // 底盘速度命令（接收）
} trans_msg_id_e;
```

### 1.2 添加发送 IMU 数据函数

```c
/**
 * @brief 发送IMU数据（四元数）
 * @param qx 四元数 x 分量
 * @param qy 四元数 y 分量
 * @param qz 四元数 z 分量
 * @param qw 四元数 w 分量
 */
void send_imu_data(float qx, float qy, float qz, float qw)
{
    uint8_t data[17];
    size_t offset = 0;
    
    // 消息ID
    data[offset++] = (uint8_t)TRANS_MSG_ID_IMU_DATA;
    
    // 四元数分量
    memcpy(data + offset, &qx, sizeof(float)); offset += sizeof(float);
    memcpy(data + offset, &qy, sizeof(float)); offset += sizeof(float);
    memcpy(data + offset, &qz, sizeof(float)); offset += sizeof(float);
    memcpy(data + offset, &qw, sizeof(float)); offset += sizeof(float);
    
    send_custom_data(data, (uint16_t)offset);
}
```

### 1.3 添加发送弹速和模式函数

```c
/**
 * @brief 发送弹速和模式信息
 * @param bullet_speed 弹速 (m/s)
 * @param mode 模式 (0=idle, 1=auto_aim, 2=small_buff, 3=big_buff, 4=outpost)
 * @param shoot_mode 射击模式 (0=left, 1=right, 2=both)
 */
void send_bullet_speed_mode(float bullet_speed, uint8_t mode, uint8_t shoot_mode)
{
    uint8_t data[7];
    size_t offset = 0;
    
    // 消息ID
    data[offset++] = (uint8_t)TRANS_MSG_ID_BULLET_SPEED;
    
    // 弹速
    memcpy(data + offset, &bullet_speed, sizeof(float)); offset += sizeof(float);
    
    // 模式
    data[offset++] = mode;
    data[offset++] = shoot_mode;
    
    send_custom_data(data, (uint16_t)offset);
}
```

### 1.4 添加接收云台命令的处理

在 `process_usb_bytes()` 函数的 `switch (msg_id)` 部分添加：

```c
case TRANS_MSG_ID_GIMBAL_COMMAND:
{
    // 解析云台控制命令：2个uint8_t + 2个float
    if (expected_data_length >= 1 + 1 + 1 + 4 + 4)
    {
        uint8_t control_flag = data[offset++];
        uint8_t shoot_flag = data[offset++];
        
        float yaw, pitch;
        memcpy(&yaw, data + offset, sizeof(float)); offset += sizeof(float);
        memcpy(&pitch, data + offset, sizeof(float)); offset += sizeof(float);
        
        // TODO: 在这里添加你的云台控制逻辑
        // 例如：
        // gimbal_set_target(yaw, pitch);
        // if (shoot_flag) {
        //     trigger_shoot();
        // }
        
        // 可以通过全局变量或消息队列传递给云台控制任务
    }
    break;
}
```

---

## 2. 在 trans_task.h 中添加函数声明

```c
/**
 * @brief 发送IMU数据（四元数）
 * @param qx 四元数 x 分量
 * @param qy 四元数 y 分量
 * @param qz 四元数 z 分量
 * @param qw 四元数 w 分量
 */
void send_imu_data(float qx, float qy, float qz, float qw);

/**
 * @brief 发送弹速和模式信息
 * @param bullet_speed 弹速 (m/s)
 * @param mode 模式
 * @param shoot_mode 射击模式
 */
void send_bullet_speed_mode(float bullet_speed, uint8_t mode, uint8_t shoot_mode);
```

---

## 3. 在主任务中调用发送函数

### 3.1 发送 IMU 数据（高频率，如 100Hz）

```c
void imu_task(void)
{
    // 假设你有一个 IMU 数据结构
    extern struct imu_data_t imu;
    
    // 每 10ms 发送一次（100Hz）
    static uint32_t last_send_time = 0;
    if (dwt_get_time_ms() - last_send_time >= 10)
    {
        last_send_time = dwt_get_time_ms();
        
        // 发送四元数
        send_imu_data(imu.quat.x, imu.quat.y, imu.quat.z, imu.quat.w);
    }
}
```

### 3.2 发送弹速和模式（低频率，如 10Hz）

```c
void status_task(void)
{
    // 假设你有弹速和模式变量
    extern float current_bullet_speed;
    extern uint8_t current_mode;
    extern uint8_t current_shoot_mode;
    
    // 每 100ms 发送一次（10Hz）
    static uint32_t last_send_time = 0;
    if (dwt_get_time_ms() - last_send_time >= 100)
    {
        last_send_time = dwt_get_time_ms();
        
        send_bullet_speed_mode(current_bullet_speed, current_mode, current_shoot_mode);
    }
}
```

---

## 4. USB 虚拟串口配置

### 4.1 STM32CubeMX 配置

1. 启用 USB Device
2. 选择 Class: Communication Device Class (CDC)
3. 配置 USB 参数：
   - Speed: High Speed (如果支持) 或 Full Speed
   - 端点大小: 64 字节（Full Speed）或 512 字节（High Speed）

### 4.2 修改 usbd_cdc_if.c

在 `CDC_Receive_HS()` 或 `CDC_Receive_FS()` 函数中调用 `process_usb_data()`：

```c
static int8_t CDC_Receive_HS(uint8_t* Buf, uint32_t *Len)
{
    /* USER CODE BEGIN 6 */
    
    // 调用通信任务的接收处理函数
    process_usb_data(Buf, Len);
    
    // 准备接收下一包数据
    USBD_CDC_SetRxBuffer(&hUsbDeviceHS, &Buf[0]);
    USBD_CDC_ReceivePacket(&hUsbDeviceHS);
    
    return (USBD_OK);
    /* USER CODE END 6 */
}
```

---

## 5. 完整示例：主循环集成

```c
void main_loop(void)
{
    // 初始化通信任务
    trans_task_init();
    
    while (1)
    {
        // 通信任务（处理接收和发送）
        trans_control_task();
        
        // 发送 IMU 数据（100Hz）
        static uint32_t imu_last_time = 0;
        if (dwt_get_time_ms() - imu_last_time >= 10)
        {
            imu_last_time = dwt_get_time_ms();
            
            // 获取 IMU 数据
            float qx = imu.quat.x;
            float qy = imu.quat.y;
            float qz = imu.quat.z;
            float qw = imu.quat.w;
            
            // 发送
            send_imu_data(qx, qy, qz, qw);
        }
        
        // 发送弹速和模式（10Hz）
        static uint32_t status_last_time = 0;
        if (dwt_get_time_ms() - status_last_time >= 100)
        {
            status_last_time = dwt_get_time_ms();
            
            send_bullet_speed_mode(bullet_speed, mode, shoot_mode);
        }
        
        // 其他任务...
        
        osDelay(1);  // 如果使用 FreeRTOS
    }
}
```

---

## 6. 调试技巧

### 6.1 使用串口调试助手

在 Windows 上可以使用：
- SSCOM
- Serial Port Utility
- Tera Term

在 Linux 上可以使用：
- minicom
- screen
- cutecom

### 6.2 查看原始数据

```bash
# Linux 下查看原始数据
hexdump -C /dev/ttyACM0
```

### 6.3 添加调试输出

在 `process_usb_bytes()` 中添加：

```c
if (received_crc == calculated_crc)
{
    // CRC校验通过
    printf("RX: MsgID=0x%02X, Len=%d\r\n", data[0], expected_data_length);
    
    // ... 解析数据
}
else
{
    printf("CRC Error! RX=0x%04X, Calc=0x%04X\r\n", received_crc, calculated_crc);
}
```

---

## 7. 性能优化

### 7.1 使用 DMA

配置 USB 使用 DMA 传输，减少 CPU 占用：

```c
// 在 STM32CubeMX 中启用 USB DMA
```

### 7.2 调整缓冲区大小

根据实际数据量调整 `USB_RX_MSG_COUNT` 和 `USB_TX_MSG_COUNT`：

```c
#define USB_RX_MSG_COUNT        16  // 增加接收队列深度
#define USB_TX_MSG_COUNT        16  // 增加发送队列深度
```

### 7.3 降低发送频率

- IMU 数据：100-200Hz
- 弹速/模式：10-20Hz
- 遥控器数据：50Hz

---

## 8. 常见问题

### Q1: USB 设备无法识别

**A**: 检查 USB 描述符配置，确保 VID/PID 正确。

### Q2: 数据丢包

**A**: 
- 增加队列深度
- 降低发送频率
- 检查 USB 带宽是否足够

### Q3: CRC 校验失败

**A**:
- 确保上下位机使用相同的 CRC16 算法
- 检查字节序（小端序）
- 确认数据长度字段正确

### Q4: 接收不到数据

**A**:
- 检查 `CDC_Receive_HS()` 是否正确调用 `process_usb_data()`
- 确认 USB 连接正常
- 使用串口调试工具验证数据发送

---

## 9. 与原 CAN 通信的对比

| 特性 | CAN 通信 | 虚拟串口通信 |
|------|---------|-------------|
| 硬件接口 | CAN 收发器 | USB |
| 波特率 | 1Mbps | 12Mbps (Full Speed) / 480Mbps (High Speed) |
| 协议开销 | CAN 帧头 | 自定义协议头 + CRC16 |
| 数据长度 | 8 字节/帧 | 最大 512 字节/帧 |
| 可靠性 | CAN 硬件 CRC | CRC16 软件校验 |
| 调试难度 | 需要 CAN 分析仪 | 普通串口工具即可 |
| 成本 | 需要 CAN 收发器 | 无额外硬件 |

---

## 10. 总结

通过以上步骤，您可以将原有的 CAN 通信替换为虚拟串口通信：

1. ✅ 使用 CRC16 协议保证数据可靠性
2. ✅ 支持多种消息类型
3. ✅ 内置断线重连机制
4. ✅ 易于调试和扩展

如有问题，请参考 `docs/serial_protocol.md` 文档。
