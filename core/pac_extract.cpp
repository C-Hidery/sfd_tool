/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#include "pac_extract.h"
#include "../i18n.h"
#include "logging.h"  // 使用统一的 ERR_EXIT
#include "app_state.h"
#include "../common.h"
#define _GNU_SOURCE 1
#define _FILE_OFFSET_BITS 64

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <filesystem>  // C++17 filesystem
#include <sstream>
#include "XmlParser.hpp"
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <direct.h> // for chdir
#else
#include <unistd.h>
#endif
#include <filesystem>  // C++17 filesystem
#include <iostream>    // for error output

#include "logging.h"  // 使用统一的 ERR_EXIT
#include "result.h"   // T2-02: Result/ErrorCode


typedef struct
{
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

typedef struct
{
    uint32_t struct_size;
    uint16_t id[256];
    uint16_t name[256];
    uint16_t unknown1[256 - 4];
    uint32_t size_high;
    uint32_t pac_offset_high;
    uint32_t size;
    uint32_t type; // 0 - operation, 1 - file, 2 - xml, 0x101 - fdl
    uint32_t flash_use; // 1 - used during flashing process
    uint32_t pac_offset;
    uint32_t omit_flag;
    uint32_t addr_num;
    uint32_t addr[5];
    uint32_t unknown2[249];
} sprd_file_t;

static unsigned u_crc16(uint32_t crc, const void* src, unsigned len)
{
    uint8_t* s = (uint8_t*)src;
    int i;
    while (len--)
    {
        crc ^= *s++;
        for (i = 0; i < 8; i++)
            crc = crc >> 1 ^ ((0 - (crc & 1)) & 0xa001);
    }
    return crc;
}

// 使用统一的 ERR_EXIT 而不是本地 U_ERR_EXIT
#define READ(p, n, name) \
    if (fread(p, n, 1, fi) != 1) \
        ERR_EXIT("fread(%s) failed\n", #name)
#define READ1(p) READ(&p, sizeof(p), #p)

enum
{
    MODE_NONE = 0,
    MODE_LIST,
    MODE_EXTRACT,
    MODE_CHECK
};

static size_t u16_to_u8(char* d, size_t dn, const uint16_t* s, size_t sn)
{
    size_t i = 0, j = 0;
    unsigned a;
    if (!d) dn = 0;
    while (i < sn)
    {
        a = s[i++];
        if (!a) break;
        if ((a - 0x20) >= 0x5f) a = '?';
        if (j + 1 < dn) d[j++] = a;
    }
    if (dn) d[j] = 0;
    return i;
}

static int compare_u8_u16(int depth, char* d, const uint16_t* s, size_t sn)
{
    size_t i = 0;
    int a, b;
    if (depth > 10) ERR_EXIT("use less wildcards\n");
    for (;;)
    {
        a = *d++;
        if (a == '*') goto wildcard;
        b = i < sn ? s[i++] : 0;
        if (a == '?')
        {
            if (!b) return 1;
        }
        else
        {
            if (a != b) return 1;
            if (!a) break;
        }
    }
    return 0;

wildcard:
    for (;;)
    {
        if (!compare_u8_u16(depth + 1, d, s + i, sn - i)) return 0;
        b = i < sn ? s[i++] : 0;
        if (!b) break;
    }
    return 1;
}

static int check_path(char* path)
{
    char* s = path;
    int a;
    for (; (a = *s); s++)
    {
        if (a == '/' || a == '\\' || a == ':') return -1;
    }
    return s - path;
}

class Unpac
{
private:
    EnhancedFile fi;
    sprd_head_t head;
    char str_buf[257];
    unsigned chunk;
    const char* dir;
    uint8_t* buf;
    int argc;
    char** argv;
    std::filesystem::path orig_dir;

public:
    Unpac() : fi(NULL), chunk(0x1000), dir(NULL), buf(NULL), argc(0), argv(NULL),
              orig_dir(std::filesystem::current_path())
    {
        memset(&head, 0, sizeof(head));
        memset(str_buf, 0, sizeof(str_buf));
    }

    ~Unpac()
    {
        if (buf) free(buf);
        if (fi) fi.close();
    }

    void setDirectory(const char* directory)
    {
        dir = directory;
        orig_dir = std::filesystem::current_path();
    }

    // 新增：检查和准备输出目录的函数
    void prepareOutputDirectory()
    {
        if (!dir) ERR_EXIT("Error: Output directory not set\n");
        namespace fs = std::filesystem;
        try
        {
#ifdef _WIN32
            fs::path outputPath = utf8_to_utf16(dir);
#else
            fs::path outputPath = dir;
#endif
            if (fs::exists(outputPath)) fs::remove_all(outputPath);
            fs::create_directories(outputPath);
        }
        catch (const fs::filesystem_error& e) { ERR_EXIT("Filesystem error: %s\n", e.what()); }
    }

    bool openPacFile(const char* filename)
    {
        fi = my_oxfopen_enhanced(filename, "rb");
        if (!fi)
        {
            printf("fopen(input) failed\n");
            return false;
        }

        READ1(head);
        if (head.pac_magic != ~0x50005u)
        {
            printf("bad pac_magic\n");
            return false;
        }

        if (head.dir_offset != sizeof(head))
        {
            printf("unexpected directory offset\n");
            return false;
        }

        if (head.file_count >> 10)
        {
            printf("too many files\n");
            return false;
        }

        return true;
    }

    void setFilter(int filter_argc, char** filter_argv)
    {
        argc = filter_argc;
        argv = filter_argv;
    }

#define CONV_STR(x) \
        u16_to_u8(str_buf, sizeof(str_buf), x, sizeof(x) / 2)

    bool listFiles();
    bool extractFiles();
    bool checkCrc();
    sfd::Result<void> checkCrc_result();
    void close();
};


bool Unpac::listFiles()
{
    if (!fi)
    {
        printf("PAC file not opened\n");
        return false;
    }

    CONV_STR(head.pac_version);
    printf("pac_version: %s\n", str_buf);
    printf("pac_size: %u\n", head.pac_size);

    CONV_STR(head.fw_name);
    printf("fw_name: %s\n", str_buf);
    CONV_STR(head.fw_version);
    printf("fw_version: %s\n", str_buf);
    CONV_STR(head.fw_alias);
    printf("fw_alias: %s\n", str_buf);

    // CRC check
    uint32_t head_crc = u_crc16(0, &head, sizeof(head) - 4);
    printf("head_crc: 0x%04x", head.head_crc);
    if (head.head_crc != head_crc)
        printf(" (expected 0x%04x)", head_crc);
    printf("\n");

    // List files
    for (unsigned i = 0; i < head.file_count; i++)
    {
        sprd_file_t file;
        int j;
        READ1(file);
        if (file.struct_size != sizeof(sprd_file_t))
        {
            printf("unexpected struct size\n");
            return false;
        }

        long long file_size = (long long)file.size_high << 32 | file.size;
        long long pac_offset = (long long)file.pac_offset_high << 32 | file.pac_offset;

        // Apply filter
        for (j = 0; j < argc; j++)
            if (!compare_u8_u16(0, argv[j], file.name, 256) ||
                (file.id[0] && !compare_u8_u16(0, argv[j], file.id, 256)))
                break;

        if (argc && j == argc) continue;

        printf(file.type > 9 ? "type = 0x%x" : "type = %u", file.type);
        if (file_size)
            printf(", size = 0x%llx", file_size);
        if (pac_offset)
            printf(", offset = 0x%llx", pac_offset);

        if (file.addr_num <= 5)
            for (j = 0; j < (int)file.addr_num; j++)
            {
                if (!file.addr[j]) continue;
                if (!j) printf(", addr = 0x%x", file.addr[j]);
                else printf(", addr%u = 0x%x", j, file.addr[j]);
            }

        if (file.id[0])
        {
            CONV_STR(file.id);
            printf(", id = \"%s\"", str_buf);
        }
        if (file.name[0])
        {
            CONV_STR(file.name);
            printf(", name = \"%s\"", str_buf);
        }
        printf("\n");
    }
    return true;
}

bool Unpac::extractFiles()
{
    if (!fi)
    {
        printf("PAC file not opened\n");
        return false;
    }

    // 在提取文件前准备输出目录
    prepareOutputDirectory();

#ifdef _WIN32
    std::wstring wdir = utf8_to_utf16(dir);
    if (_wchdir(wdir.c_str())) { printf("chdir failed\n"); return false; }
#else
    if (dir && chdir(dir)) { printf("chdir failed\n"); return false; }
#endif

    for (unsigned i = 0; i < head.file_count; i++)
    {
        sprd_file_t file;
        int j;
        long long file_size, pac_offset;
        READ1(file);
        if (file.struct_size != sizeof(sprd_file_t))
        {
            printf("unexpected struct size\n");
            return false;
        }

        file_size = (long long)file.size_high << 32 | file.size;
        pac_offset = (long long)file.pac_offset_high << 32 | file.pac_offset;

        if (!file.name[0] || !pac_offset || !file_size) continue;

        // Apply filter
        for (j = 0; j < argc; j++)
            if (!compare_u8_u16(0, argv[j], file.name, 256) ||
                (file.id[0] && !compare_u8_u16(0, argv[j], file.id, 256)))
                break;

        if (argc && j == argc) continue;

        EnhancedFile fo;
        uint64_t l;
        uint32_t n;

        CONV_STR(file.name);
        printf("%s\n", str_buf);

        if (fi.seeko(pac_offset, SEEK_SET))
        {
            printf("fseek failed\n");
            return false;
        }

        if (check_path(str_buf) < 1)
        {
            printf("!!! unsafe filename detected\n");
            continue;
        }

        if (!buf)
        {
            buf = (uint8_t*)malloc(chunk);
            if (!buf)
            {
                printf("malloc failed\n");
                return false;
            }
        }

        fo = oxfopen_enhanced(str_buf, "wb");
        if (!fo)
        {
            printf("fopen(output) failed\n");
            return false;
        }

        l = file_size;
        for (; l; l -= n)
        {
            n = (uint32_t)(l > chunk ? chunk : l);
            READ(buf, n, "chunk");
            fo.write(buf, n, 1);
        }
        fo.close();

        if (fi.seek(sizeof(head) + (i + 1) * sizeof(sprd_file_t), SEEK_SET))
        {
            printf("fseek failed\n");
            return false;
        }
    }
#ifdef _WIN32
    std::wstring w_orig = orig_dir.wstring();
    int len = WideCharToMultiByte(CP_UTF8, 0, w_orig.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string path_str;
    if (len > 0) {
        path_str.resize(len);
        WideCharToMultiByte(CP_UTF8, 0, w_orig.c_str(), -1, path_str.data(), len, nullptr, nullptr);
        path_str.pop_back();
    }
#else
    const std::string path_str = orig_dir.string();
#endif
    const char* path = path_str.c_str();
#ifndef _WIN32
    if (path && chdir(path))
    {
        printf("chdir failed\n");
        return false;
    }
#else
    if (path)
    {
        if (_chdir(path))
        {
            printf("chdir failed\n");
            return false;
        }
    }
#endif
    return true;
}

bool Unpac::checkCrc()
{
    auto r = checkCrc_result();
    return static_cast<bool>(r);
}

sfd::Result<void> Unpac::checkCrc_result()
{
    if (!fi)
    {
        printf("PAC file not opened\n");
        return sfd::Result<void>::error(sfd::ErrorCode::InvalidArgument, "pac file not opened");
    }

    // Head CRC
    uint32_t head_crc = u_crc16(0, &head, sizeof(head) - 4);
    printf("head_crc: 0x%04x", head.head_crc);
    bool head_mismatch = false;
    if (head.head_crc != head_crc)
    {
        printf(" (expected 0x%04x)", head_crc);
        head_mismatch = true;
    }
    printf("\n");

    // Data CRC
    uint32_t l = head.pac_size;
    if (l < sizeof(head))
    {
        printf("unexpected pac size\n");
        return sfd::Result<void>::error(sfd::ErrorCode::ParseError, "unexpected pac size");
    }

    if (fi.seeko(sizeof(head), SEEK_SET))
    {
        printf("fseeko failed in checkCrc\n");
        return sfd::Result<void>::error(sfd::ErrorCode::IoError, "fseeko failed in checkCrc");
    }

    uint32_t data_crc = 0;
    uint32_t chunk_size = chunk ? chunk : 0x1000;
    uint8_t* local_buf = (uint8_t*)malloc(chunk_size);
    if (!local_buf)
    {
        printf("malloc failed\n");
        return sfd::Result<void>::error(sfd::ErrorCode::InternalError, "malloc failed in checkCrc");
    }

    l -= sizeof(head);
    while (l)
    {
        uint32_t n = l > chunk_size ? chunk_size : l;
        size_t read_count = fi.read(local_buf, 1, n);
        if (read_count != n)
        {
            printf("fread failed in checkCrc\n");
            free(local_buf);
            return sfd::Result<void>::error(sfd::ErrorCode::IoError, "fread failed in checkCrc");
        }
        data_crc = u_crc16(data_crc, local_buf, n);
        l -= n;
    }

    free(local_buf);

    printf("data_crc: 0x%04x", head.data_crc);
    bool data_mismatch = false;
    if (head.data_crc != data_crc)
    {
        printf(" (expected 0x%04x)", data_crc);
        data_mismatch = true;
    }
    printf("\n");

    if (head_mismatch || data_mismatch)
    {
        if (head_mismatch && data_mismatch)
        {
            return sfd::Result<void>::error(sfd::ErrorCode::ParseError,
                                            "PAC CRC mismatch (head and data)");
        }
        else if (head_mismatch)
        {
            return sfd::Result<void>::error(sfd::ErrorCode::ParseError,
                                            "PAC CRC mismatch (head)");
        }
        else
        {
            return sfd::Result<void>::error(sfd::ErrorCode::ParseError,
                                            "PAC CRC mismatch (data)");
        }
    }

    return sfd::Result<void>::ok();
}

void Unpac::close()
{
    if (buf)
    {
        free(buf);
        buf = NULL;
    }
    if (fi)
    {
        fi.close();
    }
}

static sfd::Result<void> parse_partitions_xml_result(const char* temp_xml_path,
                                                     partition_t* pacptable,
                                                     int* pac_part_count)
{
    // 1. 解析 XML 文件
    XmlParser parser;
    auto root = parser.parseFile(temp_xml_path);
    if (!root)
    {
        DEG_LOG(E, "Failed to parse XML file\n");
        if (isHelperInit)
            gui_idle_call_wait_drag([]()
            {
                showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", _("Failed to parse XML file."));
            }, GTK_WINDOW(helper.getWidget("main_window")));
        return sfd::Result<void>::error(sfd::ErrorCode::IoError, "loadfile failed");
    }

    // 2. 查找 <Partitions> 节点（必须唯一，对应原 stage 检测）
    auto partitionsNodes = root->getDescendants("Partitions");
    if (partitionsNodes.empty())
    {
        DEG_LOG(E, "No Partitions element found\n");
        if (isHelperInit)
            gui_idle_call_wait_drag([]()
            {
                showErrorDialog(
                    GTK_WINDOW(helper.getWidget("main_window")), "Error", _("No <Partitions> element in XML."));
            }, GTK_WINDOW(helper.getWidget("main_window")));
        return sfd::Result<void>::error(sfd::ErrorCode::ParseError, "xml: no Partitions");
    }
    if (partitionsNodes.size() > 1)
    {
        DEG_LOG(E, "More than one partition lists\n");
        if (isHelperInit)
            gui_idle_call_wait_drag([]()
            {
                showErrorDialog(
                    GTK_WINDOW(helper.getWidget("main_window")), "Error",
                    _("More than one partition list found in XML file."));
            }, GTK_WINDOW(helper.getWidget("main_window")));
        return sfd::Result<void>::error(sfd::ErrorCode::ParseError, "xml: more than one partition lists");
    }

    auto partitions = partitionsNodes[0];
    // 获取所有直接子节点 <Partition>（原函数只处理一级子节点）
    auto partitionNodes = partitions->getChildren("Partition");
    if (partitionNodes.empty())
    {
        DEG_LOG(E, "No Partition elements inside Partitions\n");
        if (isHelperInit)
            gui_idle_call_wait_drag([]()
            {
                showErrorDialog(
                    GTK_WINDOW(helper.getWidget("main_window")), "Error",
                    _("No <Partition> elements inside <Partitions>."));
            }, GTK_WINDOW(helper.getWidget("main_window")));
        return sfd::Result<void>::error(sfd::ErrorCode::ParseError, "xml: no Partition elements");
    }

    // 3. 准备 buf（与原函数一致）
    uint32_t buf_size = 0xffff;
    uint8_t* buf_orig = NEWN uint8_t[0x4c * 128];
    uint8_t* buf = buf_orig;
    int found = 0;

    // 4. 遍历每个 Partition，提取 id 和 size 属性
    for (auto& partNode : partitionNodes)
    {
        // 提取 id 属性（原函数通过 sscanf 读取 name）
        std::string id;
        auto it_id = partNode->attributes.find("id");
        if (it_id != partNode->attributes.end())
            id = it_id->second;
        if (id.empty())
        {
            DEG_LOG(E, "Partition missing id attribute\n");
            if (isHelperInit)
                gui_idle_call_wait_drag([]()
                {
                    showErrorDialog(
                        GTK_WINDOW(helper.getWidget("main_window")), "Error",
                        _("Partition element missing 'id' attribute."));
                }, GTK_WINDOW(helper.getWidget("main_window")));
            delete[] buf_orig;
            return sfd::Result<void>::error(sfd::ErrorCode::ParseError, "xml: missing id");
        }

        // 提取 size 属性，支持十进制和十六进制（如 0xffffffff）
        std::string sizeStr;
        auto it_size = partNode->attributes.find("size");
        if (it_size != partNode->attributes.end())
            sizeStr = it_size->second;
        if (sizeStr.empty())
        {
            DEG_LOG(E, "Partition missing size attribute\n");
            if (isHelperInit)
                gui_idle_call_wait_drag([]()
                {
                    showErrorDialog(
                        GTK_WINDOW(helper.getWidget("main_window")), "Error",
                        _("Partition element missing 'size' attribute."));
                }, GTK_WINDOW(helper.getWidget("main_window")));
            delete[] buf_orig;
            return sfd::Result<void>::error(sfd::ErrorCode::ParseError, "xml: missing size");
        }

        char* endptr;
        long long size = strtoll(sizeStr.c_str(), &endptr, 0); // base=0 自动识别 0x 前缀
        if (*endptr != '\0' && !isspace(static_cast<unsigned char>(*endptr)))
        {
            DEG_LOG(E, "Invalid size value\n");
            if (isHelperInit)
                gui_idle_call_wait_drag([]()
                {
                    showErrorDialog(
                        GTK_WINDOW(helper.getWidget("main_window")), "Error", _("Invalid size value in Partition."));
                }, GTK_WINDOW(helper.getWidget("main_window")));
            delete[] buf_orig;
            return sfd::Result<void>::error(sfd::ErrorCode::ParseError, "xml: bad size");
        }

        // 检查剩余缓冲区容量（每个分区固定 0x4c 字节）
        if (buf_size < 0x4c)
        {
            DEG_LOG(E, "Too many partitions\n");
            if (isHelperInit)
                gui_idle_call_wait_drag([]()
                {
                    showErrorDialog(
                        GTK_WINDOW(helper.getWidget("main_window")), "Error", _("Too many partitions in XML file."));
                }, GTK_WINDOW(helper.getWidget("main_window")));
            delete[] buf_orig;
            return sfd::Result<void>::error(sfd::ErrorCode::ParseError, "xml: too many partitions");
        }
        buf_size -= 0x4c;

        // 填充名称区域（36 个宽字符，共 72 字节），每个字符的低字节存 ASCII，高字节清零
        memset(buf, 0, 36 * 2);
        for (size_t i = 0; i < id.size() && i < 36; ++i)
        {
            buf[i * 2] = static_cast<uint8_t>(id[i]);
        }
        if (id.empty())
        {
            DEG_LOG(E, "Empty partition name\n");
            if (isHelperInit)
                gui_idle_call_wait_drag([]()
                {
                    showErrorDialog(
                        GTK_WINDOW(helper.getWidget("main_window")), "Error",
                        _("Empty partition name found in XML file."));
                }, GTK_WINDOW(helper.getWidget("main_window")));
            delete[] buf_orig;
            return sfd::Result<void>::error(sfd::ErrorCode::ParseError, "xml: empty partition name");
        }

        // 写入原始 size（小端，偏移 0x48），与原函数 WRITE32_LE(buf + 0x48, size) 一致
        WRITE32_LE(buf + 0x48, static_cast<uint32_t>(size));

        // 填充 pacptable 结构：名称拷贝，size 左移 20 位（对应原函数 (*(pacptable + found)).size = size << 20）
        strncpy(pacptable[found].name, id.c_str(), sizeof(pacptable[found].name) - 1);
        pacptable[found].name[sizeof(pacptable[found].name) - 1] = '\0';
        pacptable[found].size = size << 20;

        DBG_LOG("[%d] %s, %d\n", found + 1, pacptable[found].name, (int)size);

        buf += 0x4c;
        ++found;
    }

    *pac_part_count = found;
    delete[] buf_orig;
    return sfd::Result<void>::ok();
}

std::string ExtractPartitionsWithTags(const std::string& xmlContent)
{
    XmlParser parser;
    auto root = parser.parseString(xmlContent);
    if (!root) return "";
    auto partitions = root->getFirstDescendant("Partitions");
    if (!partitions) return "";
    return partitions->toXml(); // 直接调用 toXml()
}

std::string FindFirstXMLFile(const std::string& folderPath)
{
    namespace fs = std::filesystem;
    try
    {
#ifdef _WIN32
        fs::path dir = utf8_to_utf16(folderPath);
#else
        fs::path dir = folderPath;
#endif
        if (!fs::exists(dir) || !fs::is_directory(dir))
        {
            std::cerr << "Folder not found: " << folderPath << std::endl;
            return "";
        }

        for (const auto& entry : fs::directory_iterator(dir))
        {
            if (entry.is_regular_file())
            {
                // 使用宽字符获取文件名和扩展名，然后转成 UTF-8
#ifdef _WIN32
                std::wstring wfilename = entry.path().filename().wstring();
                int len = WideCharToMultiByte(CP_UTF8, 0, wfilename.c_str(), -1, nullptr, 0, nullptr, nullptr);
                if (len <= 0) continue;
                std::string filename(len, '\0');
                WideCharToMultiByte(CP_UTF8, 0, wfilename.c_str(), -1, filename.data(), len, nullptr, nullptr);
                filename.pop_back();

                std::wstring wext = entry.path().extension().wstring();
                len = WideCharToMultiByte(CP_UTF8, 0, wext.c_str(), -1, nullptr, 0, nullptr, nullptr);
                if (len <= 0) continue;
                std::string ext(len, '\0');
                WideCharToMultiByte(CP_UTF8, 0, wext.c_str(), -1, ext.data(), len, nullptr, nullptr);
                ext.pop_back();
#else
                std::string filename = entry.path().filename().string();
                std::string ext = entry.path().extension().string();
#endif
                if (ext == ".xml" || ext == ".XML")
                {
#ifdef _WIN32
                    std::wstring wpath = entry.path().wstring();
                    int len = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, nullptr, 0, nullptr, nullptr);
                    if (len <= 0) return "";
                    std::string utf8_path(len, '\0');
                    WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, utf8_path.data(), len, nullptr, nullptr);
                    utf8_path.pop_back();
                    return utf8_path;
#else
                    return entry.path().string();
#endif
                }
            }
        }
    }
    catch (const fs::filesystem_error& e)
    {
        std::cerr << "Filesystem error in FindFirstXMLFile: " << e.what() << std::endl;
    }
    return "";
}

std::string FindFDLInExtFolder(const char* folder, Stages mode)
{
    if (!folder || !*folder)
    {
        return "";
    }

    namespace fs = std::filesystem;

    auto to_lower = [](std::string s)
    {
        for (char& ch : s)
        {
            ch = static_cast<char>(::tolower(static_cast<unsigned char>(ch)));
        }
        return s;
    };

    auto find_in_dir = [&](const char* stage_tag) -> std::string
    {
        try
        {
#ifdef _WIN32
            fs::path dir = utf8_to_utf16(folder);
#else
            fs::path dir = folder;
#endif
            if (!fs::exists(dir) || !fs::is_directory(dir)) return "";

            std::string best_path;
            int best_score = 0;

            for (const auto& entry : fs::directory_iterator(dir))
            {
                if (!entry.is_regular_file()) continue;

                fs::path p = entry.path();
#ifdef _WIN32
                std::wstring wfilename = p.filename().wstring();
                int len = WideCharToMultiByte(CP_UTF8, 0, wfilename.c_str(), -1, nullptr, 0, nullptr, nullptr);
                if (len <= 0) continue;
                std::string filename(len, '\0');
                WideCharToMultiByte(CP_UTF8, 0, wfilename.c_str(), -1, filename.data(), len, nullptr, nullptr);
                filename.pop_back();
                std::wstring wext = p.extension().wstring();
                len = WideCharToMultiByte(CP_UTF8, 0, wext.c_str(), -1, nullptr, 0, nullptr, nullptr);
                if (len <= 0) continue;
                std::string ext(len, '\0');
                WideCharToMultiByte(CP_UTF8, 0, wext.c_str(), -1, ext.data(), len, nullptr, nullptr);
                ext.pop_back();
#else
                std::string filename = p.filename().string();
                std::string ext = p.extension().string();
#endif

                int score = 0;
                std::string tag(stage_tag);
                if (ext == "." + tag + "-sign")
                {
                    score = 3; // strongest match: .fdl1-sign / .fdl2-sign
                }
                else if (ext == "." + tag)
                {
                    score = 2; // .fdl1 / .fdl2
                }
                else if (filename.find(tag) != std::string::npos)
                {
                    score = 1; // filename contains "fdl1" or "fdl2"
                }

                if (score > best_score)
                {
                    best_score = score;
#ifdef _WIN32
                    std::wstring wpath = p.wstring();
                    int len = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, nullptr, 0, nullptr, nullptr);
                    if (len > 0) {
                        std::string utf8_path(len, '\0');
                        WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, utf8_path.data(), len, nullptr, nullptr);
                        utf8_path.pop_back();
                        best_path = utf8_path;
                    } else {
                        best_path.clear();
                    }
#else
                    best_path = p.string();
#endif
                }
            }

            return best_path;
        }
        catch (const fs::filesystem_error& e)
        {
            std::cerr << "File system error in FindFDLInExtFloder: " << e.what() << std::endl;
            return "";
        }
    };

    switch (mode)
    {
    case FDL1:
        return find_in_dir("fdl1");
    case FDL2:
        return find_in_dir("fdl2");
    default:
        return "";
    }
}

