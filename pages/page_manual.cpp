/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#include "page_manual.h"
#include "../common.h"
#include "../main.h"
#include "../i18n.h"
#include "ui/ui_common.h"
#include "../core/flash_service.h"
#include "ui/ui_common.h"
#include <thread>

extern spdio_t*& io;
extern int& m_bOpened;
extern int blk_size;
extern AppState g_app_state;

namespace {
void packBoxChild(GtkWidget* box, GtkWidget* child, bool expand, bool fill, int padding) {
#if GTK_CHECK_VERSION(4, 0, 0)
    (void)expand;
    (void)fill;
    (void)padding;
    gtk_box_append(GTK_BOX(box), child);
#else
    gtk_box_pack_start(GTK_BOX(box), child, expand, fill, padding);
#endif
}

void setFrameChild(GtkWidget* frame, GtkWidget* child) {
#if GTK_CHECK_VERSION(4, 0, 0)
    gtk_frame_set_child(GTK_FRAME(frame), child);
#else
    gtk_container_add(GTK_CONTAINER(frame), child);
#endif
}

void setFrameLabelAlign(GtkWidget* frame, float xalign, float yalign) {
#if GTK_CHECK_VERSION(4, 0, 0)
    (void)yalign;
    gtk_frame_set_label_align(GTK_FRAME(frame), xalign);
#else
    gtk_frame_set_label_align(GTK_FRAME(frame), xalign, yalign);
#endif
}
}

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

static void on_button_clicked_m_select(GtkWidgetHelper helper) {
	GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
	std::string filename = showFileChooser(parent, true);
	if (!filename.empty()) {
		helper.setEntryText(helper.getWidget("m_file_path"), filename);
	}
}

