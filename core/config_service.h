/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#pragma once

#include <string>
#include <memory>
namespace sfd {

// 这里直接使用完整类型定义，避免前向声明不完整导致的问题

enum class ConfigErrorCode {
    Ok = 0,
    IoError,
    ParseError,
    InvalidFormat,
    NotFound,
    InternalError,
};

struct ConfigStatus {
    bool success = false;
    ConfigErrorCode code = ConfigErrorCode::Ok;
    std::string message;
};

// 应用级配置：承载 UI 与 Service 共享的领域配置
struct AppConfig {
    // 配置文件路径（运行时信息，记录当前加载/保存位置）
    std::string config_path;

    // 最近使用路径
    std::string last_pac_path;
    std::string last_fdl1_path;
    std::string last_fdl2_path;

    // 最近FDL地址
    std::string last_fdl1_addr;
    std::string last_fdl2_addr;

    // 最近EXEC_ADDR文件和地址
    std::string last_exec_addr_file;
    std::string last_exec_addr;

    // 最近是否使用了EXEC_ADDR
    std::string last_use_exec_addr;
    std::string last_use_exec_addr_v2;

    // UI 相关设置
    std::string ui_language;  // 例如 "zh_CN"、"en_US"
};

// 配置服务：负责 JSON <-> AppConfig/ConnectionConfig 的映射与持久化
class ConfigService {
public:
    virtual ~ConfigService() = default;

    // ===== 加载/保存整体配置 =====

    // 从默认位置加载配置（例如 ~/.config/sfd_tool/config.json）
    virtual ConfigStatus loadAppConfig(AppConfig& out_config) = 0;

    // 从指定文件加载配置
    virtual ConfigStatus loadAppConfigFromFile(const std::string& path,
                                               AppConfig& out_config) = 0;

    // 保存配置到默认位置（可使用 config.config_path 或约定路径）
    virtual ConfigStatus saveAppConfig(const AppConfig& config) = 0;

    // 保存配置到指定文件
    virtual ConfigStatus saveAppConfigToFile(const AppConfig& config,
                                             const std::string& path) = 0;
};

// 默认配置初始化与加载辅助函数
void initDefaultAppConfig(AppConfig& cfg);
bool loadAppConfigOrDefault(AppConfig& out_config);

// 默认 ConfigService 工厂
std::unique_ptr<ConfigService> createConfigService();

} // namespace sfd
