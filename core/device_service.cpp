#include "device_service.h"
#include "app_state.h"
#include "logging.h"
#include "usb_transport.h"
#include "result.h"
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

static Result<DeviceInfo> build_device_info(AppState* app) {
    DeviceInfo info{};

    // 当前信息分散在全局变量中，这里先填充最基本的字段
    info.chipset.clear();
    info.product_name.clear();
    info.firmware_version.clear();
    info.build_info.clear();
    info.nand_total_size = 0;
    info.block_size = 0;
    info.stage = map_stage_int(app->device.device_stage);
    info.mode = deduce_mode_from_globals();

    return Result<DeviceInfo>::ok(info);
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

        auto r = build_device_info(app_);
        if (!r) {
            DEG_LOG(E,
                    "DeviceService::probeDevice: build_device_info failed, code=%d, msg=%s",
                    static_cast<int>(r.code),
                    r.message.c_str());
            const std::string msg = r.message.empty() ? "failed to build device info" : r.message;
            return make_error(DeviceErrorCode::InternalError, msg);
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
