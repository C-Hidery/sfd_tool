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
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "file_io.h"
#include "logging.h"
#include "spd_protocol.h"

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

// 工具内置的 PhaseCheck.ini 默认值（拷贝自 RD 发行版
// SPD_Research_Tool_R23.0.0001/PhaseCheck.ini：SP09 / PASS VALUE 0 / 7 站点）。
// 用户通过 GUI/CLI 选择的 ini 仅用于 ISE，不改变 PhaseCheck 的内容。
constexpr const char* kBuiltinPhaseCheckIni = R"INI(
[VERSION]
MAGIC NUMBER     = SP09

[STATE FLAG]
PASS VALUE = 0

[STATION]
STATION NUMBER   = 7
STATION 1        =DOWNLOAD
STATION 2        =WRITESN
STATION 3        =CFT
STATION 4        =BBAT
STATION 5        =WCN
STATION 6        =ANTENNA
STATION 7        =IMEI
)INI";

// 解析 PhaseCheck.ini 文本
void parse_phasecheck_ini(std::istream& in, PhaseCheckConfig& cfg)
{
    std::string section;
    std::vector<std::pair<int, std::string>> stations;  // (序号, 名字)
    int station_number = -1;

    std::string line;
    while (std::getline(in, line))
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
    for (const auto& st : stations) parsed.push_back(st.second);
    if (station_number > 0 && static_cast<std::size_t>(station_number) <= parsed.size())
    {
        parsed.resize(static_cast<std::size_t>(station_number));
    }

    cfg.stations = std::move(parsed);
}

PhaseCheckConfig load_phasecheck_config()
{
    PhaseCheckConfig cfg;
    cfg.magic = PhaseCheckMagic::Sp09;
    cfg.pass_value = 0;
    cfg.stations = {"DOWNLOAD", "WRITESN", "CFT", "BBAT", "WCN", "ANTENNA", "IMEI"};

    // 固定使用内置默认值（等价 RD 的 PhaseCheck.ini）。
    std::istringstream builtin(kBuiltinPhaseCheckIni);
    PhaseCheckConfig from_builtin;
    parse_phasecheck_ini(builtin, from_builtin);
    if (!from_builtin.stations.empty())
    {
        cfg = from_builtin;
    }
    return cfg;
}

// 读取 ini 的所有段落，供 ISE 负载使用
bool read_ini_sections(const std::string& path,
                       std::map<std::string, std::map<std::string, std::string>>& out)
{
    std::ifstream f(path);
    if (!f) return false;

    std::string section;
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
        if (eq == std::string::npos || section.empty()) continue;
        out[section][upper_copy(trim_copy(line.substr(0, eq)))] = trim_copy(line.substr(eq + 1));
    }
    return true;
}

