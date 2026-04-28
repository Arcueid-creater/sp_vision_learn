# 自瞄系统数据流程详解

## 🎯 核心问题：下位机传输的四元数在自瞄中的作用

**答案：是的！下位机传输的四元数是自瞄系统的核心数据之一。**

---

## 📊 完整数据流程

```
┌─────────────────────────────────────────────────────────────┐
│                      下位机 (STM32)                          │
│  ┌────────────────────────────────────────────────────┐    │
│  │  IMU 传感器 (陀螺仪 + 加速度计)                     │    │
│  │  - 测量云台姿态                                      │    │
│  │  - 输出四元数 (x, y, z, w)                         │    │
│  └────────────────┬───────────────────────────────────┘    │
│                   │                                          │
│  ┌────────────────▼───────────────────────────────────┐    │
│  │  串口发送                                            │    │
│  │  - 消息 ID: 0x01                                    │    │
│  │  - 四元数: (x, y, z, w)                            │    │
│  │  - 弹速: bullet_speed                               │    │
│  │  - 模式: mode                                       │    │
│  └────────────────┬───────────────────────────────────┘    │
└───────────────────┼──────────────────────────────────────────┘
                    │ 串口通信
                    │ (CRC16 校验)
┌───────────────────▼──────────────────────────────────────────┐
│                      上位机 (视觉系统)                        │
│  ┌────────────────────────────────────────────────────┐    │
│  │  VirtualSerial (独立线程)                           │    │
│  │  - 接收串口数据                                      │    │
│  │  - 解析协议帧                                        │    │
│  │  - CRC16 校验                                       │    │
│  │  - 调用回调函数                                      │    │
│  └────────────────┬───────────────────────────────────┘    │
│                   │                                          │
│  ┌────────────────▼───────────────────────────────────┐    │
│  │  CBoardSerial::callback                             │    │
│  │  - 解析四元数                                        │    │
│  │  - 存入时间戳队列 (ThreadSafeQueue)                 │    │
│  │  - 更新弹速和模式                                    │    │
│  └────────────────┬───────────────────────────────────┘    │
│                   │                                          │
│  ┌────────────────▼───────────────────────────────────┐    │
│  │  主循环 (main)                                      │    │
│  │  1. 读取相机图像                                     │    │
│  │  2. q = cboard.imu_at(t - 1ms)  ← 获取四元数       │    │
│  │  3. solver.set_R_gimbal2world(q) ← 设置云台姿态    │    │
│  │  4. 检测装甲板                                       │    │
│  │  5. 跟踪目标                                         │    │
│  │  6. 瞄准决策                                         │    │
│  │  7. 发送控制命令                                     │    │
│  └────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
```

---

## 🔍 详细代码分析

### **步骤 1：下位机发送四元数**

```c
// 下位机代码（STM32）
void send_imu_data(void) {
  // 从 IMU 读取姿态
  float quat[4];  // x, y, z, w
  imu_get_quaternion(quat);
  
  // 构建数据包
  uint8_t data[23];
  data[0] = 0x01;  // 消息 ID
  
  // 四元数（16 字节）
  memcpy(&data[1], &quat[0], 4);   // x
  memcpy(&data[5], &quat[1], 4);   // y
  memcpy(&data[9], &quat[2], 4);   // z
  memcpy(&data[13], &quat[3], 4);  // w
  
  // 弹速和模式（6 字节）
  memcpy(&data[17], &bullet_speed, 4);
  data[21] = mode;
  data[22] = shoot_mode;
  
  // 发送（自动添加帧头和 CRC16）
  serial_send(data, 23);
}
```

---

### **步骤 2：上位机接收四元数**

```cpp
// io/cboard_serial.hpp
void CBoardSerial::callback(const uint8_t * data, uint16_t length)
{
  uint8_t msg_id = data[0];
  
  if (msg_id == MSG_ID_STM32_STATUS) {  // 0x01
    size_t offset = 1;
    
    // 1. 解析四元数（16 字节）
    float x, y, z, w;
    std::memcpy(&x, data + offset, sizeof(float)); offset += 4;
    std::memcpy(&y, data + offset, sizeof(float)); offset += 4;
    std::memcpy(&z, data + offset, sizeof(float)); offset += 4;
    std::memcpy(&w, data + offset, sizeof(float)); offset += 4;
    
    // 2. 存入队列（带时间戳）
    IMUData imu_data;
    imu_data.q = Eigen::Quaterniond(w, x, y, z);  // ⭐ 关键数据
    imu_data.timestamp = std::chrono::steady_clock::now();
    queue_.push(imu_data);
    
    // 3. 解析弹速和模式
    std::memcpy(&bullet_speed, data + offset, sizeof(float));
    mode = static_cast<Mode>(data[offset + 4]);
    shoot_mode = static_cast<ShootMode>(data[offset + 5]);
  }
}
```

---

### **步骤 3：主程序使用四元数**

