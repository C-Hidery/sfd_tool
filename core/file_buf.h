/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#pragma once
#include <cstdio>
#include <cstddef>
#include <streambuf>
#include <ios>

#ifdef _MSC_VER
#  ifndef fseeko
#    define fseeko _fseeki64
#  endif
#  ifndef ftello
#    define ftello _ftelli64
#  endif
#endif

// 把 FILE* 适配成 std::basic_streambuf<char>：
//  - 1 字节 get 区（窥视不消耗 FILE，不再依赖 fgetc+ungetc）
//  - 单字符 overflow/uflow 走 fputc/fgetc，批量走 fwrite/fread
//  - seek/tell 直接转发 fseeko/ftello，seek 前把窥视字符退回
//  - 按 C 标准处理更新流的方向切换（输出→输入 fflush；输入→输出 fseek）
//
// 【线程安全】本类不是线程安全的。同一 FILE* 不能被多个 FileBuf 同时
// 接管；外部如果从别的线程直接操作同一个 FILE*，行为未定义。
//
// 【外部直通 FILE* 的约定】
// 若调用者绕过本 streambuf 直接对 FILE* 进行读/写/定位，必须按顺序：
//   1) 读之前：prepare_external_input()      —— 退回 peek_，Out→In 时 fflush
//   2) 读之后：mark_after_external_input()   —— 清 peek_，dir_ = In
//   3) 定位之后：mark_after_external_seek()  —— 清 peek_，dir_ = Unknown
// 三步缺一不可。仅 sync_position() 只退 peek_、不 fflush，不能单独用于
// 外部读（见 C11 7.21.5.3 p6 关于更新流的读写切换要求）。
class FileBuf : public std::basic_streambuf<char> {
public:
    explicit FileBuf(FILE* f = nullptr) noexcept : file_(f) {
        setg(nullptr, nullptr, nullptr);
    }
    FILE* file() const noexcept { return file_; }

    // 绑定新 FILE*。绑定后本 buf 状态重置（peek_/last_/dir_ 全部清空）。
    // 【约定】调用者需保证传入的 FILE* 自身状态干净（刚打开或刚 fseek），
    // 不要把一个方向未知、缓冲有残留的 FILE* 交进来。
    void attach(FILE* f) noexcept {
        file_ = f;
        setg(nullptr, nullptr, nullptr);
        dir_ = Dir::Unknown;
        has_last_ = false;
    }

    // 把 get 区里的窥视字符退回 FILE（逻辑位置 = FILE 位置 - pending）。
    // 无 peek_ 时是 no-op 且返回 true；fseeko 失败返回 false。
    // 仅对齐 peek_，不处理方向（不 fflush）。外部自己 fseek 前用它即可；
    // 外部自己 fscanf 前必须改用 prepare_external_input()。
    bool sync_position() noexcept {
        if (!file_) return false;
        if (gptr() == nullptr || gptr() >= egptr()) return true;  // 无待退字符
        std::ptrdiff_t pending = egptr() - gptr();
        if (fseeko(file_, -static_cast<off_type>(pending), SEEK_CUR) != 0)
            return false;
        setg(nullptr, nullptr, nullptr);
        return true;
    }

    // [修复] 外部即将绕过 streambuf 直接对 FILE* 读（如 vfscanf）前调用。
    // 成功返回后：peek_ 已退回 FILE，方向已切到 In（若之前是 Out 已 fflush）。
    // 满足 C 标准"更新流输出→输入必须 fflush 或 fseek"。
    // 失败（文件未绑定 / 退回失败 / fflush 失败）返回 false，外部不应读。
    bool prepare_external_input() noexcept {
        if (!file_) return false;
        if (!sync_position()) return false;     // 退回 peek_
        if (!ensure_input()) return false;      // Out→In 的 fflush，dir_=In
        return true;
    }

    // [修复] 外部绕过 streambuf 直接对 FILE* 读之后调用。
    // 丢弃 peek_ / last_，并标记方向为 In。
    // 【约定】仅当 FILE 位置确实已前进（外部读过）后调用；只读不写。
    void mark_after_external_input() noexcept {
        setg(nullptr, nullptr, nullptr);
        has_last_ = false;
        dir_ = Dir::In;
    }

    // [修复] 外部绕过 streambuf 直接对 FILE* 定位（fseek/fseeko/rewind）之后
    // 调用。丢弃 peek_ / last_，并把方向重置为 Unknown——下次读/写由
    // ensure_input/ensure_output 根据更新流规则重新判定。
    void mark_after_external_seek() noexcept {
        setg(nullptr, nullptr, nullptr);
        has_last_ = false;
        dir_ = Dir::Unknown;
    }

protected:
    // ---------------- 输出 ----------------
    int_type overflow(int_type c) override {
        if (!file_ || !ensure_output()) return traits_type::eof();
        if (traits_type::eq_int_type(c, traits_type::eof()))
            return traits_type::not_eof(c);
        char ch = traits_type::to_char_type(c);
        return std::fputc(static_cast<unsigned char>(ch), file_) == EOF
                   ? traits_type::eof()
                   : traits_type::to_int_type(ch);
    }

    std::streamsize xsputn(const char* s, std::streamsize n) override {
        if (!file_ || n <= 0 || !ensure_output()) return 0;
        return static_cast<std::streamsize>(
            std::fwrite(s, 1, static_cast<size_t>(n), file_));
    }

