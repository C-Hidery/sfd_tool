/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "result.h"
#include "spd_protocol.h"  // for Stages enum

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
