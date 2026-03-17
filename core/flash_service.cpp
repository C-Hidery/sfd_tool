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
        DEG_LOG(OP,
                "loadPacMetadata: pac_unpack_and_analyze(%s, %s)", pac_path.c_str(), unpack_dir);
        auto r = pac_unpack_and_analyze(pac_path.c_str(), unpack_dir);
        if (!r) {
            const int code_int = static_cast<int>(r.code);
            const char* code_str = "unknown";
            switch (r.code) {
            case ErrorCode::InvalidArgument: code_str = "InvalidArgument"; break;
            case ErrorCode::NotFound:        code_str = "NotFound";        break;
            case ErrorCode::ParseError:      code_str = "ParseError";      break;
            default:                         code_str = "Other";           break;
            }

            DEG_LOG(E,
                    "loadPacMetadata: pac_unpack_and_analyze failed for %s, code=%d(%s), msg=%s",
                    pac_path.c_str(),
                    code_int,
                    code_str,
                    r.message.c_str());
            FlashErrorCode code = map_error_code(r.code);
            std::string msg = r.message.empty() ? "pac_unpack_and_analyze failed" : r.message;
            return make_error(code, msg);
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

    FlashStatus preparePacFlash(const FlashPacOptions& options,
                                const char*& out_unpack_dir,
                                FlashPacStageCallback on_stage) {
        DEG_LOG(OP, "preparePacFlash: begin (pac=%s)", options.pac_path.c_str());

        auto emit_stage = [&](FlashPacStage stage) {
            if (on_stage) {
                on_stage(stage);
            }
        };

        emit_stage(FlashPacStage::ValidateContext);
        // Stage 1: ValidateContext
        if (!io_ || !app_) {
            DEG_LOG(E, "preparePacFlash: stage=ValidateContext, context not set");
            return make_error(
                FlashErrorCode::DeviceNotConnected,
                "PAC flash failed at ValidateContext: context not set");
        }

        if (app_->device.m_bOpened == -1) {
            DEG_LOG(E, "preparePacFlash: stage=ValidateContext, device detached");
            return make_error(
                FlashErrorCode::DeviceNotConnected,
                "PAC flash failed at ValidateContext: device detached");
        }

        emit_stage(FlashPacStage::ValidatePac);
        // Stage 2: ValidatePac
        DEG_LOG(OP, "preparePacFlash: stage=ValidatePac path=%s", options.pac_path.c_str());

        if (options.pac_path.empty()) {
            DEG_LOG(E, "preparePacFlash: stage=ValidatePac, empty pac_path");
            return make_error(
                FlashErrorCode::InvalidPacFile,
                "PAC flash failed at ValidatePac: empty pac path");
        }

        if (!std::filesystem::exists(options.pac_path)) {
            DEG_LOG(E, "preparePacFlash: stage=ValidatePac, PAC not found: %s", options.pac_path.c_str());
            return make_error(
                FlashErrorCode::InvalidPacFile,
                "PAC flash failed at ValidatePac: PAC file not found");
        }

        emit_stage(FlashPacStage::ExtractPac);
        // Stage 3: ExtractPac
        const char* unpack_dir = "pac_unpack_output";
        DEG_LOG(OP,
                "preparePacFlash: stage=ExtractPac pac_unpack_and_analyze(%s, %s)",
                options.pac_path.c_str(),
                unpack_dir);
        auto info_result = pac_unpack_and_analyze(options.pac_path.c_str(), unpack_dir);
        if (!info_result) {
            DEG_LOG(E,
                    "preparePacFlash: stage=ExtractPac, pac_unpack_and_analyze failed for %s, code=%d, msg=%s",
                    options.pac_path.c_str(),
                    static_cast<int>(info_result.code),
                    info_result.message.c_str());
            FlashErrorCode code = map_error_code(info_result.code);
            std::string detail = info_result.message.empty() ? "pac_unpack_and_analyze failed" : info_result.message;
            std::string msg = "PAC flash failed at ExtractPac: " + detail;
            return make_error(code, msg);
        }

        out_unpack_dir = unpack_dir;
        return make_ok();
    }

    FlashStatus executePacFlash(const FlashPacOptions& options,
                                const char* unpack_dir,
                                FlashPacStageCallback on_stage) {
        auto emit_stage = [&](FlashPacStage stage) {
            if (on_stage) {
                on_stage(stage);
            }
        };

        emit_stage(FlashPacStage::ConfigureState);
        // Stage 4: ConfigureState
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

        DEG_LOG(OP,
                "executePacFlash: stage=ConfigureState slot=%d CMethod=%d",
                app_->flash.selected_ab,
                app_->flash.isCMethod);

        emit_stage(FlashPacStage::ExecuteFlash);
        // Stage 5: ExecuteFlash
        unsigned step = DEFAULT_BLK_SIZE;
        DEG_LOG(OP,
                "executePacFlash: stage=ExecuteFlash load_partitions(dir=%s, step=%u, ab=%d, CMethod=%d)",
                unpack_dir,
                step,
                app_->flash.selected_ab,
                app_->flash.isCMethod);

        load_partitions(io_, unpack_dir, step, app_->flash.selected_ab, app_->flash.isCMethod);

        // 目前 load_partitions 内部自行处理错误，执行到此视为成功
        DEG_LOG(OP, "executePacFlash: success");
        emit_stage(FlashPacStage::Done);
        return make_ok();
    }

    FlashStatus flashPac(const FlashPacOptions& options,
                         FlashPacStageCallback on_stage) override {
        DEG_LOG(OP, "flashPac: begin (pac=%s)", options.pac_path.c_str());

        const char* unpack_dir = nullptr;
        FlashStatus prep = preparePacFlash(options, unpack_dir, on_stage);
        if (!prep.success) {
            return prep;
        }

        return executePacFlash(options, unpack_dir, on_stage);
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
                                 SlotSelection /*slot_selection*/,
                                 std::uint32_t block_size) override {
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

        unsigned step = block_size ? block_size : DEFAULT_BLK_SIZE;

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
};

std::unique_ptr<FlashService> createFlashService() {
    return std::unique_ptr<FlashService>(new DefaultFlashService());
}

} // namespace sfd
