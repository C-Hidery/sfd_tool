/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#include "pac_extract.h"
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
                    size_str = "DEFAULT, ERASE!!!";
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
    std::size_t mismatch_count)
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
    summary += _(", PAC partitions: ");
    summary += std::to_string(pac_count);
    summary += _(", Missing in PAC: ");
    summary += std::to_string(missing_count);
    summary += _(", Extra in PAC: ");
    summary += std::to_string(extra_count);
    summary += _(", Size mismatch: ");
    summary += std::to_string(mismatch_count);

    GtkWidget* summary_label = gtk_label_new(summary.c_str());
    gtk_widget_set_halign(summary_label, GTK_ALIGN_START);
    gtkBoxPackStart(content, summary_label, FALSE, FALSE, 0);

    GtkWidget* warning_label = gtk_label_new(
        _("PAC flashing has been aborted because the device and PAC partition tables are inconsistent."));
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
    sfd_row_view_add_text_column(tree, _("PAC Size"), 3, FALSE);
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
            status = _("Missing in PAC");
            break;
        case PartitionDiffKind::ExtraInPac:
            status = _("Extra in PAC");
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

bool pac_flash(const char* folder)
{
    bool isMergingNV_GUI = helper.getCheckButtonState(helper.getWidget("pac_merge_nv"));
    bool isRepartitionGUI = helper.getCheckButtonState(helper.getWidget("pac_repartition"));
    DEG_LOG(I, "PAC Flash: Merging NV=%s, Repartition=%s", isMergingNV_GUI ? "true" : "false", isRepartitionGUI ? "true" : "false");
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
    }
    else
    {
        std::cout << "Do you want to merge NV partition(Y/n)?" << std::endl;
        std::string n;
        std::getline(std::cin, n);
        g_app_state.flash.isPacMergingNV = (n == "Y" || n == "y");
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
        int dlnv_id = 0;
        if (g_app_state.flash.selected_ab < 0) select_ab(io);
        if (unpac.fileCount > 0)
        {
            for (int o = 0; o < unpac.fileCount; o++)
            {
                const sprd_file_t& file = unpac.files[o];
                std::string id = unpac.u16_to_u8(file.id, MAX_U16_SN);
                if (file.type == 0 || file.type == 0x101 || file.type == 2) continue;
                if (!strncmp(id.c_str(), "FDL", 3)) continue;
                std::string partition = g_app_state.pacXml.getPartitionByOperation(id);
                if (partition.empty()) continue;
                gui_idle_call([id, partition](){ bottom_bar_set_status("PAC Flashing: " + id + " -> " + partition); });
                if (!strcmp(partition.c_str(), "miscdata") && hasPartition(
                    g_app_state.flash.selected_pacptable, "miscdata"))
                {
                    continue;
                }
                if (!strcmp(partition.c_str(), "prodnv") && hasPartition(g_app_state.flash.selected_pacptable, "prodnv"))
                {
                    continue;
                }
                if (!strcmp(partition.c_str(), "userdata") && hasPartition(
                    g_app_state.flash.selected_pacptable, "userdata"))
                {
                    erase_partition(io, partition.c_str(), 0);
                    continue;
                }
                if (g_app_state.flash.isPacMergingNV)
                {
                    if (strstr(partition.c_str(), "fixnv1"))
                    {
                        if (strstr(partition.c_str(), "nr_fixnv1"))
                        {
                            if (hasPartition(g_app_state.flash.selected_pacptable,
                                             partition))
                            {
                                std::string name = unpac.u16_to_u8(file.name, MAX_U16_SN);
                                if (name.empty()) continue;
                                if (g_app_state.pac.nr_fixnv1_file_path.empty())
                                {
                                    DEG_LOG(W, "Failed to load old NV data for nr_fixnv1, skipping writing.");
                                    continue;
                                }
                                if (get_nvlist_xml(io, g_app_state.flash.pac_xmlPath.c_str()))
                                {
                                    size_t a_size = 0, b_size = 0, c_size = 0;
                                    uint8_t* a = loadfile(g_app_state.pac.nr_fixnv1_file_path.c_str(), &a_size, 0);
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
                                        continue;
                                    }
                                    uint8_t* c = (uint8_t*)malloc(a_size + b_size + MERGE_NV_SLACK);
                                    merge_nv(io, a, a_size, b, b_size, c, a_size + b_size + MERGE_NV_SLACK, &c_size);
                                    load_merged_nv(io, partition.c_str(), c, c_size,
                                                   blk_size ? blk_size : DEFAULT_BLK_SIZE);
                                    delete[](a);
                                    delete[](b);
                                    free(c);
                                    continue;
                                }
                                
                            }
                        }
                        else if (strstr(partition.c_str(), "l_fixnv1"))
                        {
                            if (g_app_state.pac.l_fixnv1_file_path.empty())
                            {
                                DEG_LOG(W, "Failed to load old NV data for l_fixnv1, skipping writing.");
                                continue;
                            }
                            if (hasPartition(g_app_state.flash.selected_pacptable,
                                             partition))
                            {
                                std::string name = unpac.u16_to_u8(file.name, MAX_U16_SN);
                                if (name.empty()) continue;
                                if (get_nvlist_xml(io, g_app_state.flash.pac_xmlPath.c_str()))
                                {
                                    size_t a_size = 0, b_size = 0, c_size = 0;
                                    uint8_t* a = loadfile(g_app_state.pac.l_fixnv1_file_path.c_str(), &a_size, 0);
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
                                        continue;
                                    }
                                    uint8_t* c = (uint8_t*)malloc(a_size + b_size + MERGE_NV_SLACK);
                                    merge_nv(io, a, a_size, b, b_size, c, a_size + b_size + MERGE_NV_SLACK, &c_size);
                                    load_merged_nv(io, partition.c_str(), c, c_size,
                                                   blk_size ? blk_size : DEFAULT_BLK_SIZE);
                                    delete[](a);
                                    delete[](b);
                                    free(c);
                                    continue;
                                }
                                
                            }
                        }
                    }
                }
                if (strstr(partition.c_str(), "downloadnv"))
                {
                    dlnv_id = o;
                    continue;
                }
                if (!partition.empty())
                {
                    if (hasPartition(g_app_state.flash.selected_pacptable, partition))
                    {
                        DEG_LOG(I, "Flashing partition: %s", partition.c_str());
                        std::string name = unpac.u16_to_u8(file.name, MAX_U16_SN);
                        if (name.empty()) continue;
#ifndef _WIN32
                        std::string file_path = g_app_state.flash.pac_folder + "/" + name;
#else
                        std::string file_path = g_app_state.flash.pac_folder + "\\" + name;
#endif
                        EnhancedFile f = oxfopen_enhanced(file_path.c_str(), "r");
                        if (!f)
                        {
                            DEG_LOG(E, "File %s does not exist.", file_path.c_str());
                            continue;
                        }
                        f.close();
                        load_partition_unify(io, partition.c_str(), file_path.c_str(),
                                             blk_size ? blk_size : DEFAULT_BLK_SIZE, 0);
                    }
                    else
                    {
                        DEG_LOG(W, "Partition %s not selected for flashing, skipping.", partition.c_str());
                    }
                }
            }

            if (dlnv_id)
            {
                const sprd_file_t& file = g_app_state.pacFile.files[dlnv_id];
                std::string id = unpac.u16_to_u8(file.id, 256);
                if (file.type == 0 || file.type == 0x101 || file.type == 2) return;
                if (!strncmp(id.c_str(), "FDL", 3)) return;
                std::string partition = g_app_state.pacXml.getPartitionByOperation(id);
                gui_idle_call([id, partition](){ bottom_bar_set_status("PAC Flashing: " + id + " -> " + partition); });
                if (!partition.empty())
                {
                    if (hasPartition(g_app_state.flash.selected_pacptable, partition))
                    {
                        DEG_LOG(I, "Flashing partition: %s", partition.c_str());
                        std::string name = unpac.u16_to_u8(file.name, MAX_U16_SN);
                        if (name.empty()) return;
                        if (g_app_state.flash.isPacMergingNV)
                        {
                            if (g_app_state.pac.downloadnv_file_path.empty())
                            {
                                DEG_LOG(W, "Failed to load old NV data for downloadnv, skipping writing.");
                            }
                            else
                            {
                                if (get_nvlist_xml(io, g_app_state.flash.pac_xmlPath.c_str()))
                                {
                                    size_t a_size = 0, b_size = 0, c_size = 0;
                                    uint8_t* a = loadfile(g_app_state.pac.downloadnv_file_path.c_str(), &a_size, 0);
#ifndef _WIN32
                                    std::string file_path = g_app_state.flash.pac_folder + "/" + name;
#else
                                    std::string file_path = g_app_state.flash.pac_folder + "\\" + name;
#endif
                                    uint8_t* b = loadfile(file_path.c_str(), &b_size, 0);
                                    if (!a || !b)
                                    {
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
                                
                            }
                        }
                        else
                        {
#ifndef _WIN32
                            std::string file_path = g_app_state.flash.pac_folder + "/" + name;
#else
                            std::string file_path = g_app_state.flash.pac_folder + "\\" + name;
#endif
                            load_partition_unify(io, partition.c_str(), file_path.c_str(),
                                                 blk_size ? blk_size : DEFAULT_BLK_SIZE, 0);
                        }
                    }
                    else
                    {
                        DEG_LOG(W, "Partition %s not selected for flashing, skipping.", partition.c_str());
                    }
                }
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
