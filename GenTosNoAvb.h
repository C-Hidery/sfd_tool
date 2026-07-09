/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <vector>
#include <algorithm>

//额外FILE库
#include "core/file_io.h"

// 4. 定义结构体（与 bsp_sign_fxxker.c 一致）
typedef struct {
    uint32_t mMagicNum;
    uint32_t mVersion;
    uint8_t  mPayloadHash[32];
    uint64_t mImgAddr;
    uint32_t mImgSize;
    uint32_t is_packed;
    uint32_t mFirmwareSize;
    uint32_t mFirmwareOff;
    uint8_t  reserved[448];
} sys_img_header;

typedef struct {
    uint8_t  magic[8];
    uint32_t header_version_major;
    uint32_t header_version_minor;
    uint64_t payload_size;
    uint64_t payload_offset;
    uint64_t cert_size;
    uint64_t cert_offset;
    uint64_t priv_size;
    uint64_t priv_offset;
    uint64_t cert_dbg_prim_size;
    uint64_t cert_dbg_prim_offset;
    uint64_t cert_dbg_developer_size;
    uint64_t cert_dbg_developer_offset;
} sprdsignedimageheader;

class TosPatcher {
private:
    // 加载文件到内存（只读）
    static uint8_t* loadfile(const char* fn, size_t* num) {
        size_t n, j = 0;
        uint8_t* buf = nullptr;
        EnhancedFile fi = oxfopen_enhanced(fn, "rb");
        if (fi) {
            fi.seek(0, SEEK_END);
            n = fi.tell();
            if (n) {
                fi.seek(0, SEEK_SET);
                buf = (uint8_t*)malloc(n);
                if (buf) j = fi.read(buf, 1, n);
            }
            fi.close();
        }
        if (num) *num = j;
        return buf;
    }

    // 计算固件的有效大小（去掉末尾填充）
    static size_t get_effective_size(uint8_t* mem, size_t file_size) {
        if (*(uint32_t*)mem != 0x42544844) return 0;
        uint32_t offset = *(uint32_t*)&mem[0x30];
        if (offset == 0) return 0;
        if (offset + 0x260 >= file_size) return 0;

        size_t size = 0;
        if (*(uint32_t*)&mem[offset + 0x200 + 0x50] && *(uint32_t*)&mem[offset + 0x200 + 0x58])
            size = *(uint32_t*)&mem[offset + 0x200 + 0x50] + *(uint32_t*)&mem[offset + 0x200 + 0x58];
        else if (*(uint32_t*)&mem[offset + 0x200 + 0x30] && *(uint32_t*)&mem[offset + 0x200 + 0x38])
            size = *(uint32_t*)&mem[offset + 0x200 + 0x30] + *(uint32_t*)&mem[offset + 0x200 + 0x38];
        else if (*(uint32_t*)&mem[offset + 0x200 + 0x20] && *(uint32_t*)&mem[offset + 0x200 + 0x28])
            size = *(uint32_t*)&mem[offset + 0x200 + 0x20] + *(uint32_t*)&mem[offset + 0x200 + 0x28];
        else
            size = offset + 0x200;
        return size;
    }

