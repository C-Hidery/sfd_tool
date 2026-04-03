#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <vector>
#include <algorithm>

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
    // 加载文件到内存
    static uint8_t* loadfile(const char* fn, size_t* num) {
        size_t n, j = 0;
        uint8_t* buf = nullptr;
        FILE* fi = fopen(fn, "rb");
        if (fi) {
            fseek(fi, 0, SEEK_END);
            n = ftell(fi);
            if (n) {
                fseek(fi, 0, SEEK_SET);
                buf = (uint8_t*)malloc(n);
                if (buf) j = fread(buf, 1, n, fi);
            }
            fclose(fi);
        }
        if (num) *num = j;
        return buf;
    }

    // 计算固件的有效大小（与原 chsize / patcher 中的逻辑一致）
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

    // 对内存中的固件进行 AVB 禁用补丁（原 patcher 的核心逻辑，去掉文件操作）
    static uint8_t* patch_in_memory(uint8_t* orig_buf, size_t orig_size, size_t* out_size) {
        size_t eff_size = get_effective_size(orig_buf, orig_size);
        if (eff_size == 0) return nullptr;

        // 复制有效区域到新缓冲区
        uint8_t* mem = (uint8_t*)malloc(eff_size);
        if (!mem) return nullptr;
        memcpy(mem, orig_buf, eff_size);

        // 以下完全复制原 GenTosNoAvb.h 中的扫描/修改逻辑（只改内存，不写文件）
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
    // 公开接口：输入原始有签名的固件路径，输出最终绕过签名的禁用 AVB 固件
    int AvbFxxker(const char* original_signed_file, const char* output_file) {
        printf("[TosPatcher] [INFO] AvbFxxker v1.0\n");

        // 1. 加载原始固件
        size_t orig_file_size = 0;
        uint8_t* orig_file_buf = loadfile(original_signed_file, &orig_file_size);
        if (!orig_file_buf) {
            printf("[TosPatcher] [ERROR] Failed to load %s\n", original_signed_file);
            return 1;
        }

        // 2. 计算有效区域（即包含签名证书的完整镜像）
        size_t eff_size = get_effective_size(orig_file_buf, orig_file_size);
        if (eff_size == 0) {
            printf("[TosPatcher] [ERROR] Invalid or broken sprd trusted firmware\n");
            free(orig_file_buf);
            return 1;
        }

        // 3. 对有效区域进行 AVB 补丁，得到修补后的固件（已禁用 AVB）
        size_t patched_size = 0;
        uint8_t* patched_buf = patch_in_memory(orig_file_buf, orig_file_size, &patched_size);
        if (!patched_buf) {
            printf("[TosPatcher] [ERROR] AVB patch failed\n");
            free(orig_file_buf);
            return 1;
        }

        // 4. 从原始固件中提取头部和签名尾部（有效区域已包含这些）

        // 有效区域指针（即原始固件截断后的完整镜像）
        uint8_t* eff_buf = orig_file_buf;  // 实际有效区域是从文件头开始的 eff_size 字节
        sys_img_header* orig_hdr = (sys_img_header*)eff_buf;
        uint32_t orig_img_size = orig_hdr->mImgSize;   // 原始镜像体大小

        // 定位签名头部（sprdsignedimageheader）的位置
        uint8_t* sechdr = eff_buf + orig_hdr->mImgSize + sizeof(sys_img_header);
        sprdsignedimageheader* sig_hdr = (sprdsignedimageheader*)sechdr;

        // 计算尾部数据（签名证书等）的起始偏移和长度
        size_t tail_offset = sizeof(sys_img_header) + orig_img_size;
        size_t tail_len = eff_size - tail_offset;   // 尾部数据长度

        // 5. 构建新固件：新头部 + 修补后的镜像体 + 原始尾部（调整偏移）
        //    修补后的镜像体是从 patched_buf 中提取的（patched_buf 已经是有效区域，包含头部）
        uint8_t* patched_payload = patched_buf + sizeof(sys_img_header);
        size_t new_payload_size = patched_size - sizeof(sys_img_header);  // 修补后的镜像体大小
        int64_t delta = (int64_t)new_payload_size - (int64_t)orig_img_size; // 偏移变化量

        // 更新头部中的 mImgSize
        sys_img_header new_hdr = *orig_hdr;
        new_hdr.mImgSize = new_payload_size;

        // 更新签名头部中的所有偏移量（加上 delta）
        sig_hdr->payload_offset += delta;
        sig_hdr->cert_offset += delta;
        sig_hdr->priv_offset += delta;
        sig_hdr->cert_dbg_prim_offset += delta;
        sig_hdr->cert_dbg_developer_offset += delta;

        // 分配输出缓冲区：新头部 + 新镜像体 + 原始尾部
        size_t out_size = sizeof(sys_img_header) + new_payload_size + tail_len;
        uint8_t* out_buf = (uint8_t*)malloc(out_size);
        if (!out_buf) {
            printf("[TosPatcher] [ERROR] malloc failed for output buffer\n");
            free(orig_file_buf);
            free(patched_buf);
            return 1;
        }

        // 拷贝新头部
        memcpy(out_buf, &new_hdr, sizeof(sys_img_header));
        // 拷贝修补后的镜像体
        memcpy(out_buf + sizeof(sys_img_header), patched_payload, new_payload_size);
        // 拷贝原始尾部数据（签名证书等，注意这些数据中引用的偏移量已在上面更新）
        memcpy(out_buf + sizeof(sys_img_header) + new_payload_size, eff_buf + tail_offset, tail_len);

        // 6. 写入最终文件
        FILE* fp = fopen(output_file, "wb");
        if (!fp) {
            printf("[TosPatcher] [ERROR] Cannot create output file %s\n", output_file);
            free(out_buf);
            free(orig_file_buf);
            free(patched_buf);
            return 1;
        }
        fwrite(out_buf, 1, out_size, fp);
        fclose(fp);

        printf("[TosPatcher] [INFO] Successfully generated %s (size: %zu bytes)\n", output_file, out_size);

        // 清理
        free(out_buf);
        free(orig_file_buf);
        free(patched_buf);
        return 0;
    }
};