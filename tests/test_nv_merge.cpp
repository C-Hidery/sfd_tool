#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "common.h"
#include "nv_preserve.h"
#include "spd_calibration.h"

namespace
{
class TempXnl
{
public:
    explicit TempXnl(const std::string& name) : path_("test_nv_merge_" + name + ".xml") {}
    ~TempXnl() { std::remove(path_.c_str()); }
    std::string path() const { return path_; }

private:
    std::string path_;
};

void write_xml(const std::string& path, const std::string& body)
{
    std::ofstream f(path);
    REQUIRE(f.good());
    f << "<?xml version=\"1.0\"?><BMAConfig>" << body << "</BMAConfig>";
}

bool has(const spdio_t& io, int id) { return io.nvid_list && io.nvid_list[id] != 0; }

std::vector<uint8_t> make_nv(std::initializer_list<std::pair<uint16_t, std::vector<uint8_t>>> items)
{
    std::vector<uint8_t> buf(4, 0);  // timestamp
    for (const auto& it : items)
    {
        const uint16_t type = it.first;
        const uint16_t len = static_cast<uint16_t>(it.second.size());
        buf.push_back(static_cast<uint8_t>(type & 0xFF));
        buf.push_back(static_cast<uint8_t>(type >> 8));
        buf.push_back(static_cast<uint8_t>(len & 0xFF));
        buf.push_back(static_cast<uint8_t>(len >> 8));
        buf.insert(buf.end(), it.second.begin(), it.second.end());
        while (buf.size() % 4 != 0) buf.push_back(0);
    }
    return buf;
}

std::vector<uint8_t> bytes(const char* s)
{
    return std::vector<uint8_t>(s, s + std::strlen(s));
}

// 构造某个校准版本的合法长度负载：全 0，仅写入版本号。
std::vector<uint8_t> make_cali_payload(uint16_t item_id, uint16_t ver)
{
    std::size_t len = spd_cali::cali_expected_size(item_id, ver);
    if (len == 0) len = 16;
    std::vector<uint8_t> buf(len, 0);
    buf[0] = static_cast<uint8_t>(ver & 0xFF);
    buf[1] = static_cast<uint8_t>(ver >> 8);
    return buf;
}
}  // namespace

// ---------------- get_nvlist_xml ----------------
TEST_CASE("get_nvlist_xml: honours backup, ignores Replace like RD")
{
    TempXnl f("backup");
    write_xml(f.path(),
        "<ProductList><Product name=\"p\"><NVBackup backup=\"1\">"
        "<NVItem name=\"A\" backup=\"1\"><ID>0x100</ID>"
        "<BackupFlag use=\"1\"><NVFlag name=\"Continue\" check=\"1\"/></BackupFlag></NVItem>"
        "<NVItem name=\"B\" backup=\"0\"><ID>0x101</ID></NVItem>"
        "<NVItem name=\"C\" backup=\"1\"><ID>0x102</ID>"
        "<BackupFlag use=\"1\"><NVFlag name=\"Replace\" check=\"1\"/></BackupFlag></NVItem>"
        "</NVBackup></Product></ProductList>");

    spdio_t io{};
    io.verbose = 0;
    CHECK(get_nvlist_xml(&io, f.path().c_str()) == 1);
    CHECK(has(io, 0x100));       // backup=1 → 保留旧值
    CHECK_FALSE(has(io, 0x101)); // backup=0 → 跳过
    CHECK(has(io, 0x102));       // Replace=1 在 RD 里被忽略 → 仍保留旧值
    CHECK(has(io, 5));           // 固定 ID
    CHECK(has(io, 0x1e4));
    delete[] io.nvid_list;
    delete[] io.nv_continue;
}

TEST_CASE("get_nvlist_xml: NVBackup backup=0 disables the whole merge list")
{
    TempXnl f("disabled");
    write_xml(f.path(),
        "<ProductList><Product name=\"p\"><NVBackup backup=\"0\">"
        "<NVItem name=\"A\" backup=\"1\"><ID>0x100</ID></NVItem>"
        "</NVBackup></Product></ProductList>");

    spdio_t io{};
    io.verbose = 0;
    CHECK(get_nvlist_xml(&io, f.path().c_str()) == 0);
    CHECK_FALSE(has(io, 0x100));
    CHECK_FALSE(has(io, 5));  // 提前返回，固定 ID 也不加
    delete[] io.nvid_list;
    delete[] io.nv_continue;
}

