#pragma once

#include <string>
#include <memory>
#include "device_service.h"  // 提供 ConnectionConfig 定义
#include "flash_service.h"   // 提供 FlashPacOptions 定义

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
    std::string last_partition_export_dir;
    std::string last_fdl1_path;
    std::string last_fdl2_path;

    // 连接默认设置
    ConnectionConfig connection;

    // 默认刷机选项
    bool default_verify_after_flash = true;
    bool default_backup_before_flash = false;

    // UI 相关设置
    std::string ui_language;  // 例如 "zh_CN"、"en_US"
    int log_level = 0;        // 日志等级，具体含义由 logging 模块约定
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

    // ===== 运行时辅助接口 =====

    // 更新“最近使用的 PAC 路径”
    virtual void updateLastPacPath(AppConfig& config,
                                   const std::string& pac_path) = 0;

    // 更新“最近的分区导出目录”
    virtual void updateLastPartitionExportDir(AppConfig& config,
                                              const std::string& dir) = 0;

    // 根据当前配置填充连接默认值
    virtual void applyDefaultsToConnectionConfig(AppConfig& config,
                                                 ConnectionConfig& inout_connection) = 0;

    // 根据当前配置填充刷机默认值
    virtual void applyDefaultsToFlashOptions(AppConfig& config,
                                             FlashPacOptions& inout_flash_options) = 0;
};

// 默认配置初始化与加载辅助函数
void initDefaultAppConfig(AppConfig& cfg);
bool loadAppConfigOrDefault(AppConfig& out_config);

// 默认 ConfigService 工厂
std::unique_ptr<ConfigService> createConfigService();

} // namespace sfd
