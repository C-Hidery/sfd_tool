/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#include "page_partition.h"
#include "../common.h"
#include "../main.h"
#include "../i18n.h"
#include "../core/flash_service.h"
#include "ui/ui_common.h"
#include <thread>
#include <iostream>
#ifndef _WIN32
#include <dirent.h>
#include <sys/stat.h>
#endif
#include <cstdint>
#include <algorithm>
#include <map>
#include <filesystem>
#include <string>
#include <vector>
#include "../core/XmlParser.hpp"

extern spdio_t*& io;
extern int ret;
extern int& m_bOpened;
extern int blk_size;
extern AppState g_app_state;

// 兼容旧逻辑：isCMethod 始终映射到 AppState::flash.isCMethod
static int& isCMethod = g_app_state.flash.isCMethod;

static std::unique_ptr<sfd::FlashService> g_flash_service;

static sfd::FlashService* ensure_flash_service() {
	if (!g_flash_service) {
		g_flash_service = sfd::createFlashService();
	}
	g_flash_service->setContext(io, &g_app_state);
	return g_flash_service.get();
}

static void set_button_label_if_valid(GtkWidget* widget, const char* text) {
	if (!widget || !GTK_IS_BUTTON(widget) || !text) return;
	gtk_button_set_label(GTK_BUTTON(widget), text);
}

static void set_partition_modify_busy(GtkWidgetHelper helper, bool busy) {
	if (busy) {
		helper.disableWidget("modify_part");
		helper.disableWidget("modify_new_part");
		helper.disableWidget("modify_rm_part");
		helper.disableWidget("modify_ren_part");
		set_button_label_if_valid(helper.getWidget("modify_part"), _("Processing..."));
		set_button_label_if_valid(helper.getWidget("modify_new_part"), _("Processing..."));
		set_button_label_if_valid(helper.getWidget("modify_rm_part"), _("Processing..."));
		set_button_label_if_valid(helper.getWidget("modify_ren_part"), _("Processing..."));
		helper.setLabelText(helper.getWidget("con"), _("Modify partition table"));
		return;
	}

	helper.enableWidget("modify_part");
	helper.enableWidget("modify_new_part");
	helper.enableWidget("modify_rm_part");
	helper.enableWidget("modify_ren_part");
	set_button_label_if_valid(helper.getWidget("modify_part"), _("Modify"));
	set_button_label_if_valid(helper.getWidget("modify_new_part"), _("Modify"));
	set_button_label_if_valid(helper.getWidget("modify_rm_part"), _("Delete"));
	set_button_label_if_valid(helper.getWidget("modify_ren_part"), _("Modify"));
	helper.setLabelText(helper.getWidget("con"), "Ready");
}

void populatePartitionList(GtkWidgetHelper& helper, const std::vector<sfd::DevicePartitionInfo>& partitions) {
	GtkWidget* part_list = helper.getWidget("part_list");
	if (!part_list || !GTK_IS_TREE_VIEW(part_list)) {
		std::cerr << "part_list not found or not a TreeView" << std::endl;
		return;
	}

	GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(part_list));
	if (!model) {
		std::cerr << "TreeView model not found" << std::endl;
		return;
	}

	GtkListStore* store = GTK_LIST_STORE(model);
	gtk_list_store_clear(store);

	int index = 1;
	GtkTreeIter iter_spl;
	gtk_list_store_append(store, &iter_spl);
	long long spl_size = g_spl_size > 0 ? g_spl_size : 0;
	std::string display_name = std::to_string(index) + ". splloader";
	std::string size_str;
	if (spl_size < 1024) {
		size_str = std::to_string(spl_size) + " B";
	} else {
		size_str = std::to_string(spl_size / 1024) + " KB";
	}
	gtk_list_store_set(store, &iter_spl,
	                   0, display_name.c_str(),
	                   1, size_str.c_str(),
	                   2, "splloader",
	                   -1);

	index++;
	for (const auto& partition : partitions) {
		GtkTreeIter iter;
		gtk_list_store_append(store, &iter);

		std::string display_name = std::to_string(index) + ". " + partition.name;

		std::string size_str;
		if (partition.size < 1024ULL) {
			size_str = std::to_string(partition.size) + " B";
		} else if (partition.size < 1024ULL * 1024) {
			size_str = std::to_string(partition.size / 1024) + " KB";
		} else if (partition.size < 1024ULL * 1024 * 1024) {
			size_str = std::to_string(partition.size / (1024 * 1024)) + " MB";
		} else {
			size_str = std::to_string(partition.size / (1024 * 1024 * 1024.0)) + " GB";
		}

		gtk_list_store_set(store, &iter,
		                   0, display_name.c_str(),
		                   1, size_str.c_str(),
		                   2, partition.name.c_str(),
		                   -1);

		index++;
	}

	gtk_widget_queue_draw(part_list);
}

std::string getSelectedPartitionName(GtkWidgetHelper& helper) {
	GtkWidget* part_list = helper.getWidget("part_list");
	if (!part_list || !GTK_IS_TREE_VIEW(part_list)) {
		return "";
	}

	GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(part_list));
	GtkTreeModel* model;
	GtkTreeIter iter;

	if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
		gchar* original_name = nullptr;
		gtk_tree_model_get(model, &iter, 2, &original_name, -1);

		if (original_name) {
			std::string name = original_name;
			g_free(original_name);
			return name;
		}
	}

	return "";
}

void update_partition_size(spdio_t* io) {
    if(!isCMethod) {
        for(int i = 0; i < io->part_count; i++) {
            int v1 = io->verbose;
            io->verbose = -1;
            (*(io->ptable + i)).size = check_partition(io, (*(io->ptable + i)).name, 1);
            io->verbose = v1;
        }
    }
    else {
        for(int i = 0; i < io->part_count_c; i++) {
            int v1 = io->verbose;
            io->verbose = -1;
            (*(io->Cptable + i)).size = check_partition(io, (*(io->Cptable + i)).name, 1);
            io->verbose = v1;
        }
    }

}

// 修改分区表后统一刷新主分区表（io->ptable）尺寸，避免在 UI 线程中执行耗时 check_partition。
static void update_partition_size_primary(spdio_t* io) {
    if (!io || !io->ptable || io->part_count <= 0) return;
    for (int i = 0; i < io->part_count; ++i) {
        int v1 = io->verbose;
        io->verbose = -1;
        io->ptable[i].size = check_partition(io, io->ptable[i].name, 1);
        io->verbose = v1;
    }
}

static std::vector<sfd::DevicePartitionInfo> build_primary_partition_view() {
    std::vector<sfd::DevicePartitionInfo> partitions;
    if (!io || !io->ptable || io->part_count <= 0) return partitions;
    partitions.reserve(io->part_count);
    for (int i = 0; i < io->part_count; ++i) {
        sfd::DevicePartitionInfo info{};
        info.name = io->ptable[i].name;
        info.size = static_cast<std::uint64_t>(io->ptable[i].size);
        info.readable = true;
        info.writable = true;
        partitions.push_back(info);
    }
    return partitions;
}

struct BatchPartitionWriteItem {
    sfd::DevicePartitionInfo part;
    std::string image_path;
    std::uint64_t image_size;
    bool is_critical;
    bool selected;
};

struct BackupInspectionItem {
    sfd::DevicePartitionInfo part;
    std::string image_path;
    std::uint64_t expected_size = 0;
    std::uint64_t image_size = 0;
    bool matched = false;
    bool is_critical = false;
    bool size_match = false;
    bool empty_file = false;
    bool all_zero = false;
    bool analysis_error = false;
    std::string note;
};

struct BackupImageFileInfo {
    std::string path;
    std::uint64_t size = 0;
    int priority = 0;
};

static std::string format_size_bytes(std::uint64_t bytes) {
    if (bytes < 1024ULL) {
        return std::to_string(bytes) + " B";
    }
    if (bytes < 1024ULL * 1024ULL) {
        return std::to_string(bytes / 1024ULL) + " KB";
    }
    if (bytes < 1024ULL * 1024ULL * 1024ULL) {
        return std::to_string(bytes / (1024ULL * 1024ULL)) + " MB";
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.2f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    return std::string(buf);
}

static bool starts_with(const std::string& value, const char* prefix) {
    if (!prefix) return false;
    const std::size_t prefix_len = std::strlen(prefix);
    if (value.size() < prefix_len) return false;
    return std::memcmp(value.data(), prefix, prefix_len) == 0;
}

bool is_critical_partition_name(const std::string& name) {
    if (my_stricmp(name.c_str(), "splloader") == 0) return true;
    if (my_stricmp(name.c_str(), "super") == 0) return true;
    if (my_stricmp(name.c_str(), "metadata") == 0) return true;
    if (my_stricmp(name.c_str(), "sml") == 0) return true;
    if (my_stricmp(name.c_str(), "trustos") == 0) return true;
    if (my_stricmp(name.c_str(), "teecfg") == 0) return true;
    if (my_stricmp(name.c_str(), "recovery") == 0) return true;
    if (starts_with(name, "uboot")) return true;
    if (starts_with(name, "boot")) return true;   // boot / boot_a / boot_b
    if (starts_with(name, "vbmeta")) return true; // vbmeta / vbmeta_a / vbmeta_system
    if (starts_with(name, "dtbo")) return true;
    return false;
}

static std::uint64_t expected_backup_image_size(const std::string& partition_name,
                                                std::uint64_t partition_size) {
    if (partition_name.find("nv1") != std::string::npos && partition_size > 512ULL) {
        return partition_size - 512ULL;
    }
    return partition_size;
}

static int partition_image_priority_from_extension(std::string ext) {
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (ext == ".img") return 2;
    if (ext == ".bin") return 1;
    return 0;
}

static bool parse_partition_image_filename(const std::string& filename,
                                           std::string& partition_name,
                                           int* out_priority = nullptr) {
    const auto dot_pos = filename.find_last_of('.');
    if (dot_pos == std::string::npos || dot_pos == 0) {
        return false;
    }

    int priority = partition_image_priority_from_extension(filename.substr(dot_pos));
    if (priority <= 0) {
        return false;
    }

    partition_name = filename.substr(0, dot_pos);
    if (partition_name.empty()) {
        return false;
    }

    if (out_priority) {
        *out_priority = priority;
    }
    return true;
}

static std::map<std::string, BackupImageFileInfo>
scan_backup_image_files(const std::string& folder) {
    std::map<std::string, BackupImageFileInfo> files;

    std::error_code ec;
    const std::filesystem::path root(folder);
    if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
        DEG_LOG(E, "[backup-scan] invalid directory: %s", folder.c_str());
        return files;
    }

    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (ec) {
            DEG_LOG(E, "[backup-scan] directory iteration failed: %s", ec.message().c_str());
            break;
        }

        std::error_code file_ec;
        if (!entry.is_regular_file(file_ec)) {
            continue;
        }

        const auto filename = entry.path().filename().string();
        std::string partition_name;
        int priority = 0;
        if (!parse_partition_image_filename(filename, partition_name, &priority)) {
            continue;
        }

        const auto file_size = entry.file_size(file_ec);
        if (file_ec) {
            DEG_LOG(W, "[backup-scan] skip file size failed: %s", entry.path().string().c_str());
            continue;
        }

        BackupImageFileInfo info;
        info.path = entry.path().string();
        info.size = static_cast<std::uint64_t>(file_size);
        info.priority = priority;

        auto it = files.find(partition_name);
        if (it == files.end() || info.priority > it->second.priority) {
            if (it != files.end()) {
                DEG_LOG(W, "[backup-scan] duplicate image for %s, prefer %s over %s",
                        partition_name.c_str(), info.path.c_str(), it->second.path.c_str());
            }
            files[partition_name] = std::move(info);
        }
    }

    return files;
}

static bool collect_current_device_partitions_for_batch_ops(GtkWidgetHelper& helper,
                                                            std::vector<sfd::DevicePartitionInfo>& partitions) {
    GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
    partitions.clear();

    if (!io) {
        showErrorDialog(parent, _("Error"), _("Device context not ready."));
        return false;
    }

    if (!isCMethod) {
        if (io->part_count == 0) {
            showWarningDialog(parent, _("Warning"),
                            _("No partition table loaded on the current device."));
            return false;
        }
        partitions.reserve(static_cast<std::size_t>(io->part_count) + 1U);
        for (int i = 0; i < io->part_count; ++i) {
            sfd::DevicePartitionInfo info{};
            info.name = io->ptable[i].name;
            info.size = static_cast<std::uint64_t>(io->ptable[i].size);
            info.readable = true;
            info.writable = true;
            partitions.push_back(info);
        }
    } else {
        if (io->part_count_c == 0) {
            showErrorDialog(parent, _("Error"),
                            _("No partition table loaded on the current device."));
            return false;
        }
        partitions.reserve(static_cast<std::size_t>(io->part_count_c) + 1U);
        for (int i = 0; i < io->part_count_c; ++i) {
            sfd::DevicePartitionInfo info{};
            info.name = io->Cptable[i].name;
            info.size = static_cast<std::uint64_t>(io->Cptable[i].size);
            info.readable = true;
            info.writable = true;
            partitions.push_back(info);
        }
    }

    if (g_spl_size > 0) {
        bool has_splloader = false;
        for (const auto& p : partitions) {
            if (p.name == "splloader") {
                has_splloader = true;
                break;
            }
        }
        if (!has_splloader) {
            sfd::DevicePartitionInfo spl{};
            spl.name = "splloader";
            spl.size = g_spl_size;
            spl.readable = true;
            spl.writable = true;
            partitions.push_back(spl);
        }
    }

    return true;
}

static bool inspect_file_is_all_zero(const std::string& path,
                                     bool& out_all_zero,
                                     std::string& out_error) {
    out_all_zero = true;
    out_error.clear();

    EnhancedFile fi = oxfopen_enhanced(path.c_str(), "rb");
    if (!fi) {
        out_error = _("Failed to open image file.");
        return false;
    }

    std::vector<unsigned char> buffer(1024 * 1024);
    while (true) {
        const std::size_t nread = fi.read(buffer.data(), 1, buffer.size());
        if (nread == 0) {
            if (fi.error()) {
                out_error = _("Failed to read image file.");
                return false;
            }
            break;
        }
        for (std::size_t i = 0; i < nread; ++i) {
            if (buffer[i] != 0) {
                out_all_zero = false;
                return true;
            }
        }
    }

    return true;
}

