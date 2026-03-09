#include "flash_service.h"
#include "../common.h"
#include "app_state.h"
#include "logging.h"
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

} // namespace

class DefaultFlashService : public FlashService {
public:
    DefaultFlashService() = default;
    ~DefaultFlashService() override = default;

    void setContext(spdio_t* io, AppState* app_state) override {
        io_ = io;
        app_ = app_state;
        DEG_LOG(I, "FlashService context set: io=%p, app_state=%p", io_, app_);
    }

    FlashStatus loadPacMetadata(const std::string& pac_path,
                                PacMetadata& out_metadata,
                                std::vector<PacPartitionEntry>& out_entries) override {
        out_entries.clear();

        if (pac_path.empty()) {
            DEG_LOG(E, "loadPacMetadata: empty pac_path");
            return make_error(FlashErrorCode::InvalidPacFile, "empty pac path");
        }

        if (!std::filesystem::exists(pac_path)) {
            DEG_LOG(E, "loadPacMetadata: PAC not found: %s", pac_path.c_str());
            return make_error(FlashErrorCode::InvalidPacFile, "PAC file not found");
        }

        const char* unpack_dir = "pac_unpack_output";
        DEG_LOG(I, "loadPacMetadata: pac_extract(%s, %s)", pac_path.c_str(), unpack_dir);
        if (!pac_extract(pac_path.c_str(), unpack_dir)) {
            DEG_LOG(E, "loadPacMetadata: pac_extract failed for %s", pac_path.c_str());
            return make_error(FlashErrorCode::InvalidPacFile, "pac_extract failed");
        }

        out_metadata.path = pac_path;
        std::error_code ec;
        auto sz = std::filesystem::file_size(pac_path, ec);
        if (!ec) {
            out_metadata.file_size = sz;
        }

        // 暂不从 PAC 中解析 product_name/version，留待后续任务完善

        DEG_LOG(I, "loadPacMetadata: ok, size=%llu", (unsigned long long)out_metadata.file_size);
        return make_ok();
    }

    FlashStatus flashPac(const FlashPacOptions& options) override {
        if (!io_ || !app_) {
            DEG_LOG(E, "flashPac: context not set");
            return make_error(FlashErrorCode::DeviceNotConnected, "context not set");
        }

        if (app_->device.m_bOpened == -1) {
            DEG_LOG(E, "flashPac: device detached");
            return make_error(FlashErrorCode::DeviceNotConnected, "device detached");
        }

        if (options.pac_path.empty()) {
            DEG_LOG(E, "flashPac: empty pac_path");
            return make_error(FlashErrorCode::InvalidPacFile, "empty pac path");
        }

        if (!std::filesystem::exists(options.pac_path)) {
            DEG_LOG(E, "flashPac: PAC not found: %s", options.pac_path.c_str());
            return make_error(FlashErrorCode::InvalidPacFile, "PAC file not found");
        }

        const char* unpack_dir = "pac_unpack_output";
        DEG_LOG(OP, "flashPac: pac_extract(%s, %s)", options.pac_path.c_str(), unpack_dir);
        if (!pac_extract(options.pac_path.c_str(), unpack_dir)) {
            DEG_LOG(E, "flashPac: pac_extract failed for %s", options.pac_path.c_str());
            return make_error(FlashErrorCode::InvalidPacFile, "pac_extract failed");
        }

        // slot 选择
        switch (options.slot_selection) {
        case SlotSelection::Auto:
            app_->flash.selected_ab = 0;
            break;
        case SlotSelection::SlotA:
            app_->flash.selected_ab = 1;
            break;
        case SlotSelection::SlotB:
            app_->flash.selected_ab = 2;
            break;
        }

        app_->flash.isCMethod = options.compatibility_mode ? 1 : 0;

        unsigned step = DEFAULT_BLK_SIZE;
        DEG_LOG(OP,
                "flashPac: load_partitions(dir=%s, step=%u, ab=%d, CMethod=%d)",
                unpack_dir,
                step,
                app_->flash.selected_ab,
                app_->flash.isCMethod);

        load_partitions(io_, unpack_dir, step, app_->flash.selected_ab, app_->flash.isCMethod);

        // 目前 load_partitions 内部自行处理错误，执行到此视为成功
        return make_ok();
    }

