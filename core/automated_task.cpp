/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#include "automated_task.h"

#include "app_state.h"
#include "file_io.h"
#include "spd_protocol.h"
#include "../common.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

// GenTosNoAvb.h 定义了 max_size 宏，必须在标准库头文件之后引入。
#include "../GenTosNoAvb.h"
#undef max_size

namespace sfd {
namespace {

std::string trim_copy(const std::string& value)
{
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    const auto begin = std::find_if_not(value.begin(), value.end(), is_space);
    const auto end = std::find_if_not(value.rbegin(), value.rend(), is_space).base();
    if (begin >= end) {
        return {};
    }
    return std::string(begin, end);
}

bool iequals_ascii(const std::string& a, const char* b)
{
    if (!b || a.size() != std::strlen(b)) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

bool parse_u32(const std::string& text, std::uint32_t& out)
{
    if (text.empty() || text[0] == '-') {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const unsigned long value = std::strtoul(text.c_str(), &end, 0);
    if (errno == ERANGE || end == text.c_str() || *end != '\0' ||
        value > 0xffffffffUL) {
        return false;
    }
    out = static_cast<std::uint32_t>(value);
    return true;
}

bool parse_uint64(const std::string& text, std::uint64_t& out)
{
    if (text.empty() || text[0] == '-') {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text.c_str(), &end, 0);
    if (errno == ERANGE || end == text.c_str() || *end != '\0') {
        return false;
    }
    out = static_cast<std::uint64_t>(value);
    return true;
}

bool parse_int(const std::string& text, int& out)
{
    if (text.empty()) {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 0);
    if (errno == ERANGE || end == text.c_str() || *end != '\0' ||
        value < INT_MIN || value > INT_MAX) {
        return false;
    }
    out = static_cast<int>(value);
    return true;
}

// 支持引号，便于路径或文本中包含空格。
bool tokenize(const std::string& line, std::vector<std::string>& tokens, std::string& error)
{
    tokens.clear();
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() &&
               std::isspace(static_cast<unsigned char>(line[i])) != 0) {
            ++i;
        }
        if (i >= line.size()) {
            break;
        }

        std::string token;
        const char quote = (line[i] == '"' || line[i] == '\'') ? line[i] : '\0';
        if (quote != '\0') {
            ++i;
            bool closed = false;
            while (i < line.size()) {
                const char c = line[i];
                if (c == quote) {
                    ++i;
                    closed = true;
                    break;
                }
                if (c == '\\' && i + 1 < line.size() &&
                    (line[i + 1] == quote || line[i + 1] == '\\')) {
                    token.push_back(line[i + 1]);
                    i += 2;
                    continue;
                }
                token.push_back(c);
                ++i;
            }
            if (!closed) {
                error = "unterminated quote";
                return false;
            }
        } else {
            while (i < line.size() &&
                   std::isspace(static_cast<unsigned char>(line[i])) == 0) {
                token.push_back(line[i]);
                ++i;
            }
        }
        tokens.push_back(std::move(token));
    }
    if (tokens.empty()) {
        error = "empty command";
        return false;
    }
    return true;
}

// print 保留命令名之后的原始文本（去掉首尾空白和成对引号）。
std::string command_rest(const std::string& line)
{
    std::size_t i = 0;
    while (i < line.size() &&
           std::isspace(static_cast<unsigned char>(line[i])) == 0) {
        ++i;
    }
    while (i < line.size() &&
           std::isspace(static_cast<unsigned char>(line[i])) != 0) {
        ++i;
    }
    std::string rest = trim_copy(line.substr(i));
    if (rest.size() >= 2 &&
        ((rest.front() == '"' && rest.back() == '"') ||
         (rest.front() == '\'' && rest.back() == '\''))) {
        rest = rest.substr(1, rest.size() - 2);
    }
    return rest;
}

std::string xml_escape(const std::string& value)
{
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        default: out.push_back(c); break;
        }
    }
    return out;
}

