# 上位机与下位机双向通信详解

## 🔄 通信概览

上位机和下位机之间是**双向通信**：

```
┌─────────────────────────────────────────────────────────────┐
│                      下位机 (STM32)                          │
│                                                              │
│  发送 ↑                                    接收 ↓           │
│  - IMU 四元数                              - Yaw 角度        │
│  - 弹速                                    - Pitch 角度      │
│  - 工作模式                                - 控制标志        │
│                                            - 射击标志        │
└───────────────┬──────────────────────┬─────────────────────┘
                │ 上传                  │ 下发
                │ (100-200 Hz)         │ (60-200 Hz)
┌───────────────▼──────────────────────▼─────────────────────┐
│                      上位机 (视觉系统)                        │
│                                                              │
│  接收 ↓                                    发送 ↑           │
│  - IMU 四元数                              - Yaw 角度        │
│  - 弹速                                    - Pitch 角度      │
│  - 工作模式                                - 控制标志        │
│                                            - 射击标志        │
└─────────────────────────────────────────────────────────────┘
```

---

## 📤 上位机发送给下位机的数据

### **控制命令结构**

```cpp
// io/command.hpp
struct Command {
  bool control;           // 控制标志：是否启用自瞄控制
  bool shoot;             // 射击标志：是否开火
  double yaw;             // Yaw 角度（弧度）⭐⭐⭐
  double pitch;           // Pitch 角度（弧度）⭐⭐⭐
  double horizon_distance; // 水平距离（无人机专用）
};
```

---

### **发送函数**

```cpp
// io/cboard_serial.hpp
void CBoardSerial::send(Command command) const
{
  // 构建数据包
  uint8_t data[32];
  size_t offset = 0;

  // 1. 消息 ID
  data[offset++] = MSG_ID_VISION_COMMAND;  // 0x02

  // 2. 控制标志（1 字节）
  data[offset++] = command.control ? 1 : 0;

  // 3. 射击标志（1 字节）
  data[offset++] = command.shoot ? 1 : 0;

  // 4. Yaw 角度（float，4 字节）⭐⭐⭐
  float yaw = static_cast<float>(command.yaw);
  std::memcpy(data + offset, &yaw, sizeof(float));
  offset += sizeof(float);

  // 5. Pitch 角度（float，4 字节）⭐⭐⭐
  float pitch = static_cast<float>(command.pitch);
  std::memcpy(data + offset, &pitch, sizeof(float));
  offset += sizeof(float);

  // 6. 发送（自动添加帧头和 CRC16）
  serial_->write(data, offset);
}
```

---

### **数据包格式**

```
┌────┬────┬────┬────┬──────────┬────┬────┐
│0xFF│0xAA│长度L│长度H│  数据(N)  │CRC低│CRC高│
└────┴────┴────┴────┴──────────┴────┴────┘
                     │
                     └─ 数据部分详细格式：
                        ┌────┬────┬────┬────────┬────────┐
                        │0x02│控制│射击│Yaw(4)  │Pitch(4)│
                        └────┴────┴────┴────────┴────────┘
                         ID   1B   1B   float    float
```

**总长度：** 帧头(2) + 长度(2) + 数据(11) + CRC(2) = **17 字节**

---

## 📥 下位机发送给上位机的数据

### **状态数据结构**

