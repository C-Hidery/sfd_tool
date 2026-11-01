/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#pragma once
#include "file_buf.h"

#include <stdio.h>
#include <memory>
#include <string>
#include <cstdint>
#include <iostream>
#include <filesystem>
#include <ios>
#include <type_traits>
#include <cstdarg>

#ifndef __cplusplus
#error "This header requires C++. Please compile with a C++ compiler."
#endif

#ifdef _MSC_VER
#  ifndef fseeko
#    define fseeko _fseeki64
#  endif
#  ifndef ftello
#    define ftello _ftelli64
#  endif
#endif

// 旧的定长缓冲尺寸：CLI 参数与 save_path 已改为动态存储，长度不再受它限制。
// 保留宏是为了兼容可能仍引用它的外部代码。
#define ARGV_LEN 384

// 保存目录（UTF-8）。
extern std::string save_path;

// 生成系统临时目录下的唯一文件路径（不创建文件）。
std::string make_temp_file_path(const char* tag);
// 在当前工作目录下生成唯一临时文件路径（绝对路径，不创建文件）。
std::string make_temp_file_path_in_cwd(const char* tag);
// 删除文件（Windows 使用 _wremove，其它平台 remove）。
void remove_file(const char* path);
void remove_file(const std::string& path);

// UTF-8 <-> std::filesystem::path 的统一转换。
std::filesystem::path utf8_to_path(const std::string& utf8_path);
std::string path_to_utf8(const std::filesystem::path& path);

// RAII 临时文件：持有路径，析构时自动删除。
class TempFile {
public:
    TempFile() = default;
    explicit TempFile(std::string path) noexcept
        : path_(path.empty() ? make_temp_file_path_in_cwd("tmp") : std::move(path)) {}
    ~TempFile() { remove(); }

    TempFile(TempFile&& other) noexcept : path_(std::move(other.path_)) {}
    TempFile& operator=(TempFile&& other) noexcept {
        if (this != &other) {
            remove();
            path_ = std::move(other.path_);
        }
        return *this;
    }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    bool empty() const noexcept { return path_.empty(); }
    const std::string& path() const noexcept { return path_; }
    const char* c_str() const noexcept { return path_.c_str(); }

    void remove();
    std::string release() noexcept { std::string p; p.swap(path_); return p; }

private:
    std::string path_;
};

// 把内存缓冲写入系统临时文件，返回 RAII 句柄（空对象表示失败）。
TempFile write_buffer_to_temp(const void* data, size_t size, const char* tag);

struct FileDeleter {
    void operator()(FILE* f) const noexcept {
        if (f) fclose(f);
    }
};

using UniqueFile = std::unique_ptr<FILE, FileDeleter>;

// 增强文件类：在 FILE* 之上包一层 std::iostream。
//  - file_   负责所有权 + fclose
//  - buf_    把 FILE* 适配成 streambuf
//  - stream_ 真正的 iostream，负责格式化/操纵符/异常位
// 所有 operator<< / operator>> 都转发到 stream_，因此 std::hex、
// std::setw、std::setprecision、std::endl 等标准操纵符全部生效。
class EnhancedFile {
private:
    UniqueFile file_;
    FileBuf    buf_;
    std::unique_ptr<std::iostream> stream_;

    void bind_stream_noexcept() noexcept {
        if (!file_) return;
        try {
            stream_ = std::make_unique<std::iostream>(&buf_);
        } catch (...) {
            // 分配失败：退回只有 FILE* 的退化状态，对象无效
            stream_.reset();
            file_.reset();
            buf_.attach(nullptr);
        }
    }

public:
    EnhancedFile() noexcept = default;

    explicit EnhancedFile(FILE* f) noexcept
        : file_(f), buf_(f) { bind_stream_noexcept(); }

    explicit EnhancedFile(UniqueFile&& f) noexcept
        : file_(std::move(f)), buf_(file_.get()) { bind_stream_noexcept(); }

    EnhancedFile(EnhancedFile&& rhs) noexcept
        : file_(std::move(rhs.file_)), buf_(file_.get()) {
        if (rhs.stream_) {
            bind_stream_noexcept();
            if (stream_) {
                try {
                    stream_->copyfmt(*rhs.stream_);
                    stream_->setstate(rhs.stream_->rdstate());
                } catch (...) {
                    stream_.reset();
                    file_.reset();
                    buf_.attach(nullptr);
                }
            }
        }
        rhs.buf_.attach(nullptr);
        rhs.stream_.reset();
    }

    EnhancedFile& operator=(EnhancedFile&& rhs) noexcept {
        if (this != &rhs) {
            stream_.reset();
            buf_.attach(nullptr);
            file_ = std::move(rhs.file_);
            buf_.attach(file_.get());
            if (file_) bind_stream_noexcept();
            if (stream_ && rhs.stream_) {
                try {
                    stream_->copyfmt(*rhs.stream_);
                    stream_->setstate(rhs.stream_->rdstate());
                } catch (...) {
                    stream_.reset();
                    file_.reset();
                    buf_.attach(nullptr);
                }
            }
            rhs.buf_.attach(nullptr);
            rhs.stream_.reset();
        }
        return *this;
    }

