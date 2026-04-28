#ifndef IO__CBOARD_SERIAL_HPP
#define IO__CBOARD_SERIAL_HPP

#include <Eigen/Geometry>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "io/cboard.hpp"
#include "io/command.hpp"
#include "io/virtual_serial.hpp"
#include "tools/logger.hpp"
#include "tools/thread_safe_queue.hpp"
#include "tools/yaml.hpp"

namespace io
{

// 消息 ID 定义（与下位机保持一致）
enum MessageID : uint8_t
{
  MSG_ID_STM32_STATUS = 0x01,      // STM32状态数据（批量上传）
  MSG_ID_VISION_COMMAND = 0x02     // 视觉控制命令（批量下发）
};

/**
 * @brief 基于虚拟串口的 CBoard 类
 * 
 * 使用 CRC16 协议与下位机通信
 */
class CBoardSerial
{
public:
  double bullet_speed;
  Mode mode;
  ShootMode shoot_mode;
  double ft_angle;  // 无人机专有

  CBoardSerial(const std::string & config_path)
  : bullet_speed(0),
    mode(Mode::idle),
    shoot_mode(ShootMode::both_shoot),
    ft_angle(0),
    queue_(5000)
  {
    auto config = read_yaml(config_path);
    std::string device = config.first;
    int baudrate = config.second;

    // 创建虚拟串口对象
    serial_ = std::make_unique<VirtualSerial>(
      device, baudrate, std::bind(&CBoardSerial::callback, this, std::placeholders::_1,
                                  std::placeholders::_2));

    tools::logger()->info("CBoardSerial initialized on {}", device);
  }

  /**
   * @brief 获取指定时间戳的 IMU 数据（四元数）
   */
  Eigen::Quaterniond imu_at(std::chrono::steady_clock::time_point timestamp)
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

    // 线性插值
    auto dt = std::chrono::duration<double>(data_ahead_.timestamp - data_behind_.timestamp).count();
    auto t = std::chrono::duration<double>(timestamp - data_behind_.timestamp).count();
    double ratio = t / dt;

    return data_behind_.q.slerp(ratio, data_ahead_.q);
  }

  /**
   * @brief 发送视觉控制命令（批量下发）
   */
  void send(Command command) const
  {
    // 构建数据包
    uint8_t data[32];
    size_t offset = 0;

    // 消息 ID
    data[offset++] = MSG_ID_VISION_COMMAND;

    // 控制标志
    data[offset++] = command.control ? 1 : 0;
    data[offset++] = command.shoot ? 1 : 0;

    // Yaw 角度（float，4 字节）
    float yaw = static_cast<float>(command.yaw);
    std::memcpy(data + offset, &yaw, sizeof(float));
    offset += sizeof(float);

    // Pitch 角度（float，4 字节）
    float pitch = static_cast<float>(command.pitch);
    std::memcpy(data + offset, &pitch, sizeof(float));
    offset += sizeof(float);

    // 发送
    try {
      serial_->write(data, offset);
    } catch (const std::exception & e) {
      tools::logger()->warn("Failed to send vision command: {}", e.what());
    }
  }

private:
  struct IMUData
  {
    Eigen::Quaterniond q;
    std::chrono::steady_clock::time_point timestamp;
  };

  tools::ThreadSafeQueue<IMUData> queue_;
  std::unique_ptr<VirtualSerial> serial_;
  IMUData data_ahead_;
  IMUData data_behind_;

  /**
   * @brief 接收数据回调函数
   */
  void callback(const uint8_t * data, uint16_t length)
  {
    if (length < 1) return;

    auto timestamp = std::chrono::steady_clock::now();
    size_t offset = 0;

    // 解析消息 ID
    uint8_t msg_id = data[offset++];

    switch (msg_id) {
      case MSG_ID_STM32_STATUS: {
        // STM32状态数据：IMU(16) + 弹速模式(6) = 23字节
        if (length >= 1 + 16 + 6) {
          // 1. 解析IMU数据（四元数）：16字节
          float x, y, z, w;
          std::memcpy(&x, data + offset, sizeof(float));
          offset += sizeof(float);
          std::memcpy(&y, data + offset, sizeof(float));
          offset += sizeof(float);
          std::memcpy(&z, data + offset, sizeof(float));
          offset += sizeof(float);
          std::memcpy(&w, data + offset, sizeof(float));
          offset += sizeof(float);

          IMUData imu_data;
          imu_data.q = Eigen::Quaterniond(w, x, y, z);
          imu_data.timestamp = timestamp;
          queue_.push(imu_data);

          // 2. 解析弹速和模式：6字节
          float speed;
          std::memcpy(&speed, data + offset, sizeof(float));
          offset += sizeof(float);

          uint8_t mode_val = data[offset++];
          uint8_t shoot_mode_val = data[offset++];

          bullet_speed = speed;
          mode = static_cast<Mode>(mode_val);
          shoot_mode = static_cast<ShootMode>(shoot_mode_val);
        }
        break;
      }

      case MSG_ID_VISION_COMMAND: {
        // 这是下发命令，上位机不应该接收到
        tools::logger()->warn("Received vision command (should not happen)");
        break;
      }

      default:
        tools::logger()->warn("Unknown message ID: 0x{:02X}", msg_id);
        break;
    }
  }

  /**
   * @brief 读取 YAML 配置
   * @return pair<设备路径, 波特率>
   */
  std::pair<std::string, int> read_yaml(const std::string & config_path)
  {
    auto yaml = tools::load(config_path);

    std::string device = "/dev/ttyACM0";  // 默认值
    int baudrate = 115200;                // 默认值

    if (yaml["serial_device"]) {
      device = yaml["serial_device"].as<std::string>();
    } else {
      tools::logger()->warn("Missing 'serial_device' in YAML, using default: {}", device);
    }

    if (yaml["serial_baudrate"]) {
      baudrate = yaml["serial_baudrate"].as<int>();
    } else {
      tools::logger()->warn("Missing 'serial_baudrate' in YAML, using default: {}", baudrate);
    }

    return {device, baudrate};
  }
};

}  // namespace io

#endif  // IO__CBOARD_SERIAL_HPP