    FlashStatus refreshDevicePartitions(std::vector<DevicePartitionInfo>& out_partitions) override {
        out_partitions.clear();

        if (!io_ || !app_) {
            DEG_LOG(E, "refreshDevicePartitions: context not set");
            return make_error(FlashErrorCode::InternalError, "context not set");
        }

        // 如果还没有分区表，这里暂不主动触发 partition_list，保持与现有流程一致
        if (!io_->part_count && !io_->part_count_c) {
            DEG_LOG(W, "refreshDevicePartitions: no partition table loaded");
            return make_error(FlashErrorCode::PartitionTableNotLoaded, "no partition table loaded");
        }

        if (!app_->flash.isCMethod && io_->part_count) {
            for (int i = 0; i < io_->part_count; ++i) {
                const auto& p = io_->ptable[i];
                DevicePartitionInfo info{};
                info.name = p.name;
                // partition_t::size 目前语义是 MiB，在此转换为字节
                info.size = (std::uint64_t)p.size << 20;
                info.readable = true;
                info.writable = true;
                out_partitions.push_back(info);
            }
        } else if (app_->flash.isCMethod && io_->part_count_c) {
            for (int i = 0; i < io_->part_count_c; ++i) {
                const auto& p = io_->Cptable[i];
                DevicePartitionInfo info{};
                // partition_t::size 目前语义是 MiB，在此转换为字节
                info.size = (std::uint64_t)p.size << 20;
                out_partitions.push_back(info);
            }
        } else {
            DEG_LOG(W, "refreshDevicePartitions: inconsistent CMethod/ptable state");
            return make_error(FlashErrorCode::PartitionTableNotLoaded, "partition table not available");
        }

        cached_partitions_ = out_partitions;
        DEG_LOG(I, "refreshDevicePartitions: %zu partitions", out_partitions.size());
        return make_ok();
    }

    FlashStatus getCachedDevicePartitions(std::vector<DevicePartitionInfo>& out_partitions) const override {
        out_partitions = cached_partitions_;
        if (out_partitions.empty()) {
            return make_error(FlashErrorCode::PartitionTableNotLoaded, "no cached partitions");
        }
        return make_ok();
    }

    FlashStatus readPartitionToFile(const PartitionIoOptions& options) override {
        if (!io_ || !app_) {
            DEG_LOG(E, "readPartitionToFile: context not set");
            return make_error(FlashErrorCode::InternalError, "context not set");
        }

        if (options.partition_name.empty() || options.file_path.empty()) {
            DEG_LOG(E, "readPartitionToFile: empty partition name or file path");
            return make_error(FlashErrorCode::InvalidPacFile, "invalid partition io options");
        }

        // 确保分区表存在
        if (!io_->part_count && !io_->part_count_c) {
            DEG_LOG(E, "readPartitionToFile: no partition table loaded");
            return make_error(FlashErrorCode::PartitionTableNotLoaded, "no partition table loaded");
        }

        get_partition_info(io_, options.partition_name.c_str(), 1);
        if (!gPartInfo.size) {
            DEG_LOG(E, "readPartitionToFile: partition %s not found", options.partition_name.c_str());
            return make_error(FlashErrorCode::PartitionNotFound, "partition not found");
        }

        unsigned step = options.block_size ? options.block_size : DEFAULT_BLK_SIZE;

        DEG_LOG(OP, "readPartitionToFile: %s -> %s, size=%lld, step=%u",
                gPartInfo.name, options.file_path.c_str(), (long long)gPartInfo.size, step);

        dump_partition(io_, gPartInfo.name, 0, gPartInfo.size, options.file_path.c_str(), step);

        // 暂不根据返回值细化错误，保持与现有行为一致
        return make_ok();
    }