```c
// 下位机代码（STM32）
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

**总长度：** 1 + 16 + 4 + 1 + 1 = **23 字节**

---

## 🔄 完整通信流程

### **主循环中的数据流**

```cpp
// src/standard.cpp
int main() {
  io::CBoardSerial cboard(config_path);
  io::Camera camera(config_path);
  
  auto_aim::YOLO detector(config_path);
  auto_aim::Solver solver(config_path);
  auto_aim::Tracker tracker(config_path, solver);
  auto_aim::Aimer aimer(config_path);
  
  while (!exiter.exit()) {
    // ═══════════════════════════════════════════════════════
    // 第一步：接收下位机数据
    // ═══════════════════════════════════════════════════════
    
    // 1. 读取相机图像（带时间戳）
    camera.read(img, t);
    
    // 2. 获取下位机发送的 IMU 四元数 ⬇️⬇️⬇️
    q = cboard.imu_at(t - 1ms);
    
    // 3. 获取下位机发送的工作模式 ⬇️⬇️⬇️
    mode = cboard.mode;
    
    // 4. 获取下位机发送的弹速 ⬇️⬇️⬇️
    double speed = cboard.bullet_speed;
    
    // ═══════════════════════════════════════════════════════
    // 第二步：视觉处理
    // ═══════════════════════════════════════════════════════
    
    // 5. 设置云台姿态（使用四元数）
    solver.set_R_gimbal2world(q);
    
    // 6. 检测装甲板
    auto armors = detector.detect(img);
    
    // 7. 跟踪目标
    auto targets = tracker.track(armors, t);
    
    // 8. 瞄准决策（计算 Yaw 和 Pitch 角度）
    auto command = aimer.aim(targets, t, speed);
    
    // ═══════════════════════════════════════════════════════
    // 第三步：发送控制命令给下位机
    // ═══════════════════════════════════════════════════════
    
    // 9. 发送 Yaw 和 Pitch 角度 ⬆️⬆️⬆️
    cboard.send(command);
  }
}
```

---

## 📊 数据流时序图

```
时间 (ms)    下位机                           上位机
─────────────────────────────────────────────────────────────
0.0         发送 IMU 数据 ⬆️
            (q, speed, mode)
            
1.0                                         接收 IMU 数据 ⬇️
                                            存入队列
            
2.0         发送 IMU 数据 ⬆️
            
3.0                                         读取相机图像
                                            获取 IMU: q = imu_at(t)
                                            
4.0                                         检测装甲板
                                            
5.0                                         跟踪目标
                                            
6.0                                         计算控制命令
                                            command.yaw = 0.15 rad
                                            command.pitch = 0.08 rad
                                            
7.0                                         发送控制命令 ⬆️
                                            
8.0         接收控制命令 ⬇️
            yaw = 0.15 rad
            pitch = 0.08 rad
            
9.0         执行云台控制
            电机转动到目标角度
            
10.0        发送 IMU 数据 ⬆️
            (新的姿态)
            
11.0                                        接收 IMU 数据 ⬇️
                                            继续下一帧...
```

---

## 🎯 角度的含义

### **Yaw 角度（偏航角）**

```
俯视图（从上往下看）：

        前方
         ↑
         │
    ─────┼─────  Yaw = 0°
         │
         │
    
    ╱────┼─────  Yaw > 0（向右转）
   ╱     │
  ╱      │
  
    ─────┼────╲  Yaw < 0（向左转）
         │     ╲
         │      ╲
```

**单位：** 弧度（rad）
- 正值：向右转
- 负值：向左转
- 范围：通常 -π 到 +π

---

### **Pitch 角度（俯仰角）**

```
侧视图（从侧面看）：

         ╱
        ╱   Pitch > 0（抬头）
       ╱
    ─────────  Pitch = 0°
       ╲
        ╲   Pitch < 0（低头）
         ╲
