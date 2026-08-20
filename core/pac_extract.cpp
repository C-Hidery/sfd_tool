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
#include <iostream>    // for error output

#include "logging.h"  // 使用统一的 ERR_EXIT
#include "result.h"   // T2-02: Result/ErrorCode
#include "../pages/page_pac_flash.h"
#include "Unpac.h"

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
PacFile& unpac = g_app_state.pacFile;
bool pac_extract(const char* fn, const char* folder)
{
    auto *pacptable = NEWN partition_t[128];
    if (!pacptable) ERR_EXIT("Failed to allocate memory for partition table.\n");
    int pac_part_count = 0;
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
static inline bool iequals(const std::string& a, const std::string& b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(),
               [](char a, char b) {
                   return std::tolower(static_cast<unsigned char>(a)) ==
                          std::tolower(static_cast<unsigned char>(b));
               });
}
static bool hasPartition(const std::vector<std::string>& partitions, const std::string& partitionName)
{
    return std::find_if(partitions.begin(), partitions.end(),
        [&partitionName](const std::string& s) {
            return iequals(s, partitionName);
        }) != partitions.end();
}

bool pac_flash(spdio_t* io, const char* folder)
{
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
    if (isHelperInit)
    {
        if (!showConfirmDialogSyncInThread(GTK_WINDOW(helper.getWidget("main_window")),
            _("Confirm"), _("Do you really want to start flashing PAC firmware?")))
        {
            return false;
        }
    }
    g_app_state.flash.pac_xmlPath = xmlPath;
    if (isHelperInit)
    {
        g_app_state.flash.isPacMergingNV = showConfirmDialogSyncInThread(GTK_WINDOW(helper.getWidget("main_window")),
            _("Confirm"), _("Do you want to merge NV partition?"));
    }
    else
    {
        std::cout << "Do you want to merge NV partition(Y/n)?" << std::endl;
        std::string n;
        std::getline(std::cin, n);
        g_app_state.flash.isPacMergingNV = (n == "Y" || n == "y");
    }

    auto into_func = [io, xmlPath]() mutable
    {
        
        if (g_app_state.flash.isPacMergingNV)
        {
            auto pacptable = getSelectedPartitions(helper);
            get_partition_info(io, "nr_fixnv1", 1);
            if (gPartInfo.size && hasPartition(pacptable, gPartInfo.name))
            {
                g_app_state.pac.nr_fixnv1_mem = dump_partition_to_mem(io, gPartInfo.name, 0, gPartInfo.size, blk_size ? blk_size : DEFAULT_BLK_SIZE , &g_app_state.pac.nr_fixnv1_mem_size);
            }
            get_partition_info(io, "l_fixnv1", 1);
            if (gPartInfo.size && hasPartition(pacptable, gPartInfo.name))
            {
                g_app_state.pac.l_fixnv1_mem = dump_partition_to_mem(io, gPartInfo.name, 0, gPartInfo.size, blk_size ? blk_size : DEFAULT_BLK_SIZE , &g_app_state.pac.l_fixnv1_mem_size);
            }
            get_partition_info(io, "downloadnv", 1);
            if (gPartInfo.size && hasPartition(pacptable, gPartInfo.name))
            {
                g_app_state.pac.downloadnv_mem = dump_partition_to_mem(io, gPartInfo.name, 0, gPartInfo.size, blk_size ? blk_size : DEFAULT_BLK_SIZE , &g_app_state.pac.downloadnv_mem_size);
            }
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
            std::string response;
            std::getline(std::cin, response);
            i_is = (response == "y" || response == "Y");
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

        load_partitions(io, "pac_unpack_output", blk_size ? blk_size : DEFAULT_BLK_SIZE , g_app_state.flash.selected_ab, 0);
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
