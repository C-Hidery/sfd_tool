#include "page_pac_flash.h"
#include "../core/app_state.h"
#include "../core/usb_transport.h"
#include "../core/config_service.h"
#include "../core/pac_extract.h"  // FindFDLInExtFloder & Stages
#include "../i18n.h"
#include "../ui_common.h"
#include <string>
#include <thread>
#include <cstdio>

extern AppState g_app_state;
extern spdio_t*& io;



static std::string format_size(std::uint64_t bytes) {
	// 非常简单的格式化：优先用 MB，否则用 KB
	const double kb = 1024.0;
	const double mb = kb * 1024.0;
	char buf[64] = {0};
	if (bytes >= static_cast<std::uint64_t>(mb)) {
		std::snprintf(buf, sizeof(buf), "%.1f MB", bytes / mb);
	} else if (bytes >= static_cast<std::uint64_t>(kb)) {
		std::snprintf(buf, sizeof(buf), "%.0f KB", bytes / kb);
	} else {
		std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
	}
	return std::string(buf);
}

static void populate_pac_partition_list(GtkWidgetHelper& helper, const std::vector<sfd::PacPartitionEntry>& entries) {
	GtkWidget* tree = helper.getWidget("pac_list");
	if (!tree) {
		return;
	}
	GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
	GtkListStore* store = GTK_LIST_STORE(model);
	if (!store) {
		return;
	}

	gtk_list_store_clear(store);

	for (const auto& e : entries) {
		GtkTreeIter iter;
		gtk_list_store_append(store, &iter);

		std::string size_str;
		if (e.image_size > 0) {
			size_str = format_size(e.image_size);
		} else {
			size_str = "-";
		}

		std::string type_str;
		if (e.critical) {
			type_str = "critical";
		} else {
			type_str = "normal";
		}

		gtk_list_store_set(store, &iter,
		                   0, e.name.c_str(),
		                   1, size_str.c_str(),
		                   2, type_str.c_str(),
		                   -1);
	}
}

// ===== 按钮回调函数 =====


void on_button_clicked_abpart_auto(GtkWidgetHelper helper) {
	(void)helper;
	g_app_state.flash.selected_ab = 0;
}

void on_button_clicked_abpart_a(GtkWidgetHelper helper) {
	(void)helper;
	g_app_state.flash.selected_ab = 1;
}

void on_button_clicked_abpart_b(GtkWidgetHelper helper) {
	(void)helper;
	g_app_state.flash.selected_ab = 2;
}

void on_button_clicked_pac_select(GtkWidgetHelper helper) {
	GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
	std::string filename = showFileChooser(parent, true);
	if (!filename.empty()) {
		helper.setEntryText(helper.getWidget("pac_file_path"), filename);
	}
}

void on_button_clicked_pac_unpack(GtkWidgetHelper helper) {
	ensure_device_attached_or_exit(helper);
	const char* pac_path = helper.getEntryText(helper.getWidget("pac_file_path"));
	if (!pac_path || !*pac_path) {
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("File does not exist.\n文件不存在！"));
		}, GTK_WINDOW(helper.getWidget("main_window")));
		return;
	}

	bool i_is = pac_extract(pac_path, "pac_unpack_output");
	if (i_is)
	{
		gui_idle_call_wait_drag([helper]() {
			showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Success"), _("PAC unpacked successfully.\nPAC文件解包成功。"));
		}, GTK_WINDOW(helper.getWidget("main_window")));
	}
	else
	{
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("Failed to unpack PAC.\nPAC文件解包失败。"));
		}, GTK_WINDOW(helper.getWidget("main_window")));
		return;
	}

}

void on_button_clicked_pac_flash_start(GtkWidgetHelper helper) {
	ensure_device_attached_or_exit(helper);
	bool i_is = pac_flash(io, "pac_unpack_output");
	if (i_is)
	{
		gui_idle_call_wait_drag([helper]() {
			showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Success"), _("PAC flashed successfully. this tool will exit.\nPAC文件刷写成功, 工具即将退出。"));
		}, GTK_WINDOW(helper.getWidget("main_window")));
	}
	else
	{
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("Failed to flash PAC.\nPAC文件刷写失败。"));
		}, GTK_WINDOW(helper.getWidget("main_window")));
		return;
	}
	std::thread([](){
#ifndef _WIN32
    	sleep(5);
#else
    	Sleep(5000);
#endif
		exit(0);
	}).detach();
}

// ===== UI 构建 =====

