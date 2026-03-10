#include "device_service.h"
#include "app_state.h"
#include "logging.h"
#include "usb_transport.h"
#include "result.h"
#include "spd_protocol.h"
#include "../common.h"

#include <memory>

namespace sfd {

namespace {

static DeviceStatus make_error(DeviceErrorCode code, const std::string& msg) {
    DeviceStatus s;
    s.success = false;
    s.code = code;
    s.message = msg;
    return s;
}

static DeviceStatus make_ok(const std::string& msg = std::string()) {
    DeviceStatus s;
    s.success = true;
    s.code = DeviceErrorCode::Ok;
    s.message = msg;
    return s;
}

static DeviceStage map_stage_int(int stage) {
    switch (stage) {
    case 0:  return DeviceStage::BootRom;
    case 1:  return DeviceStage::Fdl1;
    case 2:  return DeviceStage::Fdl2;
    case 3:  return DeviceStage::NormalOs;
    case 99: return DeviceStage::Reconnect;
    default: return DeviceStage::Unknown;
    }
}

static DeviceMode deduce_mode_from_globals() {
    // 现有代码通过 mode_str 文本判断 SPRD3 / SPRD4，这里先用简单占位
    // 后续可以根据 mode_str 进一步细分 Download/Diag/Fastboot
    return DeviceMode::Download;
}

// 各种 Result 封装的前向声明
static Result<FlashStorageType> try_read_flash_info(spdio_t* io);
static Result<std::string>      read_chip_type(spdio_t* io);

// 基于 AppState / Da_Info 聚合 DeviceInfo 的基础信息
static Result<DeviceInfo> build_device_info(AppState* app) {
    DeviceInfo info{};

    info.chipset.clear();
    info.product_name.clear();
    info.firmware_version.clear();
    info.build_info.clear();

    info.nand_total_size = 0;
    info.block_size      = 0;

    info.stage = map_stage_int(app->device.device_stage);
    info.mode  = deduce_mode_from_globals();

    // 先根据 Da_Info 里已有的探测结果粗略推导存储类型
    info.flash_type = FlashStorageType::Unknown;
    switch (Da_Info.dwStorageType) {
    case 0x101: // NAND
        info.flash_type = FlashStorageType::Nand;
        break;
    case 0x102: // eMMC
        info.flash_type = FlashStorageType::Emmc;
        break;
    case 0x103: // UFS
        info.flash_type = FlashStorageType::Ufs;
        break;
    default:
        break;
    }

    return Result<DeviceInfo>::ok(info);
}

// READ_FLASH_INFO 试点封装：
// - 发送 BSL_CMD_READ_FLASH_INFO
// - 校验应答类型
// - 成功时返回一个粗粒度的 FlashStorageType（当前仅用于区分 NAND）
static Result<FlashStorageType> try_read_flash_info(spdio_t* io) {
    if (!io) {
        return Result<FlashStorageType>::error(ErrorCode::DeviceNotConnected, "io is null");
    }

    encode_msg_nocpy(io, BSL_CMD_READ_FLASH_INFO, 0);
    send_msg(io);

    int ret = recv_msg(io);
    if (!ret) {
        return Result<FlashStorageType>::error(ErrorCode::Timeout, "recv flash info timeout");
    }

    unsigned type = recv_type(io);
    if (type != BSL_REP_READ_FLASH_INFO) {
        const char* name = get_bsl_enum_name(type);
        char buf[128];
        snprintf(buf,
                 sizeof(buf),
                 "unexpected response (%s : 0x%04x)",
                 name ? name : "UNKNOWN",
                 type);
        return Result<FlashStorageType>::error(ErrorCode::ProtocolError, buf);
    }

    // 旧代码在收到 READ_FLASH_INFO 时，将存储视为 NAND：
    //   Da_Info.dwStorageType = 0x101;
    // 这里沿用这一最小假设用于 DeviceInfo 填充，但不改变其它逻辑。
    FlashStorageType storage = FlashStorageType::Nand;
    return Result<FlashStorageType>::ok(storage);
}

// READ_CHIP_TYPE 封装：返回芯片类型字符串（如 "SC9863A"）
static Result<std::string> read_chip_type(spdio_t* io) {
    if (!io) {
        return Result<std::string>::error(ErrorCode::DeviceNotConnected, "io is null");
    }

    encode_msg_nocpy(io, BSL_CMD_READ_CHIP_TYPE, 0);
    send_msg(io);

    int ret = recv_msg(io);
    if (!ret) {
        return Result<std::string>::error(ErrorCode::Timeout, "recv chip type timeout");
    }

    unsigned type = recv_type(io);
    if (type != BSL_REP_READ_CHIP_TYPE) {
        const char* name = get_bsl_enum_name(type);
        char buf[128];
        snprintf(buf,
                 sizeof(buf),
                 "unexpected response (%s : 0x%04x)",
                 name ? name : "UNKNOWN",
                 type);
        return Result<std::string>::error(ErrorCode::ProtocolError, buf);
    }

    const std::uint16_t payload_len = READ16_BE(io->raw_buf + 2);
    std::string chipset;
    chipset.resize(256);

    const int copied = print_to_string(
        chipset.data(),
        chipset.size(),
        io->raw_buf + 4,
        payload_len,
        0);
    if (copied < 0) {
        return Result<std::string>::error(ErrorCode::InternalError, "print_to_string failed");
    }

    if (!chipset.empty() && chipset.back() == '\n') {
        chipset.pop_back();
    }

    return Result<std::string>::ok(chipset);
}

} // namespace

class DefaultDeviceService : public DeviceService {
public:
    DefaultDeviceService() = default;
    ~DefaultDeviceService() override = default;

