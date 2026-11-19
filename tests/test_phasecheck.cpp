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

void set_env(const char* key, const char* value)
{
#ifdef _WIN32
    _putenv_s(key, value);
#else
    setenv(key, value, 1);
#endif
}

void unset_env(const char* key)
{
#ifdef _WIN32
    _putenv_s(key, "");
#else
    unsetenv(key);
#endif
}

// 在指定块偏移处放一个 magic + SN（结构体其余部分按 RD 的 ZeroMemory 语义补 0）
void put_block(std::vector<uint8_t>& data, std::size_t offset, uint32_t magic, const char* sn)
{
    std::memcpy(data.data() + offset, &magic, sizeof(magic));
    std::memset(data.data() + offset + 4, 0, 64);
    std::memcpy(data.data() + offset + 4, sn, std::strlen(sn));
}
}  // namespace

TEST_CASE("phasecheck defaults match RD (SP09 / PASS VALUE 0 / 7 stations)")
{
    unset_env("SFD_PHASECHECK_INI");

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

    set_env("SFD_PHASECHECK_INI", path.c_str());
    const std::vector<uint8_t> buf = pac_generate::build_phasecheck_buffer();
    unset_env("SFD_PHASECHECK_INI");
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
    unset_env("SFD_PHASECHECK_INI");

    const std::vector<uint8_t> buf = pac_generate::build_phasecheck_buffer("SN123456");

    CHECK(read_u32(buf, 0) == kSp09Magic);
    CHECK(std::memcmp(buf.data() + 4, "SN123456", 8) == 0);
    CHECK(pac_generate::find_phasecheck_sn(buf) == "SN123456");
    // SN2 保持为空
    CHECK(buf[4 + 24] == 0);
}
