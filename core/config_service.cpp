#include "config_service.h"
#include "logging.h"

#include "../third_party/nlohmann/json.hpp"
#include "../core/file_io.h"

#include <filesystem>

using nlohmann::json;

namespace sfd {

namespace {

static ConfigStatus make_error(ConfigErrorCode code, const std::string& msg) {
    ConfigStatus s;
    s.success = false;
    s.code = code;
    s.message = msg;
    return s;
}

static ConfigStatus make_ok() {
    ConfigStatus s;
    s.success = true;
    s.code = ConfigErrorCode::Ok;
    return s;
}

static std::string default_config_path() {
    // T1-04 阶段采用简单的当前目录文件，后续可根据平台迁移到 ~/.config/sfd_tool
    return "sfd_tool_config.json";
}

static void to_json(json& j, const ConnectionConfig& c) {
    j = json{
        {"default_wait_seconds", c.default_wait_seconds},
        {"default_sprd4_mode", c.default_sprd4_mode},
        {"default_sprd4_one_step", c.default_sprd4_one_step},
        {"default_use_cve", c.default_use_cve},
        {"default_cve_binary_path", c.default_cve_binary_path},
        {"default_cve_load_address", c.default_cve_load_address},
        {"default_async_receive", c.default_async_receive},
    };
}

static void from_json(const json& j, ConnectionConfig& c) {
    if (j.contains("default_wait_seconds")) j.at("default_wait_seconds").get_to(c.default_wait_seconds);
    if (j.contains("default_sprd4_mode")) j.at("default_sprd4_mode").get_to(c.default_sprd4_mode);
    if (j.contains("default_sprd4_one_step")) j.at("default_sprd4_one_step").get_to(c.default_sprd4_one_step);
    if (j.contains("default_use_cve")) j.at("default_use_cve").get_to(c.default_use_cve);
    if (j.contains("default_cve_binary_path")) j.at("default_cve_binary_path").get_to(c.default_cve_binary_path);
    if (j.contains("default_cve_load_address")) j.at("default_cve_load_address").get_to(c.default_cve_load_address);
    if (j.contains("default_async_receive")) j.at("default_async_receive").get_to(c.default_async_receive);
}

static void to_json(json& j, const AppConfig& c) {
    j = json{
        {"config_path", c.config_path},
        {"last_pac_path", c.last_pac_path},
        {"last_partition_export_dir", c.last_partition_export_dir},
        {"last_fdl1_path", c.last_fdl1_path},
        {"last_fdl2_path", c.last_fdl2_path},
        {"default_verify_after_flash", c.default_verify_after_flash},
        {"default_backup_before_flash", c.default_backup_before_flash},
        {"ui_language", c.ui_language},
        {"log_level", c.log_level},
    };
    json conn;
    to_json(conn, c.connection);
    j["connection"] = conn;
}

static void from_json(const json& j, AppConfig& c) {
    if (j.contains("config_path")) j.at("config_path").get_to(c.config_path);
    if (j.contains("last_pac_path")) j.at("last_pac_path").get_to(c.last_pac_path);
    if (j.contains("last_partition_export_dir")) j.at("last_partition_export_dir").get_to(c.last_partition_export_dir);
    if (j.contains("last_fdl1_path")) j.at("last_fdl1_path").get_to(c.last_fdl1_path);
    if (j.contains("last_fdl2_path")) j.at("last_fdl2_path").get_to(c.last_fdl2_path);
    if (j.contains("default_verify_after_flash")) j.at("default_verify_after_flash").get_to(c.default_verify_after_flash);
    if (j.contains("default_backup_before_flash")) j.at("default_backup_before_flash").get_to(c.default_backup_before_flash);
    if (j.contains("ui_language")) j.at("ui_language").get_to(c.ui_language);
    if (j.contains("log_level")) j.at("log_level").get_to(c.log_level);

    if (j.contains("connection")) {
        from_json(j.at("connection"), c.connection);
    }
}

} // namespace

class DefaultConfigService : public ConfigService {
public:
    DefaultConfigService() = default;
    ~DefaultConfigService() override = default;

    ConfigStatus loadAppConfig(AppConfig& out_config) override {
        const std::string path = default_config_path();
        return loadAppConfigFromFile(path, out_config);
    }

    ConfigStatus loadAppConfigFromFile(const std::string& path,
                                       AppConfig& out_config) override {
        if (path.empty()) {
            return make_error(ConfigErrorCode::InvalidFormat, "empty config path");
        }

        if (!std::filesystem::exists(path)) {
            return make_error(ConfigErrorCode::NotFound, "config file not found");
        }

        FILE* f = xfopen(path.c_str(), "rb");
        if (!f) {
            return make_error(ConfigErrorCode::IoError, "failed to open config file");
        }
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        fseek(f, 0, SEEK_SET);
        std::string buf;
        buf.resize(static_cast<size_t>(len));
        if (len > 0) {
            if (fread(&buf[0], 1, static_cast<size_t>(len), f) != static_cast<size_t>(len)) {
                fclose(f);
                return make_error(ConfigErrorCode::IoError, "failed to read config file");
            }
        }
        fclose(f);

        try {
            json j = json::parse(buf);
            from_json(j, out_config);
            out_config.config_path = path;
            return make_ok();
        } catch (const std::exception& e) {
            return make_error(ConfigErrorCode::ParseError, e.what());
        }
    }

    ConfigStatus saveAppConfig(const AppConfig& config) override {
        std::string path = config.config_path.empty() ? default_config_path() : config.config_path;
        return saveAppConfigToFile(config, path);
    }

    ConfigStatus saveAppConfigToFile(const AppConfig& config,
                                     const std::string& path) override {
        if (path.empty()) {
            return make_error(ConfigErrorCode::InvalidFormat, "empty config path");
        }

        json j;
        to_json(j, config);
        std::string data = j.dump(2);

        FILE* f = xfopen(path.c_str(), "wb");
        if (!f) {
            return make_error(ConfigErrorCode::IoError, "failed to open config file for write");
        }
        if (fwrite(data.data(), 1, data.size(), f) != data.size()) {
            fclose(f);
            return make_error(ConfigErrorCode::IoError, "failed to write config file");
        }
        fclose(f);

        return make_ok();
    }

    void updateLastPacPath(AppConfig& config,
                           const std::string& pac_path) override {
        config.last_pac_path = pac_path;
    }

    void updateLastPartitionExportDir(AppConfig& config,
                                      const std::string& dir) override {
        config.last_partition_export_dir = dir;
    }

    void applyDefaultsToConnectionConfig(AppConfig& config,
                                         ConnectionConfig& inout_connection) override {
        inout_connection = config.connection;
    }

    void applyDefaultsToFlashOptions(AppConfig& config,
                                     FlashPacOptions& inout_flash_options) override {
        if (inout_flash_options.pac_path.empty()) {
            inout_flash_options.pac_path = config.last_pac_path;
        }
        inout_flash_options.verify_after_flash = config.default_verify_after_flash;
        inout_flash_options.backup_before_flash = config.default_backup_before_flash;
    }
};

std::unique_ptr<ConfigService> createConfigService() {
    return std::unique_ptr<ConfigService>(new DefaultConfigService());
}

} // namespace sfd
