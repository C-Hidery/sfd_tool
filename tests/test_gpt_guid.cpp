/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "common.h"

#include <stdint.h>
#include <string.h>
#include <vector>

namespace {

const uint8_t kGuidA[16] = {
    0x11, 0x11, 0x11, 0x11, 0x22, 0x22, 0x33, 0x33,
    0x44, 0x44, 0x55, 0x55, 0x66, 0x66, 0x77, 0x77
};
const uint8_t kGuidB[16] = {
    0xAA, 0xBB, 0xCC, 0xDD, 0x01, 0x02, 0x03, 0x04,
    0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C
};
const uint8_t kGuidC[16] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10
};

const uint32_t kEntryCount = 128;
const uint32_t kEntrySize = 128;

void set_u64_le(uint8_t* p, uint64_t v)
{
    for (int i = 0; i < 8; ++i) p[i] = (uint8_t)(v >> (8 * i));
}

// 在 mem+off 处构造一个最小但自洽的 GPT header。
void build_header(uint8_t* mem, size_t size, size_t off, const uint8_t guid[16])
{
    memset(mem, 0, size);
    efi_header* h = reinterpret_cast<efi_header*>(mem + off);
    memcpy(h->signature, "EFI PART", 8);
    h->revision = 0x00010000;
    h->header_size = sizeof(efi_header);
    h->header_crc32 = 0;
    h->current_lba = 1;
    h->backup_lba = 0x1000;
    h->first_usable_lba = 0x22;
    h->last_usable_lba = 0xFF0;
    memcpy(h->disk_guid, guid, 16);
    h->partition_entry_lba = 2;
    h->number_of_partition_entries = kEntryCount;
    h->size_of_partition_entry = kEntrySize;
    h->partition_entry_array_crc32 = 0;
}

void write_entry(uint8_t* mem, size_t entry_off, int idx, const char* name,
                 const uint8_t type[16], const uint8_t uid[16],
                 uint64_t start, uint64_t end)
{
    uint8_t* e = mem + entry_off + (size_t)idx * kEntrySize;
    memcpy(e + 0x00, type, 16);
    memcpy(e + 0x10, uid, 16);
    set_u64_le(e + 0x20, start);
    set_u64_le(e + 0x28, end);
    for (size_t i = 0; name[i] && i < 35; ++i)
    {
        e[0x38 + i * 2] = (uint8_t)name[i];
        e[0x38 + i * 2 + 1] = 0;
    }
}

} // namespace

TEST_CASE("crc32 matches the standard CRC-32/ISO-HDLC check value")
{
    const uint8_t v[] = "123456789";
    CHECK(crc32(0, v, 9) == 0xCBF43926u);
}

TEST_CASE("gpt_get_disk_guid reads the GUID at the 512-byte sector offset")
{
    std::vector<uint8_t> mem(32 * 1024, 0);
    build_header(mem.data(), mem.size(), 512, kGuidA);

    uint8_t got[16] = {};
    CHECK(gpt_get_disk_guid(mem.data(), mem.size(), got) == 0);
    CHECK(memcmp(got, kGuidA, 16) == 0);
}

TEST_CASE("gpt_get_disk_guid reads the GUID at the 4K sector offset")
{
    std::vector<uint8_t> mem(32 * 1024, 0);
    build_header(mem.data(), mem.size(), 4096, kGuidA);

    uint8_t got[16] = {};
    CHECK(gpt_get_disk_guid(mem.data(), mem.size(), got) == 0);
    CHECK(memcmp(got, kGuidA, 16) == 0);
}

TEST_CASE("gpt_get_disk_guid fails without an EFI header")
{
    std::vector<uint8_t> mem(32 * 1024, 0);
    uint8_t got[16] = {};
    CHECK(gpt_get_disk_guid(mem.data(), mem.size(), got) == -1);
}

TEST_CASE("gpt_get_partition_identities reads name, type and unique GUIDs")
{
    std::vector<uint8_t> mem(32 * 1024, 0);
    build_header(mem.data(), mem.size(), 512, kGuidA);
    const size_t entry_off = 2 * 512;
    write_entry(mem.data(), entry_off, 0, "boot", kGuidA, kGuidB, 0x100, 0x200);
    write_entry(mem.data(), entry_off, 1, "userdata", kGuidB, kGuidC, 0x201, 0x400);
    write_entry(mem.data(), entry_off, 2, "esp", kGuidC, kGuidA, 0x401, 0x500);

    std::vector<GptPartitionIdentity> ids;
    CHECK(gpt_get_partition_identities(mem.data(), mem.size(), ids) == 3);
    REQUIRE(ids.size() == 3);
    CHECK(strcmp(ids[0].name, "boot") == 0);
    CHECK(memcmp(ids[0].type_guid, kGuidA, 16) == 0);
    CHECK(memcmp(ids[0].unique_guid, kGuidB, 16) == 0);
    CHECK(ids[0].start_lba == 0x100);
    CHECK(ids[0].end_lba == 0x200);
    CHECK(strcmp(ids[1].name, "userdata") == 0);
    CHECK(strcmp(ids[2].name, "esp") == 0);
    CHECK(ids[2].start_lba == 0x401);
}

TEST_CASE("gpt_get_partition_identities stops at the first empty entry")
{
    std::vector<uint8_t> mem(32 * 1024, 0);
    build_header(mem.data(), mem.size(), 512, kGuidA);
    const size_t entry_off = 2 * 512;
    write_entry(mem.data(), entry_off, 0, "boot", kGuidA, kGuidB, 0x100, 0x200);
    // entry 1 保持全 0

    std::vector<GptPartitionIdentity> ids;
    CHECK(gpt_get_partition_identities(mem.data(), mem.size(), ids) == 1);
}

TEST_CASE("gpt_format_guid prints the canonical mixed-endian form")
{
    // raw 字节 = "prodnv" 的名字派生 GUID，应显示为 646F7270-766E-...
    const uint8_t raw[16] = {0x70, 0x72, 0x6F, 0x64, 0x6E, 0x76, 0, 0,
                             0, 0, 0, 0, 0, 0, 0, 0};
    char s[40] = {};
    gpt_format_guid(raw, s, sizeof(s));
    CHECK(strcmp(s, "646F7270-766E-0000-0000-000000000000") == 0);
}