class Runner {
public:
    Runner(const AutomatedTaskRequest& request, const AutomatedTaskLogFn& log)
        : request_(request), log_(log) {}

    AutomatedTaskResult run()
    {
        for (const auto& action : request_.actions) {
            const std::string line = trim_copy(action);
            if (line.empty() || line[0] == '#') {
                continue;
            }
            write_log("[cmd] " + line);
            try {
                if (!execute(line)) {
                    error_ = line + " -> " + error_;
                    return {false, error_};
                }
            } catch (const std::exception& e) {
                error_ = line + " -> " + e.what();
                write_log("ERROR: " + error_);
                return {false, error_};
            }
            if (terminal_) {
                break;
            }
        }
        return {true, {}, terminal_};
    }

private:
    const AutomatedTaskRequest& request_;
    const AutomatedTaskLogFn& log_;
    std::string error_;
    std::string exec_file_;
    std::uint32_t exec_addr_ = 0;
    bool terminal_ = false;

    void write_log(const std::string& message)
    {
        if (log_) {
            log_(message);
        }
    }

    bool fail(const std::string& message)
    {
        error_ = message;
        write_log("ERROR: " + message);
        return false;
    }

    unsigned step() const
    {
        return blk_size ? static_cast<unsigned>(blk_size) : DEFAULT_BLK_SIZE;
    }

    bool require_device()
    {
        if (!io || m_bOpened <= 0) {
            return fail("device is not connected");
        }
        return true;
    }

    bool require_fdl2()
    {
        if (!require_device()) {
            return false;
        }
        if (g_app_state.device.device_stage != FDL2 && fdl2_executed <= 0) {
            return fail("command requires FDL2 stage");
        }
        return true;
    }

    std::filesystem::path package_root() const
    {
        return utf8_to_path(request_.package_root);
    }

    std::filesystem::path image_root() const
    {
        if (!request_.image_root.empty()) {
            return utf8_to_path(request_.image_root);
        }
        return package_root() / "images";
    }

    std::filesystem::path resolve_fdl_file(const std::string& file) const
    {
        const std::filesystem::path p = utf8_to_path(file);
        if (request_.fdl_root.empty()) {
            return p;
        }
        if (p.is_absolute()) {
            throw std::runtime_error("fdl file path must be relative to fdl/");
        }

        const std::filesystem::path root = utf8_to_path(request_.fdl_root).lexically_normal();
        const std::filesystem::path out = (root / p).lexically_normal();
        const std::filesystem::path rel = out.lexically_relative(root);
        bool unsafe = rel.empty() || rel.is_absolute();
        for (const auto& part : rel) {
            if (part == "..") {
                unsafe = true;
                break;
            }
        }
        if (unsafe) {
            throw std::runtime_error("fdl file path escapes fdl/: " + file);
        }
        return out;
    }

    std::filesystem::path resolve_image(const std::string& image) const
    {
        const std::filesystem::path p = utf8_to_path(image);
        if (p.is_absolute()) {
            throw std::runtime_error("image path must be relative to images/");
        }

        const std::filesystem::path root = image_root().lexically_normal();
        const std::filesystem::path out = (root / p).lexically_normal();
        const std::filesystem::path rel = out.lexically_relative(root);
        bool unsafe = rel.empty() || rel.is_absolute();
        for (const auto& part : rel) {
            if (part == "..") {
                unsafe = true;
                break;
            }
        }
        if (unsafe) {
            throw std::runtime_error("image path escapes images/: " + image);
        }
        return out;
    }

