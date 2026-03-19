#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <functional>
struct spdio_t;   // 前向声明，避免头文件循环依赖
struct AppState;  // 来自 core/app_state.h

namespace sfd {

// 暂时代替统一 Result/ErrorCode，后续可与 T2-02 对齐
enum class FlashErrorCode {
    Ok = 0,
    DeviceNotConnected,
    InvalidPacFile,
    PartitionTableNotLoaded,
    PartitionNotFound,
    IoError,
    Cancelled,
    InternalError,
    UnsupportedOperation,
};

struct FlashStatus {
    bool success = false;
    FlashErrorCode code = FlashErrorCode::Ok;
    std::string message;  // 用于日志和 UI 展示
};

// PAC 文件的基础元信息
struct PacMetadata {
    std::string path;
    std::string product_name;
    std::string version;
    std::uint64_t file_size = 0;
};

// PAC 内单个分区条目（供 UI 选择用）
struct PacPartitionEntry {
    std::string name;          // 分区名
    std::string file_name;     // PAC 中对应镜像名
    std::uint64_t image_size = 0;
    bool enabled_by_default = true;
    bool critical = false;     // 如 spl/bootloader 等关键分区
};

// 当前设备上的分区视图（来自 GPT/Cptable 等）
struct DevicePartitionInfo {
    std::string name;
    std::uint64_t start = 0;   // 起始位置（具体语义由实现决定）
    std::uint64_t size = 0;    // 字节数
    bool readable = true;
    bool writable = true;
};

// A/B slot 选择策略，对应 g_app_state.flash.selected_ab
enum class SlotSelection {
    Auto = 0,
    SlotA = 1,
    SlotB = 2,
};

// 一次 PAC 刷机的高层选项
struct FlashPacOptions {
    std::string pac_path;

    // 为空表示按 PAC 默认刷写全部分区
    std::vector<std::string> selected_partitions;

    SlotSelection slot_selection = SlotSelection::Auto;

    bool verify_after_flash = true;
    bool backup_before_flash = false;

    // 兼容模式 / CMethod 等开关
    bool compatibility_mode = false;
};

// PAC 刷机阶段枚举，用于表达清晰的高层阶段语义
enum class FlashPacStage {
    ValidateContext,
    ValidatePac,
    ExtractPac,
    ConfigureState,
    ExecuteFlash,
    Done,
};

// PAC 刷机阶段回调，用于 UI 展示进度/阶段信息
using FlashPacStageCallback = std::function<void(FlashPacStage)>;

// 单分区读写/备份选项
struct PartitionIoOptions {
    std::string partition_name;
    std::string file_path;

    SlotSelection slot_selection = SlotSelection::Auto;

    std::uint32_t block_size = 0;  // 0 表示使用默认块大小
    bool force = false;            // 对应“强制写入”等模式
    bool verify = false;           // 写入后校验
};

// GUI/调用层统一的块大小模式
enum class BlockSizeMode {
    AUTO_DEFAULT,      // 使用 legacy/AUTO 模式（握手默认步长和速率）
    MANUAL_BLOCK_SIZE, // 固定块大小，由调用层指定
};

struct BlockSizeConfig {
    BlockSizeMode mode = BlockSizeMode::AUTO_DEFAULT;
    std::uint32_t manual_block_size = 0; // MANUAL_BLOCK_SIZE 模式下有效
    bool use_compat_chain = false;       // 预留：是否强制走兼容旧链路
};

inline unsigned ResolveBlockStep(const BlockSizeConfig& cfg,
                                 unsigned default_step) {
    switch (cfg.mode) {
    case BlockSizeMode::AUTO_DEFAULT:
        // AUTO_DEFAULT：优先使用 cfg.manual_block_size 中已经解析好的“默认步长”
        // （例如握手阶段得到的 0xF800），仅当其为 0 时退回协议层默认步长。
        return cfg.manual_block_size ? cfg.manual_block_size : default_step;
    case BlockSizeMode::MANUAL_BLOCK_SIZE:
        // MANUAL_BLOCK_SIZE：固定块大小模式，同样以 cfg.manual_block_size 为准，
        // 在未设置时退回 default_step。
        return cfg.manual_block_size ? cfg.manual_block_size : default_step;
    }
    return default_step;
}

// 分区读取统一抽象
struct PartitionReadInfo {
    std::string   name;       // 分区名
    std::uint64_t start = 0;  // 起始位置（如可用）
    std::uint64_t size = 0;   // 读取总字节数
};

struct PartitionReadOptions {
    std::string     output_path;   // 输出文件完整路径
    BlockSizeConfig block_cfg;     // 块大小与链路配置
};

struct PartitionReadCallbacks {
    // 开始读取某个分区时回调
    std::function<void(const PartitionReadInfo&)> on_start;