    FlashStatus writePartitionFromFile(const PartitionIoOptions& options) override {
        if (!io_ || !app_) {
            DEG_LOG(E, "writePartitionFromFile: context not set");
            return make_error(FlashErrorCode::InternalError, "context not set");
        }

        if (options.partition_name.empty() || options.file_path.empty()) {
            DEG_LOG(E, "writePartitionFromFile: empty partition name or file path");
            return make_error(FlashErrorCode::InvalidPacFile, "invalid partition io options");
        }

        if (!std::filesystem::exists(options.file_path)) {
            DEG_LOG(E, "writePartitionFromFile: file not found: %s", options.file_path.c_str());
            return make_error(FlashErrorCode::IoError, "input file not found");
        }

        // 确保分区表存在
        if (!io_->part_count && !io_->part_count_c) {
            DEG_LOG(E, "writePartitionFromFile: no partition table loaded");
            return make_error(FlashErrorCode::PartitionTableNotLoaded, "no partition table loaded");
        }

        get_partition_info(io_, options.partition_name.c_str(), 0);
        if (!gPartInfo.size) {
            DEG_LOG(E, "writePartitionFromFile: partition %s not found", options.partition_name.c_str());
            return make_error(FlashErrorCode::PartitionNotFound, "partition not found");
        }

        unsigned step = options.block_size ? options.block_size : DEFAULT_BLK_SIZE;

        DEG_LOG(OP, "writePartitionFromFile: %s <- %s, step=%u, force=%d, CMethod=%d",
                gPartInfo.name, options.file_path.c_str(), step,
                options.force ? 1 : 0, app_->flash.isCMethod);

        if (!options.force) {
            load_partition_unify(io_, gPartInfo.name, options.file_path.c_str(), step, app_->flash.isCMethod);
        } else {
            // 强制写：需要找到分区索引，并避免 splloader
            int index = -1;
            if (!app_->flash.isCMethod && io_->part_count) {
                for (int i = 0; i < io_->part_count; ++i) {
                    if (!strcmp(io_->ptable[i].name, gPartInfo.name)) {
                        index = i;
                        break;
                    }
                }
            } else if (app_->flash.isCMethod && io_->part_count_c) {
                for (int i = 0; i < io_->part_count_c; ++i) {
                    if (!strcmp(io_->Cptable[i].name, gPartInfo.name)) {
                        index = i;
                        break;
                    }
                }
            }

            if (index < 0) {
                DEG_LOG(E, "writePartitionFromFile: cannot locate partition index for %s", gPartInfo.name);
                return make_error(FlashErrorCode::PartitionNotFound, "partition index not found");
            }

            if (!strcmp(gPartInfo.name, "splloader")) {
                DEG_LOG(E, "writePartitionFromFile: refusing to force write splloader");
                return make_error(FlashErrorCode::UnsupportedOperation, "cannot force write splloader");
            }

            load_partition_force(io_, index, options.file_path.c_str(), step,
                                 app_->flash.isCMethod ? 1 : 0);
        }

        return make_ok();
    }

    FlashStatus backupPartitions(const std::vector<std::string>& partition_names,
                                 const std::string& output_directory,
                                 SlotSelection /*slot_selection*/) override {
        if (!io_ || !app_) {
            DEG_LOG(E, "backupPartitions: context not set");
            return make_error(FlashErrorCode::InternalError, "context not set");
        }

        if (output_directory.empty()) {
            DEG_LOG(E, "backupPartitions: empty output directory");
            return make_error(FlashErrorCode::IoError, "empty output directory");
        }

        std::error_code ec;
        std::filesystem::create_directories(output_directory, ec);

        std::vector<std::string> names = partition_names;
        if (names.empty()) {
            // 备份全部分区
            if (!app_->flash.isCMethod && io_->part_count) {
                for (int i = 0; i < io_->part_count; ++i) {
                    names.push_back(io_->ptable[i].name);
                }
            } else if (app_->flash.isCMethod && io_->part_count_c) {
                for (int i = 0; i < io_->part_count_c; ++i) {
                    names.push_back(io_->Cptable[i].name);
                }
            }
        }

        unsigned step = DEFAULT_BLK_SIZE;

        for (const auto& name : names) {
            get_partition_info(io_, name.c_str(), 1);
            if (!gPartInfo.size) {
                DEG_LOG(W, "backupPartitions: skip missing partition %s", name.c_str());
                continue;
            }

            auto out_path = std::filesystem::path(output_directory) / (name + ".img");
            DEG_LOG(OP, "backupPartitions: %s -> %s, size=%lld, step=%u",
                    gPartInfo.name, out_path.string().c_str(), (long long)gPartInfo.size, step);

            dump_partition(io_, gPartInfo.name, 0, gPartInfo.size,
                           out_path.string().c_str(), step);
        }

        return make_ok();
    }

    FlashStatus verifyPartition(const std::string& partition_name,
                                SlotSelection /*slot_selection*/) override {
        // 当前工程中暂无显式 verify 实现，这里先返回不支持
        DEG_LOG(W, "verifyPartition: not implemented, partition=%s", partition_name.c_str());
        return make_error(FlashErrorCode::UnsupportedOperation, "verifyPartition not implemented");
    }

    FlashStatus queryPacFlashTime(std::uint64_t& out_seconds) override {
        if (!io_) {
            DEG_LOG(E, "queryPacFlashTime: io not set");
            return make_error(FlashErrorCode::DeviceNotConnected, "io not set");
        }

        out_seconds = read_pactime(io_);
        DEG_LOG(I, "queryPacFlashTime: %llu s", (unsigned long long)out_seconds);
        return make_ok();
    }

private:
    spdio_t* io_ = nullptr;
    AppState* app_ = nullptr;
    std::vector<DevicePartitionInfo> cached_partitions_;
};

std::unique_ptr<FlashService> createFlashService() {
    return std::unique_ptr<FlashService>(new DefaultFlashService());
}

} // namespace sfd