bool parse_hex_bytes(const std::string& text, std::vector<uint8_t>& out)
{
    std::string s;
    s.reserve(text.size());
    for (char c : text)
    {
        if (!std::isspace(static_cast<unsigned char>(c))) s.push_back(c);
    }
    if (s.empty() || (s.size() % 2) != 0) return false;

    auto hex_val = [](char c) -> int
    {
        if (c >= '0' && c <= '9') return c - '0';
        const char u = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (u >= 'A' && u <= 'F') return u - 'A' + 10;
        return -1;
    };

    out.clear();
    out.reserve(s.size() / 2);
    for (std::size_t i = 0; i < s.size(); i += 2)
    {
        const int hi = hex_val(s[i]);
        const int lo = hex_val(s[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return true;
}

constexpr std::size_t kMaxIsePayload = 64u * 1024u * 1024u;

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

    // RD 的 Cnst8KBuffer：找到名为 DOWNLOAD 的站点后调整测试位（只处理第一个）
    for (int32_t i = 0; i < pc.station_num; ++i)
    {
        if (!my_stricmp(cfg.stations[static_cast<std::size_t>(i)].c_str(), "DOWNLOAD"))
        {
            if (pc.sign_flag == 0x01)
            {
                pc.item = static_cast<uint16_t>(pc.item & (0x7FFFu & ~(1u << i)));
                pc.test_sign = static_cast<uint16_t>(pc.test_sign & (0xFFFFu & ~(1u << i)));
            }
            else
            {
                pc.item = static_cast<uint16_t>(pc.item | (1u << i));
            }
            break;
        }
    }
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

    // 与 RD 的 Cnst8KBuffer 一致：只处理第一个 DOWNLOAD 站点
    for (int32_t i = 0; i < pc.station_num; ++i)
    {
        if (!my_stricmp(cfg.stations[static_cast<std::size_t>(i)].c_str(), "DOWNLOAD"))
        {
            if (pc.sign_flag == 0x01)
            {
                pc.item &= ~(1u << i);
                pc.test_sign &= ~(1u << i);
            }
            else
            {
                pc.item |= (1u << i);
            }
            break;
        }
    }
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

// 读设备上 PhaseCheck 所在分区的前 8K，用于提取旧 SN 并判断产测格式
// （RD 的 AddReadSN 行为：读同一个 Block，长度按 8K 处理）。
bool read_phasecheck_from_device(spdio_t* io, const std::string& partition,
                                 std::string& sn, PhaseCheckFormat& format)
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
    format = detect_phasecheck_format(data);
    sn = find_phasecheck_sn(data);
    return true;
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

PhaseCheckFormat detect_phasecheck_format(const std::vector<uint8_t>& data)
{
    if (data.size() < 4) return PhaseCheckFormat::Sp09;

    uint32_t magic0 = 0;
    std::memcpy(&magic0, data.data(), sizeof(magic0));
    if (magic0 == kSp05Magic) return PhaseCheckFormat::Sp05;
    if (magic0 == kSp09Magic) return PhaseCheckFormat::Sp09;
    if (magic0 == kSp15Magic) return PhaseCheckFormat::Sp15;

    // 头块没有 magic 时，按 RD 的方式在 8K 内反向找块（SP15 优先，因为块更大）。
    if (data.size() >= kProductionInfoSize)
    {
        auto scan = [&data](std::size_t block, uint32_t magic) -> bool
        {
            if (block == 0 || data.size() < block) return false;
            const std::size_t total = data.size() / block;
            const uint8_t* cur = data.data() + data.size() - block;
            for (std::size_t i = 0; i + 1 < total; ++i)
            {
                uint32_t v = 0;
                std::memcpy(&v, cur, sizeof(v));
                if (v == magic) return true;
                cur -= block;
            }
            return false;
        };
        if (scan(sizeof(Sp15PhaseCheck), kSp15Magic)) return PhaseCheckFormat::Sp15;
        if (scan(sizeof(Sp09PhaseCheck), kSp09Magic)) return PhaseCheckFormat::Sp09;
    }
    return PhaseCheckFormat::Sp09;
}

bool ise_ini_usable(const std::string& ini_path)
{
    if (ini_path.empty()) return false;

    std::map<std::string, std::map<std::string, std::string>> sections;
    if (!read_ini_sections(ini_path, sections)) return false;

    for (const char* name : {"ISE", "ISE_DATA"})
    {
        auto it = sections.find(name);
        if (it == sections.end()) continue;
        const auto& kv = it->second;
        if (kv.count("DATA") || kv.count("FILE") || kv.count("SIZE")) return true;
    }
    return false;
}

bool build_ise_payload(const std::string& ini_path, const std::string& op_id,
                       std::vector<uint8_t>& out, std::string& err)
{
    out.clear();
    err.clear();
    if (ini_path.empty())
    {
        err = "no PhaseCheck.ini selected";
        return false;
    }

    std::map<std::string, std::map<std::string, std::string>> sections;
    if (!read_ini_sections(ini_path, sections))
    {
        err = "ini not readable";
        return false;
    }

    // 优先用与操作同名的段（[ISE] / [ISE_DATA]），否则回退到 [ISE]。
    const std::string want = upper_copy(op_id);
    auto it = sections.find(want);
    if (it == sections.end()) it = sections.find("ISE");
    if (it == sections.end())
    {
        err = "no [ISE] section in ini";
        return false;
    }
    const auto& kv = it->second;

    auto data_it = kv.find("DATA");
    auto file_it = kv.find("FILE");
    auto size_it = kv.find("SIZE");

    if (data_it != kv.end())
    {
        if (!parse_hex_bytes(data_it->second, out) || out.empty())
        {
            err = "invalid Data= (expect even-length hex)";
            return false;
        }
    }
    else if (file_it != kv.end())
    {
        std::filesystem::path p(file_it->second);
        if (p.is_relative())
        {
            p = std::filesystem::path(ini_path).parent_path() / p;
        }
        size_t n = 0;
        uint8_t* raw = loadfile(p.string().c_str(), &n, 0);
        if (!raw || n == 0)
        {
            delete[] raw;
            err = "File= not readable: " + p.string();
            return false;
        }
        out.assign(raw, raw + n);
        delete[] raw;
    }
    else if (size_it != kv.end())
    {
        const unsigned long long n = std::strtoull(size_it->second.c_str(), nullptr, 0);
        if (n == 0)
        {
            err = "Size= is zero";
            return false;
        }
        out.assign(static_cast<std::size_t>(n), 0xFF);  // 擦除态填充
    }
    else
    {
        err = "no Data/File/Size in [ISE] section";
        return false;
    }

    if (out.size() > kMaxIsePayload)
    {
        err = "payload exceeds 64MB";
        out.clear();
        return false;
    }
    return true;
}

std::vector<uint8_t> build_phasecheck_buffer(const std::string& sn1, const std::string& sn2,
                                             PhaseCheckFormat format)
{
    // RD: memset(lpData, 0xFF, 8K) 后把结构体放在缓冲区起始处
    std::vector<uint8_t> buf(kProductionInfoSize, 0xFF);
    PhaseCheckConfig cfg = load_phasecheck_config();

    // 站点表固定用内置默认值；只有结构版本跟随设备现有 phasecheck。
    cfg.magic = (format == PhaseCheckFormat::Sp05) ? PhaseCheckMagic::Sp05
              : (format == PhaseCheckFormat::Sp15) ? PhaseCheckMagic::Sp15
                                                   : PhaseCheckMagic::Sp09;

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

    DEG_LOG(I, "PhaseCheck: built-in default config (SP09 / PASS VALUE 0 / %zu stations)",
            cfg.stations.size());
    return buf;
}

namespace
{
uint64_t file_mtime_filetime(const std::string& path)
{
    if (path.empty()) return 0;
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fad)) return 0;
    return (static_cast<uint64_t>(fad.ftLastWriteTime.dwHighDateTime) << 32) |
           static_cast<uint64_t>(fad.ftLastWriteTime.dwLowDateTime);
#else
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) return 0;
    return apr_filetime_from_unix(static_cast<uint64_t>(st.st_mtime), 0);
#endif
}
}  // namespace