static std::vector<BackupInspectionItem>
inspect_backup_folder(const std::string& folder,
                      const std::vector<sfd::DevicePartitionInfo>& partitions) {
    std::vector<BackupInspectionItem> result;
    auto image_files = scan_backup_image_files(folder);

    // 构建设备分区的小写映射，用于快速查找和纠正
    std::map<std::string, const sfd::DevicePartitionInfo*> partition_map_lower;
    for (const auto& part : partitions) {
        std::string lower = part.name;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        partition_map_lower[lower] = &part;
    }

    // 1. 检查每个设备分区
    for (const auto& part : partitions) {
        BackupInspectionItem item;
        item.part = part;
        item.expected_size = expected_backup_image_size(part.name, part.size);
        item.is_critical = is_critical_partition_name(part.name);

        // 查找镜像文件（忽略大小写）
        std::string part_lower = part.name;
        std::transform(part_lower.begin(), part_lower.end(), part_lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        auto file_it = std::find_if(image_files.begin(), image_files.end(),
            [&part_lower](const auto& kv) {
                std::string lower = kv.first;
                std::transform(lower.begin(), lower.end(), lower.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return lower == part_lower;
            });

        if (file_it == image_files.end()) {
            if (item.is_critical) {
                item.note = _("Critical partition backup not found in folder.");
                result.push_back(std::move(item));
            }
            continue;
        }

        item.matched = true;
        item.image_path = file_it->second.path;
        item.image_size = file_it->second.size;
        item.size_match = (item.image_size == item.expected_size);
        item.empty_file = (item.image_size == 0);

        bool all_zero = false;
        std::string error;
        if (inspect_file_is_all_zero(item.image_path, all_zero, error)) {
            item.all_zero = all_zero;
        } else {
            item.analysis_error = true;
            item.note = std::move(error);
        }

        if (item.note.empty()) {
            if (item.empty_file) {
                item.note = _("Image file is empty.");
            } else if (!item.size_match) {
                item.note = _("Image size does not match the current partition size.");
            }

            if (item.all_zero) {
                if (!item.note.empty()) {
                    item.note += " ";
                }
                item.note += _("All bytes are 0x00; this may be normal for an empty or erased partition.");
            }
        }

        result.push_back(std::move(item));
    }

    // 2. 处理多余的镜像文件（即设备表中没有匹配的分区）
    for (const auto& kv : image_files) {
        const std::string& image_name = kv.first;
        // 判断该镜像名称是否在设备表中（忽略大小写）
        std::string lower = image_name;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (partition_map_lower.find(lower) != partition_map_lower.end()) {
            continue;
        }

        BackupInspectionItem item;
        // 使用文件名的分区名作为虚拟分区（原始大小写）
        item.part.name = image_name;
        item.part.size = 0;
        item.part.readable = true;
        item.part.writable = true;
        item.image_path = kv.second.path;
        item.image_size = kv.second.size;
        item.matched = false;
        item.note = _("No matching partition exists on the current device.");
        result.push_back(std::move(item));
    }

    // 排序（与原逻辑一致）
    std::stable_sort(result.begin(), result.end(), [](const BackupInspectionItem& lhs,
                                                      const BackupInspectionItem& rhs) {
        auto rank = [](const BackupInspectionItem& item) {
            if (!item.matched && item.is_critical) return 0;
            if (!item.matched) return 1;
            if (item.empty_file) return 2;
            if (!item.size_match) return 3;
            if (item.analysis_error) return 4;
            if (item.all_zero) return 5;
            return 6;
        };

        const int lhs_rank = rank(lhs);
        const int rhs_rank = rank(rhs);
        if (lhs_rank != rhs_rank) {
            return lhs_rank < rhs_rank;
        }
        return lhs.part.name < rhs.part.name;
    });

    return result;
}

static std::string backup_inspection_status_text(const BackupInspectionItem& item) {
    if (!item.matched && item.is_critical) return _("Missing Critical");
    if (!item.matched) return _("Unmatched");
    if (item.empty_file) return _("Empty");
    if (!item.size_match) return _("Size Mismatch");
    if (item.analysis_error) return _("Unchecked");
    if (item.all_zero) return _("All Zero");
    return _("OK");
}

static int batch_write_priority(const BatchPartitionWriteItem& item) {
    if (item.part.name == "splloader") return -10;
    if (item.part.name == "super") return 10;
    if (item.part.name == "metadata") return 11;
    return 0;
}

static std::vector<BatchPartitionWriteItem>
scan_folder_and_match_partitions(const std::string& folder,
                                 const std::vector<sfd::DevicePartitionInfo>& partitions) {
    std::vector<BatchPartitionWriteItem> result;
    BatchPartitionWriteItem downloadnv_item;
    bool has_downloadnv = false;

    // 1. 扫描文件夹，获取所有镜像文件（已按优先级去重）
    auto image_files = scan_backup_image_files(folder);

    // 2. 构建设备分区表的忽略大小写映射（用于自动纠正名称）
    std::map<std::string, sfd::DevicePartitionInfo> partition_map; // key 为小写
    for (const auto& part : partitions) {
        std::string lower = part.name;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        partition_map[lower] = part; // 如有重复，保留最后一个（实际不会）
    }

    // 3. 遍历所有镜像文件，而不是遍历 partitions
    for (const auto& kv : image_files) {
        const std::string& image_part_name = kv.first;      // 从文件名解析出的分区名（原始大小写）
        const BackupImageFileInfo& file_info = kv.second;

        // 4. 尝试忽略大小写匹配设备分区表
        std::string lower_name = image_part_name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        BatchPartitionWriteItem item;
        auto map_it = partition_map.find(lower_name);
        if (map_it != partition_map.end()) {
            // 匹配成功：使用设备表中的正确大小写和分区大小
            item.part = map_it->second;
        } else {
            // 未匹配：创建虚拟分区项，仅保留文件名中的分区名（原始大小写），大小设为 0
            sfd::DevicePartitionInfo virtual_part;
            virtual_part.name = image_part_name;   // 保留原始大小写
            virtual_part.size = 0;                 // 未知，刷入时会使用镜像实际大小
            virtual_part.readable = true;
            virtual_part.writable = true;
            item.part = virtual_part;
        }

        // 5. 填充其它字段
        item.image_path = file_info.path;
        item.image_size = file_info.size;
        item.is_critical = is_critical_partition_name(item.part.name); // 使用纠正后的名称判断
        item.selected = true;

        DEG_LOG(I, "[restore-folder] matched partition=%s (from file), corrected=%s, path=%s size=%llu critical=%d",
                image_part_name.c_str(), item.part.name.c_str(), item.image_path.c_str(),
                static_cast<unsigned long long>(item.image_size), item.is_critical ? 1 : 0);

        // 6. 特殊处理 downloadnv 分区，置于最后
        if (item.part.name.find("downloadnv") != std::string::npos) {
            downloadnv_item = std::move(item);
            has_downloadnv = true;
        } else {
            result.push_back(std::move(item));
        }
    }

    // 7. 追加 downloadnv（如果存在）
    if (has_downloadnv) {
        result.push_back(std::move(downloadnv_item));
    }

    return result;
}

void on_button_clicked_list_write(GtkWidgetHelper helper) {
	GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
	std::string filename = showFileChooser(parent, true);
	std::string part_name = getSelectedPartitionName(helper);
	ensure_device_attached_or_exit(helper);
	if (filename.empty()) {
		showErrorDialog(parent, _(_(("Error"))), _("No partition list file selected!"));
		return;
	}
	if (io->part_count == 0 && io->part_count_c == 0) {
		showErrorDialog(parent, _(_(("Error"))), _("No partition table loaded, cannot write partition list!"));
		return;
	}
	EnhancedFile fi = oxfopen_enhanced(filename.c_str(), "r");
	if (!fi) {
		DEG_LOG(E, "File does not exist.\n");
		return;
	}
	fi.close();

	sfd::PartitionIoOptions opts;
	opts.partition_name = part_name;
	opts.file_path = filename;
	opts.block_size = GetEffectiveManualBlockSize();
	opts.force = false;

	LongTaskConfig cfg{
		// worker：在后台线程中执行分区写入
		[parent, helper, opts](std::atomic_bool& cancel_flag) {
			(void)cancel_flag;
			auto* svc = ensure_flash_service();
			sfd::FlashStatus st = svc->writePartitionFromFile(opts);
			gui_idle_call_wait_drag([parent, helper, st]() mutable {
				if (!st.success) {
					showErrorDialog(parent, _(_(("Error"))), st.message.c_str());
				} else {
					showInfoDialog(GTK_WINDOW(parent), _(_(("Completed"))), _("Partition write completed!"));
				}
			}, GTK_WINDOW(helper.getWidget("main_window")));
		},
		[helper]() mutable {
			helper.setLabelText(helper.getWidget("con"), "Writing partition");
		},
		[helper]() mutable {
			helper.setLabelText(helper.getWidget("con"), "Ready");
		}
	};

	run_long_task(cfg);

}


void on_button_clicked_list_force_write(GtkWidgetHelper helper) {
	GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
	std::string filename = showFileChooser(parent, true);
	std::string part_name = getSelectedPartitionName(helper);
	ensure_device_attached_or_exit(helper);
	if (filename.empty()) {
		showErrorDialog(parent, _(_(("Error"))), _("No partition list file selected!"));
		return;
	}
	if (io->part_count == 0 && io->part_count_c == 0) {
		showErrorDialog(parent, _(_(("Error"))), _("No partition table loaded, cannot write partition list!"));
		return;
	}
	EnhancedFile fi = oxfopen_enhanced(filename.c_str(), "r");
	if (!fi) {
		DEG_LOG(E, "File does not exist.\n");
		return;
	}
	fi.close();

	bool i_op = showConfirmDialog(parent, _(_(("Confirm"))), _("Force writing partitions may brick the device, do you want to continue?"));
	if (!i_op) return;

	sfd::PartitionIoOptions opts;
	opts.partition_name = part_name;
	opts.file_path = filename;
	opts.block_size = GetEffectiveManualBlockSize();
	opts.force = true;

	LongTaskConfig cfg{
		// worker：在后台线程中执行分区写入（强制）
		[parent, helper, opts](std::atomic_bool& cancel_flag) {
			(void)cancel_flag;
			auto* svc = ensure_flash_service();
			sfd::FlashStatus st = svc->writePartitionFromFile(opts);
			gui_idle_call_wait_drag([parent, helper, st]() mutable {
				if (!st.success) {
					showErrorDialog(parent, _(_(("Error"))), st.message.c_str());
				} else {
					showInfoDialog(GTK_WINDOW(parent), _(_(("Completed"))), _("Partition force write completed!"));
				}
			}, GTK_WINDOW(helper.getWidget("main_window")));
		},
		[helper]() mutable {
			helper.setLabelText(helper.getWidget("con"), "Force Writing partition");
		},
		[helper]() mutable {
			helper.setLabelText(helper.getWidget("con"), "Ready");
		}
	};

	run_long_task(cfg);

}


void on_button_clicked_list_erase(GtkWidgetHelper helper) {
	GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
	std::string part_name = getSelectedPartitionName(helper);
	ensure_device_attached_or_exit(helper);

	LongTaskConfig cfg{
		[parent, helper, part_name](std::atomic_bool& cancel_flag) {
			(void)cancel_flag;
			auto* svc = ensure_flash_service();
			sfd::FlashStatus st = svc->erasePartition(part_name);
			gui_idle_call_wait_drag([parent, helper, st]() mutable {
				if (!st.success) {
					showErrorDialog(parent, _(_(("Error"))), st.message.c_str());
				} else {
					showInfoDialog(GTK_WINDOW(parent), _(_(("Completed"))), _("Partition erase completed!"));
				}
			}, GTK_WINDOW(helper.getWidget("main_window")));
		},
		[helper]() mutable {
			helper.setLabelText(helper.getWidget("con"), "Erase partition");
		},
		[helper]() mutable {
			helper.setLabelText(helper.getWidget("con"), "Ready");
		}
	};

	run_long_task(cfg);

}

void on_button_clicked_erase_all_partitions(GtkWidgetHelper helper) {
	confirm_erase_all_partitions(helper);
}

void confirm_erase_all_partitions(GtkWidgetHelper helper) {
	ensure_device_attached_or_exit(helper);
	gui_idle_call_with_callback(
		[helper]() -> bool {
			return showConfirmDialog(
				GTK_WINDOW(helper.getWidget("main_window")),
				_(("Confirm")),
				_(("This operation will erase ALL partitions on the current device.\nAll data on all partitions will be lost and CANNOT be recovered.\nDo you want to continue?"))
			);
		},
		[helper](bool result) mutable {
			if (!result) {
				DEG_LOG(I, "Erase all partitions cancelled by user");
				return;
			}

			helper.setLabelText(helper.getWidget("con"), "Erase all partitions");

			LongTaskConfig cfg{
				[helper](std::atomic_bool& cancel_flag) mutable {
					(void)cancel_flag;
					auto* svc = ensure_flash_service();
					sfd::FlashStatus st = svc->eraseAllPartitions();
					gui_idle_call_wait_drag([helper, st]() mutable {
						GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
						if (!st.success) {
							showErrorDialog(parent, _(_(("Error"))), st.message.c_str());
						} else {
							showInfoDialog(parent, _(_(("Completed"))), _("Erase ALL partitions completed!"));
						}
						helper.setLabelText(helper.getWidget("con"), "Ready");
					}, GTK_WINDOW(helper.getWidget("main_window")));
				},
				[helper]() mutable {
					helper.setLabelText(helper.getWidget("con"), "Erase all partitions");
				},
				[helper]() mutable {
					helper.setLabelText(helper.getWidget("con"), "Ready");
				}
			};

			run_long_task(cfg);
		},
		GTK_WINDOW(helper.getWidget("main_window"))
	);
}

void on_button_clicked_modify_part(GtkWidgetHelper helper) {
	if (io->part_count == 0 && io->part_count_c == 0) {
		showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("No partition table loaded, cannot modify partition size!"));
		return;
	}
	GtkWindow* window = GTK_WINDOW(helper.getWidget("main_window"));
	std::string part_name = getSelectedPartitionName(helper);
	ensure_device_attached_or_exit(helper);
	std::string secondPartName = helper.getEntryText(helper.getWidget("modify_second_part"));
	std::string newSizeStr = helper.getEntryText(helper.getWidget("modify_new_size"));
	if (secondPartName.empty() || newSizeStr.empty()) {
		showErrorDialog(window, _(_(_("Error"))), _("Please fill in complete modification info!"));
		return;
	}
	int newSizeMB = atoi(newSizeStr.c_str());
	if (newSizeMB <= 0) {
		showErrorDialog(window, _(_(_("Error"))), _("Please enter a valid new size!"));
		return;
	}
	const bool use_cmethod = (isCMethod != 0);
	bool i_is = true;
	if (use_cmethod) {
		i_is = showConfirmDialog(window, _(_(_("Warning"))), _("Currently in compatibility-method-PartList mode, modifying partition may brick the device!"));
	}
	if (!i_is) {
		set_partition_modify_busy(helper, false);
		return;
	}
	set_partition_modify_busy(helper, true);
	std::thread([secondPartName, newSizeMB, window, helper, part_name, use_cmethod]() {
		int i_part = 0;
		int i_se_part = 0;
		if (!use_cmethod) {
			for (i_part = 0; i_part < io->part_count; i_part++) {
				if (!strcmp(part_name.c_str(), (*(io->ptable + i_part)).name)) {
					break;
				}
			}
			if (i_part == io->part_count) {
				DEG_LOG(E, "Partition not exist\n");
				gui_idle_call_wait_drag([window, helper]() {
					showErrorDialog(window, _(_(_("Error"))), _("Partition does not exist!"));
					set_partition_modify_busy(helper, false);
				},GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			for (i_se_part = 0; i_se_part < io->part_count; i_se_part++) {
				if (!strcmp(secondPartName.c_str(), (*(io->ptable + i_se_part)).name)) {
					break;
				}
			}
			if (i_se_part == io->part_count) {
				DEG_LOG(E, "Second partition not exist\n");
				gui_idle_call_wait_drag([window, helper]() {
					showErrorDialog(window, _(_(_("Error"))), _("Second partition does not exist!"));
					set_partition_modify_busy(helper, false);
				},GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			long long k = (*(io->ptable + i_part)).size << 20;
			(*(io->ptable + i_part)).size = (long long)newSizeMB << 20;
			(*(io->ptable + i_se_part)).size = (*(io->ptable + i_se_part)).size + k - ((long long)newSizeMB << 20);
			// 创建根节点
			auto root = std::make_shared<XmlNode>("Partitions");

			// 遍历分区并添加子节点
			for (int i = 0; i < io->part_count; i++) {
				auto partitionNode = std::make_shared<XmlNode>("Partition");
				
				// 设置 id 属性
				partitionNode->setAttribute("id", (*(io->ptable + i)).name);
				
				// 设置 size 属性
				if (i + 1 == io->part_count) {
					partitionNode->setAttribute("size", "0xffffffff");  // ~0 通常等于 0xffffffff
				} else {
					char sizeStr[32];
					snprintf(sizeStr, sizeof(sizeStr), "%lld", ((*(io->ptable + i)).size >> 20));
					partitionNode->setAttribute("size", sizeStr);
				}
				
				root->addChild(partitionNode);
			}

			// 保存到文件
			if (!root->saveXmlFile("partition_temp.xml")) {
				ERR_EXIT("Failed to save XML file\n");
			}
			uint8_t* buf = io->temp_buf;
			int n = scan_xml_partitions(io, "partition_temp.xml", buf, 0xffff);
			if (n <= 0) {
				DEG_LOG(E, "Failed to parse modified partition table\n");
				gui_idle_call_wait_drag([window, helper]() {
					showErrorDialog(window, _(_(_("Error"))), _("Failed to parse modified partition table!"));
					set_partition_modify_busy(helper, false);
				},GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			encode_msg_nocpy(io, BSL_CMD_REPARTITION, n * 0x4c);
			if (!send_and_check(io)) g_app_state.flash.gpt_failed = 0;
		}
		else {
			for (i_part = 0; i_part < io->part_count_c; i_part++) {
				if (!strcmp(part_name.c_str(), (*(io->Cptable + i_part)).name)) {
					break;
				}
			}
			if (i_part == io->part_count_c) {
				DEG_LOG(E, "Partition not exist\n");
				gui_idle_call_wait_drag([window, helper]() {
					showErrorDialog(window, _(_(_("Error"))), _("Partition does not exist!"));
					set_partition_modify_busy(helper, false);
				},GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			for (i_se_part = 0; i_se_part < io->part_count_c; i_se_part++) {
				if (!strcmp(secondPartName.c_str(), (*(io->Cptable + i_se_part)).name)) {
					break;
				}
			}
			if (i_se_part == io->part_count_c) {
				DEG_LOG(E, "Second partition not exist\n");
				gui_idle_call_wait_drag([window, helper]() {
					showErrorDialog(window, _(_(_("Error"))), _("Second partition does not exist!"));
					set_partition_modify_busy(helper, false);
				},GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			long long k = (*(io->Cptable + i_part)).size << 20;
			(*(io->Cptable + i_part)).size = (long long)newSizeMB << 20;
			(*(io->Cptable + i_se_part)).size = (*(io->Cptable + i_se_part)).size + k - ((long long)newSizeMB << 20);
			auto root = std::make_shared<XmlNode>("Partitions");

			for (int i = 0; i < io->part_count_c; i++) {
				auto partitionNode = std::make_shared<XmlNode>("Partition");
				partitionNode->setAttribute("id", (*(io->Cptable + i)).name);
				
				if (i + 1 == io->part_count_c) {
					partitionNode->setAttribute("size", "0xffffffff");
				} else {
					char sizeStr[32];
					snprintf(sizeStr, sizeof(sizeStr), "%lld", 
							((*(io->Cptable + i)).size >> 20));
					partitionNode->setAttribute("size", sizeStr);
				}
				
				root->addChild(partitionNode);
			}

			if (!root->saveXmlFile("partition_temp.xml")) {
				ERR_EXIT("Failed to save XML file\n");
			}
			uint8_t* buf = io->temp_buf;
			int n = scan_xml_partitions(io, "partition_temp.xml", buf, 0xffff);
			if (n <= 0) {
				DEG_LOG(E, "Failed to parse modified partition table\n");
				gui_idle_call_wait_drag([window, helper]() {
					showErrorDialog(window, _(_(_("Error"))), _("Failed to parse modified partition table!"));
					set_partition_modify_busy(helper, false);
				},GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			encode_msg_nocpy(io, BSL_CMD_REPARTITION, n * 0x4c);
			if (!send_and_check(io)) g_app_state.flash.gpt_failed = 0;
		}

		// 耗时尺寸探测放在后台线程，避免阻塞 GTK 主线程。
		update_partition_size_primary(io);
		auto partitions = build_primary_partition_view();
		if (use_cmethod) {
			delete[] io->Cptable;
			io->Cptable = nullptr;
			io->part_count_c = 0;
			isCMethod = 0;
		}

		gui_idle_call_wait_drag([window, helper, partitions = std::move(partitions)]() mutable {
			showInfoDialog(window, _(_(_("Completed"))), _("Partition modification completed!"));
			populatePartitionList(helper, partitions);
			set_partition_modify_busy(helper, false);
		}, window);
	}).detach();
}

void on_button_clicked_xml_get(GtkWidgetHelper helper) {
    GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));

	ensure_device_attached_or_exit(helper);
	std::string filename = showFileChooser(parent, true);
	if(filename.empty()){return;}
	uint8_t* buf = io->temp_buf;
	int n = scan_xml_partitions(io, filename.c_str(), buf, 0xffff);
	if(n <= 0) return;
	for (int i = 0; i < io->part_count; i++)
	{
		if (strcmp(io->ptable[i].name, "userdata") == 0) {
			io->ptable[i].size = check_partition(io, "userdata", 1);
			break;
		}
	}
	std::vector<sfd::DevicePartitionInfo> partitions;
	partitions.reserve(io->part_count);

	for (int i = 0; i < io->part_count; i++) {
		sfd::DevicePartitionInfo info{};
		info.name = io->ptable[i].name;
		info.size = (std::uint64_t)io->ptable[i].size;
		info.readable = true;
		info.writable = true;
		partitions.push_back(info);
	}
	populatePartitionList(helper, partitions);
    if(isCMethod){
		delete[] io->Cptable;
		io->Cptable = nullptr;
		io->part_count_c = 0;
		isCMethod = 0;
	}
}

void on_button_clicked_modify_new_part(GtkWidgetHelper helper) {
	if (io->part_count == 0 && io->part_count_c == 0) {
		showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("No partition table loaded, cannot modify partition size!"));
		return;
	}
	GtkWindow* window = GTK_WINDOW(helper.getWidget("main_window"));
	ensure_device_attached_or_exit(helper);
	std::string newPartName = helper.getEntryText(helper.getWidget("new_part"));
	std::string newSizeText = helper.getEntryText(helper.getWidget("modify_add_size"));
	std::string beforePart = helper.getEntryText(helper.getWidget("before_new_part"));
	if(newPartName.empty()){
		showErrorDialog(window, _(_(_("Error"))), _("Please fill in complete modification info!"));
		return;
	}
	long long newPartSize = strtoll(newSizeText.c_str(), nullptr, 0);
	if(newPartSize <= 0){
		showErrorDialog(window, _(_(_("Error"))), _("Please enter a valid new size!"));
		return;
	}
	const bool use_cmethod = (isCMethod != 0);
	bool i_is = true;
	if (use_cmethod) {
		i_is = showConfirmDialog(window, _(_(_("Warning"))), _("Currently in compatibility-method-PartList mode, modifying partition may brick the device!"));
	}
	if (!i_is) {
		set_partition_modify_busy(helper, false);
		return;
	}
	set_partition_modify_busy(helper, true);

		std::thread([window, newPartName, helper, newPartSize, beforePart, use_cmethod]() mutable {
			if(!use_cmethod) {
				partition_t* ptable = NEWN partition_t[128];
				if (ptable == nullptr) {
					gui_idle_call_wait_drag([window, helper]() {
						showErrorDialog(window, _(_(_("Error"))), _("Memory allocation failed!"));
						set_partition_modify_busy(helper, false);
					}, GTK_WINDOW(helper.getWidget("main_window")));
					return;
				}

			for (int i = 0; i < io->part_count; i++) {
				if (strcmp(io->ptable[i].name, newPartName.c_str()) == 0) {
					DEG_LOG(W, "Partition %s already exists", newPartName.c_str());
					gui_idle_call_wait_drag([window, helper]() {
						showErrorDialog(window, _(_(_("Error"))), _("Partition already exists!"));
						set_partition_modify_busy(helper, false);
					}, GTK_WINDOW(helper.getWidget("main_window")));
					return;
				}
			}
			int i_o = 0;
			for (i_o=0;i_o < io->part_count;i_o++){
				if(strcmp(beforePart.c_str(),(*(io->ptable + i_o)).name) == 0){
					break;
				}
			}
			if(i_o == io->part_count){
				gui_idle_call_wait_drag([window, helper]() {
					showErrorDialog(window, _(_(_("Error"))), _("Partition after does not exist!"));
					set_partition_modify_busy(helper, false);
				}, GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			int i_op = 0;
			for (i_op = 0; i_op < io->part_count; i_op++) {
				if(strcmp(beforePart.c_str(),(*(io->ptable + i_op)).name) != 0){
					snprintf(ptable[i_op].name, sizeof(ptable[i_op].name), "%s", io->ptable[i_op].name);
					ptable[i_op].size = io->ptable[i_op].size;
				}
				else{
					break;
				}
			}
			snprintf(ptable[i_op].name, sizeof(ptable[i_op].name), "%s", newPartName.c_str());
			ptable[i_op].size = newPartSize << 20;
			for (; i_op < io->part_count; i_op++) {
				snprintf(ptable[i_op + 1].name, sizeof(ptable[i_op + 1].name), "%s", io->ptable[i_op].name);
				ptable[i_op + 1].size = io->ptable[i_op].size;
			}
			partition_t* old_ptable = io->ptable;
			io->ptable = ptable;
			io->part_count++;
			if (old_ptable && old_ptable != ptable) {
				delete[] old_ptable;
			}
			auto root = std::make_shared<XmlNode>("Partitions");

			for (int i = 0; i < io->part_count; i++) {
				auto partitionNode = std::make_shared<XmlNode>("Partition");
				partitionNode->setAttribute("id", (*(io->ptable + i)).name);
				
				if (i + 1 == io->part_count) {
					partitionNode->setAttribute("size", "0xffffffff");
				} else {
					char sizeStr[32];
					snprintf(sizeStr, sizeof(sizeStr), "%lld", ((*(io->ptable + i)).size >> 20));
					partitionNode->setAttribute("size", sizeStr);
				}
				
				root->addChild(partitionNode);
			}

			// 保存为格式化的 XML（带换行和缩进）
			std::ofstream file("partition_temp.xml");
			if (!file.is_open()) {
				ERR_EXIT("Failed to open file\n");
			}
			file << root->toXml();
			uint8_t* buf = io->temp_buf;
			int n = scan_xml_partitions(io, "partition_temp.xml", buf, 0xffff);
			if (n <= 0) {
				DEG_LOG(E, "Failed to parse modified partition table\n");
				gui_idle_call_wait_drag([window, helper]() {
					showErrorDialog(window, _(_(_("Error"))), _("Failed to parse modified partition table!"));
					set_partition_modify_busy(helper, false);
				},GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			encode_msg_nocpy(io, BSL_CMD_REPARTITION, n * 0x4c);
			if (!send_and_check(io)) g_app_state.flash.gpt_failed = 0;
		}
			else {
				partition_t* ptable = NEWN partition_t[128];
				if (ptable == nullptr) {
					gui_idle_call_wait_drag([window, helper]() {
						showErrorDialog(window, _(_(_("Error"))), _("Memory allocation failed!"));
						set_partition_modify_busy(helper, false);
					}, GTK_WINDOW(helper.getWidget("main_window")));
					return;
				}
			for (int i = 0; i < io->part_count_c; i++) {
				if (strcmp(io->Cptable[i].name, newPartName.c_str()) == 0) {
					DEG_LOG(W, "Partition %s already exists", newPartName.c_str());
					gui_idle_call_wait_drag([window, helper]() {
						showErrorDialog(window, _(_(_("Error"))), _("Partition already exists!"));
						set_partition_modify_busy(helper, false);
					}, GTK_WINDOW(helper.getWidget("main_window")));
					return;
				}
			}
			int i_o = 0;
			for (i_o=0;i_o < io->part_count_c;i_o++){
				if(strcmp(beforePart.c_str(),(*(io->Cptable + i_o)).name) == 0){
					break;
				}
			}
			if(i_o == io->part_count_c){
				gui_idle_call_wait_drag([window, helper]() {
					showErrorDialog(window, _(_(_("Error"))), _("Partition after does not exist!"));
					set_partition_modify_busy(helper, false);
				}, GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			int i_op = 0;
			for (i_op = 0; i_op < io->part_count_c; i_op++) {
				if(strcmp(beforePart.c_str(),(*(io->Cptable + i_op)).name) != 0){
					snprintf(ptable[i_op].name, sizeof(ptable[i_op].name), "%s", io->Cptable[i_op].name);
					ptable[i_op].size = io->Cptable[i_op].size;
				}
				else{
					break;
				}
			}
			snprintf(ptable[i_op].name, sizeof(ptable[i_op].name), "%s", newPartName.c_str());
			ptable[i_op].size = newPartSize << 20;
			for (; i_op < io->part_count_c; i_op++) {
				snprintf(ptable[i_op + 1].name, sizeof(ptable[i_op + 1].name), "%s", io->Cptable[i_op].name);
				ptable[i_op + 1].size = io->Cptable[i_op].size;
			}
			partition_t* old_cptable = io->Cptable;
			io->Cptable = ptable;
			io->part_count_c++;
			if (old_cptable && old_cptable != ptable) {
				delete[] old_cptable;
			}
			auto root = std::make_shared<XmlNode>("Partitions");

			for (int i = 0; i < io->part_count; i++) {
				auto partitionNode = std::make_shared<XmlNode>("Partition");
				partitionNode->setAttribute("id", (*(io->ptable + i)).name);
				
				if (i + 1 == io->part_count) {
					partitionNode->setAttribute("size", "0xffffffff");
				} else {
					char sizeStr[32];
					snprintf(sizeStr, sizeof(sizeStr), "%lld", ((*(io->ptable + i)).size >> 20));
					partitionNode->setAttribute("size", sizeStr);
				}
				
				root->addChild(partitionNode);
			}

			// 保存为格式化的 XML（带换行和缩进）
			std::ofstream file("partition_temp.xml");
			if (!file.is_open()) {
				ERR_EXIT("Failed to open file\n");
			}
			file << root->toXml();
			uint8_t* buf = io->temp_buf;
			int n = scan_xml_partitions(io, "partition_temp.xml", buf, 0xffff);
			if (n <= 0) {
				DEG_LOG(E, "Failed to parse modified partition table\n");
				gui_idle_call_wait_drag([window, helper]() {
					showErrorDialog(window, _(_(_("Error"))), _("Failed to parse modified partition table!"));
					set_partition_modify_busy(helper, false);
				},GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			encode_msg_nocpy(io, BSL_CMD_REPARTITION, n * 0x4c);
			if (!send_and_check(io)) g_app_state.flash.gpt_failed = 0;
		}

		update_partition_size_primary(io);
		auto partitions = build_primary_partition_view();
		if (use_cmethod) {
			delete[] io->Cptable;
			io->Cptable = nullptr;
			io->part_count_c = 0;
			isCMethod = 0;
		}

		gui_idle_call_wait_drag([window, helper, partitions = std::move(partitions)]() mutable {
			showInfoDialog(window, _(_(_("Completed"))), _("Partition modification completed!"));
			populatePartitionList(helper, partitions);
			set_partition_modify_busy(helper, false);
		}, window);
	}).detach();
}

void on_button_clicked_modify_rm_part(GtkWidgetHelper helper) {
	if (io->part_count == 0 && io->part_count_c == 0) {
		showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("No partition table loaded, cannot modify partition size!"));
		return;
	}
	GtkWindow* window = GTK_WINDOW(helper.getWidget("main_window"));
	std::string part_name = getSelectedPartitionName(helper);
	ensure_device_attached_or_exit(helper);
	const bool use_cmethod = (isCMethod != 0);
	bool i_is = true;
	if (use_cmethod) {
		i_is = showConfirmDialog(window, _(_(_("Warning"))), _("Currently in compatibility-method-PartList mode, modifying partition may brick the device!"));
	}
	if (!i_is) {
		set_partition_modify_busy(helper, false);
		return;
	}
	set_partition_modify_busy(helper, true);

	std::thread([part_name, helper, window, use_cmethod]() mutable {
		int i = 0;
		if (!use_cmethod) {
			for (i = 0; i < io->part_count; i++) {
				if (strcmp((*(io->ptable + i)).name, part_name.c_str()) == 0) {
					break;
				}
			}
			if (i == io->part_count) {
				DEG_LOG(E, "Partition not exist\n");
				gui_idle_call_wait_drag([window, helper]() {
					showErrorDialog(window, _(_(_("Error"))), _("Second partition does not exist!"));
					set_partition_modify_busy(helper, false);
				}, GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			partition_t* ptable = NEWN partition_t[128];
			if (ptable == nullptr) {
				gui_idle_call_wait_drag([window, helper]() {
					showErrorDialog(window, _(_(_("Error"))), _("Memory allocation failed!"));
					set_partition_modify_busy(helper, false);
				}, GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			int new_index = 0;
			for (int j = 0; j < io->part_count; j++) {
				// 使用 strcmp 比较字符串内容
				if (strcmp(io->ptable[j].name, part_name.c_str()) != 0) {
					// 复制不需要删除的分区到新表
					snprintf(ptable[new_index].name, sizeof(ptable[new_index].name), "%s", io->ptable[j].name);
					ptable[new_index].size = io->ptable[j].size;
					new_index++;
				}
			}

			// 更新 io 结构
			io->part_count--;
			// 注意：需要释放原来的 ptable 内存
			delete[] (io->ptable);
			io->ptable = ptable;
			auto root = std::make_shared<XmlNode>("Partitions");

			for (int i = 0; i < io->part_count; i++) {
				auto partitionNode = std::make_shared<XmlNode>("Partition");
				partitionNode->setAttribute("id", (*(io->ptable + i)).name);
				
				if (i + 1 == io->part_count) {
					partitionNode->setAttribute("size", "0xffffffff");
				} else {
					char sizeStr[32];
					snprintf(sizeStr, sizeof(sizeStr), "%lld", ((*(io->ptable + i)).size >> 20));
					partitionNode->setAttribute("size", sizeStr);
				}
				
				root->addChild(partitionNode);
			}

			// 保存为格式化的 XML（带换行和缩进）
			std::ofstream file("partition_temp.xml");
			if (!file.is_open()) {
				ERR_EXIT("Failed to open file\n");
			}
			file << root->toXml(); 
			uint8_t* buf = io->temp_buf;
			int n = scan_xml_partitions(io, "partition_temp.xml", buf, 0xffff);
			if (n <= 0) {
				DEG_LOG(E, "Failed to parse modified partition table\n");
				gui_idle_call_wait_drag([window, helper]() {
					showErrorDialog(window, _(_(_("Error"))), _("Failed to parse modified partition table!"));
					set_partition_modify_busy(helper, false);
				}, GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			encode_msg_nocpy(io, BSL_CMD_REPARTITION, n * 0x4c);
			if (!send_and_check(io)) g_app_state.flash.gpt_failed = 0;
		} else {

			for (i = 0; i < io->part_count_c; i++) {
				if (strcmp((*(io->Cptable + i)).name, part_name.c_str()) == 0) {
					break;
				}
			}
			if (i == io->part_count_c) {
				DEG_LOG(E, "Partition not exist\n");
				gui_idle_call_wait_drag([window, helper]() {
					showErrorDialog(window, _(_(_("Error"))), _("Second partition does not exist!"));
					set_partition_modify_busy(helper, false);
				}, GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			partition_t* ptable = NEWN partition_t[128];
			if (ptable == nullptr) {
				gui_idle_call_wait_drag([window, helper]() {
					showErrorDialog(window, _(_(_("Error"))), _("Memory allocation failed!"));
					set_partition_modify_busy(helper, false);
				}, GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			int new_index = 0;
			for (int j = 0; j < io->part_count_c; j++) {
				// 使用 strcmp 比较字符串内容
				if (strcmp(io->Cptable[j].name, part_name.c_str()) != 0) {
					// 复制不需要删除的分区到新表
					snprintf(ptable[new_index].name, sizeof(ptable[new_index].name), "%s", io->Cptable[j].name);
					ptable[new_index].size = io->Cptable[j].size;
					new_index++;
				}
			}

			// 更新 io 结构
			io->part_count_c--;
			// 注意：需要释放原来的 ptable 内存
			delete[] (io->Cptable);
			io->Cptable = ptable;
			auto root = std::make_shared<XmlNode>("Partitions");

			for (int i = 0; i < io->part_count; i++) {
				auto partitionNode = std::make_shared<XmlNode>("Partition");
				partitionNode->setAttribute("id", (*(io->ptable + i)).name);
				
				if (i + 1 == io->part_count) {
					partitionNode->setAttribute("size", "0xffffffff");
				} else {
					char sizeStr[32];
					snprintf(sizeStr, sizeof(sizeStr), "%lld", ((*(io->ptable + i)).size >> 20));
					partitionNode->setAttribute("size", sizeStr);
				}
				
				root->addChild(partitionNode);
			}

			// 保存为格式化的 XML（带换行和缩进）
			std::ofstream file("partition_temp.xml");
			if (!file.is_open()) {
				ERR_EXIT("Failed to open file\n");
			}
			file << root->toXml();
			uint8_t* buf = io->temp_buf;
			int n = scan_xml_partitions(io, "partition_temp.xml", buf, 0xffff);
			if (n <= 0) {
				DEG_LOG(E, "Failed to parse modified partition table\n");
				gui_idle_call_wait_drag([window, helper]() {
					showErrorDialog(window, _(_(_("Error"))), _("Failed to parse modified partition table!"));
					set_partition_modify_busy(helper, false);
				}, GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			encode_msg_nocpy(io, BSL_CMD_REPARTITION, n * 0x4c);
			if (!send_and_check(io)) g_app_state.flash.gpt_failed = 0;
		}

		update_partition_size_primary(io);
		auto partitions = build_primary_partition_view();
		if (use_cmethod) {
			delete[] io->Cptable;
			io->Cptable = nullptr;
			io->part_count_c = 0;
			isCMethod = 0;
		}

		gui_idle_call_wait_drag([window, helper, partitions = std::move(partitions)]() mutable {
			showInfoDialog(window, _(_(_("Completed"))), _("Partition modification completed!"));
			populatePartitionList(helper, partitions);
			set_partition_modify_busy(helper, false);
		}, window);
	}).detach();
}

void on_button_clicked_modify_ren_part(GtkWidgetHelper helper) {
	if (io->part_count == 0 && io->part_count_c == 0) {
		showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("No partition table loaded, cannot modify partition size!"));
		return;
	}
	GtkWindow* window = GTK_WINDOW(helper.getWidget("main_window"));
	std::string part_name = getSelectedPartitionName(helper);
	std::string new_part_name = helper.getEntryText(helper.getWidget("modify_rename_part"));
	ensure_device_attached_or_exit(helper);
	if(part_name.empty() || new_part_name.empty()){
		showErrorDialog(window, _(_(_("Error"))), _("Please fill in complete modification info!"));
		return;
	}
	const bool use_cmethod = (isCMethod != 0);
	bool i_is = true;
	if (use_cmethod) {
		i_is = showConfirmDialog(window, _(_(_("Warning"))), _("Currently in compatibility-method-PartList mode, modifying partition may brick the device!"));
	}
	if (!i_is) {
		set_partition_modify_busy(helper, false);
		return;
	}
	set_partition_modify_busy(helper, true);

	std::thread([part_name, new_part_name, helper, window, use_cmethod]() mutable {
		int i = 0;
		if(!use_cmethod){
			for(i = 0;i < io->part_count; i++) {
				if(strcmp((*(io->ptable + i)).name, part_name.c_str()) == 0){
					break;
				}
			}
			if(i == io->part_count){
				DEG_LOG(E, "Partition not exist\n");
				gui_idle_call_wait_drag([window, helper]() {
					showErrorDialog(window, _(_(_("Error"))), _("Second partition does not exist!"));
					set_partition_modify_busy(helper, false);
				},GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}

			snprintf(io->ptable[i].name, sizeof(io->ptable[i].name), "%s", new_part_name.c_str());

			auto root = std::make_shared<XmlNode>("Partitions");

			for (int i = 0; i < io->part_count; i++) {
				auto partitionNode = std::make_shared<XmlNode>("Partition");
				partitionNode->setAttribute("id", (*(io->ptable + i)).name);
				
				if (i + 1 == io->part_count) {
					partitionNode->setAttribute("size", "0xffffffff");
				} else {
					char sizeStr[32];
					snprintf(sizeStr, sizeof(sizeStr), "%lld", ((*(io->ptable + i)).size >> 20));
					partitionNode->setAttribute("size", sizeStr);
				}
				
				root->addChild(partitionNode);
			}

			// 保存为格式化的 XML（带换行和缩进）
			std::ofstream file("partition_temp.xml");
			if (!file.is_open()) {
				ERR_EXIT("Failed to open file\n");
			}
			file << root->toXml();
			uint8_t* buf = io->temp_buf;
			int n = scan_xml_partitions(io, "partition_temp.xml", buf, 0xffff);
			if (n <= 0) {
				DEG_LOG(E, "Failed to parse modified partition table\n");
				gui_idle_call_wait_drag([window, helper]() {
					showErrorDialog(window, _(_(_("Error"))), _("Failed to parse modified partition table!"));
					set_partition_modify_busy(helper, false);
				},GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			encode_msg_nocpy(io, BSL_CMD_REPARTITION, n * 0x4c);
			if (!send_and_check(io)) g_app_state.flash.gpt_failed = 0;
		}
		else{


			for(i = 0;i < io->part_count_c; i++) {
				if(strcmp((*(io->Cptable + i)).name, part_name.c_str()) == 0){
					break;
				}
			}
			if(i == io->part_count_c){
				DEG_LOG(E, "Partition not exist\n");
				gui_idle_call_wait_drag([window, helper]() {
					showErrorDialog(window, _(_(_("Error"))), _("Second partition does not exist!"));
					set_partition_modify_busy(helper, false);
				},GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}

			snprintf(io->Cptable[i].name, sizeof(io->Cptable[i].name), "%s", new_part_name.c_str());

			auto root = std::make_shared<XmlNode>("Partitions");

			for (int i = 0; i < io->part_count; i++) {
				auto partitionNode = std::make_shared<XmlNode>("Partition");
				partitionNode->setAttribute("id", (*(io->ptable + i)).name);
				
				if (i + 1 == io->part_count) {
					partitionNode->setAttribute("size", "0xffffffff");
				} else {
					char sizeStr[32];
					snprintf(sizeStr, sizeof(sizeStr), "%lld", ((*(io->ptable + i)).size >> 20));
					partitionNode->setAttribute("size", sizeStr);
				}
				
				root->addChild(partitionNode);
			}

			// 保存为格式化的 XML（带换行和缩进）
			std::ofstream file("partition_temp.xml");
			if (!file.is_open()) {
				ERR_EXIT("Failed to open file\n");
			}
			file << root->toXml();
			uint8_t* buf = io->temp_buf;
			int n = scan_xml_partitions(io, "partition_temp.xml", buf, 0xffff);
			if (n <= 0) {
				DEG_LOG(E, "Failed to parse modified partition table\n");
				gui_idle_call_wait_drag([window, helper]() {
					showErrorDialog(window, _(_(_("Error"))), _("Failed to parse modified partition table!"));
					set_partition_modify_busy(helper, false);
				},GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			encode_msg_nocpy(io, BSL_CMD_REPARTITION, n * 0x4c);
			if (!send_and_check(io)) g_app_state.flash.gpt_failed = 0;
		}
		auto partitions = build_primary_partition_view();
		if (use_cmethod) {
			delete[] io->Cptable;
			io->Cptable = nullptr;
			io->part_count_c = 0;
			isCMethod = 0;
		}

		gui_idle_call_wait_drag([window, helper, partitions = std::move(partitions)]() mutable {
			showInfoDialog(window, _(_(_("Completed"))), _("Partition modification completed!"));
			populatePartitionList(helper, partitions);
			set_partition_modify_busy(helper, false);
		}, window);
	}).detach();
}

void on_button_clicked_list_cancel(GtkWidgetHelper helper) {
	ensure_device_attached_or_exit(helper);
	signal_handler(0);
	showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Tips"), _("Current partition operation cancelled!"));
}

void on_button_clicked_export_part_xml(GtkWidgetHelper helper) {
	ensure_device_attached_or_exit(helper);
	GtkWidget* parent = helper.getWidget("main_window");
	std::string savePath = showSaveFileDialog(GTK_WINDOW(parent), "partition_table.xml", { {_("XML files (*.xml)"), "*.xml"} });
	if (savePath.empty()) {
		showErrorDialog(GTK_WINDOW(parent), _(_(_(("Error")))), _("No save path selected!"));
		return;
	}

	auto* svc = ensure_flash_service();
	sfd::FlashStatus st = svc->exportPartitionTableToXml(savePath);
	if (!st.success) {
		DEG_LOG(E, "exportPartitionTableToXml failed: %s", st.message.c_str());
		showErrorDialog(GTK_WINDOW(parent), _(_(_(("Error")))), st.message.c_str());
		return;
	}

	showInfoDialog(GTK_WINDOW(parent), _(_(_(("Completed")))), _("Partition table export completed!"));
}

#if defined(__APPLE__)
std::string BuildBackupRootDirForGuiBackup() {
    if (savepath[0]) {
        char time_buf[32];
        std::time_t now = std::time(nullptr);
        std::tm tm_now{};
        localtime_r(&now, &tm_now);
        std::strftime(time_buf, sizeof(time_buf), "%Y%m%d_%H%M%S", &tm_now);
        return std::string(savepath) + "/" + time_buf;
    }
    return std::string("partitions_backup");
}
#else
std::string BuildBackupRootDirForGuiBackup() {
    return std::string("partitions_backup");
}
#endif

void on_button_clicked_backup_all(GtkWidgetHelper helper) {
	ensure_device_attached_or_exit(helper);

#if defined(__APPLE__)
	helper.setLabelText(helper.getWidget("con"), "Backup partitions");
	std::thread([helper]() mutable {
		auto& settings = GetGuiIoSettings();

		std::unique_ptr<sfd::FlashService> svc = sfd::createFlashService();
		svc->setContext(io, &g_app_state);

		std::string output_dir = BuildBackupRootDirForGuiBackup();
		std::vector<std::string> names; // 为空表示备份全部

		auto blk_cfg = MakeBlockSizeConfigFromGui();
		DEG_LOG(I, "[blk] backup_all GUI step=%u", blk_cfg.manual_block_size);

		sfd::FlashStatus st = svc->backupPartitions(names, output_dir, sfd::SlotSelection::Auto, blk_cfg);
		gui_idle_call_wait_drag([helper, st, output_dir]() mutable {
			if (!st.success) {
				// 备份取消：使用可本地化字符串
				if (st.code == sfd::FlashErrorCode::Cancelled) {
					showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("partition backup cancelled"));
				} else {
					showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), st.message.c_str());
				}
			} else {
				// 成功时提示输出目录
				std::string msg = std::string(_("Partition backup completed! Saved to: ")) + output_dir;
				showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Completed"))), msg.c_str());
			}
			helper.setLabelText(helper.getWidget("con"), "Ready");
		}, GTK_WINDOW(helper.getWidget("main_window")));
	}).detach();
#else
	GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
	std::string output_dir = showFolderChooser(parent);
	if (output_dir.empty()) {
		// 用户取消选择，直接返回
		return;
	}

	helper.setLabelText(helper.getWidget("con"), "Backup partitions");
	std::thread([helper, output_dir]() mutable {
		auto& settings = GetGuiIoSettings();

		std::unique_ptr<sfd::FlashService> svc = sfd::createFlashService();
		svc->setContext(io, &g_app_state);

		std::vector<std::string> names; // 为空表示备份全部

		auto blk_cfg = MakeBlockSizeConfigFromGui();
		DEG_LOG(I, "[blk] backup_all GUI step=%u", blk_cfg.manual_block_size);

		sfd::FlashStatus st = svc->backupPartitions(names, output_dir, sfd::SlotSelection::Auto, blk_cfg);
		gui_idle_call_wait_drag([helper, st, output_dir]() mutable {
			if (!st.success) {
				// 备份取消：使用可本地化字符串
				if (st.code == sfd::FlashErrorCode::Cancelled) {
					showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("partition backup cancelled"));
				} else {
					showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), st.message.c_str());
				}
			} else {
				// 成功时提示输出目录
				std::string msg = std::string(_("Partition backup completed! Saved to: ")) + output_dir;
				showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Completed"))), msg.c_str());
			}
			helper.setLabelText(helper.getWidget("con"), "Ready");
		}, GTK_WINDOW(helper.getWidget("main_window")));
	}).detach();
#endif
}

void confirm_partition_c(GtkWidgetHelper helper) {
    ensure_device_attached_or_exit(helper);

    // 1. 为当前工作线程创建独立的 GMainContext
    GMainContext* worker_context = g_main_context_new();
    g_main_context_push_thread_default(worker_context);  // 设为线程默认

    // 2. 创建基于该上下文的 GMainLoop
    GMainLoop* loop = g_main_loop_new(worker_context, FALSE);

    // 3. 存储结果的容器（线程局部，安全）
    struct Result {
        bool confirmed = false;
    } result;

    // 4. 将对话框任务投递到主线程（使用全局默认上下文）
    gui_idle_call_wait_drag([&result, loop, helper]() {
        bool r = showConfirmDialog(
            GTK_WINDOW(helper.getWidget("main_window")),
            _("Confirm"),
            _("No partition table found on current device, read partition list through compatibility method?\nWarn: This mode may not find all partitions on your device, use caution with force write or editing partition table!")
        );
        result.confirmed = r;
        // 退出工作线程的 loop（注意：需要在 worker_context 中执行退出）
        g_main_loop_quit(loop);
    }, GTK_WINDOW(helper.getWidget("main_window")));

    // 5. 运行工作线程的事件循环（会处理内部消息，直到 loop 被退出）
    g_main_loop_run(loop);

    // 6. 清理
    g_main_loop_unref(loop);
    g_main_context_pop_thread_default(worker_context);
    g_main_context_unref(worker_context);

    // 7. 根据结果继续执行
    if (result.confirmed) {
        isUseCptable = 1;
        io->Cptable = partition_list_d(io);
        if (io->Cptable) isCMethod = 1;
		else
		{
		    showInfoDialogSyncInThread(GTK_WINDOW(helper.getWidget("main_window")), _("Info"), _("Partition list found in device, compatibility-method mode is disabled."));
		    return;
		}
        std::vector<sfd::DevicePartitionInfo> partitions;
        partitions.reserve(io->part_count_c);
        for (int i = 0; i < io->part_count_c; i++) {
            sfd::DevicePartitionInfo info{};
            info.name = io->Cptable[i].name;
            info.size = (std::uint64_t)io->Cptable[i].size;
            info.readable = true;
            info.writable = true;
            partitions.push_back(info);
        }
        populatePartitionList(helper, partitions);
    } else {
        DEG_LOG(W, "Partition table not read.");
    }
}

GtkWidget* PartitionPage::init(GtkWidgetHelper& helper, GtkWidget* notebook) {
	// 直接复用原 create_partition_page 的实现
	return create_partition_page(helper, notebook);
}

void PartitionPage::bindSignals(GtkWidgetHelper& helper) {
	// 直接复用原 bind_partition_signals 的实现
	bind_partition_signals(helper);
}


GtkWidget* create_partition_page(GtkWidgetHelper& helper, GtkWidget* notebook) {
    // ── 页面根容器（垂直 Box，整体可滚动） ──
    GtkWidget* outerScroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(outerScroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(outerScroll, TRUE);
    gtk_widget_set_vexpand(outerScroll, TRUE);

    GtkWidget* mainBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start(mainBox, 16);
    gtk_widget_set_margin_end(mainBox, 16);
    gtk_widget_set_margin_top(mainBox, 10);
    gtk_widget_set_margin_bottom(mainBox, 10);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(outerScroll), mainBox);

    // 把 outerScroll 作为 notebook 的 page
    helper.addWidget("part_page", outerScroll);
    helper.addNotebookPage(notebook, outerScroll, _("Partition Operation"));

    // ── 区块辅助 lambda：创建带内边距的垂直内容 box ──
    auto makeCardBox = [](int pad_h, int pad_v) -> GtkWidget* {
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gtk_widget_set_margin_start(box, pad_h);
        gtk_widget_set_margin_end(box, pad_h);
        gtk_widget_set_margin_top(box, pad_v);
        gtk_widget_set_margin_bottom(box, pad_v);
        return box;
    };

    // ══════════════════════════════════════════════
    //  第一部分：请选择一个分区
    // ══════════════════════════════════════════════
    GtkWidget* selectTitle = gtk_label_new(_("Please check a partition"));
    helper.addWidget("part_instruction", selectTitle);
    gtk_widget_set_halign(selectTitle, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_bottom(selectTitle, 6);
    gtk_box_append(GTK_BOX(mainBox), selectTitle);

    GtkWidget* listScroll = gtk_scrolled_window_new();
    gtk_widget_set_size_request(listScroll, -1, 180);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(listScroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	// 阴影效果移除
    gtk_widget_set_hexpand(listScroll, TRUE);

    GtkListStore* store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget* treeView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_widget_set_name(treeView, "part_list");
    helper.addWidget("part_list", treeView);
    GtkCellRenderer* renderer = gtk_cell_renderer_text_new();

    GtkTreeViewColumn* col_name = gtk_tree_view_column_new_with_attributes(_("Partition Name"), renderer, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), col_name);
    gtk_tree_view_column_set_sort_column_id(col_name, 0);

    GtkTreeViewColumn* col_size = gtk_tree_view_column_new_with_attributes(_("Size"), renderer, "text", 1, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), col_size);
    gtk_tree_view_column_set_sort_column_id(col_size, 1);

    GtkTreeViewColumn* col_type = gtk_tree_view_column_new_with_attributes(_("Type"), renderer, "text", 2, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), col_type);
    gtk_tree_view_column_set_sort_column_id(col_type, 2);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(listScroll), treeView);
    gtk_box_append(GTK_BOX(mainBox), listScroll);

    // ── 包含操作按钮的外框 ──
    GtkWidget* opFrame = gtk_frame_new(NULL);
    gtk_widget_set_margin_top(opFrame, 16);
    gtk_widget_set_margin_bottom(opFrame, 16);
    GtkWidget* opTitleLabel = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(opTitleLabel),
        (std::string("<span size='large'><b>") + _("Operation") + "</b></span>").c_str());
    helper.addWidget("op_label", opTitleLabel);
    gtk_frame_set_label_widget(GTK_FRAME(opFrame), opTitleLabel);
    gtkFrameSetLabelAlign(opFrame, 0.5, 0.5);  // 辅助函数

    GtkWidget* opContainerBox = makeCardBox(12, 10);
    gtk_frame_set_child(GTK_FRAME(opFrame), opContainerBox);

    // ── 操作区按钮 ──
    // 创建各个按钮（使用 GTK4 原生函数）
    GtkWidget* writeBtn = gtk_button_new_with_label(_("WRITE"));
    gtk_widget_set_name(writeBtn, "list_write");
    gtk_widget_set_size_request(writeBtn, -1, 32);
    helper.addWidget("list_write", writeBtn);

    GtkWidget* writeFBtn = gtk_button_new_with_label(_("FORCE WRITE"));
    gtk_widget_set_name(writeFBtn, "list_force_write");
    gtk_widget_set_size_request(writeFBtn, -1, 32);
    helper.addWidget("list_force_write", writeFBtn);

    GtkWidget* eraseBtn = gtk_button_new_with_label(_("ERASE"));
    gtk_widget_set_name(eraseBtn, "list_erase");
    gtk_widget_set_size_request(eraseBtn, -1, 32);
    helper.addWidget("list_erase", eraseBtn);

    GtkWidget* readBtn = gtk_button_new_with_label(_("EXTRACT"));
    gtk_widget_set_name(readBtn, "list_read");
    gtk_widget_set_size_request(readBtn, -1, 32);
    helper.addWidget("list_read", readBtn);

    GtkWidget* eraseAllBtn = gtk_button_new_with_label(_("ERASE ALL"));
    gtk_widget_set_name(eraseAllBtn, "erase_all_partitions");
    gtk_widget_set_size_request(eraseAllBtn, -1, 32);
    helper.addWidget("erase_all_partitions", eraseAllBtn);

    GtkWidget* backupAllBtn = gtk_button_new_with_label(_("Backup All"));
    gtk_widget_set_name(backupAllBtn, "backup_all");
    gtk_widget_set_size_request(backupAllBtn, -1, 32);
    helper.addWidget("backup_all", backupAllBtn);

    GtkWidget* backupCheckBtn = gtk_button_new_with_label(_("Check Backup Integrity"));
    gtk_widget_set_name(backupCheckBtn, "check_backup_integrity");
    gtk_widget_set_size_request(backupCheckBtn, -1, 32);
    helper.addWidget("check_backup_integrity", backupCheckBtn);

    GtkWidget* restoreFolderBtn = gtk_button_new_with_label(_("Restore From Folder"));
    gtk_widget_set_name(restoreFolderBtn, "restore_from_folder");
    gtk_widget_set_size_request(restoreFolderBtn, -1, 32);
    helper.addWidget("restore_from_folder", restoreFolderBtn);

    GtkWidget* xmlGetBtn = gtk_button_new_with_label(_("Get partition table through scanning an Xml file"));
    gtk_widget_set_name(xmlGetBtn, "xml_get");
    gtk_widget_set_size_request(xmlGetBtn, -1, 32);
    helper.addWidget("xml_get", xmlGetBtn);

    GtkWidget* xmlExportBtn = gtk_button_new_with_label(_("Extract part info to a XML file (if support)"));
    gtk_widget_set_name(xmlExportBtn, "export_part_xml");
    gtk_widget_set_size_request(xmlExportBtn, -1, 32);
    helper.addWidget("export_part_xml", xmlExportBtn);

    GtkWidget* cancelBtn = gtk_button_new_with_label(_("Cancel"));
    gtk_widget_set_name(cancelBtn, "list_cancel");
    gtk_widget_set_size_request(cancelBtn, 120, 32);
    helper.addWidget("list_cancel", cancelBtn);

    // 设置所有按钮的 hexpand 和 halign（按原代码）
    gtk_widget_set_hexpand(writeBtn, TRUE);
    gtk_widget_set_halign(writeBtn, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(writeFBtn, TRUE);
    gtk_widget_set_halign(writeFBtn, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(restoreFolderBtn, TRUE);
    gtk_widget_set_halign(restoreFolderBtn, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(readBtn, TRUE);
    gtk_widget_set_halign(readBtn, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(backupAllBtn, TRUE);
    gtk_widget_set_halign(backupAllBtn, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(backupCheckBtn, TRUE);
    gtk_widget_set_halign(backupCheckBtn, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(xmlExportBtn, TRUE);
    gtk_widget_set_halign(xmlExportBtn, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(xmlGetBtn, TRUE);
    gtk_widget_set_halign(xmlGetBtn, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(eraseBtn, TRUE);
    gtk_widget_set_halign(eraseBtn, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(eraseAllBtn, TRUE);
    gtk_widget_set_halign(eraseAllBtn, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(cancelBtn, TRUE);
    gtk_widget_set_halign(cancelBtn, GTK_ALIGN_FILL);

    // 写入 / 恢复分组
    GtkWidget* writeFrame = gtk_frame_new(NULL);
    gtk_widget_set_margin_bottom(writeFrame, 8);
    GtkWidget* writeTitleLabel = gtk_label_new(NULL);
    {
        std::string title = "<b>";
        title += _("WRITE");
        title += " / ";
        title += _("Restore From Folder");
        title += "</b>";
        gtk_label_set_markup(GTK_LABEL(writeTitleLabel), title.c_str());
    }
    gtk_frame_set_label_widget(GTK_FRAME(writeFrame), writeTitleLabel);
    gtkFrameSetLabelAlign(writeFrame, 0.0, 0.5);

    GtkWidget* writeBox = makeCardBox(8, 8);
    gtk_frame_set_child(GTK_FRAME(writeFrame), writeBox);

    GtkWidget* writeRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(writeBox), writeRow);
    gtk_box_append(GTK_BOX(writeRow), writeBtn);
    gtk_box_append(GTK_BOX(writeRow), writeFBtn);
    gtk_box_append(GTK_BOX(writeRow), restoreFolderBtn);

    gtk_box_append(GTK_BOX(opContainerBox), writeFrame);

    // 读取 / 备份分组
    GtkWidget* readFrame = gtk_frame_new(NULL);
    gtk_widget_set_margin_bottom(readFrame, 8);
    GtkWidget* readTitleLabel = gtk_label_new(NULL);
    {
        std::string title = "<b>";
        title += _("EXTRACT");
        title += " / ";
        title += _("Backup All");
        title += "</b>";
        gtk_label_set_markup(GTK_LABEL(readTitleLabel), title.c_str());
    }
    gtk_frame_set_label_widget(GTK_FRAME(readFrame), readTitleLabel);
    gtkFrameSetLabelAlign(readFrame, 0.0, 0.5);

    GtkWidget* readBox = makeCardBox(8, 8);
    gtk_frame_set_child(GTK_FRAME(readFrame), readBox);

    GtkWidget* readRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(readBox), readRow);
    gtk_box_append(GTK_BOX(readRow), readBtn);
    gtk_box_append(GTK_BOX(readRow), backupAllBtn);
    gtk_box_append(GTK_BOX(readRow), xmlExportBtn);
    gtk_box_append(GTK_BOX(readRow), xmlGetBtn);

    GtkWidget* readRow2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(readBox), readRow2);
    gtk_box_append(GTK_BOX(readRow2), backupCheckBtn);

    gtk_box_append(GTK_BOX(opContainerBox), readFrame);

    // 擦除分组
    GtkWidget* eraseFrame = gtk_frame_new(NULL);
    gtk_widget_set_margin_bottom(eraseFrame, 8);
    GtkWidget* eraseTitleLabel = gtk_label_new(NULL);
    {
        std::string title = "<b>";
        title += _("ERASE");
        title += " / ";
        title += _("ERASE ALL");
        title += "</b>";
        gtk_label_set_markup(GTK_LABEL(eraseTitleLabel), title.c_str());
    }
    gtk_frame_set_label_widget(GTK_FRAME(eraseFrame), eraseTitleLabel);
    gtkFrameSetLabelAlign(eraseFrame, 0.0, 0.5);

    GtkWidget* eraseBox = makeCardBox(8, 8);
    gtk_frame_set_child(GTK_FRAME(eraseFrame), eraseBox);

    GtkWidget* eraseRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(eraseBox), eraseRow);
    gtk_box_append(GTK_BOX(eraseRow), eraseBtn);
    gtk_box_append(GTK_BOX(eraseRow), eraseAllBtn);

    gtk_box_append(GTK_BOX(opContainerBox), eraseFrame);

    // 取消按钮单独占一行
    GtkWidget* cancelRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_margin_top(cancelRow, 4);
    gtk_box_append(GTK_BOX(cancelRow), cancelBtn);
    gtk_widget_set_halign(cancelBtn, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(opContainerBox), cancelRow);

    gtk_box_append(GTK_BOX(mainBox), opFrame);

    // 设置按钮初始敏感度
    gtk_widget_set_sensitive(writeBtn,    FALSE);
    gtk_widget_set_sensitive(readBtn,     FALSE);
    gtk_widget_set_sensitive(eraseBtn,    FALSE);
    gtk_widget_set_sensitive(eraseAllBtn, FALSE);
    gtk_widget_set_sensitive(backupAllBtn, FALSE);
    gtk_widget_set_sensitive(backupCheckBtn, FALSE);
    gtk_widget_set_sensitive(cancelBtn,   TRUE);
    gtk_widget_set_sensitive(restoreFolderBtn, FALSE);
    gtk_widget_set_sensitive(xmlExportBtn, FALSE);

    // ══════════════════════════════════════════════
    //  第二部分：包含修改分区的最大外框
    // ══════════════════════════════════════════════
    GtkWidget* modifyPageFrame = gtk_frame_new(NULL);
    gtk_widget_set_margin_bottom(modifyPageFrame, 16);
    GtkWidget* modifyPageTitle = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(modifyPageTitle),
        (std::string("<span size='large'><b>") + _("Modify Partition Table") + "</b></span>").c_str());
    helper.addWidget("modify_label", modifyPageTitle);
    gtk_frame_set_label_widget(GTK_FRAME(modifyPageFrame), modifyPageTitle);
    gtkFrameSetLabelAlign(modifyPageFrame, 0.5, 0.5);

    GtkWidget* modifyPageBox = makeCardBox(12, 10);
    gtk_frame_set_child(GTK_FRAME(modifyPageFrame), modifyPageBox);

    // ══════════════════════════════════════════════
    //  卡片1：修改分区大小
    // ══════════════════════════════════════════════
    GtkWidget* sizeFrame = gtk_frame_new(NULL);
    gtk_widget_set_margin_bottom(sizeFrame, 10);
    GtkWidget* sizeTitleLabel = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(sizeTitleLabel),
        (std::string("<b>") + _("Change size") + "</b>").c_str());
    helper.addWidget("fff_label", sizeTitleLabel);
    gtk_frame_set_label_widget(GTK_FRAME(sizeFrame), sizeTitleLabel);
    gtkFrameSetLabelAlign(sizeFrame, 0.5, 0.5);

    GtkWidget* sizeCardBox = makeCardBox(12, 10);
    gtk_frame_set_child(GTK_FRAME(sizeFrame), sizeCardBox);

    GtkWidget* sizeDescLabel = gtk_label_new(_("Please check a partition you want to change"));
    gtk_widget_set_halign(sizeDescLabel, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(sizeDescLabel, 6);
    gtk_box_append(GTK_BOX(sizeCardBox), sizeDescLabel);

    GtkWidget* sizeCtrlRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    GtkWidget* SeLabel = gtk_label_new(_("Second-change partition"));
    helper.addWidget("second_part_label", SeLabel);

    GtkWidget* secondPart = gtk_entry_new();
    gtk_widget_set_name(secondPart, "modify_second_part");
    gtk_widget_set_size_request(secondPart, 180, 32);
    helper.addWidget("modify_second_part", secondPart);

    GtkWidget* newSizeLabel = gtk_label_new(_("New size (MB)"));
    helper.addWidget("new_size_label", newSizeLabel);

    GtkWidget* newSizeEntry = gtk_entry_new();
    gtk_widget_set_name(newSizeEntry, "modify_new_size");
    gtk_widget_set_size_request(newSizeEntry, 120, 32);
    helper.addWidget("modify_new_size", newSizeEntry);

    GtkWidget* modifyBtn = gtk_button_new_with_label(_("Modify"));
    gtk_widget_set_name(modifyBtn, "modify_part");
    gtk_widget_set_size_request(modifyBtn, 80, 32);
    helper.addWidget("modify_part", modifyBtn);

    gtk_box_append(GTK_BOX(sizeCtrlRow), SeLabel);
    gtk_box_append(GTK_BOX(sizeCtrlRow), secondPart);
    gtk_box_append(GTK_BOX(sizeCtrlRow), newSizeLabel);
    gtk_box_append(GTK_BOX(sizeCtrlRow), newSizeEntry);
    gtk_box_append(GTK_BOX(sizeCtrlRow), modifyBtn);

    gtk_box_append(GTK_BOX(sizeCardBox), sizeCtrlRow);
    gtk_box_append(GTK_BOX(modifyPageBox), sizeFrame);

    // ══════════════════════════════════════════════
    //  卡片2：添加分区
    // ══════════════════════════════════════════════
    GtkWidget* addFrame = gtk_frame_new(NULL);
    gtk_widget_set_margin_bottom(addFrame, 10);
    GtkWidget* addTitleLabel = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(addTitleLabel),
        (std::string("<b>") + _("Add partition") + "</b>").c_str());
    helper.addWidget("ff_label", addTitleLabel);
    gtk_frame_set_label_widget(GTK_FRAME(addFrame), addTitleLabel);
    gtkFrameSetLabelAlign(addFrame, 0.5, 0.5);

    GtkWidget* addCardBox = makeCardBox(12, 10);
    gtk_frame_set_child(GTK_FRAME(addFrame), addCardBox);

    GtkWidget* addDescLabel = gtk_label_new(_("Enter partition name:"));
    gtk_widget_set_halign(addDescLabel, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(addDescLabel, 6);
    gtk_box_append(GTK_BOX(addCardBox), addDescLabel);

    GtkWidget* addCtrlRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    GtkWidget* Add2Label = gtk_label_new(_("Part name:"));
    helper.addWidget("f_label", Add2Label);

    GtkWidget* newPartEntry = gtk_entry_new();
    gtk_widget_set_name(newPartEntry, "new_part");
    gtk_widget_set_size_request(newPartEntry, 160, 32);
    helper.addWidget("new_part", newPartEntry);

    GtkWidget* new2SizeLabel = gtk_label_new(_("Size (MB):"));
    helper.addWidget("f3_label", new2SizeLabel);

    GtkWidget* newPartSize = gtk_entry_new();
    gtk_widget_set_name(newPartSize, "modify_add_size");
    gtk_widget_set_size_request(newPartSize, 100, 32);
    helper.addWidget("modify_add_size", newPartSize);

    GtkWidget* afterPartLabel = gtk_label_new(_("Part after this new part:"));
    helper.addWidget("after_part_label", afterPartLabel);

    GtkWidget* afterPart = gtk_entry_new();
    gtk_widget_set_name(afterPart, "before_new_part");
    gtk_widget_set_size_request(afterPart, 120, 32);
    helper.addWidget("before_new_part", afterPart);

    GtkWidget* addNewPartBtn = gtk_button_new_with_label(_("Modify"));
    gtk_widget_set_name(addNewPartBtn, "modify_new_part");
    gtk_widget_set_size_request(addNewPartBtn, 80, 32);
    helper.addWidget("modify_new_part", addNewPartBtn);

    gtk_box_append(GTK_BOX(addCtrlRow), Add2Label);
    gtk_box_append(GTK_BOX(addCtrlRow), newPartEntry);
    gtk_box_append(GTK_BOX(addCtrlRow), new2SizeLabel);
    gtk_box_append(GTK_BOX(addCtrlRow), newPartSize);
    gtk_box_append(GTK_BOX(addCtrlRow), afterPartLabel);
    gtk_box_append(GTK_BOX(addCtrlRow), afterPart);
    gtk_box_append(GTK_BOX(addCtrlRow), addNewPartBtn);

    gtk_box_append(GTK_BOX(addCardBox), addCtrlRow);
    gtk_box_append(GTK_BOX(modifyPageBox), addFrame);

    // ══════════════════════════════════════════════
    //  卡片3：移除分区
    // ══════════════════════════════════════════════
    GtkWidget* rmFrame = gtk_frame_new(NULL);
    gtk_widget_set_margin_bottom(rmFrame, 10);
    GtkWidget* rmTitleLabel = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(rmTitleLabel),
        (std::string("<b>") + _("Remove partition") + "</b>").c_str());
    helper.addWidget("ffff_label", rmTitleLabel);
    gtk_frame_set_label_widget(GTK_FRAME(rmFrame), rmTitleLabel);
    gtkFrameSetLabelAlign(rmFrame, 0.5, 0.5);

    GtkWidget* rmCardBox = makeCardBox(12, 10);
    gtk_frame_set_child(GTK_FRAME(rmFrame), rmCardBox);

    GtkWidget* rmDescLabel = gtk_label_new(_("Please check a partition you want to remove"));
    gtk_widget_set_halign(rmDescLabel, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(rmDescLabel, 6);
    gtk_box_append(GTK_BOX(rmCardBox), rmDescLabel);

    GtkWidget* RemvPartBtn = gtk_button_new_with_label(_("Delete"));
    gtk_widget_set_name(RemvPartBtn, "modify_rm_part");
    gtk_widget_set_size_request(RemvPartBtn, 80, 32);
    helper.addWidget("modify_rm_part", RemvPartBtn);
    gtk_widget_set_name(RemvPartBtn, "danger_button");
    gtk_style_context_add_class(gtk_widget_get_style_context(RemvPartBtn), "destructive-action");
    gtk_widget_set_halign(RemvPartBtn, GTK_ALIGN_START);

    gtk_box_append(GTK_BOX(rmCardBox), RemvPartBtn);
    gtk_box_append(GTK_BOX(modifyPageBox), rmFrame);

    // ══════════════════════════════════════════════
    //  卡片4：重命名分区
    // ══════════════════════════════════════════════
    GtkWidget* renFrame = gtk_frame_new(NULL);
    gtk_widget_set_margin_bottom(renFrame, 10);
    GtkWidget* renTitleLabel = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(renTitleLabel),
        (std::string("<b>") + _("Rename partition") + "</b>").c_str());
    helper.addWidget("f2_label", renTitleLabel);
    gtk_frame_set_label_widget(GTK_FRAME(renFrame), renTitleLabel);
    gtkFrameSetLabelAlign(renFrame, 0.5, 0.5);

    GtkWidget* renCardBox = makeCardBox(12, 10);
    gtk_frame_set_child(GTK_FRAME(renFrame), renCardBox);

    GtkWidget* renDescLabel = gtk_label_new(_("Please check a partition you want to rename"));
    gtk_widget_set_halign(renDescLabel, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(renDescLabel, 6);
    gtk_box_append(GTK_BOX(renCardBox), renDescLabel);

    GtkWidget* renCtrlRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    GtkWidget* RenmPartLabel = gtk_label_new(_("New name"));
    helper.addWidget("f4_label", RenmPartLabel);

    GtkWidget* RenmPartEntry = gtk_entry_new();
    gtk_widget_set_name(RenmPartEntry, "modify_rename_part");
    gtk_widget_set_size_request(RenmPartEntry, 200, 32);
    helper.addWidget("modify_rename_part", RenmPartEntry);

    GtkWidget* RenmPartBtn = gtk_button_new_with_label(_("Modify"));
    gtk_widget_set_name(RenmPartBtn, "modify_ren_part");
    gtk_widget_set_size_request(RenmPartBtn, 80, 32);
    helper.addWidget("modify_ren_part", RenmPartBtn);

    gtk_box_append(GTK_BOX(renCtrlRow), RenmPartLabel);
    gtk_box_append(GTK_BOX(renCtrlRow), RenmPartEntry);
    gtk_box_append(GTK_BOX(renCtrlRow), RenmPartBtn);

    gtk_box_append(GTK_BOX(renCardBox), renCtrlRow);
    gtk_box_append(GTK_BOX(modifyPageBox), renFrame);

    gtk_box_append(GTK_BOX(mainBox), modifyPageFrame);

    gtk_widget_show(outerScroll);
    return outerScroll;
}

static std::vector<BatchPartitionWriteItem>
show_restore_from_folder_dialog(GtkWidgetHelper& helper,
                                const std::vector<BatchPartitionWriteItem>& items) {
	GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));

	GtkWidget* dialog = gtk_dialog_new_with_buttons(
	    _("Restore From Folder"),
	    parent,
	    GTK_DIALOG_MODAL,
	    _("Cancel"), GTK_RESPONSE_CANCEL,
	    _("WRITE"), GTK_RESPONSE_OK,
	    nullptr);

	GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

	// 1. 使用 GtkBox 替代 GtkButtonBox
	GtkWidget* button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);  // spacing = 8

	// 2. 设置左对齐（等同于 GTK_BUTTONBOX_START）
	gtk_widget_set_halign(button_box, GTK_ALIGN_START);
	gtk_box_set_spacing(GTK_BOX(button_box), 6);
	gtkBoxPackStart((content), button_box, FALSE, FALSE, 6);

	GtkWidget* select_all_btn = gtk_button_new_with_label(_("Select All"));
	GtkWidget* unselect_all_btn = gtk_button_new_with_label(_("Unselect All"));
	gtkBoxPackStart((button_box), select_all_btn, FALSE, FALSE, 0);
	gtkBoxPackStart((button_box), unselect_all_btn, FALSE, FALSE, 0);

	GtkWidget* scrolled = gtk_scrolled_window_new();
	// 在对话框中适当减小默认高度，避免在低分辨率屏幕上遮挡底部状态栏
	gtk_widget_set_size_request(scrolled, 900, 560);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtkBoxPackStart((content), scrolled, TRUE, TRUE, 0);

	GtkListStore* store = gtk_list_store_new(7,
	                                         G_TYPE_BOOLEAN,  // 0: selected
	                                         G_TYPE_STRING,   // 1: part name
	                                         G_TYPE_STRING,   // 2: image path
	                                         G_TYPE_STRING,   // 3: part size
	                                         G_TYPE_STRING,   // 4: file size
	                                         G_TYPE_STRING,   // 5: critical mark
	                                         G_TYPE_INT);     // 6: original index
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store), 1, GTK_SORT_ASCENDING);

	int idx = 0;
	for (const auto& item : items) {
		GtkTreeIter iter;
		gtk_list_store_append(store, &iter);

		std::string part_size_text;
		if (item.part.size < 1024ULL) {
			part_size_text = std::to_string(item.part.size) + " B";
		} else if (item.part.size < 1024ULL * 1024) {
			part_size_text = std::to_string(item.part.size / 1024ULL) + " KB";
		} else if (item.part.size < 1024ULL * 1024 * 1024) {
			part_size_text = std::to_string(item.part.size / (1024ULL * 1024)) + " MB";
		} else {
			part_size_text = std::to_string(item.part.size / (1024ULL * 1024 * 1024)) + " GB";
		}

		std::string file_size_text;
		if (item.image_size < 1024ULL) {
			file_size_text = std::to_string(item.image_size) + " B";
		} else if (item.image_size < 1024ULL * 1024) {
			file_size_text = std::to_string(item.image_size / 1024ULL) + " KB";
		} else if (item.image_size < 1024ULL * 1024 * 1024) {
			file_size_text = std::to_string(item.image_size / (1024ULL * 1024)) + " MB";
		} else {
			file_size_text = std::to_string(item.image_size / (1024ULL * 1024 * 1024)) + " GB";
		}

		const char* critical_mark = item.is_critical ? _("Critical") : "";

		gtk_list_store_set(store, &iter,
		                   0, item.selected,
		                   1, item.part.name.c_str(),
		                   2, item.image_path.c_str(),
		                   3, part_size_text.c_str(),
		                   4, file_size_text.c_str(),
		                   5, critical_mark,
		                   6, idx,
		                   -1);
		++idx;
	}

	GtkWidget* tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), tree);
	g_object_unref(store);

	// 列 0: checkbox
	GtkCellRenderer* toggle_renderer = gtk_cell_renderer_toggle_new();
	GtkTreeViewColumn* col_toggle = gtk_tree_view_column_new_with_attributes(
	    "", toggle_renderer, "active", 0, nullptr);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree), col_toggle);

	g_signal_connect(toggle_renderer, "toggled", G_CALLBACK(+[] (GtkCellRendererToggle* /*cell*/, gchar* path_str, gpointer data) {
		GtkTreeView* view = GTK_TREE_VIEW(data);
		GtkTreeModel* model = gtk_tree_view_get_model(view);
		GtkTreePath* path = gtk_tree_path_new_from_string(path_str);
		GtkTreeIter iter;
		if (gtk_tree_model_get_iter(model, &iter, path)) {
			gboolean active = FALSE;
			gtk_tree_model_get(model, &iter, 0, &active, -1);
			gtk_list_store_set(GTK_LIST_STORE(model), &iter, 0, !active, -1);
		}
		gtk_tree_path_free(path);
	}), tree);

	g_signal_connect(select_all_btn, "clicked", G_CALLBACK(+[] (GtkButton* /*button*/, gpointer data) {
		GtkTreeView* view = GTK_TREE_VIEW(data);
		GtkTreeModel* model = gtk_tree_view_get_model(view);
		GtkTreeIter iter;
		gboolean valid = gtk_tree_model_get_iter_first(model, &iter);
		while (valid) {
			gtk_list_store_set(GTK_LIST_STORE(model), &iter, 0, TRUE, -1);
			valid = gtk_tree_model_iter_next(model, &iter);
		}
	}), tree);

	g_signal_connect(unselect_all_btn, "clicked", G_CALLBACK(+[] (GtkButton* /*button*/, gpointer data) {
		GtkTreeView* view = GTK_TREE_VIEW(data);
		GtkTreeModel* model = gtk_tree_view_get_model(view);
		GtkTreeIter iter;
		gboolean valid = gtk_tree_model_get_iter_first(model, &iter);
		while (valid) {
			gtk_list_store_set(GTK_LIST_STORE(model), &iter, 0, FALSE, -1);
			valid = gtk_tree_model_iter_next(model, &iter);
		}
	}), tree);

	// 其余列：分区名、镜像路径、分区大小、文件大小、关键标记
	GtkCellRenderer* text_renderer = gtk_cell_renderer_text_new();
	GtkTreeViewColumn* col_part = gtk_tree_view_column_new_with_attributes(_("Partition Name"), text_renderer, "text", 1, nullptr);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree), col_part);

	GtkTreeViewColumn* col_path = gtk_tree_view_column_new_with_attributes(_("Image file path:"), text_renderer, "text", 2, nullptr);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree), col_path);

	GtkTreeViewColumn* col_part_size = gtk_tree_view_column_new_with_attributes(_("Partition Size"), text_renderer, "text", 3, nullptr);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree), col_part_size);

	GtkTreeViewColumn* col_file_size = gtk_tree_view_column_new_with_attributes(_("File Size"), text_renderer, "text", 4, nullptr);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree), col_file_size);

	GtkTreeViewColumn* col_critical = gtk_tree_view_column_new_with_attributes(_("Critical"), text_renderer, "text", 5, nullptr);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree), col_critical);

#if GTK_CHECK_VERSION(4, 0, 0)
	gtk_widget_show(dialog);
#else
	gtk_widget_show_all(dialog);
#endif

	std::vector<BatchPartitionWriteItem> result;
	if (runDialog(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
		GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
		GtkTreeIter iter;
		gboolean valid = gtk_tree_model_get_iter_first(model, &iter);
		while (valid) {
			gboolean active = FALSE;
			gint orig_index = -1;
			gtk_tree_model_get(model, &iter,
			                   0, &active,
			                   6, &orig_index,
			                   -1);

			if (orig_index >= 0 && (std::size_t)orig_index < items.size()) {
				BatchPartitionWriteItem item = items[orig_index];
				item.selected = active;
				result.push_back(std::move(item));
			}

			valid = gtk_tree_model_iter_next(model, &iter);
		}

		// 若全未选中则提示
		bool any_selected = false;
		for (const auto& it : result) {
			if (it.selected) { any_selected = true; break; }
		}
		if (!any_selected) {
			showInfoDialog(parent, _("Info"), _("No partitions selected to flash."));
			result.clear();
		}
	}

	destroyWidget(dialog);
	return result;
}

static void show_backup_inspection_dialog(GtkWidgetHelper& helper,
                                          const std::vector<BackupInspectionItem>& items) {
	GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));

	GtkWidget* dialog = gtk_dialog_new_with_buttons(
	    _("Backup Integrity Check"),
	    parent,
	    GTK_DIALOG_MODAL,
	    _("Close"), GTK_RESPONSE_CLOSE,
	    nullptr);

	GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

	std::size_t ok_count = 0;
	std::size_t issue_count = 0;
	for (const auto& item : items) {
		const bool ok = item.matched && item.size_match && !item.empty_file &&
		                !item.all_zero && !item.analysis_error;
		if (ok) {
			++ok_count;
		} else {
			++issue_count;
		}
	}

	std::string summary = _("Items shown: ");
	summary += std::to_string(items.size());
	summary += _(", OK: ");
	summary += std::to_string(ok_count);
	summary += _(", Need attention: ");
	summary += std::to_string(issue_count);

	GtkWidget* summary_label = gtk_label_new(summary.c_str());
	gtk_widget_set_halign(summary_label, GTK_ALIGN_START);
	gtk_widget_set_margin_bottom(summary_label, 8);
	gtkBoxPackStart((content), summary_label, FALSE, FALSE, 0);

	GtkWidget* scrolled = gtk_scrolled_window_new();
	gtk_widget_set_size_request(scrolled, 980, 560);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtkBoxPackStart((content), scrolled, TRUE, TRUE, 0);

	GtkListStore* store = gtk_list_store_new(6,
	                                         G_TYPE_STRING,
	                                         G_TYPE_STRING,
	                                         G_TYPE_STRING,
	                                         G_TYPE_STRING,
	                                         G_TYPE_STRING,
	                                         G_TYPE_STRING);

	for (const auto& item : items) {
		GtkTreeIter iter;
		gtk_list_store_append(store, &iter);

		const std::string status_text = backup_inspection_status_text(item);
		const std::string expected_size_text =
		    item.expected_size ? format_size_bytes(item.expected_size) : "-";
		const std::string file_size_text =
		    item.image_size ? format_size_bytes(item.image_size) : "0 B";

		gtk_list_store_set(store, &iter,
		                   0, status_text.c_str(),
		                   1, item.part.name.c_str(),
		                   2, item.image_path.c_str(),
		                   3, expected_size_text.c_str(),
		                   4, file_size_text.c_str(),
		                   5, item.note.c_str(),
		                   -1);
	}

	GtkWidget* tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), tree);
	g_object_unref(store);

	GtkCellRenderer* text_renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree),
	                            gtk_tree_view_column_new_with_attributes(_("Status"), text_renderer, "text", 0, nullptr));
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree),
	                            gtk_tree_view_column_new_with_attributes(_("Partition Name"), text_renderer, "text", 1, nullptr));
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree),
	                            gtk_tree_view_column_new_with_attributes(_("Image file path:"), text_renderer, "text", 2, nullptr));
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree),
	                            gtk_tree_view_column_new_with_attributes(_("Partition Size"), text_renderer, "text", 3, nullptr));
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree),
	                            gtk_tree_view_column_new_with_attributes(_("File Size"), text_renderer, "text", 4, nullptr));
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree),
	                            gtk_tree_view_column_new_with_attributes(_("Note"), text_renderer, "text", 5, nullptr));

