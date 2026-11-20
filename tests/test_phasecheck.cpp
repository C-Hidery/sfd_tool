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
    const std::vector<uint8_t> buf = pac_generate::build_phasecheck_buffer();

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

TEST_CASE("phasecheck ignores a user ini (always built-in SP09 / 7 stations)")
{
    // 用户提供的 ini 只用于 ISE；PhaseCheck 必须始终使用内置默认值。
    const std::string path = "test_phasecheck_tmp.ini";
    {
        std::ofstream f(path);
        REQUIRE(f.good());
        f << "[VERSION]\nMAGIC NUMBER = SP15\n[STATE FLAG]\nPASS VALUE = 1\n"
             "[STATION]\nSTATION NUMBER = 3\nSTATION 1 = DOWNLOAD\n"
             "STATION 2 = CFT\nSTATION 3 = IMEI\n";
    }

    const std::vector<uint8_t> buf = pac_generate::build_phasecheck_buffer();
    std::remove(path.c_str());

    CHECK(read_u32(buf, 0) == kSp09Magic);
    int32_t stations = 0;
    std::memcpy(&stations, buf.data() + 4 + 24 + 24, sizeof(stations));
    CHECK(stations == 7);   // 内置 7 站点，不受 ini 的 3 站点影响
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
    const std::vector<uint8_t> buf = pac_generate::build_phasecheck_buffer("SN123456");

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

TEST_CASE("ise_ini_usable: needs [ISE]/[ISE_DATA] with Data/File/Size")
{
    const std::string ise_ok = "test_ise_ok.ini";
    {
        std::ofstream f(ise_ok);
        REQUIRE(f.good());
        f << "[ISE]\nData=00112233\n";
    }
    const std::string only_station = "test_ise_station_only.ini";
    {
        std::ofstream f(only_station);
        REQUIRE(f.good());
        f << "[VERSION]\nMAGIC NUMBER = SP09\n[STATION]\nSTATION NUMBER = 2\n"
             "STATION 1 = DOWNLOAD\nSTATION 2 = CFT\n";
    }
    const std::string empty_ise = "test_ise_empty.ini";
    {
        std::ofstream f(empty_ise);
        REQUIRE(f.good());
        f << "[ISE]\n; nothing useful here\n";
    }

    CHECK(pac_generate::ise_ini_usable(ise_ok));
    CHECK_FALSE(pac_generate::ise_ini_usable(only_station));
    CHECK_FALSE(pac_generate::ise_ini_usable(empty_ise));
    CHECK_FALSE(pac_generate::ise_ini_usable("no_such_ini.ini"));
    CHECK_FALSE(pac_generate::ise_ini_usable(""));

    std::remove(ise_ok.c_str());
    std::remove(only_station.c_str());
    std::remove(empty_ise.c_str());
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

TEST_CASE("phasecheck: DOWNLOAD station bits match RD Cnst8KBuffer")
{
    // 内置默认：SP09 / PASS VALUE 0 / 7 站点（DOWNLOAD 在第 0 位）
    // RD: iItem &= ~(1<<0) (0x7FFF -> 0x7FFE), iTestSign &= ~(1<<0) (0xFFFF -> 0xFFFE)
    const std::vector<uint8_t> buf = pac_generate::build_phasecheck_buffer();

    CHECK(buf[219] == 0x01);  // sign_flag = PASS_0_FAIL_1
    uint16_t test_sign = 0;
    uint16_t item = 0;
    std::memcpy(&test_sign, buf.data() + 252, sizeof(test_sign));
    std::memcpy(&item, buf.data() + 254, sizeof(item));
    CHECK(test_sign == 0xFFFE);
    CHECK(item == 0x7FFE);
}

TEST_CASE("detect_phasecheck_format: follows the magic found in the 8K")
{
    std::vector<uint8_t> data(pac_generate::kProductionInfoSize, 0xFF);

    put_block(data, 0, kSp09Magic, "X");
    CHECK(pac_generate::detect_phasecheck_format(data) == pac_generate::PhaseCheckFormat::Sp09);

    put_block(data, 0, kSp15Magic, "X");
    CHECK(pac_generate::detect_phasecheck_format(data) == pac_generate::PhaseCheckFormat::Sp15);

    put_block(data, 0, kSp05Magic, "X");
    CHECK(pac_generate::detect_phasecheck_format(data) == pac_generate::PhaseCheckFormat::Sp05);

    // 头块没有 magic，但末尾有 SP15 块 → SP15
    std::vector<uint8_t> tail_only(pac_generate::kProductionInfoSize, 0xFF);
    put_block(tail_only, tail_only.size() - 492, kSp15Magic, "Y");
    CHECK(pac_generate::detect_phasecheck_format(tail_only) == pac_generate::PhaseCheckFormat::Sp15);

    // 全是 0xFF → 回退 SP09
    const std::vector<uint8_t> blank(pac_generate::kProductionInfoSize, 0xFF);
    CHECK(pac_generate::detect_phasecheck_format(blank) == pac_generate::PhaseCheckFormat::Sp09);
}

TEST_CASE("build_phasecheck_buffer: explicit SP15 format")
{
    const std::vector<uint8_t> buf = pac_generate::build_phasecheck_buffer(
        "SN15SN", std::string(), pac_generate::PhaseCheckFormat::Sp15);

    CHECK(buf.size() == pac_generate::kProductionInfoSize);
    CHECK(read_u32(buf, 0) == kSp15Magic);

    int32_t stations = 0;
    std::memcpy(&stations, buf.data() + 4 + 64 + 64, sizeof(stations));
    CHECK(stations == 7);
    CHECK(std::memcmp(buf.data() + 136, "DOWNLOAD", 8) == 0);
    CHECK(std::memcmp(buf.data() + 4, "SN15SN", 6) == 0);
    CHECK(buf[449] == 0x01);  // sign_flag = PASS_0_FAIL_1

    uint32_t test_sign32 = 0;
    uint32_t item32 = 0;
    std::memcpy(&test_sign32, buf.data() + 484, sizeof(test_sign32));
    std::memcpy(&item32, buf.data() + 488, sizeof(item32));
    CHECK(test_sign32 == 0xFFFFFFFEu);  // DOWNLOAD 位被清
    CHECK(item32 == 0xFFFFFFFEu);
}

TEST_CASE("build_phasecheck_buffer: explicit SP05 format marks DOWNLOAD as PASS")
{
    const std::vector<uint8_t> buf = pac_generate::build_phasecheck_buffer(
        "SN05", std::string(), pac_generate::PhaseCheckFormat::Sp05);

    CHECK(read_u32(buf, 0) == kSp05Magic);
    CHECK(buf[4] == 1);   // big_endian
    CHECK(buf[5] == 7);   // station_num

    // header(216) + item0: station_name[16] test_times[4] failure[4] test_state[16]
    CHECK(std::memcmp(buf.data() + 216, "DOWNLOAD", 8) == 0);
    CHECK(std::memcmp(buf.data() + 240, "PASS", 4) == 0);
}