```

**单位：** 弧度（rad）
- 正值：抬头（向上）
- 负值：低头（向下）
- 范围：通常 -π/2 到 +π/2

---

## 🔧 下位机如何使用接收到的角度

### **方法 1：位置控制（推荐）**

```c
// 下位机代码（STM32）
void process_vision_command(VisionCommand *cmd) {
  if (cmd->control) {
    // 1. 将弧度转换为角度
    float yaw_deg = cmd->yaw * 180.0f / PI;
    float pitch_deg = cmd->pitch * 180.0f / PI;
    
    // 2. 设置电机目标位置
    gimbal_set_yaw_position(yaw_deg);
    gimbal_set_pitch_position(pitch_deg);
    
    // 3. PID 控制器驱动电机到目标位置
    gimbal_pid_update();
  }
  
  if (cmd->shoot) {
    // 触发射击
    trigger_shoot();
  }
}
```

---

### **方法 2：速度控制**

```c
// 下位机代码（STM32）
void process_vision_command(VisionCommand *cmd) {
  if (cmd->control) {
    // 1. 计算角度误差
    float yaw_error = cmd->yaw - current_yaw;
    float pitch_error = cmd->pitch - current_pitch;
    
    // 2. 转换为速度命令
    float yaw_speed = yaw_error * KP_YAW;
    float pitch_speed = pitch_error * KP_PITCH;
    
    // 3. 限制速度
    yaw_speed = CLAMP(yaw_speed, -MAX_YAW_SPEED, MAX_YAW_SPEED);
    pitch_speed = CLAMP(pitch_speed, -MAX_PITCH_SPEED, MAX_PITCH_SPEED);
    
    // 4. 设置电机速度
    gimbal_set_yaw_speed(yaw_speed);
    gimbal_set_pitch_speed(pitch_speed);
  }
}
```

---

### **方法 3：增量控制**

```c
// 下位机代码（STM32）
void process_vision_command(VisionCommand *cmd) {
  static float last_yaw = 0;
  static float last_pitch = 0;
  
  if (cmd->control) {
    // 1. 计算角度增量
    float delta_yaw = cmd->yaw - last_yaw;
    float delta_pitch = cmd->pitch - last_pitch;
    
    // 2. 更新目标位置
    target_yaw += delta_yaw;
    target_pitch += delta_pitch;
    
    // 3. 执行控制
    gimbal_set_yaw_position(target_yaw);
    gimbal_set_pitch_position(target_pitch);
    
    // 4. 保存当前值
    last_yaw = cmd->yaw;
    last_pitch = cmd->pitch;
  }
}
```

---

## 📋 控制标志的作用

### **control 标志**

```cpp
command.control = true;   // 启用自瞄控制
command.control = false;  // 禁用自瞄控制（手动控制）
```

**用途：**
- `true`：下位机使用上位机发送的角度控制云台
- `false`：下位机忽略上位机角度，使用手动控制

**典型场景：**
```cpp
// 检测到目标时
if (targets.size() > 0) {
  command.control = true;   // 启用自瞄
  command.yaw = calculated_yaw;
  command.pitch = calculated_pitch;
} else {
  command.control = false;  // 无目标，手动控制
}
```

---

### **shoot 标志**

```cpp
command.shoot = true;   // 触发射击
command.shoot = false;  // 不射击
```

**用途：**
- `true`：下位机触发发射机构射击
- `false`：不射击

**典型场景：**
```cpp
// shooter.cpp
bool Shooter::shoot(const Command & command, const Aimer & aimer, 
                    const std::list<Target> & targets, 
                    const Eigen::Vector3d & ypr)
{
  // 1. 检查是否有目标
  if (targets.empty()) return false;
  
  // 2. 检查瞄准误差是否在容差范围内
  double yaw_error = abs(command.yaw - ypr[0]);
  double pitch_error = abs(command.pitch - ypr[1]);
  
  if (yaw_error < tolerance && pitch_error < tolerance) {
    return true;  // 瞄准精确，可以射击
  }
  
  return false;  // 瞄准不够精确，不射击
}
```

---

## 🔍 实际示例

### **完整的一帧处理**

```cpp
// 假设当前状态
// 下位机发送: q = (0, 0, 0, 1), speed = 15.5 m/s, mode = auto_aim
// 相机看到: 装甲板在图像中心偏右上方

// 1. 接收下位机数据
q = cboard.imu_at(t);              // q = (0, 0, 0, 1)
speed = cboard.bullet_speed;       // speed = 15.5
mode = cboard.mode;                // mode = auto_aim

// 2. 视觉处理
solver.set_R_gimbal2world(q);      // 设置云台姿态
auto armors = detector.detect(img); // 检测到 1 个装甲板
auto targets = tracker.track(armors, t); // 跟踪目标

// 3. 计算控制命令
auto command = aimer.aim(targets, t, speed);
// 结果: command.yaw = 0.15 rad (8.6°)
//       command.pitch = 0.08 rad (4.6°)
//       command.control = true
//       command.shoot = false (瞄准误差还太大)