GtkWidget* PacFlashPage::init(GtkWidgetHelper& helper, GtkWidget* notebook) {
	// ── 可滚动外层容器 ──
	GtkWidget* outerScroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(outerScroll),
	                               GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_hexpand(outerScroll, TRUE);
	gtk_widget_set_vexpand(outerScroll, TRUE);

	GtkWidget* mainBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_margin_start(mainBox, 16);
	gtk_widget_set_margin_end(mainBox, 16);
	gtk_widget_set_margin_top(mainBox, 10);
	gtk_widget_set_margin_bottom(mainBox, 10);
	gtk_container_add(GTK_CONTAINER(outerScroll), mainBox);

	helper.addWidget("pac_flash_page", outerScroll);
	helper.addNotebookPage(notebook, outerScroll, _("PAC Flash"));

	// ── 卡片辅助 lambda ──
	auto makeCardBox = [](int pad_h, int pad_v) -> GtkWidget* {
		GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
		gtk_widget_set_margin_start(box, pad_h);
		gtk_widget_set_margin_end(box, pad_h);
		gtk_widget_set_margin_top(box, pad_v);
		gtk_widget_set_margin_bottom(box, pad_v);
		return box;
	};

	//  第一部分：选择 PAC 文件
	GtkWidget* fileFrame = gtk_frame_new(NULL);
	gtk_widget_set_margin_bottom(fileFrame, 16);
	GtkWidget* fileTitleLabel = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(fileTitleLabel),
	    (std::string("<span size='large'><b>") + _("PAC File") + "</b></span>").c_str());
	helper.addWidget("pac_file_title_label", fileTitleLabel);
	gtk_frame_set_label_widget(GTK_FRAME(fileFrame), fileTitleLabel);
	gtk_frame_set_label_align(GTK_FRAME(fileFrame), 0.5, 0.5);

	GtkWidget* fileCardBox = makeCardBox(12, 10);
	gtk_container_add(GTK_CONTAINER(fileFrame), fileCardBox);

	GtkWidget* fileRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget* pacFileLabel = gtk_label_new(_("PAC File Path"));
	helper.addWidget("pac_file_label", pacFileLabel);
	GtkWidget* pacFilePath  = helper.createEntry("pac_file_path", "", false, 0, 0, 360, 32);

	// 从配置中恢复最近使用的 PAC 路径（如果有）
	auto cfgSvc = sfd::createConfigService();
	if (cfgSvc) {
		sfd::AppConfig cfg{};
		sfd::ConfigStatus status = cfgSvc->loadAppConfig(cfg);
		if (status.success && !cfg.last_pac_path.empty()) {
			helper.setEntryText(pacFilePath, cfg.last_pac_path.c_str());
		}
	}

	GtkWidget* pacSelectBtn = helper.createButton("...", "pac_select", nullptr, 0, 0, 40, 32);
	GtkWidget* pacUnpackBtn = helper.createButton(_("Unpack PAC"), "pac_unpack", nullptr, 0, 0, -1, 32);
	gtk_box_pack_start(GTK_BOX(fileRow), pacFileLabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(fileRow), pacFilePath,  TRUE,  TRUE,  0);
	gtk_box_pack_start(GTK_BOX(fileRow), pacSelectBtn, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(fileRow), pacUnpackBtn, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(fileCardBox), fileRow, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(mainBox), fileFrame, FALSE, FALSE, 0);

	//  第二部分：PAC 分区列表
	GtkWidget* listTitle = gtk_label_new(_("Please select a partition"));
	helper.addWidget("pac_list_title", listTitle);
	gtk_widget_set_halign(listTitle, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_bottom(listTitle, 6);
	gtk_box_pack_start(GTK_BOX(mainBox), listTitle, FALSE, FALSE, 0);

	GtkWidget* listScroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_widget_set_size_request(listScroll, -1, 260);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(listScroll),
	                               GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(listScroll), GTK_SHADOW_IN);
	gtk_widget_set_hexpand(listScroll, TRUE);

	GtkListStore* p_store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	GtkWidget* p_treeView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(p_store));
	gtk_widget_set_name(p_treeView, "pac_list");
	helper.addWidget("pac_list", p_treeView);
	GtkCellRenderer* p_renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(p_treeView), -1,
	    _("Partition Name"), p_renderer, "text", 0, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(p_treeView), -1,
	    _("Size"), p_renderer, "text", 1, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(p_treeView), -1,
	    _("Type"), p_renderer, "text", 2, NULL);
	gtk_container_add(GTK_CONTAINER(listScroll), p_treeView);
	gtk_box_pack_start(GTK_BOX(mainBox), listScroll, FALSE, FALSE, 0);

	//  第三部分：烧录操作卡片
	GtkWidget* flashFrame = gtk_frame_new(NULL);
	gtk_widget_set_margin_top(flashFrame, 16);
	gtk_widget_set_margin_bottom(flashFrame, 16);
	GtkWidget* flashTitleLabel = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(flashTitleLabel),
	    (std::string("<span size='large'><b>") + _("Flash Operation") + "</b></span>").c_str());
	helper.addWidget("pac_flash_op_label", flashTitleLabel);
	gtk_frame_set_label_widget(GTK_FRAME(flashFrame), flashTitleLabel);
	gtk_frame_set_label_align(GTK_FRAME(flashFrame), 0.5, 0.5);

	GtkWidget* flashCardBox = makeCardBox(12, 10);
	gtk_container_add(GTK_CONTAINER(flashFrame), flashCardBox);

	GtkWidget* abDescLabel = gtk_label_new(_("Select slot mode:"));
	helper.addWidget("pac_ab_desc_label", abDescLabel);
	gtk_widget_set_halign(abDescLabel, GTK_ALIGN_START);
	gtk_widget_set_margin_bottom(abDescLabel, 6);
	gtk_box_pack_start(GTK_BOX(flashCardBox), abDescLabel, FALSE, FALSE, 0);

	GtkWidget* abpart_auto = helper.createButton(_("Not VAB (Non-AB)"), "abpart_auto", nullptr, 0, 0, -1, 32);
	GtkWidget* abpart_a    = helper.createButton(_("Slot A"),           "abpart_a",    nullptr, 0, 0, -1, 32);
	GtkWidget* abpart_b    = helper.createButton(_("Slot B"),           "abpart_b",    nullptr, 0, 0, -1, 32);
	GtkWidget* abRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_margin_bottom(abRow, 10);
	gtk_box_pack_start(GTK_BOX(abRow), abpart_auto, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(abRow), abpart_a,    TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(abRow), abpart_b,    TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(flashCardBox), abRow, FALSE, FALSE, 0);

	GtkWidget* pacFlashBtn = helper.createButton(_("START PAC Flash"), "pac_flash_start", nullptr, 0, 0, -1, 36);
	gtk_widget_set_hexpand(pacFlashBtn, TRUE);
	gtk_box_pack_start(GTK_BOX(flashCardBox), pacFlashBtn, TRUE, TRUE, 0);

	gtk_box_pack_start(GTK_BOX(mainBox), flashFrame, FALSE, FALSE, 0);

	gtk_widget_show_all(outerScroll);
	return outerScroll;
}