#if GTK_CHECK_VERSION(4, 0, 0)
	gtk_widget_show(dialog);
#else
	gtk_widget_show_all(dialog);
#endif
	runDialog(GTK_DIALOG(dialog));
	destroyWidget(dialog);
}

static void run_batch_partition_write(GtkWidgetHelper helper,
                                      const std::vector<BatchPartitionWriteItem>& items,
                                      bool force) {
	if (items.empty()) {
		return;
	}

	GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
	std::vector<BatchPartitionWriteItem> execution_items = items;
	std::stable_sort(execution_items.begin(), execution_items.end(), [](const BatchPartitionWriteItem& lhs,
	                                                                   const BatchPartitionWriteItem& rhs) {
		const int lhs_priority = batch_write_priority(lhs);
		const int rhs_priority = batch_write_priority(rhs);
		if (lhs_priority != rhs_priority) {
			return lhs_priority < rhs_priority;
		}
		return lhs.part.name < rhs.part.name;
	});

	LongTaskConfig cfg{
		[parent, helper, execution_items, force](std::atomic_bool& cancel_flag) {
			auto* svc = ensure_flash_service();
			const std::size_t total = execution_items.size();
			const bool has_super =
			    std::any_of(execution_items.begin(), execution_items.end(), [](const BatchPartitionWriteItem& item) {
				    return item.part.name == "super";
			    });
			const bool has_metadata =
			    std::any_of(execution_items.begin(), execution_items.end(), [](const BatchPartitionWriteItem& item) {
				    return item.part.name == "metadata";
			    });
			bool completed_all = true;

			for (std::size_t idx = 0; idx < total; ++idx) {
				if (cancel_flag.load()) {
					completed_all = false;
					break;
				}

				const auto& item = execution_items[idx];

				// 更新状态栏：当前进度
				gui_idle_call([helper, idx, total, part_name = item.part.name]() mutable {
					char buf[256];
					std::snprintf(buf, sizeof(buf),
					             "Flashing %s (%zu/%zu)...",
					             part_name.c_str(), idx + 1, total);
					helper.setLabelText(helper.getWidget("con"), buf);
				});

				sfd::PartitionIoOptions opts;
				opts.partition_name = item.part.name;
				opts.file_path = item.image_path;
				opts.block_size = GetEffectiveManualBlockSize();
				opts.force = force;

				sfd::FlashStatus st = svc->writePartitionFromFile(opts);
				if (!st.success) {
					completed_all = false;
					gui_idle_call_wait_drag([parent, st, part_name = item.part.name, part_size = item.part.size, image_size = item.image_size]() mutable {
						std::string msg = "Failed to flash partition ";
						msg += part_name;
						msg += ": ";
						msg += st.message;
						if (part_size > 0 && image_size > part_size) {
							msg += " (image larger than partition)";
						}
						showErrorDialog(parent, _("Error"), msg.c_str());
					}, parent);
					break;
				}
			}

			if (completed_all && !cancel_flag.load() && has_super && !has_metadata) {
				sfd::FlashStatus erase_st = svc->erasePartition("metadata");
				if (!erase_st.success && erase_st.code != sfd::FlashErrorCode::PartitionNotFound) {
					gui_idle_call_wait_drag([parent, erase_st]() mutable {
						std::string msg = _("Failed to reset metadata after flashing super: ");
						msg += erase_st.message;
						showErrorDialog(parent, _("Error"), msg.c_str());
					}, parent);
				}
			}
		},
		[helper]() mutable {
			helper.setLabelText(helper.getWidget("con"), "Starting restore from folder...");
		},
		[helper]() mutable {
			helper.setLabelText(helper.getWidget("con"), "Ready");
		}
	};

	run_long_task(cfg);
}

