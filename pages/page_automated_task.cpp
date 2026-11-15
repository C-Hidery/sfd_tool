/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#include "page_automated_task.h"

#include "../common.h"
#include "../core/pac_extract.h"
#include "../core/zipxx.hpp"
#include "../i18n.h"
#include "ui/gtk_row_view.hpp"
#include "ui/ui_common.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const char* kUnknown = "Unknown";

std::string trim_ascii(std::string value)
{
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(),
                                            [&](unsigned char c) { return !is_space(c); }));
    value.erase(std::find_if(value.rbegin(), value.rend(),
                             [&](unsigned char c) { return !is_space(c); }).base(),
                value.end());
    return value;
}

std::string lower_ascii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

// package.json 中的分区大小默认按 MB 解释；同时兼容 "35MB" / "35840KB" / "35M"
// 这类带单位的写法，比较时统一换算为字节。
std::uint64_t parse_package_size_bytes(const std::string& raw)
{
    const std::string text = trim_ascii(raw);
    if (text.empty()) {
        throw std::runtime_error(_("Partition size is empty."));
    }

    errno = 0;
    char* end = nullptr;
    const double number = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || errno == ERANGE || number < 0.0) {
        throw std::runtime_error(std::string(_("Invalid partition size: ")) + raw);
    }

    const std::string unit = lower_ascii(trim_ascii(std::string(end)));
    double multiplier = 1024.0 * 1024.0; // 默认 MB
    if (unit.empty() || unit == "m" || unit == "mb") {
        multiplier = 1024.0 * 1024.0;
    } else if (unit == "k" || unit == "kb") {
        multiplier = 1024.0;
    } else if (unit == "g" || unit == "gb") {
        multiplier = 1024.0 * 1024.0 * 1024.0;
    } else if (unit == "b" || unit == "byte" || unit == "bytes") {
        multiplier = 1.0;
    } else {
        throw std::runtime_error(std::string(_("Invalid partition size: ")) + raw);
    }

    const double bytes = number * multiplier;
    if (bytes > static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
        throw std::runtime_error(std::string(_("Partition size is too large: ")) + raw);
    }
    return static_cast<std::uint64_t>(bytes);
}

std::string format_size_bytes(std::uint64_t bytes)
{
    if (bytes < 1024ULL) {
        return std::to_string(bytes) + " B";
    }
    if (bytes < 1024ULL * 1024ULL) {
        return std::to_string(bytes / 1024ULL) + " KB";
    }
    if (bytes < 1024ULL * 1024ULL * 1024ULL) {
        return std::to_string(bytes / (1024ULL * 1024ULL)) + " MB";
    }
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.2f GB",
                  static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    return buffer;
}

struct PackagePartitionSpec {
    std::string name;
    std::string size;
};

// 创建带统一内边距的 card 内容盒，与其它页面的卡片保持一致。
GtkWidget* make_card_box(int pad_h, int pad_v)
{
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(box, pad_h);
    gtk_widget_set_margin_end(box, pad_h);
    gtk_widget_set_margin_top(box, pad_v);
    gtk_widget_set_margin_bottom(box, pad_v);
    return box;
}

// 创建带大号粗体标题的 GtkFrame。
GtkWidget* make_titled_frame(const char* title, const char* title_widget_name)
{
    GtkWidget* frame = gtk_frame_new(nullptr);
    GtkWidget* title_label = gtk_label_new(nullptr);
    const std::string markup = std::string("<span size='large'><b>") + title + "</b></span>";
    gtk_label_set_markup(GTK_LABEL(title_label), markup.c_str());
    gtk_widget_set_name(title_label, title_widget_name);
    gtk_frame_set_label_widget(GTK_FRAME(frame), title_label);
    gtkFrameSetLabelAlign(frame, 0.5, 0.5);
    return frame;
}

struct PackageManifest {
    AutomatedTaskPackageInfo info;
    nlohmann::json actions;
    std::vector<PackagePartitionSpec> partitions;
};

