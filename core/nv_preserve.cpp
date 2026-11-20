/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#include "nv_preserve.h"

#include "../common.h"
#include "spd_calibration.h"

#include <cstring>

namespace
{
uint16_t read16(const uint8_t* p, bool be)
{
    uint16_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    if (be)
    {
        v = static_cast<uint16_t>((v >> 8) | (v << 8));
    }
    return v;
}

uint32_t read32(const uint8_t* p)
{
    uint32_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

// 对应 RD 的 GSMCaliPreserve / XTDCaliPreserve（只实现“版本一致”的拷贝路径）。
bool preserve_cali(uint16_t item_id, uint8_t* new_buf, size_t new_size,
                   const uint8_t* old_buf, size_t old_size)
{
    nv_preserve::ItemLocation new_it;
    nv_preserve::ItemLocation old_it;
    if (!nv_preserve::find_item(item_id, new_buf, new_size, new_it))
    {
        DEG_LOG(E, "merge_nv_rd: cali item 0x%x not in new image", item_id);
        return false;
    }
    if (!nv_preserve::find_item(item_id, old_buf, old_size, old_it))
    {
        DEG_LOG(E, "merge_nv_rd: cali item 0x%x not in old image", item_id);
        return false;
    }
    if (old_it.length != new_it.length)
    {
        DEG_LOG(E, "merge_nv_rd: cali item 0x%x length mismatch (old=%zu new=%zu)",
                item_id, old_it.length, new_it.length);
        return false;
    }
    if (old_it.big_endian != new_it.big_endian)
    {
        DEG_LOG(E, "merge_nv_rd: cali item 0x%x endian mismatch", item_id);
        return false;
    }

    const uint16_t old_ver = read16(old_buf + old_it.offset, old_it.big_endian);
    const uint16_t new_ver = read16(new_buf + new_it.offset, new_it.big_endian);
    if (old_ver != new_ver)
    {
        // RD 在这种情况下返回 ERR_STRUCT_VER_NOT_MATCH；保持新值并告警。
        DEG_LOG(W, "merge_nv_rd: cali item 0x%x version differs (old=0x%x new=0x%x), "
                   "keeping PAC value (RD would fail the merge)",
                item_id, old_ver, new_ver);
        return false;
    }

    const std::size_t expected = spd_cali::cali_expected_size(item_id, old_ver);
    if (expected != 0 && new_it.length < expected)
    {
        DEG_LOG(E, "merge_nv_rd: cali item 0x%x length %zu < expected %zu",
                item_id, new_it.length, expected);
        return false;
    }

    if (item_id == nv_preserve::kGsmCaliItemId)
    {
        if (old_ver == 0xFF0A)
        {
            spd_cali::gsm_cali_ver_a_to_a(new_buf, static_cast<DWORD>(new_it.offset),
                                          new_it.big_endian, const_cast<uint8_t*>(old_buf),
                                          static_cast<DWORD>(old_it.offset), old_it.big_endian);
            return true;
        }
        if (old_ver == 0xFF0B)
        {
            spd_cali::gsm_cali_ver_b_to_b(new_buf, static_cast<DWORD>(new_it.offset),
                                          new_it.big_endian, const_cast<uint8_t*>(old_buf),
                                          static_cast<DWORD>(old_it.offset), old_it.big_endian);
            return true;
        }
    }
    else if (item_id == nv_preserve::kTdCaliItemId)
    {
        if (old_ver == 3)
        {
            spd_cali::xtd_cali_ver3_to3(new_buf, static_cast<DWORD>(new_it.offset),
                                        new_it.big_endian, const_cast<uint8_t*>(old_buf),
                                        static_cast<DWORD>(old_it.offset), old_it.big_endian);
            return true;
        }
        if (old_ver == 4)
        {
            spd_cali::xtd_cali_ver4_to4(new_buf, static_cast<DWORD>(new_it.offset),
                                        new_it.big_endian, const_cast<uint8_t*>(old_buf),
                                        static_cast<DWORD>(old_it.offset), old_it.big_endian);
            return true;
        }
    }

    // Ver C / Ver 5 或未知版本：整体拷贝旧值。
    std::memcpy(new_buf + new_it.offset, old_buf + old_it.offset, new_it.length);
    return true;
}
}  // namespace

bool nv_preserve::find_item(uint16_t id, const uint8_t* buf, std::size_t size,
                            nv_preserve::ItemLocation& out)
{
    if (!buf || size < 4)
    {
        return false;
    }

    // 先按小端解析；失败再按大端重试（对应 XCheckNVStructEx 的 endian 探测）。
    for (int pass = 0; pass < 2; ++pass)
    {
        const bool be = (pass == 1);
        std::size_t pos = 4;
        if (read32(buf) == 0x4e56)
        {
            pos += 0x200;
        }

        bool valid = true;
        while (pos + 4 <= size)
        {
            const uint16_t type = read16(buf + pos, be);
            const uint16_t length = read16(buf + pos + 2, be);
            pos += 4;

            if (type == 0xFFFF)
            {
                break;
            }
            if (length == 0 || pos + length > size)
            {
                valid = false;
                break;
            }
            if (type == id)
            {
                out.offset = pos;
                out.length = length;
                out.big_endian = be;
                return true;
            }

            pos += length;
            pos = (pos + 3) & ~static_cast<std::size_t>(3);
        }

        if (valid)
        {
            // 解析成功但没找到，无需再试另一种端序。
            break;
        }
    }

    return false;
}

// RD 风格的合并：以新镜像为底，把旧 item 逐个 preserve 进去（不追加旧镜像独有 item）。
// 返回 false 表示合并失败（与 RD 一致，调用方应中止刷机并报错）。
bool merge_nv_rd(spdio_t* io, const uint8_t* a, size_t a_size, const uint8_t* b,
                 size_t b_size, uint8_t* c, size_t c_cap, size_t* c_size)
{
    if (c_size)
    {
        *c_size = 0;
    }
    if (!c || !b || b_size < 4)
    {
        DEG_LOG(E, "merge_nv_rd: invalid buffer");
        return false;
    }
    if (c_cap < b_size)
    {
        DEG_LOG(E, "merge_nv_rd: output buffer too small (%zu < %zu bytes)", c_cap, b_size);
        return false;
    }

    std::memcpy(c, b, b_size);
    *c_size = b_size;

    // 没有 NV 备份列表（XML 里 backup=0 或没有 NVItem）视为“不合并”，不算失败。
    if (!io || !io->nvid_list || !a || a_size < 4)
    {
        return true;
    }

    for (int id = 0; id < 0x10000; ++id)
    {
        if (!io->nvid_list[id])
        {
            continue;
        }

        const uint16_t item_id = static_cast<uint16_t>(id);
        nv_preserve::ItemLocation old_it;
        if (!nv_preserve::find_item(item_id, a, a_size, old_it))
        {
            // RD: bContinue=1 时容忍（返回 ERR_NONE），否则 ERR_NOT_FIND_PARAM_IN_MODULE。
            if (io->nv_continue && io->nv_continue[id])
            {
                DEG_LOG(W, "merge_nv_rd: item 0x%x not in old image (Continue=1), skipped", item_id);
                continue;
            }
            DEG_LOG(E, "merge_nv_rd: item 0x%x not in old image (Continue=0)", item_id);
            return false;
        }

        nv_preserve::ItemLocation new_it;
        if (!nv_preserve::find_item(item_id, c, *c_size, new_it))
        {
            DEG_LOG(E, "merge_nv_rd: item 0x%x not in new image", item_id);
            return false;
        }

        if (item_id == nv_preserve::kGsmCaliItemId || item_id == nv_preserve::kTdCaliItemId)
        {
            if (!preserve_cali(item_id, c, *c_size, a, a_size))
            {
                return false;
            }
            continue;
        }

        if (item_id == nv_preserve::kBtItemId && old_it.big_endian != new_it.big_endian)
        {
            DEG_LOG(E, "merge_nv_rd: BT item endian mismatch");
            return false;
        }

        if (old_it.length != new_it.length)
        {
            DEG_LOG(E, "merge_nv_rd: item 0x%x length mismatch (old=%zu new=%zu)",
                    item_id, old_it.length, new_it.length);
            return false;
        }

        std::memcpy(c + new_it.offset, a + old_it.offset, new_it.length);
    }

    return true;
}
