#include "Unpac.h"
#include <cstring>
#include <cstdlib>
#include <climits>
#include <cerrno>
#include <cassert>
#include "logging.h"
#include "../common.h"
#ifdef _WIN32
    #include <direct.h>
    #define mkdir _wmkdir
    #define chdir _wchdir
    #define getcwd _wgetcwd
#else
    #include <sys/stat.h>
    #include <unistd.h>
    #define mkdir(path, mode) mkdir(path, mode)
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

size_t PacFile::u16_to_u8(char* d, size_t dn, const uint16_t* s, size_t sn) {
    size_t i = 0, j = 0;
    if (!d) dn = 0;
    while (i < sn) {
        unsigned a = s[i++];
        if (!a) break;
        if ((a - 0x20) >= 0x5F) a = '?';
        if (j + 1 < dn) d[j++] = (char)a;
    }
    if (dn) d[j] = '\0';
    return i;
}

int PacFile::compare_u8_u16(int depth, const char* d, const uint16_t* s, size_t sn) {
    if (depth > 10) ERR_EXIT("use less wildcards\n");
    while (true) {
        int a = *d++;
        if (a == '*') {
            while (true) {
                if (!compare_u8_u16(depth + 1, d, s, sn)) return 0;
                if (sn == 0 || !s[0]) break;
                ++s; --sn;
            }
            return 1;
        }
        int b = (sn > 0) ? *s++ : 0;
        if (a == '?') {
            if (!b) return 1;
        } else {
            if (a != b) return 1;
            if (!a) break;
        }
    }
    return 0;
}

int PacFile::check_path(const char* path) {
    for (const char* p = path; *p; ++p) {
        if (*p == '/' || *p == '\\' || *p == ':')
            return -1;
    }
    return (int)strlen(path);
}

// ---------- 类构造/析构 ----------
PacFile::PacFile() : files(nullptr), fileCount(0), fp(nullptr) {
    memset(&head, 0, sizeof(head));
}

PacFile::~PacFile() {
    if (fp) fp.close();
    if (files) free(files);
}
// ---------- 设置目录 ----------
void PacFile::setDirectory(const char* dir) {
#ifndef _WIN32
    if (dir) m_outputDir = dir;
    else m_outputDir.clear();
#else
    if (dir) m_outputDir = utf8_to_utf16(std::string(dir));
    else m_outputDir.clear();
#endif
}

