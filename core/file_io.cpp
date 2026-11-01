/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#include "file_io.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <cstdio>
#include <cctype>
#include <vector>
#include <string>
#include <atomic>
#include <chrono>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

std::string save_path;

// ---------------------------------------------------------------
//  UTF-8 / 路径工具（原样保留）
// ---------------------------------------------------------------

static bool is_absolute_utf8(const char* fn) {
    if (!fn || !*fn) return false;
#ifdef _WIN32
    if (fn[0] == '\\' || fn[0] == '/') return true;
    return std::isalpha((unsigned char)fn[0]) && fn[1] == ':' &&
           (fn[2] == '\\' || fn[2] == '/');
#else
    return fn[0] == '/';
#endif
}

#ifndef _WIN32
static bool is_valid_utf8(const std::string& s) {
    size_t i = 0;
    const size_t n = s.size();
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { i += 1; continue; }
        size_t extra = 0;
        unsigned char lo = 0x80, hi = 0xBF;
        if (c < 0xC2) {
            return false;
        } else if (c <= 0xDF) {
            extra = 1;
        } else if (c <= 0xEF) {
            extra = 2;
            if (c == 0xE0) lo = 0xA0;
            else if (c == 0xED) hi = 0x9F;
        } else if (c <= 0xF4) {
            extra = 3;
            if (c == 0xF0) lo = 0x90;
            else if (c == 0xF4) hi = 0x8F;
        } else {
            return false;
        }
        if (i + extra >= n) return false;
        unsigned char c1 = (unsigned char)s[i + 1];
        if (c1 < lo || c1 > hi) return false;
        for (size_t k = 2; k <= extra; ++k) {
            if (((unsigned char)s[i + k] & 0xC0) != 0x80) return false;
        }
        i += extra + 1;
    }
    return true;
}
#endif // !_WIN32

std::filesystem::path utf8_to_path(const std::string& utf8_path) {
#ifdef _WIN32
    if (utf8_path.empty()) return std::filesystem::path();
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                  utf8_path.c_str(), (int)utf8_path.size(), nullptr, 0);
    if (len <= 0) {
        return std::filesystem::path(utf8_path);
    }
    std::wstring w((size_t)len, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                        utf8_path.c_str(), (int)utf8_path.size(), w.data(), len);
    return std::filesystem::path(w);
#else
    return std::filesystem::path(utf8_path);
#endif
}

std::string path_to_utf8(const std::filesystem::path& path) {
#ifdef _WIN32
    const std::wstring& w = path.native();
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                  w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out((size_t)len, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                        w.c_str(), (int)w.size(), out.data(), len, nullptr, nullptr);
    return out;
#else
    return path.native();
#endif
}

// ---------------------------------------------------------------
//  临时文件（原样保留）
// ---------------------------------------------------------------

static std::string make_temp_name(const char* tag) {
    std::string name = "sfd_tool_";
    if (tag && *tag) {
        for (const char* p = tag; *p; ++p) {
            unsigned char c = (unsigned char)*p;
            name += (std::isalnum(c) || c == '_' || c == '-') ? (char)c : '_';
        }
    } else {
        name += "tmp";
    }
    static std::atomic<unsigned long long> seq{0};
    unsigned long long n = seq.fetch_add(1, std::memory_order_relaxed);
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    name += "_" + std::to_string((long long)now) + "_" + std::to_string(n);
    return name;
}

static std::string join_temp_path(const std::string& base, const std::string& name) {
    if (base.empty()) return name;
    std::string out = base;
#ifdef _WIN32
    if (out.back() != '\\' && out.back() != '/') out += '\\';
#else
    if (out.back() != '/') out += '/';
#endif
    out += name;
    return out;
}

#ifdef _WIN32
static std::string wide_dir_to_utf8(const std::filesystem::path& dir) {
    const std::wstring& wdir = dir.native();
    if (wdir.empty()) return {};
    int wlen = (int)wdir.size();
    int len = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wdir.c_str(), wlen, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out((size_t)len, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wdir.c_str(), wlen, out.data(), len, nullptr, nullptr);
    return out;
}
#endif

