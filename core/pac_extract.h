/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#pragma once
#include <cstddef>
#include <string>
#include <vector>
#include <cstdint>
#include "result.h"
#include "spd_protocol.h"  // for Stages enum
#include "Unpac.h"
#include "PacXMLParser.hpp"

struct _GtkWindow;

// 自动化任务包的分区兼容性检查结果。弹窗复用 PAC 分区表比较对话框。
enum class PackagePartitionCheckKind
{
    Match,
    MissingOnDevice,
    SizeMismatch
};

struct PackagePartitionCheckEntry
{
    std::string name;
    std::string device_size;
    std::string package_size;
    PackagePartitionCheckKind kind = PackagePartitionCheckKind::Match;
};

void show_package_partition_check_dialog(
    _GtkWindow* parent,
    const std::vector<PackagePartitionCheckEntry>& entries,
    std::size_t device_count,
    std::size_t package_count,
    std::size_t missing_count,
    std::size_t mismatch_count);

bool pac_extract(const char* fn, const char* folder);

// 离线计划：不连接设备，打印 PAC 头 / 文件表 / XML 顺序计划 / 摘要。
// 用于 tool mode 下对比两个 PAC（例如原包与重打包包）的差异。
void pac_print_offline_plan();
bool pac_flash(const char* folder);
std::string ExtractPartitionsWithTags(const std::string& xmlContent);
std::string FindFirstXMLFile(const std::string& folderPath);
std::string FindFDLInExtFolder(const char* folder, Stages mode);

// PAC XML 中没有文件数据的操作项（Flag=0）：
// 与闭源 pac 一样只记录并跳过，不发送空文件/擦除命令。
void pac_skip_empty_operations();
// 检查 Flag=1/CheckFlag=1 的必选文件在 PAC 中是否有数据。
bool pac_validate_required_files();

// ---------------------------------------------------------------------------
// PAC 刷写计划：与 ResearchDownload 对齐的“XML 清单”模型。
// 纯数据运算（不碰设备），便于单元测试；真正执行由 pac_flash() 负责。
// ---------------------------------------------------------------------------
namespace pac_plan
{
enum class Kind
{
    Flash,        // Flag=1 且有数据：写入 <Block id> 分区
    Erase,        // Flag=0 且 Type=EraseFlash*：擦除 <Block id> 分区
    Unsupported,  // Flag=0 且需要工具生成内容（PhaseCheck/ISE/…）
    Skip          // FDL、无数据、或未知条目
};

struct Item
{
    Kind kind = Kind::Skip;
    FileInfo info;             // XML 中对应的 File
    int pac_index = -1;        // PAC 文件表下标（-1 表示 XML 有、PAC 没有）
    std::string pac_id;
    std::string pac_name;
    uint64_t pac_size = 0;
    std::string partition;     // XML <Block id>
    std::string reason;        // Skip 的诊断原因
    bool leftover = false;     // PAC 里有数据但 XML 没列（兜底补刷）
};

// 设备存储类型（与 Da_Info.dwStorageType 的取值一致）
constexpr int kStorageNand = 0x101;
constexpr int kStorageEmmc = 0x102;
constexpr int kStorageUfs = 0x103;

// 按 PAC 头里的产品名在 XML 的 ProductList 中选中对应 Product，返回其名字；
// 匹配不上时返回空串（调用方回退到第一个 Scheme）。
std::string product_name(const PacFile& pac, const PacXMLParser& xml);

// 按 PAC 头里的产品名选 Scheme，返回 XML 原始顺序的 File 列表。
std::vector<FileInfo> xml_exec_order(const PacFile& pac, const PacXMLParser& xml);

// 生成执行计划：XML 顺序 + PAC 表匹配 + 类型分类 + 兜底补刷。
//
// storage_type：设备存储类型。当 PAC 同时提供 SPLLoaderEMMC / SPLLoaderUFS
// 两个 SPL 变体（它们都指向 splloader 分区）时，按该值只保留匹配的那个；
// 传 0（未知）或 0x101（NAND）时不做筛选，保持原行为。
std::vector<Item> build(const PacFile& pac, const PacXMLParser& xml, int storage_type = 0);
}  // namespace pac_plan
