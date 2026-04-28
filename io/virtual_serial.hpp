#ifndef IO__VIRTUAL_SERIAL_HPP
#define IO__VIRTUAL_SERIAL_HPP

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <thread>
#include <vector>

#include "tools/logger.hpp"

using namespace std::chrono_literals;

namespace io
{

// CRC16-CCITT 查找表
static const uint16_t CRC16_TABLE[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};

// 协议常量
constexpr uint8_t FRAME_HEADER_1 = 0xFF;
constexpr uint8_t FRAME_HEADER_2 = 0xAA;
constexpr size_t MAX_PAYLOAD_SIZE = 512;
constexpr size_t RX_BUFFER_SIZE = 1024;

// 接收状态机
enum class RxState
{
  WAIT_HEADER1,
  WAIT_HEADER2,
  RECEIVING_LENGTH,
  RECEIVING_DATA
};

/**
 * @brief 虚拟串口通信类（使用 CRC16 协议）
 * 
 * 协议格式：[0xFF][0xAA][长度(2)][数据(N)][CRC16(2)]
 */
class VirtualSerial
{
public:
  /**
   * @brief 构造函数
   * @param device 串口设备路径，如 "/dev/ttyACM0"
   * @param baudrate 波特率，如 115200
   * @param rx_handler 接收数据回调函数
   */
  VirtualSerial(
    const std::string & device, int baudrate,
    std::function<void(const uint8_t * data, uint16_t length)> rx_handler)
  : device_(device),
    baudrate_(baudrate),
    serial_fd_(-1),
    rx_handler_(rx_handler),
    quit_(false),
    ok_(false),
    rx_state_(RxState::WAIT_HEADER1),
    rx_index_(0),
    expected_length_(0)
  {
    try_open();

    // 守护线程：监控连接状态，断线重连
    daemon_thread_ = std::thread{[this] {
      while (!quit_) {
        std::this_thread::sleep_for(100ms);

        if (ok_) continue;

        if (read_thread_.joinable()) read_thread_.join();

        close();
        try_open();
      }
    }};
  }

  ~VirtualSerial()
  {
    quit_ = true;
    if (daemon_thread_.joinable()) daemon_thread_.join();
    if (read_thread_.joinable()) read_thread_.join();
    close();
    tools::logger()->info("VirtualSerial destructed.");
  }

  /**
   * @brief 发送数据（自动添加协议头和 CRC16）
   * @param data 要发送的数据
   * @param length 数据长度
   */
  void write(const uint8_t * data, uint16_t length)
  {
    if (serial_fd_ < 0 || !ok_) {
      throw std::runtime_error("Serial port not opened!");
    }

    if (length > MAX_PAYLOAD_SIZE) {
      length = MAX_PAYLOAD_SIZE;
    }

    // 构建数据帧
    std::vector<uint8_t> frame;
    frame.reserve(length + 6);  // 帧头(2) + 长度(2) + 数据 + CRC16(2)

    // 1. 帧头
    frame.push_back(FRAME_HEADER_1);
    frame.push_back(FRAME_HEADER_2);

    // 2. 数据长度（小端序）
    frame.push_back(length & 0xFF);
    frame.push_back((length >> 8) & 0xFF);

    // 3. 数据
    frame.insert(frame.end(), data, data + length);

    // 4. 计算 CRC16
    uint16_t crc = calculate_crc16(frame.data(), frame.size());
    frame.push_back(crc & 0xFF);
    frame.push_back((crc >> 8) & 0xFF);

    // 5. 发送
    ssize_t bytes_written = ::write(serial_fd_, frame.data(), frame.size());
    if (bytes_written != static_cast<ssize_t>(frame.size())) {
      throw std::runtime_error("Failed to write complete frame!");
    }
  }

private:
  std::string device_;
  int baudrate_;
  int serial_fd_;
  bool quit_;
  bool ok_;
  std::thread read_thread_;
  std::thread daemon_thread_;
  std::function<void(const uint8_t * data, uint16_t length)> rx_handler_;

  // 接收状态机相关
  RxState rx_state_;
  uint8_t rx_buffer_[RX_BUFFER_SIZE];
  uint16_t rx_index_;
  uint16_t expected_length_;

  /**
   * @brief 计算 CRC16-CCITT
   */
  static uint16_t calculate_crc16(const uint8_t * data, size_t len)
  {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
      uint8_t index = (crc >> 8) ^ data[i];
      crc = (crc << 8) ^ CRC16_TABLE[index];
    }
    return crc;
  }

  /**
   * @brief 打开串口
   */
  void open()
  {
    serial_fd_ = ::open(device_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (serial_fd_ < 0) {
      throw std::runtime_error("Failed to open serial port: " + device_);
    }

    // 配置串口参数
    struct termios tty;
    if (tcgetattr(serial_fd_, &tty) != 0) {
      ::close(serial_fd_);
      throw std::runtime_error("Failed to get serial port attributes!");
    }

    // 设置波特率
    speed_t speed = B115200;  // 默认
    switch (baudrate_) {
      case 9600:
        speed = B9600;
        break;
      case 19200:
        speed = B19200;
        break;
      case 38400:
        speed = B38400;
        break;
      case 57600:
        speed = B57600;
        break;
      case 115200:
        speed = B115200;
        break;
      case 230400:
        speed = B230400;
        break;
      case 460800:
        speed = B460800;
        break;
      case 921600:
        speed = B921600;
        break;
      default:
        tools::logger()->warn("Unsupported baudrate {}, using 115200", baudrate_);
        speed = B115200;
    }

    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    // 8N1 模式
    tty.c_cflag &= ~PARENB;         // 无校验位
    tty.c_cflag &= ~CSTOPB;         // 1 个停止位
    tty.c_cflag &= ~CSIZE;          // 清除数据位设置
    tty.c_cflag |= CS8;             // 8 数据位
    tty.c_cflag &= ~CRTSCTS;        // 禁用硬件流控制
    tty.c_cflag |= CREAD | CLOCAL;  // 启用接收，忽略调制解调器控制线

    // 原始模式
    tty.c_lflag &= ~ICANON;  // 禁用规范模式
    tty.c_lflag &= ~ECHO;    // 禁用回显
    tty.c_lflag &= ~ECHOE;
    tty.c_lflag &= ~ECHONL;
    tty.c_lflag &= ~ISIG;  // 禁用信号字符解释

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);                         // 禁用软件流控制
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);  // 禁用特殊处理

