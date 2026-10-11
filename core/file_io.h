/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#pragma once
#include <stdio.h>
#include <memory>
#include <string>
#include <cstdint>
#include <iostream>
#include <filesystem>

#ifndef __cplusplus
    #error "This header requires C++. Please compile with a C++ compiler."
#endif

#ifdef _MSC_VER
#define fseeko _fseeki64
#define ftello _ftelli64
#endif

// 旧的定长缓冲尺寸：CLI 参数与 save_path 已改为动态存储，长度不再受它限制。
// 保留宏是为了兼容可能仍引用它的外部代码。
#define ARGV_LEN 384
// 保存目录（UTF-8）。用 std::string 承载，路径长度不再受定长缓冲限制。
extern std::string save_path;

// 生成系统临时目录下的唯一文件路径（不创建文件）。
// 系统临时目录为空/不可用时退回当前目录。
std::string make_temp_file_path(const char* tag);
// 在当前工作目录下生成唯一临时文件路径（绝对路径，不创建文件）。
std::string make_temp_file_path_in_cwd(const char* tag);
// 删除文件（Windows 使用 _wremove，其它平台 remove）。不存在时静默忽略。
void remove_file(const char* path);
void remove_file(const std::string& path);

// UTF-8 <-> std::filesystem::path 的统一转换。
// Windows 上 std::filesystem::path 的窄字符构造按 ANSI(ACP) 解释，直接用
// std::string 构造会破坏 UTF-8 统一性；跨平台代码请一律走这两个函数：
//   std::filesystem::exists(utf8_to_path(p))
//   std::string utf8_p = path_to_utf8(fs_path);
std::filesystem::path utf8_to_path(const std::string& utf8_path);
std::string path_to_utf8(const std::filesystem::path& path);

// RAII 临时文件：持有路径，析构时自动删除（Windows 使用 _wremove）。
class TempFile {
public:
    TempFile() = default;
    // 空路径检查：传入空路径时直接在当前目录下生成一个唯一临时文件名。
    // RAII 语义不变，析构时仍会删除所持路径。
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

    // 立即删除并清空
    void remove();
    // 放弃所有权（返回路径，析构时不再删除）
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

// 基础类型定义
using UniqueFile = std::unique_ptr<FILE, FileDeleter>;

// 格式化状态标志
enum class NumberBase {
    Dec,
    Hex,
    Oct
};

// 增强的文件类，支持运算符重载
class EnhancedFile {
private:
    UniqueFile file;
    
    // 格式化状态
    struct FormatState {
        NumberBase base = NumberBase::Dec;
        bool showbase = false;   // 显示进制前缀 (0x, 0)
        bool uppercase = false;  // 十六进制大写字母
        bool boolalpha = false;  // true/false 而不是 1/0
        bool skipws = true;      // 跳过空白字符
        int width = 0;           // 字段宽度 (0=无限制)
        int precision = 6;       // 浮点数精度
        char fill = ' ';         // 填充字符
    } format;
    
public:
    // 构造函数
    EnhancedFile() noexcept = default;
    explicit EnhancedFile(FILE* f) noexcept;
    explicit EnhancedFile(UniqueFile&& f) noexcept;
    
    // 移动语义
    EnhancedFile(EnhancedFile&& other) noexcept = default;
    EnhancedFile& operator=(EnhancedFile&& other) noexcept = default;
    
    // 禁止拷贝
    EnhancedFile(const EnhancedFile&) = delete;
    EnhancedFile& operator=(const EnhancedFile&) = delete;
    
    // 析构函数
    ~EnhancedFile() = default;
    
    // 运算符重载
    explicit operator bool() const noexcept;
    bool operator!() const noexcept;
    operator FILE*() const noexcept;
    FILE* operator->() const noexcept;
    
