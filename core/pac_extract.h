#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "result.h"
#include "spd_protocol.h"  // for Stages enum

// FDL 文件路径与基地址等 PAC 元信息抽象
struct FdlFiles {
    std::string fdl1;
    std::string fdl2;
};

struct FdlBaseAddrs {
    std::uint32_t fdl1_base = 0;
    std::uint32_t fdl2_base = 0;
    bool          has_fdl1  = false;
    bool          has_fdl2  = false;
};

// 基于现有 partition_t 的简单抽象，主要用于 PAC 解析结果
struct PacPartitionInfo {
    std::string   name;        // 分区名（对应 partition_t::name）
    std::string   image_path;  // 解包后的镜像路径（当前实现可留空）
    std::uint64_t size_bytes;  // 分区大小（字节，对应 partition_t::size）
};

// 统一的 PAC 解包 + 解析结果，用于后续 FlashService / console 复用
struct PacUnpackInfo {
    std::string pac_path;    // 原始 PAC 文件路径
    std::string unpack_dir;  // 解包输出目录

    FdlFiles        fdl_files;   // FDL1/FDL2 路径
    FdlBaseAddrs    fdl_addrs;   // FDL1/FDL2 Base 地址
    std::vector<PacPartitionInfo> partitions; // 从 XML 解析出来的分区列表
};

bool pac_extract(const char* fn, const char* folder);
bool pac_flash(const char* folder, spdio_t* io);
std::string ExtractPartitionsWithTags(const std::string& xmlContent);
std::string FindFirstXMLFile(const std::string& folderPath);
std::string FindFDLInExtFloder(const char* folder, Stages mode);

namespace sfd {

Result<void> pac_extract_result(const char* fn, const char* folder);

// 纯逻辑 PAC 解包 + 分析接口：不做 GUI 操作，由调用方决定如何展示错误
Result<PacUnpackInfo> pac_unpack_and_analyze(const char* pac_path,
                                             const char* out_folder);

} // namespace sfd