```cpp
// src/standard.cpp
int main() {
  io::CBoardSerial cboard(config_path);
  io::Camera camera(config_path);
  
  auto_aim::YOLO detector(config_path);
  auto_aim::Solver solver(config_path);
  auto_aim::Tracker tracker(config_path, solver);
  auto_aim::Aimer aimer(config_path);
  
  cv::Mat img;
  Eigen::Quaterniond q;
  std::chrono::steady_clock::time_point t;
  
  while (!exiter.exit()) {
    // 1. 读取相机图像（带时间戳）
    camera.read(img, t);
    
    // 2. 获取对应时间戳的四元数（带插值）⭐⭐⭐
    q = cboard.imu_at(t - 1ms);
    
    // 3. 设置云台姿态到世界坐标系的旋转矩阵 ⭐⭐⭐
    solver.set_R_gimbal2world(q);
    
    // 4. 检测装甲板（图像坐标系）
    auto armors = detector.detect(img);
    
    // 5. 跟踪目标（使用云台姿态进行坐标转换）
    auto targets = tracker.track(armors, t);
    
    // 6. 瞄准决策（计算控制命令）
    auto command = aimer.aim(targets, t, cboard.bullet_speed);
    
    // 7. 发送控制命令
    cboard.send(command);
  }
}
```

---

## 🎯 四元数的关键作用

### **1. 坐标系转换**

自瞄系统涉及多个坐标系：

```
相机坐标系 → 云台坐标系 → 世界坐标系
   (固定)        (旋转)        (固定)
```

**四元数的作用：** 描述云台坐标系相对于世界坐标系的旋转

```cpp
// solver.cpp
void Solver::set_R_gimbal2world(const Eigen::Quaterniond & q)
{
  // 四元数转旋转矩阵
  R_gimbal2world_ = q.toRotationMatrix();
  
  // 计算完整的变换链
  R_camera2world_ = R_gimbal2world_ * R_camera2gimbal_;
}
```

---

### **2. 目标位置预测**

```cpp
// tracker.cpp
Eigen::Vector3d Tracker::predict_position(const Target & target, double dt)
{
  // 使用云台姿态（四元数）进行坐标转换
  Eigen::Vector3d pos_world = R_gimbal2world_ * pos_gimbal;
  
  // 预测未来位置
  Eigen::Vector3d predicted_pos = pos_world + velocity * dt;
  
  return predicted_pos;
}
```

---

### **3. 弹道补偿**

```cpp
// aimer.cpp
Command Aimer::aim(const Target & target, double bullet_speed)
{
  // 1. 获取目标在世界坐标系中的位置（使用四元数转换）
  Eigen::Vector3d target_pos_world = solver.transform_to_world(target.pos);
  
  // 2. 计算弹道补偿
  Eigen::Vector3d aim_pos = calculate_ballistic_compensation(
    target_pos_world, bullet_speed);
  
  // 3. 转换回云台坐标系（使用四元数的逆）
  Eigen::Vector3d aim_pos_gimbal = R_gimbal2world_.inverse() * aim_pos;
  
  // 4. 计算控制角度
  Command command;
  command.yaw = atan2(aim_pos_gimbal.y(), aim_pos_gimbal.x());
  command.pitch = atan2(aim_pos_gimbal.z(), aim_pos_gimbal.x());
  
  return command;
}
```

---

## 📊 数据同步机制

### **为什么需要时间戳？**

```cpp
// 相机图像和 IMU 数据的时间戳不完全同步
camera.read(img, t);           // t = 100.000 ms
q = cboard.imu_at(t - 1ms);    // 获取 t = 99.000 ms 的姿态
```

**原因：**
1. 相机曝光需要时间（2-5 ms）
2. 图像传输需要时间
3. IMU 数据更新频率更高（200-1000 Hz）

---

### **时间戳插值**

```cpp
// io/cboard_serial.hpp
Eigen::Quaterniond CBoardSerial::imu_at(
  std::chrono::steady_clock::time_point timestamp)
{
  // 从队列中获取最新数据
  while (queue_.size() > 1) {
    data_behind_ = data_ahead_;
    queue_.pop(data_ahead_);
  }
  
  // 如果时间戳在最新数据之后，返回最新数据
  if (timestamp >= data_ahead_.timestamp) {
    return data_ahead_.q;
  }
  
  // 如果时间戳在最旧数据之前，返回最旧数据
  if (timestamp <= data_behind_.timestamp) {
    return data_behind_.q;
  }
  
  // 线性插值（球面插值 slerp）⭐⭐⭐
  auto dt = std::chrono::duration<double>(
    data_ahead_.timestamp - data_behind_.timestamp).count();
  auto t = std::chrono::duration<double>(
    timestamp - data_behind_.timestamp).count();
  double ratio = t / dt;
  
  // 四元数球面插值
  return data_behind_.q.slerp(ratio, data_ahead_.q);
}
```

**球面插值（slerp）的优势：**
- ✅ 保持四元数的单位长度
- ✅ 旋转路径最短
- ✅ 角速度恒定

---