    // ---------------- 输入 ----------------
    int_type underflow() override {
        if (!file_ || !ensure_input()) return traits_type::eof();
        if (gptr() < egptr()) return traits_type::to_int_type(*gptr());
        int c = std::fgetc(file_);
        if (c == EOF) return traits_type::eof();
        peek_ = static_cast<char>(c);
        setg(&peek_, &peek_, &peek_ + 1);
        return traits_type::to_int_type(peek_);
    }

    int_type uflow() override {
        if (!file_ || !ensure_input()) return traits_type::eof();
        if (gptr() < egptr()) {
            last_ = *gptr();
            has_last_ = true;
            setg(nullptr, nullptr, nullptr);
            return traits_type::to_int_type(last_);
        }
        int c = std::fgetc(file_);
        if (c == EOF) return traits_type::eof();
        last_ = static_cast<char>(c);
        has_last_ = true;
        return traits_type::to_int_type(last_);
    }

    std::streamsize xsgetn(char* s, std::streamsize n) override {
        if (!file_ || n <= 0 || !ensure_input()) return 0;
        std::streamsize got = 0;
        if (gptr() < egptr()) {                 // 先消耗 get 区
            *s = *gptr();
            last_ = *gptr();
            has_last_ = true;
            setg(nullptr, nullptr, nullptr);
            ++s; --n; ++got;
        }
        if (n > 0) {
            std::streamsize r = static_cast<std::streamsize>(
                std::fread(s, 1, static_cast<size_t>(n), file_));
            if (r > 0) {
                last_ = s[r - 1];
                has_last_ = true;
                got += r;
            }
        }
        return got;
    }

    int_type pbackfail(int_type c) override {
        if (!file_) return traits_type::eof();
        if (gptr() < egptr()) return traits_type::eof();  // 1 字节缓冲已满
        if (traits_type::eq_int_type(c, traits_type::eof())) {
            if (!has_last_) return traits_type::eof();
            peek_ = last_;                              // sungetc：退回最近读到的字符
        } else {
            peek_ = traits_type::to_char_type(c);       // sputbackc(c)
        }
        setg(&peek_, &peek_, &peek_ + 1);
        return traits_type::to_int_type(peek_);
    }

    int sync() override {
        if (!file_) return -1;
        // 输入方向无缓冲可刷；窥视字符的位置同步交给 seek
        if (dir_ == Dir::Out) return std::fflush(file_) == 0 ? 0 : -1;
        return 0;
    }

    // ---------------- 定位 ----------------
    pos_type seekoff(off_type off, std::ios_base::seekdir dir,
                     std::ios_base::openmode) override {
        if (!file_) return pos_type(off_type(-1));
        if (!flush_pending()) return pos_type(off_type(-1));
        int whence;
        switch (dir) {
            case std::ios_base::beg: whence = SEEK_SET; break;
            case std::ios_base::cur: whence = SEEK_CUR; break;
            case std::ios_base::end: whence = SEEK_END; break;
            default: return pos_type(off_type(-1));
        }
        if (fseeko(file_, off, whence) != 0) return pos_type(off_type(-1));
        dir_ = Dir::Unknown;
        has_last_ = false;
        return pos_type(off_type(ftello(file_)));
    }

    pos_type seekpos(pos_type pos, std::ios_base::openmode) override {
        if (!file_) return pos_type(off_type(-1));
        if (!flush_pending()) return pos_type(off_type(-1));
        if (fseeko(file_, static_cast<off_type>(pos), SEEK_SET) != 0)
            return pos_type(off_type(-1));
        dir_ = Dir::Unknown;
        has_last_ = false;
        return pos_type(off_type(ftello(file_)));
    }

private:
    enum class Dir : signed char { Unknown = -1, In = 0, Out = 1 };

    // 把 get 区里的窥视字符退回 FILE 位置（逻辑位置 = FILE 位置 - pending）
    bool flush_pending() noexcept {
        if (gptr() && gptr() < egptr()) {
            std::ptrdiff_t pending = egptr() - gptr();
            if (fseeko(file_, -static_cast<off_type>(pending), SEEK_CUR) != 0)
                return false;
            setg(nullptr, nullptr, nullptr);
        }
        return true;
    }

    // 输出→输入：C 标准允许 fflush
    bool ensure_input() noexcept {
        if (dir_ == Dir::Out) {
            if (std::fflush(file_) != 0) return false;
        }
        dir_ = Dir::In;
        return true;
    }

    // 输入→输出：C 标准要求 fseek（不能 fflush 输入流）；
    // 同时把窥视字符退回，让输出从逻辑位置开始。
    bool ensure_output() noexcept {
        if (dir_ == Dir::In) {
            bool did_seek = false;
            if (gptr() && gptr() < egptr()) {
                std::ptrdiff_t pending = egptr() - gptr();
                if (fseeko(file_, -static_cast<off_type>(pending), SEEK_CUR) != 0)
                    return false;
                setg(nullptr, nullptr, nullptr);
                did_seek = true;
            }
            if (!did_seek && fseeko(file_, 0, SEEK_CUR) != 0)
                return false;
        }
        dir_ = Dir::Out;
        return true;
    }

    FILE* file_ = nullptr;
    char  peek_ = 0;
    char  last_ = 0;
    bool  has_last_ = false;
    Dir   dir_ = Dir::Unknown;
};