#pragma once
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

// 日志等级（与 common.h 中 msg_type 的 I/W/E/OP/DE 一一对应）
enum class LogLevel {
    Info,
    Warning,
    Error,
    Op,
    Debug,
};

// 兼容旧接口：致命错误与基础日志函数
void ERR_EXIT(const char* format, ...);
void DEG_LOG(int type, const char* format, ...);

// 新增：统一的类型化日志入口（内部最终仍复用 DEG_LOG）
int logLevelToType(LogLevel level);
void logMessage(LogLevel level,
                const char* module,   // 可为 nullptr
                const char* format,
                ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

// 辅助打印函数（原有接口保持不变）
void print_mem(FILE *f, const uint8_t *buf, size_t len);
void print_string(FILE *f, const void *src, size_t n);
int print_to_string(char* dest, size_t dest_size, const void* src, size_t n, int mode);

// ================= 便捷宏：统一调用风格 =================
// 注意：宏本身不带分号，调用时写成 LOG_INFO("..."); 形式

// 无模块名版本
#define LOG_INFO(fmt, ...)   logMessage(LogLevel::Info,   nullptr, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)   logMessage(LogLevel::Warning, nullptr, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...)  logMessage(LogLevel::Error,  nullptr, fmt, ##__VA_ARGS__)
#define LOG_OP(fmt, ...)     logMessage(LogLevel::Op,     nullptr, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...)  logMessage(LogLevel::Debug,  nullptr, fmt, ##__VA_ARGS__)

// 带模块名版本
#define LOGM_INFO(module, fmt, ...)   logMessage(LogLevel::Info,  module, fmt, ##__VA_ARGS__)
#define LOGM_WARN(module, fmt, ...)   logMessage(LogLevel::Warning, module, fmt, ##__VA_ARGS__)
#define LOGM_ERROR(module, fmt, ...)  logMessage(LogLevel::Error, module, fmt, ##__VA_ARGS__)
#define LOGM_OP(module, fmt, ...)     logMessage(LogLevel::Op,    module, fmt, ##__VA_ARGS__)
#define LOGM_DEBUG(module, fmt, ...)  logMessage(LogLevel::Debug, module, fmt, ##__VA_ARGS__)

// 每个 .cpp 文件可声明自己的模块名，供 LOGM_* 使用
#define DECLARE_LOG_MODULE(name) \
    static constexpr const char* kLogModule = name;
