/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#ifndef PAC_GENERATE_H
#define PAC_GENERATE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "../common.h"

// ResearchDownload 里有一类 Flag=0 的条目：它们不需要 PAC 里的文件数据，
// 而是由工具在刷机时“生成”内容再写入某个分区（例如 PhaseCheck -> miscdata）。
// 该模块集中实现这些生成型操作，避免把生成逻辑散落到 pac_extract.cpp。
namespace pac_generate
{
// PhaseCheck 生产信息区大小（RD: PRODUCTION_INFO_SIZE = 8K）
constexpr std::size_t kProductionInfoSize = 0x2000;

// 构造 8K PhaseCheck 缓冲区：整体 0xFF 填充，头部放 SP15 结构。
// RD 会用 PhaseCheck.ini + 设备 SN 填充站点信息；在缺少这些输入时，
// 这里写入“单站点 DOWNLOAD、空 SN”的合法结构（与 RD 的默认值一致）。
std::vector<uint8_t> build_phasecheck_buffer(const std::string& sn1 = std::string(),
                                             const std::string& sn2 = std::string());

// 从设备读回的 8K PhaseCheck 数据里提取 SN（移植自 RD 的
// CPhaseCheckBuild::FindSnFrom8K：SP05 取 header.SN，SP09/SP15 从 8K 末尾
// 反向找最后一个 magic 匹配的块，取其 SN1）。
std::string find_phasecheck_sn(const std::vector<uint8_t>& data);

// 处理一个生成型操作。
// 返回 true 表示该操作已被本模块识别并处理（是否真正写入见日志）；
// 返回 false 表示本模块不认识该操作，调用方继续走“不支持”告警。
bool run_operation(spdio_t* io,
                   const std::string& op_id,
                   const std::string& type,
                   const std::string& partition,
                   uint64_t block_size,
                   int CMethod);
}  // namespace pac_generate

#endif  // PAC_GENERATE_H