uint64_t apr_filetime_from_unix(uint64_t sec, uint32_t nsec)
{
    // Windows FILETIME：自 1601-01-01 起的 100ns 数；1601 -> 1970 相差 11644473600 秒。
    constexpr uint64_t kEpochDiff = 11644473600ULL;
    return (sec + kEpochDiff) * 10000000ULL + static_cast<uint64_t>(nsec / 100U);
}

std::vector<uint8_t> build_apr_payload(const std::string& pac_path)
{
    const uint64_t filetime = file_mtime_filetime(pac_path);
    std::vector<uint8_t> payload(sizeof(uint64_t), 0);
    for (std::size_t i = 0; i < payload.size(); ++i)
    {
        payload[i] = static_cast<uint8_t>((filetime >> (8 * i)) & 0xFFu);
    }
    return payload;
}

bool write_apr_info(spdio_t* io, const std::string& pac_path)
{
    if (!io || pac_path.empty()) return false;

    const uint64_t filetime = file_mtime_filetime(pac_path);
    const std::vector<uint8_t> payload = build_apr_payload(pac_path);

    std::memcpy(io->temp_buf, payload.data(), payload.size());
    encode_msg_nocpy(io, BSL_CMD_WRITE_APR_INFO, payload.size());

    if (send_and_check(io) != 0)
    {
        DEG_LOG(W, "APR: device rejected BSL_CMD_WRITE_APR_INFO, continuing");
        return false;
    }
    DEG_LOG(OP, "APR: wrote PAC build time (filetime=0x%016llx) from %s",
            static_cast<unsigned long long>(filetime), pac_path.c_str());
    return true;
}

