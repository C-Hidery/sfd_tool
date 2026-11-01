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
class FileBuf : public std::basic_streambuf<char> {
public:
    explicit FileBuf(FILE* f = nullptr) noexcept : file_(f) {
        setg(nullptr, nullptr, nullptr);
    }
    FILE* file() const noexcept { return file_; }

    void attach(FILE* f) noexcept {
        file_ = f;
        setg(nullptr, nullptr, nullptr);   // 修复：清空 get 区指针
        dir_ = Dir::Unknown;
        has_last_ = false;
    }
    bool sync_position() noexcept {
        if (!file_) return false;
        if (gptr() == nullptr || gptr() >= egptr()) return true;  // 无待退字符
        std::ptrdiff_t pending = egptr() - gptr();
        if (fseeko(file_, -static_cast<off_type>(pending), SEEK_CUR) != 0)
            return false;
        setg(nullptr, nullptr, nullptr);
        return true;
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