static std::string system_temp_dir_utf8() {
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
    if (ec || dir.empty()) return {};

#ifdef _WIN32
    return wide_dir_to_utf8(dir);
#else
    const std::string native = dir.native();
    return is_valid_utf8(native) ? native : std::string("/tmp");
#endif
}

static std::string current_dir_utf8() {
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::current_path(ec);
    if (ec || dir.empty()) return {};

#ifdef _WIN32
    return wide_dir_to_utf8(dir);
#else
    const std::string native = dir.native();
    return is_valid_utf8(native) ? native : std::string();
#endif
}

std::string make_temp_file_path(const char* tag) {
    const std::string name = make_temp_name(tag);
    std::string dir = system_temp_dir_utf8();
    if (dir.empty()) {
        dir = current_dir_utf8();
        if (dir.empty()) dir = ".";
    }
    return join_temp_path(dir, name);
}

std::string make_temp_file_path_in_cwd(const char* tag) {
    std::string dir = current_dir_utf8();
    if (dir.empty()) dir = ".";
    return join_temp_path(dir, make_temp_name(tag));
}

// ---------------------------------------------------------------
//  删除 / TempFile / write_buffer_to_temp（原样保留）
// ---------------------------------------------------------------

void remove_file(const char* path) {
    if (!path || !*path) return;
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, nullptr, 0);
    if (wlen <= 0) {
        ::remove(path);
        return;
    }
    std::wstring wpath((size_t)wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wpath.data(), wlen);
    _wremove(wpath.c_str());
#else
    ::remove(path);
#endif
}

void remove_file(const std::string& path) {
    remove_file(path.c_str());
}

void TempFile::remove() {
    if (path_.empty()) return;
    remove_file(path_);
    path_.clear();
}

TempFile write_buffer_to_temp(const void* data, size_t size, const char* tag) {
    if (!data || !size) return TempFile{};
    std::string path = make_temp_file_path(tag);
    if (path.empty()) return TempFile{};
    FILE* f = oxfopen(path.c_str(), "wb");
    if (!f) {
        remove_file(path);
        return TempFile{};
    }
    size_t written = fwrite(data, 1, size, f);
    fclose(f);
    if (written != size) {
        remove_file(path);
        return TempFile{};
    }
    return TempFile(std::move(path));
}

// ---------------------------------------------------------------
//  xfopen / my_fopen / my_xfopen / oxfopen / my_oxfopen（原样保留）
// ---------------------------------------------------------------

FILE* xfopen(const char* fn, const char* mode) {
#ifdef _WIN32
    FILE* file = nullptr;

    int wpath_len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, fn, -1, nullptr, 0);
    int wmode_len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, mode, -1, nullptr, 0);

    if (wpath_len <= 0 || wmode_len <= 0) {
        return nullptr;
    }

    wchar_t* wpath = (wchar_t*)malloc(wpath_len * sizeof(wchar_t));
    wchar_t* wmode = (wchar_t*)malloc(wmode_len * sizeof(wchar_t));

    if (wpath && wmode) {
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, fn, -1, wpath, wpath_len);
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, mode, -1, wmode, wmode_len);
        file = _wfopen(wpath, wmode);
    }

    free(wpath);
    free(wmode);

    return file;
#else
    return fopen(fn, mode);
#endif
}

static const char* path_basename(const char* fn) {
    const char* ch = strrchr(fn, '/');
    if (!ch) ch = strrchr(fn, '\\');
    return ch ? ch + 1 : fn;
}

FILE* my_fopen(const char* fn, const char* mode) {
    if (!save_path.empty() && !is_absolute_utf8(fn)) {
        std::string fix_fn = save_path + "/" + path_basename(fn);
        return fopen(fix_fn.c_str(), mode);
    }
    else return fopen(fn, mode);
}