    // 成员函数
    FILE* get() const noexcept;
    UniqueFile release() noexcept;
    void reset(FILE* f = nullptr) noexcept;
    void close() noexcept;
    int flush() noexcept;
    long tell() const noexcept;
#ifdef _MSC_VER
    int64_t tello() const noexcept;
#else
    off_t tello() const noexcept; // `-D_FILE_OFFSET_BITS=64` if on 32-bit Linux
#endif
    int seek(long offset, int origin) noexcept;
#ifdef _MSC_VER
    int seeko(int64_t offset, int origin) noexcept;
#else
    int seeko(off_t offset, int origin) noexcept; // `-D_FILE_OFFSET_BITS=64` if on 32-bit Linux
#endif
    void rewind() noexcept;
    int eof() const noexcept;
    int error() const noexcept;
    void clearerr() noexcept;
    
    // 读写操作
    size_t write(const void* buffer, size_t size, size_t count) noexcept;
    size_t read(void* buffer, size_t size, size_t count) noexcept;
    int putc(int ch) noexcept;
    int getc() noexcept;
    int puts(const char* str) noexcept;
    char* gets(char* buffer, int maxSize) noexcept;
    int printf(const char* format, ...) noexcept;
    int scanf(const char* format, ...) noexcept;
    // 从当前位置读到 EOF
    std::string read_all_string() const;
    
    std::string read_all_chunked(size_t chunk_size = 4096);
    
    // 格式化状态设置
    void set_base(NumberBase base) noexcept { format.base = base; }
    void set_hex(bool enable) noexcept { format.base = enable ? NumberBase::Hex : NumberBase::Dec; }
    void set_oct(bool enable) noexcept { format.base = enable ? NumberBase::Oct : NumberBase::Dec; }
    void set_dec(bool enable) noexcept { if (enable) format.base = NumberBase::Dec; }
    void set_showbase(bool enable) noexcept { format.showbase = enable; }
    void set_uppercase(bool enable) noexcept { format.uppercase = enable; }
    void set_boolalpha(bool enable) noexcept { format.boolalpha = enable; }
    void set_skipws(bool enable) noexcept { format.skipws = enable; }
    void set_width(int w) noexcept { format.width = w; }
    void set_precision(int p) noexcept { format.precision = p; }
    void set_fill(char c) noexcept { format.fill = c; }
    
    // 格式化状态获取
    NumberBase base() const noexcept { return format.base; }
    bool is_hex() const noexcept { return format.base == NumberBase::Hex; }
    bool is_oct() const noexcept { return format.base == NumberBase::Oct; }
    bool is_dec() const noexcept { return format.base == NumberBase::Dec; }
    bool showbase() const noexcept { return format.showbase; }
    bool uppercase() const noexcept { return format.uppercase; }
    bool boolalpha() const noexcept { return format.boolalpha; }
    bool skipws() const noexcept { return format.skipws; }
    int width() const noexcept { return format.width; }
    int precision() const noexcept { return format.precision; }
    char fill() const noexcept { return format.fill; }
};

// 工厂函数
EnhancedFile oxfopen_enhanced(const char* fn, const char* mode);
// 工厂函数
EnhancedFile my_oxfopen_enhanced(const char* fn, const char* mode);

// 原有的函数声明
FILE* oxfopen(const char* fn, const char* mode);
FILE* my_oxfopen(const char* fn, const char* mode);

// 此接口已废弃
UniqueFile oxfopen_unique(const char* fn, const char* mode);
// 此接口已废弃
UniqueFile my_oxfopen_unique(const char* fn, const char* mode);

// 自定义操纵符
namespace file_manip {
    // 进制操纵符
    struct hex_tag {};    inline constexpr hex_tag hex;
    struct dec_tag {};    inline constexpr dec_tag dec;
    struct oct_tag {};    inline constexpr oct_tag oct;
    
    // 格式操纵符
    struct showbase_tag {};    inline constexpr showbase_tag showbase;
    struct noshowbase_tag {};  inline constexpr noshowbase_tag noshowbase;
    struct uppercase_tag {};   inline constexpr uppercase_tag uppercase;
    struct nouppercase_tag {}; inline constexpr nouppercase_tag nouppercase;
    struct boolalpha_tag {};   inline constexpr boolalpha_tag boolalpha;
    struct noboolalpha_tag {}; inline constexpr noboolalpha_tag noboolalpha;
    struct skipws_tag {};      inline constexpr skipws_tag skipws;
    struct noskipws_tag {};    inline constexpr noskipws_tag noskipws;
    