## 🔄 完整数据流示例

### **时间线：**

```
时间 (ms)    事件
─────────────────────────────────────────────────────
0.0         下位机发送 IMU 数据 (q1)
1.0         下位机发送 IMU 数据 (q2)
2.0         下位机发送 IMU 数据 (q3)
2.5         相机开始曝光
4.5         相机曝光结束，图像时间戳 t=4.5ms
5.0         上位机读取图像
5.1         上位机调用 imu_at(t=3.5ms)
            → 在 q3(2.0ms) 和 q4(3.0ms) 之间插值
            → 返回插值后的四元数 q_interp
5.2         设置云台姿态: solver.set_R_gimbal2world(q_interp)
5.3         检测装甲板
5.5         跟踪目标（使用 q_interp 进行坐标转换）
5.8         瞄准决策（使用 q_interp 计算控制命令）
6.0         发送控制命令
```

---

## 📋 下位机需要传输的所有数据

### **必需数据（自瞄核心）**

| 数据 | 类型 | 字节数 | 作用 |
|------|------|--------|------|
| **四元数 x** | float | 4 | 云台姿态（旋转） ⭐⭐⭐ |
| **四元数 y** | float | 4 | 云台姿态（旋转） ⭐⭐⭐ |
| **四元数 z** | float | 4 | 云台姿态（旋转） ⭐⭐⭐ |
| **四元数 w** | float | 4 | 云台姿态（旋转） ⭐⭐⭐ |
| **弹速** | float | 4 | 弹道补偿 ⭐⭐ |

### **辅助数据**

| 数据 | 类型 | 字节数 | 作用 |
|------|------|--------|------|
| 工作模式 | uint8_t | 1 | 切换自瞄/打符/待机 |
| 射击模式 | uint8_t | 1 | 哨兵专用（左/右/双射） |
| FT 角度 | float | 4 | 无人机专用 |

---

## 🎯 为什么使用四元数而不是欧拉角？

### **四元数的优势**

| 特性 | 四元数 | 欧拉角 |
|------|--------|--------|
| 万向锁 | ✅ 无 | ❌ 有 |
| 插值 | ✅ 平滑（slerp） | ❌ 不平滑 |
| 计算效率 | ✅ 高 | ⚠️ 中等 |
| 存储空间 | 4 个 float | 3 个 float |
| 数值稳定性 | ✅ 好 | ⚠️ 一般 |

**万向锁示例：**
```
欧拉角 (roll, pitch, yaw)
当 pitch = 90° 时，roll 和 yaw 的旋转效果相同
→ 失去一个自由度
→ 无法准确描述姿态

四元数不会出现这个问题！
```

---

## 🧪 测试和验证

### **1. 检查四元数是否正常**

```cpp
// 四元数必须是单位四元数
double norm = sqrt(x*x + y*y + z*z + w*w);
if (abs(norm - 1.0) > 0.01) {
  tools::logger()->warn("Invalid quaternion: norm = {}", norm);
}
```

### **2. 检查数据更新频率**

```cpp
// IMU 数据应该以 100-200 Hz 更新
static auto last_time = std::chrono::steady_clock::now();
auto now = std::chrono::steady_clock::now();
auto dt = std::chrono::duration<double>(now - last_time).count();

if (dt > 0.02) {  // > 20ms = < 50Hz
  tools::logger()->warn("IMU update rate too low: {} Hz", 1.0/dt);
}
last_time = now;
```

### **3. 可视化云台姿态**

```cpp
// 将四元数转换为欧拉角（便于理解）
Eigen::Vector3d ypr = tools::eulers(q.toRotationMatrix(), 2, 1, 0);
tools::logger()->info("Gimbal pose: yaw={:.2f}° pitch={:.2f}° roll={:.2f}°",
  ypr[0] * 180 / M_PI,
  ypr[1] * 180 / M_PI,
  ypr[2] * 180 / M_PI);
```

---

## ✅ 总结

### **下位机传输的四元数是自瞄的核心数据！**

**作用：**
1. ⭐⭐⭐ **坐标系转换** - 将相机坐标系的目标转换到世界坐标系
2. ⭐⭐⭐ **姿态估计** - 知道云台当前朝向哪里
3. ⭐⭐ **目标预测** - 预测移动目标的未来位置
4. ⭐⭐ **弹道补偿** - 计算准确的瞄准角度

**数据流：**
```
IMU 传感器 → 下位机 → 串口 → 上位机 → 时间戳插值 → 坐标转换 → 自瞄算法
```

**关键点：**
- ✅ 四元数避免万向锁
- ✅ 时间戳同步确保精度
- ✅ 球面插值保证平滑
- ✅ 高频更新（100-200 Hz）

**没有四元数，自瞄系统无法工作！** 它是连接物理世界和视觉算法的桥梁。

---

## 📚 相关文档

- [串口协议说明](serial_protocol.md)
- [STM32 集成指南](stm32_integration_guide.md)
- [通信架构对比](communication_architecture.md)
