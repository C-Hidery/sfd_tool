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
#include <cstring>       // [修复] strlen（EnhancedFile::puts）

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
//
// 【注意】TempFile 只是"路径 + 析构删除"，本身没有 IO 接口（没有
// write/read/operator<<）。如果要往临时文件写数据，有二选一：
//   1) 一次性内存写：TempFile fi = write_buffer_to_temp(buf, n, "tag");
//   2) 流式读写：先 TempFile fi(make_temp_file_path_in_cwd("tag"));
//      再用 oxfopen_enhanced(fi.c_str(), "wb") 打开 EnhancedFile 做流式。
// 另外：TempFile("foo") 会登记"析构时删除 ./foo"，不负责创建也不负责
// 生成唯一名字——如果 ./foo 是已存在的用户文件会被误删，慎用。
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
//
// 【本类所有 IO 入口的线程安全约定】
// 非线程安全。同一对象不能被多线程并发调用任何 IO 成员。
//
// 【本类所有 IO 入口的数据流一致约定】
// 以下接口都统一走 FileBuf，与 << / >> / stream() 共用同一套 peek_/dir_
// 状态，可以任意交叉调用，无丢失/错位：
//   - operator<< / operator>> / stream()
//   - read() / write() / putc() / getc() / puts() / gets() / printf()
//   - scanf()     —— 唯一例外：vfscanf 必须直接吃 FILE*，内部用
//                    prepare_external_input + mark_after_external_input
//                    两步包起来，因此对外也安全。
//   - seek() / seeko() / tell() / rewind() / flush()
//   - read_all_string() / read_all_chunked()
// 【唯一必须避免的用法】把 get() 或 operator FILE* 交给外部 fread/fwrite/
// fscanf/fprintf 直接用——一旦 FILE* 离开本类，FileBuf 里的 peek_ 就
// 无人维护，会和后续的流操作错乱。要"自己直通 FILE*"，要么自己先调
// ef.seeko(0, SEEK_CUR) 对齐，要么改用上面的包装接口。
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
            stream_.reset();
            file_.reset();
            buf_.attach(nullptr);
        }
    }

    // [修复] const 接口内部需要调用 FileBuf::sync_position（非 const）。
    // 对外仍是 const：逻辑上只是把"逻辑读位置"对齐到 FILE，不改变对象的
    // 可观察内容。调用失败返回 false（文件未绑定 / fseeko 失败）。
    bool sync_buf_position() const noexcept {
        return const_cast<FileBuf&>(buf_).sync_position();
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

    // [约定] 仅供"只读 FILE* 做非 IO 操作"（如 fileno、setvbuf、fdopen 的
    // 前置查询）使用。不要拿它做 fread/fwrite/fscanf/fprintf/fseek——
    // 那种用法绕过了 FileBuf，会让 peek_/dir_ 失去维护。
    FILE* get() const noexcept { return file_.get(); }

    // ---- iostream 访问 ----
    std::iostream& stream() const noexcept { return *stream_; }

    // ---- 所有权 ----
    // 释放 FILE* 所有权（调用后本对象失效）。调用者需自行负责 fclose。
    UniqueFile release() noexcept {
        stream_.reset();
        buf_.attach(nullptr);
        return std::move(file_);
    }

    // 重置为接管一个新的 FILE*（nullptr 等价于 close）。
    void reset(FILE* f = nullptr) noexcept {
        stream_.reset();
        buf_.attach(f);
        file_.reset(f);
        if (f) bind_stream_noexcept();
    }

    void close() noexcept { reset(nullptr); }

    // ---- 缓冲/位置 ----
    // 刷新"输出方向"的 FILE 缓冲到 OS。不改变 FileBuf 的 peek_/dir_，
    // 因此可以安全地在任何时刻调用（比如流式写之后主动落盘）。
    // 返回 0 表示成功，-1 表示失败或文件未绑定。
    int flush() noexcept {
        return file_ ? std::fflush(file_.get()) : -1;
    }

    // [修复] 返回"逻辑读位置"的字节偏移。内部先退回 peek_ 再 ftell，
    // 因此和流、read()、write() 协同后仍是准确值。
    // 【异常安全】不抛异常；返回 -1L 表示文件未绑定或对齐失败。
    long tell() const noexcept {
        if (!file_) return -1L;
        if (!sync_buf_position()) return -1L;
        return std::ftell(file_.get());
    }

#ifdef _MSC_VER
    int64_t tello() const noexcept {
        if (!file_) return -1LL;
        if (!const_cast<EnhancedFile*>(this)->sync_buf_position()) return -1LL;
        return static_cast<int64_t>(ftello(file_.get()));
    }
    // 定位到偏移。内部先退回 peek_，再 fseeko，方向重置为 Unknown。
    // 返回 0 成功，-1 失败。
    int seeko(int64_t offset, int origin) noexcept {
        if (!file_) return -1;
        if (!buf_.sync_position()) return -1;
        int r = fseeko(file_.get(), offset, origin);
        if (r == 0) buf_.mark_after_external_seek();
        return r;
    }
#else
    off_t tello() const noexcept {
        if (!file_) return static_cast<off_t>(-1);
        if (!sync_buf_position()) return static_cast<off_t>(-1);
        return ftello(file_.get());
    }
    int seeko(off_t offset, int origin) noexcept {
        if (!file_) return -1;
        if (!buf_.sync_position()) return -1;
        int r = fseeko(file_.get(), offset, origin);
        if (r == 0) buf_.mark_after_external_seek();
        return r;
    }
#endif

    // 同上，long 版本。
    int seek(long offset, int origin) noexcept {
        if (!file_) return -1;
        if (!buf_.sync_position()) return -1;
        int r = std::fseek(file_.get(), offset, origin);
        if (r == 0) buf_.mark_after_external_seek();
        return r;
    }

    // [修复] rewind 定位到 0，并清掉 stream 的 eof/error 状态。
    // 与原实现不同：会先退回 peek_ 再 rewind，避免旧 peek_ 被下一次
    // 流读先吐出来。
    void rewind() noexcept {
        if (!file_) return;
        buf_.sync_position();     // 退回 peek_（失败也继续，rewind 本身就是重置）
        std::rewind(file_.get());
        buf_.mark_after_external_seek();
        if (stream_) stream_->clear();
    }

    // 是否 EOF。同时参考 iostream 的 eofbit 和 FILE 的 feof。
    int eof() const noexcept {
        if (stream_ && stream_->eof()) return 1;
        return file_ ? std::feof(file_.get()) : 0;
    }

    // 是否出错。同时参考 iostream 的 badbit 和 FILE 的 ferror。
    int error() const noexcept {
        if (stream_ && stream_->bad()) return 1;
        return file_ ? std::ferror(file_.get()) : 0;
    }

    // 清除 iostream 状态位和 FILE 的 eof/error 位。
    void clearerr() noexcept {
        if (file_) std::clearerr(file_.get());
        if (stream_) stream_->clear();
    }

    // ================= C 风格 IO（[修复]：全部走 FileBuf）=================
    // 与 << / >> / stream() 共用同一套 peek_/dir_ 状态，可任意交叉调用。

    // 写数据。语义与 std::fwrite 相同：
    //   返回成功写入的"完整元素数"（floor(写入字节数 / size)）。
    //   参数 size 或 count 为 0 时返回 0。
    //   count > SIZE_MAX / size 时按 SIZE_MAX / size 截断，防乘法溢出。
    //   文件未绑定或写出失败返回 0。
    // 【异常安全】noexcept。
    size_t write(const void* buffer, size_t size, size_t count) noexcept {
        if (!file_ || !stream_ || !buffer) return 0;
        if (size == 0 || count == 0) return 0;
        if (count > SIZE_MAX / size) count = SIZE_MAX / size;
        const size_t bytes = size * count;
        const std::streamsize ss =
            bytes > static_cast<size_t>(PTRDIFF_MAX)
                ? static_cast<std::streamsize>(PTRDIFF_MAX)
                : static_cast<std::streamsize>(bytes);
        const std::streamsize got =
            buf_.sputn(static_cast<const char*>(buffer), ss);
        return got <= 0 ? 0 : static_cast<size_t>(got) / size;
    }

    // 读数据。语义与 std::fread 相同：
    //   返回成功读到的"完整元素数"（floor(读到字节数 / size)）。
    //   参数 size 或 count 为 0 时返回 0。
    //   读不足（包括 EOF）返回实际读到的完整元素数。
    //   文件未绑定返回 0。
    // 【异常安全】noexcept。
    size_t read(void* buffer, size_t size, size_t count) noexcept {
        if (!file_ || !stream_ || !buffer) return 0;
        if (size == 0 || count == 0) return 0;
        if (count > SIZE_MAX / size) count = SIZE_MAX / size;
        const size_t bytes = size * count;
        const std::streamsize ss =
            bytes > static_cast<size_t>(PTRDIFF_MAX)
                ? static_cast<std::streamsize>(PTRDIFF_MAX)
                : static_cast<std::streamsize>(bytes);
        const std::streamsize got =
            buf_.sgetn(static_cast<char*>(buffer), ss);
        return got <= 0 ? 0 : static_cast<size_t>(got) / size;
    }

    // 写单字节。成功返回写入的字符（作为 unsigned char 提升的 int），
    // 失败/未绑定返回 EOF。与 std::fputc 语义一致。
    int putc(int ch) noexcept {
        if (!file_ || !stream_ || ch == EOF) return EOF;
        auto r = buf_.sputc(static_cast<char>(ch));
        if (std::char_traits<char>::eq_int_type(
                r, std::char_traits<char>::eof()))
            return EOF;
        return static_cast<int>(static_cast<unsigned char>(
            std::char_traits<char>::to_char_type(r)));
    }

    // 读单字节。成功返回读到的字符（作为 unsigned char 提升的 int），
    // EOF/失败/未绑定返回 EOF。与 std::fgetc 语义一致。
    int getc() noexcept {
        if (!file_ || !stream_) return EOF;
        auto r = buf_.sbumpc();
        if (std::char_traits<char>::eq_int_type(
                r, std::char_traits<char>::eof()))
            return EOF;
        return static_cast<int>(static_cast<unsigned char>(
            std::char_traits<char>::to_char_type(r)));
    }

    // 写以 '\0' 结尾的字符串。
    // 【行为约定】与原实现一致：等价于 std::fputs，不追加换行
    // （名字叫 puts 但语义是 fputs，保留是为了兼容旧调用者）。
    // 成功返回非负值（当前实现返回 0），失败/未绑定返回 EOF。
    int puts(const char* s) noexcept {
        if (!file_ || !stream_ || !s) return EOF;
        const std::size_t len = std::strlen(s);
        if (len == 0) return 0;
        auto got = buf_.sputn(s, static_cast<std::streamsize>(len));
        return (got < 0 ||
                static_cast<size_t>(got) != len) ? EOF : 0;
    }

    // 读一行到 b，最多写 m-1 个字符 + '\0'。
    // 遇 '\n' 结束（'\n' 包含在结果里，若有），或 m-1 字符填满，或 EOF。
    // 语义与 std::fgets 一致：
    //   - 成功（哪怕只读到 EOF 前的部分内容）返回 b。
    //   - 立刻 EOF（一个字符都没读到）、参数非法、文件未绑定返回 nullptr。
    //   - b 始终以 '\0' 结尾（m > 0 时）。
    char* gets(char* b, int m) noexcept {
        if (!file_ || !stream_ || !b || m <= 0) return nullptr;
        int i = 0;
        while (i < m - 1) {
            auto r = buf_.sgetc();     // peek，不消耗
            if (std::char_traits<char>::eq_int_type(
                    r, std::char_traits<char>::eof())) {
                if (i == 0) return nullptr;
                break;
            }
            char ch = std::char_traits<char>::to_char_type(r);
            buf_.sbumpc();             // 消耗
            b[i++] = ch;
            if (ch == '\n') break;
        }
        b[i] = '\0';
        return b;
    }

    // 格式化输出。走 vsnprintf + FileBuf::sputn，完全绕开 FILE*，
    // 因此与 << / >> / read / write 完全一致。
    // 返回实际写入的字节数（不含 '\0'），失败/未绑定返回 -1。
    // 【异常安全】noexcept：内部对 std::string 分配失败做捕获，
    // 出错时返回 -1 而不是抛出。
    int printf(const char* fmt, ...) noexcept {
        if (!file_ || !stream_ || !fmt) return -1;
        va_list a; va_start(a, fmt);

        // 第一次：探测需要多少字节
        va_list a2; va_copy(a2, a);
        int n = std::vsnprintf(nullptr, 0, fmt, a2);
        va_end(a2);

        if (n < 0) { va_end(a); return -1; }

        std::string big;
        try {
            big.resize(static_cast<size_t>(n) + 1);
        } catch (...) {
            va_end(a);
            return -1;
        }
        std::vsnprintf(&big[0], big.size(), fmt, a);
        va_end(a);

        auto got = buf_.sputn(big.data(),
                              static_cast<std::streamsize>(n));
        return got == n ? n : -1;
    }

    // 格式化输入。这是唯一必须直接吃 FILE* 的接口（vfscanf 没有
    // streambuf 等价物），内部三步包起来保证与流/read/write 安全交叉：
    //   1) prepare_external_input：退回 peek_，Out→In 时 fflush
    //   2) vfscanf
    //   3) mark_after_external_input：清 peek_，dir_=In
    // 返回值与 std::vfscanf 一致：成功赋值的转换数；到 EOF 前没有任何
    // 转换返回 EOF。文件未绑定、对齐失败（fseeko/fflush 失败）返回 EOF。
    // 【注意】vfscanf 只设置 FILE 的 feof/ferror，不会设 stream 的
    // eofbit/badbit。需要判断时请用 ef.eof() / ef.error()（同时查两侧），
    // 不要用 ef.stream().eof()（只查 stream 侧，看不到 FILE 侧）。
    int scanf(const char* fmt, ...) noexcept {
        if (!file_ || !stream_ || !fmt) return EOF;
        if (!buf_.prepare_external_input()) return EOF;
        va_list a; va_start(a, fmt);
        int r = std::vfscanf(file_.get(), fmt, a);
        va_end(a);
        buf_.mark_after_external_input();
        return r;
    }

    // ================= 一次性读入整文件 =================
    // 语义：从文件开头读全量，读完后 FILE 位置在 EOF 之后，
    // 方向切换为 In（后续 << 会触发 ensure_output 的 fseeko）。
    // 与流、read()、write() 协同：内部先退回 peek_，再 fseeko 到 END
    // 获取大小（fseeko 隐式 fflush，满足更新流规则），再 fseeko 到 0，
    // 再走 FileBuf::sgetn 读全量。因此与前面是否有 << / >> / write 无关，
    // 无需调用者再手动 seek。
    // 【异常安全】不抛异常：内部对 std::string::resize 的 bad_alloc 做
    // 捕获，失败时返回空串。
    std::string read_all_string() const;

    // 从当前位置循环 sgetn 直到 EOF。chunk_size == 0 时按 4096。
    // 与 << / >> / write 协同：sgetn 内部会调 ensure_input 处理方向。
    // 返回读到的全部内容，EOF 或错误后停止。
    // 【异常安全】可能抛 std::bad_alloc（vector/string 增长时）。
    std::string read_all_chunked(size_t chunk_size = 4096);

    // ================= 格式化状态转发到 iostream =================
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