    void setContext(spdio_t* io, AppState* app_state) override {
        io_ = io;
        app_ = app_state;
        DEG_LOG(I, "DeviceService context set: io=%p, app_state=%p", io_, app_);
    }

    DeviceStatus connect(const ConnectionOptions& options) override {
        return connectInternal(options, false);
    }

    DeviceStatus reconnect(const ConnectionOptions& options) override {
        return connectInternal(options, true);
    }

    DeviceStatus disconnect() override {
        if (!app_) {
            DEG_LOG(E, "DeviceService::disconnect: app_state not set");
            return make_error(DeviceErrorCode::InternalError, "app_state not set");
        }

        if (!io_) {
            DEG_LOG(W, "DeviceService::disconnect: io already null");
        }

        DEG_LOG(OP, "DeviceService::disconnect: closing device");
        spdio_free(io_);
        io_ = nullptr;

        app_->device.m_bOpened = 0;
        app_->device.device_stage = -1;
        app_->device.device_mode = -1;
        app_->transport.io = nullptr;
        app_->transport.bListenLibusb = 0;

        return make_ok("disconnected");
    }

    bool isConnected() const override {
        return app_ && app_->device.m_bOpened == 1;
    }

    DeviceStatus probeDevice(DeviceInfo& out_info) override {
        if (!app_) {
            DEG_LOG(E, "DeviceService::probeDevice: app_state not set");
            return make_error(DeviceErrorCode::InternalError, "app_state not set");
        }

        if (app_->device.m_bOpened != 1) {
            DEG_LOG(W, "DeviceService::probeDevice: device not connected");
            return make_error(DeviceErrorCode::NoDeviceFound, "device not connected");
        }

        // 1) 先构建基础信息（stage/mode 等）
        auto r = build_device_info(app_);
        if (!r) {
            DEG_LOG(E,
                    "DeviceService::probeDevice: build_device_info failed, code=%d, msg=%s",
                    static_cast<int>(r.code),
                    r.message.c_str());
            const std::string msg = r.message.empty() ? "failed to build device info" : r.message;
            return make_error(DeviceErrorCode::InternalError, msg);
        }

        // 2) 试图读取 CHIP_TYPE，用于填充 DeviceInfo.chipset（失败仅记日志，不影响返回值）
        if (io_) {
            auto chip = read_chip_type(io_);
            if (!chip) {
                DEG_LOG(W,
                        "DeviceService::probeDevice: read_chip_type failed, code=%d, msg=%s",
                        static_cast<int>(chip.code),
                        chip.message.c_str());
            } else {
                r.value.chipset = chip.value;
            }
        }

        // 3) READ_FLASH_INFO 试点：若成功，将结果映射到 DeviceInfo::flash_type
        if (io_) {
            auto flash = try_read_flash_info(io_);
            if (!flash) {
                DEG_LOG(W,
                        "DeviceService::probeDevice: try_read_flash_info failed, code=%d, msg=%s",
                        static_cast<int>(flash.code),
                        flash.message.c_str());
            } else {
                if (flash.value != FlashStorageType::Unknown) {
                    r.value.flash_type = flash.value;
                }
            }
        }

        out_info = r.value;
        cached_info_ = out_info;
        DEG_LOG(I, "DeviceService::probeDevice: stage=%d", app_->device.device_stage);
        return make_ok();
    }

