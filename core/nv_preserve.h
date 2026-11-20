/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#pragma once

#include <cstddef>
#include <cstdint>

// 从 SPD Flash Tool 源码移植的 NV 合并辅助函数：
//   - find_item(): 对应 XFindNVOffsetEx / XFindNVOffset（含大小端探测）
//   - merge_nv_rd(): 对应 RD 的“读回旧分区 → 逐 item preserve → 写回”流程
// 注意：GSM/TD 校准的跨版本结构转换（GSM_VerAToA/VerBToB、XTD_VerXToY）尚未移植，
// 版本不一致时保留 PAC 的新值并告警，避免写坏校准数据。
namespace nv_preserve
{
constexpr uint16_t kGsmCaliItemId = 0x2;    // GSM_CALI_ITEM_ID
constexpr uint16_t kTdCaliItemId = 0x516;   // XTD_CALI_ITEM_ID
constexpr uint16_t kBtItemId = 0x191;       // BT_ITEM_ID

struct ItemLocation
{
    std::size_t offset = 0;
    std::size_t length = 0;
    bool big_endian = false;
};

// 在 NV 镜像中查找指定 item；找不到返回 false。
bool find_item(uint16_t id, const uint8_t* buf, std::size_t size, ItemLocation& out);
}  // namespace nv_preserve
