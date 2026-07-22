cmake_minimum_required(VERSION 3.10)
project(Bridge)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 强制 32 位编译（若未通过 -A Win32 指定，则报错）
if(NOT CMAKE_GENERATOR_PLATFORM STREQUAL "Win32")
    message(FATAL_ERROR "Bridge must be compiled as 32-bit. Use -A Win32 when invoking cmake.")
endif()

# 添加可执行文件（控制台程序，便于调试）
add_executable(Bridge
    # ---------- Bridge 服务端主程序 ----------
    Bridge.cpp
    BMPlatform.cpp       # 加载 Channel9.dll 的核心逻辑
)

# 链接必要的系统库
target_link_libraries(Bridge
    ws2_32          # Winsock2
)

target_compile_definitions(Bridge PRIVATE
    WIN32_LEAN_AND_MEAN
    _WINSOCKAPI_
    _CRT_SECURE_NO_WARNINGS
)