// 读取 ZIP 根目录的 package.json，并校验包类型、actions 和可选 partitions。
// actions 的具体命令解析在执行阶段完成，这里只检查其结构。
PackageManifest read_package_manifest(const std::string& archive_path)
{
    zipxx::Archive archive(archive_path, zipxx::Archive::Mode::Read);

    if (!archive.contains("package.json")) {
        throw std::runtime_error(_("package.json is missing in the ZIP archive."));
    }

    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(archive.read_to_string("package.json"));
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(std::string(_("Failed to parse package.json: ")) + e.what());
    }
    if (!doc.is_object()) {
        throw std::runtime_error(_("package.json must be a JSON object."));
    }

    auto get_string = [&doc](const char* key) -> std::string {
        if (!doc.contains(key) || doc[key].is_null()) {
            return {};
        }
        if (!doc[key].is_string()) {
            throw std::runtime_error(std::string(_("package.json field must be a string: ")) + key);
        }
        return doc[key].get<std::string>();
    };

    PackageManifest manifest;
    manifest.info.name = get_string("name");
    manifest.info.author = get_string("author");
    manifest.info.version = get_string("version");
    manifest.info.contact = get_string("contact");
    manifest.info.type = get_string("type");

    if (manifest.info.type.empty()) {
        throw std::runtime_error(_("package.json field \"type\" is required."));
    }
    if (manifest.info.type != "write_part" && manifest.info.type != "single_act") {
        throw std::runtime_error(std::string(_("Unsupported package type: ")) +
                                 manifest.info.type +
                                 _(" (expected write_part or single_act)."));
    }

    if (!doc.contains("actions") || !doc["actions"].is_array() ||
        doc["actions"].empty()) {
        throw std::runtime_error(
            _("package.json field \"actions\" must be a non-empty array."));
    }
    manifest.actions = doc["actions"];

    // partitions 为可选字段；存在时必须是 [name, size] 形式的数组。
    if (doc.contains("partitions") && !doc["partitions"].is_null()) {
        if (!doc["partitions"].is_array()) {
            throw std::runtime_error(
                _("package.json field \"partitions\" must be an array."));
        }
        for (const auto& item : doc["partitions"]) {
            if (!item.is_array() || item.size() != 2 || !item[0].is_string() ||
                (!item[1].is_string() && !item[1].is_number())) {
                throw std::runtime_error(
                    _("package.json partitions entries must be [name, size] pairs."));
            }
            PackagePartitionSpec spec;
            spec.name = item[0].get<std::string>();
            spec.size = item[1].is_string() ? item[1].get<std::string>()
                                            : item[1].dump();
            if (spec.name.empty()) {
                throw std::runtime_error(
                    _("package.json partition name must not be empty."));
            }
            if (lower_ascii(spec.name) == "splloader") {
                throw std::runtime_error(
                    _("The package partition list must not contain splloader."));
            }
            parse_package_size_bytes(spec.size); // 尽早校验格式
            manifest.partitions.push_back(spec);
        }
    }

    if (manifest.info.type == "write_part") {
        bool has_images_dir = false;
        archive.for_each([&has_images_dir](const zipxx::EntryInfo& entry) {
            if (entry.name.rfind("images/", 0) == 0) {
                has_images_dir = true;
            }
        });
        if (!has_images_dir) {
            throw std::runtime_error(
                _("write_part package requires an images folder in the ZIP archive."));
        }
    }

    return manifest;
}

void set_info_value(GtkWidgetHelper& helper, const char* widget_name, const std::string& value)
{
    GtkWidget* label = helper.getWidget(widget_name);
    if (!label) {
        return;
    }
    const std::string text = (value.empty() || value == kUnknown) ? _("Unknown") : value;
    helper.setLabelText(label, text);
}

std::string choose_zip_archive(GtkWindow* parent)
{
    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        _("Select ZIP archive"), parent, GTK_FILE_CHOOSER_ACTION_OPEN,
        _("_Cancel"), GTK_RESPONSE_CANCEL,
        _("_Open"), GTK_RESPONSE_ACCEPT, nullptr);

    GtkFileFilter* zip_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(zip_filter, _("ZIP archive (*.zip)"));
    gtk_file_filter_add_pattern(zip_filter, "*.zip");
    gtk_file_filter_add_pattern(zip_filter, "*.ZIP");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), zip_filter);

    GtkFileFilter* all_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(all_filter, _("All files (*.*)"));
    gtk_file_filter_add_pattern(all_filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), all_filter);

    std::string filename;
    if (runDialog(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GFile* file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
        if (file) {
            char* path = g_file_get_path(file);
            if (path) {
                filename = path;
                g_free(path);
            }
            g_object_unref(file);
        }
    }
    destroyWidget(dialog);
    return filename;
}