void on_button_clicked_restore_from_folder(GtkWidgetHelper helper) {
	GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));

	// 仅检查连接状态，不直接退出程序
	if (ensure_device_attached_or_warn(helper)) {
		return;
	}

	std::vector<sfd::DevicePartitionInfo> partitions;
	collect_current_device_partitions_for_batch_ops(helper, partitions);

	std::string folder = showFolderChooser(parent);
	if (folder.empty()) {
		// 用户取消
		return;
	}

	auto items = scan_folder_and_match_partitions(folder, partitions);
	if (items.empty()) {
		showInfoDialog(parent, _("Info"),
		              _("No partition images matching the current partition table were found in the selected folder."));
		return;
	}

	auto dialog_items = show_restore_from_folder_dialog(helper, items);
	if (dialog_items.empty()) {
		// 用户取消或未选择任何分区
		return;
	}

	// 仅保留选中的条目
	std::vector<BatchPartitionWriteItem> final_items;
	final_items.reserve(dialog_items.size());
	for (const auto& it : dialog_items) {
		if (it.selected) {
			final_items.push_back(it);
		}
	}

	if (final_items.empty()) {
		showInfoDialog(parent, _("Info"),
		              _("No partitions were selected to flash from the folder."));
		return;
	}

	// 总体确认
	if (!showConfirmDialog(parent, _("Confirm"),
	                       _("Start flashing the selected partitions from the folder?"))) {
		return;
	}

	run_batch_partition_write(helper, final_items, /*force=*/false);
}

