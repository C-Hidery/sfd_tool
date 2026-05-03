#include "pac_extract.h"
#include "../i18n.h"
#include "logging.h"  // 使用统一的 ERR_EXIT
#include "app_state.h"
#include "../common.h"
#include "../main.h"
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
    #define read _read
    #define open _open
    #define close _close
    #define lseek _lseek
    #define O_RDONLY _O_RDONLY
    #define O_WRONLY _O_WRONLY
    #define O_RDWR   _O_RDWR
    #define O_CREAT  _O_CREAT
    #define O_TRUNC  _O_TRUNC
    #define O_BINARY _O_BINARY
    #define ssize_t int
#else
    #include <unistd.h>
#endif
#include <filesystem>  // C++17 filesystem
#include <iostream>    // for error output

#include "logging.h"  // 使用统一的 ERR_EXIT
#include "result.h"   // T2-02: Result/ErrorCode

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
    uint32_t type; // 0 - operation, 1 - file, 2 - xml, 0x101 - fdl
    uint32_t flash_use; // 1 - used during flashing process
    uint32_t pac_offset;
    uint32_t omit_flag;
    uint32_t addr_num;
    uint32_t addr[5];
    uint32_t unknown2[249];
} sprd_file_t;

static unsigned u_crc16(uint32_t crc, const void *src, unsigned len) {
    uint8_t *s = (uint8_t*)src; int i;
    while (len--) {
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

enum {
    MODE_NONE = 0,
    MODE_LIST,
    MODE_EXTRACT,
    MODE_CHECK
};

static size_t u16_to_u8(char *d, size_t dn, const uint16_t *s, size_t sn) {
    size_t i = 0, j = 0; unsigned a;
    if (!d) dn = 0;
    while (i < sn) {
        a = s[i++];
        if (!a) break;
        if ((a - 0x20) >= 0x5f) a = '?';
        if (j + 1 < dn) d[j++] = a;
    }
    if (dn) d[j] = 0;
    return i;
}

static int compare_u8_u16(int depth, char *d, const uint16_t *s, size_t sn) {
    size_t i = 0; int a, b;
    if (depth > 10) ERR_EXIT("use less wildcards\n");
    for (;;) {
        a = *d++;
        if (a == '*') goto wildcard;
        b = i < sn ? s[i++] : 0;
        if (a == '?') {
            if (!b) return 1;
        } else {
            if (a != b) return 1;
            if (!a) break;
        }
    }
    return 0;

wildcard:
    for (;;) {
        if (!compare_u8_u16(depth + 1, d, s + i, sn - i)) return 0;
        b = i < sn ? s[i++] : 0;
        if (!b) break;
    }
    return 1;
}

static int check_path(char *path) {
    char *s = path; int a;
    for (; (a = *s); s++) {
        if (a == '/' || a == '\\' || a == ':') return -1;
    }
    return s - path;
}

class Unpac {
private:
    FILE* fi;
    sprd_head_t head;
    char str_buf[257];
    unsigned chunk;
    const char* dir;
    uint8_t* buf;
    int argc;
    char** argv;
    std::filesystem::path orig_dir;

public:
    Unpac() : fi(NULL), chunk(0x1000), dir(NULL), buf(NULL), argc(0), argv(NULL), orig_dir(std::filesystem::current_path()) {
        memset(&head, 0, sizeof(head));
        memset(str_buf, 0, sizeof(str_buf));
    }

    ~Unpac() {
        if (buf) free(buf);
        if (fi) fclose(fi);
    }

    void setDirectory(const char* directory) {
        dir = directory;
        orig_dir = std::filesystem::current_path();
    }

    // 新增：检查和准备输出目录的函数
    void prepareOutputDirectory() {
        if (!dir) {
            ERR_EXIT("Error: Output directory not set\n");
        }

        namespace fs = std::filesystem;
        fs::path outputPath(dir);

        try {
            if (fs::exists(outputPath)) {
                // 如果目录存在，删除它及其所有内容
                fs::remove_all(outputPath);
                printf("Removed existing directory: %s\n", dir);
            }

            // 创建新目录
            if (fs::create_directories(outputPath)) {
                printf("Created directory: %s\n", dir);
            } else {
                ERR_EXIT("Failed to create directory: %s\n", dir);
            }
        } catch (const fs::filesystem_error& e) {
            ERR_EXIT("Filesystem error: %s\n", e.what());
        }
    }

    bool openPacFile(const char* filename) {
        fi = fopen(filename, "rb");
        if (!fi) {
            printf("fopen(input) failed\n");
            return false;
        }

        READ1(head);
        if (head.pac_magic != ~0x50005u) {
            printf("bad pac_magic\n");
            return false;
        }

        if (head.dir_offset != sizeof(head)) {
            printf("unexpected directory offset\n");
            return false;
        }

        if (head.file_count >> 10) {
            printf("too many files\n");
            return false;
        }

        return true;
    }

    void setFilter(int filter_argc, char** filter_argv) {
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

// 这里保留原有的 listFiles / extractFiles / checkCrc / close 实现
// ......

bool Unpac::listFiles() {
    if (!fi) { printf("PAC file not opened\n"); return false; }

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
    for (unsigned i = 0; i < head.file_count; i++) {
        sprd_file_t file; int j;
        READ1(file);
        if (file.struct_size != sizeof(sprd_file_t)) {
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
            for (j = 0; j < (int)file.addr_num; j++) {
                if (!file.addr[j]) continue;
                if (!j) printf(", addr = 0x%x", file.addr[j]);
                else printf(", addr%u = 0x%x", j, file.addr[j]);
            }

        if (file.id[0]) {
            CONV_STR(file.id);
            printf(", id = \"%s\"", str_buf);
        }
        if (file.name[0]) {
            CONV_STR(file.name);
            printf(", name = \"%s\"", str_buf);
        }
        printf("\n");
    }
    return true;
}

bool Unpac::extractFiles() {
    if (!fi) { printf("PAC file not opened\n"); return false; }

    // 在提取文件前准备输出目录
    prepareOutputDirectory();

#ifndef _WIN32
    if (dir && chdir(dir)) {
        printf("chdir failed\n");
        return false;
    }
#else
    if (dir) {
        if (_chdir(dir)) {
            printf("chdir failed\n");
            return false;
        }
    }
#endif

    for (unsigned i = 0; i < head.file_count; i++) {
        sprd_file_t file; int j;
        long long file_size, pac_offset;
        READ1(file);
        if (file.struct_size != sizeof(sprd_file_t)) {
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

        FILE* fo; uint64_t l; uint32_t n;

        CONV_STR(file.name);
        printf("%s\n", str_buf);

        if (fseeko(fi, pac_offset, SEEK_SET)) {
            printf("fseek failed\n");
            return false;
        }

        if (check_path(str_buf) < 1) {
            printf("!!! unsafe filename detected\n");
            continue;
        }

        if (!buf) {
            buf = (uint8_t*) malloc(chunk);
            if (!buf) {
                printf("malloc failed\n");
                return false;
            }
        }

        fo = fopen(str_buf, "wb");
        if (!fo) {
            printf("fopen(output) failed\n");
            return false;
        }

        l = file_size;
        for (; l; l -= n) {
            n = (uint32_t)(l > chunk ? chunk : l);
            READ(buf, n, "chunk");
            fwrite(buf, n, 1, fo);
        }
        fclose(fo);

        if (fseek(fi, sizeof(head) + (i + 1) * sizeof(sprd_file_t), SEEK_SET)) {
            printf("fseek failed\n");
            return false;
        }
    }
    const std::string path_str = orig_dir.string();
    const char* path = path_str.c_str();
#ifndef _WIN32
    if (path && chdir(path)) {
        printf("chdir failed\n");
        return false;
    }
#else
    if (path) {
        if (_chdir(path)) {
            printf("chdir failed\n");
            return false;
        }
    }
#endif
    return true;
}

bool Unpac::checkCrc() {
    auto r = checkCrc_result();
    return static_cast<bool>(r);
}

sfd::Result<void> Unpac::checkCrc_result() {
    if (!fi) {
        printf("PAC file not opened\n");
        return sfd::Result<void>::error(sfd::ErrorCode::InvalidArgument, "pac file not opened");
    }

    // Head CRC
    uint32_t head_crc = u_crc16(0, &head, sizeof(head) - 4);
    printf("head_crc: 0x%04x", head.head_crc);
    bool head_mismatch = false;
    if (head.head_crc != head_crc) {
        printf(" (expected 0x%04x)", head_crc);
        head_mismatch = true;
    }
    printf("\n");

    // Data CRC
    uint32_t l = head.pac_size;
    if (l < sizeof(head)) {
        printf("unexpected pac size\n");
        return sfd::Result<void>::error(sfd::ErrorCode::ParseError, "unexpected pac size");
    }

    if (fseeko(fi, sizeof(head), SEEK_SET)) {
        printf("fseeko failed in checkCrc\n");
        return sfd::Result<void>::error(sfd::ErrorCode::IoError, "fseeko failed in checkCrc");
    }

    uint32_t data_crc = 0;
    uint32_t chunk_size = chunk ? chunk : 0x1000;
    uint8_t* local_buf = (uint8_t*) malloc(chunk_size);
    if (!local_buf) {
        printf("malloc failed\n");
        return sfd::Result<void>::error(sfd::ErrorCode::InternalError, "malloc failed in checkCrc");
    }

    l -= sizeof(head);
    while (l) {
        uint32_t n = l > chunk_size ? chunk_size : l;
        size_t read_count = fread(local_buf, 1, n, fi);
        if (read_count != n) {
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
    if (head.data_crc != data_crc) {
        printf(" (expected 0x%04x)", data_crc);
        data_mismatch = true;
    }
    printf("\n");

    if (head_mismatch || data_mismatch) {
        if (head_mismatch && data_mismatch) {
            return sfd::Result<void>::error(sfd::ErrorCode::ParseError,
                                            "PAC CRC mismatch (head and data)");
        } else if (head_mismatch) {
            return sfd::Result<void>::error(sfd::ErrorCode::ParseError,
                                            "PAC CRC mismatch (head)");
        } else {
            return sfd::Result<void>::error(sfd::ErrorCode::ParseError,
                                            "PAC CRC mismatch (data)");
        }
    }

    return sfd::Result<void>::ok();
}

void Unpac::close() {
    if (buf) {
        free(buf);
        buf = NULL;
    }
    if (fi) {
        fclose(fi);
        fi = NULL;
    }
}
static sfd::Result<void> parse_partitions_xml_result(const char* temp_xml_path,
                                                     partition_t* pacptable,
                                                     int& pac_part_count) {
    // 1. 解析 XML 文件
    XmlParser parser;
    auto root = parser.parseFile(temp_xml_path);
    if (!root) {
        DEG_LOG(E, "Failed to parse XML file\n");
        if (isHelperInit) gui_idle_call_wait_drag([](){
            showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", _("Failed to parse XML file."));
        }, GTK_WINDOW(helper.getWidget("main_window")));
        return sfd::Result<void>::error(sfd::ErrorCode::IoError, "loadfile failed");
    }

    // 2. 查找 <Partitions> 节点（必须唯一，对应原 stage 检测）
    auto partitionsNodes = root->getDescendants("Partitions");
    if (partitionsNodes.empty()) {
        DEG_LOG(E, "No Partitions element found\n");
        if (isHelperInit) gui_idle_call_wait_drag([](){
            showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", _("No <Partitions> element in XML."));
        }, GTK_WINDOW(helper.getWidget("main_window")));
        return sfd::Result<void>::error(sfd::ErrorCode::ParseError, "xml: no Partitions");
    }
    if (partitionsNodes.size() > 1) {
        DEG_LOG(E, "More than one partition lists\n");
        if (isHelperInit) gui_idle_call_wait_drag([](){
            showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", _("More than one partition list found in XML file."));
        }, GTK_WINDOW(helper.getWidget("main_window")));
        return sfd::Result<void>::error(sfd::ErrorCode::ParseError, "xml: more than one partition lists");
    }

    auto partitions = partitionsNodes[0];
    // 获取所有直接子节点 <Partition>（原函数只处理一级子节点）
    auto partitionNodes = partitions->getChildren("Partition");
    if (partitionNodes.empty()) {
        DEG_LOG(E, "No Partition elements inside Partitions\n");
        if (isHelperInit) gui_idle_call_wait_drag([](){
            showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", _("No <Partition> elements inside <Partitions>."));
        }, GTK_WINDOW(helper.getWidget("main_window")));
        return sfd::Result<void>::error(sfd::ErrorCode::ParseError, "xml: no Partition elements");
    }

    // 3. 准备 buf（与原函数一致）
    uint32_t buf_size = 0xffff;
    uint8_t* buf_orig = NEWN uint8_t[0x4c * 128];
    uint8_t* buf = buf_orig;
    int found = 0;

    // 4. 遍历每个 Partition，提取 id 和 size 属性
    for (auto& partNode : partitionNodes) {
        // 提取 id 属性（原函数通过 sscanf 读取 name）
        std::string id;
        auto it_id = partNode->attributes.find("id");
        if (it_id != partNode->attributes.end())
            id = it_id->second;
        if (id.empty()) {
            DEG_LOG(E, "Partition missing id attribute\n");
            if (isHelperInit) gui_idle_call_wait_drag([](){
                showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", _("Partition element missing 'id' attribute."));
            }, GTK_WINDOW(helper.getWidget("main_window")));
            delete[] buf_orig;
            return sfd::Result<void>::error(sfd::ErrorCode::ParseError, "xml: missing id");
        }

        // 提取 size 属性，支持十进制和十六进制（如 0xffffffff）
        std::string sizeStr;
        auto it_size = partNode->attributes.find("size");
        if (it_size != partNode->attributes.end())
            sizeStr = it_size->second;
        if (sizeStr.empty()) {
            DEG_LOG(E, "Partition missing size attribute\n");
            if (isHelperInit) gui_idle_call_wait_drag([](){
                showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", _("Partition element missing 'size' attribute."));
            }, GTK_WINDOW(helper.getWidget("main_window")));
            delete[] buf_orig;
            return sfd::Result<void>::error(sfd::ErrorCode::ParseError, "xml: missing size");
        }

        char* endptr;
        long long size = strtoll(sizeStr.c_str(), &endptr, 0);   // base=0 自动识别 0x 前缀
        if (*endptr != '\0' && !isspace(static_cast<unsigned char>(*endptr))) {
            DEG_LOG(E, "Invalid size value\n");
            if (isHelperInit) gui_idle_call_wait_drag([](){
                showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", _("Invalid size value in Partition."));
            }, GTK_WINDOW(helper.getWidget("main_window")));
            delete[] buf_orig;
            return sfd::Result<void>::error(sfd::ErrorCode::ParseError, "xml: bad size");
        }

        // 检查剩余缓冲区容量（每个分区固定 0x4c 字节）
        if (buf_size < 0x4c) {
            DEG_LOG(E, "Too many partitions\n");
            if (isHelperInit) gui_idle_call_wait_drag([](){
                showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", _("Too many partitions in XML file."));
            }, GTK_WINDOW(helper.getWidget("main_window")));
            delete[] buf_orig;
            return sfd::Result<void>::error(sfd::ErrorCode::ParseError, "xml: too many partitions");
        }
        buf_size -= 0x4c;

        // 填充名称区域（36 个宽字符，共 72 字节），每个字符的低字节存 ASCII，高字节清零
        memset(buf, 0, 36 * 2);
        for (size_t i = 0; i < id.size() && i < 36; ++i) {
            buf[i * 2] = static_cast<uint8_t>(id[i]);
        }
        if (id.empty()) {
            DEG_LOG(E, "Empty partition name\n");
            if (isHelperInit) gui_idle_call_wait_drag([](){
                showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", _("Empty partition name found in XML file."));
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

    pac_part_count = found;
    delete[] buf_orig;
    return sfd::Result<void>::ok();
}

std::string ExtractPartitionsWithTags(const std::string& xmlContent) {
    XmlParser parser;
    auto root = parser.parseString(xmlContent);
    if (!root) return "";
    auto partitions = root->getFirstDescendant("Partitions");
    if (!partitions) return "";
    return partitions->toXml();   // 直接调用 toXml()
}
std::string FindFirstXMLFile(const std::string& folderPath) {
	namespace fs = std::filesystem;
    try {
        if (!fs::exists(folderPath)) {
            std::cerr << "Floder not found: " << folderPath << std::endl;
            return "";
        }
        
        for (const auto& entry : fs::directory_iterator(folderPath)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                // 检查扩展名是否为.xml（不区分大小写）
                std::string ext = entry.path().extension().string();
                if (ext == ".xml" || ext == ".XML") {
                    return entry.path().string(); // 返回完整路径
                }
            }
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "File system error: " << e.what() << std::endl;
    }
    
    return ""; // 没找到
}
std::string FindFDLInExtFloder(const char* folder, Stages mode)
{
    if (!folder || !*folder) {
        return "";
    }

    namespace fs = std::filesystem;

    auto to_lower = [](std::string s) {
        for (char& ch : s) {
            ch = static_cast<char>(::tolower(static_cast<unsigned char>(ch)));
        }
        return s;
    };

    auto find_in_dir = [&](const char* stage_tag) -> std::string {
        try {
            fs::path dir(folder);
            if (!fs::exists(dir) || !fs::is_directory(dir)) {
                std::cerr << "Folder not found or not directory: " << folder << std::endl;
                return "";
            }

            std::string best_path;
            int best_score = 0;

            for (const auto& entry : fs::directory_iterator(dir)) {
                if (!entry.is_regular_file()) {
                    continue;
                }

                fs::path p = entry.path();
                std::string filename = to_lower(p.filename().string());
                std::string ext = to_lower(p.extension().string());

                int score = 0;
                std::string tag(stage_tag);
                if (ext == "." + tag + "-sign") {
                    score = 3;               // strongest match: .fdl1-sign / .fdl2-sign
                } else if (ext == "." + tag) {
                    score = 2;               // .fdl1 / .fdl2
                } else if (filename.find(tag) != std::string::npos) {
                    score = 1;               // filename contains "fdl1" or "fdl2"
                }

                if (score > best_score) {
                    best_score = score;
                    best_path = p.string();
                }
            }

            return best_path;
        } catch (const fs::filesystem_error& e) {
            std::cerr << "File system error in FindFDLInExtFloder: " << e.what() << std::endl;
            return "";
        }
    };

    switch (mode) {
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
	int pac_part_count;
	Unpac unpac;
	unpac.setDirectory(floder);
	if(!unpac.openPacFile(fn)) {
		DEG_LOG(E,"Failed to open PAC file.\n");
		if(isHelperInit) {
			gui_idle_call_wait_drag([](){
				showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("Failed to open PAC file."));
			},GTK_WINDOW(helper.getWidget("main_window")));
		}
		return false;
	}
	unpac.setFilter(0, NULL);
	if(!unpac.extractFiles()) {
		DEG_LOG(E,"Failed to extract files from PAC file.\n");
		if(isHelperInit) {
			gui_idle_call_wait_drag([](){
				showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("Failed to extract files from PAC file."));
			},GTK_WINDOW(helper.getWidget("main_window")));
		}
		return false;
	}
	unpac.listFiles();
	unpac.close();
	std::string xmlPath = FindFirstXMLFile(floder);
	if(xmlPath.empty()) {
		if(isHelperInit) gui_idle_call_wait_drag([](){
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("No XML file found in the extracted folder."));
		},GTK_WINDOW(helper.getWidget("main_window")));
		DEG_LOG(E, "No XML file found in the extracted folder.");
		return false;
	}
	std::ifstream file(xmlPath);
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    std::string partxml = ExtractPartitionsWithTags(content);
    if (partxml.empty())
    {
        if(isHelperInit) gui_idle_call_wait_drag([](){
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("No partition info found in xml"));
		},GTK_WINDOW(helper.getWidget("main_window")));
		DEG_LOG(E, "No partition info found in xml");
		return false;
    }
	FILE* fi = oxfopen("partitions_temp.xml","w");
	if(fi) {
		fwrite(partxml.c_str(), 1, partxml.size(), fi);
		fclose(fi);
	}
	else {
		DEG_LOG(E, "Failed to create temporary partitions XML file.");
		ERR_EXIT("Failed to create temporary partitions XML file.");
	}
	partition_t* pacptable = NEWN partition_t[128];
	pac_part_count = 0;
	{
		auto r = parse_partitions_xml_result("partitions_temp.xml", pacptable, pac_part_count);
		if (!r) {
			// parse_partitions_xml_result 已经处理了日志和 GUI 提示，这里维持返回 false 即可
			delete[] pacptable;
			return false;
		}
	}
    if (isHelperInit)
    {
        const std::vector<partition_t>& partitions = std::vector<partition_t>(pacptable, pacptable + pac_part_count);
        // 获取列表视图
        GtkWidget* part_list = helper.getWidget("pac_list");
        if (!part_list || !GTK_IS_TREE_VIEW(part_list)) {
            std::cerr << "pac_list not found or not a TreeView" << std::endl;
            if(isHelperInit) gui_idle_call_wait_drag([](){
                showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("Partition list view not found."));
            },GTK_WINDOW(helper.getWidget("main_window")));
            delete[] pacptable;
            return false;
        }

        // 获取列表存储模型
        GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(part_list));
        if (!model) {
            std::cerr << "TreeView model not found" << std::endl;
            if(isHelperInit) gui_idle_call_wait_drag([](){
                showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("Partition list model not found."));
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
                        1, display_name.c_str(),   // 显示名称（带序号）
                        2, size_str.c_str(),       // 格式化的大小
                        3, "splloader",            // 原始分区名
                        -1);

        index++;  // 递增序号
        for (const auto& partition : partitions) {
            GtkTreeIter iter;
            std::string size_str;
            gtk_list_store_append(store, &iter);
            // 格式化显示文本
            std::string display_name = std::to_string(index) + ". " + partition.name;

            if (strcmp(partition.name, "userdata") != 0) 
            {
                // 格式化大小显示
                if (partition.size < 1024) {
                    size_str = std::to_string(partition.size) + " B";
                } else if (partition.size < 1024 * 1024) {
                    size_str = std::to_string(partition.size / 1024) + " KB";
                } else if (partition.size < 1024 * 1024 * 1024) {
                    size_str = std::to_string(partition.size / (1024 * 1024)) + " MB";
                } else {
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
                            1, display_name.c_str(),  // 显示名称（带序号）
                            2, size_str.c_str(),      // 格式化的大小
                            3, partition.name,        // 原始分区名（隐藏列，可选）
                            -1);

            index++;
        }

        // 更新显示
        gtk_widget_queue_draw(part_list);
    }
	
	delete[] pacptable;

    return true;
}

// 查找文件中第一个出现的 <ID>xxx</ID> 对应的 <Base>
std::string findBaseForID(const std::string& filename, const std::string& targetID) {
    // 读取文件
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();
    
    XmlParser parser;
    auto root = parser.parseString(buffer.str());
    if (!root) return "";
    
    // 查找所有 <File> 节点
    auto files = root->getDescendants("File");
    for (auto& fileNode : files) {
        // 查找文件内的 ID 或 IDAlias
        auto idNode = fileNode->getFirstChild("ID");
        std::string idValue;
        if (idNode) {
            idValue = idNode->getTextContent();
        } else {
            auto aliasNode = fileNode->getFirstChild("IDAlias");
            if (aliasNode) idValue = aliasNode->getTextContent();
        }
        
        if (idValue == targetID) {
            // 查找 Block 内的 Base
            auto blockNode = fileNode->getFirstChild("Block");
            if (blockNode) {
                auto baseNode = blockNode->getFirstChild("Base");
                if (baseNode) {
                    return baseNode->getTextContent();
                }
            }
        }
    }
    
    return "";
}
bool pac_flash(spdio_t* io, const char* floder)
{
    std::string xmlPath = FindFirstXMLFile(floder);
    if (xmlPath.empty()) {
        if(isHelperInit) gui_idle_call_wait_drag([](){
            showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("No XML file found in the extracted folder."));
        },GTK_WINDOW(helper.getWidget("main_window")));
        DEG_LOG(E, "No XML file found in the extracted folder.");
        return false;
    }
    g_app_state.flash.pac_xmlPath = xmlPath;
    std::string fdl1_path = FindFDLInExtFloder(floder, FDL1);
    std::string fdl2_path = FindFDLInExtFloder(floder, FDL2);
    std::string fdl1_base = findBaseForID(xmlPath, "fdl1");
    std::string fdl2_base = findBaseForID(xmlPath, "fdl2");
    if (fdl1_path.empty() || fdl1_base.empty()) {
        if(isHelperInit) gui_idle_call_wait_drag([](){
            showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("FDL1 file or base address not found."));
        },GTK_WINDOW(helper.getWidget("main_window")));
        DEG_LOG(E, "FDL1 file or base address not found.");
        return false;
    }
    if (fdl2_path.empty() || fdl2_base.empty()) {
        if(isHelperInit) gui_idle_call_wait_drag([](){
            showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("FDL2 file or base address not found."));
        },GTK_WINDOW(helper.getWidget("main_window")));
        DEG_LOG(E, "FDL2 file or base address not found.");
        return false;
    }
    if (isHelperInit && !showConfirmDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Confirm"), _("Are you sure you want to flash the device with the extracted files? Make sure you have the correct PAC file."))) {
        return false;
    }
    ensure_device_attached_or_exit(helper);
    
    if (isHelperInit) gui_idle_call_wait_drag([](){
        showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Info"), _("Start executing FDL1 and FDL2."));
    },GTK_WINDOW(helper.getWidget("main_window")));
    uint32_t fdl1_base_addr = std::stoul(fdl1_base, nullptr, 0);
    uint32_t fdl2_base_addr = std::stoul(fdl2_base, nullptr, 0);
    int highspeed = 0;
    uint32_t baudrate = 0;
    uint16_t blk_size = DEFAULT_BLK_SIZE;
    auto into_func = [=]() mutable
    {
                FILE* fi;
                fi = oxfopen(fdl1_path.c_str(), "r");
				if (fi == nullptr) {
					DEG_LOG(W, "File does not exist.\n");
					showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("File does not exist."));
                      return;
                } else fclose(fi);
					
				send_file(io, fdl1_path.c_str(), fdl1_base_addr, 0, 528, 0, 0);
				encode_msg_nocpy(io, BSL_CMD_EXEC_DATA, 0);
				if (send_and_check(io)) ERR_EXIT("FDL exec failed\n");
				
				DEG_LOG(OP, "Execute FDL1");
				// Tiger 310(0x5500) and Tiger 616(0x65000800) need to change baudrate after FDL1

				if (fdl1_base_addr == 0x5500 || fdl1_base_addr == 0x65000800) {
					highspeed = 1;
					if (!baudrate) baudrate = 921600;
				}

				/* FDL1 (chk = sum) */
				io->flags &= ~FLAGS_CRC16;

				encode_msg(io, BSL_CMD_CHECK_BAUD, nullptr, 1);
				for (int i = 0; ; i++) {
					send_msg(io);
					recv_msg(io);
					if (recv_type(io) == BSL_REP_VER) break;
					DEG_LOG(W, "Failed to check baud, retry...");
					if (i == 4) {
						ERR_EXIT("Can not execute FDL, please reboot your phone by pressing POWER and VOL_UP for 7-10 seconds.\n");
					}
					usleep(500000);
				}
				DEG_LOG(I, "Check baud FDL1 done.");

				DEG_LOG(I, "Device REP_Version: ");
				print_string(stderr, io->raw_buf + 4, READ16_BE(io->raw_buf + 2));


				encode_msg_nocpy(io, BSL_CMD_CONNECT, 0);
				if (send_and_check(io)) ERR_EXIT("FDL connect failed\n");
				DEG_LOG(I, "FDL1 connected.");
#if !USE_LIBUSB
				if (baudrate) {
					uint8_t* data = io->temp_buf;
					WRITE32_BE(data, baudrate);
					encode_msg_nocpy(io, BSL_CMD_CHANGE_BAUD, 4);
					if (!send_and_check(io)) {
						DEG_LOG(OP, "Change baud FDL1 to %d", baudrate);
						call_SetProperty(io->handle, 0, 100, (LPCVOID)&baudrate);
					}
				}
#endif
				
				encode_msg_nocpy(io, BSL_CMD_KEEP_CHARGE, 0);
				if (!send_and_check(io)) DEG_LOG(OP, "Keep charge FDL1.");
				
				fdl1_loaded = 1;
                g_app_state.device.device_stage = FDL1;
    // FDL2
                memset(&Da_Info, 0, sizeof(Da_Info));
				encode_msg_nocpy(io, BSL_CMD_EXEC_DATA, 0);
				send_msg(io);
				// Feature phones respond immediately,
				// but it may take a second for a smartphone to respond.
				int ret = recv_msg_timeout(io, 15000);
				if (!ret) {
					
					ERR_EXIT("timeout reached\n");
				}
				ret = recv_type(io);
				// Is it always bullshit?
				if (ret == BSL_REP_INCOMPATIBLE_PARTITION)
					get_Da_Info(io);
				else if (ret != BSL_REP_ACK) {
					
					const char* name = get_bsl_enum_name(ret);
					ERR_EXIT("excepted response (%s : 0x%04x)\n", name, ret);
				}
				DEG_LOG(OP, "Execute FDL2");
				//remove 0d detection for nand device
				//This is not supported on certain devices.
				/*
				encode_msg_nocpy(io, BSL_CMD_READ_FLASH_INFO, 0);
				send_msg(io);
				ret = recv_msg(io);
				if (ret) {
					ret = recv_type(io);
					if (ret != BSL_REP_READ_FLASH_INFO) DEG_LOG(E,"excepted response (0x%04x)\n", ret);
					else Da_Info.dwStorageType = 0x101;
					// need more samples to cover BSL_REP_READ_MCP_TYPE packet to nand_id/nand_info
					// for nand_id 0x15, packet is 00 9b 00 0c 00 00 00 00 00 02 00 00 00 00 08 00
				}
				*/
				if (Da_Info.bDisableHDLC) {
					encode_msg_nocpy(io, BSL_CMD_DISABLE_TRANSCODE, 0);
					if (!send_and_check(io)) {
						io->flags &= ~FLAGS_TRANSCODE;
						DEG_LOG(OP, "Try to disable transcode 0x7D.");
					}
				}
				int o = io->verbose;
				io->verbose = -1;
				g_spl_size = check_partition(io, "splloader", 1);
				io->verbose = o;
				if (Da_Info.bSupportRawData) {
					blk_size = 0xf800;
					io->ptable = partition_list(io, fn_partlist, &io->part_count);
					if (fdl2_executed) {
						Da_Info.bSupportRawData = 0;
						DEG_LOG(OP, "Raw data mode disabled for SPRD4.");
					} else {
						encode_msg_nocpy(io, BSL_CMD_ENABLE_RAW_DATA, 0);
						if (!send_and_check(io)) DEG_LOG(OP, "Raw data mode enabled.");
					}
				}


				else if (highspeed || Da_Info.dwStorageType == 0x103) {
					blk_size = 0xf800;
					io->ptable = partition_list(io, fn_partlist, &io->part_count);
				} else if (Da_Info.dwStorageType == 0x102) {
					io->ptable = partition_list(io, fn_partlist, &io->part_count);
				} else if (Da_Info.dwStorageType == 0x101) DEG_LOG(I, "Device storage is nand.");
				if (g_app_state.flash.gpt_failed != 1) {
					if (g_app_state.flash.selected_ab == 2) DEG_LOG(I, "Device is using slot b\n");
					else if (g_app_state.flash.selected_ab == 1) DEG_LOG(I, "Device is using slot a\n");
					else {
						DEG_LOG(I, "Device is not using VAB\n");
						if (Da_Info.bSupportRawData) {
							DEG_LOG(I, "Raw data mode is supported (level is %u) ,but DISABLED for stability, you can set it manually.", (unsigned)Da_Info.bSupportRawData);
							Da_Info.bSupportRawData = 0;
						}
					}
				}
				if (!io->part_count) {
					DEG_LOG(W, "No partition table found on current device");
				}
                int nand_id = DEFAULT_NAND_ID;
                uint8_t nand_info[3] = {0}; // page size, spare area size, block size
				if (nand_id == DEFAULT_NAND_ID) {
					nand_info[0] = (uint8_t)pow(2, nand_id & 3); //page size
					nand_info[1] = 32 / (uint8_t)pow(2, (nand_id >> 2) & 3); //spare area size
					nand_info[2] = 64 * (uint8_t)pow(2, (nand_id >> 4) & 3); //block size
				}
				fdl2_executed = 1;
				g_app_state.device.device_stage = FDL2;
    DEG_LOG(I, "Device is in FDL2 stage now, flash pac");
    get_partition_info(io, "nr_fixnv1", 1);
    if (gPartInfo.size)
    {
        dump_partition(io, gPartInfo.name, 0, gPartInfo.size, "old_nv_nr_fixnv1.bin",g_default_blk_size ? g_default_blk_size : DEFAULT_BLK_SIZE);
    }
    get_partition_info(io, "l_fixnv1", 1);
    if (gPartInfo.size)
    {
        dump_partition(io, gPartInfo.name, 0, gPartInfo.size, "old_nv_l_fixnv1.bin",g_default_blk_size ? g_default_blk_size : DEFAULT_BLK_SIZE);
    }
    get_partition_info(io, "downloadnv", 1);
    if (gPartInfo.size)
    {
        dump_partition(io, gPartInfo.name, 0, gPartInfo.size, "old_nv_downloadnv.bin",g_default_blk_size ? g_default_blk_size : DEFAULT_BLK_SIZE);
    }
    bool i_is = false;
    if (isHelperInit)
    {
        std::promise<bool> promise;
        auto future = promise.get_future();
        auto* promise_ptr = new std::promise<bool>(std::move(promise));
        gui_idle_call_wait_drag([promise_ptr]() {
            bool result = showConfirmDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Confirm"), _("Do you want to repartition?"));
            promise_ptr->set_value(result);
            delete promise_ptr;
        }, GTK_WINDOW(helper.getWidget("main_window")));
        i_is = future.get();
    }
    else
    {
        std::cout << "Do you want to repartition? (y/n): ";
        char response;
        std::cin >> response;
        i_is = (response == 'y' || response == 'Y');
    }
    if (i_is)
    {
        partition_t* repartition_table = NEWN partition_t[128];
        std::ifstream file(xmlPath);
        std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

        std::string partxml = ExtractPartitionsWithTags(content);
        FILE* f1 = oxfopen("repartition_xml_temp.xml", "w");
        if (!f1) ERR_EXIT("Failed to create temporary repartition XML file.\n");
        if(f1) {
		    fwrite(partxml.c_str(), 1, partxml.size(), f1);
		    fclose(f1);
	    }
        uint8_t* buf = io->temp_buf;
        int n = scan_xml_partitions(io, "repartition_xml_temp.xml", buf, 0xffff);
        encode_msg_nocpy(io, BSL_CMD_REPARTITION, n * 0x4c);
	    if (!send_and_check(io)) g_app_state.flash.gpt_failed = 0;
    }
    g_app_state.flash.isPacFlashing = true;
    
        
    load_partitions(io, "pac_unpack_output", blk_size, g_app_state.flash.selected_ab, 0);
    encode_msg_nocpy(io, BSL_CMD_NORMAL_RESET, 0);
    if (!send_and_check(io))
    {
        if (isHelperInit) gui_idle_call_wait_drag([]() {
			showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Success"), _("PAC flashed successfully, the program will be exited in 5 seconds..."));
		}, GTK_WINDOW(helper.getWidget("main_window")));
        DEG_LOG(I, "PAC flashed successfully, the program will be exited in 5 seconds...");
    }
#ifndef _WIN32
    sleep(5);
#else
    Sleep(5000);
#endif
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