// ---------- 准备目录 ----------
bool PacFile::prepareDirectory() {
    if (m_outputDir.empty()) {
        fprintf(stderr, "No output directory set.\n");
        return false;
    }
#ifndef _WIN32
    // 保存当前工作目录（如果尚未保存）
    if (m_originalCwd.empty()) {
        char* cwd = getcwd(nullptr, 0);
        if (!cwd) {
            perror("getcwd");
            return false;
        }
        m_originalCwd = cwd;
        free(cwd);
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
#endif

    // 创建目录（如果不存在）
    if (mkdir(m_outputDir.c_str(), 0755) != 0 && errno != EEXIST) {
        perror("mkdir");
        return false;
    }

    // 切换工作目录
    if (chdir(m_outputDir.c_str()) != 0) {
        perror("chdir");
        return false;
    }

    return true;
}

// ---------- 内部切换和恢复 ----------
bool PacFile::changeToDirectory(const char* dir) {
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
    if (chdir(utf8_to_utf16(std::string(dir))) != 0) {
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
    char str_buf[257];
    for (int i = 0; i < fileCount; ++i) {
        const sprd_file_t& f = files[i];

        // 如果指定了 pattern，且不匹配则跳过
        if (pattern) {
            // 检查 name 或 id 是否匹配
            bool matchName = !compare_u8_u16(0, pattern, f.name, 256);
            bool matchId = (f.id[0] && !compare_u8_u16(0, pattern, f.id, 256));
            if (!matchName && !matchId) continue;
        }
        if (f.type > 9)
        {
            printf("type = 0x%x", f.type);
        }
        else
        {
            printf("type = %u",  f.type);
        }
        long long size = (long long)f.size_high << 32 | f.size;
        if (size) printf(", size = 0x%llx", size);
        long long offset = (long long)f.pac_offset_high << 32 | f.pac_offset;
        if (offset) printf(", offset = 0x%llx", offset);

        if (f.addr_num <= 5) {
            for (unsigned j = 0; j < f.addr_num; ++j) {
                if (!f.addr[j]) continue;
                if (!j) printf(", addr = 0x%x", f.addr[j]);
                else printf(", addr%u = 0x%x", j, f.addr[j]);
            }
        }

        if (f.id[0]) {
            u16_to_u8(str_buf, sizeof(str_buf), f.id, 256);
            printf(", id = \"%s\"", str_buf);
        }
        if (f.name[0]) {
            u16_to_u8(str_buf, sizeof(str_buf), f.name, 256);
            printf(", name = \"%s\"", str_buf);
        }
        printf("\n");
    }
}

// ---------- 提取文件 ----------
bool PacFile::extract(const char* outputDir, const char* pattern) {
    if (!fp) {
        fprintf(stderr, "No PAC file loaded\n");
        return false;
    }

    // 确定实际使用的输出目录
    const char* useDir = outputDir;
    if (!useDir && !m_outputDir.empty()) {
        useDir = m_outputDir.c_str();
    }

    // 切换工作目录（如果指定了输出目录）
    bool dirSwitched = false;
    if (useDir) {
        if (!changeToDirectory(useDir)) {
            return false;
        }
        dirSwitched = true;
    }

    // 提取文件
    char str_buf[257];
    bool anyExtracted = false;
    for (int i = 0; i < fileCount; ++i) {
        const sprd_file_t& f = files[i];
        if (!f.name[0] || !f.pac_offset || !f.size) continue;

        if (pattern) {
            bool matchName = !compare_u8_u16(0, pattern, f.name, 256);
            bool matchId = (f.id[0] && !compare_u8_u16(0, pattern, f.id, 256));
            if (!matchName && !matchId) continue;
        }

        u16_to_u8(str_buf, sizeof(str_buf), f.name, 256);
        if (check_path(str_buf) < 1) {
            fprintf(stderr, "!!! unsafe filename: %s\n", str_buf);
            continue;
        }

        if (!extractFile(f, useDir ? useDir : ".")) {
            fprintf(stderr, "Failed to extract %s\n", str_buf);
            // 恢复目录并返回
            if (dirSwitched) restoreDirectory();
            return false;
        }
        printf("Extracted: %s\n", str_buf);
        anyExtracted = true;
    }

    // 恢复原始工作目录
    if (dirSwitched) {
        if (!restoreDirectory()) {
            return false;
        }
    }

    if (!anyExtracted) {
        fprintf(stderr, "No files matched the pattern\n");
        return false;
    }
    return true;
}

bool PacFile::extractFile(const sprd_file_t& file, const char* outDir) const {
    char str_buf[257];
    u16_to_u8(str_buf, sizeof(str_buf), file.name, 256);

    // 定位到数据偏移
    long long pac_offset = (long long)file.pac_offset_high << 32 | file.pac_offset;
    if (fseeko(fp, pac_offset, SEEK_SET) != 0) {
        fprintf(stderr, "fseek to data offset failed\n");
        return false;
    }

    EnhancedFile fo = oxfopen_enhanced(str_buf, "wb");
    if (!fo) {
        fprintf(stderr, "fopen(output) failed for %s\n", str_buf);
        return false;
    }

    uint64_t remaining = (long long)file.size_high << 32 | file.size;
    const uint64_t chunk = 0x1000;
    uint8_t* buf = (uint8_t*)malloc(chunk);
    if (!buf) {
        fo.close();
        return false;
    }

    while (remaining > 0) {
        size_t n = (remaining > chunk) ? chunk : (size_t)remaining;
        if (fread(buf, n, 1, fp) != 1) {
            fprintf(stderr, "fread chunk failed\n");
            free(buf);
            fo.close();
            return false;
        }
        fwrite(buf, n, 1, fo);
        remaining -= n;
    }

    free(buf);
    fo.close();
    return true;
}

// ---------- 校验数据 CRC ----------
bool PacFile::check() const {
    if (!fp) return false;

    // 先校验头部 CRC（可选）
    uint16_t head_calc = crc16(0, &head, sizeof(head) - 4);
    if (head_calc != head.head_crc) {
        fprintf(stderr, "head_crc mismatch: 0x%04x vs expected 0x%04x\n",
                head.head_crc, head_calc);
        return false;
    }

    // 校验数据 CRC
    if (fseeko(fp, 0, SEEK_SET) != 0) {
        fprintf(stderr, "fseek to beginning failed\n");
        return false;
    }

    uint32_t data_crc = 0;
    uint32_t remaining = head.pac_size;
    const size_t chunk = 0x1000;
    uint8_t* buf = (uint8_t*)malloc(chunk);
    if (!buf) return false;

    // 跳过头部（其数据不参与数据 CRC？原代码从头部之后开始计算）
    // 原代码：先读头部，然后从头部之后开始读剩余部分。
    // 我们按照原逻辑：先跳过头部 size
    if (remaining < sizeof(head)) {
        free(buf);
        return false;
    }
    fseeko(fp, sizeof(head), SEEK_SET);
    remaining -= sizeof(head);

    while (remaining > 0) {
        size_t n = (remaining > chunk) ? chunk : (size_t)remaining;
        if (fread(buf, n, 1, fp) != 1) {
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