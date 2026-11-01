/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#pragma once
#include <cstdio>
#include <streambuf>
#include <ios>

// 与 file_io.h 保持一致的兼容宏（file_buf.h 可单独包含）
#ifdef _MSC_VER
#  ifndef fseeko
#    define fseeko _fseeki64
#  endif
#  ifndef ftello
#    define ftello _ftelli64
#  endif
#endif

// 把 FILE* 适配成 std::basic_streambuf<char>：
//  - 不额外缓冲（FILE 本身有 libc 缓冲，再加一层无益）；
//  - 单字符走 fgetc/fputc，批量走 fread/fwrite；
//  - seek/tell 直接转发 fseeko/ftello；
//  - 一字节 pushback 借 ungetc 实现。
class FileBuf : public std::basic_streambuf<char> {
public:
    explicit FileBuf(FILE* f = nullptr) noexcept : file_(f) {}
    FILE* file() const noexcept { return file_; }
    void attach(FILE* f) noexcept { file_ = f; }

protected:
    // ---- 输出 ----
    int_type overflow(int_type c) override {
        if (!file_) return traits_type::eof();
        if (traits_type::eq_int_type(c, traits_type::eof()))
            return traits_type::not_eof(c);
        char ch = traits_type::to_char_type(c);
        return std::fputc(static_cast<unsigned char>(ch), file_) == EOF
                   ? traits_type::eof()
                   : traits_type::to_int_type(ch);
    }

    std::streamsize xsputn(const char* s, std::streamsize n) override {
        if (!file_ || n <= 0) return 0;
        return static_cast<std::streamsize>(
            std::fwrite(s, 1, static_cast<size_t>(n), file_));
    }

    // ---- 输入 ----
    int_type underflow() override {
        if (!file_) return traits_type::eof();
        int c = std::fgetc(file_);
        if (c == EOF) return traits_type::eof();
        std::ungetc(static_cast<unsigned char>(c), file_);  // 窥视，不消耗
        return traits_type::to_int_type(static_cast<char>(c));
    }

    int_type uflow() override {
        if (!file_) return traits_type::eof();
        int c = std::fgetc(file_);
        return c == EOF ? traits_type::eof()
                        : traits_type::to_int_type(static_cast<char>(c));
    }

    std::streamsize xsgetn(char* s, std::streamsize n) override {
        if (!file_ || n <= 0) return 0;
        return static_cast<std::streamsize>(
            std::fread(s, 1, static_cast<size_t>(n), file_));
    }

    int_type pbackfail(int_type c) override {
        if (!file_) return traits_type::eof();
        if (traits_type::eq_int_type(c, traits_type::eof())) {
            if (fseeko(file_, -1, SEEK_CUR) != 0) return traits_type::eof();
            return traits_type::not_eof(c);
        }
        char ch = traits_type::to_char_type(c);
        return std::ungetc(static_cast<unsigned char>(ch), file_) == EOF
                   ? traits_type::eof()
                   : traits_type::to_int_type(ch);
    }

    int sync() override { return file_ ? std::fflush(file_) : -1; }

    // ---- 定位 ----
    pos_type seekoff(off_type off, std::ios_base::seekdir dir,
                     std::ios_base::openmode) override {
        if (!file_) return pos_type(off_type(-1));
        int whence;
        switch (dir) {
            case std::ios_base::beg: whence = SEEK_SET; break;
            case std::ios_base::cur: whence = SEEK_CUR; break;
            case std::ios_base::end: whence = SEEK_END; break;
            default: return pos_type(off_type(-1));
        }
        if (fseeko(file_, static_cast<off_type>(off), whence) != 0)
            return pos_type(off_type(-1));
        return pos_type(off_type(ftello(file_)));
    }

    pos_type seekpos(pos_type pos, std::ios_base::openmode) override {
        if (!file_) return pos_type(off_type(-1));
        if (fseeko(file_, static_cast<off_type>(pos), SEEK_SET) != 0)
            return pos_type(off_type(-1));
        return pos;
    }

private:
    FILE* file_ = nullptr;
};