#include "pac_extract.h"
#include "logging.h"  // 使用统一的 ERR_EXIT
#include "app_state.h"
#include "../common.h"
#define _GNU_SOURCE 1
#define _FILE_OFFSET_BITS 64

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
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

public:
    Unpac() : fi(NULL), chunk(0x1000), dir(NULL), buf(NULL), argc(0), argv(NULL) {
        memset(&head, 0, sizeof(head));
        memset(str_buf, 0, sizeof(str_buf));
    }

    ~Unpac() {
        if (buf) free(buf);
        if (fi) fclose(fi);
    }

    void setDirectory(const char* directory) {
        dir = directory;
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
	const char *part1 = "Partitions>";
	char *src, *p; size_t fsize = 0;
	int part1_len = strlen(part1), found = 0, stage = 0;

	src = (char *)loadfile(temp_xml_path, &fsize, 1);
	if (!src) {
		ERR_EXIT("loadfile failed\n");
		return sfd::Result<void>::error(sfd::ErrorCode::IoError, "loadfile failed");
	}
	src[fsize] = 0;
	p = src;

	uint32_t buf_size = 0xffff;
	uint8_t* buf = NEWN uint8_t[0x4c * 128];

	for (;;) {
		int i, a = *p++, n; char c; long long size;
		if (a == ' ' || a == '\t' || a == '\n' || a == '\r') continue;
		if (a != '<') {
			if (!a) break;
			if (stage != 1) continue;
			DEG_LOG(E,"xml: unexpected symbol\n");
			if(isHelperInit) gui_idle_call_wait_drag([](){
				showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", "Unexpected symbol in XML file.\nXML文件中出现了意外的符号\n");
			},GTK_WINDOW(helper.getWidget("main_window")));
			delete[] src;
			delete[] buf;
			return sfd::Result<void>::error(sfd::ErrorCode::ParseError, "xml: unexpected symbol");
		}
		if (!memcmp(p, "!--", 3)) {
			p = strstr(p + 3, "--");
			if (!p || !((p[-1] - '!') | (p[-2] - '<')) || p[2] != '>')
			{
				DEG_LOG(E,"xml: unexpected syntax\n");
				if(isHelperInit) gui_idle_call_wait_drag([](){
					showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", "Unexpected syntax in XML file.\nXML文件中出现了意外的语法\n");
				},GTK_WINDOW(helper.getWidget("main_window")));
				delete[] src;
				delete[] buf;
				return sfd::Result<void>::error(sfd::ErrorCode::ParseError, "xml: unexpected syntax in comment");
			}
			p += 3;
			continue;
		}
		if (stage != 1) {
			stage += !memcmp(p, part1, part1_len);
			if (stage > 2)
			{
				DEG_LOG(E,"xml: more than one partition lists\n");
				if(isHelperInit) gui_idle_call_wait_drag([](){
					showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", "More than one partition list found in XML file.\nXML文件中找到多个分区列表\n");
				},GTK_WINDOW(helper.getWidget("main_window")));
				delete[] src;
				delete[] buf;
				return sfd::Result<void>::error(sfd::ErrorCode::ParseError, "xml: more than one partition lists");
			}
			p = strchr(p, '>');
			if (!p) {
				DEG_LOG(E,"xml: unexpected syntax\n");
				if(isHelperInit) gui_idle_call_wait_drag([](){
					showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", "Unexpected syntax in XML file.\nXML文件中出现了意外的语法\n");
				},GTK_WINDOW(helper.getWidget("main_window")));
				delete[] src;
				delete[] buf;
				return sfd::Result<void>::error(sfd::ErrorCode::ParseError, "xml: unexpected syntax before partitions");
			}
			p++;
			continue;
		}
		if (*p == '/' && !memcmp(p + 1, part1, part1_len)) {
			p = p + 1 + part1_len;
			stage++;
			continue;
		}
		i = sscanf(p, "Partition id=\"%35[^\"]\" size=\"%lli\"/%n%c", (*(pacptable + found)).name, &size, &n, &c);
		if (i != 3 || c != '>')
		{
			DEG_LOG(E,"xml: unexpected syntax\n");
			if(isHelperInit) gui_idle_call_wait_drag([](){
				showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", "Unexpected syntax in XML file.\nXML文件中出现了意外的语法\n");
			},GTK_WINDOW(helper.getWidget("main_window")));
			delete[] src;
			delete[] buf;
			return sfd::Result<void>::error(sfd::ErrorCode::ParseError, "xml: bad Partition element");
		}
		p += n + 1;
		if (buf_size < 0x4c)
		{
			DEG_LOG(E,"xml: too many partitions\n");
			if(isHelperInit) gui_idle_call_wait_drag([](){
				showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", "Too many partitions in XML file.\nXML文件中的分区数量过多\n");
			},GTK_WINDOW(helper.getWidget("main_window")));
			delete[] src;
			delete[] buf;
			return sfd::Result<void>::error(sfd::ErrorCode::ParseError, "xml: too many partitions");
		}

		buf_size -= 0x4c;
		memset(buf, 0, 36 * 2);
		for (i = 0; (a = (*(pacptable + found)).name[i]); i++) buf[i * 2] = a;
		if (!i)
		{
			DEG_LOG(E,"xml: empty partition name\n");
			if(isHelperInit) gui_idle_call_wait_drag([](){
				showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", "Empty partition name found in XML file.\nXML文件中发现了空的分区名称\n");
			},GTK_WINDOW(helper.getWidget("main_window")));
			delete[] src;
			delete[] buf;
			return sfd::Result<void>::error(sfd::ErrorCode::ParseError, "xml: empty partition name");
		}
		WRITE32_LE(buf + 0x48, (uint32_t)size);
		buf += 0x4c;
		DBG_LOG("[%d] %s, %d\n", found + 1, (*(pacptable + found)).name, (int)size);
		(*(pacptable + found)).size = size << 20;
		found++;
	}

	pac_part_count = found;
	if (p - 1 != src + fsize) {
		DEG_LOG(E,"xml: zero byte\n");
		if(isHelperInit) gui_idle_call_wait_drag([](){
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", "Zero byte found in XML file.\nXML文件中发现了零字节\n");
		},GTK_WINDOW(helper.getWidget("main_window")));
		delete[] src;
		delete[] buf;
		return sfd::Result<void>::error(sfd::ErrorCode::ParseError, "xml: zero byte");
	}
	if (stage != 2) {
		DEG_LOG(E,"xml: unexpected syntax\n");
		if(isHelperInit) gui_idle_call_wait_drag([](){
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", "Unexpected syntax in XML file.\nXML文件中出现了意外的语法\n");
		},GTK_WINDOW(helper.getWidget("main_window")));
		delete[] src;
		delete[] buf;
		return sfd::Result<void>::error(sfd::ErrorCode::ParseError, "xml: unexpected syntax after partitions");
	}

	delete[] src;
	delete[] buf;
	return sfd::Result<void>::ok();
}

