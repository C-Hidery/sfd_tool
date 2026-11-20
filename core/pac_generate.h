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

// PhaseCheck 的格式版本（对应 RD 的 SP05/SP09/SP15 结构）。
enum class PhaseCheckFormat
{
    Sp05,
    Sp09,
    Sp15
};

// 从设备读回的 8K 数据里判断产测格式；识别不出时返回 Sp09。
PhaseCheckFormat detect_phasecheck_format(const std::vector<uint8_t>& data);

// 构造 8K PhaseCheck 缓冲区：整体 0xFF 填充，头部放 RD 的结构。
// 站点表固定使用工具内置的 PhaseCheck.ini 默认值（SP09 / PASS VALUE 0 / 7 站点，
// 取自 RD 发行版），不受用户为 ISE 选择的 ini 影响。
// format 默认 Sp09；刷写时由设备现有 phasecheck 的 magic 决定（见 detect_phasecheck_format）。
std::vector<uint8_t> build_phasecheck_buffer(const std::string& sn1 = std::string(),
                                             const std::string& sn2 = std::string(),
                                             PhaseCheckFormat format = PhaseCheckFormat::Sp09);

// 用户为 ISE 选择的 ini 是否可用（可读且含 [ISE]/[ISE_DATA] 段）。
bool ise_ini_usable(const std::string& ini_path);

// ---- ISE ----
// 从 ini 的 [ISE] / [ISE_DATA] 段生成 ISE 负载，支持：
//   Data = 十六进制串
//   File = 相对于 ini 的文件（也可以是绝对路径）
//   Size = 十进制/0x 十六进制字节数（无 Data/File 时按 0xFF 填充）
// 返回 false 表示 ini 不可用或该段没有可用值（调用方保持“空壳+警告”）。
bool build_ise_payload(const std::string& ini_path,
                       const std::string& op_id,
                       std::vector<uint8_t>& out,
                       std::string& err);

// 从设备读回的 8K PhaseCheck 数据里提取 SN（移植自 RD 的
// CPhaseCheckBuild::FindSnFrom8K：SP05 取 header.SN，SP09/SP15 从 8K 末尾
// 反向找最后一个 magic 匹配的块，取其 SN1）。
std::string find_phasecheck_sn(const std::vector<uint8_t>& data);

// 处理一个生成型操作。
// 返回 true 表示该操作已被本模块识别并处理（是否真正写入见日志）；
// 返回 false 表示本模块不认识该操作，调用方继续走“不支持”告警。
// write_phasecheck：是否生成并写入 PhaseCheck（对应 PAC 页的勾选项）。
bool run_operation(spdio_t* io,
                   const std::string& op_id,
                   const std::string& type,
                   const std::string& partition,
                   uint64_t block_size,
                   int CMethod,
                   bool write_phasecheck,
                   bool write_ise,
                   const std::string& ini_path);

// ---- APR ----
// unix 时间 -> Windows FILETIME（100ns，自 1601-01-01），用于 APR 负载。
uint64_t apr_filetime_from_unix(uint64_t sec, uint32_t nsec);
// 生成 8 字节小端 FILETIME 负载（取 PAC 文件的修改时间）。
std::vector<uint8_t> build_apr_payload(const std::string& pac_path);
// 发送 BSL_CMD_WRITE_APR_INFO（0x22，把 PAC 构建时间写入 miscdata）。
bool write_apr_info(spdio_t* io, const std::string& pac_path);
}  // namespace pac_generate

#endif  // PAC_GENERATE_H