FILE* my_xfopen(const char* fn, const char* mode) {
    FILE* file = nullptr;
    std::string fix_fn;

    if (!save_path.empty() && !is_absolute_utf8(fn)) {
        fix_fn = save_path + "/" + path_basename(fn);
    }

    const char* path_to_open = fix_fn.empty() ? fn : fix_fn.c_str();

#ifdef _WIN32
    int wpath_len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path_to_open, -1, nullptr, 0);
    int wmode_len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, mode, -1, nullptr, 0);

    if (wpath_len <= 0 || wmode_len <= 0) {
        return nullptr;
    }

    wchar_t* wpath = (wchar_t*)malloc(wpath_len * sizeof(wchar_t));
    wchar_t* wmode = (wchar_t*)malloc(wmode_len * sizeof(wchar_t));

    if (wpath && wmode) {
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path_to_open, -1, wpath, wpath_len);
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, mode, -1, wmode, wmode_len);
        file = _wfopen(wpath, wmode);

        free(wpath);
        free(wmode);
    } else {
        if (wpath) free(wpath);
        if (wmode) free(wmode);
    }
#else
    file = fopen(path_to_open, mode);
#endif

    return file;
}

FILE* oxfopen(const char* fn, const char* mode) {
    FILE* file = xfopen(fn, mode);
    if (file == nullptr) file = fopen(fn, mode);
    return file;
}

FILE* my_oxfopen(const char* fn, const char* mode) {
    FILE* file = my_xfopen(fn, mode);
    if (file == nullptr) file = my_fopen(fn, mode);
    return file;
}

UniqueFile oxfopen_unique(const char* fn, const char* mode) {
    return UniqueFile(oxfopen(fn, mode));
}

UniqueFile my_oxfopen_unique(const char* fn, const char* mode) {
    return UniqueFile(my_oxfopen(fn, mode));
}

EnhancedFile oxfopen_enhanced(const char* fn, const char* mode) {
    return EnhancedFile(oxfopen(fn, mode));
}

EnhancedFile my_oxfopen_enhanced(const char* fn, const char* mode) {
    return EnhancedFile(my_oxfopen(fn, mode));
}

// ---------------------------------------------------------------
//  EnhancedFile 非内联成员（其余成员已在 header 内联）
// ---------------------------------------------------------------

std::string EnhancedFile::read_all_string() const {
    if (!file_) return {};

    // 与原实现一致：定位到文件头读全量（不恢复位置）
    std::fseek(file_.get(), 0, SEEK_END);
    long size = std::ftell(file_.get());
    std::fseek(file_.get(), 0, SEEK_SET);

    if (size <= 0) return {};

    std::string content;
    content.resize(static_cast<size_t>(size));

    size_t read_bytes = std::fread(&content[0], 1, content.size(), file_.get());
    content.resize(read_bytes);

    return content;
}

std::string EnhancedFile::read_all_chunked(size_t chunk_size) {
    if (!file_) return {};
    if (chunk_size == 0) chunk_size = 4096;
    std::string content;
    std::vector<char> buffer(chunk_size);
    size_t bytes_read;
    while ((bytes_read = std::fread(buffer.data(), 1, chunk_size, file_.get())) > 0) {
        content.append(buffer.data(), bytes_read);
    }
    return content;
}

// ---------------------------------------------------------------
//  三处显式特化（非模板，优先级高于模板）
// ---------------------------------------------------------------

// 旧代码 f << (unsigned char)c 按原始字节输出；std::ostream 会当整数打印。
EnhancedFile& operator<<(EnhancedFile& ef, unsigned char c) {
    if (ef) std::fputc(static_cast<int>(c), ef.get());
    return ef;
}

// std::istream 没有 unsigned char 重载，自己提供：读单字节，跳过检查与旧版一致。
EnhancedFile& operator>>(EnhancedFile& ef, unsigned char& c) {
    if (ef) {
        int ch = std::fgetc(ef.get());
        if (ch != EOF) c = static_cast<unsigned char>(ch);
    }
    return ef;
}

// 旧代码 f >> b 无条件接受 "true"/"false"/"1"/"0"；保持该行为。
EnhancedFile& operator>>(EnhancedFile& ef, bool& v) {
    if (!ef) return ef;
    std::string s;
    ef.stream() >> s;
    if      (s == "true"  || s == "1") v = true;
    else if (s == "false" || s == "0") v = false;
    else ef.stream().setstate(std::ios::failbit);
    return ef;
}