void on_button_clicked_select_package(GtkWidgetHelper helper)
{
    GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
    const std::string filename = choose_zip_archive(parent);
    if (filename.empty()) {
        return;
    }
    helper.setEntryText(helper.getWidget("auto_task_pkg_path"), filename);
}


struct PackagePartitionCheckResult {
    std::vector<AutomatedTaskPartitionRow> rows;
    std::vector<PackagePartitionCheckEntry> entries;
    std::size_t device_count = 0;
    std::size_t missing = 0;
    std::size_t mismatch = 0;
    bool device_available = false;
};

// 逐个检查包内分区：check_partition 返回 0 视为不存在，否则与包声明大小比较。
PackagePartitionCheckResult check_package_partitions(
    const std::vector<PackagePartitionSpec>& specs)
{
    PackagePartitionCheckResult result;
    const bool has_primary_table =
        io != nullptr && io->ptable != nullptr && io->part_count > 0;
    const bool has_compat_table =
        io != nullptr && io->Cptable != nullptr && io->part_count_c > 0;
    result.device_available = io != nullptr && m_bOpened > 0 &&
                              (has_primary_table || has_compat_table);
    if (result.device_available) {
        result.device_count = static_cast<std::size_t>(
            has_primary_table ? io->part_count : io->part_count_c);
    }

    for (const auto& spec : specs) {
        const std::uint64_t expected = parse_package_size_bytes(spec.size);

        AutomatedTaskPartitionRow row;
        row.name = spec.name;
        row.size = format_size_bytes(expected);
        row.raw_name = spec.name;

        PackagePartitionCheckEntry entry;
        entry.name = spec.name;
        entry.package_size = row.size;

        if (!result.device_available) {
            row.device_size = "-";
            entry.device_size = "-";
        } else {
            const std::uint64_t actual =
                check_partition(io, spec.name.c_str(), 1);
            if (actual == 0) {
                ++result.missing;
                entry.kind = PackagePartitionCheckKind::MissingOnDevice;
                row.device_size = "-";
                entry.device_size = "-";
            } else {
                row.device_size = format_size_bytes(actual);
                entry.device_size = row.device_size;
                if (actual != expected) {
                    ++result.mismatch;
                    entry.kind = PackagePartitionCheckKind::SizeMismatch;
                }
            }
        }

        result.rows.push_back(row);
        result.entries.push_back(entry);
    }
    return result;
}

