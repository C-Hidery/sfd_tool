#ifndef PACFILE_H
#define PACFILE_H

#include <cstdint>
#include <cstdio>
#include <vector>
#include <string>
#include "file_io.h"

#pragma pack(push, 1)
typedef struct {
    uint16_t pac_version[24];
    uint32_t pac_size;
    uint16_t fw_name[256];
    uint16_t fw_version[256];
    uint32_t file_count;
    uint32_t dir_offset;
    uint32_t unknown1[5];
    uint16_t fw_alias[100];
    uint32_t unknown2[3];
    uint32_t unknown[200];
    uint32_t pac_magic;
    uint16_t head_crc, data_crc;
} sprd_head_t;

typedef struct {
    uint32_t struct_size;
    uint16_t id[256];
    uint16_t name[256];
    uint16_t unknown1[256 - 4];
    uint32_t size_high;
    uint32_t pac_offset_high;
    uint32_t size;
    uint32_t type;
    uint32_t flash_use;
    uint32_t pac_offset;
    uint32_t omit_flag;
    uint32_t addr_num;
    uint32_t addr[5];
    uint32_t unknown2[249];
} sprd_file_t;
#pragma pack(pop)

class PacFile {
public:
    // 公有成员（按需求）
    sprd_file_t* files;     // 文件条目数组
    int fileCount;          // 文件数量

    PacFile();
    ~PacFile();

    // 加载 PAC 文件
    bool load(const char* filename);

    // 设置输出目录（后续提取使用）
    void setDirectory(const char* dir);

    // 准备目录：创建（若不存在）并切换工作目录到该目录
    // 返回 true 表示成功，内部会保存原始目录以便恢复
    bool prepareDirectory();

    // 列出文件（可选 pattern 匹配）
    void list(const char* pattern = nullptr) const;

    // 提取文件：若指定 outputDir，则临时切换；否则使用 setDirectory 设置的目录
    // 提取完成后自动恢复原始工作目录
    bool extract(const char* outputDir = nullptr, const char* pattern = nullptr);

    // 校验数据 CRC
    bool check() const;

    // 获取头部信息
    const sprd_head_t& getHead() const { return head; }

    static size_t u16_to_u8(char* d, size_t dn, const uint16_t* s, size_t sn);

private:
    sprd_head_t head;
    EnhancedFile fp;
#ifndef _WIN32
    std::string m_outputDir;      // 由 setDirectory 设置
    std::string m_originalCwd;    // 保存原始工作目录，用于恢复
#else
    std::wstring m_outputDir;      // 由 setDirectory 设置
    std::wstring m_originalCwd;    // 保存原始工作目录，用于恢复
#endif

    // 辅助静态函数
    static uint16_t crc16(uint32_t crc, const void* src, unsigned len);
    static int compare_u8_u16(int depth, const char* d, const uint16_t* s, size_t sn);
    static int check_path(const char* path);

    bool parseDirectory();
    bool extractFile(const sprd_file_t& file, const char* outDir) const;
    bool changeToDirectory(const char* dir);       // 内部切换（平台无关）
    bool restoreDirectory();                       // 恢复原始目录
};

#endif // PACFILE_H