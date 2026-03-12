#include "page_manual.h"
#include "../common.h"
#include "../main.h"
#include "../i18n.h"
#include "../core/flash_service.h"
#include "../ui_common.h"
#include <thread>

extern spdio_t*& io;
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

static void on_button_clicked_m_select(GtkWidgetHelper helper) {
	GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
	std::string filename = showFileChooser(parent, true);
	if (!filename.empty()) {
		helper.setEntryText(helper.getWidget("m_file_path"), filename);
	}
}

static void on_button_clicked_m_write(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_(("Error")))), _("Device unattached, exiting..."));
			exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));

	}
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
	FILE* fi;
	fi = oxfopen(filename.c_str(), "r");
	if (fi == nullptr) {
		DEG_LOG(E, "File does not exist.\n");
		return;
	} else fclose(fi);

	sfd::PartitionIoOptions opts;
	opts.partition_name = part_name;
	opts.file_path = filename;
	opts.block_size = blk_size;
	opts.force = false;

	LongTaskConfig cfg{
		helper,
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
		[&helper]() {
			helper.setLabelText(helper.getWidget("con"), "Writing partition");
		},
		// on_finished：GUI 线程中执行，恢复状态
		[&helper]() {
			helper.setLabelText(helper.getWidget("con"), "Ready");
		}
	};

	run_long_task(cfg);
}

static void on_button_clicked_m_read(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_(("Error")))), _("Device unattached, exiting..."));
			exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));

	}
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

	sfd::PartitionIoOptions opts;
	opts.partition_name = part_name;
	opts.file_path = savePath;
	opts.block_size = blk_size;

	LongTaskConfig cfg{
		helper,
		// worker：在后台线程中执行分区读取
		[parent, helper, opts](std::atomic_bool& cancel_flag) {
			(void)cancel_flag; // 当前实现暂不支持取消
			auto* svc = ensure_flash_service();
			sfd::FlashStatus st = svc->readPartitionToFile(opts);
			gui_idle_call_wait_drag([parent, helper, st]() mutable {
				if (!st.success) {
					showErrorDialog(GTK_WINDOW(parent), _(_(_(("Error")))), st.message.c_str());
				} else {
					showInfoDialog(GTK_WINDOW(parent), _(_(_(("Completed")))), _("Partition read completed!"));
				}
			},GTK_WINDOW(helper.getWidget("main_window")));
		},
		// on_started：GUI 线程中执行，设置状态
		[&helper]() {
			helper.setLabelText(helper.getWidget("con"), "Reading partition");
		},
		// on_finished：GUI 线程中执行，恢复状态
		[&helper]() {
			helper.setLabelText(helper.getWidget("con"), "Ready");
		}
	};

	run_long_task(cfg);
}

static void on_button_clicked_m_erase(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_(("Error")))), _("Device unattached, exiting..."));
			exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));

	}
	GtkWidget *parent = helper.getWidget("main_window");
	std::string part_name = helper.getEntryText(helper.getWidget("m_part_erase"));
	if (part_name.empty()) {
		showErrorDialog(GTK_WINDOW(parent), _(_(_(("Error")))), _("No partition name specified!"));
		return;
	}

	LongTaskConfig cfg{
		helper,
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
		[&helper]() {
			helper.setLabelText(helper.getWidget("con"), "Erase partition");
		},
		// on_finished：GUI 线程中执行，恢复状态
		[&helper]() {
			helper.setLabelText(helper.getWidget("con"), "Ready");
		}
	};

	run_long_task(cfg);
}

static void on_button_clicked_m_cancel(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_(("Error")))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));

	}
	signal_handler(0);
	showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Tips"), _("Current partition operation cancelled!"));
}

