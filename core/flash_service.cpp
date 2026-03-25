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

        // 计算逻辑期望读取大小
        uint64_t expected = (uint64_t)gPartInfo.size;
        if (std::strstr(gPartInfo.name, "nv1")) {
            if (expected > 512) {
                expected -= 512; // 与 dump_partition 中 nv1 特例保持一致
            } else {
                DEG_LOG(E, "PartitionReadService::readOne: nv1 partition too small, size=%llu",\
                        (unsigned long long)expected);
                FlashStatus err = make_error(FlashErrorCode::InvalidPacFile, "nv1 partition too small");
                if (callbacks && callbacks->on_finished) {
                    callbacks->on_finished(actual_info, err);
                }
                return err;
            }
        }

        // 粗粒度进度回调：整个分区读完后上报一次
        if (callbacks && callbacks->on_progress) {
            double elapsed_s = (end_ts > start_ts) ? (end_ts - start_ts) : 0.0;
            double speed_mb_s = 0.0;
            if (elapsed_s > 0.0 && expected > 0) {
                speed_mb_s = (static_cast<double>(expected) / (1024.0 * 1024.0)) / elapsed_s;
            }
            callbacks->on_progress(actual_info, saved, speed_mb_s);
        }

        FlashStatus result;
        if (saved != expected) {
            if (isCancel) {
                DEG_LOG(W, "PartitionReadService::readOne: cancelled, saved=%llu / %llu",\
                        (unsigned long long)saved, (unsigned long long)expected);
                result = make_error(FlashErrorCode::Cancelled, "partition read cancelled");
            } else {
                DEG_LOG(E, "PartitionReadService::readOne: short read, saved=%llu / %llu (raw_size=%lld)",\
                        (unsigned long long)saved, (unsigned long long)expected, (long long)gPartInfo.size);
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

class DefaultFlashService : public FlashService {
public:
    DefaultFlashService() = default;
    ~DefaultFlashService() override = default;

    PartitionReadService& partitionReader() override {
        return partition_reader_;
    }

    void setContext(spdio_t* io, AppState* app_state) override {
        io_ = io;
        app_ = app_state;
        DEG_LOG(I, "FlashService context set: io=%p, app_state=%p", io_, app_);
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
                // partition_t::size 当前语义是字节，这里直接透传
                info.size = (std::uint64_t)p.size;
                info.readable = true;
                info.writable = true;
                out_partitions.push_back(info);
            }
        } else if (app_->flash.isCMethod && io_->part_count_c) {
            for (int i = 0; i < io_->part_count_c; ++i) {
                const auto& p = io_->Cptable[i];
                DevicePartitionInfo info{};
                info.name = p.name;
                // partition_t::size 当前语义是字节，这里直接透传
                info.size = (std::uint64_t)p.size;
                info.readable = true;
                info.writable = true;
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

        // 构造 PartitionReadInfo
        PartitionReadInfo info{};
        info.name = options.partition_name;

        // 构造 BlockSizeConfig：GUI/CLI 通过 block_size 选择 AUTO 或 MANUAL
        BlockSizeConfig blk_cfg{};
        if (options.block_size) {
            blk_cfg.mode = BlockSizeMode::MANUAL_BLOCK_SIZE;
            blk_cfg.manual_block_size = options.block_size;
        } else {
            blk_cfg.mode = BlockSizeMode::AUTO_DEFAULT;
            blk_cfg.manual_block_size = 0;
        }

        PartitionReadOptions read_opts{};
        read_opts.output_path = options.file_path;
        read_opts.block_cfg = blk_cfg;

        DEG_LOG(OP, "readPartitionToFile: via PartitionReadService, part=%s, file=%s, blk_mode=%d, blk_manual=%u",\
                options.partition_name.c_str(), options.file_path.c_str(),\
                static_cast<int>(blk_cfg.mode), blk_cfg.manual_block_size);

        return partition_reader_.readOne(info, read_opts, nullptr);
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
                                 SlotSelection /*slot_selection*/,
                                 const BlockSizeConfig& blk_cfg) override {
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
            // 备份全部分区（默认排除 userdata，避免体积过大影响体验）
            if (!app_->flash.isCMethod && io_->part_count) {
                for (int i = 0; i < io_->part_count; ++i) {
                    const char* pname = io_->ptable[i].name;
                    if (pname && std::strncmp(pname, "userdata", 8) == 0) {
                        continue;
                    }
                    names.push_back(pname);
                }
            } else if (app_->flash.isCMethod && io_->part_count_c) {
                for (int i = 0; i < io_->part_count_c; ++i) {
                    const char* pname = io_->Cptable[i].name;
                    if (pname && std::strncmp(pname, "userdata", 8) == 0) {
                        continue;
                    }
                    names.push_back(pname);
                }
            }
        }

        auto& reader = partition_reader_;

        for (const auto& name : names) {
            PartitionReadInfo info{};
            info.name = name;

            auto out_path = std::filesystem::path(output_directory) / (name + ".img");

            PartitionReadOptions opts{};
            opts.output_path = out_path.string();
            opts.block_cfg = blk_cfg;

            DEG_LOG(OP, "backupPartitions: %s -> %s", name.c_str(), opts.output_path.c_str());

            FlashStatus st = reader.readOne(info, opts, nullptr);
            if (!st.success) {
                if (st.code == FlashErrorCode::PartitionNotFound) {
                    // 与旧实现一致：缺失分区仅记录并跳过
                    DEG_LOG(W, "backupPartitions: skip missing partition %s", name.c_str());
                    continue;
                }
                return st;
            }
        }

        return make_ok();
    }

    FlashStatus erasePartition(const std::string& partition_name) override {
        if (!io_ || !app_) {
            DEG_LOG(E, "erasePartition: context not set");
            return make_error(FlashErrorCode::InternalError, "context not set");
        }

        if (partition_name.empty()) {
            DEG_LOG(E, "erasePartition: empty partition name");
            return make_error(FlashErrorCode::PartitionNotFound, "empty partition name");
        }

        // 确保分区表存在
        if (!io_->part_count && !io_->part_count_c) {
            DEG_LOG(E, "erasePartition: no partition table loaded");
            return make_error(FlashErrorCode::PartitionTableNotLoaded, "no partition table loaded");
        }

        get_partition_info(io_, partition_name.c_str(), 0);
        if (!gPartInfo.size) {
            DEG_LOG(E, "erasePartition: partition %s not found", partition_name.c_str());
            return make_error(FlashErrorCode::PartitionNotFound, "partition not found");
        }

        DEG_LOG(OP, "erasePartition: %s, CMethod=%d", gPartInfo.name, app_->flash.isCMethod);
        erase_partition(io_, gPartInfo.name, app_->flash.isCMethod);
        return make_ok();
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

    FlashStatus exportPartitionTableToXml(const std::string& output_path) override {
        if (!io_ || !app_) {
            DEG_LOG(E, "exportPartitionTableToXml: context not set");
            return make_error(FlashErrorCode::InternalError, "context not set");
        }

        if (output_path.empty()) {
            DEG_LOG(E, "exportPartitionTableToXml: empty output path");
            return make_error(FlashErrorCode::IoError, "empty output path");
        }

        FILE* fo = my_oxfopen(output_path.c_str(), "wb");
        if (!fo) {
            DEG_LOG(E, "exportPartitionTableToXml: failed to open %s", output_path.c_str());
            return make_error(FlashErrorCode::IoError, "failed to open output file");
        }

        std::fprintf(fo, "<Partitions>\n");

        if (!app_->flash.isCMethod) {
            if (!io_->part_count) {
                fclose(fo);
                DEG_LOG(E, "exportPartitionTableToXml: no partition table");
                return make_error(FlashErrorCode::PartitionTableNotLoaded, "no partition table loaded");
            }

            for (int i = 0; i < io_->part_count; ++i) {
                const auto& p = io_->ptable[i];
                std::fprintf(fo, "    <Partition id=\"%s\" size=\"", p.name);
                if (i + 1 == io_->part_count) {
                    std::fprintf(fo, "0x%x\"/>\n", ~0);
                } else {
                    std::fprintf(fo, "%lld\"/>\n", ((long long)p.size >> 20));
                }
            }
        } else {
            int c = io_->part_count_c;
            if (!c) {
                fclose(fo);
                DEG_LOG(E, "exportPartitionTableToXml: no CMethod partition table");
                return make_error(FlashErrorCode::PartitionTableNotLoaded, "no CMethod partition table");
            }

            int o = io_->verbose;
            io_->verbose = -1;
            for (int i = 0; i < c; ++i) {
                char* name = io_->Cptable[i].name;
                std::fprintf(fo, "    <Partition id=\"%s\" size=\"", name);
                if (check_partition(io_, "userdata", 0) != 0 && i + 1 == io_->part_count_c) {
                    std::fprintf(fo, "0x%x\"/>\n", ~0);
                } else {
                    std::fprintf(fo, "%lld\"/>\n", ((long long)io_->Cptable[i].size >> 20));
                }
            }
            io_->verbose = o;
        }

        std::fprintf(fo, "</Partitions>");
        fclose(fo);
        DEG_LOG(I, "exportPartitionTableToXml: saved to %s", output_path.c_str());
        return make_ok();
    }

private:
    spdio_t* io_ = nullptr;
    AppState* app_ = nullptr;
    std::vector<DevicePartitionInfo> cached_partitions_;
    DefaultPartitionReadService partition_reader_{io_, app_};
};

std::unique_ptr<FlashService> createFlashService() {
    return std::unique_ptr<FlashService>(new DefaultFlashService());
}

} // namespace sfd
