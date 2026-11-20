/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "spd_calibration_structs.h"

// Windows 类型/宏的兼容定义（只在本移植文件里使用）。
typedef std::uint8_t  BYTE;
typedef std::uint16_t WORD;
typedef std::uint32_t DWORD;
typedef BYTE*         LPBYTE;
typedef int           BOOL;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifndef MAKEWORD
#define MAKEWORD(a, b) ((WORD)(((BYTE)(a)) | ((WORD)((BYTE)(b))) << 8))
#endif
#ifndef LOBYTE
#define LOBYTE(w) ((BYTE)((w) & 0xFF))
#endif
#ifndef HIBYTE
#define HIBYTE(w) ((BYTE)(((WORD)(w) >> 8) & 0xFF))
#endif
#ifndef CONV_SHORT
#define CONV_SHORT(w) ((WORD)(((w) >> 8) | (((w) & 0xFF) << 8)))
#endif

#ifndef MAKELONG
#define MAKELONG(a, b) ((DWORD)(((WORD)(a)) | ((DWORD)((WORD)(b))) << 16))
#endif
#ifndef LOWORD
#define LOWORD(l) ((WORD)((l) & 0xFFFF))
#endif
#ifndef HIWORD
#define HIWORD(l) ((WORD)(((DWORD)(l) >> 16) & 0xFFFF))
#endif
#ifndef CONV_DWORD
#define CONV_DWORD(dw) (MAKELONG(MAKEWORD(HIBYTE(HIWORD(dw)), LOBYTE(HIWORD(dw))), \
                                 MAKEWORD(HIBYTE(LOWORD(dw)), LOBYTE(LOWORD(dw)))))
#endif

typedef struct BT_CONFIG_T
{
    BYTE bt_addr[6];
    WORD xtal_dac;
} BT_CONFIG;

namespace spd_cali
{
// 对应 RD 的 [DownloadNV] GSMCaliVaPolicy，默认 0。
constexpr int kGsmCaliVaPolicy = 0;

// 校准 item 在“给定版本”下的期望长度（4 字节对齐）；未知版本返回 0。
std::size_t cali_expected_size(std::uint16_t item_id, std::uint16_t version);

void gsm_cali_ver_a_to_a(LPBYTE pFileBuf, DWORD dwOffsetFile, BOOL bFileBigEndian,
                         LPBYTE pModuBuf, DWORD dwOffsetModu, BOOL bModuBigEndian);
void gsm_cali_ver_b_to_b(LPBYTE pFileBuf, DWORD dwOffsetFile, BOOL bFileBigEndian,
                         LPBYTE pModuBuf, DWORD dwOffsetModu, BOOL bModuBigEndian);
void xtd_cali_ver3_to3(LPBYTE pFileBuf, DWORD dwOffsetFile, BOOL bFileBigEndian,
                       LPBYTE pModuBuf, DWORD dwOffsetModu, BOOL bModuBigEndian);
void xtd_cali_ver4_to4(LPBYTE pFileBuf, DWORD dwOffsetFile, BOOL bFileBigEndian,
                       LPBYTE pModuBuf, DWORD dwOffsetModu, BOOL bModuBigEndian);
void lte_cali_ver1_to1(LPBYTE pFileBuf, DWORD dwOffsetFile, BOOL bFileBigEndian,
                       LPBYTE pModuBuf, DWORD dwOffsetModu, BOOL bModuBigEndian);
}  // namespace spd_cali