void PacFlashPage::bindSignals(GtkWidgetHelper& helper) {
	GtkWidget* pacSelectBtn = helper.getWidget("pac_select");
	if (pacSelectBtn) {
		helper.bindClick(pacSelectBtn, [helper]() {
			on_button_clicked_pac_select(helper);
		});
	}
	GtkWidget* pacUnpackBtn = helper.getWidget("pac_unpack");
	if (pacUnpackBtn) {
		helper.bindClick(pacUnpackBtn, [helper]() {
			on_button_clicked_pac_unpack(helper);
		});
	}
	GtkWidget* pacFlashBtn = helper.getWidget("pac_flash_start");
	if (pacFlashBtn) {
		helper.bindClick(pacFlashBtn, [helper]() {
			on_button_clicked_pac_flash_start(helper);
		});
	}
	GtkWidget* abpart_auto = helper.getWidget("abpart_auto");
	if (abpart_auto) {
		helper.bindClick(abpart_auto, [helper]() {
			on_button_clicked_abpart_auto(helper);
		});
	}
	GtkWidget* abpart_a = helper.getWidget("abpart_a");
	if (abpart_a) {
		helper.bindClick(abpart_a, [helper]() {
			on_button_clicked_abpart_a(helper);
		});
	}
	GtkWidget* abpart_b = helper.getWidget("abpart_b");
	if (abpart_b) {
		helper.bindClick(abpart_b, [helper]() {
			on_button_clicked_abpart_b(helper);
		});
	}
}

// 保持原有对外接口
GtkWidget* create_pac_flash_page(GtkWidgetHelper& helper, GtkWidget* notebook) {
	PacFlashPage page;
	return page.init(helper, notebook);
}

void bind_pac_flash_signals(GtkWidgetHelper& helper) {
	PacFlashPage page;
	page.bindSignals(helper);
}