    tty.c_oflag &= ~OPOST;  // 禁用输出处理
    tty.c_oflag &= ~ONLCR;

    // 超时设置
    tty.c_cc[VTIME] = 1;  // 0.1 秒超时
    tty.c_cc[VMIN] = 0;   // 非阻塞读取

    // 应用设置
    if (tcsetattr(serial_fd_, TCSANOW, &tty) != 0) {
      ::close(serial_fd_);
      throw std::runtime_error("Failed to set serial port attributes!");
    }

    // 清空缓冲区
    tcflush(serial_fd_, TCIOFLUSH);

    // 重置接收状态机
    rx_state_ = RxState::WAIT_HEADER1;
    rx_index_ = 0;
    expected_length_ = 0;

    // 启动接收线程
    read_thread_ = std::thread([this]() {
      ok_ = true;
      while (!quit_) {
        try {
          read();
        } catch (const std::exception & e) {
          tools::logger()->warn("VirtualSerial::read() failed: {}", e.what());
          ok_ = false;
          break;
        }
        std::this_thread::sleep_for(1ms);
      }
    });

    tools::logger()->info("VirtualSerial opened: {}", device_);
  }

  /**
   * @brief 尝试打开串口
   */
  void try_open()
  {
    try {
      open();
    } catch (const std::exception & e) {
      tools::logger()->warn("VirtualSerial::open() failed: {}", e.what());
    }
  }

  /**
   * @brief 读取并解析数据
   */
  void read()
  {
    uint8_t byte;
    ssize_t n = ::read(serial_fd_, &byte, 1);

    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return;  // 非阻塞模式，无数据可读
      }
      throw std::runtime_error("Serial read error!");
    }

    if (n == 0) {
      return;  // 无数据
    }

    // 状态机解析
    process_byte(byte);
  }

  /**
   * @brief 处理接收到的单个字节（状态机）
   */
  void process_byte(uint8_t byte)
  {
    switch (rx_state_) {
      case RxState::WAIT_HEADER1:
        if (byte == FRAME_HEADER_1) {
          rx_index_ = 0;
          rx_buffer_[rx_index_++] = byte;
          rx_state_ = RxState::WAIT_HEADER2;
        }
        break;

      case RxState::WAIT_HEADER2:
        if (byte == FRAME_HEADER_2) {
          rx_buffer_[rx_index_++] = byte;
          rx_state_ = RxState::RECEIVING_LENGTH;
        } else {
          rx_state_ = RxState::WAIT_HEADER1;
        }
        break;

      case RxState::RECEIVING_LENGTH:
        rx_buffer_[rx_index_++] = byte;
        if (rx_index_ >= 4) {  // 帧头(2) + 长度(2)
          expected_length_ = rx_buffer_[2] | (rx_buffer_[3] << 8);
          if (expected_length_ > MAX_PAYLOAD_SIZE) {
            tools::logger()->warn("Invalid payload length: {}", expected_length_);
            rx_state_ = RxState::WAIT_HEADER1;
          } else {
            rx_state_ = RxState::RECEIVING_DATA;
          }
        }
        break;

      case RxState::RECEIVING_DATA:
        rx_buffer_[rx_index_++] = byte;

        // 检查是否接收完整：帧头(2) + 长度(2) + 数据(N) + CRC16(2)
        if (rx_index_ >= 4 + expected_length_ + 2) {
          // 验证 CRC16
          size_t crc_offset = 4 + expected_length_;
          uint16_t received_crc = rx_buffer_[crc_offset] | (rx_buffer_[crc_offset + 1] << 8);
          uint16_t calculated_crc = calculate_crc16(rx_buffer_, crc_offset);

          if (received_crc == calculated_crc) {
            // CRC 校验通过，调用回调函数
            rx_handler_(rx_buffer_ + 4, expected_length_);
          } else {
            tools::logger()->warn(
              "CRC16 mismatch! Received: 0x{:04X}, Calculated: 0x{:04X}", received_crc,
              calculated_crc);
          }

          // 重置状态机
          rx_state_ = RxState::WAIT_HEADER1;
          rx_index_ = 0;
        }

        // 防止缓冲区溢出
        if (rx_index_ >= RX_BUFFER_SIZE) {
          tools::logger()->warn("RX buffer overflow!");
          rx_state_ = RxState::WAIT_HEADER1;
          rx_index_ = 0;
        }
        break;
    }
  }

  /**
   * @brief 关闭串口
   */
  void close()
  {
    if (serial_fd_ == -1) return;
    ::close(serial_fd_);
    serial_fd_ = -1;
  }
};

}  // namespace io

#endif  // IO__VIRTUAL_SERIAL_HPP
