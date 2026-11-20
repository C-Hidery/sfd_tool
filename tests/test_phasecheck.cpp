#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "pac_generate.h"

namespace
{
// RD 的 magic 是直接赋给 uint32 的常量（SP09_SPPH_MAGIC_NUMBER 等），
// 在 x86/ARM 小端机器上落盘字节序为 "90PS"，判定时也按 uint32 比较。
constexpr uint32_t kSp05Magic = 0x53503035u;
constexpr uint32_t kSp09Magic = 0x53503039u;
constexpr uint32_t kSp15Magic = 0x53503135u;

uint32_t read_u32(const std::vector<uint8_t>& data, std::size_t offset)
{
    uint32_t v = 0;
    std::memcpy(&v, data.data() + offset, sizeof(v));
    return v;
}

// 在指定块偏移处放 magic + SN（其余按 RD 的 ZeroMemory 语义补 0）
void put_block(std::vector<uint8_t>& data, std::size_t offset, uint32_t magic, const char* sn)
{
    std::memcpy(data.data() + offset, &magic, sizeof(magic));
    std::memset(data.data() + offset + 4, 0, 64);
    std::memcpy(data.data() + offset + 4, sn, std::strlen(sn));
}

}  // namespace

TEST_CASE("phasecheck defaults match RD (SP09 / PASS VALUE 0 / 7 stations)")
{
    const std::vector<uint8_t> buf = pac_generate::build_phasecheck_buffer("");

    CHECK(buf.size() == pac_generate::kProductionInfoSize);
    CHECK(read_u32(buf, 0) == kSp09Magic);
    // SP09：magic(4) + sn1(24) + sn2(24) + station_num(int)
    int32_t stations = 0;
    std::memcpy(&stations, buf.data() + 4 + 24 + 24, sizeof(stations));
    CHECK(stations == 7);
    CHECK(std::memcmp(buf.data() + 56, "DOWNLOAD", 8) == 0);
    // 结构体之后仍保持 0xFF 填充（RD: memset 0xFF）
    CHECK(buf[0x1000] == 0xFF);
}

TEST_CASE("phasecheck reads SP15 multi-station config from PhaseCheck.ini")
{
    const std::string path = "test_phasecheck_tmp.ini";
    {
        std::ofstream f(path);
        REQUIRE(f.good());
        f << "[VERSION]\n"
             "MAGIC NUMBER = SP15\n"
             "[STATE FLAG]\n"
             "PASS VALUE = 0\n"
             "[STATION]\n"
             "STATION NUMBER = 3\n"
             "STATION 1 = DOWNLOAD\n"
             "STATION 2 = CFT\n"
             "STATION 3 = IMEI\n";
    }

    const std::vector<uint8_t> buf = pac_generate::build_phasecheck_buffer(path);
    std::remove(path.c_str());

    CHECK(buf.size() == pac_generate::kProductionInfoSize);
    CHECK(read_u32(buf, 0) == kSp15Magic);
    // SP15：magic(4) + sn1(64) + sn2(64) + station_num(int)
    int32_t stations = 0;
    std::memcpy(&stations, buf.data() + 4 + 64 + 64, sizeof(stations));
    CHECK(stations == 3);
    CHECK(std::memcmp(buf.data() + 136, "DOWNLOAD", 8) == 0);
}

TEST_CASE("find_phasecheck_sn: SP09 picks SN1 of the last block in 8K")
{
    std::vector<uint8_t> data(pac_generate::kProductionInfoSize, 0xFF);
    put_block(data, 0, kSp09Magic, "FIRST");
    put_block(data, data.size() - 256, kSp09Magic, "LASTSN");

    CHECK(pac_generate::find_phasecheck_sn(data) == "LASTSN");
}

TEST_CASE("find_phasecheck_sn: SP15 falls back to the first block when only one exists")
{
    std::vector<uint8_t> data(pac_generate::kProductionInfoSize, 0xFF);
    put_block(data, 0, kSp15Magic, "SN15ABC");

    CHECK(pac_generate::find_phasecheck_sn(data) == "SN15ABC");
}

TEST_CASE("find_phasecheck_sn: SP05 reads header.SN")
{
    std::vector<uint8_t> data(pac_generate::kProductionInfoSize, 0xFF);
    put_block(data, 0, kSp05Magic, "SN05XYZ");

    CHECK(pac_generate::find_phasecheck_sn(data) == "SN05XYZ");
}

TEST_CASE("find_phasecheck_sn: no magic or short buffer returns empty")
{
    const std::vector<uint8_t> blank(pac_generate::kProductionInfoSize, 0xFF);
    CHECK(pac_generate::find_phasecheck_sn(blank).empty());

    const std::vector<uint8_t> too_short(16, 0);
    CHECK(pac_generate::find_phasecheck_sn(too_short).empty());
}