void on_button_clicked_inspect_backup_folder(GtkWidgetHelper helper) {
	GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));

	if (ensure_device_attached_or_warn(helper)) {
		return;
	}

	std::vector<sfd::DevicePartitionInfo> partitions;
	if (!collect_current_device_partitions_for_batch_ops(helper, partitions)) {
		return;
	}

	std::string folder = showFolderChooser(parent);
	if (folder.empty()) {
		return;
	}

	LongTaskConfig cfg{
		[parent, helper, folder, partitions](std::atomic_bool& cancel_flag) {
			(void)cancel_flag;
			auto items = inspect_backup_folder(folder, partitions);
			gui_idle_call_wait_drag([helper, items = std::move(items)]() mutable {
				show_backup_inspection_dialog(helper, items);
			}, GTK_WINDOW(helper.getWidget("main_window")));
		},
		[helper]() mutable {
			helper.setLabelText(helper.getWidget("con"), _("Checking backup integrity..."));
		},
		[helper]() mutable {
			helper.setLabelText(helper.getWidget("con"), "Ready");
		}
	};

	run_long_task(cfg);
}

void on_button_clicked_list_read(GtkWidgetHelper& helper) {
	GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
	std::string part_name = getSelectedPartitionName(helper);

	auto& settings_enter = GetGuiIoSettings();
	LogBlkState("partition list_read enter");

#if defined(__APPLE__)
	if (g_is_macos_bundle) {
		// macOS Finder 双击 .app 时，savepath 在 gtk_kmain 中已设置，
		// 直接保存到固定目录，不再弹出路径选择对话框。
		ensure_device_attached_or_exit(helper);
		if (io->part_count == 0 && io->part_count_c == 0) {
			showErrorDialog(parent, _(_(("Error"))), _("No partition table loaded, cannot write partition list!"));
			return;
		}

		auto& settings = GetGuiIoSettings();

		// 默认路径：savepath/partition.img，如果 savepath 为空则退回当前目录
		std::string finalPath;
		if (savepath[0]) {
			std::string root = BuildBackupRootDirForGuiBackup();
			finalPath = root + "/" + part_name + ".img";
		} else {
			finalPath = part_name + ".img";
		}

		LongTaskConfig cfg{
			[parent, helper, part_name, finalPath](std::atomic_bool& cancel_flag) {
				(void)cancel_flag;
				sfd::PartitionReadInfo info{};
				info.name = part_name;
				sfd::PartitionReadOptions opts{};
				opts.output_path = finalPath;
				opts.block_cfg = MakeBlockSizeConfigFromGui();
				DEG_LOG(I, "[blk] list_read(macos) part=%s", part_name.c_str());
				sfd::PartitionReadCallbacks cb;
				cb.on_progress = [helper](const sfd::PartitionReadInfo& p, std::uint64_t bytes_read, double speed_mb_s) {
					gui_idle_call([helper, p, bytes_read, speed_mb_s]() mutable {
						GtkWidget* conStatus = helper.getWidget("con");
						if (conStatus && GTK_IS_LABEL(conStatus)) {
							char status_text[256];
							snprintf(status_text, sizeof(status_text),
							         "Backing up %s | size: %llu | read: %llu | speed: %.1f MB/s",
							         p.name.c_str(),
							         static_cast<unsigned long long>(p.size),
							         static_cast<unsigned long long>(bytes_read),
							         speed_mb_s);
							gtk_label_set_text(GTK_LABEL(conStatus), status_text);
						}
					});
				};
				auto* svc = ensure_flash_service();
				sfd::FlashStatus st = svc->partitionReader().readOne(info, opts, &cb);
				gui_idle_call_wait_drag([parent, helper, st, finalPath]() mutable {
					if (!st.success) {
						showErrorDialog(parent, _(_(("Error"))), st.message.c_str());
					} else {
						std::string msg = std::string(_("Partition read completed! Saved to: ")) + finalPath;
						showInfoDialog(GTK_WINDOW(parent), _(_(("Completed"))), msg.c_str());
					}
				}, GTK_WINDOW(helper.getWidget("main_window")));
			},
			[helper]() mutable {
				helper.setLabelText(helper.getWidget("con"), "Reading partition");
			},
			[helper]() mutable {
				helper.setLabelText(helper.getWidget("con"), "Ready");
			}
		};

		run_long_task(cfg);
		return;
	}
#endif

	// 非 macOS 或 macOS 下 CLI 运行：始终弹出保存路径对话框
	std::string savePath = showSaveFileDialog(parent, part_name + ".img");
	ensure_device_attached_or_exit(helper);
	if (savePath.empty()) {
		showErrorDialog(parent, _(_(("Error"))), _("No save path selected!"));
		return;
	}
	if (io->part_count == 0 && io->part_count_c == 0) {
		showErrorDialog(parent, _(_(("Error"))), _("No partition table loaded, cannot write partition list!"));
		return;
	}

	LongTaskConfig cfg2{
		[parent, helper, part_name, savePath](std::atomic_bool& cancel_flag) mutable {
			(void)cancel_flag;
			sfd::PartitionReadInfo info{};
			info.name = part_name;

			sfd::PartitionReadOptions opts{};
			opts.output_path = savePath;
			opts.block_cfg = MakeBlockSizeConfigFromGui();

			DEG_LOG(I, "[blk] list_read GUI part=%s", part_name.c_str());
			sfd::PartitionReadCallbacks cb;
			cb.on_progress = [helper](const sfd::PartitionReadInfo& p, std::uint64_t bytes_read, double speed_mb_s) {
				gui_idle_call([helper, p, bytes_read, speed_mb_s]() mutable {
					GtkWidget* conStatus = helper.getWidget("con");
					if (conStatus && GTK_IS_LABEL(conStatus)) {
						char status_text[256];
						snprintf(status_text, sizeof(status_text),
						         "Backing up %s | size: %llu | read: %llu | speed: %.1f MB/s",
						         p.name.c_str(),
						         static_cast<unsigned long long>(p.size),
						         static_cast<unsigned long long>(bytes_read),
						         speed_mb_s);
						gtk_label_set_text(GTK_LABEL(conStatus), status_text);
					}
				});
			};

			auto* svc = ensure_flash_service();
			sfd::FlashStatus st = svc->partitionReader().readOne(info, opts, &cb);
			gui_idle_call_wait_drag([parent, helper, st, savePath]() mutable {
				if (!st.success) {
					showErrorDialog(parent, _(_(("Error"))), st.message.c_str());
				} else {
					std::string msg = std::string(_("Partition read completed! Saved to: ")) + savePath;
					showInfoDialog(GTK_WINDOW(parent), _(_(("Completed"))), msg.c_str());
				}
			}, GTK_WINDOW(helper.getWidget("main_window")));
		},
		[helper]() mutable {
			helper.setLabelText(helper.getWidget("con"), "Reading partition");
		},
		[helper]() mutable {
			helper.setLabelText(helper.getWidget("con"), "Ready");
		}
	};

	run_long_task(cfg2);
}


