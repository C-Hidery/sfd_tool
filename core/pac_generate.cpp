/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#include "pac_generate.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

#include "file_io.h"
#include "logging.h"

namespace pac_generate
{
namespace
{
// ---------------------------------------------------------------------------
// RD 的 phasecheck 结构（源码 Source/phasecheck/phdef.h），pack(4)
// ---------------------------------------------------------------------------
#pragma pack(push, 4)
struct Sp05PhaseCheckHeader
{
    uint32_t magic;          // "SP05"
    uint8_t big_endian;      // 1
    uint8_t station_num;
    uint8_t reserved1[2];
    char sn[24];
    char pcb_version[24];
    char hw_version[24];
    char sw_version[24];
    char factory_name[24];
    char product_line[24];
    uint8_t guid[16];
    char sn2[24];
    char reserve[24];
};

struct Sp05PhaseCheckItem
{
    char station_name[16];
    char test_times[4];
    char failure_times[4];
    char test_state[16];
    char fail_description[32];
    char last_test_time[24];
    char reserve[32];
};

struct Sp05PhaseCheck
{
    Sp05PhaseCheckHeader header;
    Sp05PhaseCheckItem items[12];
};

struct Sp09PhaseCheck
{
    uint32_t magic;          // "SP09"
    char sn1[24];
    char sn2[24];
    int32_t station_num;
    char station_name[15][10];
    uint8_t reserved[13];
    uint8_t sign_flag;       // 0x01 = PASS_0_FAIL_1（RD 默认）
    char last_fail_description[32];
    uint16_t test_sign;
    uint16_t item;
};

struct Sp15PhaseCheck
{
    uint32_t magic;          // "SP15"
    char sn1[64];
    char sn2[64];
    int32_t station_num;
    char station_name[20][15];
    uint8_t reserved[13];
    uint8_t sign_flag;
    char last_fail_description[32];
    uint32_t test_sign;
    uint32_t item;
};
#pragma pack(pop)

constexpr uint32_t kSp05Magic = 0x53503035u;  // 'S''P''0''5'
constexpr uint32_t kSp09Magic = 0x53503039u;  // 'S''P''0''9'
constexpr uint32_t kSp15Magic = 0x53503135u;  // 'S''P''1''5'

static_assert(sizeof(Sp05PhaseCheck) == 1752, "SP05 phase check layout mismatch");
static_assert(sizeof(Sp09PhaseCheck) == 256, "SP09 phase check layout mismatch");
static_assert(sizeof(Sp15PhaseCheck) == 492, "SP15 phase check layout mismatch");

// ---------------------------------------------------------------------------
// PhaseCheck.ini（与 RD 的 [VERSION]/[STATE FLAG]/[STATION] 段一致）
// ---------------------------------------------------------------------------
enum class PhaseCheckMagic
{
    Sp05,
    Sp09,
    Sp15
};

struct PhaseCheckConfig
{
    PhaseCheckMagic magic = PhaseCheckMagic::Sp09;
    int pass_value = 0;
    std::vector<std::string> stations;
    std::string ini_path;  // 为空表示使用内置默认
};

std::string trim_copy(std::string s)
{
    const char* ws = " \t\r\n";
    const std::size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos) return std::string();
    const std::size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

std::string upper_copy(std::string s)
{
    for (char& c : s)
    {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string exe_dir()
{
#ifdef _WIN32
    char buf[MAX_PATH] = {0};
    if (GetModuleFileNameA(nullptr, buf, MAX_PATH) == 0) return std::string();
    std::string p(buf);
#else
    char buf[PATH_MAX] = {0};
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return std::string();
    std::string p(buf, static_cast<std::size_t>(n));
#endif
    const std::size_t pos = p.find_last_of("/\\");
    return (pos == std::string::npos) ? std::string() : p.substr(0, pos);
}

std::string env_value(const char* name)
{
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

// 与 RD 一致：PhaseCheck.ini 放在程序目录；额外支持环境变量与当前目录，便于产线覆盖。
std::string find_phasecheck_ini()
{
    const std::string from_env = env_value("SFD_PHASECHECK_INI");
    if (!from_env.empty())
    {
        std::ifstream f(from_env);
        if (f.good()) return from_env;
    }

    const std::string cwd_ini = "PhaseCheck.ini";
    {
        std::ifstream f(cwd_ini);
        if (f.good()) return cwd_ini;
    }

    const std::string dir = exe_dir();
    if (!dir.empty())
    {
        const std::string p = dir + "/PhaseCheck.ini";
        std::ifstream f(p);
        if (f.good()) return p;
    }
    return std::string();
}

void parse_phasecheck_ini(const std::string& path, PhaseCheckConfig& cfg)
{
    std::ifstream f(path);
    if (!f) return;

    std::string section;
    std::vector<std::pair<int, std::string>> stations;  // (序号, 名字)
    int station_number = -1;

    std::string line;
    while (std::getline(f, line))
    {
        const std::size_t comment = line.find_first_of(";#");
        if (comment != std::string::npos)
        {
            line = line.substr(0, comment);
        }
        line = trim_copy(line);
        if (line.empty()) continue;

        if (line.front() == '[' && line.back() == ']')
        {
            section = upper_copy(trim_copy(line.substr(1, line.size() - 2)));
            continue;
        }

        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string key = upper_copy(trim_copy(line.substr(0, eq)));
        const std::string value = trim_copy(line.substr(eq + 1));

        if (section == "VERSION" && key == "MAGIC NUMBER")
        {
            const std::string magic = upper_copy(value);
            if (magic == "SP05") cfg.magic = PhaseCheckMagic::Sp05;
            else if (magic == "SP15") cfg.magic = PhaseCheckMagic::Sp15;
            else cfg.magic = PhaseCheckMagic::Sp09;
        }
        else if (section == "STATE FLAG" && key == "PASS VALUE")
        {
            cfg.pass_value = std::atoi(value.c_str()) != 0 ? 1 : 0;
        }
        else if (section == "STATION")
        {
            if (key == "STATION NUMBER")
            {
                station_number = std::atoi(value.c_str());
            }
            else if (key.rfind("STATION ", 0) == 0)
            {
                const int idx = std::atoi(key.c_str() + 8);
                if (idx > 0 && !value.empty()) stations.emplace_back(idx, value);
            }
        }
    }

    std::sort(stations.begin(), stations.end(),
              [](const std::pair<int, std::string>& a, const std::pair<int, std::string>& b)
              {
                  return a.first < b.first;
              });

    std::vector<std::string> parsed;
    for (const auto& s : stations) parsed.push_back(s.second);
    if (station_number > 0 && static_cast<std::size_t>(station_number) <= parsed.size())
    {
        parsed.resize(static_cast<std::size_t>(station_number));
    }

    cfg.stations = std::move(parsed);
    cfg.ini_path = path;
}

PhaseCheckConfig load_phasecheck_config()
{
    PhaseCheckConfig cfg;
    // RD 发行版 PhaseCheck.ini 的默认值：SP09 / PASS VALUE 0 / 7 个站点
    cfg.magic = PhaseCheckMagic::Sp09;
    cfg.pass_value = 0;
    cfg.stations = {"DOWNLOAD", "WRITESN", "CFT", "BBAT", "WCN", "ANTENNA", "IMEI"};

    const std::string ini = find_phasecheck_ini();
    if (!ini.empty())
    {
        PhaseCheckConfig from_ini;
        parse_phasecheck_ini(ini, from_ini);
        if (!from_ini.stations.empty())
        {
            cfg = from_ini;
        }
        else
        {
            // 文件存在但站点表为空：沿用默认站点，保留 ini 里的 magic/pass value。
            cfg.magic = from_ini.magic;
            cfg.pass_value = from_ini.pass_value;
            cfg.ini_path = ini;
        }
    }
    return cfg;
}

void fill_sp05(std::vector<uint8_t>& buf, const PhaseCheckConfig& cfg,
               const std::string& sn1, const std::string& sn2)
{
    Sp05PhaseCheck pc{};
    pc.header.magic = kSp05Magic;
    pc.header.big_endian = 1;
    const std::size_t n = std::min<std::size_t>(cfg.stations.size(), 12);
    pc.header.station_num = static_cast<uint8_t>(n);
    std::snprintf(pc.header.sn, sizeof(pc.header.sn), "%s", sn1.c_str());
    std::snprintf(pc.header.sn2, sizeof(pc.header.sn2), "%s", sn2.c_str());

    for (std::size_t i = 0; i < n; ++i)
    {
        Sp05PhaseCheckItem& item = pc.items[i];
        std::snprintf(item.station_name, sizeof(item.station_name), "%s", cfg.stations[i].c_str());
        if (!my_stricmp(cfg.stations[i].c_str(), "DOWNLOAD"))
        {
            std::memcpy(item.test_state, "PASS", 4);  // RD: DOWNLOAD 站直接标 PASS
        }
    }
    std::memcpy(buf.data(), &pc, sizeof(pc));
}

void fill_sp09(std::vector<uint8_t>& buf, const PhaseCheckConfig& cfg,
               const std::string& sn1, const std::string& sn2)
{
    Sp09PhaseCheck pc{};
    pc.magic = kSp09Magic;
    std::snprintf(pc.sn1, sizeof(pc.sn1), "%s", sn1.c_str());
    std::snprintf(pc.sn2, sizeof(pc.sn2), "%s", sn2.c_str());
    pc.station_num = static_cast<int32_t>(std::min<std::size_t>(cfg.stations.size(), 15));
    for (int32_t i = 0; i < pc.station_num; ++i)
    {
        std::snprintf(pc.station_name[i], sizeof(pc.station_name[i]), "%s",
                      cfg.stations[static_cast<std::size_t>(i)].c_str());
    }
    if (cfg.pass_value == 0)
    {
        pc.sign_flag = 0x01;  // PASS_0_FAIL_1
        pc.test_sign = 0xFFFF;
        pc.item = 0x7FFF;
    }
    std::snprintf(pc.last_fail_description, sizeof(pc.last_fail_description), "PASS");
    std::memcpy(buf.data(), &pc, sizeof(pc));
}

void fill_sp15(std::vector<uint8_t>& buf, const PhaseCheckConfig& cfg,
               const std::string& sn1, const std::string& sn2)
{
    Sp15PhaseCheck pc{};
    pc.magic = kSp15Magic;
    std::snprintf(pc.sn1, sizeof(pc.sn1), "%s", sn1.c_str());
    std::snprintf(pc.sn2, sizeof(pc.sn2), "%s", sn2.c_str());
    pc.station_num = static_cast<int32_t>(std::min<std::size_t>(cfg.stations.size(), 20));
    for (int32_t i = 0; i < pc.station_num; ++i)
    {
        std::snprintf(pc.station_name[i], sizeof(pc.station_name[i]), "%s",
                      cfg.stations[static_cast<std::size_t>(i)].c_str());
    }
    if (cfg.pass_value == 0)
    {
        pc.sign_flag = 0x01;
        pc.test_sign = 0xFFFFFFFFu;
        pc.item = 0xFFFFFFFFu;
    }
    std::snprintf(pc.last_fail_description, sizeof(pc.last_fail_description), "PASS");
    std::memcpy(buf.data(), &pc, sizeof(pc));
}

// 把生成的缓冲区写入分区：先落临时文件，再走普通的“按分区名下载”。
bool write_buffer_to_partition(spdio_t* io, const std::string& partition,
                               const std::vector<uint8_t>& data, int CMethod)
{
    TempFile tmp(make_temp_file_path(partition.empty() ? "pacgen" : partition.c_str()));
    if (tmp.empty())
    {
        DEG_LOG(E, "pac_generate: failed to create temp file");
        return false;
    }

    EnhancedFile f = oxfopen_enhanced(tmp.c_str(), "wb");
    if (!f)
    {
        DEG_LOG(E, "pac_generate: failed to open temp file: %s", tmp.c_str());
        return false;
    }
    const std::size_t written = f.write(data.data(), 1, data.size());
    f.close();
    if (written != data.size())
    {
        DEG_LOG(E, "pac_generate: failed to write temp file (%zu / %zu)", written, data.size());
        return false;
    }

    const unsigned step = blk_size ? blk_size : DEFAULT_BLK_SIZE;
    load_partition(io, partition.c_str(), tmp.c_str(), step, CMethod);
    return true;
}

// 读设备上 PhaseCheck 所在分区的前 8K，用于提取旧 SN（RD 的 AddReadSN 行为：
// 读同一个 Block，长度按 8K 处理）。
bool read_device_sn(spdio_t* io, const std::string& partition, std::string& sn)
{
    get_partition_info(io, partition.c_str(), 1);
    if (!gPartInfo.size) return false;

    const uint64_t len = std::min<uint64_t>(static_cast<uint64_t>(gPartInfo.size),
                                            static_cast<uint64_t>(kProductionInfoSize));
    if (len == 0) return false;

    uint64_t got = 0;
    TempFile tmp = dump_partition_to_temp(io, partition.c_str(), 0, len,
                                          blk_size ? blk_size : DEFAULT_BLK_SIZE, &got);
    if (tmp.empty() || got == 0) return false;

    size_t fsize = 0;
    uint8_t* raw = loadfile(tmp.c_str(), &fsize, 0);
    if (!raw || fsize == 0)
    {
        delete[] raw;
        return false;
    }
    std::vector<uint8_t> data(raw, raw + fsize);
    delete[] raw;

    data.resize(kProductionInfoSize, 0xFF);  // RD: 读回不足 8K 时用 0xFF 补齐
    sn = find_phasecheck_sn(data);
    return !sn.empty();
}
}  // namespace

std::string find_phasecheck_sn(const std::vector<uint8_t>& data)
{
    if (data.size() < kProductionInfoSize) return std::string();

    auto read_u32 = [](const uint8_t* p) -> uint32_t
    {
        uint32_t v = 0;
        std::memcpy(&v, p, sizeof(v));
        return v;
    };
    auto extract = [](const uint8_t* base, std::size_t max_len) -> std::string
    {
        std::size_t n = 0;
        while (n < max_len && base[n] != 0) ++n;
        return std::string(reinterpret_cast<const char*>(base), n);
    };

    const uint8_t* p = data.data();
    const uint32_t magic0 = read_u32(p);

    // SP05：SN 在 header 起始 +4
    if (magic0 == kSp05Magic)
    {
        return extract(p + 4, 24);
    }

    // SP09：8K 按 256 字节块，从末尾反向找最后一个 magic 匹配块
    if (magic0 == kSp09Magic)
    {
        const std::size_t block = sizeof(Sp09PhaseCheck);
        const std::size_t total = kProductionInfoSize / block;
        const uint8_t* cur = p + kProductionInfoSize - block;
        for (std::size_t i = 0; i + 1 < total; ++i)
        {
            if (read_u32(cur) == kSp09Magic) break;
            cur -= block;
        }
        // RD 的循环不会检查第一块；若整段都没有后续块（例如刚写入的 8K），
        // 退回第一块读 SN1，避免把 0xFF 当 SN。
        if (read_u32(cur) != kSp09Magic) cur = p;
        return extract(cur + 4, 24);
    }

    // SP15：8K 按 492 字节块，规则同上
    if (magic0 == kSp15Magic)
    {
        const std::size_t block = sizeof(Sp15PhaseCheck);
        const std::size_t total = kProductionInfoSize / block;
        const uint8_t* cur = p + kProductionInfoSize - block;
        for (std::size_t i = 0; i + 1 < total; ++i)
        {
            if (read_u32(cur) == kSp15Magic) break;
            cur -= block;
        }
        if (read_u32(cur) != kSp15Magic) cur = p;
        return extract(cur + 4, 64);
    }

    return std::string();
}

std::vector<uint8_t> build_phasecheck_buffer(const std::string& sn1, const std::string& sn2)
{
    // RD: memset(lpData, 0xFF, 8K) 后把结构体放在缓冲区起始处
    std::vector<uint8_t> buf(kProductionInfoSize, 0xFF);
    const PhaseCheckConfig cfg = load_phasecheck_config();

    switch (cfg.magic)
    {
    case PhaseCheckMagic::Sp05:
        fill_sp05(buf, cfg, sn1, sn2);
        break;
    case PhaseCheckMagic::Sp15:
        fill_sp15(buf, cfg, sn1, sn2);
        break;
    case PhaseCheckMagic::Sp09:
    default:
        fill_sp09(buf, cfg, sn1, sn2);
        break;
    }

    if (cfg.ini_path.empty())
    {
        DEG_LOG(I, "PhaseCheck: PhaseCheck.ini not found, using RD defaults (SP09 / PASS VALUE 0 / %zu stations)",
                cfg.stations.size());
    }
    else
    {
        DEG_LOG(I, "PhaseCheck: using %s (%zu stations)", cfg.ini_path.c_str(), cfg.stations.size());
    }
    return buf;
}

bool run_operation(spdio_t* io, const std::string& op_id, const std::string& type,
                   const std::string& partition, uint64_t block_size, int CMethod)
{
    // ---- PhaseCheck：生成 8K 生产信息并写 miscdata（RD 行为）----
    if (!my_stricmp(op_id.c_str(), "PhaseCheck"))
    {
        if (partition.empty())
        {
            DEG_LOG(W, "PhaseCheck: no Block/partition in XML, skipped");
            return true;
        }

        std::size_t size = kProductionInfoSize;
        if (block_size > 0 && block_size <= kProductionInfoSize)
        {
            // RD: Block Size 为 0 或超过 8K 时按 8K；否则按 Block Size。
            size = static_cast<std::size_t>(block_size);
        }

        // RD 会先读设备上同一分区的旧 PhaseCheck，用 FindSnFrom8K 取 SN，
        // 再带着 SN 重新生成 8K 写回去。
        std::string sn;
        if (read_device_sn(io, partition, sn))
        {
            DEG_LOG(OP, "PhaseCheck: read SN \"%s\" from device partition %s", sn.c_str(), partition.c_str());
        }
        else
        {
            DEG_LOG(W, "PhaseCheck: no SN found on device partition %s, writing empty SN", partition.c_str());
        }

        std::vector<uint8_t> buf = build_phasecheck_buffer(sn);
        buf.resize(size, 0xFF);

        get_partition_info(io, partition.c_str(), 1);
        if (!gPartInfo.size)
        {
            DEG_LOG(W, "PhaseCheck: partition %s does not exist, skipped", partition.c_str());
            return true;
        }

        DEG_LOG(OP, "Generate PhaseCheck: %zu bytes -> %s (Type=%s)", buf.size(),
                partition.c_str(), type.c_str());
        if (!write_buffer_to_partition(io, partition, buf, CMethod))
        {
            DEG_LOG(E, "PhaseCheck: failed to write %s", partition.c_str());
        }
        return true;
    }

    // ---- ISE / ISE_DATA ----
    // 查过 RD 的 BMFileType.ini（R23 / R27）：没有 ISE 段，它们落到 [CODE2] =
    // DownloadByID；而 RD 的 _Download 对 llCodeSize==0 会直接返回失败
    // ("[ID] file size is zero.")。这两条在 XML 里 Flag=0、Block Size=0x0，
    // 所以 RD 实际不会写它们（写了必然报错）。这里与之等价：只告警、不写。
    if (!my_stricmp(op_id.c_str(), "ISE") || !my_stricmp(op_id.c_str(), "ISE_DATA"))
    {
        DEG_LOG(W,
                "%s (Type=%s, Block=%s): Flag=0 with Block Size=0; RD [CODE2] only has "
                "DownloadByID which fails on zero length, so RD skips it; nothing written here.",
                op_id.c_str(), type.c_str(), partition.c_str());
        return true;
    }

    return false;
}
}  // namespace pac_generate
