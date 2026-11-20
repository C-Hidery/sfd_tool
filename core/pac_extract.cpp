/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#include "pac_extract.h"
#include "pac_generate.h"
#include "../i18n.h"
#include "logging.h"  // 使用统一的 ERR_EXIT
#include "app_state.h"
#include "../common.h"
#include "../ui/gtk_row_view.hpp"
#define _GNU_SOURCE 1
#define _FILE_OFFSET_BITS 64

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>  // C++17 filesystem
#include <sstream>
#include <sys/stat.h>
#include <cerrno>

#include "../ui/layout/bottom_bar.h"
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <direct.h> // for chdir
#else
#include <unistd.h>
#endif
#include <iostream>    // for error output

#include "logging.h"  // 使用统一的 ERR_EXIT
#include "result.h"   // T2-02: Result/ErrorCode
#include "../pages/page_pac_flash.h"
#include "Unpac.h"
#include "PacXMLParser.hpp"
#include "xmlutil.hpp"

static int& selected_ab = g_app_state.flash.selected_ab;

static sfd::Result<void> parse_partitions_xml_result(const std::string& pxml,
                                                     partition_t* pacptable,
                                                     int* pac_part_count)
{
    // 1. 解析 XML（RAII 管理 doc 生命周期）
    auto doc = xmlutil::loadString(pxml, "pac_partitions.xml");

    if (!doc)
    {
        DEG_LOG(E, "Failed to parse XML file\n");
        if (isHelperInit)
            gui_idle_call_wait_drag([]()
            {
                showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")),
                                "Error", _("Failed to parse XML file."));
            }, GTK_WINDOW(helper.getWidget("main_window")));
        return sfd::Result<void>::error(sfd::ErrorCode::IoError, "loadfile failed");
    }

    xmlNodePtr root = xmlDocGetRootElement(doc.get());
    if (!root)
    {
        DEG_LOG(E, "Empty XML document\n");
        if (isHelperInit)
            gui_idle_call_wait_drag([]()
            {
                showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")),
                                "Error", _("Failed to parse XML file."));
            }, GTK_WINDOW(helper.getWidget("main_window")));
        return sfd::Result<void>::error(sfd::ErrorCode::ParseError, "empty xml");
    }

    // 2. 查找 <Partitions> 节点（必须唯一）
    std::vector<xmlNodePtr> partitionsNodes;
    xmlutil::collectDescendants(root, "Partitions", partitionsNodes);

    if (partitionsNodes.empty())
    {
        DEG_LOG(E, "No Partitions element found\n");
        if (isHelperInit)
            gui_idle_call_wait_drag([]()
            {
                showErrorDialog(
                    GTK_WINDOW(helper.getWidget("main_window")),
                    "Error", _("No <Partitions> element in XML."));
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
                    GTK_WINDOW(helper.getWidget("main_window")),
                    "Error",
                    _("More than one partition list found in XML file."));
            }, GTK_WINDOW(helper.getWidget("main_window")));
        return sfd::Result<void>::error(sfd::ErrorCode::ParseError,
                                        "xml: more than one partition lists");
    }

    xmlNodePtr partitions = partitionsNodes[0];

    // 仅处理一级子节点 <Partition>
    auto partitionNodes = xmlutil::children(partitions, "Partition");
    if (partitionNodes.empty())
    {
        DEG_LOG(E, "No Partition elements inside Partitions\n");
        if (isHelperInit)
            gui_idle_call_wait_drag([]()
            {
                showErrorDialog(
                    GTK_WINDOW(helper.getWidget("main_window")),
                    "Error",
                    _("No <Partition> elements inside <Partitions>."));
            }, GTK_WINDOW(helper.getWidget("main_window")));
        return sfd::Result<void>::error(sfd::ErrorCode::ParseError,
                                        "xml: no Partition elements");
    }

    // 3. 准备 buf
    uint32_t buf_size = 0xffff;
    uint8_t* buf_orig = NEWN uint8_t[0x4c * 128];
    uint8_t* buf = buf_orig;
    int found = 0;

    // 4. 遍历每个 <Partition>
    for (xmlNodePtr partNode : partitionNodes)
    {
        // 提取 id
        std::string id = xmlutil::prop(partNode, "id");
        if (id.empty())
        {
            DEG_LOG(E, "Partition missing id attribute\n");
            if (isHelperInit)
                gui_idle_call_wait_drag([]()
                {
                    showErrorDialog(
                        GTK_WINDOW(helper.getWidget("main_window")),
                        "Error",
                        _("Partition element missing 'id' attribute."));
                }, GTK_WINDOW(helper.getWidget("main_window")));
            delete[] buf_orig;
            return sfd::Result<void>::error(sfd::ErrorCode::ParseError,
                                            "xml: missing id");
        }

        // 提取 size
        std::string sizeStr = xmlutil::prop(partNode, "size");
        if (sizeStr.empty())
        {
            DEG_LOG(E, "Partition missing size attribute\n");
            if (isHelperInit)
                gui_idle_call_wait_drag([]()
                {
                    showErrorDialog(
                        GTK_WINDOW(helper.getWidget("main_window")),
                        "Error",
                        _("Partition element missing 'size' attribute."));
                }, GTK_WINDOW(helper.getWidget("main_window")));
            delete[] buf_orig;
            return sfd::Result<void>::error(sfd::ErrorCode::ParseError,
                                            "xml: missing size");
        }

        char* endptr = nullptr;
        long long size = strtoll(sizeStr.c_str(), &endptr, 0);
        if (*endptr != '\0' &&
            !isspace(static_cast<unsigned char>(*endptr)))
        {
            DEG_LOG(E, "Invalid size value\n");
            if (isHelperInit)
                gui_idle_call_wait_drag([]()
                {
                    showErrorDialog(
                        GTK_WINDOW(helper.getWidget("main_window")),
                        "Error",
                        _("Invalid size value in Partition."));
                }, GTK_WINDOW(helper.getWidget("main_window")));
            delete[] buf_orig;
            return sfd::Result<void>::error(sfd::ErrorCode::ParseError,
                                            "xml: bad size");
        }

        // 剩余缓冲区容量
        if (buf_size < 0x4c)
        {
            DEG_LOG(E, "Too many partitions\n");
            if (isHelperInit)
                gui_idle_call_wait_drag([]()
                {
                    showErrorDialog(
                        GTK_WINDOW(helper.getWidget("main_window")),
                        "Error",
                        _("Too many partitions in XML file."));
                }, GTK_WINDOW(helper.getWidget("main_window")));
            delete[] buf_orig;
            return sfd::Result<void>::error(sfd::ErrorCode::ParseError,
                                            "xml: too many partitions");
        }
        buf_size -= 0x4c;

        // 名称区域：36 个宽字符，共 72 字节
        memset(buf, 0, 36 * 2);
        for (size_t i = 0; i < id.size() && i < 36; ++i)
        {
            buf[i * 2] = static_cast<uint8_t>(id[i]);
        }

        // 写入 size（LE，偏移 0x48）
        WRITE32_LE(buf + 0x48, static_cast<uint32_t>(size));

        // 填充 pacptable
        strncpy(pacptable[found].name, id.c_str(),
                sizeof(pacptable[found].name) - 1);
        pacptable[found].name[sizeof(pacptable[found].name) - 1] = '\0';
        pacptable[found].size = size << 20;

        DBG_LOG("[%d] %s, %d\n",
                found + 1, pacptable[found].name, (int)size);

        buf += 0x4c;
        ++found;
    }

    *pac_part_count = found;
    delete[] buf_orig;
    return sfd::Result<void>::ok();
}

std::string ExtractPartitionsWithTags(const std::string& xmlContent)
{
    if (xmlContent.empty()) return "";

    // 用内存解析，禁止网络实体
    xmlDocPtr doc = xmlReadMemory(
        xmlContent.c_str(),
        static_cast<int>(xmlContent.size()),
        "inline.xml",
        nullptr,
        XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);

    if (!doc) return "";

    xmlNodePtr root = xmlDocGetRootElement(doc);
    if (!root) {
        xmlFreeDoc(doc);
        return "";
    }

    // 递归查找第一个 <Partitions>
    xmlNodePtr partitions = xmlutil::findFirstDescendant(root, "Partitions");
    if (!partitions) {
        xmlFreeDoc(doc);
        return "";
    }

    // 序列化该子树（含自身标签）
    xmlBufferPtr buf = xmlBufferCreate();
    if (!buf) {
        xmlFreeDoc(doc);
        return "";
    }

    // 参数：buffer, doc, node, indent_level=0, format=1
    xmlNodeDump(buf, doc, partitions, 0, 1);

    std::string result(
        reinterpret_cast<const char*>(xmlBufferContent(buf)),
        xmlBufferLength(buf));

    xmlBufferFree(buf);
    xmlFreeDoc(doc);
    return result;
}