    DeviceStatus getCachedDeviceInfo(DeviceInfo& out_info) const override {
        out_info = cached_info_;
        if (out_info.mode == DeviceMode::Unknown && out_info.stage == DeviceStage::Unknown) {
            return make_error(DeviceErrorCode::InternalError, "no cached device info");
        }
        return make_ok();
    }

    DeviceStage getCurrentStage() const override {
        if (!app_) return DeviceStage::Unknown;
        return map_stage_int(app_->device.device_stage);
    }

    DeviceMode getCurrentMode() const override {
        if (!app_) return DeviceMode::Unknown;
        return deduce_mode_from_globals();
    }

    DeviceStatus rebootToNormalOs() override {
        DEG_LOG(W, "DeviceService::rebootToNormalOs: not implemented yet");
        return make_error(DeviceErrorCode::Unsupported, "rebootToNormalOs not implemented");
    }

    DeviceStatus rebootToDownloadMode() override {
        DEG_LOG(W, "DeviceService::rebootToDownloadMode: not implemented yet");
        return make_error(DeviceErrorCode::Unsupported, "rebootToDownloadMode not implemented");
    }

    DeviceStatus setKeepCharge(bool /*enabled*/) override {
        DEG_LOG(W, "DeviceService::setKeepCharge: not implemented yet");
        return make_error(DeviceErrorCode::Unsupported, "setKeepCharge not implemented");
    }

    DeviceStatus startEventLoop() override {
#if USE_LIBUSB
        // 当前事件循环由 startUsbEventHandle 控制
        startUsbEventHandle();
        if (app_) app_->transport.bListenLibusb = 1;
        DEG_LOG(I, "DeviceService::startEventLoop: libusb event loop started");
        return make_ok();
#else
        DEG_LOG(W, "DeviceService::startEventLoop: not applicable on this platform");
        return make_error(DeviceErrorCode::Unsupported, "startEventLoop not supported");
#endif
    }

    DeviceStatus stopEventLoop() override {
#if USE_LIBUSB
        // 对应 usb_transport.cpp 中 spdio_free 的行为，这里只在需要时显式停止
        if (app_ && app_->transport.bListenLibusb) {
            stopUsbEventHandle();
            app_->transport.bListenLibusb = 0;
            DEG_LOG(I, "DeviceService::stopEventLoop: libusb event loop stopped");
        }
        return make_ok();
#else
        DEG_LOG(W, "DeviceService::stopEventLoop: not applicable on this platform");
        return make_error(DeviceErrorCode::Unsupported, "stopEventLoop not supported");
#endif
    }

    bool isEventLoopRunning() const override {
        return app_ && app_->transport.bListenLibusb != 0;
    }

private:
    DeviceStatus connectInternal(const ConnectionOptions& /*options*/, bool reconnect_mode) {
        if (!io_ || !app_) {
            DEG_LOG(E, "DeviceService::connectInternal: context not set");
            return make_error(DeviceErrorCode::InternalError, "context not set");
        }

        DEG_LOG(OP,
                "DeviceService::connectInternal: reconnect=%d",
                reconnect_mode ? 1 : 0);

        // T1-04 阶段仅提供占位实现，不主动修改全局连接流程，
        // 由现有 UI 逻辑负责真正的连接操作。
        if (app_->device.m_bOpened == 1) {
            return make_ok("already connected");
        }

        return make_error(DeviceErrorCode::NoDeviceFound, "device not connected (connect logic not yet wired)");
    }

    spdio_t* io_ = nullptr;
    AppState* app_ = nullptr;
    DeviceInfo cached_info_{};
};

std::unique_ptr<DeviceService> createDeviceService() {
    return std::unique_ptr<DeviceService>(new DefaultDeviceService());
}

} // namespace sfd