bool run_operation(spdio_t* io, const std::string& op_id, const std::string& type,
                   const std::string& partition, uint64_t block_size, int CMethod,
                   bool write_phasecheck, bool write_ise, const std::string& ini_path)
{
    // ---- PhaseCheck：生成 8K 生产信息并写 miscdata（RD 行为）----
    if (!my_stricmp(op_id.c_str(), "PhaseCheck"))
    {
        // 由 PAC 页的勾选项控制（默认不勾，等同 RD 的 Research 配置 NeedPhaseCheck=0）。
        if (!write_phasecheck)
        {
            DEG_LOG(I, "PhaseCheck: not selected on the PAC page, skipped");
            return true;
        }

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
        PhaseCheckFormat format = PhaseCheckFormat::Sp09;
        if (read_phasecheck_from_device(io, partition, sn, format))
        {
            const char* fmt_name = (format == PhaseCheckFormat::Sp05) ? "SP05"
                                 : (format == PhaseCheckFormat::Sp15) ? "SP15" : "SP09";
            DEG_LOG(OP, "PhaseCheck: device format=%s, SN=\"%s\" (partition %s)",
                    fmt_name, sn.c_str(), partition.c_str());
        }
        else
        {
            DEG_LOG(W, "PhaseCheck: no phasecheck found on device partition %s, "
                       "using SP09 and empty SN", partition.c_str());
        }

        std::vector<uint8_t> buf = build_phasecheck_buffer(sn, std::string(), format);
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
    // 默认“空壳”：RD 的 [CODE2] 只有 DownloadByID，零长度会直接失败，所以 RD 实际跳过。
    // 若用户在 PAC 页打开 ISE 开关并显式选择可用的 ini，则按 ini 的 [ISE]/[ISE_DATA]
    // 段生成内容写入对应分区；否则保持告警、不写。
    if (!my_stricmp(op_id.c_str(), "ISE") || !my_stricmp(op_id.c_str(), "ISE_DATA"))
    {
        if (!write_ise)
        {
            DEG_LOG(W,
                    "%s (Type=%s, Block=%s): ISE generation disabled (shell only); "
                    "enable it on the PAC page and select a PhaseCheck.ini to generate.",
                    op_id.c_str(), type.c_str(), partition.c_str());
            return true;
        }

        if (!ise_ini_usable(ini_path))
        {
            DEG_LOG(W, "%s: no usable ISE ini selected (need [ISE]/[ISE_DATA] with Data/File/Size), skipped",
                    op_id.c_str());
            return true;
        }

        std::vector<uint8_t> payload;
        std::string err;
        if (!build_ise_payload(ini_path, op_id, payload, err))
        {
            DEG_LOG(W, "%s: cannot generate from ini (%s), skipped; keeping shell warning.",
                    op_id.c_str(), err.c_str());
            return true;
        }

        if (partition.empty())
        {
            DEG_LOG(W, "%s: no Block/partition in XML, skipped", op_id.c_str());
            return true;
        }
        get_partition_info(io, partition.c_str(), 1);
        if (!gPartInfo.size)
        {
            DEG_LOG(W, "%s: partition %s does not exist, skipped", op_id.c_str(), partition.c_str());
            return true;
        }

        DEG_LOG(OP, "Generate %s: %zu bytes from %s -> %s (Type=%s)",
                op_id.c_str(), payload.size(), ini_path.c_str(), partition.c_str(), type.c_str());
        if (!write_buffer_to_partition(io, partition, payload, CMethod))
        {
            DEG_LOG(E, "%s: failed to write %s", op_id.c_str(), partition.c_str());
        }
        return true;
    }

    return false;
}
}  // namespace pac_generate
