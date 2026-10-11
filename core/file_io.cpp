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

char savepath[ARGV_LEN] = { 0 };

// 判断 UTF-8 路径是否为绝对路径。绝对路径不再被 savepath 重定基，这样
// 系统临时文件（绝对路径）不会被写到 `path` 目录下。
static bool is_absolute_utf8(const char* fn) {
    if (!fn || !*fn) return false;
#ifdef _WIN32
    // 支持 "\dir"、"/dir"、"X:\dir"、"X:/dir"、UNC "\\server\share"。
    if (fn[0] == '\\' || fn[0] == '/') return true;
    return std::isalpha((unsigned char)fn[0]) && fn[1] == ':' &&
           (fn[2] == '\\' || fn[2] == '/');
#else
    return fn[0] == '/';
#endif
}

// 简易 UTF-8 合法性检查（只做结构校验，不解析字符）。仅 POSIX 分支使用。
#ifndef _WIN32
static bool is_valid_utf8(const std::string& s) {
    size_t i = 0;
    const size_t n = s.size();
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        size_t extra = 0;
        if (c < 0x80) { i += 1; continue; }
        else if ((c & 0xE0) == 0xC0) extra = 1;
        else if ((c & 0xF0) == 0xE0) extra = 2;
        else if ((c & 0xF8) == 0xF0) extra = 3;
        else return false;
        if (i + extra >= n) return false;
        for (size_t k = 1; k <= extra; ++k) {
            if (((unsigned char)s[i + k] & 0xC0) != 0x80) return false;
        }
        i += extra + 1;
    }
    return true;
}
#endif // !_WIN32