// 4. 发送给下位机
cboard.send(command);

// 5. 下位机接收并执行
// - 云台向右转 8.6°
// - 云台向上抬 4.6°
// - 不射击（等待瞄准更精确）
```

---

## 📊 数据对比表

### **上传数据（下位机 → 上位机）**

| 数据 | 类型 | 字节数 | 频率 | 作用 |
|------|------|--------|------|------|
| 消息 ID | uint8_t | 1 | - | 0x01 |
| 四元数 x | float | 4 | 100-200 Hz | 云台姿态 |
| 四元数 y | float | 4 | 100-200 Hz | 云台姿态 |
| 四元数 z | float | 4 | 100-200 Hz | 云台姿态 |
| 四元数 w | float | 4 | 100-200 Hz | 云台姿态 |
| 弹速 | float | 4 | 10-20 Hz | 弹道补偿 |
| 工作模式 | uint8_t | 1 | 变化时 | 切换模式 |
| 射击模式 | uint8_t | 1 | 变化时 | 哨兵专用 |
| **总计** | - | **23** | - | - |

---

### **下发数据（上位机 → 下位机）**

| 数据 | 类型 | 字节数 | 频率 | 作用 |
|------|------|--------|------|------|
| 消息 ID | uint8_t | 1 | - | 0x02 |
| 控制标志 | uint8_t | 1 | 60-200 Hz | 启用/禁用自瞄 |
| 射击标志 | uint8_t | 1 | 60-200 Hz | 触发射击 |
| **Yaw 角度** | **float** | **4** | **60-200 Hz** | **云台水平转动** ⭐ |
| **Pitch 角度** | **float** | **4** | **60-200 Hz** | **云台俯仰转动** ⭐ |
| **总计** | - | **11** | - | - |

---

## ⚠️ 重要注意事项

### **1. 角度单位**

```cpp
// 上位机发送的是弧度（rad）
command.yaw = 0.15;    // 弧度

// 下位机可能需要转换为角度（deg）
float yaw_deg = cmd->yaw * 180.0f / PI;  // 8.6°
```

---

### **2. 坐标系一致性**

```
上位机和下位机必须使用相同的坐标系定义：

Yaw:   正值 = 向右转
       负值 = 向左转

Pitch: 正值 = 向上抬
       负值 = 向下低
```

---

### **3. 角度范围**

```cpp
// 确保角度在合理范围内
command.yaw = CLAMP(command.yaw, -PI, PI);
command.pitch = CLAMP(command.pitch, -PI/2, PI/2);
```

---

### **4. 通信频率**

```
上传（下位机 → 上位机）：
- IMU 数据: 100-200 Hz（高频）
- 弹速/模式: 10-20 Hz（低频）

下发（上位机 → 下位机）：
- 控制命令: 60-200 Hz（与相机帧率一致）
```

---

## ✅ 总结

### **下位机接收的自瞄数据：**

1. **Yaw 角度** ⭐⭐⭐ - 云台水平转动角度（弧度）
2. **Pitch 角度** ⭐⭐⭐ - 云台俯仰转动角度（弧度）
3. **控制标志** ⭐⭐ - 是否启用自瞄控制
4. **射击标志** ⭐⭐ - 是否触发射击

### **完整通信流程：**

```
下位机发送 → 上位机接收 → 视觉处理 → 计算角度 → 上位机发送 → 下位机接收 → 执行控制
   (IMU)        (四元数)      (检测跟踪)   (Yaw/Pitch)    (命令)       (角度)      (电机)
```

### **关键点：**

- ✅ 下位机**必须接收**上位机计算的 Yaw 和 Pitch 角度
- ✅ 角度单位是**弧度**（rad）
- ✅ 下位机根据角度控制云台电机
- ✅ 双向通信，实时闭环控制

---

## 📚 相关文档

- [自瞄数据流程](auto_aim_data_flow.md)
- [串口协议说明](serial_protocol.md)
- [STM32 集成指南](stm32_integration_guide.md)