    bool finish_brom_exec(std::uint32_t addr)
    {
        if (addr == 0x5500 || addr == 0x65000800) {
            highspeed = 1;
            if (!baudrate) {
                baudrate = 921600;
            }
        }

        io->flags &= ~FLAGS_CRC16;
        encode_msg(io, BSL_CMD_CHECK_BAUD, nullptr, 1);
        for (int i = 0; ; ++i) {
            send_msg(io);
            recv_msg(io);
            if (recv_type(io) == BSL_REP_VER) {
                break;
            }
            write_log("Failed to check baud, retry...");
            if (i == 4) {
                return fail("can not execute FDL, please reboot the device");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        if (!std::memcmp(io->raw_buf + 4, "SPRD4", 5)) {
            g_app_state.device.device_mode = SPRD4;
            if (no_fdl_mode) {
                fdl2_executed = -1;
            }
        }

        encode_msg_nocpy(io, BSL_CMD_CONNECT, 0);
        if (send_and_check(io)) {
            return fail("FDL connect failed");
        }
        fdl1_loaded = 1;
        g_app_state.device.device_stage = FDL1;
        write_log("FDL1 executed and connected");
        return true;
    }

    bool exec_fdl1_to_fdl2()
    {
        std::memset(&Da_Info, 0, sizeof(Da_Info));
        encode_msg_nocpy(io, BSL_CMD_EXEC_DATA, 0);
        send_msg(io);
        if (!recv_msg_timeout(io, 15000)) {
            return fail("FDL2 exec timeout");
        }

        int response = recv_type(io);
        if (response == BSL_REP_INCOMPATIBLE_PARTITION) {
            get_Da_Info(io);
        } else if (response != BSL_REP_ACK) {
            return fail(std::string("unexpected FDL2 response: ") +
                        get_bsl_enum_name(response));
        }

        if (Da_Info.bDisableHDLC) {
            encode_msg_nocpy(io, BSL_CMD_DISABLE_TRANSCODE, 0);
            if (!send_and_check(io)) {
                io->flags &= ~FLAGS_TRANSCODE;
            }
        }

        io->ptable = partition_list(io, &io->part_count);
        if (g_app_state.flash.selected_ab < 0) {
            select_ab(io);
        }
        fdl2_executed = 1;
        g_app_state.device.device_stage = FDL2;
        if (exec_addr_ > 0) {
            g_app_state.flash.g_w_force = 1;
        }
        write_log("FDL2 executed");
        return true;
    }

    bool execute(const std::string& line)
    {
        // print 的文本不参与分词，允许任意引号/空格。
        const std::size_t name_end = line.find_first_of(" \t\r\n");
        const std::string first_word =
            line.substr(0, name_end == std::string::npos ? line.size() : name_end);
        if (first_word == "print") {
            write_log(command_rest(line));
            return true;
        }

        if (first_word == "pause") {
            const std::string message = command_rest(line);
            if (message.empty()) {
                return fail("usage: pause <message>");
            }
            if (!request_.on_pause) {
                return fail("pause is not available without a UI callback");
            }
            write_log("pause: waiting for confirmation");
            request_.on_pause(message);
            write_log("pause resumed");
            return true;
        }

        std::vector<std::string> tokens;
        std::string tokenize_error;
        if (!tokenize(line, tokens, tokenize_error)) {
            return fail(tokenize_error);
        }

        const std::string& command = tokens[0];

        if (command == "blk_size") {
            if (tokens.size() != 2) return fail("usage: blk_size <size>");
            errno = 0;
            char* end = nullptr;
            long value = std::strtol(tokens[1].c_str(), &end, 0);
            if (errno == ERANGE || end == tokens[1].c_str() || *end != '\0') {
                return fail("invalid blk_size: " + tokens[1]);
            }
            if (value < 0) value = 0;
            if (value > 0xf800) value = 0xf800;
            blk_size = static_cast<int>(value);
            write_log("blk_size=" + std::to_string(blk_size));
            return true;
        }

        if (command == "write_part") {
            if (tokens.size() != 3) return fail("usage: write_part <name> <image>");
            if (!require_fdl2()) return false;
            const std::filesystem::path image = resolve_image(tokens[2]);
            std::error_code ec;
            if (!std::filesystem::is_regular_file(image, ec) || ec) {
                return fail("image file not found: " + path_to_utf8(image));
            }
            get_partition_info(io, tokens[1].c_str(), 0);
            if (!gPartInfo.size) {
                return fail("partition does not exist: " + tokens[1]);
            }
            const std::string partition_name = gPartInfo.name;
            write_log("writing partition " + partition_name + " <- " + path_to_utf8(image));
            if (!load_partition_unify(io, partition_name.c_str(),
                                      path_to_utf8(image).c_str(),
                                      step(), g_app_state.flash.isCMethod)) {
                return fail("write_part failed: " + partition_name);
            }
            write_log("write_part ok: " + partition_name);
            return true;
        }

        if (command == "read_part") {
            if (tokens.size() != 3) return fail("usage: read_part <name> <path>");
            if (!require_fdl2()) return false;
            get_partition_info(io, tokens[1].c_str(), 0);
            if (!gPartInfo.size) {
                return fail("partition does not exist: " + tokens[1]);
            }
            const std::string partition_name = gPartInfo.name;
            const std::uint64_t partition_size = gPartInfo.size;
            write_log("reading partition " + partition_name + " -> " + tokens[2]);
            if (!dump_partition(io, partition_name.c_str(), 0, partition_size,
                                tokens[2].c_str(), step())) {
                return fail("read_part failed: " + partition_name);
            }
            write_log("read_part ok: " + partition_name);
            return true;
        }

        if (command == "erase_part") {
            if (tokens.size() != 2) return fail("usage: erase_part <name>");
            if (!require_fdl2()) return false;
            get_partition_info(io, tokens[1].c_str(), 0);
            if (!gPartInfo.size) {
                return fail("partition does not exist: " + tokens[1]);
            }
            const std::string partition_name = gPartInfo.name;
            write_log("erasing partition " + partition_name);
            erase_partition(io, partition_name.c_str(), g_app_state.flash.isCMethod);
            write_log("erase_part ok: " + partition_name);
            return true;
        }

        if (command == "repartition") {
            if (tokens.size() != 1) return fail("usage: repartition");
            if (!require_fdl2()) return false;
            if (request_.partitions.empty()) {
                return fail("package.json has no partitions for repartition");
            }

            std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Partitions>\n";
            for (const auto& part : request_.partitions) {
                std::string size_text;
                if (iequals_ascii(part.name, "userdata") && part.size_bytes == 0) {
                    size_text = "0xffffffff";
                } else {
                    const std::uint64_t size_mb = part.size_bytes / (1024ULL * 1024ULL);
                    if (!size_mb) {
                        return fail("invalid partition size for repartition: " + part.name);
                    }
                    size_text = std::to_string(size_mb);
                }
                xml += "  <Partition id=\"" + xml_escape(part.name) + "\" size=\"" +
                       size_text + "\"/>\n";
            }
            xml += "</Partitions>\n";

            TempFile xml_file = write_buffer_to_temp(xml.data(), xml.size(), "sfd_repartition");
            if (xml_file.empty()) {
                return fail("cannot create temporary partition XML");
            }
            write_log("repartition with " + std::to_string(request_.partitions.size()) +
                      " partitions");
            repartition(io, xml_file.c_str());
            write_log("repartition ok");
            return true;
        }

        if (command == "write_parts") {
            if (tokens.size() != 1) return fail("usage: write_parts");
            if (!require_fdl2()) return false;
            const std::filesystem::path dir = image_root();
            std::error_code ec;
            if (!std::filesystem::is_directory(dir, ec) || ec) {
                return fail("images folder not found: " + path_to_utf8(dir));
            }
            write_log("write_parts from " + path_to_utf8(dir));
            load_partitions(io, path_to_utf8(dir).c_str(), step(), 0,
                            g_app_state.flash.isCMethod);
            write_log("write_parts ok");
            return true;
        }

        if (command == "dis_avb_tos") {
            if (tokens.size() != 2) return fail("usage: dis_avb_tos <backup>");
            if (!require_fdl2()) return false;

            get_partition_info(io, "trustos", 1);
            if (!gPartInfo.size) {
                return fail("trustos partition not found");
            }
            const std::string trustos_name = gPartInfo.name;
            const std::uint64_t trustos_size = gPartInfo.size;
            if (!dump_partition(io, trustos_name.c_str(), 0, trustos_size,
                                tokens[1].c_str(), step())) {
                return fail("backup trustos failed: " + tokens[1]);
            }

            std::uint64_t t_size = 0;
            TempFile t_path = dump_partition_to_temp(io, trustos_name.c_str(), 0,
                                                      trustos_size, step(), &t_size);
            if (t_path.empty() || !t_size) {
                return fail("dump trustos to temp failed");
            }

            get_partition_info(io, "sml", 1);
            if (!gPartInfo.size) {
                return fail("sml partition not found");
            }
            std::uint64_t s_size = 0;
            TempFile s_path = dump_partition_to_temp(io, gPartInfo.name, 0,
                                                      gPartInfo.size, step(), &s_size);
            if (s_path.empty() || !s_size) {
                return fail("dump sml to temp failed");
            }

            TempFile save_path(make_temp_file_path("tos_noavb"));
            if (save_path.empty()) {
                return fail("cannot create temporary output file");
            }

            TosPatcher patcher;
            if (patcher.AvbFxxker(s_path.c_str(), t_path.c_str(),
                                  save_path.c_str(), true, true) != 0) {
                return fail("disable AVB in trustos failed");
            }
            if (!load_partition_unify(io, trustos_name.c_str(), save_path.c_str(),
                                      step(), g_app_state.flash.isCMethod)) {
                return fail("write patched trustos failed");
            }
            write_log("dis_avb_tos ok, backup=" + tokens[1]);
            return true;
        }

        if (command == "e_bl") {
            if (tokens.size() != 2) return fail("usage: e_bl <backup>");
            if (!require_fdl2()) return false;
            get_partition_info(io, "miscdata", 1);
            if (!gPartInfo.size) {
                return fail("miscdata partition not found");
            }
            const std::string miscdata_name = gPartInfo.name;
            const std::uint64_t miscdata_size = gPartInfo.size;
            if (!dump_partition(io, miscdata_name.c_str(), 0, miscdata_size,
                                tokens[1].c_str(), step())) {
                return fail("backup miscdata failed: " + tokens[1]);
            }
            write_log("setting bootloader unlock status");
            do_e_bl(io, 4096, g_app_state.flash.isCMethod);
            write_log("e_bl ok, backup=" + tokens[1]);
            return true;
        }

        if (command == "verity") {
            if (tokens.size() != 2) return fail("usage: verity <0|1>");
            int value = 0;
            if (!parse_int(tokens[1], value) || (value != 0 && value != 1)) {
                return fail("verity expects 0 or 1");
            }
            if (!require_fdl2()) return false;
            if (value) {
                dm_enable(io, step(), g_app_state.flash.isCMethod);
            } else {
                dm_disable(io, step(), g_app_state.flash.isCMethod);
            }
            write_log(std::string("verity ") + (value ? "enabled" : "disabled"));
            return true;
        }

        if (command == "slot") {
            if (tokens.size() != 2) return fail("usage: slot <0|1|2>");
            int value = 0;
            if (!parse_int(tokens[1], value) || value < 0 || value > 2) {
                return fail("slot expects 0, 1 or 2");
            }
            g_app_state.flash.selected_ab = value;
            write_log("slot=" + std::to_string(value));
            return true;
        }

        if (command == "rawdata") {
            if (tokens.size() != 2) return fail("usage: rawdata <0|1|2>");
            int value = 0;
            if (!parse_int(tokens[1], value) || value < 0 || value > 2) {
                return fail("rawdata expects 0, 1 or 2");
            }
            Da_Info.bSupportRawData = static_cast<unsigned char>(value);
            write_log("rawdata=" + std::to_string(value));
            return true;
        }

        if (command == "set_active") {
            if (tokens.size() != 2) return fail("usage: set_active <a|b>");
            if (tokens[1] != "a" && tokens[1] != "b") {
                return fail("set_active expects a or b");
            }
            if (!require_fdl2()) return false;
            if (g_app_state.flash.selected_ab < 0) {
                select_ab(io);
            }
            set_active(io, tokens[1].c_str(), g_app_state.flash.isCMethod);
            write_log("set_active " + tokens[1]);
            return true;
        }

        if (command == "send") {
            if (tokens.size() != 3) return fail("usage: send <file> <addr>");
            if (!require_device()) return false;
            std::uint32_t addr = 0;
            if (!parse_u32(tokens[2], addr)) {
                return fail("invalid address: " + tokens[2]);
            }
            const std::filesystem::path file = resolve_fdl_file(tokens[1]);
            std::error_code ec;
            if (!std::filesystem::is_regular_file(file, ec) || ec) {
                return fail("file not found: " + path_to_utf8(file));
            }
            write_log("send " + path_to_utf8(file) + " to 0x" +
                      std::to_string(addr));
            send_file(io, path_to_utf8(file).c_str(), addr, 0, 528, 0, 0);
            write_log("send ok");
            return true;
        }

        if (command == "exec_addr") {
            if (tokens.size() != 3) return fail("usage: exec_addr <file> <addr>");
            std::uint32_t addr = 0;
            if (!parse_u32(tokens[2], addr)) {
                return fail("invalid address: " + tokens[2]);
            }
            const std::filesystem::path file = resolve_fdl_file(tokens[1]);
            std::error_code ec;
            if (!std::filesystem::is_regular_file(file, ec) || ec) {
                return fail("file not found: " + path_to_utf8(file));
            }
            exec_file_ = path_to_utf8(file);
            exec_addr_ = addr;
            write_log("exec_addr file=" + exec_file_ + " addr=0x" +
                      std::to_string(addr));
            return true;
        }

        if (command == "fdl") {
            if (tokens.size() != 3) return fail("usage: fdl <file> <addr>");
            if (!require_device()) return false;
            std::uint32_t addr = 0;
            if (!parse_u32(tokens[2], addr)) {
                return fail("invalid address: " + tokens[2]);
            }
            const std::filesystem::path file = resolve_fdl_file(tokens[1]);
            std::error_code ec;
            if (!std::filesystem::is_regular_file(file, ec) || ec) {
                return fail("FDL file not found: " + path_to_utf8(file));
            }
            const std::string file_utf8 = path_to_utf8(file);

            if (fdl2_executed > 0) {
                write_log("FDL2 is already executed, skipped");
                return true;
            }

            if (fdl1_loaded > 0) {
                write_log("loading FDL2: " + file_utf8);
                send_file(io, file_utf8.c_str(), addr, 0, 528, 0, 0);
                write_log("FDL2 image sent");
                return true;
            }

            write_log("loading FDL1: " + file_utf8);
            send_file(io, file_utf8.c_str(), addr, 0, 528, 0, 0);
            if (exec_addr_ > 0 && !exec_file_.empty()) {
                write_log("sending exec_addr file: " + exec_file_);
                send_file(io, exec_file_.c_str(), exec_addr_, 0, 528, 0, 0);
            } else {
                encode_msg_nocpy(io, BSL_CMD_EXEC_DATA, 0);
                if (send_and_check(io)) {
                    return fail("FDL1 exec failed");
                }
            }
            return finish_brom_exec(addr);
        }

        if (command == "exec") {
            if (tokens.size() > 2) return fail("usage: exec [addr]");
            if (!require_device()) return false;
            if (fdl2_executed > 0) {
                write_log("FDL2 is already executed, skipped");
                return true;
            }
            // FDL1 阶段执行 exec 不需要地址；BROM 阶段才需要 addr。
            if (fdl1_loaded > 0) {
                return exec_fdl1_to_fdl2();
            }

            if (tokens.size() != 2) {
                return fail("exec requires an address when running in BROM");
            }
            std::uint32_t addr = 0;
            if (!parse_u32(tokens[1], addr)) {
                return fail("invalid address: " + tokens[1]);
            }
            encode_msg_nocpy(io, BSL_CMD_EXEC_DATA, 0);
            if (send_and_check(io)) {
                return fail("FDL exec failed");
            }
            return finish_brom_exec(addr);
        }

        if (command == "cptable") {
            if (tokens.size() != 1) return fail("usage: cptable");
            if (!require_fdl2()) return false;
            if (!io->part_count_c && !io->part_count) {
                write_log("reading partition table through compatibility method");
                io->Cptable = partition_list_d(io);
                if (io->Cptable) {
                    g_app_state.flash.isCMethod = 1;
                    write_log("cptable loaded");
                } else if (io->part_count > 0) {
                    write_log("partition table loaded through normal method");
                } else {
                    return fail("cptable failed: no partition table");
                }
            } else {
                write_log("partition table already loaded");
            }
            return true;
        }

        if (command == "wait") {
            if (tokens.size() != 2) return fail("usage: wait <seconds>");
            std::uint64_t seconds = 0;
            if (!parse_uint64(tokens[1], seconds)) {
                return fail("wait expects a non-negative integer number of seconds");
            }
            const auto max_seconds = std::chrono::seconds::max().count();
            if (seconds > static_cast<std::uint64_t>(max_seconds)) {
                return fail("wait duration is too large");
            }
            write_log("waiting " + std::to_string(seconds) + " second(s)");
            std::this_thread::sleep_for(
                std::chrono::seconds(static_cast<std::chrono::seconds::rep>(seconds)));
            write_log("wait finished");
            return true;
        }

        if (command == "timeout") {
            if (tokens.size() != 2) return fail("usage: timeout <ms>");
            int value = 0;
            if (!parse_int(tokens[1], value) || value <= 0) {
                return fail("timeout expects a positive integer");
            }
            if (!io) {
                return fail("device is not connected");
            }
            io->timeout = value;
            write_log("timeout=" + std::to_string(value));
            return true;
        }

        if (command == "reset") {
            if (tokens.size() != 1) return fail("usage: reset");
            if (!fdl1_loaded) return fail("FDL has not been executed");
            if (!require_device()) return false;
            encode_msg_nocpy(io, BSL_CMD_NORMAL_RESET, 0);
            if (send_and_check(io)) return fail("reset failed");
            write_log("reset ok");
            terminal_ = true;
            return true;
        }

        if (command == "poweroff") {
            if (tokens.size() != 1) return fail("usage: poweroff");
            if (!fdl1_loaded) return fail("FDL has not been executed");
            if (!require_device()) return false;
            encode_msg_nocpy(io, BSL_CMD_POWER_OFF, 0);
            if (send_and_check(io)) return fail("poweroff failed");
            write_log("poweroff ok");
            terminal_ = true;
            return true;
        }

        if (command == "reboot-recovery" || command == "reboot-fastboot") {
            if (tokens.size() != 1) return fail("usage: " + command);
            if (!fdl1_loaded) return fail("FDL has not been executed");
            if (!require_device()) return false;

            std::vector<char> miscbuf(0x800, 0);
            std::strcpy(miscbuf.data(), "boot-recovery");
            if (command == "reboot-fastboot") {
                std::strcpy(miscbuf.data() + 0x40, "recovery\n--fastboot\n");
            }
            w_mem_to_part_offset(io, "misc", 0, reinterpret_cast<uint8_t*>(miscbuf.data()),
                                 0x800, 0x1000, g_app_state.flash.isCMethod);
            encode_msg_nocpy(io, BSL_CMD_NORMAL_RESET, 0);
            if (send_and_check(io)) return fail(command + " failed");
            write_log(command + " ok");
            terminal_ = true;
            return true;
        }

        return fail("unknown command: " + command);
    }
};

} // namespace

AutomatedTaskResult run_automated_task(const AutomatedTaskRequest& request,
                                       const AutomatedTaskLogFn& log)
{
    try {
        Runner runner(request, log);
        return runner.run();
    } catch (const std::exception& e) {
        return {false, e.what(), false};
    } catch (...) {
        return {false, "unknown automated task error", false};
    }
}

} // namespace sfd