TEST_CASE("get_nvlist_xml: no NVItem still yields the fixed ID list")
{
    TempXnl f("empty");
    write_xml(f.path(), "<ProductList><Product name=\"p\"><NVBackup backup=\"1\"/></Product></ProductList>");

    spdio_t io{};
    io.verbose = 0;
    CHECK(get_nvlist_xml(&io, f.path().c_str()) == 1);
    CHECK(has(io, 0x179));
    CHECK_FALSE(has(io, 0x100));
    delete[] io.nvid_list;
    delete[] io.nv_continue;
}

// ---------------- merge_nv_rd ----------------
TEST_CASE("merge_nv_rd: listed items come from old image, unlisted from new, no append")
{
    const auto old_nv = make_nv({{0x100, bytes("OLD1")}, {0x200, bytes("ONLYOLD")}});
    const auto new_nv = make_nv({{0x100, bytes("NEW1")}, {0x101, bytes("NEW2")}});

    spdio_t io{};
    io.verbose = 0;
    io.nvid_list = new int[0x10000]();
    io.nvid_list[0x100] = 1;

    std::vector<uint8_t> out(new_nv.size() + MERGE_NV_SLACK, 0);
    size_t out_size = 0;
    REQUIRE(merge_nv_rd(&io, old_nv.data(), old_nv.size(), new_nv.data(), new_nv.size(),
                        out.data(), out.size(), &out_size));

    CHECK(out_size == new_nv.size());

    nv_preserve::ItemLocation it;
    REQUIRE(nv_preserve::find_item(0x100, out.data(), out_size, it));
    CHECK(std::memcmp(out.data() + it.offset, "OLD1", 4) == 0);
    REQUIRE(nv_preserve::find_item(0x101, out.data(), out_size, it));
    CHECK(std::memcmp(out.data() + it.offset, "NEW2", 4) == 0);
    CHECK_FALSE(nv_preserve::find_item(0x200, out.data(), out_size, it));  // 不追加旧镜像独有项

    delete[] io.nvid_list;
    delete[] io.nv_continue;
}

TEST_CASE("merge_nv_rd: length mismatch keeps the PAC value")
{
    const auto old_nv = make_nv({{0x100, bytes("OLDOLD")}});
    const auto new_nv = make_nv({{0x100, bytes("NEW")}});

    spdio_t io{};
    io.verbose = 0;
    io.nvid_list = new int[0x10000]();
    io.nvid_list[0x100] = 1;

    std::vector<uint8_t> out(new_nv.size() + MERGE_NV_SLACK, 0);
    size_t out_size = 0;
    CHECK_FALSE(merge_nv_rd(&io, old_nv.data(), old_nv.size(), new_nv.data(), new_nv.size(),
                            out.data(), out.size(), &out_size));
    delete[] io.nvid_list;
    delete[] io.nv_continue;
    delete[] io.nv_continue;
}

TEST_CASE("merge_nv_rd: GSM calibration keeps old payload when versions match")
{
    const auto old_cali = make_cali_payload(0x2, 0xFF0A);
    auto new_cali = make_cali_payload(0x2, 0xFF0A);
    new_cali[2] = 0xAA;  // 新值有差异（远离 ramp_delta_timing，policy 0 下会被旧值覆盖）

    const auto old_nv = make_nv({{0x2, old_cali}});
    const auto new_nv = make_nv({{0x2, new_cali}});

    spdio_t io{};
    io.verbose = 0;
    io.nvid_list = new int[0x10000]();
    io.nvid_list[0x2] = 1;

    std::vector<uint8_t> out(new_nv.size() + MERGE_NV_SLACK, 0);
    size_t out_size = 0;
    REQUIRE(merge_nv_rd(&io, old_nv.data(), old_nv.size(), new_nv.data(), new_nv.size(),
                        out.data(), out.size(), &out_size));

    nv_preserve::ItemLocation it;
    REQUIRE(nv_preserve::find_item(0x2, out.data(), out_size, it));
    // GSM_VerAToA 在 policy 0 下只保留 file 的 ramp_delta_timing，其余字段用旧值。
    CHECK(std::memcmp(out.data() + it.offset, old_cali.data(), 8) == 0);
    delete[] io.nvid_list;
    delete[] io.nv_continue;
}