bool pac_extract(const char* fn, const char* floder)
{
    auto *pacptable = NEWN partition_t[128];
    if (!pacptable) ERR_EXIT("Failed to allocate memory for partition table.\n");
    int pac_part_count = 0;
    Unpac unpac;
    unpac.setDirectory(floder);
    if (!unpac.openPacFile(fn))
    {
        DEG_LOG(E, "Failed to open PAC file.\n");
        if (isHelperInit)
        {
            gui_idle_call_wait_drag([]()
            {
                showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("Failed to open PAC file."));
            },GTK_WINDOW(helper.getWidget("main_window")));
        }
        return false;
    }
    unpac.setFilter(0, NULL);
    if (!unpac.extractFiles())
    {
        DEG_LOG(E, "Failed to extract files from PAC file.\n");
        if (isHelperInit)
        {
            gui_idle_call_wait_drag([]()
            {
                showErrorDialog(
                    GTK_WINDOW(helper.getWidget("main_window")), _("Error"),
                    _("Failed to extract files from PAC file."));
            },GTK_WINDOW(helper.getWidget("main_window")));
        }
        return false;
    }
    unpac.listFiles();
    unpac.close();
    std::string xmlPath = FindFirstXMLFile(floder);
    if (xmlPath.empty())
    {
        if (isHelperInit)
            gui_idle_call_wait_drag([]()
            {
                showErrorDialog(
                    GTK_WINDOW(helper.getWidget("main_window")), _("Error"),
                    _("No XML file found in the extracted folder."));
            },GTK_WINDOW(helper.getWidget("main_window")));
        DEG_LOG(E, "No XML file found in the extracted folder.");
        return false;
    }
    EnhancedFile file = oxfopen_enhanced(xmlPath.c_str(), "r");
    if (!file) { DEG_LOG(E, "Failed to open xml for reading"); return false;}
    std::string content;
    content = file.read_all_chunked();
    std::string partxml = ExtractPartitionsWithTags(content);
    if (partxml.empty())
    {
        if (isHelperInit)
            gui_idle_call_wait_drag([]()
            {
                showErrorDialog(
                    GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("No partition info found in xml"));
            },GTK_WINDOW(helper.getWidget("main_window")));
        DEG_LOG(E, "No partition info found in xml");
        return false;
    }
    EnhancedFile fi = oxfopen_enhanced("partitions_temp.xml", "w");
    if (fi)
    {
        fi << partxml;
        fi.close();
    }
    else
    {
        DEG_LOG(E, "Failed to create temporary partitions XML file.");
        ERR_EXIT("Failed to create temporary partitions XML file.");
    }

    auto r = parse_partitions_xml_result("partitions_temp.xml", pacptable, &pac_part_count);
    if (!r)
    {
        // parse_partitions_xml_result 已经处理了日志和 GUI 提示，这里维持返回 false 即可
        if (pacptable) delete[] pacptable;
        return false;
    }

    if (isHelperInit)
    {
        const std::vector<partition_t>& partitions = std::vector<partition_t>(pacptable, pacptable + pac_part_count);
        // 获取列表视图
        GtkWidget* part_list = helper.getWidget("pac_list");
        if (!part_list || !GTK_IS_TREE_VIEW(part_list))
        {
            std::cerr << "pac_list not found or not a TreeView" << std::endl;
            if (isHelperInit)
                gui_idle_call_wait_drag([]()
                {
                    showErrorDialog(
                        GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("Partition list view not found."));
                },GTK_WINDOW(helper.getWidget("main_window")));
            delete[] pacptable;
            return false;
        }

        // 获取列表存储模型
        GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(part_list));
        if (!model)
        {
            std::cerr << "TreeView model not found" << std::endl;
            if (isHelperInit)
                gui_idle_call_wait_drag([]()
                {
                    showErrorDialog(
                        GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("Partition list model not found."));
                },GTK_WINDOW(helper.getWidget("main_window")));
            delete[] pacptable;
            return false;
        }

        // 清空现有数据
        GtkListStore* store = GTK_LIST_STORE(model);
        gtk_list_store_clear(store);

        // 添加分区数据
        int index = 1;
        GtkTreeIter iter_spl;
        gtk_list_store_append(store, &iter_spl);
        long long spl_size = g_spl_size > 0 ? g_spl_size : 0;
        std::string display_name = std::to_string(index) + ". splloader";
        std::string size_str;

        size_str = "DEFAULT";

        gtk_list_store_set(store, &iter_spl,
                           0, TRUE,
                           1, display_name.c_str(), // 显示名称（带序号）
                           2, size_str.c_str(), // 格式化的大小
                           3, "splloader", // 原始分区名
                           -1);

        index++; // 递增序号
        for (const auto& partition : partitions)
        {
            GtkTreeIter iter;
            std::string size_str;
            gtk_list_store_append(store, &iter);
            // 格式化显示文本
            std::string display_name = std::to_string(index) + ". " + partition.name;

            if (strcmp(partition.name, "userdata") != 0)
            {
                // 格式化大小显示
                if (partition.size < 1024)
                {
                    size_str = std::to_string(partition.size) + " B";
                }
                else if (partition.size < 1024 * 1024)
                {
                    size_str = std::to_string(partition.size / 1024) + " KB";
                }
                else if (partition.size < 1024 * 1024 * 1024)
                {
                    size_str = std::to_string(partition.size / (1024 * 1024)) + " MB";
                }
                else
                {
                    size_str = std::to_string(partition.size / (1024 * 1024 * 1024.0)) + " GB";
                }
            }
            else
            {
                size_str = "DEFAULT";
            }


            // 设置行数据
            gtk_list_store_set(store, &iter,
                               0, TRUE,
                               1, display_name.c_str(), // 显示名称（带序号）
                               2, size_str.c_str(), // 格式化的大小
                               3, partition.name, // 原始分区名（隐藏列，可选）
                               -1);

            index++;
        }

        // 更新显示
        gtk_widget_queue_draw(part_list);
    }
    if (pacptable) delete[] pacptable;
    return true;
}