    EnhancedFile(const EnhancedFile&) = delete;
    EnhancedFile& operator=(const EnhancedFile&) = delete;

    ~EnhancedFile() = default;

    // ---- 状态查询 ----
    explicit operator bool() const noexcept { return static_cast<bool>(file_); }
    bool operator!() const noexcept { return !file_; }
    operator FILE*() const noexcept { return file_.get(); }
    FILE* operator->() const noexcept { return file_.get(); }

    FILE* get() const noexcept { return file_.get(); }

    // ---- iostream 访问 ----
    std::iostream& stream() const noexcept { return *stream_; }

    // ---- 所有权 ----
    UniqueFile release() noexcept {
        stream_.reset();
        buf_.attach(nullptr);
        return std::move(file_);
    }

    void reset(FILE* f = nullptr) noexcept {
        stream_.reset();
        buf_.attach(f);
        file_.reset(f);
        if (f) bind_stream_noexcept();
    }

    void close() noexcept { reset(nullptr); }

    // ---- C 风格便利函数（保持原签名） ----
    int flush() noexcept { return file_ ? std::fflush(file_.get()) : -1; }
    long tell() const noexcept { return file_ ? std::ftell(file_.get()) : -1L; }

#ifdef _MSC_VER
    int64_t tello() const noexcept { return file_ ? static_cast<int64_t>(ftello(file_.get())) : -1LL; }
    int seeko(int64_t offset, int origin) noexcept {
        if (!file_) return -1;
        if (!buf_.sync_position()) return -1;
        return fseeko(file_.get(), offset, origin);
    }
#else
    off_t tello() const noexcept { return file_ ? ftello(file_.get()) : static_cast<off_t>(-1); }
    int seeko(off_t offset, int origin) noexcept {
        if (!file_) return -1;
        if (!buf_.sync_position()) return -1;
        return fseeko(file_.get(), offset, origin);
    }
#endif

    int seek(long offset, int origin) noexcept {
        if (!file_) return -1;
        if (!buf_.sync_position()) return -1;
        return std::fseek(file_.get(), offset, origin);
    }
    void rewind() noexcept {
        if (file_) std::rewind(file_.get());
        if (stream_) stream_->clear();
    }

    int eof() const noexcept {
        if (stream_ && stream_->eof()) return 1;
        return file_ ? std::feof(file_.get()) : 0;
    }
    int error() const noexcept {
        if (stream_ && stream_->bad()) return 1;
        return file_ ? std::ferror(file_.get()) : 0;
    }
    void clearerr() noexcept {
        if (file_) std::clearerr(file_.get());
        if (stream_) stream_->clear();
    }

    // ---- C 风格 IO ----
    size_t write(const void* buffer, size_t size, size_t count) noexcept {
        return file_ ? std::fwrite(buffer, size, count, file_.get()) : 0;
    }
    size_t read(void* buffer, size_t size, size_t count) noexcept {
        return file_ ? std::fread(buffer, size, count, file_.get()) : 0;
    }
    int putc(int ch) noexcept { return file_ ? std::fputc(ch, file_.get()) : EOF; }
    int getc() noexcept { return file_ ? std::fgetc(file_.get()) : EOF; }
    int puts(const char* s) noexcept { return file_ ? std::fputs(s, file_.get()) : EOF; }
    char* gets(char* b, int m) noexcept { return file_ ? std::fgets(b, m, file_.get()) : nullptr; }

    int printf(const char* fmt, ...) noexcept {
        if (!file_) return -1;
        va_list a; va_start(a, fmt);
        int r = std::vfprintf(file_.get(), fmt, a);
        va_end(a);
        return r;
    }
    int scanf(const char* fmt, ...) noexcept {
        if (!file_) return EOF;
        va_list a; va_start(a, fmt);
        int r = std::vfscanf(file_.get(), fmt, a);
        va_end(a);
        return r;
    }

    std::string read_all_string() const;
    std::string read_all_chunked(size_t chunk_size = 4096);