// 生成系统临时目录下的唯一文件路径，保证返回合法 UTF-8。
std::string make_temp_file_path(const char* tag) {
    // 1) 名称部分保证为 ASCII。
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

    std::error_code ec;
    std::filesystem::path dir = std::filesystem::temp_directory_path(ec);

#ifdef _WIN32
    // 2) Windows：原生是宽字符，显式转 UTF-8（不依赖 path::u8string 的实现，
    //    也避免 C++20 下 u8string() 返回 std::u8string 的兼容问题）。
    if (ec || dir.empty()) return {};
    const std::wstring& wdir = dir.native();
    if (wdir.empty()) return {};
    int wlen = (int)wdir.size();
    int len = WideCharToMultiByte(CP_UTF8, 0, wdir.c_str(), wlen, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out((size_t)len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wdir.c_str(), wlen, out.data(), len, nullptr, nullptr);
    if (!out.empty() && out.back() != '\\' && out.back() != '/') out += '\\';
    out += name; // name 为 ASCII
    return out;
#else
    // 3) POSIX：temp_directory_path 返回原生字节。只有当它是合法 UTF-8 时
    //    才直接使用，否则退回 /tmp，保证返回值始终是 UTF-8。
    std::string base;
    if (!ec && !dir.empty()) base = dir.native();
    if (base.empty() || !is_valid_utf8(base)) base = "/tmp";
    if (base.empty()) return {};
    if (base.back() != '/') base += '/';
    base += name;
    return base;
#endif
}

void remove_file(const char* path) {
    if (!path || !*path) return;
#ifdef _WIN32
    // Windows 上路径按 UTF-8 存储，需转宽字符后用 _wremove。
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    if (wlen <= 0) return;
    std::wstring wpath((size_t)wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath.data(), wlen);
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

// 原有的 xfopen 函数实现
FILE* xfopen(const char* fn, const char* mode) {
#ifdef _WIN32
    FILE* file = nullptr;
    
    int wpath_len = MultiByteToWideChar(CP_UTF8, 0, fn, -1, nullptr, 0);
    int wmode_len = MultiByteToWideChar(CP_UTF8, 0, mode, -1, nullptr, 0);
    
    if (wpath_len <= 0 || wmode_len <= 0) {
        return nullptr;
    }
    
    wchar_t* wpath = (wchar_t*)malloc(wpath_len * sizeof(wchar_t));
    wchar_t* wmode = (wchar_t*)malloc(wmode_len * sizeof(wchar_t));
    
    if (wpath && wmode) {
        MultiByteToWideChar(CP_UTF8, 0, fn, -1, wpath, wpath_len);
        MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, wmode_len);
        file = _wfopen(wpath, wmode);
    }
    
    free(wpath);
    free(wmode);
    
    return file;
#else
    return fopen(fn, mode);
#endif
}

FILE *my_fopen(const char *fn, const char *mode) {
    if (savepath[0] && !is_absolute_utf8(fn)) {
        size_t fn_len = strlen(fn);
        size_t path_len = strlen(savepath);
        char* fix_fn = new (std::nothrow) char[path_len + fn_len + 3];
        if (!fix_fn) return nullptr;
        char* ch;
        if ((ch = const_cast<char*>(strrchr(fn, '/')))) 
            snprintf(fix_fn, path_len + fn_len + 3, "%s/%s", savepath, ch + 1);
        else if ((ch = const_cast<char*>(strrchr(fn, '\\')))) 
            snprintf(fix_fn, path_len + fn_len + 3, "%s/%s", savepath, ch + 1);
        else 
            snprintf(fix_fn, path_len + fn_len + 3, "%s/%s", savepath, fn);
        FILE* fe = fopen(fix_fn, mode);
        delete[] fix_fn;
        return fe;
    }
    else return fopen(fn, mode);
}

// 原有的 my_xfopen 函数实现
FILE* my_xfopen(const char* fn, const char* mode) {
    FILE* file = nullptr;
    char* fix_fn = nullptr;
    
    if (savepath[0] && !is_absolute_utf8(fn)) {
        size_t fn_len = strlen(fn);
        size_t path_len = strlen(savepath);
        fix_fn = (char*)malloc(path_len + fn_len + 3);
        if (!fix_fn) return nullptr;
        
        const char* filename_part;
        const char* ch;
        
        if ((ch = strrchr(fn, '/'))) 
            filename_part = ch + 1;
        else if ((ch = strrchr(fn, '\\'))) 
            filename_part = ch + 1;
        else 
            filename_part = fn;
        
        snprintf(fix_fn, path_len + fn_len + 3, "%s/%s", savepath, filename_part);
    }
    
    const char* path_to_open = fix_fn ? fix_fn : fn;
    
#ifdef _WIN32
    int wpath_len = MultiByteToWideChar(CP_UTF8, 0, path_to_open, -1, nullptr, 0);
    int wmode_len = MultiByteToWideChar(CP_UTF8, 0, mode, -1, nullptr, 0);
    
    if (wpath_len <= 0 || wmode_len <= 0) {
        if (fix_fn) free(fix_fn);
        return nullptr;
    }
    
    wchar_t* wpath = (wchar_t*)malloc(wpath_len * sizeof(wchar_t));
    wchar_t* wmode = (wchar_t*)malloc(wmode_len * sizeof(wchar_t));
    
    if (wpath && wmode) {
        MultiByteToWideChar(CP_UTF8, 0, path_to_open, -1, wpath, wpath_len);
        MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, wmode_len);
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
    
    if (fix_fn) free(fix_fn);
    return file;
}

// 原有的函数实现
FILE* oxfopen(const char* fn, const char* mode) {
    FILE* file = xfopen(fn, mode);
    if(file == nullptr) file = fopen(fn, mode);
    return file;
}

FILE* my_oxfopen(const char* fn, const char* mode) {
    FILE* file = my_xfopen(fn, mode);
    if(file == nullptr) file = my_fopen(fn, mode);
    return file;
}

UniqueFile oxfopen_unique(const char* fn, const char* mode) {
    return UniqueFile(oxfopen(fn, mode));
}

UniqueFile my_oxfopen_unique(const char* fn, const char* mode) {
    return UniqueFile(my_oxfopen(fn, mode));
}

// EnhancedFile 实现
EnhancedFile::EnhancedFile(FILE* f) noexcept { file = UniqueFile(f); }

EnhancedFile::EnhancedFile(UniqueFile&& f) noexcept { file = std::move(f); }

EnhancedFile::operator bool() const noexcept {
    return file != nullptr;
}

bool EnhancedFile::operator!() const noexcept {
    return file == nullptr;
}

EnhancedFile::operator FILE*() const noexcept {
    return file.get();
}

FILE* EnhancedFile::operator->() const noexcept {
    return file.get();
}

FILE* EnhancedFile::get() const noexcept {
    return file.get();
}

UniqueFile EnhancedFile::release() noexcept {
    return std::move(file);
}

void EnhancedFile::reset(FILE* f) noexcept {
    file.reset(f);
}

void EnhancedFile::close() noexcept {
    file.reset();
}

int EnhancedFile::flush() noexcept {
    if (file) return fflush(file.get());
    return -1;
}

long EnhancedFile::tell() const noexcept {
    if (file) return ftell(file.get());
    return -1L;
}

#ifdef _MSC_VER
int64_t EnhancedFile::tello() const noexcept {
    if (file) return ftello(file.get());
    return -1LL;
}
#else
off_t EnhancedFile::tello() const noexcept {
    if (file) return ftello(file.get());
    return -1L;
}
#endif

int EnhancedFile::seek(long offset, int origin) noexcept {
    if (file) return fseek(file.get(), offset, origin);
    return -1;
}

#ifdef _MSC_VER 
int EnhancedFile::seeko(int64_t offset, int origin) noexcept {
    if (file) return fseeko(file.get(), offset, origin);
    return -1;
}
#else
int EnhancedFile::seeko(off_t offset, int origin) noexcept {
    if (file) return fseeko(file.get(), offset, origin);
    return -1;
}
#endif

int EnhancedFile::eof() const noexcept {
    if (file) return feof(file.get());
    return -1;
}

int EnhancedFile::error() const noexcept {
    if (file) return ferror(file.get());
    return -1;
}

void EnhancedFile::clearerr() noexcept {
    if (file) ::clearerr(file.get());
}

void EnhancedFile::rewind() noexcept {
    if (file) ::rewind(file.get());
}

size_t EnhancedFile::write(const void* buffer, size_t size, size_t count) noexcept {
    if (file) return fwrite(buffer, size, count, file.get());
    return 0;
}

size_t EnhancedFile::read(void* buffer, size_t size, size_t count) noexcept {
    if (file) return fread(buffer, size, count, file.get());
    return 0;
}

int EnhancedFile::putc(int ch) noexcept {
    if (file) return fputc(ch, file.get());
    return EOF;
}

int EnhancedFile::getc() noexcept {
    if (file) return fgetc(file.get());
    return EOF;
}

int EnhancedFile::puts(const char* str) noexcept {
    if (file) return fputs(str, file.get());
    return EOF;
}

char* EnhancedFile::gets(char* buffer, int maxSize) noexcept {
    if (file) return fgets(buffer, maxSize, file.get());
    return nullptr;
}

int EnhancedFile::printf(const char* format, ...) noexcept {
    if (!file) return -1;
    va_list args;
    va_start(args, format);
    int result = vfprintf(file.get(), format, args);
    va_end(args);
    return result;
}

int EnhancedFile::scanf(const char* format, ...) noexcept {
    if (!file) return EOF;
    va_list args;
    va_start(args, format);
    int result = vfscanf(file.get(), format, args);
    va_end(args);
    return result;
}
std::string EnhancedFile::read_all_string() const {
    if (!file) return {};

    // 获取文件大小
    fseek(file.get(), 0, SEEK_END);
    long size = ftell(file.get());
    fseek(file.get(), 0, SEEK_SET);

    if (size <= 0) return {};

    std::string content;
    content.resize(size);          // 分配空间

    size_t read_bytes = fread(&content[0], 1, size, file.get());
    content.resize(read_bytes);

    return content;
}

std::string EnhancedFile::read_all_chunked(size_t chunk_size)
{
    if (!file) return {};
    std::string content;
    std::vector<char> buffer(chunk_size);
    size_t bytes_read;
    while ((bytes_read = fread(buffer.data(), 1, chunk_size, file.get())) > 0) {
        content.append(buffer.data(), bytes_read);
    }
    return content;
}

// 工厂函数实现
EnhancedFile oxfopen_enhanced(const char* fn, const char* mode) {
    return EnhancedFile(oxfopen(fn, mode));
}

EnhancedFile my_oxfopen_enhanced(const char* fn, const char* mode) {
    return EnhancedFile(my_oxfopen(fn, mode));
}

// 操纵符实现
EnhancedFile& operator<<(EnhancedFile& ef, file_manip::hex_tag) {
    ef.set_hex(true);
    return ef;
}

EnhancedFile& operator<<(EnhancedFile& ef, file_manip::dec_tag) {
    ef.set_dec(true);
    return ef;
}

EnhancedFile& operator<<(EnhancedFile& ef, file_manip::oct_tag) {
    ef.set_oct(true);
    return ef;
}

EnhancedFile& operator<<(EnhancedFile& ef, file_manip::showbase_tag) {
    ef.set_showbase(true);
    return ef;
}

EnhancedFile& operator<<(EnhancedFile& ef, file_manip::noshowbase_tag) {
    ef.set_showbase(false);
    return ef;
}

EnhancedFile& operator<<(EnhancedFile& ef, file_manip::uppercase_tag) {
    ef.set_uppercase(true);
    return ef;
}

EnhancedFile& operator<<(EnhancedFile& ef, file_manip::nouppercase_tag) {
    ef.set_uppercase(false);
    return ef;
}

EnhancedFile& operator<<(EnhancedFile& ef, file_manip::boolalpha_tag) {
    ef.set_boolalpha(true);
    return ef;
}

EnhancedFile& operator<<(EnhancedFile& ef, file_manip::noboolalpha_tag) {
    ef.set_boolalpha(false);
    return ef;
}

EnhancedFile& operator<<(EnhancedFile& ef, file_manip::skipws_tag) {
    ef.set_skipws(true);
    return ef;
}

EnhancedFile& operator<<(EnhancedFile& ef, file_manip::noskipws_tag) {
    ef.set_skipws(false);
    return ef;
}

EnhancedFile& operator<<(EnhancedFile& ef, file_manip::endl_tag) {
    if (ef) {
        fputc('\n', ef.get());
        ef.flush();
    }
    return ef;
}

EnhancedFile& operator<<(EnhancedFile& ef, file_manip::flush_tag) {
    if (ef) ef.flush();
    return ef;
}

// 标准库兼容的 endl
EnhancedFile& operator<<(EnhancedFile& ef, std::ostream& (*manip)(std::ostream&)) {
    if (ef && manip == static_cast<std::ostream& (*)(std::ostream&)>(std::endl)) {
        fputc('\n', ef.get());
        ef.flush();
    }
    return ef;
}

// 流式输出运算符实现
EnhancedFile& operator<<(EnhancedFile& ef, const char* str) {
    if (ef && str) fputs(str, ef.get());
    return ef;
}

EnhancedFile& operator<<(EnhancedFile& ef, const std::string& str) {
    if (ef) fputs(str.c_str(), ef.get());
    return ef;
}

EnhancedFile& operator<<(EnhancedFile& ef, int val) {
    if (ef) {
        if (ef.is_hex() || ef.is_oct()) {
            unsigned int uval = static_cast<unsigned int>(val);
            if (ef.is_hex()) {
                if (ef.showbase()) fprintf(ef.get(), "0x");
                fprintf(ef.get(), ef.uppercase() ? "%X" : "%x", uval);
            } else {
                if (ef.showbase()) fprintf(ef.get(), "0");
                fprintf(ef.get(), "%o", uval);
            }
        } else {
            fprintf(ef.get(), "%d", val);
        }
    }
    return ef;
}

EnhancedFile& operator<<(EnhancedFile& ef, unsigned int val) {
    if (ef) {
        if (ef.is_hex()) {
            if (ef.showbase()) fprintf(ef.get(), "0x");
            fprintf(ef.get(), ef.uppercase() ? "%X" : "%x", val);
        } else if (ef.is_oct()) {
            if (ef.showbase()) fprintf(ef.get(), "0");
            fprintf(ef.get(), "%o", val);
        } else {
            fprintf(ef.get(), "%u", val);
        }

    }
    return ef;
}

EnhancedFile& operator<<(EnhancedFile& ef, long val) {
    if (ef) {
        if (ef.is_hex() || ef.is_oct()) {
            unsigned long uval = static_cast<unsigned long>(val);
            if (ef.is_hex()) {
                if (ef.showbase()) fprintf(ef.get(), "0x");
                fprintf(ef.get(), ef.uppercase() ? "%lX" : "%lx", uval);
            } else {
                if (ef.showbase()) fprintf(ef.get(), "0");
                fprintf(ef.get(), "%lo", uval);
            }
        } else {
            fprintf(ef.get(), "%ld", val);
        }
    }
    return ef;
}

EnhancedFile& operator<<(EnhancedFile& ef, unsigned long val) {
    if (ef) {
        FILE* f = ef.get();
        if (ef.is_hex()) {
            if (ef.showbase()) fprintf(f, "0x");
            fprintf(f, ef.uppercase() ? "%lX" : "%lx", val);
        } else if (ef.is_oct()) {
            if (ef.showbase()) fprintf(f, "0");
            fprintf(f, "%lo", val);
        } else {
            fprintf(f, "%lu", val);
        }
    }
    return ef;
}

EnhancedFile& operator<<(EnhancedFile& ef, long long val) {
    if (ef) {
        FILE* f = ef.get();
         if (ef.is_hex() || ef.is_oct()) {

            unsigned long long uval = static_cast<unsigned long long>(val);
            if (ef.is_hex()) {
                if (ef.showbase()) fprintf(f, "0x");
                fprintf(f, ef.uppercase() ? "%llX" : "%llx", uval);
            } else {
                if (ef.showbase()) fprintf(f, "0");
                fprintf(f, "%llo", uval);
            }
         } else {
            fprintf(f, "%lld", val);
         }
    }
    return ef;
}

EnhancedFile& operator<<(EnhancedFile& ef, unsigned long long val) {
    if (ef) {
        FILE* f = ef.get();
        if (ef.is_hex()) {
            if (ef.showbase()) fprintf(f, "0x");
            fprintf(f, ef.uppercase() ? "%llX" : "%llx", val);
        } else if (ef.is_oct()) {
            if (ef.showbase()) fprintf(f, "0");
            fprintf(f, "%llo", val);
        } else {
            fprintf(f, "%llu", val);
        }
    }
    return ef;
}

EnhancedFile& operator<<(EnhancedFile& ef, float val) {
    if (ef) fprintf(ef.get(), "%f", val);
    return ef;
}

EnhancedFile& operator<<(EnhancedFile& ef, double val) {
    if (ef) fprintf(ef.get(), "%f", val);
    return ef;
}

EnhancedFile& operator<<(EnhancedFile& ef, long double val) {
    if (ef) fprintf(ef.get(), "%Lf", val);
    return ef;
}

EnhancedFile& operator<<(EnhancedFile& ef, char c) {
    if (ef) fputc(c, ef.get());
    return ef;
}

EnhancedFile& operator<<(EnhancedFile& ef, unsigned char c) {
    if (ef) fputc(static_cast<char>(c), ef.get());
    return ef;
}

EnhancedFile& operator<<(EnhancedFile& ef, bool val) {
    if (ef) {
        if (ef.boolalpha()) {
            fputs(val ? "true" : "false", ef.get());
        } else {
            fprintf(ef.get(), "%d", val ? 1 : 0);
        }
    }
    return ef;
}

EnhancedFile& operator<<(EnhancedFile& ef, const void* ptr) {
    if (ef) fprintf(ef.get(), "%p", ptr);
    return ef;
}

// 流式输入运算符实现
EnhancedFile& operator>>(EnhancedFile& ef, std::string& str) {
    if (ef) {
        str.clear();
        
        // 跳过空白字符
        if (ef.skipws()) {
            int ch;
            while ((ch = fgetc(ef.get())) != EOF && std::isspace(static_cast<unsigned char>(ch)));
            if (ch != EOF) ungetc(ch, ef.get());
        }
        
        // 读取非空白字符
        char buffer[4096];
        if (fscanf(ef.get(), "%4095s", buffer) == 1) {
            str = buffer;
        }
    }
    return ef;
}

EnhancedFile& operator>>(EnhancedFile& ef, int& val) {
    if (ef) {
        if (fscanf(ef.get(), "%d", &val) != 1) {
            val = 0;  // 读取失败时设置默认值
        }
    }
    return ef;
}

EnhancedFile& operator>>(EnhancedFile& ef, unsigned int& val) {
    if (ef) {
        if (fscanf(ef.get(), "%u", &val) != 1) {
            val = 0;
        }
    }
    return ef;
}

EnhancedFile& operator>>(EnhancedFile& ef, long& val) {
    if (ef) {
        if (fscanf(ef.get(), "%ld", &val) != 1) {
            val = 0;
        }
    }
    return ef;
}

EnhancedFile& operator>>(EnhancedFile& ef, unsigned long& val) {
    if (ef) {
        if (fscanf(ef.get(), "%lu", &val) != 1) {
            val = 0;
        }
    }
    return ef;
}

EnhancedFile& operator>>(EnhancedFile& ef, long long& val) {
    if (ef) {
        if (fscanf(ef.get(), "%lld", &val) != 1) {
            val = 0;
        }
    }
    return ef;
}

EnhancedFile& operator>>(EnhancedFile& ef, unsigned long long& val) {
    if (ef) {
        if (fscanf(ef.get(), "%llu", &val) != 1) {
            val = 0;
        }
    }
    return ef;
}

EnhancedFile& operator>>(EnhancedFile& ef, float& val) {
    if (ef) {
        if (fscanf(ef.get(), "%f", &val) != 1) {
            val = 0.0f;
        }
    }
    return ef;
}

EnhancedFile& operator>>(EnhancedFile& ef, double& val) {
    if (ef) {
        if (fscanf(ef.get(), "%lf", &val) != 1) {
            val = 0.0;
        }
    }
    return ef;
}

EnhancedFile& operator>>(EnhancedFile& ef, long double& val) {
    if (ef) {
        if (fscanf(ef.get(), "%Lf", &val) != 1) {
            val = 0.0L;
        }
    }
    return ef;
}

EnhancedFile& operator>>(EnhancedFile& ef, char& c) {
    if (ef) {
        if (ef.skipws()) {
            int ch;
            while ((ch = fgetc(ef.get())) != EOF && std::isspace(static_cast<unsigned char>(ch)));
            if (ch != EOF) {
                c = static_cast<char>(ch);
                return ef;
            }
        } else {
            int ch = fgetc(ef.get());
            if (ch != EOF) {
                c = static_cast<char>(ch);
                return ef;
            }
        }
    }
    return ef;
}

EnhancedFile& operator>>(EnhancedFile& ef, unsigned char& c) {
    if (ef) {
        int ch = fgetc(ef.get());
        if (ch != EOF) {
            c = static_cast<unsigned char>(ch);
        }
    }
    return ef;
}

EnhancedFile& operator>>(EnhancedFile& ef, bool& val) {
    if (ef) {
        std::string str;
        ef >> str;
        if (str == "true" || str == "1") {
            val = true;
        } else if (str == "false" || str == "0") {
            val = false;
        }
    }
    return ef;
}