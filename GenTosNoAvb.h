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
#include "core/file_io.h"

typedef struct {
    uint32_t  mMagicNum;        // "BTHD"
    uint32_t  mVersion;
    uint8_t   mPayloadHash[32];
    uint64_t  mImgAddr;
    uint32_t  mImgSize;
    uint32_t  is_packed;
    uint32_t  mFirmwareSize;
    uint32_t  mFirmwareOff;
    uint8_t   reserved[448];
    uint32_t  mPostromOffset;   // 新增：用于 postrom 支持
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

// postrom 头部结构
typedef struct postrom_main_header {
    uint32_t mMagicNum;    // "PROM"
    uint32_t mVersion;
    uint32_t mImgSize;
    uint8_t  reserved[4];
} postrom_main_header;

// 定义 max_size 宏
#define max_size(x, y) ((x) > (y) ? (x) : (y))

class TosPatcher {
private:
    // ========== 辅助函数：完全复制自 common.txt ==========

    // 加载文件（与上游一致）
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
    static size_t calculate_effective_size(uint8_t* mem, size_t file_size) {
        sys_img_header* header = (sys_img_header*)mem;
        size_t sizewithPostrom = 0;
        size_t size_in_footer = 0;

        // 1. 检查 postrom（与 bsp_chsize 完全一致）
        if (header->mPostromOffset && header->mPostromOffset + 0x200 < file_size) {
            postrom_main_header* postrom_hdr = (postrom_main_header*)(mem + header->mPostromOffset);
            if (postrom_hdr->mImgSize && 
                (header->mPostromOffset + 0x200 + postrom_hdr->mImgSize <= file_size)) {
                sizewithPostrom = header->mPostromOffset + 0x200 + postrom_hdr->mImgSize;
            }
        }

        // 2. 检查 signature footer（与 bsp_chsize 完全一致）
        if (!header->mImgSize) {
            return 0;
        }

        sprdsignedimageheader* footer = (sprdsignedimageheader*)&mem[header->mImgSize + 0x200];
        
        // 如果 footer 超出文件大小，返回文件大小
        if (header->mImgSize + 0x200 + sizeof(sprdsignedimageheader) >= file_size) {
            return file_size;
        }

        // 3. 取所有非零 offset+size 的最大值（与 bsp_chsize 完全一致）
        if (footer->cert_dbg_developer_size && footer->cert_dbg_developer_offset)
            size_in_footer = max_size(size_in_footer, 
                (size_t)(footer->cert_dbg_developer_size + footer->cert_dbg_developer_offset));
        if (footer->priv_size && footer->priv_offset)
            size_in_footer = max_size(size_in_footer, 
                (size_t)(footer->priv_size + footer->priv_offset));
        if (footer->cert_size && footer->cert_offset)
            size_in_footer = max_size(size_in_footer, 
                (size_t)(footer->cert_size + footer->cert_offset));
        if (footer->payload_size && footer->payload_offset)
            size_in_footer = max_size(size_in_footer, 
                (size_t)(footer->payload_size + footer->payload_offset));
        else
            size_in_footer = max_size(size_in_footer, 
                (size_t)(header->mImgSize + 0x200));

        // 4. 返回最大值（与 bsp_chsize 完全一致）
        return max_size(size_in_footer, sizewithPostrom);
    }
    static uint8_t* dis_avb_in_memory(uint8_t* buf, size_t size, size_t* out_size) {
        size_t pmov[3] = {0};
        size_t last_start_pos = 0, start_pos = 0;
        int mov_count = 0;

        for (size_t i = 0x200; i < size; i += 4) {
            uint32_t current = *(uint32_t*)&buf[i];
            if (current == 0xD65F03C0 || (current & 0xFF00FFFF) == 0xA8007BFD) {
                last_start_pos = 0;
                start_pos = 0;
            } else if ((current & 0xFF00FFFF) == 0xA9007BFD) {
                if (start_pos) last_start_pos = start_pos;
                start_pos = i;
            } else if (start_pos) {
                int count1 = 0, count2 = 0, ldp_count = 0, ret_flag = 0;
                for (int m = 0; m < 20; m += 4) {
                    if (*(uint8_t*)&buf[i + m + 3] == 0xA9)
                        ldp_count++;
                    else
                        break;
                }
                if (ldp_count == 5) {
                    for (int m = 24; m < 32; m += 4) {
                        if (*(uint32_t*)&buf[i + m] == 0xD65F03C0)
                            ret_flag++;
                    }
                    if (ret_flag) {
                        if (*(uint16_t*)&buf[i - 4] == 0x3E0 && *(uint8_t*)&buf[i - 7] == 0x3) {
                            if (start_pos > i && last_start_pos)
                                start_pos = last_start_pos;
                            for (size_t m = start_pos; m < i; m += 4) {
                                if (*(uint32_t*)&buf[m] >> 16 == 0x9400)
                                    count1++;
                                else if (*(uint32_t*)&buf[m] >> 16 == 0xb400)
                                    count2++;
                            }
                            if (count1 && count2 && count1 + count2 > 2) {
                                if (mov_count < 3) {
                                    pmov[mov_count] = i - 4;
                                    mov_count++;
                                } else {
                                    pmov[0] = pmov[1];
                                    pmov[1] = pmov[2];
                                    pmov[2] = i - 4;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (mov_count < 2) {
            printf("[TosPatcher] [ERROR] Failed to find mov points (mov_count < 2).\n");
            return nullptr;
        }
        int idx;
        if (mov_count > 2) {
            idx = (pmov[2] - 2 * pmov[1] + pmov[0] > 0) ? 1 : 2;
        } else {
            idx = mov_count - 1;
        }
        size_t patch_addr = pmov[idx];

        // 复制一份缓冲区，进行修改
        uint8_t* patched = (uint8_t*)malloc(size);
        if (!patched) return nullptr;
        memcpy(patched, buf, size);
        *(uint32_t*)&patched[patch_addr] = 0x52800000;  // mov w0, #0

        *out_size = size;
        return patched;
    }
    static uint8_t* bsp_cve_2img_in_memory(uint8_t* signed_buf, size_t signed_size,
                                       uint8_t* target_buf, size_t target_size,
                                       size_t* out_size, bool* already_merged = nullptr) {
        // 检查原始镜像是否已经被 BSP 拼接过
        sys_img_header* sys_img_hdr = (sys_img_header*)signed_buf;
        sprdsignedimageheader* img_hdr = (sprdsignedimageheader*)&signed_buf[sys_img_hdr->mImgSize + 0x200];
        
        if (img_hdr->payload_offset != sizeof(sys_img_header)) {
            printf("[TosPatcher] [WARNING] Original image appears already BSP-merged (payload_offset=0x%llx). Skipping merge.\n",
                (unsigned long long)img_hdr->payload_offset);
            
            // 直接返回原始镜像的副本
            uint8_t* original_copy = (uint8_t*)malloc(signed_size);
            if (!original_copy) return nullptr;
            memcpy(original_copy, signed_buf, signed_size);
            *out_size = signed_size;
            if (already_merged) *already_merged = true;
            return original_copy;
        }
        
        // 1. 目标大小 16 字节对齐（与上游一致）
        size_t modified_img_size = ((target_size + 15) / 16) * 16;

        // 2. 提取目标载荷（若目标有 BTHD 头则跳过）
        uint8_t* modified_img = target_buf;
        size_t orig_modified_img_size = *(uint32_t*)&modified_img[0x30];
        if (*(uint32_t*)modified_img == 0x42544844 && orig_modified_img_size) {
            modified_img_size = orig_modified_img_size;
            modified_img += sizeof(sys_img_header);
        }

        // 3. 处理签名镜像
        size_t signed_img_size = signed_size - sizeof(sys_img_header);  // 去除头部
        uint8_t* signed_img = signed_buf + sizeof(sys_img_header);  // 原始载荷 + 签名

        // 4. 更新头部和签名偏移
        sys_img_header new_hdr = *sys_img_hdr;
        new_hdr.mImgSize += modified_img_size;
        img_hdr->payload_offset += modified_img_size;
        img_hdr->cert_offset += modified_img_size;

        // 5. 分配输出缓冲区
        size_t out_size_total = sizeof(sys_img_header) + modified_img_size + signed_img_size;
        uint8_t* out_buf = (uint8_t*)malloc(out_size_total);
        if (!out_buf) return nullptr;

        // 6. 拼接：新头部 + 目标载荷 + 原始载荷及签名
        memcpy(out_buf, &new_hdr, sizeof(sys_img_header));
        memcpy(out_buf + sizeof(sys_img_header), modified_img, modified_img_size);
        memcpy(out_buf + sizeof(sys_img_header) + modified_img_size, signed_img, signed_img_size);

        *out_size = out_size_total;
        if (already_merged) *already_merged = false;
        return out_buf;
    }

public:
    // ========== 对外 API ==========
    // patch_avb : 是否执行 AVB 指令修补（等同于 dis_avb）
    // patch_bsp : 是否执行 BSP 拼接（等同于 bsp_cve_2img）
    // 组合逻辑：
    //   - false, false : 返回错误
    //   - true, false  : 仅 AVB 修补，输出修补后的目标镜像
    //   - false, true  : 仅 BSP 拼接，输出拼接后的镜像（目标未经 AVB 修补）
    //   - true, true   : 先 AVB 修补，再 BSP 拼接，输出最终镜像
    // 输出文件大小 = 实际拼接大小（不补零，与上游一致）
    int AvbFxxker(const char* __orig_image, const char* __target, const char* __save_path,
                  bool patch_avb, bool patch_bsp) {
        if (!patch_avb && !patch_bsp) {
            printf("[TosPatcher] [ERROR] Both flags are false, nothing to do.\n");
            return 1;
        }
        if (patch_bsp && !__orig_image) {
            printf("[TosPatcher] [ERROR] BSP merge requires an original image.\n");
            return 1;
        }

        // 1. 加载目标文件（始终需要）
        size_t target_raw_size = 0;
        uint8_t* target_raw = loadfile(__target, &target_raw_size);
        if (!target_raw) {
            printf("[TosPatcher] [ERROR] Failed to load %s\n", __target);
            return 1;
        }

        // 2. 如果需要 AVB 修补，执行 dis_avb
        uint8_t* target_after_avb = nullptr;
        size_t target_after_avb_size = 0;
        if (patch_avb) {
            // 必须先检查目标是否有效 BTHD（与 dis_avb 隐含要求一致）
            if (*(uint32_t*)target_raw != 0x42544844) {
                printf("[TosPatcher] [ERROR] AVB patch requires valid BTHD image.\n");
                free(target_raw);
                return 1;
            }
            target_after_avb = dis_avb_in_memory(target_raw, target_raw_size, &target_after_avb_size);
            if (!target_after_avb) {
                printf("[TosPatcher] [ERROR] AVB patch failed (image already patched or pattern not found).\n");
                free(target_raw);
                return 1;
            }
            // 释放原始目标，后续使用修补后的
            free(target_raw);
            target_raw = target_after_avb;
            target_raw_size = target_after_avb_size;
        }

        // 3. 如果只做 AVB 修补，直接输出
        if (!patch_bsp) {
            EnhancedFile fp = oxfopen_enhanced(__save_path, "wb");
            if (!fp) {
                printf("[TosPatcher] [ERROR] Cannot create %s\n", __save_path);
                free(target_raw);
                return 1;
            }
            fp.write(target_raw, 1, target_raw_size);
            fp.close();
            printf("[TosPatcher] [INFO] AVB-patched image saved to %s (size: %zu)\n",
                   __save_path, target_raw_size);
            free(target_raw);
            return 0;
        }

        // 4. BSP 拼接（patch_bsp == true）
        // 加载原始签名镜像
        size_t orig_raw_size = 0;
        uint8_t* orig_raw = loadfile(__orig_image, &orig_raw_size);
        if (!orig_raw) {
            printf("[TosPatcher] [ERROR] Failed to load %s\n", __orig_image);
            free(target_raw);
            return 1;
        }

        // 计算有效区域（使用与 bsp_chsize 相同的逻辑）
        size_t orig_eff_size = calculate_effective_size(orig_raw, orig_raw_size);
        if (orig_eff_size == 0) {
            printf("[TosPatcher] [ERROR] Invalid original image (no BTHD or broken).\n");
            free(orig_raw);
            free(target_raw);
            return 1;
        }
        // 将 orig_raw 截取到有效区域（内存中）
        uint8_t* orig_eff = (uint8_t*)malloc(orig_eff_size);
        if (!orig_eff) {
            free(orig_raw);
            free(target_raw);
            return 1;
        }
        memcpy(orig_eff, orig_raw, orig_eff_size);
        free(orig_raw);

        // 执行 BSP 拼接
        size_t merged_size = 0;
        bool already_merged = false;
        uint8_t* merged = bsp_cve_2img_in_memory(orig_eff, orig_eff_size,
                                                target_raw, target_raw_size,
                                                &merged_size, &already_merged);
        free(orig_eff);
        free(target_raw);
        if (!merged) {
            printf("[TosPatcher] [ERROR] BSP merge failed.\n");
            return 1;
        }

        if (already_merged) {
            printf("[TosPatcher] [INFO] Original image already BSP-merged, output unchanged.\n");
        }

        // 写入输出文件（直接写入实际大小，与上游一致）
        EnhancedFile fp = oxfopen_enhanced(__save_path, "wb");
        if (!fp) {
            printf("[TosPatcher] [ERROR] Cannot create %s\n", __save_path);
            free(merged);
            return 1;
        }
        fp.write(merged, 1, merged_size);
        fp.close();
        printf("[TosPatcher] [INFO] BSP-merged image saved to %s (size: %zu)\n",
               __save_path, merged_size);
        free(merged);
        return 0;
    }
};