bool pac_flash(spdio_t* io, const char* folder)
{
    if (g_app_state.device.device_stage != FDL2) {DEG_LOG(E, "Device is not in FDL2 stage!"); return false;}
    std::string xmlPath = FindFirstXMLFile(folder);
    if (xmlPath.empty())
    {
        if (isHelperInit)
            gui_idle_call_wait_drag([]()
            {
                showErrorDialog(
                    GTK_WINDOW(helper.getWidget("main_window")), _("Error"),
                    _("No XML file found in the extracted folder."));
            },GTK_WINDOW(helper.getWidget("main_window")));
        DEG_LOG(E, "No XML file found in the extracted folder.");
        return false;
    }
    g_app_state.flash.pac_xmlPath = xmlPath;

    auto into_func = [io, xmlPath]() mutable
    {
        get_partition_info(io, "nr_fixnv1", 1);
        if (gPartInfo.size)
        {
            g_app_state.pac.nr_fixnv1_mem = dump_partition_to_mem(io, gPartInfo.name, 0, gPartInfo.size, blk_size, &g_app_state.pac.nr_fixnv1_mem_size);
        }
        get_partition_info(io, "l_fixnv1", 1);
        if (gPartInfo.size)
        {
            g_app_state.pac.l_fixnv1_mem = dump_partition_to_mem(io, gPartInfo.name, 0, gPartInfo.size, blk_size, &g_app_state.pac.l_fixnv1_mem_size);
        }
        get_partition_info(io, "downloadnv", 1);
        if (gPartInfo.size)
        {
            g_app_state.pac.downloadnv_mem = dump_partition_to_mem(io, gPartInfo.name, 0, gPartInfo.size, blk_size, &g_app_state.pac.downloadnv_mem_size);
        }
        bool i_is = false;
        if (isHelperInit)
        {
            i_is = showConfirmDialogSyncInThread(
                GTK_WINDOW(helper.getWidget("main_window")), _("Confirm"), _("Do you want to repartition?"));
        }
        else
        {
            std::cout << "Do you want to repartition? (Y/n): ";
            char response;
            std::cin >> response;
            i_is = (response == 'y' || response == 'Y');
        }
        if (i_is)
        {
            EnhancedFile file = oxfopen_enhanced(xmlPath.c_str(), "r");
            if (!file) ERR_EXIT("Failed to open file for reading");
            std::string content;
            content = file.read_all_chunked();
            file.close();
            std::string partxml = ExtractPartitionsWithTags(content);
            uint8_t* buf = io->temp_buf;
            int n = scan_xml_partitions_from_string(io, partxml, buf, 0xffff);
            encode_msg_nocpy(io, BSL_CMD_REPARTITION, n * 0x4c);
            if (!send_and_check(io)) g_app_state.flash.gpt_failed = 0;
        }
        g_app_state.flash.isPacFlashing = true;

        load_partitions(io, "pac_unpack_output", blk_size, g_app_state.flash.selected_ab, 0);
        encode_msg_nocpy(io, BSL_CMD_NORMAL_RESET, 0);
        if (!send_and_check(io))
        {
            if (isHelperInit)
                gui_idle_call_wait_drag([]()
                {
                    showInfoDialog(
                        GTK_WINDOW(helper.getWidget("main_window")), _("Success"),
                        _("PAC flashed successfully, the program will be exited in 5 seconds..."));
                }, GTK_WINDOW(helper.getWidget("main_window")));
            DEG_LOG(I, "PAC flashed successfully, the program will be exited in 5 seconds...");
        }
#ifndef _WIN32
        sleep(5);
#else
        Sleep(5000);
#endif
        spdio_free(io);
        exit(0);
    };
    if (isHelperInit)
    {
        std::thread flash_thread(into_func);
        flash_thread.detach();
        return true;
    }
    else
    {
        into_func();
        return true;
    }
}
