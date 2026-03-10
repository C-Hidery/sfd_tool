#pragma once

#include <string>
#include <utility>

namespace sfd {

// 通用错误码：后续可以按需要扩展
enum class ErrorCode {
    Ok = 0,

    // 通用错误
    Unknown,
    Cancelled,
    InvalidArgument,
    IoError,
    NotFound,
    OutOfRange,
    InternalError,

    // 设备 / 协议相关
    DeviceNotConnected,
    ProtocolError,
    Timeout,

    // 数据 / 解析相关
    ParseError,

    // 能力 / 配置相关
    Unsupported,
};

// 带返回值的结果类型
// 用法：Result<int> r; if (!r) { ... } else { use r.value; }
template <typename T>
struct Result {
    ErrorCode code = ErrorCode::Ok;
    T value{};
    std::string message;  // 可选的人类可读错误信息

    static Result<T> ok(T v) {
        Result<T> r;
        r.code = ErrorCode::Ok;
        r.value = std::move(v);
        return r;
    }

    static Result<T> error(ErrorCode c, std::string msg = {}) {
        Result<T> r;
        r.code = c;
        r.message = std::move(msg);
        return r;
    }

    bool is_ok() const { return code == ErrorCode::Ok; }
    explicit operator bool() const { return is_ok(); }
};

// 无返回值的结果类型特化
// 用法：Result<void> r; if (!r) { ... }
template <>
struct Result<void> {
    ErrorCode code = ErrorCode::Ok;
    std::string message;

    static Result<void> ok() {
        Result<void> r;
        r.code = ErrorCode::Ok;
        return r;
    }

    static Result<void> error(ErrorCode c, std::string msg = {}) {
        Result<void> r;
        r.code = c;
        r.message = std::move(msg);
        return r;
    }

    bool is_ok() const { return code == ErrorCode::Ok; }
    explicit operator bool() const { return is_ok(); }
};

} // namespace sfd
