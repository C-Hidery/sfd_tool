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
#include "core/file_io.h"

// 结构体定义（保留原注释）
typedef struct {
    uint32_t  mMagicNum;        // "BTHD"=="0x42544844"=="boothead"
    uint32_t  mVersion;         // 1
    uint8_t   mPayloadHash[32]; // sha256 hash value
    uint64_t  mImgAddr;         // image loaded address
    uint32_t  mImgSize;         // image size
    uint32_t  is_packed;        // packed image flag 0:false 1:true
    uint32_t  mFirmwareSize;    // runtime firmware size
    uint32_t  mFirmwareOff;     // runtime firmware offset
    uint8_t   reserved[448];    // 448 + 16*4 = 512
} sys_img_header;

#define MAGIC_SIZE 8
typedef struct sprdsignedimageheader {
    uint8_t magic[MAGIC_SIZE];
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

    // 原地 AVB 补丁（来自原 GenTosNoAvb）
    static uint8_t* patch_in_memory(uint8_t* orig_buf, size_t orig_size, size_t* out_size) {
        size_t eff_size = get_effective_size(orig_buf, orig_size);
        if (eff_size == 0) return nullptr;

        uint8_t* mem = (uint8_t*)malloc(eff_size);
        if (!mem) return nullptr;
        memcpy(mem, orig_buf, eff_size);

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
        *(uint32_t*)&mem[pmov[idx]] = 0x52800000;

        *out_size = eff_size;
        return mem;
    }

public:
    // 修正后：允许 __orig_image 为 nullptr（仅当 patch_bsp == false）
    int AvbFxxker(const char* __orig_image, const char* __target, const char* __save_path,
                  bool patch_avb, bool patch_bsp) {
        if (!patch_avb && !patch_bsp) {
            printf("[TosPatcher] [ERROR] Both flags are false, nothing to do.\n");
            return 1;
        }

        // 如果需要进行 BSP 拼接，则必须提供原始镜像
        if (patch_bsp && __orig_image == nullptr) {
            printf("[TosPatcher] [ERROR] BSP merge requires an original image.\n");
            return 1;
        }

        // 1. 加载目标文件（总是需要）
        size_t target_raw_size = 0;
        uint8_t* target_raw = loadfile(__target, &target_raw_size);
        if (!target_raw) {
            printf("[TosPatcher] [ERROR] Failed to load %s\n", __target);
            return 1;
        }

        // 保存原始目标大小（用于后续大小检查）
        size_t original_target_raw_size = target_raw_size;

        // 提取目标载荷（无论是否有效）
        uint8_t* target_payload = nullptr;
        size_t target_payload_size = 0;
        uint8_t* target_for_avb = nullptr;
        size_t target_for_avb_size = 0;
        bool is_valid_target = false;

        // 检查目标是否有效 BTHD 镜像
        size_t target_eff_size = get_effective_size(target_raw, target_raw_size);
        if (target_eff_size != 0) {
            is_valid_target = true;
            sys_img_header* target_hdr = (sys_img_header*)target_raw;
            target_payload = target_raw + sizeof(sys_img_header);
            target_payload_size = target_hdr->mImgSize;

            if (patch_avb) {
                target_for_avb = (uint8_t*)malloc(target_eff_size);
                if (target_for_avb) {
                    memcpy(target_for_avb, target_raw, target_eff_size);
                    target_for_avb_size = target_eff_size;
                } else {
                    printf("[TosPatcher] [ERROR] malloc for AVB buffer failed\n");
                    free(target_raw);
                    return 1;
                }
            }
        } else {
            // 无效镜像：整个文件作为载荷（原始 fallback 行为）
            target_payload = target_raw;
            target_payload_size = target_raw_size;

            if (patch_avb) {
                printf("[TosPatcher] [ERROR] AVB patch requires valid BTHD image, but target is invalid\n");
                free(target_raw);
                return 1;
            }
        }

        // 2. AVB 修补（如果启用）
        uint8_t* avb_patched = nullptr;
        size_t avb_patched_size = 0;

        if (patch_avb) {
            if (!is_valid_target) {
                // 理论上不会到这里，但保留安全
                printf("[TosPatcher] [ERROR] Cannot perform AVB patch on invalid target\n");
                free(target_raw);
                if (target_for_avb) free(target_for_avb);
                return 1;
            }

            avb_patched = patch_in_memory(target_for_avb, target_for_avb_size, &avb_patched_size);
            free(target_for_avb);  // 释放 AVB 专用缓冲区

            if (!avb_patched) {
                printf("[TosPatcher] [ERROR] AVB patch failed on target\n");
                free(target_raw);
                return 1;
            }

            // 从修补后的区域提取载荷
            sys_img_header* patched_hdr = (sys_img_header*)avb_patched;
            target_payload = avb_patched + sizeof(sys_img_header);
            target_payload_size = patched_hdr->mImgSize;
        }

        // 3. 如果只做 AVB 修补（不进行 BSP 拼接），直接输出结果
        if (!patch_bsp) {
            // 此时 avb_patched 必须有效（因为 patch_avb 为 true）
            if (!avb_patched) {
                printf("[TosPatcher] [ERROR] AVB patch was requested but not performed\n");
                free(target_raw);
                return 1;
            }

            // 检查大小：不能大于原始大小
            if (avb_patched_size > original_target_raw_size) {
                printf("[TosPatcher] [ERROR] AVB-patched image size (%zu) exceeds original size (%zu)\n",
                       avb_patched_size, original_target_raw_size);
                free(avb_patched);
                free(target_raw);
                return 1;
            }

            // 如果小于原始大小，补 0x00 到原始大小
            uint8_t* final_buf = (uint8_t*)malloc(original_target_raw_size);
            if (!final_buf) {
                printf("[TosPatcher] [ERROR] malloc for final buffer failed\n");
                free(avb_patched);
                free(target_raw);
                return 1;
            }
            memset(final_buf, 0, original_target_raw_size);
            memcpy(final_buf, avb_patched, avb_patched_size);

            EnhancedFile fp = oxfopen_enhanced(__save_path, "wb");
            if (!fp) {
                printf("[TosPatcher] [ERROR] Cannot create %s\n", __save_path);
                free(final_buf);
                free(avb_patched);
                free(target_raw);
                return 1;
            }
            fp.write(final_buf, 1, original_target_raw_size);
            fp.close();
            printf("[TosPatcher] [INFO] AVB-patched image saved to %s (size: %zu, padded to original size)\n",
                   __save_path, original_target_raw_size);
            free(final_buf);
            free(avb_patched);
            free(target_raw);
            return 0;
        }

        // 4. BSP 拼接（patch_bsp == true）
        // 此时 __orig_image 非空（已在开头检查）
        size_t orig_raw_size = 0;
        uint8_t* orig_raw = loadfile(__orig_image, &orig_raw_size);
        if (!orig_raw) {
            printf("[TosPatcher] [ERROR] Failed to load %s\n", __orig_image);
            free(target_raw);
            if (avb_patched) free(avb_patched);
            return 1;
        }

        size_t orig_eff_size = get_effective_size(orig_raw, orig_raw_size);
        if (orig_eff_size == 0 || orig_eff_size > orig_raw_size) {
            printf("[TosPatcher] [ERROR] Invalid original image\n");
            free(orig_raw);
            free(target_raw);
            if (avb_patched) free(avb_patched);
            return 1;
        }

        uint8_t* orig_eff = (uint8_t*)malloc(orig_eff_size);
        if (!orig_eff) {
            free(orig_raw);
            free(target_raw);
            if (avb_patched) free(avb_patched);
            return 1;
        }
        memcpy(orig_eff, orig_raw, orig_eff_size);
        free(orig_raw);

        sys_img_header* orig_hdr = (sys_img_header*)orig_eff;
        uint32_t orig_payload_size = orig_hdr->mImgSize;
        uint8_t* sechdr_addr = orig_eff + sizeof(sys_img_header) + orig_payload_size;
        sprdsignedimageheader* sig_hdr = (sprdsignedimageheader*)sechdr_addr;
        uint8_t* orig_remain = orig_eff + sizeof(sys_img_header);
        size_t orig_remain_size = orig_eff_size - sizeof(sys_img_header);

        // 构建新头部并更新签名偏移
        sys_img_header new_hdr = *orig_hdr;
        new_hdr.mImgSize = orig_payload_size + target_payload_size;
        sig_hdr->payload_offset += target_payload_size;
        sig_hdr->cert_offset += target_payload_size;

        // 拼接输出
        size_t out_size = sizeof(sys_img_header) + target_payload_size + orig_remain_size;

        // 检查大小：不能大于原始目标大小
        if (out_size > original_target_raw_size) {
            printf("[TosPatcher] [ERROR] BSP-merged image size (%zu) exceeds original target size (%zu)\n",
                   out_size, original_target_raw_size);
            free(orig_eff);
            free(target_raw);
            if (avb_patched) free(avb_patched);
            return 1;
        }

        uint8_t* out_buf = (uint8_t*)malloc(original_target_raw_size);  // 使用原始大小
        if (!out_buf) {
            printf("[TosPatcher] [ERROR] malloc failed\n");
            free(orig_eff);
            free(target_raw);
            if (avb_patched) free(avb_patched);
            return 1;
        }

        // 全部填充 0x00
        memset(out_buf, 0, original_target_raw_size);
        // 复制实际数据
        memcpy(out_buf, &new_hdr, sizeof(sys_img_header));
        memcpy(out_buf + sizeof(sys_img_header), target_payload, target_payload_size);
        memcpy(out_buf + sizeof(sys_img_header) + target_payload_size, orig_remain, orig_remain_size);

        // 写入最终文件
        EnhancedFile fp = oxfopen_enhanced(__save_path, "wb");
        if (!fp) {
            printf("[TosPatcher] [ERROR] Cannot create %s\n", __save_path);
            free(out_buf);
            free(orig_eff);
            free(target_raw);
            if (avb_patched) free(avb_patched);
            return 1;
        }
        fp.write(out_buf, 1, original_target_raw_size);
        fp.close();
        printf("[TosPatcher] [INFO] BSP-merged image saved to %s (size: %zu, padded to original target size)\n",
               __save_path, original_target_raw_size);

        free(out_buf);
        free(orig_eff);
        free(target_raw);
        if (avb_patched) free(avb_patched);
        return 0;
    }
};