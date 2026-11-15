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
bool pac_flash(const char* folder);
std::string ExtractPartitionsWithTags(const std::string& xmlContent);
std::string FindFirstXMLFile(const std::string& folderPath);
std::string FindFDLInExtFolder(const char* folder, Stages mode);

// PAC XML 中没有文件数据的操作项（Flag=0）：
// 与闭源 pac 一样只记录并跳过，不发送空文件/擦除命令。
void pac_skip_empty_operations();
// 检查 Flag=1/CheckFlag=1 的必选文件在 PAC 中是否有数据。
bool pac_validate_required_files();