std::string FindFirstXMLFile(const std::string& folderPath)
{
    namespace fs = std::filesystem;
    try
    {
        fs::path dir = utf8_to_path(folderPath);
        if (!fs::exists(dir) || !fs::is_directory(dir))
        {
            std::cerr << "Folder not found: " << folderPath << std::endl;
            return "";
        }

        for (const auto& entry : fs::directory_iterator(dir))
        {
            if (entry.is_regular_file())
            {
                // 扩展名统一转成 UTF-8 再比较
                std::string ext = path_to_utf8(entry.path().extension());
                if (ext == ".xml" || ext == ".XML")
                {
                    return path_to_utf8(entry.path());
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

PacFile& unpac = g_app_state.pacFile;

bool pac_extract(const char* fn, const char* folder)
{
    // 每次解包都重新判定 PAC 类型，避免上一次 RDA PAC 的标记残留到普通 PAC。
    g_app_state.flash.isPacRdaTable = false;
    g_app_state.flash.pacFilePath = (fn != nullptr) ? fn : "";
#ifndef _WIN32
    if (mkdir(folder, 0755) != 0 && errno != EEXIST) return false;
#else
    if (_wmkdir(utf8_to_utf16(std::string(folder)).c_str()) != 0 && errno != EEXIST) return false;
#endif
    if (!unpac.load(fn))
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
    if (!unpac.extract(folder))
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
    unpac.list(nullptr);
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
    EnhancedFile file = oxfopen_enhanced(xmlPath.c_str(), "r");
    if (!file)
    {
        DEG_LOG(E, "Failed to open xml for reading");
        return false;
    }
    file.close();
    g_app_state.flash.pac_xmlPath = xmlPath;
    if (!g_app_state.pacXml.loadFromFile(xmlPath))
    {
        DEG_LOG(E, "Failed to load XML file");
        return false;
    }
    auto products = g_app_state.pacXml.getProductNames();
    for (const auto& name : products)
    {
        std::cout << "PacXMLParser: Product: " << name << ", Scheme: " << g_app_state.pacXml.getSchemeName(name) <<
            std::endl;
    }
    std::string content;
    EnhancedFile file2 = oxfopen_enhanced(xmlPath.c_str(), "r");
    if (!file2)
    {
        DEG_LOG(E, "Failed to open xml for reading");
        return false;
    }
    content = file2.read_all_chunked();
    std::string partxml = ExtractPartitionsWithTags(content);
    if (partxml.empty())
    {
        for (int k = 0; k < unpac.fileCount; ++k)
        {
            const sprd_file_t& f = unpac.files[k];
            if (strcmp(unpac.u16_to_u8(f.id, MAX_U16_SN).c_str(), "NV") == 0)
            {
                if (f.addr[0] != 0)
                {
                    // RDA Table PAC!!!
                    if (isHelperInit)
                    {
                        showErrorDialogSyncInThread(
                            GTK_WINDOW(helper.getWidget("main_window")), _("Error"),
                            _("RDA Table PAC detected, please operate in CLI mode (via '--no-gui' first parameter)."));
                        return false;
                    }
                    else
                    {
                        g_app_state.flash.isPacRdaTable = true;
                        return true;
                    }
                }
                else
                {
                    break;
                }
            }
        }
        if (isHelperInit)
            gui_idle_call_wait_drag([]()
            {
                showErrorDialog(
                    GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("No partition info found in xml"));
            },GTK_WINDOW(helper.getWidget("main_window")));
        DEG_LOG(E, "No partition info found in xml");
        return false;
    }
    if (!io->pacptable)
    {
        io->pacptable = NEWN partition_t[128];
        if (!io->pacptable)
        {
            DEG_LOG(E, "Failed to allocate memory for partition table");
            return false;
        }
    }
    auto r = parse_partitions_xml_result(partxml, io->pacptable, &io->part_count_pac);
    if (!r)
    {
        return false;
    }

    if (isHelperInit)
    {
        // 本函数可能运行在工作线程，所有 GTK 模型操作必须回到 GUI 线程
        const std::vector<partition_t> partitions(io->pacptable, io->pacptable + io->part_count_pac);
        bool view_ok = true;
        gui_idle_call_wait_drag([partitions, &view_ok]()
        {
            GtkWidget* part_list = helper.getWidget("pac_list");
            if (!part_list || !GTK_IS_COLUMN_VIEW(part_list))
            {
                std::cerr << "pac_list not found or not a GtkColumnView" << std::endl;
                showErrorDialog(
                    GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("Partition list view not found."));
                view_ok = false;
                return;
            }

            // 清空现有数据
            sfd_row_view_clear(part_list);

            // 添加分区数据
            int index = 1;
            {
                std::string display_name = std::to_string(index) + ". splloader";
                SfdRow* row = sfd_row_new();
                sfd_row_set_checked(row, TRUE);
                sfd_row_set_cell(row, 1, display_name.c_str()); // 显示名称（带序号）
                sfd_row_set_cell(row, 2, "DEFAULT");            // 格式化的大小
                sfd_row_set_cell(row, 3, "splloader");          // 原始分区名
                sfd_row_view_add(part_list, row);
            }

            index++; // 递增序号
            for (const auto& partition : partitions)
            {
                std::string size_str;
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

                SfdRow* row = sfd_row_new();
                sfd_row_set_checked(row, TRUE);
                sfd_row_set_cell(row, 1, display_name.c_str()); // 显示名称（带序号）
                sfd_row_set_cell(row, 2, size_str.c_str());     // 格式化的大小
                sfd_row_set_cell(row, 3, partition.name);       // 原始分区名
                sfd_row_view_add(part_list, row);

                index++;
            }
        }, GTK_WINDOW(helper.getWidget("main_window")));

        if (!view_ok) return false;
    }
    return true;
}

static inline bool iequals(const std::string& a, const std::string& b)
{
    return a.size() == b.size() &&
        std::equal(a.begin(), a.end(), b.begin(),
                   [](char a, char b)
                   {
                       return std::tolower(static_cast<unsigned char>(a)) ==
                           std::tolower(static_cast<unsigned char>(b));
                   });
}

static bool hasPartition(const std::vector<std::string>& partitions, const std::string& partitionName)
{
    return std::find_if(partitions.begin(), partitions.end(),
                        [&partitionName](const std::string& s)
                        {
                            return iequals(s, partitionName);
                        }) != partitions.end();
}

// 判断 PAC 目录中是否存在某个 XML 操作项对应的、带数据的文件。
static bool pac_id_has_data(const std::string& id, const std::string& alias)
{
    PacFile& unpac = g_app_state.pacFile;
    for (int i = 0; i < unpac.fileCount; ++i)
    {
        const sprd_file_t& f = unpac.files[i];
        if (f.type == 0 || f.type == 0x101 || f.type == 2) continue;
        if (!f.size || !f.name[0]) continue;
        std::string fid = unpac.u16_to_u8(f.id, MAX_U16_SN);
        if (fid == id || (!alias.empty() && fid == alias)) return true;
    }
    return false;
}

bool pac_validate_required_files()
{
    for (const auto& file : g_app_state.pacXml.getPrimaryFiles())
    {
        if (file.flag != 1 || file.checkFlag != 1) continue;
        if (file.id.empty()) continue;
        if (my_strnicmp(file.id.c_str(), "FDL", 3) == 0) continue;
        if (!pac_id_has_data(file.id, file.idAlias))
        {
            DEG_LOG(E, "Required PAC file missing for operation: %s", file.id.c_str());
            return false;
        }
    }
    return true;
}

void pac_skip_empty_operations()
{
    for (const auto& file : g_app_state.pacXml.getPrimaryFiles())
    {
        if (file.flag != 0) continue;
        if (file.id.empty()) continue;
        if (my_strnicmp(file.id.c_str(), "FDL", 3) == 0) continue;

        DEG_LOG(I, "PAC operation %s (%s) has no file, skipping.",
                file.id.c_str(), file.type.c_str());
    }
}

// ----- PAC 分区表与设备分区表比较 -----

enum class PartitionDiffKind
{
    Match,
    MissingInPac,
    ExtraInPac,
    SizeMismatch
};

struct PartitionTableDiffEntry
{
    std::string name;
    std::string device_size;
    std::string pac_size;
    PartitionDiffKind kind = PartitionDiffKind::Match;
};

static std::string format_partition_size(long long bytes)
{
    if (bytes < 0)
    {
        return "-";
    }
    const unsigned long long value = static_cast<unsigned long long>(bytes);
    if (value < 1024ULL)
    {
        return std::to_string(value) + " B";
    }
    if (value < 1024ULL * 1024ULL)
    {
        return std::to_string(value / 1024ULL) + " KB";
    }
    if (value < 1024ULL * 1024ULL * 1024ULL)
    {
        return std::to_string(value / (1024ULL * 1024ULL)) + " MB";
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.2f GB",
                  static_cast<double>(value) / (1024.0 * 1024.0 * 1024.0));
    return std::string(buf);
}

static const partition_t* find_partition_entry(const partition_t* table, int count, const char* name)
{
    if (!table || !name)
    {
        return nullptr;
    }
    for (int i = 0; i < count; ++i)
    {
        if (std::strcmp(table[i].name, name) == 0)
        {
            return &table[i];
        }
    }
    return nullptr;
}

static std::string size_mismatch_detail(const partition_t& device_part, const partition_t& pac_part)
{
    return std::string(device_part.name) + " (size mismatch: device=" +
        format_partition_size(device_part.size) + ", pac=" +
        format_partition_size(pac_part.size) + ")";
}

// 逐项比较设备分区表与 PAC 分区表，并把差异同步到 missing_parts / extra_parts。
// 大小不一致会同时记入两份列表，便于 GUI/CLI 统一展示“存在差异”。
static std::vector<PartitionTableDiffEntry> compare_partition_tables(spdio_t* io)
{
    std::vector<PartitionTableDiffEntry> entries;
    g_app_state.pac.missing_parts.clear();
    g_app_state.pac.extra_parts.clear();
    if (!io)
    {
        return entries;
    }

    const int device_count = io->ptable ? io->part_count : 0;
    const int pac_count = io->pacptable ? io->part_count_pac : 0;

    for (int i = 0; i < device_count; ++i)
    {
        const partition_t& device_part = io->ptable[i];
        const partition_t* pac_part = find_partition_entry(io->pacptable, pac_count, device_part.name);

        PartitionTableDiffEntry entry;
        entry.name = device_part.name;
        entry.device_size = format_partition_size(device_part.size);
        entry.pac_size = pac_part ? format_partition_size(pac_part->size) : "-";

        if (!pac_part)
        {
            entry.kind = PartitionDiffKind::MissingInPac;
            g_app_state.pac.missing_parts.push_back(entry.name);
            DEG_LOG(W, "Partition %s exists on device but not in PAC", device_part.name);
        }
        // userdata 的实际容量会随设备/刷机模式变化，不参与大小一致性判断。
        else if (device_part.size != pac_part->size && !iequals(device_part.name, "userdata"))
        {
            entry.kind = PartitionDiffKind::SizeMismatch;
            const std::string detail = size_mismatch_detail(device_part, *pac_part);
            g_app_state.pac.missing_parts.push_back(detail);
            g_app_state.pac.extra_parts.push_back(detail);
            DEG_LOG(W, "Partition %s size mismatch: device=%s, pac=%s",
                    device_part.name, entry.device_size.c_str(), entry.pac_size.c_str());
        }
        entries.push_back(std::move(entry));
    }

    for (int i = 0; i < pac_count; ++i)
    {
        const partition_t& pac_part = io->pacptable[i];
        if (find_partition_entry(io->ptable, device_count, pac_part.name))
        {
            continue;
        }

        PartitionTableDiffEntry entry;
        entry.name = pac_part.name;
        entry.device_size = "-";
        entry.pac_size = format_partition_size(pac_part.size);
        entry.kind = PartitionDiffKind::ExtraInPac;
        g_app_state.pac.extra_parts.push_back(entry.name);
        DEG_LOG(W, "Partition %s exists in PAC but not on device", pac_part.name);
        entries.push_back(std::move(entry));
    }

    return entries;
}

static void show_partition_table_comparison_dialog(
    GtkWindow* parent,
    const std::vector<PartitionTableDiffEntry>& entries,
    std::size_t device_count,
    std::size_t pac_count,
    std::size_t missing_count,
    std::size_t extra_count,
    std::size_t mismatch_count,
    bool package_mode = false)
{
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        _("Partition Table Comparison"),
        parent,
        GTK_DIALOG_MODAL,
        _("Close"), GTK_RESPONSE_CLOSE,
        nullptr);

    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    std::string summary = _("Device partitions: ");
    summary += std::to_string(device_count);
    if (package_mode)
    {
        summary += _(", Package partitions: ");
        summary += std::to_string(pac_count);
        summary += _(", Missing on device: ");
        summary += std::to_string(missing_count);
        summary += _(", Size mismatch: ");
        summary += std::to_string(mismatch_count);
    }
    else
    {
        summary += _(", PAC partitions: ");
        summary += std::to_string(pac_count);
        summary += _(", Missing in PAC: ");
        summary += std::to_string(missing_count);
        summary += _(", Extra in PAC: ");
        summary += std::to_string(extra_count);
        summary += _(", Size mismatch: ");
        summary += std::to_string(mismatch_count);
    }

    GtkWidget* summary_label = gtk_label_new(summary.c_str());
    gtk_widget_set_halign(summary_label, GTK_ALIGN_START);
    gtkBoxPackStart(content, summary_label, FALSE, FALSE, 0);

    const char* warning_text = package_mode
        ? _("Package rejected because the device and package partition tables are inconsistent.")
        : _("PAC flashing has been aborted because the device and PAC partition tables are inconsistent.");
    GtkWidget* warning_label = gtk_label_new(warning_text);
    gtk_widget_set_halign(warning_label, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(warning_label, 8);
    gtkBoxPackStart(content, warning_label, FALSE, FALSE, 0);

    GtkWidget* scrolled = gtk_scrolled_window_new();
    gtk_widget_set_size_request(scrolled, 900, 520);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtkBoxPackStart(content, scrolled, TRUE, TRUE, 0);

    GtkWidget* tree = sfd_row_view_new(FALSE);
    sfd_row_view_add_text_column(tree, _("Status"), 0, FALSE);
    sfd_row_view_add_text_column(tree, _("Partition Name"), 1, FALSE);
    sfd_row_view_add_text_column(tree, _("Device Size"), 2, FALSE);
    sfd_row_view_add_text_column(tree, package_mode ? _("Package Size") : _("PAC Size"), 3, FALSE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), tree);

    for (const auto& entry : entries)
    {
        const char* status = "";
        switch (entry.kind)
        {
        case PartitionDiffKind::Match:
            status = _("OK");
            break;
        case PartitionDiffKind::MissingInPac:
            status = package_mode ? _("Missing on device") : _("Missing in PAC");
            break;
        case PartitionDiffKind::ExtraInPac:
            status = package_mode ? _("Extra in package") : _("Extra in PAC");
            break;
        case PartitionDiffKind::SizeMismatch:
            status = _("Size mismatch");
            break;
        }

        SfdRow* row = sfd_row_new();
        sfd_row_set_cell(row, 0, status);
        sfd_row_set_cell(row, 1, entry.name.c_str());
        sfd_row_set_cell(row, 2, entry.device_size.c_str());
        sfd_row_set_cell(row, 3, entry.pac_size.c_str());
        sfd_row_view_add(tree, row);
    }

#if GTK_CHECK_VERSION(4, 0, 0)
    gtk_widget_set_visible(dialog, TRUE);
#else
    gtk_widget_show_all(dialog);
#endif
    runDialog(GTK_DIALOG(dialog));
    destroyWidget(dialog);
}

void show_package_partition_check_dialog(
    _GtkWindow* parent,
    const std::vector<PackagePartitionCheckEntry>& entries,
    std::size_t device_count,
    std::size_t package_count,
    std::size_t missing_count,
    std::size_t mismatch_count)
{
    std::vector<PartitionTableDiffEntry> dialog_entries;
    dialog_entries.reserve(entries.size());
    for (const auto& entry : entries)
    {
        PartitionTableDiffEntry converted;
        converted.name = entry.name;
        converted.device_size = entry.device_size;
        converted.pac_size = entry.package_size;
        switch (entry.kind)
        {
        case PackagePartitionCheckKind::Match:
            converted.kind = PartitionDiffKind::Match;
            break;
        case PackagePartitionCheckKind::MissingOnDevice:
            converted.kind = PartitionDiffKind::MissingInPac;
            break;
        case PackagePartitionCheckKind::SizeMismatch:
            converted.kind = PartitionDiffKind::SizeMismatch;
            break;
        }
        dialog_entries.push_back(std::move(converted));
    }

    show_partition_table_comparison_dialog(
        parent, dialog_entries, device_count, package_count,
        missing_count, 0, mismatch_count, true);
}

// ---- 与 ResearchDownload 对齐：PAC 内 XML 的 File/Type 语义 ----

// RD 的 EraseFlash* 操作：按分区 ID 擦除。
static bool pac_type_is_erase(const std::string& type)
{
    return !my_stricmp(type.c_str(), "EraseFlash") ||
           !my_stricmp(type.c_str(), "EraseFlash2") ||
           !my_stricmp(type.c_str(), "EraseFlashByID");
}

// FDL/FDL2 由 BROM -> FDL1 -> FDL2 引导阶段处理，不参与分区循环。
static bool pac_type_is_fdl(const std::string& type)
{
    return !my_stricmp(type.c_str(), "FDL") ||
           !my_stricmp(type.c_str(), "FDL1") ||
           !my_stricmp(type.c_str(), "FDL2") ||
           !my_stricmp(type.c_str(), "NAND_FDL");
}

// RD 会给 PAC 头里的产品名加 "PAC_" 前缀后再与 XML 的 Product@name 对齐。
static std::string pac_strip_pac_prefix(std::string s)
{
    if (s.size() > 4 && !my_strnicmp(s.c_str(), "PAC_", 4))
    {
        s = s.substr(4);
    }
    return s;
}

namespace
{
bool contains_ci(const std::string& haystack, const char* needle)
{
    std::string h(haystack);
    std::string n(needle);
    for (char& c : h) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (char& c : n) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return h.find(n) != std::string::npos;
}

enum class SplVariant
{
    None,
    Emmc,
    Ufs
};

// SPLLoaderEMMC / SPL_LOADER_EMMC / SPLLoaderUFS / SPL_LOADER_UFS：
// 只认以 SPL 开头且带存储类型后缀的 ID/IDAlias，避免误伤其它条目。
SplVariant spl_variant_of(const FileInfo& f)
{
    for (const std::string* name : {&f.id, &f.idAlias})
    {
        if (name->empty()) continue;
        if (my_strnicmp(name->c_str(), "SPL", 3) != 0) continue;
        if (contains_ci(*name, "EMMC")) return SplVariant::Emmc;
        if (contains_ci(*name, "UFS")) return SplVariant::Ufs;
    }
    return SplVariant::None;
}

// PAC 文件表里是否同时存在 EMMC / UFS 两种 SPL 变体
bool pac_has_both_spl_variants(const PacFile& pac)
{
    bool has_emmc = false;
    bool has_ufs = false;
    for (int i = 0; i < pac.fileCount; ++i)
    {
        const std::string id = PacFile::u16_to_u8(pac.files[i].id, MAX_U16_SN);
        if (id.empty() || my_strnicmp(id.c_str(), "SPL", 3) != 0) continue;
        if (contains_ci(id, "EMMC")) has_emmc = true;
        else if (contains_ci(id, "UFS")) has_ufs = true;
        if (has_emmc && has_ufs) return true;
    }
    return false;
}

}  // namespace

namespace pac_plan
{
// PAC 内 XML 的执行顺序：先按产品名选中 Scheme（取不到就用第一个 Product），
// 然后按 <SchemeList>/<Scheme>/<File> 的原始顺序返回，这就是 RD 的执行顺序。
std::string product_name(const PacFile& pac, const PacXMLParser& xml)
{
    const std::string prd = pac_strip_pac_prefix(
        PacFile::u16_to_u8(pac.head.fw_name, MAX_U16_SN));
    if (prd.empty()) return std::string();

    for (const std::string& name : xml.getProductNames())
    {
        if (!my_stricmp(pac_strip_pac_prefix(name).c_str(), prd.c_str()))
        {
            return name;
        }
    }
    return std::string();
}

std::vector<FileInfo> xml_exec_order(const PacFile& pac, const PacXMLParser& xml)
{
    std::vector<FileInfo> files;
    const std::string prd = pac_strip_pac_prefix(
        PacFile::u16_to_u8(pac.head.fw_name, MAX_U16_SN));
    const std::string chosen = product_name(pac, xml);

    if (!chosen.empty())
    {
        files = xml.getFilesByProduct(chosen);
    }
    if (files.empty())
    {
        if (!prd.empty())
        {
            DEG_LOG(W, "PAC product name \"%s\" not found in XML ProductList, using the first Scheme.",
                    prd.c_str());
        }
        files = xml.getPrimaryFiles();
    }
    return files;
}

std::vector<Item> build(const PacFile& pac, const PacXMLParser& xml, int storage_type)
{
    std::vector<Item> plan;
    if (pac.fileCount <= 0 || !pac.files) return plan;

    std::vector<char> consumed(static_cast<size_t>(pac.fileCount), 0);

    // 1) XML 顺序：每个 File 归类成 Flash/Erase/Unsupported/Skip
    for (const FileInfo& f : xml_exec_order(pac, xml))
    {
        Item it;
        it.info = f;
        it.pac_id = f.id;

        // 用 ID 或 IDAlias 在 PAC 文件表里找对应条目（大小写不敏感，与 RD 的 MakeUpper 一致）
        for (int i = 0; i < pac.fileCount; ++i)
        {
            if (consumed[static_cast<size_t>(i)]) continue;
            const std::string pid = PacFile::u16_to_u8(pac.files[i].id, MAX_U16_SN);
            if (!my_stricmp(pid.c_str(), f.id.c_str()) ||
                (!f.idAlias.empty() && !my_stricmp(pid.c_str(), f.idAlias.c_str())))
            {
                it.pac_index = i;
                consumed[static_cast<size_t>(i)] = 1;
                break;
            }
        }

        if (it.pac_index >= 0)
        {
            const sprd_file_t& pf = pac.files[it.pac_index];
            it.pac_name = PacFile::u16_to_u8(pf.name, MAX_U16_SN);
            it.pac_size = (static_cast<uint64_t>(pf.size_high) << 32) | pf.size;
        }
        it.partition = f.blocks.empty() ? std::string() : f.blocks.front().id;

        if (pac_type_is_fdl(f.type) || !my_strnicmp(it.pac_id.c_str(), "FDL", 3))
        {
            it.kind = Kind::Skip;
            it.reason = "FDL handled by boot stage";
        }
        else if (f.flag == 0)
        {
            it.kind = pac_type_is_erase(f.type) ? Kind::Erase : Kind::Unsupported;
        }
        else if (it.pac_index < 0 || it.pac_size == 0)
        {
            it.kind = Kind::Skip;
            it.reason = "no data for this file in PAC";
        }
        else
        {
            it.kind = Kind::Flash;
        }
        plan.push_back(std::move(it));
    }

    // 2) SPL 变体选择：SPLLoaderEMMC / SPLLoaderUFS 都写到 splloader，
    //    若两个都在包里，按设备存储类型只保留匹配的一个（RD 由勾选决定，这里自动化）。
    if (storage_type == kStorageEmmc || storage_type == kStorageUfs)
    {
        bool has_emmc = false;
        bool has_ufs = false;
        for (const Item& it : plan)
        {
            if (it.kind != Kind::Flash) continue;
            const SplVariant v = spl_variant_of(it.info);
            if (v == SplVariant::Emmc) has_emmc = true;
            else if (v == SplVariant::Ufs) has_ufs = true;
        }

        if (has_emmc && has_ufs)
        {
            for (Item& it : plan)
            {
                if (it.kind != Kind::Flash) continue;
                const SplVariant v = spl_variant_of(it.info);
                if (v == SplVariant::None) continue;
                if (storage_type == kStorageEmmc && v == SplVariant::Ufs)
                {
                    it.kind = Kind::Skip;
                    it.reason = "SPL variant for UFS, device storage is eMMC";
                }
                else if (storage_type == kStorageUfs && v == SplVariant::Emmc)
                {
                    it.kind = Kind::Skip;
                    it.reason = "SPL variant for eMMC, device storage is UFS";
                }
            }
        }
        else if (has_emmc || has_ufs)
        {
            // 包里只有一种变体：不筛掉，但提示可能与设备不匹配。
            const SplVariant only = has_emmc ? SplVariant::Emmc : SplVariant::Ufs;
            const char* want = (storage_type == kStorageEmmc) ? "eMMC" : "UFS";
            const char* have = (only == SplVariant::Emmc) ? "eMMC" : "UFS";
            if (storage_type == kStorageEmmc ? only != SplVariant::Emmc : only != SplVariant::Ufs)
            {
                DEG_LOG(W, "PAC plan: only the %s SPL variant is present, but device storage is %s; "
                           "keeping it (no alternative in PAC)", have, want);
            }
        }
    }

    // 2) XML 未列出的 PAC 数据条目：RD 会因“文件数与 XML 不一致”直接拒包，
    //    这里为兼容仍按 PAC 顺序补刷，并标记 leftover 由调用方告警。
    for (int i = 0; i < pac.fileCount; ++i)
    {
        if (consumed[static_cast<size_t>(i)]) continue;
        const sprd_file_t& pf = pac.files[i];
        if (pf.type == 0 || pf.type == 0x101 || pf.type == 2) continue;
        const std::string pid = PacFile::u16_to_u8(pf.id, MAX_U16_SN);
        if (!my_strnicmp(pid.c_str(), "FDL", 3)) continue;
        const uint64_t size = (static_cast<uint64_t>(pf.size_high) << 32) | pf.size;
        if (!size) continue;

        Item it;
        it.kind = Kind::Flash;
        it.leftover = true;
        it.pac_index = i;
        it.pac_id = pid;
        it.pac_name = PacFile::u16_to_u8(pf.name, MAX_U16_SN);
        it.pac_size = size;
        it.partition = xml.getPartitionByOperation(pid);
        it.reason = "PAC entry not listed in XML";
        plan.push_back(std::move(it));
    }
    return plan;
}
}  // namespace pac_plan

static const char* pac_plan_kind_name(pac_plan::Kind kind)
{
    switch (kind)
    {
    case pac_plan::Kind::Flash: return "flash";
    case pac_plan::Kind::Erase: return "erase";
    case pac_plan::Kind::Unsupported: return "unsupported";
    case pac_plan::Kind::Skip: return "skip";
    }
    return "?";
}

void pac_print_offline_plan()
{
    PacFile& pac = g_app_state.pacFile;
    PacXMLParser& xml = g_app_state.pacXml;

    DEG_LOG(I, "==== PAC offline plan ====");
    DEG_LOG(I, "pac_version: %s", PacFile::u16_to_u8(pac.head.pac_version, 24).c_str());
    DEG_LOG(I, "fw_name: %s", PacFile::u16_to_u8(pac.head.fw_name, MAX_U16_SN).c_str());
    DEG_LOG(I, "fw_version: %s", PacFile::u16_to_u8(pac.head.fw_version, MAX_U16_SN).c_str());
    DEG_LOG(I, "fw_alias: %s", PacFile::u16_to_u8(pac.head.fw_alias, 100).c_str());
    DEG_LOG(I, "file_count: %d", pac.fileCount);
    if (io != nullptr)
    {
        DEG_LOG(I, "XML partitions: %d", io->part_count_pac);
    }
    if (g_app_state.flash.isPacRdaTable)
    {
        DEG_LOG(W, "RDA table PAC detected (no <Partitions> in XML).");
    }

    // ---- PAC 文件表 ----
    DEG_LOG(I, "---- PAC file table ----");
    for (int i = 0; i < pac.fileCount; ++i)
    {
        const sprd_file_t& f = pac.files[i];
        const std::string id = PacFile::u16_to_u8(f.id, MAX_U16_SN);
        const std::string name = PacFile::u16_to_u8(f.name, MAX_U16_SN);
        const uint64_t size = (static_cast<uint64_t>(f.size_high) << 32) | f.size;
        const uint64_t offset = (static_cast<uint64_t>(f.pac_offset_high) << 32) | f.pac_offset;

        std::string addrs;
        if (f.addr_num > 0 && f.addr_num <= 5)
        {
            char buf[64];
            for (uint32_t k = 0; k < f.addr_num; ++k)
            {
                std::snprintf(buf, sizeof(buf), " 0x%x", f.addr[k]);
                addrs += buf;
            }
        }
        DEG_LOG(I, "[%2d] id=\"%s\" name=\"%s\" size=0x%llx off=0x%llx type=%u check=%u omit=%u addr_num=%u%s",
                i, id.c_str(), name.c_str(), static_cast<unsigned long long>(size),
                static_cast<unsigned long long>(offset), f.type, f.flash_use, f.omit_flag,
                f.addr_num, addrs.c_str());
    }

    // ---- XML ----
    const std::string prd = pac_plan::product_name(pac, xml);
    DEG_LOG(I, "---- XML ----");
    DEG_LOG(I, "product: \"%s\" (matched: %s)", prd.c_str(), prd.empty() ? "no" : "yes");
    if (!prd.empty())
    {
        DEG_LOG(I, "APR flag: %d", xml.getProductApr(prd));
    }

    // ---- 计划（离线：存储类型未知，不做 SPL 变体筛选）----
    const std::vector<pac_plan::Item> plan = pac_plan::build(pac, xml, 0);
    DEG_LOG(I, "---- plan (offline, storage type unknown) ----");
    int n_flash = 0, n_erase = 0, n_unsupported = 0, n_skip = 0, n_leftover = 0;
    bool has_phasecheck = false;
    bool has_ise = false;
    for (std::size_t k = 0; k < plan.size(); ++k)
    {
        const pac_plan::Item& it = plan[k];
        if (it.leftover) ++n_leftover;
        switch (it.kind)
        {
        case pac_plan::Kind::Flash: ++n_flash; break;
        case pac_plan::Kind::Erase: ++n_erase; break;
        case pac_plan::Kind::Unsupported: ++n_unsupported; break;
        case pac_plan::Kind::Skip: ++n_skip; break;
        }
        if (!my_stricmp(it.pac_id.c_str(), "PhaseCheck")) has_phasecheck = true;
        if (!my_stricmp(it.pac_id.c_str(), "ISE") || !my_stricmp(it.pac_id.c_str(), "ISE_DATA"))
        {
            has_ise = true;
        }

        const std::string extra = it.reason.empty() ? std::string() : ("  (" + it.reason + ")");
        DEG_LOG(I, "[%2zu] %-12s id=\"%s\" type=\"%s\" -> %s size=0x%llx%s",
                k, pac_plan_kind_name(it.kind), it.pac_id.c_str(), it.info.type.c_str(),
                it.partition.empty() ? "-" : it.partition.c_str(),
                static_cast<unsigned long long>(it.pac_size), extra.c_str());
    }

    // ---- SPL 变体 ----
    bool spl_emmc = false;
    bool spl_ufs = false;
    for (int i = 0; i < pac.fileCount; ++i)
    {
        const std::string id = PacFile::u16_to_u8(pac.files[i].id, MAX_U16_SN);
        if (id.empty() || my_strnicmp(id.c_str(), "SPL", 3) != 0) continue;
        if (id.find("EMMC") != std::string::npos || id.find("emmc") != std::string::npos) spl_emmc = true;
        if (id.find("UFS") != std::string::npos || id.find("ufs") != std::string::npos) spl_ufs = true;
    }

    DEG_LOG(I, "---- summary ----");
    DEG_LOG(I, "flash=%d erase=%d unsupported=%d skip=%d leftover=%d",
            n_flash, n_erase, n_unsupported, n_skip, n_leftover);
    DEG_LOG(I, "PhaseCheck entry: %s; ISE entry: %s",
            has_phasecheck ? "yes" : "no", has_ise ? "yes" : "no");
    DEG_LOG(I, "SPL variants in PAC: EMMC=%s UFS=%s (selection happens at flash time by Da_Info.dwStorageType)",
            spl_emmc ? "yes" : "no", spl_ufs ? "yes" : "no");
}

bool pac_flash(const char* folder)
{
    bool isMergingNV_GUI = helper.getCheckButtonState(helper.getWidget("pac_merge_nv"));
    bool isRepartitionGUI = helper.getCheckButtonState(helper.getWidget("pac_repartition"));
    bool writePhaseCheck_GUI = helper.getCheckButtonState(helper.getWidget("pac_write_phasecheck"));
    bool writeApr_GUI = helper.getCheckButtonState(helper.getWidget("pac_write_apr"));
    bool writeIse_GUI = helper.getCheckButtonState(helper.getWidget("pac_write_ise"));
    DEG_LOG(I, "PAC Flash: Merging NV=%s, Repartition=%s, PhaseCheck=%s, APR=%s, ISE=%s",
            isMergingNV_GUI ? "true" : "false", isRepartitionGUI ? "true" : "false",
            writePhaseCheck_GUI ? "true" : "false", writeApr_GUI ? "true" : "false",
            writeIse_GUI ? "true" : "false");
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
    if (isHelperInit)
    {
        g_app_state.flash.isPacMergingNV = isMergingNV_GUI;
        g_app_state.flash.isPacWritePhaseCheck = writePhaseCheck_GUI;
        g_app_state.flash.isPacWriteApr = writeApr_GUI;
        g_app_state.flash.isPacWriteIse = writeIse_GUI;
        // phasecheckIniPath 由 PAC 页的 “Select PhaseCheck.ini” 按钮写入。
    }
    else
    {
        std::cout << "Do you want to merge NV partition(Y/n)?" << std::endl;
        std::string n;
        std::getline(std::cin, n);
        g_app_state.flash.isPacMergingNV = (n == "Y" || n == "y");

        // 生成型操作（对应 PAC 页的勾选项）：CLI 保持询问，默认 No。
        const std::vector<FileInfo> xml_files = pac_plan::xml_exec_order(g_app_state.pacFile, g_app_state.pacXml);
        bool has_phasecheck = false;
        for (const FileInfo& f : xml_files)
        {
            if (!my_stricmp(f.id.c_str(), "PhaseCheck") ||
                (!f.idAlias.empty() && !my_stricmp(f.idAlias.c_str(), "PhaseCheck")))
            {
                has_phasecheck = true;
                break;
            }
        }
        g_app_state.flash.isPacWritePhaseCheck = false;
        if (has_phasecheck)
        {
            std::cout << "Do you want to write PhaseCheck production info? (y/N): " << std::endl;
            std::string v;
            std::getline(std::cin, v);
            g_app_state.flash.isPacWritePhaseCheck = (v == "y" || v == "Y");
        }

        const std::string prd = pac_plan::product_name(g_app_state.pacFile, g_app_state.pacXml);
        g_app_state.flash.isPacWriteApr = false;
        if (!prd.empty() && g_app_state.pacXml.getProductApr(prd) != 0)
        {
            std::cout << "Do you want to write APR info (PAC build time)? (y/N): " << std::endl;
            std::string v;
            std::getline(std::cin, v);
            g_app_state.flash.isPacWriteApr = (v == "y" || v == "Y");
        }

        // ISE：默认空壳；选择生成时会要求提供 ini 路径。
        bool has_ise = false;
        for (const FileInfo& f : xml_files)
        {
            if (!my_stricmp(f.id.c_str(), "ISE") || !my_stricmp(f.idAlias.c_str(), "ISE") ||
                !my_stricmp(f.id.c_str(), "ISE_DATA") || !my_stricmp(f.idAlias.c_str(), "ISE_DATA"))
            {
                has_ise = true;
                break;
            }
        }
        g_app_state.flash.isPacWriteIse = false;
        if (has_ise)
        {
            std::cout << "Do you want to generate ISE from your PhaseCheck.ini? (y/N): " << std::endl;
            std::string v;
            std::getline(std::cin, v);
            g_app_state.flash.isPacWriteIse = (v == "y" || v == "Y");
        }

        g_app_state.flash.phasecheckIniPath.clear();
        if (g_app_state.flash.isPacWritePhaseCheck || g_app_state.flash.isPacWriteIse)
        {
            std::cout << "Enter path to PhaseCheck.ini: " << std::endl;
            std::string v;
            std::getline(std::cin, v);
            const std::size_t b = v.find_first_not_of(" \t\r\n");
            const std::size_t e = v.find_last_not_of(" \t\r\n");
            const std::string ini = (b == std::string::npos) ? std::string() : v.substr(b, e - b + 1);
            g_app_state.flash.phasecheckIniPath = ini;

            EnhancedFile probe = oxfopen_enhanced(ini.c_str(), "rb");
            if (!probe)
            {
                DEG_LOG(W, "PhaseCheck.ini not readable: %s; generation will be skipped", ini.c_str());
            }
        }
    }
    if (!pac_validate_required_files())
    {
        if (isHelperInit)
        {
            gui_idle_call_wait_drag([]()
            {
                showErrorDialog(
                    GTK_WINDOW(helper.getWidget("main_window")), _("Error"),
                    _("A required file is missing in the PAC."));
            }, GTK_WINDOW(helper.getWidget("main_window")));
        }
        return false;
    }

    auto into_func = [isRepartitionGUI]() mutable
    {
        std::string fdl1_path;
        uint32_t fdl1_base_addr = 0;
        std::string fdl2_path;
        uint32_t fdl2_base_addr = 0;
        PacFile& unpac = g_app_state.pacFile;
        for (int i = 0; i < unpac.fileCount; i++)
        {
            const sprd_file_t& file = unpac.files[i];
            if (file.id[0])
            {
                if (!strncmp(unpac.u16_to_u8(file.id, MAX_U16_SN).c_str(), "FDL", 3))
                {
                    std::string name = unpac.u16_to_u8(file.name, MAX_U16_SN);
                    if (name.empty()) fdl1_path = "";
#ifndef _WIN32
                    else fdl1_path = g_app_state.flash.pac_folder + "/" + name;
#else
                    else fdl1_path = g_app_state.flash.pac_folder + "\\" + name;
#endif
                    auto fdl1info = g_app_state.pacXml.getFileInfoByOperation("FDL");
                    if (!fdl1info.blocks.empty())
                    {
                        fdl1_base_addr = strtoul(fdl1info.blocks.front().base.c_str(), nullptr, 16);
                        DEG_LOG(I, "XML: FDL1_BASE_ADDR=%u", fdl1_base_addr);
                    }
                    else fdl1_base_addr = file.addr[0];
                    break;
                }
            }
        }
        for (int i = 0; i < unpac.fileCount; i++)
        {
            const sprd_file_t& file = unpac.files[i];
            if (file.id[0])
            {
                if (!strncmp(unpac.u16_to_u8(file.id, MAX_U16_SN).c_str(), "FDL2", 4))
                {
                    std::string name = unpac.u16_to_u8(file.name, MAX_U16_SN);
                    if (name.empty()) fdl2_path = "";
#ifndef _WIN32
                    else fdl2_path = g_app_state.flash.pac_folder + "/" + name;
#else
                    else fdl2_path = g_app_state.flash.pac_folder + "\\" + name;
#endif
                    auto fdl2info = g_app_state.pacXml.getFileInfoByOperation("FDL2");
                    if (!fdl2info.blocks.empty())
                    {
                        fdl2_base_addr = strtoul(fdl2info.blocks.front().base.c_str(), nullptr, 16);
                        DEG_LOG(I, "XML: FDL2_BASE_ADDR=%u", fdl2_base_addr);
                    }
                    else
                    {
                        fdl2_base_addr = file.addr[0];
                    }
                    break;
                }
            }
        }
        DEG_LOG(I, "FDL1_PATH=%s", fdl1_path.c_str());
        DEG_LOG(I, "FDL1_BASE_ADDR=%u", fdl1_base_addr);
        DEG_LOG(I, "FDL2_PATH=%s", fdl2_path.c_str());
        DEG_LOG(I, "FDL2_BASE_ADDR=%u", fdl2_base_addr);
        if (g_app_state.device.device_stage == BROM)
        {
            if (fdl1_base_addr == 0 || fdl1_path.empty())
            {
                if (isHelperInit)
                {
                    showErrorDialogSyncInThread(
                        GTK_WINDOW(helper.getWidget("main_window")), _("Error"),
                        _("Can not read FDL1 info from PAC, please execute FDL manually."));
                }
                DEG_LOG(E, "Can not read FDL1 info from PAC");
                return;
            }
            EnhancedFile fi = oxfopen_enhanced(fdl1_path.c_str(), "r");
            if (!fi)
            {
                DEG_LOG(W, "File does not exist.\n");
                if (isHelperInit)
                    showErrorDialogSyncInThread(
                        GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("File does not exist."));
                return;
            }
            fi.close();
            send_file(io, fdl1_path.c_str(), fdl1_base_addr, 0, 528, 0, 0);
            encode_msg_nocpy(io, BSL_CMD_EXEC_DATA, 0);
            if (send_and_check(io)) ERR_EXIT("FDL exec failed\n");

            DEG_LOG(OP, "Execute FDL1");

            if (fdl1_base_addr == 0x5500 || fdl1_base_addr == 0x65000800)
            {
                highspeed = 1;
                if (!baudrate) baudrate = 921600;
            }

            /* FDL1 (chk = sum) */
            io->flags &= ~FLAGS_CRC16;

            encode_msg(io, BSL_CMD_CHECK_BAUD, nullptr, 1);
            for (int i = 0; ; i++)
            {
                send_msg(io);
                recv_msg(io);
                if (recv_type(io) == BSL_REP_VER) break;
                DEG_LOG(W, "Failed to check baud, retry...");
                if (i == 4)
                {
                    ERR_EXIT(
                        "Can not execute FDL, please reboot your phone by pressing POWER and VOL_UP for 7-10 seconds.\n");
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
            if (baudrate)
            {
                uint8_t* data = io->temp_buf;
                WRITE32_BE(data, baudrate);
                encode_msg_nocpy(io, BSL_CMD_CHANGE_BAUD, 4);
                if (!send_and_check(io))
                {
                    DEG_LOG(OP, "Change baud FDL1 to %d", baudrate);
                    call_SetProperty(io->handle, 0, 100, (LPCVOID) & baudrate);
                }
            }
#endif

            encode_msg_nocpy(io, BSL_CMD_KEEP_CHARGE, 0);
            if (!send_and_check(io)) DEG_LOG(OP, "Keep charge FDL1.");

            fdl1_loaded = 1;
            if (isHelperInit) gui_idle_call([](){ bottom_bar_set_mode("FDL1 - PAC"); });
            g_app_state.device.device_stage = FDL1;
        }
        if (g_app_state.device.device_stage == FDL1)
        {
            if (fdl2_base_addr == 0 || fdl2_path.empty())
            {
                if (isHelperInit)
                {
                    showErrorDialogSyncInThread(
                        GTK_WINDOW(helper.getWidget("main_window")), _("Error"),
                        _("Can not read FDL2 info from PAC, please execute FDL manually."));
                }
                DEG_LOG(E, "Can not read FDL2 info from PAC");
                return;
            }
            EnhancedFile fi = oxfopen_enhanced(fdl2_path.c_str(), "r");
            if (!fi)
            {
                DEG_LOG(W, "File does not exist.\n");
                if (isHelperInit)
                    showErrorDialogSyncInThread(
                        GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("File does not exist."));
                return;
            }
            fi.close();
            // FDL2
            send_file(io, fdl2_path.c_str(), fdl2_base_addr, 0, 528, 0, 0);
            memset(&Da_Info, 0, sizeof(Da_Info));
            encode_msg_nocpy(io, BSL_CMD_EXEC_DATA, 0);
            send_msg(io);
            // Feature phones respond immediately,
            // but it may take a second for a smartphone to respond.
            int ret = recv_msg_timeout(io, 15000);
            if (!ret)
            {
                ERR_EXIT("timeout reached\n");
            }
            ret = recv_type(io);
            // Is it always bullshit?
            if (ret == BSL_REP_INCOMPATIBLE_PARTITION)
                get_Da_Info(io);
            else if (ret != BSL_REP_ACK)
            {
                const char* name = get_bsl_enum_name(ret);
                ERR_EXIT("unexpected response (%s : 0x%04x)\n", name, ret);
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
                if (ret != BSL_REP_READ_FLASH_INFO) DEG_LOG(E,"unexpected response (0x%04x)\n", ret);
                else Da_Info.dwStorageType = 0x101;
                // need more samples to cover BSL_REP_READ_MCP_TYPE packet to nand_id/nand_info
                // for nand_id 0x15, packet is 00 9b 00 0c 00 00 00 00 00 02 00 00 00 00 08 00
            }
            */
            if (Da_Info.bDisableHDLC)
            {
                encode_msg_nocpy(io, BSL_CMD_DISABLE_TRANSCODE, 0);
                if (!send_and_check(io))
                {
                    io->flags &= ~FLAGS_TRANSCODE;
                    DEG_LOG(OP, "Try to disable transcode 0x7D.");
                }
            }
            int o = io->verbose;
            io->verbose = -1;
            g_spl_size = check_partition(io, "splloader", 1);
            io->verbose = o;
            if (Da_Info.bSupportRawData)
            {
                blk_size = 0xf800;
                io->ptable = partition_list(io, &io->part_count);
                if (fdl2_executed)
                {
                    Da_Info.bSupportRawData = 0;
                    DEG_LOG(OP, "Raw data mode disabled for SPRD4.");
                }
                else
                {
                    encode_msg_nocpy(io, BSL_CMD_ENABLE_RAW_DATA, 0);
                    if (!send_and_check(io)) DEG_LOG(OP, "Raw data mode enabled.");
                }
            }


            else if (highspeed || Da_Info.dwStorageType == 0x103)
            {
                blk_size = 0xf800;
                io->ptable = partition_list(io, &io->part_count);
            }
            else if (Da_Info.dwStorageType == 0x102)
            {
                io->ptable = partition_list(io, &io->part_count);
            }
            else if (Da_Info.dwStorageType == 0x101) DEG_LOG(I, "Device storage is nand.");
            if (selected_ab < 0) select_ab(io);
            if (g_app_state.flash.selected_ab == 2) DEG_LOG(I, "Device is using slot b");
            else if (g_app_state.flash.selected_ab == 1) DEG_LOG(I, "Device is using slot a");
            else DEG_LOG(I, "Device is not using VAB");
            if (g_app_state.flash.gpt_failed != 1)
            {
                if (selected_ab != 1 && selected_ab != 2)
                {
                    if (Da_Info.bSupportRawData)
                    {
                        DEG_LOG(
                            I,
                            "Raw data mode is supported (level is %u) ,but DISABLED for stability, you can set it manually.",
                            (unsigned)Da_Info.bSupportRawData);
                        Da_Info.bSupportRawData = 0;
                    }
                }
            }
            if (!io->part_count)
            {
                DEG_LOG(W, "No partition table found on current device");
            }
            if (nand_id == DEFAULT_NAND_ID)
            {
                nand_info[0] = (uint8_t)pow(2, nand_id & 3); //page size
                nand_info[1] = 32 / (uint8_t)pow(2, (nand_id >> 2) & 3); //spare area size
                nand_info[2] = 64 * (uint8_t)pow(2, (nand_id >> 4) & 3); //block size
            }
            fdl2_executed = 1;
            g_app_state.device.device_stage = FDL2;
        }
        DEG_LOG(I, "Device is in FDL2 stage now, flash pac");
        if (isHelperInit) gui_idle_call([](){ bottom_bar_set_mode("FDL2 - PAC"); });
        if (!io->ptable || io->part_count <= 0)
        {
            g_app_state.pac.missing_parts.clear();
            g_app_state.pac.extra_parts.clear();
            if (isHelperInit)
            {
                bool i_is = false;
                i_is = showConfirmDialogSyncInThread(
                    GTK_WINDOW(helper.getWidget("main_window")), _("Confirm"),
                    _("No partition table found on device, cannot verify if PAC is compatible. Do you want to continue flashing?"));
                if (!i_is)
                {
                    return;
                }
            }
            else
            {
                std::cout << "No partition table found on device, cannot verify if PAC is compatible. " << std::endl << "Do you want to continue flashing? (Y/n): ";
                std::string response;
                std::getline(std::cin, response);
                if (response != "y" && response != "Y")
                {
                    return;
                }
            }
        }
        else
        {
            const std::vector<PartitionTableDiffEntry> diff_entries = compare_partition_tables(io);
            std::size_t missing_count = 0;
            std::size_t extra_count = 0;
            std::size_t mismatch_count = 0;
            for (const auto& entry : diff_entries)
            {
                switch (entry.kind)
                {
                case PartitionDiffKind::MissingInPac:
                    ++missing_count;
                    break;
                case PartitionDiffKind::ExtraInPac:
                    ++extra_count;
                    break;
                case PartitionDiffKind::SizeMismatch:
                    ++mismatch_count;
                    break;
                case PartitionDiffKind::Match:
                    break;
                }
            }

            if (g_app_state.pac.isHasMissingParts() || g_app_state.pac.isHasExtraParts())
            {
                DEG_LOG(E, "Partition table mismatch detected, PAC flashing aborted.");

                if (isHelperInit)
                {
                    GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
                    const std::size_t device_count = static_cast<std::size_t>(io->part_count);
                    const std::size_t pac_count = static_cast<std::size_t>(io->part_count_pac);
                    gui_idle_call_wait_drag(
                        [parent, diff_entries, device_count, pac_count,
                         missing_count, extra_count, mismatch_count]()
                        {
                            show_partition_table_comparison_dialog(
                                parent, diff_entries, device_count, pac_count,
                                missing_count, extra_count, mismatch_count);
                        }, parent);
                }
                else
                {
                    std::cout << "Partition table mismatch detected, PAC flashing aborted." << std::endl;
                    std::cout << "missing_parts:" << std::endl;
                    for (const auto& part : g_app_state.pac.missing_parts)
                    {
                        std::cout << "  " << part << std::endl;
                    }
                    std::cout << "extra_parts:" << std::endl;
                    for (const auto& part : g_app_state.pac.extra_parts)
                    {
                        std::cout << "  " << part << std::endl;
                    }
                }
                return;
            }
        }
        if (isHelperInit)
        {
            if (!showConfirmDialogSyncInThread(GTK_WINDOW(helper.getWidget("main_window")),
                                               _("Confirm"), _("Do you really want to start flashing PAC firmware?")))
            {
                return;
            }
        }
        else
        {
            std::cout << "Do you really want to start flashing PAC firmware? (Y/n): ";
            std::string response;
            std::getline(std::cin, response);
            if (response != "y" && response != "Y")
            {
                return;
            }
        }
        if (g_app_state.flash.isPacMergingNV)
        {
            auto& pacptable = g_app_state.flash.selected_pacptable;
            uint64_t nv_size = 0;
            get_partition_info(io, "nr_fixnv1", 1);
            if (gPartInfo.size && hasPartition(pacptable, gPartInfo.name))
            {
                // TempFile 的 move 赋值会自动删除上一个临时文件
                g_app_state.pac.nr_fixnv1_file_path = dump_partition_to_temp(
                    io, gPartInfo.name, 0, gPartInfo.size,
                    blk_size ? blk_size : DEFAULT_BLK_SIZE, &nv_size);
            }
            get_partition_info(io, "l_fixnv1", 1);
            if (gPartInfo.size && hasPartition(pacptable, gPartInfo.name))
            {
                g_app_state.pac.l_fixnv1_file_path = dump_partition_to_temp(
                    io, gPartInfo.name, 0, gPartInfo.size,
                    blk_size ? blk_size : DEFAULT_BLK_SIZE, &nv_size);
            }
            get_partition_info(io, "downloadnv", 1);
            if (gPartInfo.size && hasPartition(pacptable, gPartInfo.name))
            {
                g_app_state.pac.downloadnv_file_path = dump_partition_to_temp(
                    io, gPartInfo.name, 0, gPartInfo.size,
                    blk_size ? blk_size : DEFAULT_BLK_SIZE, &nv_size);
            }
        }
        bool i_is = false;
        if (isHelperInit)
        {
            i_is = isRepartitionGUI;
        }
        else
        {
            std::cout << "Do you want to repartition? (Y/n): ";
            std::string response;
            std::getline(std::cin, response);
            i_is = (response == "y" || response == "Y");
        }
        if (i_is)
        {
            EnhancedFile file = oxfopen_enhanced(g_app_state.flash.pac_xmlPath.c_str(), "r");
            if (!file) ERR_EXIT("Failed to open file for reading");
            std::string content;
            content = file.read_all_chunked();
            file.close();
            std::string partxml = ExtractPartitionsWithTags(content);
            uint8_t* buf = io->temp_buf;
            int n = scan_xml_partitions_from_string(io, partxml, buf, 0xffff);
            encode_msg_nocpy(io, BSL_CMD_REPARTITION, n * 0x4c);
            if (!send_and_check(io))
            {
                g_app_state.flash.gpt_failed = 0;
                g_app_state.flash.ptable_from_xml = false;
                gpt_refresh_cached_identity(io);
            }
        }

        g_app_state.flash.isPacFlashing = true;
        if (g_app_state.flash.selected_ab < 0) select_ab(io);
        pac_skip_empty_operations();

        // ------------------------------------------------------------------
        // 与 ResearchDownload 对齐的执行模型（仅调度层，不动传输/校验）：
        //   * 执行顺序以 PAC 内 XML 的 <SchemeList>/<Scheme>/<File> 顺序为准，
        //     而不是 PAC 文件表顺序；
        //   * Flag=0 的条目是“操作”（PAC 表里 nFileFlag=0、无数据），按 <Type> 执行：
        //       EraseFlash / EraseFlash2 / EraseFlashByID -> 擦除 Block 对应的分区；
        //     需要生成内容的操作（PhaseCheck / ISE / APR ...）交给 pac_generate；
        //   * Flag=1 的条目仍按原有规则写入（分区勾选、NV 合并、userdata 规则）。
        // 计划本身由 pac_plan::build() 生成（纯数据，可单元测试）。
        // ------------------------------------------------------------------
        // SPL 变体筛选需要知道设备是 eMMC 还是 UFS。Da_Info 里通常已有值；
        // 若未知（老 FDL2 只回 ACK）且 PAC 同时带了两份 SPL，读一次分区表来判定。
        int storage_type = static_cast<int>(Da_Info.dwStorageType);
        if (storage_type != pac_plan::kStorageNand && storage_type != pac_plan::kStorageEmmc &&
            storage_type != pac_plan::kStorageUfs && pac_has_both_spl_variants(unpac))
        {
            if (!io->ptable)
            {
                io->ptable = partition_list(io, &io->part_count);
            }
            storage_type = static_cast<int>(Da_Info.dwStorageType);
            DEG_LOG(I, "PAC flash: storage type detected from partition table: 0x%x", storage_type);
        }
        const std::vector<pac_plan::Item> plan = pac_plan::build(unpac, g_app_state.pacXml, storage_type);
        for (const pac_plan::Item& it : plan)
        {
            if (it.leftover)
            {
                DEG_LOG(W, "PAC plan: %s not listed in XML, flashing to %s in PAC order",
                        it.pac_id.c_str(), it.partition.c_str());
                continue;
            }
            switch (it.kind)
            {
            case pac_plan::Kind::Erase:
                DEG_LOG(I, "PAC plan: erase %s (ID=%s, Type=%s, Flag=0, CheckFlag=%d)",
                        it.partition.c_str(), it.pac_id.c_str(), it.info.type.c_str(),
                        it.info.checkFlag);
                break;
            case pac_plan::Kind::Unsupported:
                DEG_LOG(W, "PAC plan: ID=%s Type=%s Flag=0 needs generated content (RD writes it)",
                        it.pac_id.c_str(), it.info.type.c_str());
                break;
            case pac_plan::Kind::Skip:
                DEG_LOG(I, "PAC plan: skip %s (%s)", it.pac_id.c_str(), it.reason.c_str());
                break;
            case pac_plan::Kind::Flash:
                DEG_LOG(I, "PAC plan: flash %s -> %s (Type=%s, Flag=1)",
                        it.pac_id.c_str(), it.partition.c_str(), it.info.type.c_str());
                break;
            }
        }

        // 写入一个 Flag=1 条目（保持原有的分区选择 / NV 合并 / userdata 规则）
        auto flash_one = [&](const pac_plan::Item& it) -> void
        {
            if (it.pac_index < 0) return;

            const sprd_file_t& file = unpac.files[it.pac_index];
            const std::string& id = it.pac_id;
            const std::string partition = it.partition;

            if (partition.empty())
            {
                DEG_LOG(W, "PAC entry %s has no Block/partition mapping in XML, skipped.", id.c_str());
                return;
            }

            gui_idle_call([id, partition]() { bottom_bar_set_status("PAC Flashing: " + id + " -> " + partition); });

            if (!strcmp(partition.c_str(), "miscdata")) return;
            if (!strcmp(partition.c_str(), "prodnv")) return;

            if (!strcmp(partition.c_str(), "userdata") &&
                hasPartition(g_app_state.flash.selected_pacptable, "userdata"))
            {
                // userdata 不再默认擦除：PAC 里有镜像就正常写入，
                // 没有文件/文件为空时才跳过。
                std::string name = unpac.u16_to_u8(file.name, MAX_U16_SN);
                if (name.empty() || file.size == 0)
                {
                    DEG_LOG(I, "userdata image is empty or missing in PAC, skipping.");
                    return;
                }
#ifndef _WIN32
                std::string file_path = g_app_state.flash.pac_folder + "/" + name;
#else
                std::string file_path = g_app_state.flash.pac_folder + "\\" + name;
#endif
                EnhancedFile uf = oxfopen_enhanced(file_path.c_str(), "rb");
                if (!uf)
                {
                    DEG_LOG(W, "userdata image %s does not exist, skipping.", file_path.c_str());
                    return;
                }
                if (uf.seeko(0, SEEK_END) != 0)
                {
                    DEG_LOG(W, "userdata image %s: seek failed, skipping.", file_path.c_str());
                    return;
                }
                const auto userdata_size = uf.tello();
                uf.close();
                if (userdata_size <= 0)
                {
                    DEG_LOG(I, "userdata image %s is empty, skipping.", file_path.c_str());
                    return;
                }
                DEG_LOG(I, "Flashing userdata image: %s (%lld bytes)",
                        file_path.c_str(), static_cast<long long>(userdata_size));
                load_partition_unify(io, partition.c_str(), file_path.c_str(),
                                     blk_size ? blk_size : DEFAULT_BLK_SIZE, 0);
                return;
            }
            if (!strcmp(partition.c_str(), "userdata")) return;

            if (g_app_state.flash.isPacMergingNV && strstr(partition.c_str(), "fixnv1"))
            {
                const bool is_nr = strstr(partition.c_str(), "nr_fixnv1") != nullptr;
                const bool is_l = strstr(partition.c_str(), "l_fixnv1") != nullptr;
                const TempFile& old_file = is_nr ? g_app_state.pac.nr_fixnv1_file_path
                                                 : g_app_state.pac.l_fixnv1_file_path;

                if ((is_nr || is_l) && hasPartition(g_app_state.flash.selected_pacptable, partition))
                {
                    std::string name = unpac.u16_to_u8(file.name, MAX_U16_SN);
                    if (name.empty()) return;
                    if (old_file.empty())
                    {
                        DEG_LOG(W, "Failed to load old NV data for %s, skipping writing.", partition.c_str());
                        return;
                    }
                    if (get_nvlist_xml(io, g_app_state.flash.pac_xmlPath.c_str()))
                    {
                        size_t a_size = 0, b_size = 0, c_size = 0;
                        uint8_t* a = loadfile(old_file.c_str(), &a_size, 0);
#ifndef _WIN32
                        std::string file_path = g_app_state.flash.pac_folder + "/" + name;
#else
                        std::string file_path = g_app_state.flash.pac_folder + "\\" + name;
#endif
                        uint8_t* b = loadfile(file_path.c_str(), &b_size, 0);
                        if (!a || !b)
                        {
                            if (a) delete[](a);
                            if (b) delete[](b);
                            return;
                        }
                        uint8_t* c = (uint8_t*)malloc(a_size + b_size + MERGE_NV_SLACK);
                        merge_nv(io, a, a_size, b, b_size, c, a_size + b_size + MERGE_NV_SLACK, &c_size);
                        load_merged_nv(io, partition.c_str(), c, c_size,
                                       blk_size ? blk_size : DEFAULT_BLK_SIZE);
                        delete[](a);
                        delete[](b);
                        free(c);
                        return;
                    }
                }
            }

            if (strstr(partition.c_str(), "downloadnv"))
            {
                if (!hasPartition(g_app_state.flash.selected_pacptable, partition))
                {
                    DEG_LOG(W, "Partition %s not selected for flashing, skipping.", partition.c_str());
                    return;
                }
                std::string name = unpac.u16_to_u8(file.name, MAX_U16_SN);
                if (name.empty()) return;
#ifndef _WIN32
                std::string file_path = g_app_state.flash.pac_folder + "/" + name;
#else
                std::string file_path = g_app_state.flash.pac_folder + "\\" + name;
#endif
                DEG_LOG(I, "Flashing partition: %s", partition.c_str());
                if (g_app_state.flash.isPacMergingNV)
                {
                    if (g_app_state.pac.downloadnv_file_path.empty())
                    {
                        DEG_LOG(W, "Failed to load old NV data for downloadnv, skipping writing.");
                        return;
                    }
                    if (get_nvlist_xml(io, g_app_state.flash.pac_xmlPath.c_str()))
                    {
                        size_t a_size = 0, b_size = 0, c_size = 0;
                        uint8_t* a = loadfile(g_app_state.pac.downloadnv_file_path.c_str(), &a_size, 0);
                        uint8_t* b = loadfile(file_path.c_str(), &b_size, 0);
                        if (!a || !b)
                        {
                            DEG_LOG(W, "downloadnv image or old NV data is empty/missing, skipping.");
                            if (a) delete[] a;
                            if (b) delete[] b;
                        }
                        else
                        {
                            uint8_t* c = (uint8_t*)malloc(a_size + b_size + MERGE_NV_SLACK);
                            merge_nv(io, a, a_size, b, b_size, c, a_size + b_size + MERGE_NV_SLACK, &c_size);
                            load_merged_nv(io, partition.c_str(), c, c_size,
                                           blk_size ? blk_size : DEFAULT_BLK_SIZE);
                            delete[] a;
                            delete[] b;
                            free(c);
                        }
                    }
                    return;
                }
                EnhancedFile f = oxfopen_enhanced(file_path.c_str(), "rb");
                if (!f)
                {
                    DEG_LOG(E, "File %s does not exist.", file_path.c_str());
                    return;
                }
                if (f.seeko(0, SEEK_END) != 0)
                {
                    DEG_LOG(W, "File %s: seek failed, skipping.", file_path.c_str());
                    return;
                }
                const auto file_size = f.tello();
                f.close();
                if (file_size <= 0)
                {
                    DEG_LOG(I, "File %s is empty, skipping.", file_path.c_str());
                    return;
                }
                load_partition_unify(io, partition.c_str(), file_path.c_str(),
                                     blk_size ? blk_size : DEFAULT_BLK_SIZE, 0);
                return;
            }

            if (!hasPartition(g_app_state.flash.selected_pacptable, partition))
            {
                DEG_LOG(W, "Partition %s not selected for flashing, skipping.", partition.c_str());
                return;
            }

            DEG_LOG(I, "Flashing partition: %s", partition.c_str());
            std::string name = unpac.u16_to_u8(file.name, MAX_U16_SN);
            if (name.empty()) return;
#ifndef _WIN32
            std::string file_path = g_app_state.flash.pac_folder + "/" + name;
#else
            std::string file_path = g_app_state.flash.pac_folder + "\\" + name;
#endif
            EnhancedFile f = oxfopen_enhanced(file_path.c_str(), "rb");
            if (!f)
            {
                DEG_LOG(E, "File %s does not exist.", file_path.c_str());
                return;
            }
            if (f.seeko(0, SEEK_END) != 0)
            {
                DEG_LOG(W, "File %s: seek failed, skipping.", file_path.c_str());
                return;
            }
            const auto file_size = f.tello();
            f.close();
            if (file_size <= 0)
            {
                DEG_LOG(I, "File %s is empty, skipping.", file_path.c_str());
                return;
            }
            load_partition_unify(io, partition.c_str(), file_path.c_str(),
                                 blk_size ? blk_size : DEFAULT_BLK_SIZE, 0);
        };

        // 按 XML 顺序执行：擦除操作与写入交错执行，与 RD 一致。
        int dlnv_plan = -1;
        for (size_t k = 0; k < plan.size(); ++k)
        {
            const pac_plan::Item& it = plan[k];

            if (it.kind == pac_plan::Kind::Erase)
            {
                if (it.partition.empty())
                {
                    DEG_LOG(W, "PAC erase operation %s has no partition name, skipped.", it.pac_id.c_str());
                    continue;
                }
                if (!hasPartition(g_app_state.flash.selected_pacptable, it.partition))
                {
                    DEG_LOG(W, "Partition %s not selected, skip erase operation %s.",
                            it.partition.c_str(), it.pac_id.c_str());
                    continue;
                }
                get_partition_info(io, it.partition.c_str(), 0);
                if (!gPartInfo.size)
                {
                    DEG_LOG(W, "Partition %s does not exist, skip erase operation %s.",
                            it.partition.c_str(), it.pac_id.c_str());
                    continue;
                }
                DEG_LOG(OP, "PAC operation: erase partition %s", it.partition.c_str());
                erase_partition(io, it.partition.c_str(), g_app_state.flash.isCMethod);
                continue;
            }

            if (it.kind == pac_plan::Kind::Unsupported)
            {
                // RD 的“生成型操作”：内容由工具生成后写入 Block 指定的分区。
                if (!it.partition.empty() &&
                    !hasPartition(g_app_state.flash.selected_pacptable, it.partition))
                {
                    DEG_LOG(W, "Partition %s not selected, skip operation %s.",
                            it.partition.c_str(), it.pac_id.c_str());
                    continue;
                }
                uint64_t block_size = 0;
                if (!it.info.blocks.empty())
                {
                    block_size = strtoull(it.info.blocks.front().size.c_str(), nullptr, 0);
                }
                if (!pac_generate::run_operation(io, it.pac_id, it.info.type, it.partition,
                                                 block_size, g_app_state.flash.isCMethod,
                                                 g_app_state.flash.isPacWritePhaseCheck,
                                                 g_app_state.flash.isPacWriteIse,
                                                 g_app_state.flash.phasecheckIniPath))
                {
                    DEG_LOG(W, "PAC operation %s (Type=%s) needs generated content, unsupported, skipped.",
                            it.pac_id.c_str(), it.info.type.c_str());
                }
                continue;
            }

            if (it.kind != pac_plan::Kind::Flash) continue;

            // downloadnv 保持“最后写”的既有行为（NV 合并需要其它 NV 先落盘）。
            if (dlnv_plan < 0 && it.partition.find("downloadnv") != std::string::npos)
            {
                dlnv_plan = static_cast<int>(k);
                continue;
            }
            flash_one(it);
        }
        if (dlnv_plan >= 0) flash_one(plan[static_cast<size_t>(dlnv_plan)]);

        // APR：RD 在最后一个刷写步骤之后追加 WriteAPRInfo（负载是 PAC 文件时间）。
        if (g_app_state.flash.isPacWriteApr)
        {
            // 与 RD 一致：要求存在实际刷写/擦除操作（bExistFlashOpr），且 XML 的
            // <APR> 为 1（IsEnableAPR）。
            bool has_flash_op = false;
            for (const pac_plan::Item& op : plan)
            {
                if (op.kind != pac_plan::Kind::Skip)
                {
                    has_flash_op = true;
                    break;
                }
            }
            const std::string apr_prd = pac_plan::product_name(unpac, g_app_state.pacXml);
            if (!has_flash_op)
            {
                DEG_LOG(W, "APR: no flash operation in this PAC, skipped");
            }
            if (apr_prd.empty() || g_app_state.pacXml.getProductApr(apr_prd) == 0)
            {
                DEG_LOG(W, "APR: XML product does not enable <APR>, skipped");
            }
            else if (g_app_state.flash.pacFilePath.empty())
            {
                DEG_LOG(W, "APR: PAC file path unknown, skipped");
            }
            else if (!pac_generate::write_apr_info(io, g_app_state.flash.pacFilePath))
            {
                DEG_LOG(W, "APR: writing PAC build time failed, continuing");
            }
        }

        gui_idle_call([](){ bottom_bar_set_status("Rebooting..."); });
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
        std::this_thread::sleep_for(std::chrono::milliseconds(5000));
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