void bind_partition_signals(GtkWidgetHelper& helper) {
	helper.bindClick(helper.getWidget("list_write"), [&]() {
		on_button_clicked_list_write(helper);
	});
	helper.bindClick(helper.getWidget("list_read"), [&]() {
		on_button_clicked_list_read(helper);
	});
	helper.bindClick(helper.getWidget("list_erase"), [&]() {
		on_button_clicked_list_erase(helper);
	});
	helper.bindClick(helper.getWidget("backup_all"),[&](){
		on_button_clicked_backup_all(helper);
	});
	helper.bindClick(helper.getWidget("check_backup_integrity"),[&](){
		on_button_clicked_inspect_backup_folder(helper);
	});
	helper.bindClick(helper.getWidget("list_cancel"), [&]() {
		on_button_clicked_list_cancel(helper);
	});
	helper.bindClick(helper.getWidget("list_force_write"), [&]() {
		on_button_clicked_list_force_write(helper);
	});
	helper.bindClick(helper.getWidget("modify_part"),[&]() {
		on_button_clicked_modify_part(helper);
	});
	helper.bindClick(helper.getWidget("modify_new_part"),[&](){
		on_button_clicked_modify_new_part(helper);
	});
	helper.bindClick(helper.getWidget("modify_rm_part"),[&](){
		on_button_clicked_modify_rm_part(helper);
	});
	helper.bindClick(helper.getWidget("modify_ren_part"),[&](){
		on_button_clicked_modify_ren_part(helper);
	});
	helper.bindClick(helper.getWidget("xml_get"),[&](){
		on_button_clicked_xml_get(helper);
	});
	helper.bindClick(helper.getWidget("export_part_xml"),[&](){
		on_button_clicked_export_part_xml(helper);
	});
	helper.bindClick(helper.getWidget("restore_from_folder"),[&](){
		on_button_clicked_restore_from_folder(helper);
	});
	helper.bindClick(helper.getWidget("erase_all_partitions"),[&](){
		on_button_clicked_erase_all_partitions(helper);
	});
}