void on_button_clicked_load_package(GtkWidgetHelper helper)
{
    GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
    const char* path = helper.getEntryText(helper.getWidget("auto_task_pkg_path"));
    const std::string archive = (path && *path) ? path : "";
    if (archive.empty()) {
        showErrorDialogSyncInThread(parent, _("Error"), _("Please select a ZIP archive first."));
        return;
    }

    GtkWidget* startBtn = helper.getWidget("auto_task_start");
    const bool was_start_enabled = startBtn && gtk_widget_get_sensitive(startBtn);
    helper.disableWidget("auto_task_start");
    helper.disableWidget("auto_task_pkg_load");

    std::thread([helper, parent, archive, was_start_enabled]() {
        auto reenable_load_button = [helper]() {
            gui_idle_call([helper]() mutable {
                helper.enableWidget("auto_task_pkg_load");
            });
        };
        auto restore_start_on_cancel = [helper, was_start_enabled]() {
            gui_idle_call([helper, was_start_enabled]() mutable {
                helper.setWidgetSensitive("auto_task_start", was_start_enabled);
            });
        };

        try {
            showInfoDialogSyncInThread(parent, _("Info"), _("Please select a folder to extract the ZIP archive."));
            const std::string extract_dir = showFolderChooserSyncInThread(parent);
            if (extract_dir.empty()) {
                restore_start_on_cancel();
                reenable_load_button();
                return;
            }

            PackageManifest manifest = read_package_manifest(archive);

            const bool has_partitions = !manifest.partitions.empty();
            PackagePartitionCheckResult check;
            if (has_partitions) {
                check = check_package_partitions(manifest.partitions);
            }

            const bool compatibility_ok =
                !has_partitions ||
                (check.device_available && check.missing == 0 && check.mismatch == 0);

            // 不兼容或无法验证时拒绝使用该包：不解压，也不启用 Start。
            if (compatibility_ok) {
                zipxx::extract_zip(archive, extract_dir);
            }

            const AutomatedTaskPackageInfo info = manifest.info;
            std::vector<AutomatedTaskItem> tasks;
            tasks.reserve(manifest.actions.size());
            for (const auto& action : manifest.actions) {
                AutomatedTaskItem item;
                item.name = action.is_string() ? action.get<std::string>()
                                               : action.dump();
                item.selected = true;
                tasks.push_back(item);
            }

            std::vector<AutomatedTaskPartitionRow> partition_rows;
            if (has_partitions) {
                partition_rows = check.rows;
            }

            gui_idle_call([helper, info, tasks, partition_rows, compatibility_ok]() mutable {
                automated_task_set_package_info(helper, info);
                automated_task_set_tasks(helper, tasks);
                automated_task_set_partitions(helper, partition_rows);
                helper.setWidgetSensitive("auto_task_start", compatibility_ok);
            });

            if (has_partitions && !check.device_available) {
                showWarningDialogSyncInThread(
                    parent, _("Warning"),
                    _("Cannot verify package compatibility: device is not connected or the partition table is empty."));
            } else if (has_partitions && !compatibility_ok) {
                gui_idle_call_wait_drag(
                    [parent, entries = check.entries, device_count = check.device_count,
                     package_count = manifest.partitions.size(),
                     missing = check.missing, mismatch = check.mismatch]() {
                        show_package_partition_check_dialog(
                            parent, entries, device_count, package_count,
                            missing, mismatch);
                    }, parent);
            } else {
                showInfoDialogSyncInThread(parent, _("Info"),
                                           _("ZIP archive extracted successfully."));
            }
        } catch (const std::exception& e) {
            showErrorDialogSyncInThread(parent, _("Error"), e.what());
        } catch (...) {
            showErrorDialogSyncInThread(parent, _("Error"),
                                        _("Failed to extract ZIP archive."));
        }
        reenable_load_button();
    }).detach();
}

void on_button_clicked_create_example_zip(GtkWidgetHelper helper)
{
    GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
    const std::string save_path = showSaveFileDialog(
        parent, "example.zip", {{_("ZIP archive (*.zip)"), "*.zip"}});
    if (save_path.empty()) {
        return;
    }

    try {
        nlohmann::json manifest = {
            {"name", "example"},
            {"author", "Ryan"},
            {"version", "0.0.0"},
            {"contact", "ryan110413@outlook.com"},
            {"type", "single_act"},
            {"actions", nlohmann::json::array({"blk_size 60000"})},
            {"partitions", nlohmann::json::array({
                nlohmann::json::array({"boot", "35"}),
                nlohmann::json::array({"recovery", "35"}),
            })},
        };

        // 写入条目只有在显式 close()（或析构）后才会真正落盘。
        zipxx::Archive archive(save_path, zipxx::Archive::Mode::Create);
        archive.add_file("package.json", manifest.dump(2));
        archive.close();

        showInfoDialog(parent, _("Info"),
                       _("Example ZIP archive created successfully."));
    } catch (const std::exception& e) {
        showErrorDialog(parent, _("Error"), e.what());
    }
}

void on_button_clicked_start(GtkWidgetHelper helper)
{
    const char* path = helper.getEntryText(helper.getWidget("auto_task_pkg_path"));
    const std::vector<std::string> tasks = automated_task_selected_tasks(helper);
    DEG_LOG(I, "Automated task start requested: package=%s, selected_tasks=%zu",
            (path && *path) ? path : "", tasks.size());
    // TODO: 接入自动化任务执行逻辑。
}

} // namespace

void automated_task_set_package_info(GtkWidgetHelper& helper,
                                     const AutomatedTaskPackageInfo& info)
{
    set_info_value(helper, "auto_task_pkg_name_value", info.name);
    set_info_value(helper, "auto_task_author_value", info.author);
    set_info_value(helper, "auto_task_version_value", info.version);
    set_info_value(helper, "auto_task_contact_value", info.contact);
    set_info_value(helper, "auto_task_type_value", info.type);
}