    // 流操纵符
    struct endl_tag {};    inline constexpr endl_tag endl;
    struct flush_tag {};   inline constexpr flush_tag flush;
}

// 进制操纵符重载
EnhancedFile& operator<<(EnhancedFile& ef, file_manip::hex_tag);
EnhancedFile& operator<<(EnhancedFile& ef, file_manip::dec_tag);
EnhancedFile& operator<<(EnhancedFile& ef, file_manip::oct_tag);

// 格式操纵符重载
EnhancedFile& operator<<(EnhancedFile& ef, file_manip::showbase_tag);
EnhancedFile& operator<<(EnhancedFile& ef, file_manip::noshowbase_tag);
EnhancedFile& operator<<(EnhancedFile& ef, file_manip::uppercase_tag);
EnhancedFile& operator<<(EnhancedFile& ef, file_manip::nouppercase_tag);
EnhancedFile& operator<<(EnhancedFile& ef, file_manip::boolalpha_tag);
EnhancedFile& operator<<(EnhancedFile& ef, file_manip::noboolalpha_tag);
EnhancedFile& operator<<(EnhancedFile& ef, file_manip::skipws_tag);
EnhancedFile& operator<<(EnhancedFile& ef, file_manip::noskipws_tag);

// 流操纵符重载
EnhancedFile& operator<<(EnhancedFile& ef, file_manip::endl_tag);
EnhancedFile& operator<<(EnhancedFile& ef, file_manip::flush_tag);

// 标准库兼容的操纵符重载（可选）
EnhancedFile& operator<<(EnhancedFile& ef, std::ostream& (*manip)(std::ostream&));

// 流式输出运算符 (<<)
EnhancedFile& operator<<(EnhancedFile& ef, const char* str);
EnhancedFile& operator<<(EnhancedFile& ef, const std::string& str);
EnhancedFile& operator<<(EnhancedFile& ef, int val);
EnhancedFile& operator<<(EnhancedFile& ef, unsigned int val);
EnhancedFile& operator<<(EnhancedFile& ef, long val);
EnhancedFile& operator<<(EnhancedFile& ef, unsigned long val);
EnhancedFile& operator<<(EnhancedFile& ef, long long val);
EnhancedFile& operator<<(EnhancedFile& ef, unsigned long long val);
EnhancedFile& operator<<(EnhancedFile& ef, float val);
EnhancedFile& operator<<(EnhancedFile& ef, double val);
EnhancedFile& operator<<(EnhancedFile& ef, long double val);
EnhancedFile& operator<<(EnhancedFile& ef, char c);
EnhancedFile& operator<<(EnhancedFile& ef, unsigned char c);
EnhancedFile& operator<<(EnhancedFile& ef, bool val);
EnhancedFile& operator<<(EnhancedFile& ef, const void* ptr);

// 流式输入运算符 (>>)
EnhancedFile& operator>>(EnhancedFile& ef, std::string& str);
EnhancedFile& operator>>(EnhancedFile& ef, int& val);
EnhancedFile& operator>>(EnhancedFile& ef, unsigned int& val);
EnhancedFile& operator>>(EnhancedFile& ef, long& val);
EnhancedFile& operator>>(EnhancedFile& ef, unsigned long& val);
EnhancedFile& operator>>(EnhancedFile& ef, long long& val);
EnhancedFile& operator>>(EnhancedFile& ef, unsigned long long& val);
EnhancedFile& operator>>(EnhancedFile& ef, float& val);
EnhancedFile& operator>>(EnhancedFile& ef, double& val);
EnhancedFile& operator>>(EnhancedFile& ef, long double& val);
EnhancedFile& operator>>(EnhancedFile& ef, char& c);
EnhancedFile& operator>>(EnhancedFile& ef, unsigned char& c);
EnhancedFile& operator>>(EnhancedFile& ef, bool& val);

// 从 UniqueFile 创建 EnhancedFile
inline EnhancedFile to_enhanced(UniqueFile&& uf) {
    return EnhancedFile(std::move(uf));
}

// 从 EnhancedFile 获取 UniqueFile
inline UniqueFile to_unique(EnhancedFile&& ef) {
    return ef.release();
}