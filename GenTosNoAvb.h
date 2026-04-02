#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <filesystem>
#include <iostream>
#include <vector>
#include <algorithm>

namespace fs = std::filesystem;

class TosPatcher {
private:
    static uint8_t *loadfile(const char *fn, size_t *num, size_t extra){
        size_t n, j = 0;
        uint8_t *buf = 0;
        FILE *fi = fopen(fn, "rb");
        if (fi)
        {
            fseek(fi, 0, SEEK_END);
            n = ftell(fi);
            if (n)
            {
                fseek(fi, 0, SEEK_SET);
                buf = (uint8_t *)malloc(n + extra);
                if (buf)
                    j = fread(buf, 1, n, fi);
            }
            fclose(fi);
        }
        if (num)
            *num = j;
        return buf;
    }

    // 在内存中搜索字节序列（精确匹配）
    static std::vector<size_t> findAllBytesSequences(const uint8_t* data, size_t dataSize,
                                                     const uint8_t* pattern, size_t patternSize) {
        std::vector<size_t> positions;
        if (patternSize == 0 || dataSize < patternSize) return positions;
        for (size_t i = 0; i <= dataSize - patternSize; ++i) {
            bool match = true;
            for (size_t j = 0; j < patternSize; ++j) {
                if (data[i + j] != pattern[j]) {
                    match = false;
                    break;
                }
            }
            if (match) positions.push_back(i);
        }
        return positions;
    }

public:
    int patcher(const char* fn) {
        printf("[TosPatcher] [INFO] GenTosPatcher v1.2 by TomKing062\n");
        uint8_t *mem;
        size_t size = 0;
        mem = loadfile(fn, &size, 0);
        fs::create_directory("backup_tos");
        fs::copy_file(fn, "backup_tos/" + fs::path(fn).filename().string(), fs::copy_options::overwrite_existing);
        if (!mem){
            printf("[TosPatcher] [ERROR] loadfile failed\n");
            free(mem);
            return 1;
        }
        if ((uint64_t)size >> 32){
            printf("[TosPatcher] [ERROR] file too big\n");
            free(mem);
            return 1;
        }
        if (*(uint32_t *)mem != 0x42544844){
            printf("[TosPatcher] [ERROR] The file is not sprd trusted firmware\n");
            free(mem);
            return 1;
        }
        else if (!(*(uint32_t *)&mem[0x30])){
            printf("[TosPatcher] [ERROR] broken sprd trusted firmware\n");
            free(mem);
            return 1;
        }
        if (*(uint32_t *)&mem[0x30] + 0x260 >= size){
            printf("0x%zx\n", size);
            free(mem);
            return 1;
        }
        if (*(uint32_t *)&mem[(*(uint32_t *)&mem[0x30]) + 0x200 + 0x50] && *(uint32_t *)&mem[(*(uint32_t *)&mem[0x30]) + 0x200 + 0x58])
        size = *(uint32_t *)&mem[(*(uint32_t *)&mem[0x30]) + 0x200 + 0x50] + *(uint32_t *)&mem[(*(uint32_t *)&mem[0x30]) + 0x200 + 0x58];
        else if (*(uint32_t *)&mem[(*(uint32_t *)&mem[0x30]) + 0x200 + 0x30] && *(uint32_t *)&mem[(*(uint32_t *)&mem[0x30]) + 0x200 + 0x38])
            size = *(uint32_t *)&mem[(*(uint32_t *)&mem[0x30]) + 0x200 + 0x30] + *(uint32_t *)&mem[(*(uint32_t *)&mem[0x30]) + 0x200 + 0x38];
        else if (*(uint32_t *)&mem[(*(uint32_t *)&mem[0x30]) + 0x200 + 0x20] && *(uint32_t *)&mem[(*(uint32_t *)&mem[0x30]) + 0x200 + 0x28])
            size = *(uint32_t *)&mem[(*(uint32_t *)&mem[0x30]) + 0x200 + 0x20] + *(uint32_t *)&mem[(*(uint32_t *)&mem[0x30]) + 0x200 + 0x28];
        else
            size = *(uint32_t *)&mem[0x30] + 0x200;
        printf("0x%zx\n", size);
        FILE *file = fopen("temp", "wb");
        if (file == NULL){
            printf("[TosPatcher] [ERROR] Failed to create the file.\n");
            free(mem);
            return 1;
        }
        size_t bytes_written = fwrite(mem, sizeof(unsigned char), size, file);
        if (bytes_written != size){
            printf("[TosPatcher] [ERROR] Failed to write the file.\n");
            free(mem);
            return 1;
        }
        fclose(file);

        if (remove(fn)){
            printf("[TosPatcher] [ERROR] Failed to delete the file.\n");
            free(mem);
            return 1;
        }
        if (rename("temp", fn)){
            printf("[TosPatcher] [ERROR] Failed to rename the file.\n");
            free(mem);
            return 1;
        }

        size_t pmov[3] = {0};
        size_t last_start_pos = 0, start_pos = 0;
        int mov_count = 0;
        for (size_t i = 0x200; i < size; i += 4)
        {
            uint32_t current = *(uint32_t *)&mem[i];
            if (current == 0xD65F03C0 || (current & 0xFF00FFFF) == 0xA8007BFD)
            {
                last_start_pos = 0;
                start_pos = 0;
            }
            else if ((current & 0xFF00FFFF) == 0xA9007BFD)
            {
                if (start_pos)
                    last_start_pos = start_pos;
                start_pos = i;
            }
            else if (start_pos)
            {
                int count1 = 0, count2 = 0, ldp_count = 0, ret_flag = 0;
                for (int m = 0; m < 20; m += 4)
                {
                    if (*(uint8_t *)&mem[i + m + 3] == 0xA9)
                        ldp_count++;
                    else
                        break;
                }
                if (ldp_count == 5)
                {
                    for (int m = 24; m < 32; m += 4)
                    {
                        if (*(uint32_t *)&mem[i + m] == 0xD65F03C0)
                            ret_flag++;
                    }
                    if (ret_flag)
                    {
                        if (*(uint16_t *)&mem[i - 4] == 0x3E0 && *(uint8_t *)&mem[i - 7] == 0x3)
                        {
                            for (size_t m = start_pos; m < i; m += 4)
                            {
                                if (*(uint32_t *)&mem[m] >> 16 == 0x9400)
                                {
                                    count1++;
                                }
                                else if (*(uint32_t *)&mem[m] >> 16 == 0xb400)
                                {
                                    count2++;
                                }
                            }
                            if (count1 && count2 && count1 + count2 > 2)
                            {
                                printf("[TosPatcher] [INFO] detected mov at 0x%zx\n", i - 4);
                                if (mov_count < 3)
                                {
                                    pmov[mov_count] = i - 4;
                                    mov_count++;
                                }
                                else
                                {
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
        if (mov_count < 2)
        {
            printf("[TosPatcher] [ERROR] patch failed (mov_count < 2), skip saving!!!\n");
            free(mem);
            return 1;
        }
        if (mov_count > 2)
            mov_count = (pmov[2] - 2 * pmov[1] + pmov[0] > 0) ? 1 : 2;
        else
            mov_count--;
        printf("[TosPatcher] [INFO] patch mov at 0x%zx\n", pmov[mov_count]);
        *(uint32_t *)&mem[pmov[mov_count]] = 0x52800000;   // 原mov修补

        printf("[TosPatcher] [INFO] Start to patch BSP functions...\n");

        // --------------------------------------------------------------------
        // 添加 C# 逻辑：搜索并修补 AVB 相关函数
        // --------------------------------------------------------------------
        // 定义字节序列（来自 C# 代码）
        static const uint8_t bytesOnNewDevices[] = {
            0xE0,0x03,0x14,0x2a, 0xF3,0x53,0x41,0xa9, 0xF5,0x5B,0x42,0xa9,
            0xF7,0x63,0x43,0xa9, 0xF9,0x6B,0x44,0xa9, 0xFB,0x73,0x45,0xa9,
            0xFD,0x7B,0xDF,0xA8, 0xc0,0x03,0x5f,0xd6
        };
        static const uint8_t bytesOnMostDevices[] = {
            0xE0,0x03,0xcc,0xcc, 0xcc,0xcc,0xcc,0xa9, 0xcc,0xcc,0xcc,0xa9,
            0xcc,0xcc,0xcc,0xa9, 0xcc,0xcc,0xcc,0xa9, 0xcc,0xcc,0xcc,0xa9,
            0xcc,0xcc,0xcc,0xcc, 0xff,0xcc,0xcc,0x91, 0xc0,0x03,0x5f,0xd6
        };

        std::vector<size_t> temp;
        // 搜索 bytesOnMostDevices
        std::vector<size_t> posMost = findAllBytesSequences(mem, size, bytesOnMostDevices, sizeof(bytesOnMostDevices));
        temp.insert(temp.end(), posMost.begin(), posMost.end());
        // 搜索 bytesOnNewDevices
        std::vector<size_t> posNew = findAllBytesSequences(mem, size, bytesOnNewDevices, sizeof(bytesOnNewDevices));
        temp.insert(temp.end(), posNew.begin(), posNew.end());

        // 排序
        std::sort(temp.begin(), temp.end());

        long loadVbmetaFuncAddr = 0;
        long avbSlotVerifyFuncAddr = 0;

        if (temp.size() >= 3) {
            long maxDiff = LONG_MAX;
            for (size_t i = 0; i < temp.size() - 1; ++i) {
                long diff = static_cast<long>(temp[i + 1] - temp[i]);
                if (diff < maxDiff && diff > 0x1000) {
                    maxDiff = diff;
                    loadVbmetaFuncAddr = static_cast<long>(temp[i]);
                    avbSlotVerifyFuncAddr = static_cast<long>(temp[i + 1]);
                }
            }
        } else if (temp.size() == 2) {
            loadVbmetaFuncAddr = static_cast<long>(temp[0]);
            avbSlotVerifyFuncAddr = static_cast<long>(temp[1]);
        } else if (temp.size() == 1) {
            loadVbmetaFuncAddr = static_cast<long>(temp[0]);
        }
        bool isOutOfRange = false;
        // 修补找到的函数地址，写入 0xA5000000
        const uint32_t patchValue = 0xA5000000;
        if (loadVbmetaFuncAddr != 0) {
            if (loadVbmetaFuncAddr + 4 <= size) {
                *(uint32_t *)&mem[loadVbmetaFuncAddr] = patchValue;
                printf("[TosPatcher] [INFO] Patched load_and_verify_vbmeta() function at 0x%lx\n", loadVbmetaFuncAddr);
            } else {
                printf("[TosPatcher] [WARNING] loadVbmetaFuncAddr out of range\n");
                isOutOfRange = true;
            }
        }
        if (avbSlotVerifyFuncAddr != 0) {
            if (avbSlotVerifyFuncAddr + 4 <= size) {
                *(uint32_t *)&mem[avbSlotVerifyFuncAddr] = patchValue;
                printf("[TosPatcher] [INFO] Patched avb_slot_verify() function at 0x%lx\n", avbSlotVerifyFuncAddr);
            } else {
                printf("[TosPatcher] [WARNING] avbSlotVerifyFuncAddr out of range\n");
                isOutOfRange = true;
            }
        }
        // --------------------------------------------------------------------

        // 保存最终修补后的文件
        file = fopen("tos-noavb.bin", "wb");
        if (file == NULL){
            printf("[TosPatcher] [ERROR] Failed to create the file.\n");
            free(mem);
            return 1;
        }
        bytes_written = fwrite(mem, sizeof(unsigned char), size, file);
        if (bytes_written != size){
            printf("[TosPatcher] [ERROR] Failed to write the file.\n");
            free(mem);
            return 1;
        }
        if (isOutOfRange) {
            printf("[TosPatcher] [WARNING] Some function addresses were out of range, patching may be incomplete\n");
        }
        fclose(file);

        free(mem);

        return 0;
    }
};