    // 读取进度回调：bytes_read 为当前已完成字节数，speed_mb_s 为当前估算速度
    std::function<void(const PartitionReadInfo&, std::uint64_t /*bytes_read*/, double /*speed_mb_s*/)> on_progress;

    // 完成当前分区读取后的回调，携带结果状态
    std::function<void(const PartitionReadInfo&, const FlashStatus&)> on_finished;
};

class PartitionReadService {
public:
    virtual ~PartitionReadService() = default;

    // 读取单个分区到指定文件
    virtual FlashStatus readOne(const PartitionReadInfo& info,
                                const PartitionReadOptions& options,
                                const PartitionReadCallbacks* callbacks = nullptr) = 0;
};

// 用例级 Flash 服务接口：PAC 刷机、分区读写、备份等
class FlashService {
public:
    virtual ~FlashService() = default;

    // 绑定底层上下文（当前设备 I/O + 运行时状态）
    virtual void setContext(spdio_t* io, AppState* app_state) = 0;

    // ===== PAC 相关 =====

    // 只解析 PAC 元信息与分区列表，不做实际刷写
    virtual FlashStatus loadPacMetadata(const std::string& pac_path,
                                        PacMetadata& out_metadata,
                                        std::vector<PacPartitionEntry>& out_entries) = 0;

    // 按选项执行一次 PAC 刷机流程（高层一站式封装）
    virtual FlashStatus flashPac(const FlashPacOptions& options,
                                 FlashPacStageCallback on_stage = {}) = 0;

    // ===== 设备分区视图 =====

    // 从设备刷新当前分区表视图
    virtual FlashStatus refreshDevicePartitions(std::vector<DevicePartitionInfo>& out_partitions) = 0;

    // 获取最近一次刷新结果（若无缓存可返回错误）
    virtual FlashStatus getCachedDevicePartitions(std::vector<DevicePartitionInfo>& out_partitions) const = 0;

    // ===== 单分区读写与备份 =====

    // 暴露统一的分区读取服务
    virtual PartitionReadService& partitionReader() = 0;

    // 从设备读取单个分区到文件
    virtual FlashStatus readPartitionToFile(const PartitionIoOptions& options) = 0;

    // 将文件写入单个分区
    virtual FlashStatus writePartitionFromFile(const PartitionIoOptions& options) = 0;

    // 备份若干分区（names 为空表示全部）到目录
    virtual FlashStatus backupPartitions(const std::vector<std::string>& partition_names,
                                         const std::string& output_directory,
                                         SlotSelection slot_selection = SlotSelection::Auto,
                                         std::uint32_t block_size = 0) = 0;

    // 对指定分区做简单校验
    virtual FlashStatus verifyPartition(const std::string& partition_name,
                                        SlotSelection slot_selection = SlotSelection::Auto) = 0;

    // 擦除指定分区
    virtual FlashStatus erasePartition(const std::string& partition_name) = 0;

    // 导出当前分区表到 XML 文件（用于 Advanced/Partition 页）
    virtual FlashStatus exportPartitionTableToXml(const std::string& output_path) = 0;

    // 查询 PAC 刷机时间（映射现有 read_pactime(io) 行为）
    virtual FlashStatus queryPacFlashTime(std::uint64_t& out_seconds) = 0;
};

// 默认 FlashService 工厂方法，供 UI 层通过接口获取实现
std::unique_ptr<FlashService> createFlashService();

} // namespace sfd