static void on_button_clicked_m_write(GtkWidgetHelper helper) {
	ensure_device_attached_or_exit(helper);
	GtkWidget *parent = helper.getWidget("main_window");
	std::string filename = helper.getEntryText(helper.getWidget("m_file_path"));
	std::string part_name = helper.getEntryText(helper.getWidget("m_part_flash"));
	if (filename.empty()) {
		showErrorDialog(GTK_WINDOW(parent), _(_(_(("Error")))), _("No partition image file selected!"));
		return;
	}
	if (part_name.empty()) {
		showErrorDialog(GTK_WINDOW(parent), _(_(_(("Error")))), _("No partition name specified!"));
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
	opts.block_size = blk_size;
	opts.force = false;

	LongTaskConfig cfg{
		// worker：在后台线程中执行分区写入
		[parent, helper, opts](std::atomic_bool& cancel_flag) {
			(void)cancel_flag; // 当前实现暂不支持取消
			auto* svc = ensure_flash_service();
			sfd::FlashStatus st = svc->writePartitionFromFile(opts);
			gui_idle_call_wait_drag([parent, helper, st]() mutable {
				if (!st.success) {
					showErrorDialog(GTK_WINDOW(parent), _(_(_(("Error")))), st.message.c_str());
				} else {
					showInfoDialog(GTK_WINDOW(parent), _(_(_(("Completed")))), _("Partition write completed!"));
				}
			},GTK_WINDOW(helper.getWidget("main_window")));
		},
		// on_started：GUI 线程中执行，设置状态
		[helper]() mutable {
			helper.setLabelText(helper.getWidget("con"), "Writing partition");
		},
		// on_finished：GUI 线程中执行，恢复状态
		[helper]() mutable {
			helper.setLabelText(helper.getWidget("con"), "Ready");
		}
	};

	run_long_task(cfg);
}

static void on_button_clicked_m_read(GtkWidgetHelper helper) {
	ensure_device_attached_or_exit(helper);
	GtkWidget *parent = helper.getWidget("main_window");
	std::string part_name = helper.getEntryText(helper.getWidget("m_part_read"));
	std::string savePath = showSaveFileDialog(GTK_WINDOW(parent), part_name + ".img");
	if (savePath.empty()) {
		showErrorDialog(GTK_WINDOW(parent), _(_(_(("Error")))), _("No save path selected!"));
		return;
	}
	if (part_name.empty()) {
		showErrorDialog(GTK_WINDOW(parent), _(_(_(("Error")))), _("No partition name specified!"));
		return;
	}

	auto& settings = GetGuiIoSettings();
	LogBlkState("manual m_read enter");

	sfd::PartitionIoOptions opts;
	opts.partition_name = part_name;
	opts.file_path = savePath;
	DEG_LOG(I, "[blk] m_read GUI part=%s", opts.partition_name.c_str());

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

	LongTaskConfig cfg{
		// worker：在后台线程中执行分区读取（统一走 PartitionReadService）
		[parent, helper, opts, cb](std::atomic_bool& cancel_flag) {
			(void)cancel_flag; // 当前实现暂不支持取消
			sfd::PartitionReadInfo info{};
			info.name = opts.partition_name;
			sfd::PartitionReadOptions read_opts{};
			read_opts.output_path = opts.file_path;
			read_opts.block_cfg = MakeBlockSizeConfigFromGui();
			auto* svc = ensure_flash_service();
			sfd::FlashStatus st = svc->partitionReader().readOne(info, read_opts, &cb);
			gui_idle_call_wait_drag([parent, helper, st]() mutable {
				if (!st.success) {
					showErrorDialog(GTK_WINDOW(parent), _(_(_((("Error"))))), st.message.c_str());
				} else {
					showInfoDialog(GTK_WINDOW(parent), _(_(_((("Completed"))))), _("Partition read completed!"));
				}
			},GTK_WINDOW(helper.getWidget("main_window")));
		},
		// on_started：GUI 线程中执行，设置状态
		[helper]() mutable {
			helper.setLabelText(helper.getWidget("con"), "Reading partition");
		},
		// on_finished：GUI 线程中执行，恢复状态
		[helper]() mutable {
			helper.setLabelText(helper.getWidget("con"), "Ready");
		}
	};

	run_long_task(cfg);
}

static void on_button_clicked_m_erase(GtkWidgetHelper helper) {
	ensure_device_attached_or_exit(helper);
	GtkWidget *parent = helper.getWidget("main_window");
	std::string part_name = helper.getEntryText(helper.getWidget("m_part_erase"));
	if (part_name.empty()) {
		showErrorDialog(GTK_WINDOW(parent), _(_(_(("Error")))), _("No partition name specified!"));
		return;
	}

	LongTaskConfig cfg{
		// worker：在后台线程中执行分区擦除
		[parent, helper, part_name](std::atomic_bool& cancel_flag) {
			(void)cancel_flag; // 当前实现暂不支持取消
			auto* svc = ensure_flash_service();
			sfd::FlashStatus st = svc->erasePartition(part_name);
			gui_idle_call_wait_drag([parent, helper, st]() mutable{
				if (!st.success) {
					showErrorDialog(GTK_WINDOW(parent), _(_(_(("Error")))), st.message.c_str());
				} else {
					showInfoDialog(GTK_WINDOW(parent), _(_(_(("Completed")))), _("Partition erase completed!"));
				}
			},GTK_WINDOW(helper.getWidget("main_window")));
		},
		// on_started：GUI 线程中执行，设置状态
		[helper]() mutable {
			helper.setLabelText(helper.getWidget("con"), "Erase partition");
		},
		// on_finished：GUI 线程中执行，恢复状态
		[helper]() mutable {
			helper.setLabelText(helper.getWidget("con"), "Ready");
		}
	};

	run_long_task(cfg);
}

static void on_button_clicked_m_cancel(GtkWidgetHelper helper) {
	ensure_device_attached_or_exit(helper);
	signal_handler(0);
	showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Tips"), _("Current partition operation cancelled!"));
}

GtkWidget* ManualPage::init(GtkWidgetHelper& helper, GtkWidget* notebook) {
    // 创建页面顶层 Grid
    GtkWidget* manualPage = gtk_grid_new();
    gtk_widget_set_name(manualPage, "manual_page");
    helper.addWidget("manual_page", manualPage);
    helper.addNotebookPage(notebook, manualPage, _("Manually Operate"));

    // 外层滚动窗口
    GtkWidget* manualScroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(manualScroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(manualScroll, TRUE);
    gtk_widget_set_vexpand(manualScroll, TRUE);

    // 主垂直盒子
    GtkWidget* mainBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(mainBox, 40);
    gtk_widget_set_margin_end(mainBox, 40);
    gtk_widget_set_margin_top(mainBox, 20);
    gtk_widget_set_margin_bottom(mainBox, 20);
    gtk_widget_set_halign(mainBox, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(mainBox, 520, -1);

    // 卡片辅助函数
    auto makeCardBox = [](int pad_h, int pad_v) -> GtkWidget* {
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
        gtk_widget_set_margin_start(box, pad_h);
        gtk_widget_set_margin_end(box, pad_h);
        gtk_widget_set_margin_top(box, pad_v);
        gtk_widget_set_margin_bottom(box, pad_v);
        return box;
    };

    // ══════════════════════════════════════════════
    //  卡片 1：刷写分区 (Write)
    // ══════════════════════════════════════════════
    GtkWidget* writeFrame = gtk_frame_new(NULL);
    GtkWidget* writeTitle = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(writeTitle),
        (std::string("<b>") + _("Write partition") + "</b>").c_str());
    gtk_widget_set_halign(writeTitle, GTK_ALIGN_CENTER);
    gtk_frame_set_label_widget(GTK_FRAME(writeFrame), writeTitle);
    gtkFrameSetLabelAlign(writeFrame, 0.5, 0.5);
    helper.addWidget("write_label", writeTitle);

    GtkWidget* writeCardBox = makeCardBox(32, 16);
    gtk_frame_set_child(GTK_FRAME(writeFrame), writeCardBox);

    // 分区名行
    GtkWidget* writePartBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    GtkWidget* writePartLabel = gtk_label_new(_("Partition name:"));
    helper.addWidget("write_part_label", writePartLabel);
    gtk_widget_set_halign(writePartLabel, GTK_ALIGN_END);
    gtk_widget_set_size_request(writePartLabel, 120, -1);

    GtkWidget* mPartFlash = gtk_entry_new();
    gtk_widget_set_name(mPartFlash, "m_part_flash");
    gtk_widget_set_hexpand(mPartFlash, TRUE);
    gtk_widget_set_size_request(mPartFlash, -1, 32);
    helper.addWidget("m_part_flash", mPartFlash);

    gtk_box_append(GTK_BOX(writePartBox), writePartLabel);
    gtk_box_append(GTK_BOX(writePartBox), mPartFlash);
    gtk_box_append(GTK_BOX(writeCardBox), writePartBox);

    // 镜像文件路径行
    GtkWidget* filePathBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    GtkWidget* filePathLabel = gtk_label_new(_("Image file path:"));
    helper.addWidget("file_path_label", filePathLabel);
    gtk_widget_set_halign(filePathLabel, GTK_ALIGN_END);
    gtk_widget_set_size_request(filePathLabel, 120, -1);

    GtkWidget* fileInputBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(fileInputBox), "linked");
    gtk_widget_set_hexpand(fileInputBox, TRUE);

    GtkWidget* mFilePath = gtk_entry_new();
    gtk_widget_set_name(mFilePath, "m_file_path");
    gtk_widget_set_hexpand(mFilePath, TRUE);
    gtk_widget_set_size_request(mFilePath, -1, 32);
    gtk_editable_set_editable(GTK_EDITABLE(mFilePath), FALSE);
    helper.addWidget("m_file_path", mFilePath);

    GtkWidget* mSelectBtn = gtk_button_new_with_label("...");
    gtk_widget_set_name(mSelectBtn, "m_select");
    gtk_widget_set_size_request(mSelectBtn, 48, 32);
    helper.addWidget("m_select", mSelectBtn);

    gtk_box_append(GTK_BOX(fileInputBox), mFilePath);
    gtk_box_append(GTK_BOX(fileInputBox), mSelectBtn);

    gtk_box_append(GTK_BOX(filePathBox), filePathLabel);
    gtk_box_append(GTK_BOX(filePathBox), fileInputBox);
    gtk_box_append(GTK_BOX(writeCardBox), filePathBox);

    // 刷写按钮
    GtkWidget* mWriteBtn = gtk_button_new_with_label(_("WRITE"));
    gtk_widget_set_name(mWriteBtn, "m_write");
    gtk_widget_set_size_request(mWriteBtn, -1, 36);
    gtk_widget_set_margin_top(mWriteBtn, 8);
    helper.addWidget("m_write", mWriteBtn);
    gtk_box_append(GTK_BOX(writeCardBox), mWriteBtn);

    gtk_box_append(GTK_BOX(mainBox), writeFrame);

    // ══════════════════════════════════════════════
    //  卡片 2：读取分区 (Extract)
    // ══════════════════════════════════════════════
    GtkWidget* readFrame = gtk_frame_new(NULL);
    GtkWidget* readTitle = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(readTitle),
        (std::string("<b>") + _("Extract partition") + "</b>").c_str());
    gtk_widget_set_halign(readTitle, GTK_ALIGN_CENTER);
    gtk_frame_set_label_widget(GTK_FRAME(readFrame), readTitle);
    gtkFrameSetLabelAlign(readFrame, 0.5, 0.5);
    helper.addWidget("extract_label", readTitle);

    GtkWidget* readCardBox = makeCardBox(32, 16);
    gtk_frame_set_child(GTK_FRAME(readFrame), readCardBox);

    // 分区名行
    GtkWidget* readPartBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    GtkWidget* extractPartLabel = gtk_label_new(_("Partition name:"));
    helper.addWidget("extract_part_label", extractPartLabel);
    gtk_widget_set_halign(extractPartLabel, GTK_ALIGN_END);
    gtk_widget_set_size_request(extractPartLabel, 120, -1);

    GtkWidget* mPartRead = gtk_entry_new();
    gtk_widget_set_name(mPartRead, "m_part_read");
    gtk_widget_set_hexpand(mPartRead, TRUE);
    gtk_widget_set_size_request(mPartRead, -1, 32);
    helper.addWidget("m_part_read", mPartRead);

    gtk_box_append(GTK_BOX(readPartBox), extractPartLabel);
    gtk_box_append(GTK_BOX(readPartBox), mPartRead);
    gtk_box_append(GTK_BOX(readCardBox), readPartBox);

    // 读取按钮
    GtkWidget* mReadBtn = gtk_button_new_with_label(_("EXTRACT"));
    gtk_widget_set_name(mReadBtn, "m_read");
    gtk_widget_set_size_request(mReadBtn, -1, 36);
    gtk_widget_set_margin_top(mReadBtn, 8);
    helper.addWidget("m_read", mReadBtn);
    gtk_box_append(GTK_BOX(readCardBox), mReadBtn);

    gtk_box_append(GTK_BOX(mainBox), readFrame);

    // ══════════════════════════════════════════════
    //  卡片 3：擦除分区 (Erase)
    // ══════════════════════════════════════════════
    GtkWidget* eraseFrame = gtk_frame_new(NULL);
    GtkWidget* eraseTitle = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(eraseTitle),
        (std::string("<b>") + _("Erase partition") + "</b>").c_str());
    gtk_widget_set_halign(eraseTitle, GTK_ALIGN_CENTER);
    gtk_frame_set_label_widget(GTK_FRAME(eraseFrame), eraseTitle);
    gtkFrameSetLabelAlign(eraseFrame, 0.5, 0.5);
    helper.addWidget("erase_label", eraseTitle);

    GtkWidget* eraseCardBox = makeCardBox(32, 16);
    gtk_frame_set_child(GTK_FRAME(eraseFrame), eraseCardBox);

    // 分区名行
    GtkWidget* erasePartBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    GtkWidget* erasePartLabel = gtk_label_new(_("Partition name:"));
    helper.addWidget("erase_part_label", erasePartLabel);
    gtk_widget_set_halign(erasePartLabel, GTK_ALIGN_END);
    gtk_widget_set_size_request(erasePartLabel, 120, -1);

    GtkWidget* mPartErase = gtk_entry_new();
    gtk_widget_set_name(mPartErase, "m_part_erase");
    gtk_widget_set_hexpand(mPartErase, TRUE);
    gtk_widget_set_size_request(mPartErase, -1, 32);
    helper.addWidget("m_part_erase", mPartErase);

    gtk_box_append(GTK_BOX(erasePartBox), erasePartLabel);
    gtk_box_append(GTK_BOX(erasePartBox), mPartErase);
    gtk_box_append(GTK_BOX(eraseCardBox), erasePartBox);

    // 擦除按钮
    GtkWidget* mEraseBtn = gtk_button_new_with_label(_("ERASE"));
    gtk_widget_set_name(mEraseBtn, "m_erase");
    gtk_widget_set_size_request(mEraseBtn, -1, 36);
    gtk_widget_set_margin_top(mEraseBtn, 8);
    helper.addWidget("m_erase", mEraseBtn);
    gtk_box_append(GTK_BOX(eraseCardBox), mEraseBtn);

    gtk_box_append(GTK_BOX(mainBox), eraseFrame);

    // ══════════════════════════════════════════════
    //  底部取消按钮
    // ══════════════════════════════════════════════
    GtkWidget* mCancelBtn = gtk_button_new_with_label(_("Cancel"));
    gtk_widget_set_name(mCancelBtn, "m_cancel");
    gtk_widget_set_size_request(mCancelBtn, -1, 36);
    gtk_widget_set_margin_top(mCancelBtn, 8);
    helper.addWidget("m_cancel", mCancelBtn);
    gtk_box_append(GTK_BOX(mainBox), mCancelBtn);

    // 将 mainBox 放入滚动窗口
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(manualScroll), mainBox);
    // 将滚动窗口放入 manualPage 网格
    gtk_grid_attach(GTK_GRID(manualPage), manualScroll, 0, 0, 5, 5);

    return manualPage;
}

void ManualPage::bindSignals(GtkWidgetHelper& helper) {
	helper.bindClick(helper.getWidget("m_select"), [&]() {
		on_button_clicked_m_select(helper);
	});
	helper.bindClick(helper.getWidget("m_write"), [&]() {
		on_button_clicked_m_write(helper);
	});
	helper.bindClick(helper.getWidget("m_read"), [&]() {
		on_button_clicked_m_read(helper);
	});
	helper.bindClick(helper.getWidget("m_erase"), [&]() {
		on_button_clicked_m_erase(helper);
	});
	helper.bindClick(helper.getWidget("m_cancel"), [&]() {
		on_button_clicked_m_cancel(helper);
	});
}

GtkWidget* create_manual_page(GtkWidgetHelper& helper, GtkWidget* notebook) {
    ManualPage page;
    return page.init(helper, notebook);
}

void bind_manual_signals(GtkWidgetHelper& helper) {
    ManualPage page;
    page.bindSignals(helper);
}
