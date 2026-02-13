// GenTosNoAvb.h
// C++17 rewrite, preserves original patching logic exactly.
// Does NOT modify the input file; only creates backup and patched output.

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <memory>
#include <filesystem>
namespace fs = std::filesystem;

class TosPatcher {
public:
    int patcher(const char* fn) {
        printf("[TosPatcher] [INFO] GenTosPatcher v2.0 Core by TomKing062\n");

        // Load firmware once
        std::vector<uint8_t> fw;
        if (!loadFile(fn, fw))
            return error("loadfile failed");

        // Backup
        if (!backupFirmware(fw))
            return error("Failed to write backup");

        // Basic checks
        if (fw.size() > UINT32_MAX) // original (uint64_t)size >> 32
            return error("file too big");

        if (*reinterpret_cast<const uint32_t*>(fw.data()) != 0x42544844)
            return error("The file is not sprd trusted firmware");

        uint32_t desc_off = *reinterpret_cast<const uint32_t*>(&fw[0x30]);
        if (desc_off == 0)
            return error("broken sprd trusted firmware");

        // Already patched? Original: if (desc_off + 0x260 >= size) -> free + return 0
        if (desc_off + 0x260 >= fw.size()) {
            printf("[TosPatcher] [INFO] Already processed? 0x%zx\n", fw.size());
            return 1;   // no patching
        }

        // Compute effective size (exactly as original)
        size_t proc_size = computeEffectiveSize(fw, desc_off);
        printf("[TosPatcher] [INFO] Processing size: 0x%zx\n", proc_size);

        // Locate patch position (exact original algorithm)
        size_t patch_off = 0;
        if (!findPatchOffset(fw.data(), proc_size, patch_off))
            return error("patch failed (mov_count < 2), skip saving!!!");

        // Apply patch: mov w0, #0
        printf("[TosPatcher] [INFO] patch mov at 0x%zx\n", patch_off);
        *reinterpret_cast<uint32_t*>(&fw[patch_off]) = 0x52800000;

        // Write output file
        if (!writeFile("tos-noavb.bin", fw.data(), proc_size))
            return error("Failed to create the file.");

        printf("[TosPatcher] [INFO] Successfully created tos-noavb.bin\n");
        return 0;
    }

private:
    // ----- File I/O -------------------------------------------------
    static bool loadFile(const char* name, std::vector<uint8_t>& out) {
        std::unique_ptr<FILE, decltype(&fclose)> fp(fopen(name, "rb"), fclose);
        if (!fp) return false;
        fseek(fp.get(), 0, SEEK_END);
        long sz = ftell(fp.get());
        if (sz < 0) return false;
        fseek(fp.get(), 0, SEEK_SET);
        out.resize(sz);
        return fread(out.data(), 1, sz, fp.get()) == static_cast<size_t>(sz);
    }

    static bool writeFile(const char* name, const void* data, size_t len) {
        std::unique_ptr<FILE, decltype(&fclose)> fp(fopen(name, "wb"), fclose);
        if (!fp) return false;
        return fwrite(data, 1, len, fp.get()) == len;
    }

    static bool backupFirmware(const std::vector<uint8_t>& fw) {
        fs::create_directories("backup_tos");
        return writeFile("backup_tos/tos_bak.bin", fw.data(), fw.size());
    }

    // ----- Helper ---------------------------------------------------
    static int error(const char* msg) {
        printf("[TosPatcher] [ERROR] %s\n", msg);
        return 1;
    }

    // ----- Size calculation (exact replica of original) ------------
    static size_t computeEffectiveSize(const std::vector<uint8_t>& fw, uint32_t desc_off) {
        size_t base = static_cast<size_t>(desc_off) + 0x200;
        const uint8_t* p = fw.data() + desc_off + 0x200; // descriptor table base

        auto get32 = [&](size_t off) {
            return *reinterpret_cast<const uint32_t*>(p + off);
        };

        uint32_t off1 = get32(0x50), sz1 = get32(0x58);
        if (off1 && sz1) return static_cast<size_t>(off1) + sz1;

        uint32_t off2 = get32(0x30), sz2 = get32(0x38);
        if (off2 && sz2) return static_cast<size_t>(off2) + sz2;

        uint32_t off3 = get32(0x20), sz3 = get32(0x28);
        if (off3 && sz3) return static_cast<size_t>(off3) + sz3;

        return base;
    }

    // ----- Pattern matching (exact original logic) -----------------
    static bool findPatchOffset(const uint8_t* mem, size_t size, size_t& out) {
        size_t pmov[3] = {0};
        int mov_cnt = 0;

        for (size_t i = 0x200; i + 4 <= size; i += 4) {
            uint32_t cur = *reinterpret_cast<const uint32_t*>(&mem[i]);
            if (cur == 0xD65F03C0 || (cur & 0xFF00FFFF) == 0xA8007BFD) {
                // ret or epilogue, ignore
            }
            else if ((cur & 0xFF00FFFF) == 0xA9007BFD) {
                // function prologue
                size_t start = i, last_start = 0;
                size_t j = i + 4;
                while (j + 4 <= size) {
                    uint32_t insn = *reinterpret_cast<const uint32_t*>(&mem[j]);
                    if ((insn & 0xFF00FFFF) == 0xA9007BFD) {
                        last_start = start;
                        start = j;
                        j += 4;
                        continue;
                    }
                    if (insn == 0xD65F03C0 || (insn & 0xFF00FFFF) == 0xA8007BFD)
                        break;
                    j += 4;
                }

                // check 5 consecutive ldp at function start
                int ldp_cnt = 0;
                for (int m = 0; m < 20; m += 4) {
                    if (mem[start + m + 3] == 0xA9) ldp_cnt++;
                    else break;
                }
                if (ldp_cnt != 5) continue;

                // look for ret in the next 32 bytes
                int ret_flag = 0;
                for (int m = 24; m < 32; m += 4) {
                    uint32_t insn = *reinterpret_cast<const uint32_t*>(&mem[start + m]);
                    if (insn == 0xD65F03C0) ret_flag++;
                }
                if (!ret_flag) continue;

                // check instruction before ret: 0x3E0?0x3 pattern
                if (*(const uint16_t*)&mem[start + 24 - 4] == 0x3E0 &&
                    mem[start + 24 - 7] == 0x3)
                {
                    int bl = 0, cbz = 0;
                    for (size_t pc = start; pc < start + 24; pc += 4) {
                        uint32_t insn = *reinterpret_cast<const uint32_t*>(&mem[pc]);
                        if (insn >> 16 == 0x9400) bl++;
                        else if (insn >> 16 == 0xB400) cbz++;
                    }
                    if (bl && cbz && bl + cbz > 2) {
                        // candidate found
                        size_t patch = start + 24 - 4;
                        pmov[0] = pmov[1];
                        pmov[1] = pmov[2];
                        pmov[2] = patch;
                        if (mov_cnt < 3) mov_cnt++;
                    }
                }
                i = j; // continue after this function
            }
        }

        if (mov_cnt < 2) return false;

        int idx;
        if (mov_cnt == 2)
            idx = 1;   // original: mov_count-- (from 2 to 1)
        else {
            ptrdiff_t diff = static_cast<ptrdiff_t>(pmov[2])
                           - 2 * static_cast<ptrdiff_t>(pmov[1])
                           + static_cast<ptrdiff_t>(pmov[0]);
            idx = (diff > 0) ? 1 : 2;
        }
        out = pmov[idx];
        return true;
    }
};