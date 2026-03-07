#include "pac_extract.h"
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
    if (!fi) { printf("PAC file not opened\n"); return false; }

    // Head CRC
    uint32_t head_crc = u_crc16(0, &head, sizeof(head) - 4);
    printf("head_crc: 0x%04x", head.head_crc);
    if (head.head_crc != head_crc)
        printf(" (expected 0x%04x)", head_crc);
    printf("\n");

    // Data CRC
    uint32_t n, l = head.pac_size;
    uint32_t data_crc = 0;
    buf = (uint8_t*) malloc(chunk);
    if (!buf) { printf("malloc failed\n"); return false; }
    n = sizeof(head);
    if (l < n) { printf("unexpected pac size\n"); return false; }
    for (l -= n; l; l -= n) {
        n = l > chunk ? chunk : l;
        READ(buf, n, "chunk");
        data_crc = u_crc16(data_crc, buf, n);
    }
    printf("data_crc: 0x%04x", head.data_crc);
    if (head.data_crc != data_crc)
        printf(" (expected 0x%04x)", data_crc);
    printf("\n");
    return true;
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
bool pac_extract(const char* fn, const char* floder)
{	
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
	uint32_t buf_size = 0xffff;
	uint8_t* buf = NEWN uint8_t[0x4c * 128];
	const char *part1 = "Partitions>";
	char *src, *p; size_t fsize = 0;
	int part1_len = strlen(part1), found = 0, stage = 0;
	src = (char *)loadfile("partitions_temp.xml", &fsize, 1);
	if (!src) ERR_EXIT("loadfile failed\n");
	src[fsize] = 0;
	p = src;
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
			return false;
		}
		if (!memcmp(p, "!--", 3)) {
			p = strstr(p + 3, "--");
			if (!p || !((p[-1] - '!') | (p[-2] - '<')) || p[2] != '>')
			{
				DEG_LOG(E,"xml: unexpected syntax\n");
				if(isHelperInit) gui_idle_call_wait_drag([](){
					showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", "Unexpected syntax in XML file.\nXML文件中出现了意外的语法\n");
				},GTK_WINDOW(helper.getWidget("main_window")));
				return false;
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
				return false;
			}
			p = strchr(p, '>');
			if (!p) {
				DEG_LOG(E,"xml: unexpected syntax\n");
				if(isHelperInit) gui_idle_call_wait_drag([](){
					showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", "Unexpected syntax in XML file.\nXML文件中出现了意外的语法\n");
				},GTK_WINDOW(helper.getWidget("main_window")));
				return false;
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
			return false;
		}
		p += n + 1;
		if (buf_size < 0x4c)
		{
			DEG_LOG(E,"xml: too many partitions\n");
			if(isHelperInit) gui_idle_call_wait_drag([](){
				showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", "Too many partitions in XML file.\nXML文件中的分区数量过多\n");
			},GTK_WINDOW(helper.getWidget("main_window")));
			return false;
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
			return false;
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
		return false;
	}
	if (stage != 2) {
		DEG_LOG(E,"xml: unexpected syntax\n");
		if(isHelperInit) gui_idle_call_wait_drag([](){
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", "Unexpected syntax in XML file.\nXML文件中出现了意外的语法\n");
		},GTK_WINDOW(helper.getWidget("main_window")));
		return false;
	}
	delete[] src;
	delete[] buf;
	const std::vector<partition_t>& partitions = std::vector<partition_t>(pacptable, pacptable + pac_part_count);
	// 获取列表视图
	GtkWidget* part_list = helper.getWidget("pac_list");
	if (!part_list || !GTK_IS_TREE_VIEW(part_list)) {
		std::cerr << "pac_list not found or not a TreeView" << std::endl;
		if(isHelperInit) gui_idle_call_wait_drag([](){
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", "Partition list view not found.\n未找到分区列表视图\n");
		},GTK_WINDOW(helper.getWidget("main_window")));
		return false;
	}

	// 获取列表存储模型
	GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(part_list));
	if (!model) {
		std::cerr << "TreeView model not found" << std::endl;
		if(isHelperInit) gui_idle_call_wait_drag([](){
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error", "Partition list model not found.\n未找到分区列表模型\n");
		},GTK_WINDOW(helper.getWidget("main_window")));
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
	return true;
}
