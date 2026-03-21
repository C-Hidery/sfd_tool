#include "flash_service.h"
#include "logging.h"
#include "result.h"
#include "../common.h"
#include "app_state.h"
#include "file_io.h"

#include <filesystem>
#include <memory>

using namespace std;

namespace sfd {

namespace {

static FlashStatus make_error(FlashErrorCode code, const std::string& msg) {
    FlashStatus s;
    s.success = false;
    s.code = code;
    s.message = msg;
    return s;
}

static FlashStatus make_ok() {
    FlashStatus s;
    s.success = true;
    s.code = FlashErrorCode::Ok;
    return s;
}

static FlashErrorCode map_error_code(ErrorCode code) {
    switch (code) {
    case ErrorCode::Ok:
        return FlashErrorCode::Ok;
    case ErrorCode::DeviceNotConnected:
        return FlashErrorCode::DeviceNotConnected;
    case ErrorCode::IoError:
        return FlashErrorCode::IoError;
    case ErrorCode::Unsupported:
        return FlashErrorCode::UnsupportedOperation;
    case ErrorCode::Cancelled:
        return FlashErrorCode::Cancelled;
    default:
        // 在 PAC 相关路径，其他错误统一视为 PAC 文件问题
        return FlashErrorCode::InvalidPacFile;
    }
}

} // namespace

// 默认实现：直接复用已有 readPartitionToFile 行为
class DefaultPartitionReadService : public PartitionReadService {
public:
    explicit DefaultPartitionReadService(spdio_t*& io_ref, AppState*& app_ref)
        : io_ref_(io_ref), app_ref_(app_ref) {}

    FlashStatus readOne(const PartitionReadInfo& info,
                        const PartitionReadOptions& options,
                        const PartitionReadCallbacks* callbacks) override {
        spdio_t* io = io_ref_;
        AppState* app = app_ref_;
        if (!io || !app) {
            DEG_LOG(E, "PartitionReadService::readOne: context not set");
            return make_error(FlashErrorCode::InternalError, "context not set");
        }

        if (info.name.empty() || options.output_path.empty()) {
            DEG_LOG(E, "PartitionReadService::readOne: empty partition name or output path");
            return make_error(FlashErrorCode::InvalidPacFile, "invalid partition read options");
        }

        // 确保分区表存在
        if (!io->part_count && !io->part_count_c) {
            DEG_LOG(E, "PartitionReadService::readOne: no partition table loaded");
            return make_error(FlashErrorCode::PartitionTableNotLoaded, "no partition table loaded");
        }

        get_partition_info(io, info.name.c_str(), 1);
        if (!gPartInfo.size) {
            DEG_LOG(E, "PartitionReadService::readOne: partition %s not found", info.name.c_str());
            return make_error(FlashErrorCode::PartitionNotFound, "partition not found");
        }

        PartitionReadInfo actual_info = info;
        actual_info.size = (std::uint64_t)gPartInfo.size;

        unsigned step = ResolveBlockStep(options.block_cfg, DEFAULT_BLK_SIZE);

        DEG_LOG(OP, "PartitionReadService::readOne: %s -> %s, size=%lld, step=%u",\
                gPartInfo.name, options.output_path.c_str(), (long long)gPartInfo.size, step);

        // 回调：开始
        if (callbacks && callbacks->on_start) {
            callbacks->on_start(actual_info);
        }

        // 这里直接调用 legacy dump_partition，保持行为与原来一致
        start_signal();
        double start_ts = get_time();
        uint64_t saved = dump_partition(io, gPartInfo.name, 0, gPartInfo.size,
                                        options.output_path.c_str(), step);
        double end_ts = get_time();

        // 粗粒度进度回调：整个分区读完后上报一次
        if (callbacks && callbacks->on_progress) {
            double elapsed_s = (end_ts > start_ts) ? (end_ts - start_ts) : 0.0;
            double speed_mb_s = 0.0;
            if (elapsed_s > 0.0 && gPartInfo.size > 0) {
                speed_mb_s = (static_cast<double>(gPartInfo.size) / (1024.0 * 1024.0)) / elapsed_s;
            }
            callbacks->on_progress(actual_info, saved, speed_mb_s);
        }

        FlashStatus result;
        if (saved != (uint64_t)gPartInfo.size) {
            if (isCancel) {
                DEG_LOG(W, "PartitionReadService::readOne: cancelled, saved=%llu / %lld",\
                        (unsigned long long)saved, (long long)gPartInfo.size);
                result = make_error(FlashErrorCode::Cancelled, "partition read cancelled");
            } else {
                DEG_LOG(E, "PartitionReadService::readOne: short read, saved=%llu / %lld",\
                        (unsigned long long)saved, (long long)gPartInfo.size);
                result = make_error(FlashErrorCode::IoError, "partition read incomplete");
            }
        } else {
            result = make_ok();
        }

        if (callbacks && callbacks->on_finished) {
            callbacks->on_finished(actual_info, result);
        }

        return result;
    }

private:
    spdio_t*&  io_ref_;
    AppState*& app_ref_;
};


} // namespace sfd
