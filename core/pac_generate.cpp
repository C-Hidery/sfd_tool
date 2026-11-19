/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#include "pac_generate.h"

#include <cstdio>
#include <cstring>

#include "file_io.h"
#include "logging.h"

namespace pac_generate
{
namespace
{
// SP15 phase check 结构（与 RD 的 phasecheck/phdef.h 保持一致，pack(4)，共 492 字节）
#pragma pack(push, 4)
struct Sp15PhaseCheck
{
    uint32_t magic;             // "SP15"
    char sn1[64];
    char sn2[64];
    int32_t station_num;
    char station_name[20][15];
    uint8_t reserved[13];
    uint8_t sign_flag;          // PASS_0_FAIL_1 = 0x01（RD 默认）
    char last_fail_description[32];
    uint32_t test_sign;
    uint32_t item;
};
#pragma pack(pop)

constexpr uint32_t kSp15Magic = 0x53503135u;  // 'S''P''1''5'
static_assert(sizeof(Sp15PhaseCheck) == 492, "SP15 phase check layout mismatch");

// 把生成的缓冲区写入分区：先落临时文件，再走普通的“按分区名下载”。
bool write_buffer_to_partition(spdio_t* io, const std::string& partition,
                               const std::vector<uint8_t>& data, int CMethod)
{
    TempFile tmp(make_temp_file_path(partition.empty() ? "pacgen" : partition.c_str()));
    if (tmp.empty())
    {
        DEG_LOG(E, "生成型操作: 创建临时文件失败");
        return false;
    }

    EnhancedFile f = oxfopen_enhanced(tmp.c_str(), "wb");
    if (!f)
    {
        DEG_LOG(E, "生成型操作: 打开临时文件失败: %s", tmp.c_str());
        return false;
    }
    const std::size_t written = f.write(data.data(), 1, data.size());
    f.close();
    if (written != data.size())
    {
        DEG_LOG(E, "生成型操作: 写入临时文件失败 (%zu / %zu)", written, data.size());
        return false;
    }

    const unsigned step = blk_size ? blk_size : DEFAULT_BLK_SIZE;
    load_partition(io, partition.c_str(), tmp.c_str(), step, CMethod);
    return true;
}
}  // namespace

std::vector<uint8_t> build_phasecheck_buffer(const std::string& sn1, const std::string& sn2)
{
    // RD: memset(lpData, 0xFF, 8K) 后把结构体放在缓冲区起始处
    std::vector<uint8_t> buf(kProductionInfoSize, 0xFF);

    Sp15PhaseCheck pc{};
    pc.magic = kSp15Magic;
    std::snprintf(pc.sn1, sizeof(pc.sn1), "%s", sn1.c_str());
    std::snprintf(pc.sn2, sizeof(pc.sn2), "%s", sn2.c_str());

    // RD 在 PhaseCheck.ini 缺失时的默认：单站点 DOWNLOAD、PASS_0_FAIL_1、
    // iTestSign/iItem 全 1、LastFailDescription = "PASS"。
    pc.station_num = 1;
    std::snprintf(pc.station_name[0], sizeof(pc.station_name[0]), "DOWNLOAD");
    pc.sign_flag = 0x01;
    pc.test_sign = 0xFFFFFFFFu;
    pc.item = 0xFFFFFFFFu;
    std::snprintf(pc.last_fail_description, sizeof(pc.last_fail_description), "PASS");

    std::memcpy(buf.data(), &pc, sizeof(pc));
    return buf;
}

bool run_operation(spdio_t* io, const std::string& op_id, const std::string& type,
                   const std::string& partition, uint64_t block_size, int CMethod)
{
    // ---- PhaseCheck：生成 8K 生产信息并写 miscdata（RD 行为）----
    if (!my_stricmp(op_id.c_str(), "PhaseCheck"))
    {
        if (partition.empty())
        {
            DEG_LOG(W, "PhaseCheck: XML 中没有 Block/分区，跳过");
            return true;
        }

        std::size_t size = kProductionInfoSize;
        if (block_size > 0 && block_size <= kProductionInfoSize)
        {
            // RD: Block Size 为 0 或超过 8K 时按 8K；否则按 Block Size。
            size = static_cast<std::size_t>(block_size);
        }

        std::vector<uint8_t> buf = build_phasecheck_buffer();
        buf.resize(size, 0xFF);

        get_partition_info(io, partition.c_str(), 1);
        if (!gPartInfo.size)
        {
            DEG_LOG(W, "PhaseCheck: 分区 %s 不存在，跳过", partition.c_str());
            return true;
        }

        DEG_LOG(OP, "生成 PhaseCheck：%zu 字节 -> %s (Type=%s)", buf.size(),
                partition.c_str(), type.c_str());
        if (!write_buffer_to_partition(io, partition, buf, CMethod))
        {
            DEG_LOG(E, "PhaseCheck: 写入 %s 失败", partition.c_str());
        }
        return true;
    }

    // ---- ISE / ISE_DATA ----
    // RD 侧这两个条目同样没有文件数据，具体内容由 BMFileType.ini 的操作序列
    // 决定（对应源码不在反汇编/泄漏范围内）。为避免向安全相关分区写入臆造内容，
    // 这里只做明确告警，不写设备。
    if (!my_stricmp(op_id.c_str(), "ISE") || !my_stricmp(op_id.c_str(), "ISE_DATA"))
    {
        DEG_LOG(W,
                "%s (Type=%s, Block=%s)：RD 的写入内容由 BMFileType.ini 决定，"
                "当前没有可靠依据，未写入。",
                op_id.c_str(), type.c_str(), partition.c_str());
        return true;
    }

    return false;
}
}  // namespace pac_generate