GtkWidget* ManualPage::init(GtkWidgetHelper& helper, GtkWidget* notebook) {
	GtkWidget* manualPage = helper.createGrid("manual_page", 5, 5);
	helper.addNotebookPage(notebook, manualPage, _("Manually Operate"));

	GtkWidget* manualScroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(manualScroll),
	                               GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_hexpand(manualScroll, TRUE);
	gtk_widget_set_vexpand(manualScroll, TRUE);

	GtkWidget* mainBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
	gtk_widget_set_margin_start(mainBox, 40);
	gtk_widget_set_margin_end(mainBox, 40);
	gtk_widget_set_margin_top(mainBox, 20);
	gtk_widget_set_margin_bottom(mainBox, 20);
	gtk_widget_set_halign(mainBox, GTK_ALIGN_CENTER);
	gtk_widget_set_size_request(mainBox, 600, -1);

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
	gtk_frame_set_label_align(GTK_FRAME(writeFrame), 0.5, 0.5);
	helper.addWidget("write_label", writeTitle);

	GtkWidget* writeCardBox = makeCardBox(32, 16);
	gtk_container_add(GTK_CONTAINER(writeFrame), writeCardBox);

	// 分区名
	GtkWidget* writePartBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
	GtkWidget* writePartLabel = gtk_label_new(_("Partition name:"));
	helper.addWidget("write_part_label", writePartLabel);
	gtk_widget_set_halign(writePartLabel, GTK_ALIGN_END);
	gtk_widget_set_size_request(writePartLabel, 120, -1);

	GtkWidget* mPartFlash = helper.createEntry("m_part_flash", "", false, 0, 0, -1, 32);
	gtk_widget_set_hexpand(mPartFlash, TRUE);
	gtk_box_pack_start(GTK_BOX(writePartBox), writePartLabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(writePartBox), mPartFlash, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(writeCardBox), writePartBox, FALSE, FALSE, 0);

	// 镜像文件路径
	GtkWidget* filePathBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
	GtkWidget* filePathLabel = gtk_label_new(_("Image file path:"));
	helper.addWidget("file_path_label", filePathLabel);
	gtk_widget_set_halign(filePathLabel, GTK_ALIGN_END);
	gtk_widget_set_size_request(filePathLabel, 120, -1);

	GtkWidget* fileInputBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(fileInputBox), "linked");
	gtk_widget_set_hexpand(fileInputBox, TRUE);

	GtkWidget* mFilePath = helper.createEntry("m_file_path", "", false, 0, 0, -1, 32);
	gtk_widget_set_hexpand(mFilePath, TRUE);
	gtk_editable_set_editable(GTK_EDITABLE(mFilePath), FALSE);
	GtkWidget* mSelectBtn = helper.createButton("...", "m_select", nullptr, 0, 0, 48, 32);

	gtk_box_pack_start(GTK_BOX(fileInputBox), mFilePath, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(fileInputBox), mSelectBtn, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(filePathBox), filePathLabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(filePathBox), fileInputBox, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(writeCardBox), filePathBox, FALSE, FALSE, 0);

	// 刷写按钮
	GtkWidget* mWriteBtn = helper.createButton(_("WRITE"), "m_write", nullptr, 0, 0, -1, 36);
	gtk_widget_set_margin_top(mWriteBtn, 8);
	gtk_box_pack_start(GTK_BOX(writeCardBox), mWriteBtn, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(mainBox), writeFrame, FALSE, FALSE, 0);

	// ══════════════════════════════════════════════
	//  卡片 2：读取分区 (Extract)
	// ══════════════════════════════════════════════
	GtkWidget* readFrame = gtk_frame_new(NULL);
	GtkWidget* readTitle = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(readTitle),
	    (std::string("<b>") + _("Extract partition") + "</b>").c_str());
	gtk_widget_set_halign(readTitle, GTK_ALIGN_CENTER);
	gtk_frame_set_label_widget(GTK_FRAME(readFrame), readTitle);
	gtk_frame_set_label_align(GTK_FRAME(readFrame), 0.5, 0.5);
	helper.addWidget("extract_label", readTitle);

	GtkWidget* readCardBox = makeCardBox(32, 16);
	gtk_container_add(GTK_CONTAINER(readFrame), readCardBox);

	// 分区名
	GtkWidget* readPartBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
	GtkWidget* extractPartLabel = gtk_label_new(_("Partition name:"));
	helper.addWidget("extract_part_label", extractPartLabel);
	gtk_widget_set_halign(extractPartLabel, GTK_ALIGN_END);
	gtk_widget_set_size_request(extractPartLabel, 120, -1);

	GtkWidget* mPartRead = helper.createEntry("m_part_read", "", false, 0, 0, -1, 32);
	gtk_widget_set_hexpand(mPartRead, TRUE);

	gtk_box_pack_start(GTK_BOX(readPartBox), extractPartLabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(readPartBox), mPartRead, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(readCardBox), readPartBox, FALSE, FALSE, 0);

	// 读取分区按钮
	GtkWidget* mReadBtn = helper.createButton(_("EXTRACT"), "m_read", nullptr, 0, 0, -1, 36);
	gtk_widget_set_margin_top(mReadBtn, 8);
	gtk_box_pack_start(GTK_BOX(readCardBox), mReadBtn, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(mainBox), readFrame, FALSE, FALSE, 0);

	// ══════════════════════════════════════════════
	//  卡片 3：擦除分区 (Erase)
	// ══════════════════════════════════════════════
	GtkWidget* eraseFrame = gtk_frame_new(NULL);
	GtkWidget* eraseTitle = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(eraseTitle),
	    (std::string("<b>") + _("Erase partition") + "</b>").c_str());
	gtk_widget_set_halign(eraseTitle, GTK_ALIGN_CENTER);
	gtk_frame_set_label_widget(GTK_FRAME(eraseFrame), eraseTitle);
	gtk_frame_set_label_align(GTK_FRAME(eraseFrame), 0.5, 0.5);
	helper.addWidget("erase_label", eraseTitle);

	GtkWidget* eraseCardBox = makeCardBox(32, 16);
	gtk_container_add(GTK_CONTAINER(eraseFrame), eraseCardBox);

	// 分区名
	GtkWidget* erasePartBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
	GtkWidget* erasePartLabel = gtk_label_new(_("Partition name:"));
	helper.addWidget("erase_part_label", erasePartLabel);
	gtk_widget_set_halign(erasePartLabel, GTK_ALIGN_END);
	gtk_widget_set_size_request(erasePartLabel, 120, -1);

	GtkWidget* mPartErase = helper.createEntry("m_part_erase", "", false, 0, 0, -1, 32);
	gtk_widget_set_hexpand(mPartErase, TRUE);

	gtk_box_pack_start(GTK_BOX(erasePartBox), erasePartLabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(erasePartBox), mPartErase, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(eraseCardBox), erasePartBox, FALSE, FALSE, 0);

	// 擦除分区按钮
	GtkWidget* mEraseBtn = helper.createButton(_("ERASE"), "m_erase", nullptr, 0, 0, -1, 36);
	gtk_widget_set_margin_top(mEraseBtn, 8);
	gtk_box_pack_start(GTK_BOX(eraseCardBox), mEraseBtn, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(mainBox), eraseFrame, FALSE, FALSE, 0);

	// ══════════════════════════════════════════════
	//  底部取消按钮
	// ══════════════════════════════════════════════
	GtkWidget* mCancelBtn = helper.createButton(_("Cancel"), "m_cancel", nullptr, 0, 0, -1, 36);
	gtk_widget_set_margin_top(mCancelBtn, 8);
	gtk_box_pack_start(GTK_BOX(mainBox), mCancelBtn, FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(manualScroll), mainBox);
	helper.addToGrid(manualPage, manualScroll, 0, 0, 5, 5);

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
