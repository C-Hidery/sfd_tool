#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

struct spdio_t;   // 来自 core/usb_transport.h
struct AppState;  // 来自 core/app_state.h

namespace sfd {

// 设备阶段（对应原有 device_stage）
enum class DeviceStage {
    Unknown   = -1,
    BootRom   = 0,
    Fdl1      = 1,
    Fdl2      = 2,
    NormalOs  = 3,
    Reconnect = 99,
};

// 设备模式（后续可根据 mode_str 进一步细分）
enum class DeviceMode {
    Unknown   = -1,
    Download  = 0,
    Diag      = 1,
    Fastboot  = 2,
};

// 存储类型（来自 FDL 探测 / READ_FLASH_INFO 等）
enum class FlashStorageType {
    Unknown = 0,
    Nand,
    Emmc,
    Ufs,
};

// 设备级错误码（服务层视角）
enum class DeviceErrorCode {
    Ok = 0,
    PermissionDenied,
    NoDeviceFound,
    OpenFailed,
    ProtocolError,
    Timeout,
    Unsupported,
    InternalError,
};

struct DeviceStatus {
    bool success = false;
    DeviceErrorCode code = DeviceErrorCode::Ok;
    std::string message;
};

// 设备基础信息，来自协议层查询
struct DeviceInfo {
    std::string chipset;          // 如 "SC9863A"
    std::string product_name;
    std::string firmware_version;
    std::string build_info;

    std::string flash_type_text;  // READ_FLASH_TYPE 返回的文本
    std::string flash_uid;        // READ_FLASH_UID 返回的 UID 文本

    std::uint64_t nand_total_size = 0;
    std::uint32_t block_size      = 0;

    DeviceStage       stage      = DeviceStage::Unknown;
    DeviceMode        mode       = DeviceMode::Unknown;
    FlashStorageType  flash_type = FlashStorageType::Unknown;
};

// 连接选项，对应 page_connect 中的各种开关
struct ConnectionOptions {
    double wait_seconds = 30.0;  // 等待设备时间

    bool sprd4_mode      = false;
    bool sprd4_one_step  = false;

    bool use_cve             = false;
    std::string cve_binary_path;
    std::string cve_load_address;

    bool kick_mode = false;   // isKickMode
    int  boot_mode = -1;      // bootmode
    int  at        = 0;       // at 参数

    bool async_receive = false;  // async 模式
};

// 持久化的连接配置（默认连接参数），用于 ConfigService
struct ConnectionConfig {
    double default_wait_seconds = 30.0;

    bool default_sprd4_mode     = false;
    bool default_sprd4_one_step = false;

    bool default_use_cve             = false;
    std::string default_cve_binary_path;
    std::string default_cve_load_address;

    bool default_async_receive = true;
};

// 设备服务：负责探测/连接/断开/模式切换等用例
class DeviceService {
public:
    virtual ~DeviceService() = default;

    // 绑定底层上下文
    virtual void setContext(spdio_t* io, AppState* app_state) = 0;

    // ===== 连接生命周期 =====

    // 按选项主动“踢设备并连接”
    virtual DeviceStatus connect(const ConnectionOptions& options) = 0;

    // 在已知设备基础上重新连接（例如 --reconnect）
    virtual DeviceStatus reconnect(const ConnectionOptions& options) = 0;

    // 主动断开连接
    virtual DeviceStatus disconnect() = 0;

    // 当前是否认为“已连接”
    virtual bool isConnected() const = 0;

    // ===== 设备信息与状态 =====

    // 探测设备信息（可能触发一次协议交互）
    virtual DeviceStatus probeDevice(DeviceInfo& out_info) = 0;

    // 获取最近一次探测结果（无 I/O）
    virtual DeviceStatus getCachedDeviceInfo(DeviceInfo& out_info) const = 0;

    virtual DeviceStage getCurrentStage() const = 0;
    virtual DeviceMode  getCurrentMode() const = 0;

    // ===== 模式切换与重启 =====

    virtual DeviceStatus rebootToNormalOs() = 0;
    virtual DeviceStatus rebootToDownloadMode() = 0;

    // 部分平台支持“保持充电”等扩展选项
    virtual DeviceStatus setKeepCharge(bool enabled) = 0;

    // ===== 事件/监听控制（USB 热插拔等） =====

    virtual DeviceStatus startEventLoop() = 0;  // 启动 libusb hotplug / Win32 消息循环包装
    virtual DeviceStatus stopEventLoop()  = 0;

    virtual bool isEventLoopRunning() const = 0;
};

std::unique_ptr<DeviceService> createDeviceService();

} // namespace sfd