TEST_CASE("merge_nv_rd: TD calibration keeps old payload when versions match")
{
    const auto old_cali = make_cali_payload(0x516, 4);
    auto new_cali = make_cali_payload(0x516, 4);
    new_cali[2] = 0xAA;

    const auto old_nv = make_nv({{0x516, old_cali}});
    const auto new_nv = make_nv({{0x516, new_cali}});

    spdio_t io{};
    io.verbose = 0;
    io.nvid_list = new int[0x10000]();
    io.nvid_list[0x516] = 1;

    std::vector<uint8_t> out(new_nv.size() + MERGE_NV_SLACK, 0);
    size_t out_size = 0;
    REQUIRE(merge_nv_rd(&io, old_nv.data(), old_nv.size(), new_nv.data(), new_nv.size(),
                        out.data(), out.size(), &out_size));

    nv_preserve::ItemLocation it;
    REQUIRE(nv_preserve::find_item(0x516, out.data(), out_size, it));
    CHECK(std::memcmp(out.data() + it.offset, old_cali.data(), 8) == 0);
    delete[] io.nvid_list;
    delete[] io.nv_continue;
}

TEST_CASE("merge_nv_rd: calibration version mismatch keeps the PAC value")
{
    const auto old_cali = make_cali_payload(0x2, 0xFF0A);
    const auto new_cali = make_cali_payload(0x2, 0xFF0B);

    const auto old_nv = make_nv({{0x2, old_cali}});
    const auto new_nv = make_nv({{0x2, new_cali}});

    spdio_t io{};
    io.verbose = 0;
    io.nvid_list = new int[0x10000]();
    io.nvid_list[0x2] = 1;

    std::vector<uint8_t> out(new_nv.size() + MERGE_NV_SLACK, 0);
    size_t out_size = 0;
    CHECK_FALSE(merge_nv_rd(&io, old_nv.data(), old_nv.size(), new_nv.data(), new_nv.size(),
                            out.data(), out.size(), &out_size));
    delete[] io.nvid_list;
    delete[] io.nv_continue;
    delete[] io.nv_continue;
}

TEST_CASE("merge_nv_rd: missing old item fails unless Continue=1")
{
    const auto old_nv = make_nv({{0x101, bytes("OTHER")}});
    const auto new_nv = make_nv({{0x100, bytes("NEW1")}});

    std::vector<uint8_t> out(new_nv.size() + MERGE_NV_SLACK, 0);
    size_t out_size = 0;

    {
        spdio_t io{};
        io.verbose = 0;
        io.nvid_list = new int[0x10000]();
        io.nv_continue = new uint8_t[0x10000]();
        io.nvid_list[0x100] = 1;  // Continue=0

        CHECK_FALSE(merge_nv_rd(&io, old_nv.data(), old_nv.size(), new_nv.data(),
                                new_nv.size(), out.data(), out.size(), &out_size));
        delete[] io.nvid_list;
        delete[] io.nv_continue;
    }

    {
        spdio_t io{};
        io.verbose = 0;
        io.nvid_list = new int[0x10000]();
        io.nv_continue = new uint8_t[0x10000]();
        io.nvid_list[0x100] = 1;
        io.nv_continue[0x100] = 1;  // Continue=1 → 容忍缺失

        REQUIRE(merge_nv_rd(&io, old_nv.data(), old_nv.size(), new_nv.data(),
                            new_nv.size(), out.data(), out.size(), &out_size));
        delete[] io.nvid_list;
        delete[] io.nv_continue;
    }
}