    // 对内存中的固件进行 AVB 禁用补丁（返回完整有效区域，包括尾部）
    static uint8_t* patch_in_memory(uint8_t* orig_buf, size_t orig_size, size_t* out_size) {
        size_t eff_size = get_effective_size(orig_buf, orig_size);
        if (eff_size == 0) return nullptr;

        uint8_t* mem = (uint8_t*)malloc(eff_size);
        if (!mem) return nullptr;
        memcpy(mem, orig_buf, eff_size);

        // 以下完全复制原 GenTosNoAvb.h 中的扫描/修改逻辑
        size_t pmov[3] = {0};
        size_t last_start_pos = 0, start_pos = 0;
        int mov_count = 0;
        for (size_t i = 0x200; i < eff_size; i += 4) {
            uint32_t current = *(uint32_t*)&mem[i];
            if (current == 0xD65F03C0 || (current & 0xFF00FFFF) == 0xA8007BFD) {
                last_start_pos = 0;
                start_pos = 0;
            } else if ((current & 0xFF00FFFF) == 0xA9007BFD) {
                if (start_pos) last_start_pos = start_pos;
                start_pos = i;
            } else if (start_pos) {
                int ldp_count = 0, ret_flag = 0;
                for (int m = 0; m < 20; m += 4) {
                    if (*(uint8_t*)&mem[i + m + 3] == 0xA9)
                        ldp_count++;
                    else
                        break;
                }
                if (ldp_count == 5) {
                    for (int m = 24; m < 32; m += 4) {
                        if (*(uint32_t*)&mem[i + m] == 0xD65F03C0)
                            ret_flag++;
                    }
                    if (ret_flag) {
                        if (*(uint16_t*)&mem[i - 4] == 0x3E0 && *(uint8_t*)&mem[i - 7] == 0x3) {
                            int count1 = 0, count2 = 0;
                            for (size_t m = start_pos; m < i; m += 4) {
                                if (*(uint32_t*)&mem[m] >> 16 == 0x9400) count1++;
                                else if (*(uint32_t*)&mem[m] >> 16 == 0xb400) count2++;
                            }
                            if (count1 && count2 && count1 + count2 > 2) {
                                if (mov_count < 3)
                                    pmov[mov_count++] = i - 4;
                                else {
                                    pmov[0] = pmov[1];
                                    pmov[1] = pmov[2];
                                    pmov[2] = i - 4;
                                    mov_count = 3;
                                }
                            }
                        }
                    }
                }
            }
        }
        if (mov_count < 2) {
            free(mem);
            return nullptr;
        }
        int idx = (mov_count > 2) ? ((pmov[2] - 2 * pmov[1] + pmov[0] > 0) ? 1 : 2) : (mov_count - 1);
        *(uint32_t*)&mem[pmov[idx]] = 0x52800000;   // MOV W0, #0

        *out_size = eff_size;
        return mem;
    }

public:
    // 公开接口：输入原始有签名的固件路径，输出最终绕过签名的禁用 AVB 固件（双镜像拼接）
    int AvbFxxker(const char* original_signed_file, const char* output_file) {
        printf("[TosPatcher] [INFO] Starting AvbFxxker v1.1\n");

        // 1. 加载原始固件
        size_t orig_file_size = 0;
        uint8_t* orig_file_buf = loadfile(original_signed_file, &orig_file_size);
        if (!orig_file_buf) {
            printf("[TosPatcher] [ERROR] Failed to load %s\n", original_signed_file);
            return 1;
        }

        // 2. 计算有效区域大小（与 bsp 的 chsize 逻辑相同）
        size_t eff_size = get_effective_size(orig_file_buf, orig_file_size);
        if (eff_size == 0) {
            printf("[TosPatcher] [ERROR] Invalid or broken sprd trusted firmware\n");
            free(orig_file_buf);
            return 1;
        }

        // 3. 截断内存缓冲区，只保留有效区域（相当于 bsp 的 chsize 物理截断）
        if (eff_size < orig_file_size) {
            uint8_t* truncated_buf = (uint8_t*)malloc(eff_size);
            if (!truncated_buf) {
                printf("[TosPatcher] [ERROR] malloc failed for truncation\n");
                free(orig_file_buf);
                return 1;
            }
            memcpy(truncated_buf, orig_file_buf, eff_size);
            free(orig_file_buf);
            orig_file_buf = truncated_buf;
            orig_file_size = eff_size;  // orig_file_buf 就是截断后的有效区域
        }

        // 4. 对有效区域进行 AVB 补丁（patch_in_memory 内部会再次复制，但只会复制有效区域）
        size_t patched_eff_size = 0;
        uint8_t* patched_buf = patch_in_memory(orig_file_buf, orig_file_size, &patched_eff_size);
        if (!patched_buf) {
            printf("[TosPatcher] [ERROR] AVB patch failed\n");
            free(orig_file_buf);
            return 1;
        }
        // 修补后的大小应与有效区域大小一致（因为补丁不改变大小）
        if (patched_eff_size != orig_file_size) {
            printf("[TosPatcher] [WARNING] patched size mismatch\n");
        }

        // 5. 提取头部信息（基于截断后的有效区域）
        sys_img_header* orig_hdr = (sys_img_header*)orig_file_buf;
        uint32_t orig_img_size = orig_hdr->mImgSize;

        // 签名头位置：有效区域中，头部 + 原始镜像体之后
        uint8_t* sechdr_addr = orig_file_buf + sizeof(sys_img_header) + orig_img_size;
        sprdsignedimageheader* sig_hdr = (sprdsignedimageheader*)sechdr_addr;

        // 修补后的载荷（从 patched_buf 中提取）
        uint8_t* patched_payload = patched_buf + sizeof(sys_img_header);
        size_t patched_payload_size = orig_img_size;   // 修补不改变载荷大小

        // 原始有效区域的剩余部分（原载荷 + 签名尾部），与 bsp 中的 signed_img 完全对应
        uint8_t* orig_remain = orig_file_buf + sizeof(sys_img_header);
        size_t orig_remain_size = orig_file_size - sizeof(sys_img_header);

        // 6. 构建新头部（与 bsp 一致：mImgSize = 原始载荷 + 新载荷）
        sys_img_header new_hdr = *orig_hdr;
        new_hdr.mImgSize = orig_img_size + patched_payload_size;  // 双镜像

        // 7. 更新签名头部偏移（仅更新 payload 和 cert，与 bsp 故意保持一致）
        sig_hdr->payload_offset += patched_payload_size;
        sig_hdr->cert_offset    += patched_payload_size;
        // 其他偏移量（priv、cert_dbg_*）保持不变

        // 8. 分配输出缓冲区并拼接
        size_t out_size = sizeof(sys_img_header) + patched_payload_size + orig_remain_size;
        uint8_t* out_buf = (uint8_t*)malloc(out_size);
        if (!out_buf) {
            printf("[TosPatcher] [ERROR] malloc failed for output buffer\n");
            free(orig_file_buf);
            free(patched_buf);
            return 1;
        }

        memcpy(out_buf, &new_hdr, sizeof(sys_img_header));
        memcpy(out_buf + sizeof(sys_img_header), patched_payload, patched_payload_size);
        memcpy(out_buf + sizeof(sys_img_header) + patched_payload_size, orig_remain, orig_remain_size);

        // 9. 写入最终文件
        EnhancedFile fp = oxfopen_enhanced(output_file, "wb");
        if (!fp) {
            printf("[TosPatcher] [ERROR] Cannot create output file %s\n", output_file);
            free(out_buf);
            free(orig_file_buf);
            free(patched_buf);
            return 1;
        }
        fp.write(out_buf, 1, out_size);
        fp.close();

        printf("[TosPatcher] [INFO] Successfully generated %s (size: %zu bytes)\n", output_file, out_size);

        // 清理
        free(out_buf);
        free(orig_file_buf);
        free(patched_buf);
        return 0;
    }
};