void automated_task_clear_package_info(GtkWidgetHelper& helper)
{
    automated_task_set_package_info(helper, AutomatedTaskPackageInfo{});
}

void automated_task_set_partitions(GtkWidgetHelper& helper,
                                   const std::vector<AutomatedTaskPartitionRow>& rows)
{
    GtkWidget* view = helper.getWidget("auto_task_partition_list");
    if (!view || !GTK_IS_COLUMN_VIEW(view)) {
        return;
    }

    sfd_row_view_clear(view);
    auto cell_text = [](const std::string& value) {
        return (value.empty() || value == kUnknown) ? std::string(_("Unknown")) : value;
    };
    int index = 1;
    for (const auto& row : rows) {
        const std::string display_name = std::to_string(index++) + ". " + row.name;
        const std::string size = cell_text(row.size);
        const std::string raw_name = cell_text(row.raw_name);
        const std::string device_size = cell_text(row.device_size);
        SfdRow* sfd_row = sfd_row_new();
        sfd_row_set_cell(sfd_row, 0, display_name.c_str());
        sfd_row_set_cell(sfd_row, 1, size.c_str());
        sfd_row_set_cell(sfd_row, 2, raw_name.c_str());
        sfd_row_set_cell(sfd_row, 3, device_size.c_str());
        sfd_row_view_add(view, sfd_row);
    }
}

void automated_task_set_tasks(GtkWidgetHelper& helper,
                              const std::vector<AutomatedTaskItem>& tasks)
{
    GtkWidget* view = helper.getWidget("auto_task_list");
    if (!view || !GTK_IS_COLUMN_VIEW(view)) {
        return;
    }

    sfd_row_view_clear(view);
    for (const auto& task : tasks) {
        SfdRow* row = sfd_row_new();
        sfd_row_set_checked(row, task.selected);
        sfd_row_set_cell(row, 1, task.name.c_str());
        sfd_row_view_add(view, row);
    }
}

std::vector<std::string> automated_task_selected_tasks(GtkWidgetHelper& helper)
{
    std::vector<std::string> selected;
    GtkWidget* view = helper.getWidget("auto_task_list");
    if (!view || !GTK_IS_COLUMN_VIEW(view)) {
        return selected;
    }

    const guint count = sfd_row_view_count(view);
    for (guint i = 0; i < count; ++i) {
        SfdRow* row = sfd_row_view_get(view, i);
        if (!row) {
            continue;
        }
        if (sfd_row_get_checked(row)) {
            const char* name = sfd_row_get_cell(row, 1);
            if (name && *name) {
                selected.emplace_back(name);
            }
        }
        g_object_unref(row);
    }
    return selected;
}

