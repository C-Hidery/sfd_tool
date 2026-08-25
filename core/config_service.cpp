/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#include "config_service.h"
#include "logging.h"

#include <nlohmann/json.hpp>
#include "../core/file_io.h"
#include "../common.h"

#include <filesystem>
#include <cstdlib>

using nlohmann::json;

namespace sfd
{
    namespace
    {
        ConfigStatus make_error(ConfigErrorCode code, const std::string& msg)
        {
            ConfigStatus s;
            s.success = false;
            s.code = code;
            s.message = msg;
            return s;
        }

        ConfigStatus make_ok()
        {
            ConfigStatus s;
            s.success = true;
            s.code = ConfigErrorCode::Ok;
            return s;
        }

        std::string legacy_config_path()
        {
            // 旧版本路径：当前工作目录下的配置文件，保留用于兼容与迁移
            return "sfd_tool_config.json";
        }

        std::string per_user_config_dir()
        {
#if defined(__linux__)
            const char* xdg = std::getenv("XDG_CONFIG_HOME");
            if (xdg && *xdg)
            {
                return std::string(xdg) + "/sfd_tool";
            }
            const char* home = std::getenv("HOME");
            if (home && *home)
            {
                return std::string(home) + "/.config/sfd_tool";
            }
            return {};
#elif defined(__APPLE__)
            const char* home = std::getenv("HOME");
            if (home && *home)
            {
                return std::string(home) + "/Library/Application Support/sfd_tool";
            }
            return std::string();
#elif defined(_WIN32)
            wchar_t* wappdata = _wgetenv(L"APPDATA");
            if (wappdata) {
                // 将宽字符串转为 UTF-8
                int len = WideCharToMultiByte(CP_UTF8, 0, wappdata, -1, nullptr, 0, nullptr, nullptr);
                if (len > 0) {
                    std::string utf8_dir(len, '\0');
                    WideCharToMultiByte(CP_UTF8, 0, wappdata, -1, utf8_dir.data(), len, nullptr, nullptr);
                    utf8_dir.pop_back();
                    return utf8_dir + "\\sfd_tool";
                }
            }
#else
            // 其他平台暂时不指定 per-user 目录，统一退回旧路径
            return std::string();
#endif
        }

        std::string per_user_config_path()
        {
            std::string dir = per_user_config_dir();
            if (dir.empty())
            {
                return {};
            }
#if defined(_WIN32)
            return dir + "\\sfd_tool_config.json";
#else
            return dir + "/sfd_tool_config.json";
#endif
        }

        bool ensure_parent_directory(const std::string& path)
        {
            std::error_code ec;
#ifdef _WIN32
            std::filesystem::path p;
            std::wstring wpath = utf8_to_utf16(path);
            if (wpath.empty()) p = std::filesystem::path(path);
            else p = std::filesystem::path(wpath);
#else
            std::filesystem::path p(path);
#endif
            auto parent = p.parent_path();
            if (parent.empty())
            {
                // 相对当前目录的文件，无需创建目录
                return true;
            }
            if (std::filesystem::exists(parent, ec))
            {
                return !ec;
            }
            std::filesystem::create_directories(parent, ec);
            return !ec;
        }

        std::string default_config_path()
        {
            // 默认优先使用 per-user 配置路径；若不可用则退回旧的当前目录文件
            std::string per_user = per_user_config_path();
            if (!per_user.empty())
            {
                return per_user;
            }
            return legacy_config_path();
        }

        void to_json(json& j, const AppConfig& c)
        {
            j = json{
                {"last_pac_path", c.last_pac_path},
                {"last_fdl1_path", c.last_fdl1_path},
                {"last_fdl2_path", c.last_fdl2_path},
                {"last_fdl1_addr", c.last_fdl1_addr},
                {"last_fdl2_addr", c.last_fdl2_addr},
                {"last_exec_addr_file", c.last_exec_addr_file},
                {"last_exec_addr", c.last_exec_addr},
                {"last_use_exec_addr", c.last_use_exec_addr},
                {"last_use_exec_addr_v2", c.last_use_exec_addr_v2},
                {"ui_language", c.ui_language},
            };
        }

        void from_json(const json& j, AppConfig& c)
        {
            if (j.contains("last_pac_path")) j.at("last_pac_path").get_to(c.last_pac_path);
            if (j.contains("last_fdl1_path")) j.at("last_fdl1_path").get_to(c.last_fdl1_path);
            if (j.contains("last_fdl2_path")) j.at("last_fdl2_path").get_to(c.last_fdl2_path);
            if (j.contains("last_fdl1_addr")) j.at("last_fdl1_addr").get_to(c.last_fdl1_addr);
            if (j.contains("last_fdl2_addr")) j.at("last_fdl2_addr").get_to(c.last_fdl2_addr);
            if (j.contains("last_exec_addr_file")) j.at("last_exec_addr_file").get_to(c.last_exec_addr_file);
            if (j.contains("last_exec_addr")) j.at("last_exec_addr").get_to(c.last_exec_addr);
            if (j.contains("last_use_exec_addr")) j.at("last_use_exec_addr").get_to(c.last_use_exec_addr);
            if (j.contains("last_use_exec_addr_v2")) j.at("last_use_exec_addr_v2").get_to(c.last_use_exec_addr_v2);
            if (j.contains("ui_language")) j.at("ui_language").get_to(c.ui_language);
        }
    } // namespace

