cmake_minimum_required(VERSION 3.16)
project(Proxy32 LANGUAGES CXX)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置输出目录（使用环境变量或默认）
if(DEFINED ENV{PROXY32_OUTPUT_DIR})
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "$ENV{PROXY32_OUTPUT_DIR}")
else()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

# 添加可执行文件
add_executable(Proxy32 
    ${CMAKE_CURRENT_SOURCE_DIR}/Proxy32.cpp
)

# 编译选项
if(MSVC)
    # 强制 32 位编译
    target_compile_options(Proxy32 PRIVATE 
        /arch:IA32
        /MT          # 静态链接运行时
        /O2          # 优化
    )
    
    # 链接选项
    set_target_properties(Proxy32 PROPERTIES
        LINK_FLAGS "/MACHINE:X86 /SUBSYSTEM:CONSOLE"
    )
endif()

# 预处理器定义
target_compile_definitions(Proxy32 PRIVATE
    _CRT_SECURE_NO_WARNINGS
    _WIN32
    _CONSOLE
    PROXY32_EXE
)

# 包含目录
target_include_directories(Proxy32 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# 链接库
target_link_libraries(Proxy32 PRIVATE
    dbghelp
    kernel32
    user32
    advapi32
)

# 设置输出名称
set_target_properties(Proxy32 PROPERTIES
    OUTPUT_NAME "Proxy32"
)

message(STATUS "Proxy32 configured for Win32 (x86)")
message(STATUS "Output directory: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")