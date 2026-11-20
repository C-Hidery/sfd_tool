/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#include "nv_preserve.h"

#include "../common.h"
#include "spd_calibration.h"

#include <cstring>
#include <ctime>

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

bool starts_with_ci(const char* s, const char* prefix)
{
    if (!s || !prefix)
    {
        return false;
    }
    while (*prefix)
    {
        char a = *s++;
        char b = *prefix++;
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b || a == '\0')
        {
            return false;
        }
    }
    return true;
}

enum class CaliKind
{
    Gsm,
    Td
};

// 对应 RD 的 GSMCaliPreserve / XTDCaliPreserve：
//   - 版本一致时按结构体做（必要时端序转换）整体旧值覆盖；
//   - 版本不一致 → 失败（RD 返回 ERR_STRUCT_VER_NOT_MATCH）。
bool preserve_cali(CaliKind kind, uint16_t item_id, uint8_t* new_buf, size_t new_size,
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
        DEG_LOG(E, "merge_nv_rd: cali item 0x%x version differs (old=0x%x new=0x%x)",
                item_id, old_ver, new_ver);
        return false;
    }

    const bool is_td = (kind == CaliKind::Td);
    const std::size_t expected = spd_cali::cali_expected_size(is_td, old_ver);
    if (expected != 0 && new_it.length < expected)
    {
        DEG_LOG(E, "merge_nv_rd: cali item 0x%x length %zu < expected %zu",
                item_id, new_it.length, expected);
        return false;
    }

    if (!is_td)
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
    else
    {
        if (old_ver == 3)
        {
            spd_cali::xtd_cali_ver3_to3(new_buf, static_cast<DWORD>(new_it.offset),
                                        new_it.big_endian, const_cast<uint8_t*>(old_buf),
                                        static_cast<DWORD>(old_it.offset), old_it.big_endian);
            std::time_t now = std::time(nullptr);
            const uint32_t ts = static_cast<uint32_t>(now);
            std::memcpy(new_buf, &ts, sizeof(ts));  // RD: 更新 NV 头时间戳
            return true;
        }
        if (old_ver == 4)
        {
            spd_cali::xtd_cali_ver4_to4(new_buf, static_cast<DWORD>(new_it.offset),
                                        new_it.big_endian, const_cast<uint8_t*>(old_buf),
                                        static_cast<DWORD>(old_it.offset), old_it.big_endian);
            std::time_t now = std::time(nullptr);
            const uint32_t ts = static_cast<uint32_t>(now);
            std::memcpy(new_buf, &ts, sizeof(ts));  // RD: 更新 NV 头时间戳
            return true;
        }
    }

    // Ver C / Ver 5 或未知版本：整体拷贝旧值。
    std::memcpy(new_buf + new_it.offset, old_buf + old_it.offset, new_it.length);
    return true;
}

// 通用 preserve（对应 XPreserveNVItem 的“整项拷贝”分支）。
bool preserve_plain(uint16_t item_id, uint8_t* new_buf, size_t new_size,
                    const uint8_t* old_buf, size_t old_size, bool continue_missing)
{
    nv_preserve::ItemLocation old_it;
    if (!nv_preserve::find_item(item_id, old_buf, old_size, old_it))
    {
        if (continue_missing)
        {
            DEG_LOG(W, "merge_nv_rd: item 0x%x not in old image (Continue=1), skipped", item_id);
            return true;
        }
        DEG_LOG(E, "merge_nv_rd: item 0x%x not in old image (Continue=0)", item_id);
        return false;
    }

    nv_preserve::ItemLocation new_it;
    if (!nv_preserve::find_item(item_id, new_buf, new_size, new_it))
    {
        DEG_LOG(E, "merge_nv_rd: item 0x%x not in new image", item_id);
        return false;
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
            break;
        }
    }

    return false;
}

// RD 风格合并：以新镜像为底，按 <NVItem> 的名字前缀选择 preserve 方式，再写回。
// 返回 false 表示失败（调用方应中止刷机）。
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

    if (!io || !a || a_size < 4)
    {
        return true;
    }

    // RD 以 XML <NVItem> 为单位分派（名字前缀决定合并方式）。
    if (io->nv_names && io->nv_name_count > 0)
    {
        for (int i = 0; i < io->nv_name_count; ++i)
        {
            uint16_t item_id = io->nv_names[i].id;
            const char* name = io->nv_names[i].name;
            if (io->nvid_list && !io->nvid_list[item_id])
            {
                continue;
            }

            if (starts_with_ci(name, "Calibration"))
            {
                if (item_id == 0xFFFF) item_id = nv_preserve::kGsmCaliItemId;
                if (!preserve_cali(CaliKind::Gsm, item_id, c, *c_size, a, a_size))
                {
                    return false;
                }
                continue;
            }
            if (starts_with_ci(name, "TD_Calibration"))
            {
                if (item_id == 0xFFFF) item_id = nv_preserve::kTdCaliItemId;
                if (!preserve_cali(CaliKind::Td, item_id, c, *c_size, a, a_size))
                {
                    return false;
                }
                continue;
            }

            // LTE_Calibration / IMEI / MMITest 等：RD 走通用整项拷贝。
            // XPreserveNVItem 会跳过 GSM/TD 校准 ID（归专用函数处理）。
            if (item_id == nv_preserve::kGsmCaliItemId || item_id == nv_preserve::kTdCaliItemId)
            {
                continue;
            }

            const bool cont = io->nv_continue && io->nv_continue[item_id];
            if (!preserve_plain(item_id, c, *c_size, a, a_size, cont))
            {
                return false;
            }
        }
        return true;
    }

    // cfg 模式（没有 <NVItem> 名字）：按 ID 通用拷贝，保持旧行为。
    if (!io->nvid_list)
    {
        return true;
    }
    for (int id = 0; id < 0x10000; ++id)
    {
        if (!io->nvid_list[id])
        {
            continue;
        }
        const bool cont = io->nv_continue && io->nv_continue[id];
        if (!preserve_plain(static_cast<uint16_t>(id), c, *c_size, a, a_size, cont))
        {
            return false;
        }
    }
    return true;
}
