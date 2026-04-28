#ifndef IO__CBOARD_WRAPPER_HPP
#define IO__CBOARD_WRAPPER_HPP

// 通过宏定义选择通信方式
// 在 CMakeLists.txt 中定义: add_definitions(-DUSE_SERIAL_COMM)

#ifdef USE_SERIAL_COMM
  #include "io/cboard_serial.hpp"
  namespace io {
    using CBoard = CBoardSerial;  // 类型别名
  }
#else
  #include "io/cboard.hpp"
  // 使用原始的 io::CBoard
#endif

#endif  // IO__CBOARD_WRAPPER_HPP