    // ---- 兼容旧 API：格式化状态转发到 iostream ----
    void set_hex(bool en) noexcept {
        if (stream_) stream_->setf(en ? std::ios::hex : std::ios::dec, std::ios::basefield);
    }
    void set_oct(bool en) noexcept {
        if (stream_) stream_->setf(en ? std::ios::oct : std::ios::dec, std::ios::basefield);
    }
    void set_dec(bool en) noexcept {
        if (stream_ && en) stream_->setf(std::ios::dec, std::ios::basefield);
    }
    void set_showbase(bool en) noexcept {
        if (stream_) stream_->setf(en ? std::ios::showbase : std::ios_base::fmtflags(0),
                                    std::ios::showbase);
    }
    void set_uppercase(bool en) noexcept {
        if (stream_) stream_->setf(en ? std::ios::uppercase : std::ios_base::fmtflags(0),
                                    std::ios::uppercase);
    }
    void set_boolalpha(bool en) noexcept {
        if (stream_) stream_->setf(en ? std::ios::boolalpha : std::ios_base::fmtflags(0),
                                    std::ios::boolalpha);
    }
    void set_skipws(bool en) noexcept {
        if (stream_) stream_->setf(en ? std::ios::skipws : std::ios_base::fmtflags(0),
                                    std::ios::skipws);
    }
    void set_width(int w) noexcept     { if (stream_) stream_->width(w); }
    void set_precision(int p) noexcept { if (stream_) stream_->precision(p); }
    void set_fill(char c) noexcept     { if (stream_) stream_->fill(c); }

    bool is_hex() const noexcept {
        return stream_ && (stream_->flags() & std::ios::basefield) == std::ios::hex;
    }
    bool is_oct() const noexcept {
        return stream_ && (stream_->flags() & std::ios::basefield) == std::ios::oct;
    }
    bool is_dec() const noexcept {
        return stream_ && (stream_->flags() & std::ios::basefield) == std::ios::dec;
    }
    bool showbase() const noexcept  { return stream_ && (stream_->flags() & std::ios::showbase) != 0; }
    bool uppercase() const noexcept { return stream_ && (stream_->flags() & std::ios::uppercase) != 0; }
    bool boolalpha() const noexcept { return stream_ && (stream_->flags() & std::ios::boolalpha) != 0; }
    bool skipws() const noexcept    { return stream_ && (stream_->flags() & std::ios::skipws) != 0; }
    int  width() const noexcept     { return stream_ ? stream_->width() : 0; }
    int  precision() const noexcept { return stream_ ? stream_->precision() : 6; }
    char fill() const noexcept      { return stream_ ? stream_->fill() : ' '; }
};

// 工厂函数
EnhancedFile oxfopen_enhanced(const char* fn, const char* mode);
EnhancedFile my_oxfopen_enhanced(const char* fn, const char* mode);

// 原有的函数声明
FILE* oxfopen(const char* fn, const char* mode);
FILE* my_oxfopen(const char* fn, const char* mode);

// 此接口已废弃
UniqueFile oxfopen_unique(const char* fn, const char* mode);
UniqueFile my_oxfopen_unique(const char* fn, const char* mode);

// 兼容旧代码：file_manip::xxx 直接等价于 std::xxx
namespace file_manip {
    using std::hex;
    using std::dec;
    using std::oct;
    using std::showbase;
    using std::noshowbase;
    using std::uppercase;
    using std::nouppercase;
    using std::boolalpha;
    using std::noboolalpha;
    using std::skipws;
    using std::noskipws;
    using std::endl;
    using std::flush;
}

// ---- 用模板一次性覆盖所有 std::ostream/istream 可处理的类型 ----
namespace detail {
    template <typename T, typename = void>
    struct is_stream_insertable : std::false_type {};
    template <typename T>
    struct is_stream_insertable<T, std::void_t<decltype(
        std::declval<std::ostream&>() << std::declval<const T&>())>>
        : std::true_type {};

    template <typename T, typename = void>
    struct is_stream_extractable : std::false_type {};
    template <typename T>
    struct is_stream_extractable<T, std::void_t<decltype(
        std::declval<std::istream&>() >> std::declval<T&>())>>
        : std::true_type {};
}

template <typename T>
EnhancedFile& operator<<(EnhancedFile& ef, const T& v) {
    if (!ef) return ef;
    static_assert(detail::is_stream_insertable<T>::value,
                  "EnhancedFile: no matching std::ostream::operator<< for this type");
    ef.stream() << v;
    return ef;
}

template <typename T>
EnhancedFile& operator>>(EnhancedFile& ef, T& v) {
    if (!ef) return ef;
    static_assert(detail::is_stream_extractable<T>::value,
                  "EnhancedFile: no matching std::istream::operator>> for this type");
    ef.stream() >> v;
    return ef;
}

// ---- 三处必须显式重载（标准 iostream 语义与本项目旧行为不一致） ----
// 1) std::ostream 有 unsigned char，但按整数打印；旧代码按原始字节写。
EnhancedFile& operator<<(EnhancedFile& ef, unsigned char c);
// 2) std::istream 没有 unsigned char 的重载，必须自己提供。
EnhancedFile& operator>>(EnhancedFile& ef, unsigned char& c);
// 3) std::istream 默认不认 "true"/"false"，旧代码无条件支持。
EnhancedFile& operator>>(EnhancedFile& ef, bool& v);

// 从 UniqueFile 创建 EnhancedFile
inline EnhancedFile to_enhanced(UniqueFile&& uf) {
    return EnhancedFile(std::move(uf));
}

// 从 EnhancedFile 获取 UniqueFile
inline UniqueFile to_unique(EnhancedFile&& ef) {
    return ef.release();
}