std::string ExtractPartitionsWithTags(const std::string& xmlContent) {
    std::regex pattern("<Partitions>.*?</Partitions>", std::regex::icase);
    std::smatch match;

    if (std::regex_search(xmlContent, match, pattern)) {
        return match.str();
    }
    return "";
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
// WIP
bool pac_extract(const char* fn, const char* floder)
{
#if 0
	int pac_part_count;
	Unpac unpac;
	unpac.setDirectory(floder);
	if(!unpac.openPacFile(fn)) {
		DEG_LOG(E,"Failed to open PAC file.\n");
		if(isHelperInit) {
			gui_idle_call_wait_drag([](){
				showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", "Failed to open PAC file.\n无法打开PAC文件\n");
			},GTK_WINDOW(helper.getWidget("main_window")));
		}
		return false;
	}
	unpac.setFilter(0, NULL);
	if(!unpac.extractFiles()) {
		DEG_LOG(E,"Failed to extract files from PAC file.\n");
		if(isHelperInit) {
			gui_idle_call_wait_drag([](){
				showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", "Failed to extract files from PAC file.\n无法从PAC文件中提取文件\n");
			},GTK_WINDOW(helper.getWidget("main_window")));
		}
		return false;
	}
	unpac.listFiles();
	unpac.close();
	std::string xmlPath = FindFirstXMLFile(floder);
	if(xmlPath.empty()) {
		if(isHelperInit) gui_idle_call_wait_drag([](){
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", "No XML file found in the extracted folder.\n在解压后的文件夹中未找到XML文件\n");
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
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", "No partition info found in xml\n不能在xml中找到分区信息\n");
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
		ERR_EXIT("Failed to create temporary partitions XML file.\n无法创建临时分区XML文件\n");
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
	const std::vector<partition_t>& partitions = std::vector<partition_t>(pacptable, pacptable + pac_part_count);
	// 获取列表视图
	GtkWidget* part_list = helper.getWidget("pac_list");
	if (!part_list || !GTK_IS_TREE_VIEW(part_list)) {
		std::cerr << "pac_list not found or not a TreeView" << std::endl;
		if(isHelperInit) gui_idle_call_wait_drag([](){
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", "Partition list view not found.\n未找到分区列表视图\n");
		},GTK_WINDOW(helper.getWidget("main_window")));
		delete[] pacptable;
		return false;
	}

	// 获取列表存储模型
	GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(part_list));
	if (!model) {
		std::cerr << "TreeView model not found" << std::endl;
		if(isHelperInit) gui_idle_call_wait_drag([](){
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", "Partition list model not found.\n未找到分区列表模型\n");
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
	if (spl_size < 1024) {
		size_str = std::to_string(spl_size) + " B";
	} else {
		size_str = std::to_string(spl_size / 1024) + " KB";
	}
	gtk_list_store_set(store, &iter_spl,
	                   0, display_name.c_str(),   // 显示名称（带序号）
	                   1, size_str.c_str(),       // 格式化的大小
	                   2, "splloader",            // 原始分区名
	                   -1);

	index++;  // 递增序号
	for (const auto& partition : partitions) {
		GtkTreeIter iter;
		gtk_list_store_append(store, &iter);

		// 格式化显示文本
		std::string display_name = std::to_string(index) + ". " + partition.name;

		// 格式化大小显示
		std::string size_str;
		if (partition.size < 1024) {
			size_str = std::to_string(partition.size) + " B";
		} else if (partition.size < 1024 * 1024) {
			size_str = std::to_string(partition.size / 1024) + " KB";
		} else if (partition.size < 1024 * 1024 * 1024) {
			size_str = std::to_string(partition.size / (1024 * 1024)) + " MB";
		} else {
			size_str = std::to_string(partition.size / (1024 * 1024 * 1024.0)) + " GB";
		}

		// 设置行数据
		gtk_list_store_set(store, &iter,
		                   0, display_name.c_str(),  // 显示名称（带序号）
		                   1, size_str.c_str(),      // 格式化的大小
		                   2, partition.name,        // 原始分区名（隐藏列，可选）
		                   -1);

		index++;
	}

	// 更新显示
	gtk_widget_queue_draw(part_list);
	delete[] pacptable;
#endif

	auto result = sfd::pac_unpack_and_analyze(fn, floder);
	if (!result) {
		DEG_LOG(E, "pac_unpack_and_analyze failed: %s", result.message.c_str());
		if (isHelperInit) {
			auto message = result.message;
			gui_idle_call_wait_drag([message]() {
				const char* ui_message = nullptr;

				if (message == "openPacFile failed") {
					ui_message = "Failed to open PAC file.\n无法打开PAC文件\n";
				} else if (message == "extractFiles failed") {
					ui_message = "Failed to extract files from PAC file.\n无法从PAC文件中提取文件\n";
				} else if (message == "XML file not found in unpack folder") {
					ui_message = "No XML file found in the extracted folder.\n在解压后的文件夹中未找到XML文件\n";
				} else if (message == "no partition info found in XML") {
					ui_message = "No partition info found in xml\n不能在xml中找到分区信息\n";
				} else if (message == "failed to create temporary partitions XML file") {
					ui_message = "Failed to create temporary partitions XML file.\n无法创建临时分区XML文件\n";
				}

				if (ui_message) {
					showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", ui_message);
				} else {
					std::string generic = "Failed to unpack PAC file: ";
					generic += message;
					generic += "\n解包 PAC 文件失败，请检查文件是否有效\n";
					showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", generic.c_str());
				}
			}, GTK_WINDOW(helper.getWidget("main_window")));
		}
		return false;
	}

	const PacUnpackInfo& info = result.value;

	GtkWidget* part_list = helper.getWidget("pac_list");
	if (!part_list || !GTK_IS_TREE_VIEW(part_list)) {
		std::cerr << "pac_list not found or not a TreeView" << std::endl;
		if (isHelperInit) gui_idle_call_wait_drag([]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", "Partition list view not found.\n未找到分区列表视图\n");
		}, GTK_WINDOW(helper.getWidget("main_window")));
		return false;
	}

	GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(part_list));
	if (!model) {
		std::cerr << "TreeView model not found" << std::endl;
		if (isHelperInit) gui_idle_call_wait_drag([]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", "Partition list model not found.\n未找到分区列表模型\n");
		}, GTK_WINDOW(helper.getWidget("main_window")));
		return false;
	}

	GtkListStore* store = GTK_LIST_STORE(model);
	gtk_list_store_clear(store);

	int index = 1;
	GtkTreeIter iter_spl;
	gtk_list_store_append(store, &iter_spl);
	long long spl_size = g_spl_size > 0 ? g_spl_size : 0;
	std::string display_name = std::to_string(index) + ". splloader";
	std::string size_str;
	if (spl_size < 1024) {
		size_str = std::to_string(spl_size) + " B";
	} else {
		size_str = std::to_string(spl_size / 1024) + " KB";
	}
	gtk_list_store_set(store, &iter_spl,
				   0, display_name.c_str(),
				   1, size_str.c_str(),
				   2, "splloader",
				   -1);

	index++;
	for (const auto& partition : info.partitions) {
		GtkTreeIter iter;
		gtk_list_store_append(store, &iter);

		std::string display_name = std::to_string(index) + ". " + partition.name;

		std::string size_str;
		auto size = partition.size_bytes;
		if (size < 1024) {
			size_str = std::to_string(size) + " B";
		} else if (size < 1024 * 1024) {
			size_str = std::to_string(size / 1024) + " KB";
		} else if (size < 1024ull * 1024ull * 1024ull) {
			size_str = std::to_string(size / (1024 * 1024)) + " MB";
		} else {
			double gb = static_cast<double>(size) / (1024.0 * 1024.0 * 1024.0);
			size_str = std::to_string(gb) + " GB";
		}

		gtk_list_store_set(store, &iter,
					   0, display_name.c_str(),
					   1, size_str.c_str(),
					   2, partition.name.c_str(),
					   -1);

		index++;
	}

	gtk_widget_queue_draw(part_list);
	return true;
}

// 辅助函数：去掉字符串前后的空格/换行/回车
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

// 查找文件中第一个出现的 <ID>xxx</ID> 对应的 <Base>
std::string findBaseForID(const std::string& filename, const std::string& targetID) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return "";
    }

    std::string line;
    bool inTargetFile = false;
    bool foundID = false;

    while (std::getline(file, line)) {
        std::string trimmed = trim(line);

        // 查找 <ID> 标签
        if (!foundID) {
            size_t idStart = trimmed.find("<ID>");
            size_t idEnd = trimmed.find("</ID>");
            if (idStart != std::string::npos && idEnd != std::string::npos) {
                std::string idContent = trimmed.substr(idStart + 4, idEnd - idStart - 4);
                if (idContent == targetID) {
                    foundID = true;
                }
                continue;
            }
        }

        // 如果已经找到目标 ID，则查找下一个 <Base>
        if (foundID) {
            size_t baseStart = trimmed.find("<Base>");
            size_t baseEnd = trimmed.find("</Base>");
            if (baseStart != std::string::npos && baseEnd != std::string::npos) {
                std::string baseContent = trimmed.substr(baseStart + 6, baseEnd - baseStart - 6);
                return trim(baseContent);
            }
        }
    }

    return ""; // 未找到
}


static ::sfd::Result<void> check_pac_crc_result(const char* fn) {
    Unpac unpac;
    if (!unpac.openPacFile(fn)) {
        // openPacFile 已经打印了具体原因，这里只做分类
        return ::sfd::Result<void>::error(::sfd::ErrorCode::ParseError,
                                          "PAC header invalid or unsupported");
    }

    auto r = unpac.checkCrc_result();
    if (!r) {
        return r;
    }

    return ::sfd::Result<void>::ok();
}

namespace {

sfd::Result<PacUnpackInfo> pac_unpack_and_analyze_impl(const char* pac_path,
                                                       const char* out_folder) {
    if (!pac_path || !*pac_path || !out_folder || !*out_folder) {
        return sfd::Result<PacUnpackInfo>::error(sfd::ErrorCode::InvalidArgument,
                                                 "empty pac path or output folder");
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(pac_path, ec) || ec) {
        return sfd::Result<PacUnpackInfo>::error(sfd::ErrorCode::NotFound,
                                                 "PAC file not found");
    }

    Unpac unpac;
    unpac.setDirectory(out_folder);
    if (!unpac.openPacFile(pac_path)) {
        // openPacFile 已经打印了具体原因
        return sfd::Result<PacUnpackInfo>::error(sfd::ErrorCode::ParseError,
                                                 "openPacFile failed");
    }
    unpac.setFilter(0, nullptr);
    if (!unpac.extractFiles()) {
        return sfd::Result<PacUnpackInfo>::error(sfd::ErrorCode::IoError,
                                                 "extractFiles failed");
    }

    PacUnpackInfo info;
    info.pac_path   = pac_path;
    info.unpack_dir = out_folder;

    // 查找 FDL1/FDL2 路径
    info.fdl_files.fdl1 = FindFDLInExtFloder(out_folder, FDL1);
    info.fdl_files.fdl2 = FindFDLInExtFloder(out_folder, FDL2);
    if (info.fdl_files.fdl1.empty() || info.fdl_files.fdl2.empty()) {
        return sfd::Result<PacUnpackInfo>::error(
            sfd::ErrorCode::ParseError,
            "FDL files not found in unpack folder");
    }

    // 查找 XML 并解析 FDL base 地址
    std::string xmlPath = FindFirstXMLFile(out_folder);
    if (xmlPath.empty()) {
        return sfd::Result<PacUnpackInfo>::error(
            sfd::ErrorCode::ParseError,
            "XML file not found in unpack folder");
    }

    std::string fdl1_base_str = findBaseForID(xmlPath, "FDL1");
    std::string fdl2_base_str = findBaseForID(xmlPath, "FDL2");
    if (!fdl1_base_str.empty()) {
        info.fdl_addrs.fdl1_base = strtoul(fdl1_base_str.c_str(), nullptr, 16);
        info.fdl_addrs.has_fdl1  = info.fdl_addrs.fdl1_base != 0;
    }
    if (!fdl2_base_str.empty()) {
        info.fdl_addrs.fdl2_base = strtoul(fdl2_base_str.c_str(), nullptr, 16);
        info.fdl_addrs.has_fdl2  = info.fdl_addrs.fdl2_base != 0;
    }

    if (!info.fdl_addrs.has_fdl1 || !info.fdl_addrs.has_fdl2) {
        return sfd::Result<PacUnpackInfo>::error(
            sfd::ErrorCode::ParseError,
            "FDL base address missing in XML");
    }

    // 解析 <Partitions> 列表
    std::ifstream file(xmlPath);
    if (!file.is_open()) {
        return sfd::Result<PacUnpackInfo>::error(sfd::ErrorCode::IoError,
                                                 "failed to open XML file");
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    std::string partxml = ExtractPartitionsWithTags(content);
    if (partxml.empty()) {
        return sfd::Result<PacUnpackInfo>::error(
            sfd::ErrorCode::ParseError,
            "no partition info found in XML");
    }

    const char* temp_xml = "partitions_temp.xml";
    {
        FILE* fi = oxfopen(temp_xml, "w");
        if (!fi) {
            return sfd::Result<PacUnpackInfo>::error(
                sfd::ErrorCode::IoError,
                "failed to create temporary partitions XML file");
        }
        fwrite(partxml.c_str(), 1, partxml.size(), fi);
        fclose(fi);
    }

    partition_t* pacptable = NEWN partition_t[128];
    int pac_part_count = 0;
    auto r = parse_partitions_xml_result(temp_xml, pacptable, pac_part_count);
    if (!r) {
        delete[] pacptable;
        return sfd::Result<PacUnpackInfo>::error(r.code, r.message);
    }

    info.partitions.clear();
    info.partitions.reserve(pac_part_count);
    for (int i = 0; i < pac_part_count; ++i) {
        PacPartitionInfo pi{};
        pi.name       = pacptable[i].name;
        pi.image_path = ""; // 当前实现暂未维护具体镜像文件名
        pi.size_bytes = static_cast<std::uint64_t>(pacptable[i].size);
        info.partitions.push_back(std::move(pi));
    }

    delete[] pacptable;
    return sfd::Result<PacUnpackInfo>::ok(std::move(info));
}

} // anonymous namespace

namespace sfd {

Result<void> pac_extract_result(const char* fn, const char* folder) {
    // 基础参数校验：用于测试和统一错误模型
    if (!fn || !*fn || !folder || !*folder) {
        return Result<void>::error(ErrorCode::InvalidArgument,
                                   "empty pac path or output folder");
    }

    // 复用新的 PAC 解包 + 分析逻辑，只关心成功/失败
    auto r = pac_unpack_and_analyze(fn, folder);
    if (!r) {
        // 将底层错误码与消息直接透传给调用方
        return Result<void>::error(r.code, r.message);
    }

    return Result<void>::ok();
}

Result<PacUnpackInfo> pac_unpack_and_analyze(const char* pac_path,
                                             const char* out_folder) {
    return pac_unpack_and_analyze_impl(pac_path, out_folder);
}

} // namespace sfd