    void initDefaultAppConfig(AppConfig& cfg)
    {
        cfg = AppConfig{};
        cfg.ui_language = "auto";
    }

    bool loadAppConfigOrDefault(AppConfig& out_config)
    {
        auto svc = createConfigService();
        if (!svc)
        {
            initDefaultAppConfig(out_config);
            return false;
        }
        ConfigStatus status = svc->loadAppConfig(out_config);
        if (status.success)
        {
            return true;
        }
        initDefaultAppConfig(out_config);
        return false;
    }

    class DefaultConfigService : public ConfigService
    {
    public:
        DefaultConfigService() = default;
        ~DefaultConfigService() override = default;

        ConfigStatus loadAppConfig(AppConfig& out_config) override
        {
            const std::string per_user = per_user_config_path();
            const std::string legacy = legacy_config_path();

            // 1. 优先从 per-user 配置路径加载
#ifndef _WIN32
            if (!per_user.empty() && std::filesystem::exists(per_user))
#else
            if (!per_user.empty() && std::filesystem::exists(utf8_to_utf16(per_user)))
#endif
            {
                ConfigStatus st = loadAppConfigFromFile(per_user, out_config);
                return st;
            }

            // 2. per-user 不存在但旧路径存在：从旧路径加载并尝试迁移
#ifndef _WIN32
            if (std::filesystem::exists(legacy))
#else
            if (std::filesystem::exists(utf8_to_utf16(legacy)) || std::filesystem::exists(legacy))
#endif
            {
                ConfigStatus st = loadAppConfigFromFile(legacy, out_config);
                if (!st.success)
                {
                    return st;
                }

                // 填写期望的 per-user 路径
                if (!per_user.empty())
                {
                    // 尝试创建目录并写入 per-user 配置文件；失败时仅记录日志，继续使用旧路径
                    if (ensure_parent_directory(per_user))
                    {
                        ConfigStatus migrate_status = saveAppConfigToFile(out_config, per_user);
                        if (migrate_status.success)
                        {
                            return st; // 已迁移，后续保存会使用 per-user 路径
                        }
                    }
                }

                return st;
            }

            // 3. 两个路径都不存在
            return make_error(ConfigErrorCode::NotFound, "config file not found in per-user or legacy path");
        }

        ConfigStatus loadAppConfigFromFile(const std::string& path,
                                           AppConfig& out_config) override
        {
            if (path.empty())
            {
                return make_error(ConfigErrorCode::InvalidFormat, "empty config path");
            }
#ifndef _WIN32
            if (!std::filesystem::exists(path))
#else
            if (!std::filesystem::exists(utf8_to_utf16(path)) || !std::filesystem::exists(path))
#endif
            {
                return make_error(ConfigErrorCode::NotFound, "config file not found");
            }

            EnhancedFile f = oxfopen_enhanced(path.c_str(), "rb");
            if (!f)
            {
                return make_error(ConfigErrorCode::IoError, "failed to open config file");
            }
            f.seek(0, SEEK_END);
            long len = f.tell();
            f.seek(0, SEEK_SET);
            std::string buf;
            buf.resize(static_cast<size_t>(len));
            if (len > 0)
            {
                if (f.read(&buf[0], 1, static_cast<size_t>(len)) != static_cast<size_t>(len))
                {
                    f.close();
                    return make_error(ConfigErrorCode::IoError, "failed to read config file");
                }
            }
            f.close();

            try
            {
                json j = json::parse(buf);
                from_json(j, out_config);
                return make_ok();
            }
            catch (const std::exception& e)
            {
                return make_error(ConfigErrorCode::ParseError, e.what());
            }
        }

        ConfigStatus saveAppConfig(const AppConfig& config) override
        {
            std::string path;
            std::string per_user = per_user_config_path();
            if (!per_user.empty())
            {
                path = per_user;
            }
            else
            {
                path = legacy_config_path();
            }
            return saveAppConfigToFile(config, path);
        }

        ConfigStatus saveAppConfigToFile(const AppConfig& config,
                                         const std::string& path) override
        {
            if (path.empty())
            {
                return make_error(ConfigErrorCode::InvalidFormat, "empty config path");
            }

            if (!ensure_parent_directory(path))
            {
                return make_error(ConfigErrorCode::IoError, "failed to create config directory");
            }

            json j;
            to_json(j, config);
            std::string data = j.dump(2);

            EnhancedFile f = oxfopen_enhanced(path.c_str(), "wb");
            if (!f)
            {
                return make_error(ConfigErrorCode::IoError, "failed to open config file for write");
            }
            if (f.write(data.data(), 1, data.size()) != data.size())
            {
                f.close();
                return make_error(ConfigErrorCode::IoError, "failed to write config file");
            }
            f.close();

            return make_ok();
        }
    };

    std::unique_ptr<ConfigService> createConfigService()
    {
        return std::unique_ptr<ConfigService>(new DefaultConfigService());
    }
} // namespace sfd
