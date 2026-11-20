#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "pac_extract.h"

namespace
{
// ---------------------------------------------------------------------------
// 构造一个最小可用的 PAC（V2 布局）。PacFile::load() 只校验 magic /
// dir_offset / file_count / struct_size，因此不需要真实 CRC。
// ---------------------------------------------------------------------------
constexpr std::size_t kHeadSize = 2124;
constexpr std::size_t kEntrySize = 2580;

struct FakeFile
{
    std::string id;
    std::string name;
    uint32_t type = 1;     // PAC 表里的 nFileFlag：0=操作项 1=有数据
    uint32_t nsize = 0;    // 文件大小
    std::string payload;   // 实际数据（nsize 会自动对齐）
};

void put_u16(std::vector<uint8_t>& buf, std::size_t off, const std::string& s, std::size_t max_chars)
{
    for (std::size_t i = 0; i < s.size() && i < max_chars - 1; ++i)
    {
        buf[off + i * 2] = static_cast<uint8_t>(s[i]);
        buf[off + i * 2 + 1] = 0;
    }
}

void put_u32(std::vector<uint8_t>& buf, std::size_t off, uint32_t v)
{
    std::memcpy(buf.data() + off, &v, sizeof(v));
}

std::string make_pac_file(const std::string& path, const std::string& fw_name,
                          const std::vector<FakeFile>& files)
{
    std::vector<uint8_t> buf(kHeadSize + kEntrySize * files.size(), 0);

    put_u16(buf, 0, "BP_R2.0.1", 24);
    put_u32(buf, 48, static_cast<uint32_t>(buf.size()));
    put_u16(buf, 52, fw_name, 256);
    put_u16(buf, 564, "V1", 256);
    put_u32(buf, 1076, static_cast<uint32_t>(files.size()));
    put_u32(buf, 1080, static_cast<uint32_t>(kHeadSize));
    put_u16(buf, 1104, fw_name, 100);
    put_u32(buf, 2116, 0xFFFAFFFAu);  // ~0x50005

    std::size_t data_off = kHeadSize + kEntrySize * files.size();
    for (std::size_t i = 0; i < files.size(); ++i)
    {
        const FakeFile& f = files[i];
        const std::size_t base = kHeadSize + i * kEntrySize;
        put_u32(buf, base + 0, static_cast<uint32_t>(kEntrySize));
        put_u16(buf, base + 4, f.id, 256);
        put_u16(buf, base + 516, f.name, 256);
        put_u32(buf, base + 1540, f.nsize);
        put_u32(buf, base + 1544, f.type);
        put_u32(buf, base + 1548, (f.nsize != 0) ? 1u : 2u);  // nCheckFlag
        put_u32(buf, base + 1552, static_cast<uint32_t>(data_off));

        if (f.nsize != 0)
        {
            buf.resize(data_off + f.nsize, 0xCD);
            data_off += f.nsize;
        }
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    REQUIRE(out.good());
    out.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    out.close();
    return path;
}

std::string xml_doc(const std::string& product, const std::string& files_xml)
{
    return "<BMAConfig><ProductList><Product name=\"" + product +
           "\"><SchemeName>S1</SchemeName></Product></ProductList>"
           "<SchemeList><Scheme name=\"S1\">" + files_xml + "</Scheme></SchemeList></BMAConfig>";
}

class TempPac
{
public:
    explicit TempPac(const std::string& name) : path_("test_pac_plan_" + name + ".pac") {}
    ~TempPac() { std::remove(path_.c_str()); }
    std::string path() const { return path_; }

private:
    std::string path_;
};

bool load(PacFile& pac, const std::string& path)
{
    return pac.load(path.c_str());
}
}  // namespace

TEST_CASE("pac_plan: execution order follows the XML scheme, not the PAC table")
{
    TempPac pac_path("order");
    std::vector<FakeFile> files = {
        {"BOOT", "boot.img", 1, 16, "0123456789abcdef"},
        {"EraseSPL", "", 0, 0, ""},
        {"FDL", "fdl1.bin", 0x101, 8, "fdl1data"},
        {"SPLLoader", "spl.bin", 1, 8, "spldata!"},
        {"PhaseCheck", "", 0, 0, ""},
    };
    make_pac_file(pac_path.path(), "demo", files);

    PacFile pac;
    REQUIRE(load(pac, pac_path.path()));

    PacXMLParser xml;
    REQUIRE(xml.loadFromString(xml_doc("demo",
        "<File><ID>FDL</ID><Type>FDL</Type><Flag>1</Flag><CheckFlag>1</CheckFlag>"
        "<Block id=\"fdl\"><Base>0x5000</Base><Size>0x0</Size></Block></File>"
        "<File><ID>EraseSPL</ID><Type>EraseFlash2</Type><Flag>0</Flag><CheckFlag>2</CheckFlag>"
        "<Block id=\"uboot\"><Base>0x0</Base><Size>0x0</Size></Block></File>"
        "<File><ID>PhaseCheck</ID><Type>CODE2</Type><Flag>0</Flag><CheckFlag>2</CheckFlag>"
        "<Block id=\"miscdata\"><Base>0x0</Base><Size>0x100000</Size></Block></File>"
        "<File><ID>BOOT</ID><Type>CODE2</Type><Flag>1</Flag><CheckFlag>0</CheckFlag>"
        "<Block id=\"boot\"><Base>0x0</Base><Size>0x0</Size></Block></File>"
        "<File><ID>SPLLoader</ID><Type>UBOOT_LOADER2</Type><Flag>1</Flag><CheckFlag>0</CheckFlag>"
        "<Block id=\"splloader\"><Base>0x80000000</Base><Size>0x0</Size></Block></File>")));

    const std::vector<pac_plan::Item> plan = pac_plan::build(pac, xml);
    REQUIRE(plan.size() == 5);

    CHECK(plan[0].pac_id == "FDL");
    CHECK(plan[0].kind == pac_plan::Kind::Skip);

    CHECK(plan[1].pac_id == "EraseSPL");
    CHECK(plan[1].kind == pac_plan::Kind::Erase);
    CHECK(plan[1].partition == "uboot");

    CHECK(plan[2].pac_id == "PhaseCheck");
    CHECK(plan[2].kind == pac_plan::Kind::Unsupported);
    CHECK(plan[2].partition == "miscdata");

    CHECK(plan[3].pac_id == "BOOT");
    CHECK(plan[3].kind == pac_plan::Kind::Flash);
    CHECK(plan[3].partition == "boot");
    CHECK(plan[3].pac_size == 16);

    CHECK(plan[4].pac_id == "SPLLoader");
    CHECK(plan[4].kind == pac_plan::Kind::Flash);
    CHECK(plan[4].partition == "splloader");
}

TEST_CASE("pac_plan: Flag=1 entry without data in PAC is skipped")
{
    TempPac pac_path("nodata");
    std::vector<FakeFile> files = {{"GHOST", "", 1, 0, ""}};
    make_pac_file(pac_path.path(), "demo", files);

    PacFile pac;
    REQUIRE(load(pac, pac_path.path()));

    PacXMLParser xml;
    REQUIRE(xml.loadFromString(xml_doc("demo",
        "<File><ID>GHOST</ID><Type>CODE2</Type><Flag>1</Flag><CheckFlag>0</CheckFlag>"
        "<Block id=\"boot\"><Base>0x0</Base><Size>0x0</Size></Block></File>")));

    const std::vector<pac_plan::Item> plan = pac_plan::build(pac, xml);
    REQUIRE(plan.size() == 1);
    CHECK(plan[0].kind == pac_plan::Kind::Skip);
    CHECK(plan[0].reason == "no data for this file in PAC");
}

TEST_CASE("pac_plan: PAC data not listed in XML becomes a leftover flash item")
{
    TempPac pac_path("leftover");
    std::vector<FakeFile> files = {
        {"BOOT", "boot.img", 1, 8, "bootdata"},
        {"EXTRA", "extra.bin", 1, 8, "extradat"},
    };
    make_pac_file(pac_path.path(), "demo", files);

    PacFile pac;
    REQUIRE(load(pac, pac_path.path()));

    PacXMLParser xml;
    REQUIRE(xml.loadFromString(xml_doc("demo",
        "<File><ID>BOOT</ID><Type>CODE2</Type><Flag>1</Flag><CheckFlag>0</CheckFlag>"
        "<Block id=\"boot\"><Base>0x0</Base><Size>0x0</Size></Block></File>")));

    const std::vector<pac_plan::Item> plan = pac_plan::build(pac, xml);
    REQUIRE(plan.size() == 2);
    CHECK(plan[0].pac_id == "BOOT");
    CHECK(plan[0].kind == pac_plan::Kind::Flash);
    CHECK(plan[0].leftover == false);

    CHECK(plan[1].pac_id == "EXTRA");
    CHECK(plan[1].kind == pac_plan::Kind::Flash);
    CHECK(plan[1].leftover == true);
}

TEST_CASE("pac_plan: PAC product name with PAC_ prefix still matches XML")
{
    TempPac pac_path("prefix");
    std::vector<FakeFile> files = {{"BOOT", "boot.img", 1, 8, "bootdata"}};
    make_pac_file(pac_path.path(), "PAC_demo", files);

    PacFile pac;
    REQUIRE(load(pac, pac_path.path()));

    PacXMLParser xml;
    REQUIRE(xml.loadFromString(xml_doc("demo",
        "<File><ID>BOOT</ID><Type>CODE2</Type><Flag>1</Flag><CheckFlag>0</CheckFlag>"
        "<Block id=\"boot\"><Base>0x0</Base><Size>0x0</Size></Block></File>")));

    const std::vector<pac_plan::Item> plan = pac_plan::build(pac, xml);
    REQUIRE(plan.size() == 1);
    CHECK(plan[0].pac_id == "BOOT");
    CHECK(plan[0].kind == pac_plan::Kind::Flash);
    CHECK(plan[0].pac_index == 0);
}

TEST_CASE("pac_plan: XML IDAlias matches the ID in the PAC table")
{
    TempPac pac_path("alias");
    std::vector<FakeFile> files = {{"NV_NR", "nv.bin", 1, 8, "nvdata!!"}};
    make_pac_file(pac_path.path(), "demo", files);

    PacFile pac;
    REQUIRE(load(pac, pac_path.path()));

    PacXMLParser xml;
    REQUIRE(xml.loadFromString(xml_doc("demo",
        "<File><ID>NVX</ID><IDAlias>NV_NR</IDAlias><Type>NV_COMM_SLOT</Type>"
        "<Flag>1</Flag><CheckFlag>2</CheckFlag>"
        "<Block id=\"nr_fixnv1_a\"><Base>0x0</Base><Size>0x0</Size></Block></File>")));

    const std::vector<pac_plan::Item> plan = pac_plan::build(pac, xml);
    REQUIRE(plan.size() == 1);
    CHECK(plan[0].pac_id == "NVX");
    CHECK(plan[0].kind == pac_plan::Kind::Flash);
    CHECK(plan[0].partition == "nr_fixnv1_a");
    CHECK(plan[0].pac_index == 0);
}

TEST_CASE("pac_plan: empty PAC or empty XML yields an empty plan")
{
    TempPac pac_path("empty");
    make_pac_file(pac_path.path(), "demo", {});

    PacFile pac;
    REQUIRE(load(pac, pac_path.path()));

    PacXMLParser xml;
    REQUIRE(xml.loadFromString(xml_doc("demo", "")));

    const std::vector<pac_plan::Item> plan = pac_plan::build(pac, xml);
    CHECK(plan.empty());
}
