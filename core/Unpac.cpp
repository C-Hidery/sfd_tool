/*
* SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */

#include "Unpac.h"
#include <cstring>
#include <cstdlib>
#include <climits>
#include <cerrno>
#include <cassert>
#include <cinttypes>   // 新增：PRIx64
#include <cctype>      // 新增：toupper
#include "logging.h"
#include "../common.h"
#ifdef _WIN32
    #include <direct.h>
    #define chdir _wchdir
    #define getcwd _wgetcwd
#endif

// ---------- 静态辅助函数（移植自原代码） ----------
uint16_t PacFile::crc16(uint32_t crc, const void* src, unsigned len) {
    const uint8_t* s = (const uint8_t*)src;
    while (len--) {
        crc ^= *s++;
        for (int i = 0; i < 8; ++i)
            crc = (crc >> 1) ^ ((0 - (crc & 1)) & 0xA001);
    }
    return (uint16_t)crc;
}

// 返回消耗的 UTF-16 码元数（包含结尾 0）
std::string PacFile::u16_to_u8(const uint16_t* s, size_t sn) {
    std::string out;
    out.reserve(sn * 3 + 1);            // 预分配，避免多次 realloc

    auto emit = [&](uint32_t cp) {
        if (cp < 0x20 || cp == 0x7F) cp = '?';   // 控制字符降级

        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    };

    size_t i = 0;
    while (i < sn) {
        uint32_t cp = s[i++];
        if (cp == 0) break;

        if (cp >= 0xD800 && cp <= 0xDBFF) {              // 高位代理
            if (i < sn && s[i] >= 0xDC00 && s[i] <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (s[i] - 0xDC00);
                ++i;
            } else {
                cp = 0xFFFD;
            }
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {       // 孤立低位代理
            cp = 0xFFFD;
        }
        emit(cp);
    }
    return out;
}

// 从 UTF-8 串解出一个码点，p 前进到下一个码点起点
static uint32_t utf8_next(const char*& p, const char* end) {
    if (p >= end) return 0;
    unsigned char c = static_cast<unsigned char>(*p++);
    if (c < 0x80) return c;                       // ASCII

    int n;
    uint32_t cp;
    if      ((c & 0xE0) == 0xC0) { n = 1; cp = c & 0x1Fu; }
    else if ((c & 0xF0) == 0xE0) { n = 2; cp = c & 0x0Fu; }
    else if ((c & 0xF8) == 0xF0) { n = 3; cp = c & 0x07u; }
    else                         { return 0xFFFD; } // 非法首字节

    while (n-- > 0) {
        if (p >= end || (static_cast<unsigned char>(*p) & 0xC0) != 0x80)
            return 0xFFFD;                        // 截断/非法续字节
        cp = (cp << 6) | (static_cast<unsigned char>(*p++) & 0x3Fu);
    }
    return cp;
}

// UTF-8 通配符匹配：支持 * 与 ?，按码点比较
// 返回 true 表示匹配
static bool wildcard_match_impl(const char* p, const char* pe,
                                const char* t, const char* te,
                                int depth) {
    if (depth > 10) return false;                 // 见下文说明
    while (true) {
        if (p >= pe) return t >= te;

        uint32_t pc = utf8_next(p, pe);
        if (pc == '*') {
            // 依次尝试：* 匹配 0 个、1 个、2 个……码点
            for (;;) {
                if (wildcard_match_impl(p, pe, t, te, depth + 1))
                    return true;
                if (t >= te) return false;
                utf8_next(t, te);
            }
        }
        if (t >= te) return false;

        uint32_t tc = utf8_next(t, te);
        if (pc == '?') continue;                  // ? 吃掉一个码点
        if (pc != tc) return false;
    }
}

static bool wildcard_match(const char* pat, const char* text) {
    const char* pe = pat  + std::strlen(pat);
    const char* te = text + std::strlen(text);
    return wildcard_match_impl(pat, pe, text, te, 0);
}

int PacFile::compare_u8_u16(int depth, const char* d, const uint16_t* s, size_t sn) {
    // 返回 0 表示匹配，非 0 表示不匹配（与原语义一致）
    if (!d) return 1;                             // 没有 pattern 视为不匹配
    if (depth > 10) return 1;                     // 不再 ERR_EXIT，交回调用方

    // UTF-16 -> UTF-8
    auto buf = u16_to_u8(s, sn);

    return wildcard_match(d, buf.c_str()) ? 0 : 1;
}

int PacFile::check_path(const char* path) {
    if (!path || !*path) return -1;                 // 空
    if (path[0] == '/' || path[0] == '\\') return -1;   // 绝对路径
    if (path[1] == ':') return -1;                  // 盘符 C:

    for (const char* p = path; *p; ++p) {
        if (*p == '/' || *p == '\\' || *p == ':')
            return -1;
    }

    // Windows 保留设备名（CON, PRN, AUX, NUL, COM1-9, LPT1-9）
    static const char* const reserved[] = {
        "CON","PRN","AUX","NUL",
        "COM1","COM2","COM3","COM4","COM5","COM6","COM7","COM8","COM9",
        "LPT1","LPT2","LPT3","LPT4","LPT5","LPT6","LPT7","LPT8","LPT9"
    };
    char main[8];
    size_t n = 0;
    for (const char* p = path; *p && *p != '.' && n < 7; ++p)
        main[n++] = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
    main[n] = '\0';
    for (const char* r : reserved)
        if (std::strcmp(main, r) == 0) return -1;

    return static_cast<int>(std::strlen(path));
}

// ---------- 类构造/析构 ----------
PacFile::PacFile() noexcept = default;

PacFile::PacFile(PacFile&& other) noexcept
    : files(other.files)
    , fileCount(other.fileCount)
    , head(other.head)
    , fp(std::move(other.fp))
    , m_originalCwd(std::move(other.m_originalCwd))

{
    // 使源对象处于有效但为空的状态
    other.files = nullptr;
    other.fileCount = 0;
    // 其他成员（head、字符串）已在移动后默认清空，无需显式重置
}

PacFile& PacFile::operator=(PacFile&& other) noexcept {
    if (this != &other) {
        // 先释放当前资源
        if (files) free(files);
        // 移动资源
        files = other.files;
        fileCount = other.fileCount;
        head = other.head;
        fp = std::move(other.fp);
        m_originalCwd = std::move(other.m_originalCwd);

        // 重置源对象
        other.files = nullptr;
        other.fileCount = 0;
        // 其他成员已通过移动构造完成，无需额外操作
    }
    return *this;
}

PacFile::~PacFile() {
    if (fp) fp.close();
    if (files) free(files);
}
// ---------- 内部切换和恢复 ----------
#ifndef _WIN32
bool PacFile::changeToDirectory(const char* dir) {
#else
bool PacFile::changeToDirectory(const wchar_t* dir) {
#endif
    if (!dir) return true;  // 无需切换
    // 保存原始目录（如果尚未保存）
#ifndef _WIN32
    if (m_originalCwd.empty()) {
        char* cwd = getcwd(nullptr, 0);
        if (!cwd) {
            perror("getcwd");
            return false;
        }
        m_originalCwd = cwd;
        free(cwd);
    }
    if (chdir(dir) != 0) {
        perror("chdir");
        return false;
    }
#else
    if (m_originalCwd.empty()) {
        wchar_t* cwd = getcwd(nullptr, 0);
        if (!cwd) {
            perror("getcwd");
            return false;
        }
        m_originalCwd = cwd;
        free(cwd);
    }
    if (chdir(dir) != 0) {
        perror("chdir");
        return false;
    }
#endif
    return true;
}

bool PacFile::restoreDirectory() {
    if (m_originalCwd.empty()) return true; // 没有切换过
    if (chdir(m_originalCwd.c_str()) != 0) {
        perror("chdir back");
        return false;
    }
    m_originalCwd.clear();  // 清除缓存，允许后续再次切换
    return true;
}
// ---------- 加载 PAC 文件 ----------
bool PacFile::load(const char* filename) {
    if (files) { free(files); files = nullptr; }
    fileCount = 0;
    if (fp) { fp.close(); }
    fp = oxfopen_enhanced(filename, "rb");
    if (!fp) {
        fprintf(stderr, "fopen(%s) failed\n", filename);
        return false;
    }

    // 读取头部
    if (fread(&head, sizeof(head), 1, fp) != 1) {
        fprintf(stderr, "fread(head) failed\n");
        return false;
    }

    // 检查 magic
    if (head.pac_magic != ~0x50005u) {
        fprintf(stderr, "bad pac_magic\n");
        return false;
    }

    if (head.dir_offset != sizeof(head)) {
        fprintf(stderr, "unexpected directory offset\n");
        return false;
    }

    if (head.file_count >> 10) {
        fprintf(stderr, "too many files\n");
        return false;
    }

    // 解析目录
    return parseDirectory();
}

bool PacFile::parseDirectory() {
    fileCount = head.file_count;
    if (fileCount == 0) {
        files = nullptr;
        return true;
    }

    files = (sprd_file_t*)malloc(fileCount * sizeof(sprd_file_t));
    if (!files) {
        fprintf(stderr, "malloc failed\n");
        return false;
    }

    // 定位到目录起始（应在头部之后）
    if (fseek(fp, head.dir_offset, SEEK_SET) != 0) {
        fprintf(stderr, "fseek to directory failed\n");
        return false;
    }

    for (int i = 0; i < fileCount; ++i) {
        if (fread(&files[i], sizeof(sprd_file_t), 1, fp) != 1) {
            fprintf(stderr, "fread(file entry) failed at index %d\n", i);
            return false;
        }
        if (files[i].struct_size != sizeof(sprd_file_t)) {
            fprintf(stderr, "unexpected struct size\n");
            return false;
        }
    }
    return true;
}

// ---------- 列出文件 ----------
void PacFile::list(const char* pattern) const {
    DEG_LOG(I, "pac_version: %s\n", u16_to_u8(head.pac_version, 24).c_str());
    DEG_LOG(I, "pac_size: %u\n", head.pac_size);
    DEG_LOG(I, "fw_name: %s\n", u16_to_u8(head.fw_name, MAX_U16_SN).c_str());
    DEG_LOG(I, "fw_version: %s\n", u16_to_u8(head.fw_version, MAX_U16_SN).c_str());
    DEG_LOG(I, "fw_alias: %s\n", u16_to_u8(head.fw_alias, 100).c_str());

    uint32_t head_crc = crc16(0, &head, sizeof(head) - 4);
    DEG_LOG(I, "head_crc: 0x%04x", head.head_crc);
    if (head.head_crc != head_crc)
        DEG_LOG(I, "head_crc: (expected 0x%04x)", head_crc);

    for (int i = 0; i < fileCount; ++i) {
        const sprd_file_t& f = files[i];

        if (pattern) {
            bool matchName = !compare_u8_u16(0, pattern, f.name, MAX_U16_SN);
            bool matchId   = (f.id[0] && !compare_u8_u16(0, pattern, f.id, MAX_U16_SN));
            if (!matchName && !matchId) continue;
        }

        if (f.type > 9) printf("type = 0x%x", f.type);
        else            printf("type = %u",  f.type);

        uint64_t size   = (uint64_t)f.size_high       << 32 | f.size;
        uint64_t offset = (uint64_t)f.pac_offset_high << 32 | f.pac_offset;

        if (size)   printf(", size = 0x%" PRIx64, size);
        if (offset) printf(", offset = 0x%" PRIx64, offset);

        if (f.addr_num <= 5) {
            for (unsigned j = 0; j < f.addr_num; ++j) {
                if (!f.addr[j]) continue;
                if (!j) printf(", addr = 0x%x",  (unsigned)f.addr[j]);
                else    printf(", addr%u = 0x%x", j, (unsigned)f.addr[j]);
            }
        }

        if (f.id[0])
            printf(", id = \"%s\"", u16_to_u8(f.id, MAX_U16_SN).c_str());
        if (f.name[0])
            printf(", name = \"%s\"", u16_to_u8(f.name, MAX_U16_SN).c_str());
        printf("\n");
    }
}

// ---------- 提取文件 ----------
bool PacFile::extract(const char* outputDir, const char* pattern) {
    if (!fp) {
        fprintf(stderr, "No PAC file loaded\n");
        return false;
    }
#ifdef _WIN32
    std::wstring wdir;
    const wchar_t* useDir = nullptr;
    if (outputDir) {
        wdir = utf8_to_utf16(outputDir);
        useDir = wdir.c_str();
    }
#else
    const char* useDir = outputDir;
#endif

    bool dirSwitched = false;
    if (useDir) {
        if (!changeToDirectory(useDir)) return false;
        dirSwitched = true;
    }

    bool anyExtracted = false;
    for (int i = 0; i < fileCount; ++i) {
        const sprd_file_t& f = files[i];
        if (!f.name[0] || !f.pac_offset || !f.size) continue;

        if (pattern) {
            bool matchName = !compare_u8_u16(0, pattern, f.name, MAX_U16_SN);
            bool matchId   = (f.id[0] && !compare_u8_u16(0, pattern, f.id, MAX_U16_SN));
            if (!matchName && !matchId) continue;
        }

        std::string name = u16_to_u8(f.name, MAX_U16_SN);
        if (check_path(name.c_str()) < 1) {
            fprintf(stderr, "!!! unsafe filename: %s\n", name.c_str());
            continue;
        }
        if (!extractFile(f, name)) {
            fprintf(stderr, "Failed to extract %s\n", name.c_str());
            if (dirSwitched) restoreDirectory();
            return false;
        }
        printf("Extracted: %s\n", name.c_str());
        anyExtracted = true;
    }

    if (dirSwitched) {
        if (!restoreDirectory()) return false;
    }

    if (!anyExtracted) {
        fprintf(stderr, "No files matched the pattern\n");
        return false;
    }
    return true;
}

bool PacFile::extractFile(const sprd_file_t& file, const std::string& name) {
    uint64_t pac_offset = ((uint64_t)file.pac_offset_high << 32) | file.pac_offset;

    if (pac_offset > (uint64_t)INT64_MAX) {
        fprintf(stderr, "pac_offset too large: 0x%" PRIx64 "\n", pac_offset);
        return false;
    }

    if (fp.seeko(static_cast<int64_t>(pac_offset), SEEK_SET) != 0) {
        fprintf(stderr, "fseek to data offset failed\n");
        return false;
    }

    EnhancedFile fo = oxfopen_enhanced(name.c_str(), "wb");
    if (!fo) {
        fprintf(stderr, "fopen(output) failed for %s\n", name.c_str());
        return false;
    }

    uint64_t remaining = (uint64_t)file.size_high << 32 | file.size;
    const uint64_t chunk = 0x1000;
    uint8_t* buf = (uint8_t*)malloc(chunk);
    if (!buf) { fo.close(); return false; }

    while (remaining > 0) {
        size_t n = (remaining > chunk) ? (size_t)chunk : (size_t)remaining;

        if (fread(buf, 1, n, fp) != n) {
            fprintf(stderr, "fread chunk failed\n");
            free(buf); fo.close();
            return false;
        }
        if (fwrite(buf, 1, n, fo) != n) {
            fprintf(stderr, "fwrite failed\n");
            free(buf); fo.close();
            return false;
        }
        remaining -= n;
    }

    free(buf);
    fo.close();
    return true;
}

// ---------- 校验数据 CRC ----------
bool PacFile::check() const {
    if (!fp) return false;

    // 头部 CRC
    uint16_t head_calc = crc16(0, &head, sizeof(head) - 4);
    if (head_calc != head.head_crc) {
        fprintf(stderr, "head_crc mismatch: 0x%04x vs expected 0x%04x\n",
                head.head_crc, head_calc);
        return false;
    }

    // 数据 CRC：从头部之后开始
    if (head.pac_size < sizeof(head)) {
        fprintf(stderr, "pac_size too small\n");
        return false;
    }

    if (fseeko(fp, sizeof(head), SEEK_SET) != 0) {   // 直接定位到数据区，去掉多余的 seek(0)
        fprintf(stderr, "fseek failed\n");
        return false;
    }

    uint32_t remaining = head.pac_size - sizeof(head);
    const size_t chunk = 0x1000;
    uint8_t* buf = (uint8_t*)malloc(chunk);
    if (!buf) return false;

    uint32_t data_crc = 0;
    while (remaining > 0) {
        size_t n = (remaining > chunk) ? chunk : (size_t)remaining;

        if (fread(buf, 1, n, fp) != n) {             // 参数顺序修正
            free(buf);
            return false;
        }
        data_crc = crc16(data_crc, buf, n);
        remaining -= n;
    }
    free(buf);

    if (data_crc != head.data_crc) {
        fprintf(stderr, "data_crc mismatch: 0x%04x vs expected 0x%04x\n",
                head.data_crc, data_crc);
        return false;
    }
    return true;
}