TEST_CASE("find_phasecheck_sn: empty SN returns empty string")
{
    std::vector<uint8_t> data(pac_generate::kProductionInfoSize, 0xFF);
    put_block(data, 0, kSp09Magic, "");

    CHECK(pac_generate::find_phasecheck_sn(data).empty());
}

TEST_CASE("build_phasecheck_buffer keeps SN in SN1 and leaves SN2 empty")
{
    const std::vector<uint8_t> buf = pac_generate::build_phasecheck_buffer("", "SN123456");

    CHECK(read_u32(buf, 0) == kSp09Magic);
    CHECK(std::memcmp(buf.data() + 4, "SN123456", 8) == 0);
    CHECK(pac_generate::find_phasecheck_sn(buf) == "SN123456");
    // SN2 保持为空
    CHECK(buf[4 + 24] == 0);
}

TEST_CASE("apr: unix time to FILETIME and 8-byte little-endian payload")
{
    CHECK(pac_generate::apr_filetime_from_unix(0, 0) == 116444736000000000ULL);
    CHECK(pac_generate::apr_filetime_from_unix(1, 0) == 116444736000000000ULL + 10000000ULL);
    CHECK(pac_generate::apr_filetime_from_unix(0, 500) == 116444736000000000ULL + 5);

    const std::string path = "test_apr_tmp.bin";
    {
        std::ofstream f(path, std::ios::binary);
        REQUIRE(f.good());
        f << "x";
    }
    const std::vector<uint8_t> payload = pac_generate::build_apr_payload(path);
    std::remove(path.c_str());

    REQUIRE(payload.size() == 8);
    uint64_t filetime = 0;
    for (int i = 0; i < 8; ++i)
    {
        filetime |= static_cast<uint64_t>(payload[static_cast<size_t>(i)]) << (8 * i);
    }
    CHECK(filetime >= 116444736000000000ULL);  // 1970 之后
}

TEST_CASE("apr: missing file yields an all-zero payload")
{
    const std::vector<uint8_t> payload = pac_generate::build_apr_payload("no_such_pac_file.pac");
    REQUIRE(payload.size() == 8);
    for (uint8_t b : payload) CHECK(b == 0);
}

TEST_CASE("phasecheck_ini_usable: requires a non-empty station table")
{
    const std::string good = "test_pc_good.ini";
    {
        std::ofstream f(good);
        REQUIRE(f.good());
        f << "[VERSION]\nMAGIC NUMBER = SP09\n[STATION]\nSTATION NUMBER = 2\n"
             "STATION 1 = DOWNLOAD\nSTATION 2 = CFT\n";
    }
    const std::string no_station = "test_pc_nostation.ini";
    {
        std::ofstream f(no_station);
        REQUIRE(f.good());
        f << "[VERSION]\nMAGIC NUMBER = SP09\n";
    }

    CHECK(pac_generate::phasecheck_ini_usable(good));
    CHECK_FALSE(pac_generate::phasecheck_ini_usable(no_station));
    CHECK_FALSE(pac_generate::phasecheck_ini_usable("no_such_ini.ini"));
    CHECK_FALSE(pac_generate::phasecheck_ini_usable(""));

    std::remove(good.c_str());
    std::remove(no_station.c_str());
}

TEST_CASE("build_ise_payload: supports Data= / File= / Size=")
{
    const std::string ini = "test_ise.ini";
    const std::string bin = "test_ise.bin";
    {
        std::ofstream f(ini);
        REQUIRE(f.good());
        f << "[ISE]\nData=00 11 22 33\n";
    }
    {
        std::ofstream f(bin, std::ios::binary);
        REQUIRE(f.good());
        f << "ABCD";
    }

    std::vector<uint8_t> out;
    std::string err;

    REQUIRE(pac_generate::build_ise_payload(ini, "ISE", out, err));
    REQUIRE(out.size() == 4);
    CHECK(out[0] == 0x00);
    CHECK(out[3] == 0x33);

    {
        std::ofstream f(ini);
        f << "[ISE_DATA]\nFile=test_ise.bin\n";
    }
    CHECK(pac_generate::build_ise_payload(ini, "ISE_DATA", out, err));
    REQUIRE(out.size() == 4);
    CHECK(std::memcmp(out.data(), "ABCD", 4) == 0);

    {
        std::ofstream f(ini);
        f << "[ISE]\nSize=0x10\n";
    }
    CHECK(pac_generate::build_ise_payload(ini, "ISE", out, err));
    CHECK(out.size() == 16);
    for (uint8_t b : out) CHECK(b == 0xFF);
    std::remove(bin.c_str());

    // 没有 [ISE] 段 → 失败并保持空壳告警
    {
        std::ofstream f(ini);
        f << "[VERSION]\nMAGIC NUMBER = SP09\n";
    }
    CHECK_FALSE(pac_generate::build_ise_payload(ini, "ISE", out, err));
    CHECK_FALSE(err.empty());

    std::remove(ini.c_str());
}