GtkWidget* AutomatedTaskPage::init(GtkWidgetHelper& helper, GtkWidget* notebook)
{
    // 页面根容器：左分区列表 + 右操作列。
    GtkWidget* pageRoot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_name(pageRoot, "auto_task_page");
    gtk_widget_set_hexpand(pageRoot, TRUE);
    gtk_widget_set_vexpand(pageRoot, TRUE);
    gtk_widget_set_margin_start(pageRoot, 16);
    gtk_widget_set_margin_end(pageRoot, 16);
    gtk_widget_set_margin_top(pageRoot, 10);
    gtk_widget_set_margin_bottom(pageRoot, 10);

    helper.addWidget("auto_task_page", pageRoot);
    helper.addNotebookPage(notebook, pageRoot, _("Automated task"));

    // ── 左侧：分区列表 ──────────────────────────────────────
    GtkWidget* listFrame = make_titled_frame(_("Partition List"), "auto_task_partition_title");
    helper.addWidget("auto_task_partition_title",
                     gtk_frame_get_label_widget(GTK_FRAME(listFrame)));
    gtk_widget_set_hexpand(listFrame, TRUE);
    gtk_widget_set_vexpand(listFrame, TRUE);

    GtkWidget* listCard = make_card_box(8, 8);
    gtk_frame_set_child(GTK_FRAME(listFrame), listCard);

    GtkWidget* listScroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(listScroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(listScroll, TRUE);
    gtk_widget_set_vexpand(listScroll, TRUE);

    GtkWidget* partitionView = sfd_row_view_new(TRUE);
    gtk_widget_set_name(partitionView, "auto_task_partition_list");
    helper.addWidget("auto_task_partition_list", partitionView);
    sfd_row_view_add_text_column(partitionView, _("Partition Name"), 0, TRUE);
    sfd_row_view_add_text_column(partitionView, _("Size"), 1, TRUE);
    sfd_row_view_add_text_column(partitionView, _("Type"), 2, TRUE);
    sfd_row_view_add_text_column(partitionView, _("Device Size"), 3, TRUE);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(listScroll), partitionView);
    gtk_box_append(GTK_BOX(listCard), listScroll);
    gtk_box_append(GTK_BOX(pageRoot), listFrame);

    // ── 右侧：操作列 ────────────────────────────────────────
    GtkWidget* rightScroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(rightScroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(rightScroll, 440, -1);
    gtk_widget_set_hexpand(rightScroll, FALSE);
    gtk_widget_set_vexpand(rightScroll, TRUE);
    gtk_widget_set_valign(rightScroll, GTK_ALIGN_FILL);

    GtkWidget* rightBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(rightBox, 4);
    gtk_widget_set_margin_end(rightBox, 4);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(rightScroll), rightBox);
    gtk_box_append(GTK_BOX(pageRoot), rightScroll);

    // 1. Package File Path
    GtkWidget* fileFrame = make_titled_frame(_("Package File Path"), "auto_task_pkg_title");
    helper.addWidget("auto_task_pkg_title",
                     gtk_frame_get_label_widget(GTK_FRAME(fileFrame)));
    GtkWidget* fileCard = make_card_box(10, 10);
    gtk_frame_set_child(GTK_FRAME(fileFrame), fileCard);

    GtkWidget* fileRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* pathEntry = gtk_entry_new();
    gtk_widget_set_name(pathEntry, "auto_task_pkg_path");
    gtk_widget_set_hexpand(pathEntry, TRUE);
    gtk_widget_set_size_request(pathEntry, 240, 32);
    helper.addWidget("auto_task_pkg_path", pathEntry);

    GtkWidget* selectBtn = gtk_button_new_with_label("...");
    gtk_widget_set_name(selectBtn, "auto_task_pkg_select");
    gtk_widget_set_size_request(selectBtn, 40, 32);
    helper.addWidget("auto_task_pkg_select", selectBtn);

    GtkWidget* loadBtn = gtk_button_new_with_label(_("Load"));
    gtk_widget_set_name(loadBtn, "auto_task_pkg_load");
    gtk_widget_set_size_request(loadBtn, 80, 32);
    helper.addWidget("auto_task_pkg_load", loadBtn);

    gtk_box_append(GTK_BOX(fileRow), pathEntry);
    gtk_box_append(GTK_BOX(fileRow), selectBtn);
    gtk_box_append(GTK_BOX(fileRow), loadBtn);
    gtk_box_append(GTK_BOX(fileCard), fileRow);

    GtkWidget* createExampleBtn = gtk_button_new_with_label(_("Create Example ZIP"));
    gtk_widget_set_name(createExampleBtn, "auto_task_create_example");
    gtk_widget_set_size_request(createExampleBtn, -1, 32);
    gtk_widget_set_hexpand(createExampleBtn, TRUE);
    helper.addWidget("auto_task_create_example", createExampleBtn);
    gtk_box_append(GTK_BOX(fileCard), createExampleBtn);

    gtk_box_append(GTK_BOX(rightBox), fileFrame);

    // 2. Package Info
    GtkWidget* infoFrame = make_titled_frame(_("Package Info"), "auto_task_info_title");
    helper.addWidget("auto_task_info_title",
                     gtk_frame_get_label_widget(GTK_FRAME(infoFrame)));
    GtkWidget* infoCard = make_card_box(10, 10);
    gtk_frame_set_child(GTK_FRAME(infoFrame), infoCard);

    GtkWidget* infoGrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(infoGrid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(infoGrid), 12);
    gtk_widget_set_hexpand(infoGrid, TRUE);
    gtk_box_append(GTK_BOX(infoCard), infoGrid);

    auto append_info_row = [&helper, infoGrid](int row, const char* label_text,
                                                const char* value_widget_name) {
        GtkWidget* label = gtk_label_new(label_text);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_widget_set_valign(label, GTK_ALIGN_CENTER);

        GtkWidget* value = gtk_label_new(_("Unknown"));
        gtk_widget_set_name(value, value_widget_name);
        gtk_widget_set_halign(value, GTK_ALIGN_START);
        gtk_widget_set_valign(value, GTK_ALIGN_CENTER);
        gtk_widget_set_hexpand(value, TRUE);
        gtk_label_set_selectable(GTK_LABEL(value), TRUE);
        helper.addWidget(value_widget_name, value);

        gtk_grid_attach(GTK_GRID(infoGrid), label, 0, row, 1, 1);
        gtk_grid_attach(GTK_GRID(infoGrid), value, 1, row, 1, 1);
    };

    append_info_row(0, _("Package name"), "auto_task_pkg_name_value");
    append_info_row(1, _("Author"), "auto_task_author_value");
    append_info_row(2, _("Version"), "auto_task_version_value");
    append_info_row(3, _("Contact"), "auto_task_contact_value");
    append_info_row(4, _("Type"), "auto_task_type_value");

    gtk_box_append(GTK_BOX(rightBox), infoFrame);

    // 3. Task Selection
    GtkWidget* taskFrame = make_titled_frame(_("Task Selection"), "auto_task_task_title");
    helper.addWidget("auto_task_task_title",
                     gtk_frame_get_label_widget(GTK_FRAME(taskFrame)));
    GtkWidget* taskCard = make_card_box(10, 10);
    gtk_frame_set_child(GTK_FRAME(taskFrame), taskCard);

    GtkWidget* taskScroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(taskScroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(taskScroll, -1, 150);
    gtk_widget_set_hexpand(taskScroll, TRUE);

    GtkWidget* taskView = sfd_row_view_new(FALSE);
    gtk_widget_set_name(taskView, "auto_task_list");
    helper.addWidget("auto_task_list", taskView);
    sfd_row_view_add_toggle_column(taskView, _("Select"));
    sfd_row_view_add_text_column(taskView, _("Task Name"), 1, FALSE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(taskScroll), taskView);

    gtk_box_append(GTK_BOX(taskCard), taskScroll);
    gtk_box_append(GTK_BOX(rightBox), taskFrame);

    // 4. Start
    GtkWidget* startBtn = gtk_button_new_with_label(_("Start"));
    gtk_widget_set_name(startBtn, "auto_task_start");
    gtk_widget_set_size_request(startBtn, -1, 40);
    gtk_widget_set_hexpand(startBtn, TRUE);
    gtk_widget_set_margin_top(startBtn, 4);
    gtk_widget_set_sensitive(startBtn, FALSE);
    helper.addWidget("auto_task_start", startBtn);
    gtk_box_append(GTK_BOX(rightBox), startBtn);

    // 初始状态：所有包信息为 Unknown。
    automated_task_clear_package_info(helper);
    automated_task_set_partitions(helper, {});
    automated_task_set_tasks(helper, {});

    gtk_widget_set_visible(pageRoot, TRUE);
    return pageRoot;
}

void AutomatedTaskPage::bindSignals(GtkWidgetHelper& helper)
{
    GtkWidget* selectBtn = helper.getWidget("auto_task_pkg_select");
    if (selectBtn) {
        helper.bindClick(selectBtn, [helper]() {
            on_button_clicked_select_package(helper);
        });
    }

    GtkWidget* loadBtn = helper.getWidget("auto_task_pkg_load");
    if (loadBtn) {
        helper.bindClick(loadBtn, [helper]() {
            on_button_clicked_load_package(helper);
        });
    }

    GtkWidget* createExampleBtn = helper.getWidget("auto_task_create_example");
    if (createExampleBtn) {
        helper.bindClick(createExampleBtn, [helper]() {
            on_button_clicked_create_example_zip(helper);
        });
    }

    GtkWidget* startBtn = helper.getWidget("auto_task_start");
    if (startBtn) {
        helper.bindClick(startBtn, [helper]() {
            on_button_clicked_start(helper);
        });
    }
}

GtkWidget* create_automated_task_page(GtkWidgetHelper& helper, GtkWidget* notebook)
{
    AutomatedTaskPage page;
    return page.init(helper, notebook);
}

void bind_automated_task_signals(GtkWidgetHelper& helper)
{
    AutomatedTaskPage page;
    page.bindSignals(helper);
}
