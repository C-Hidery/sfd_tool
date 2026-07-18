/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#include "page_pac_flash.h"
#include "../core/app_state.h"
#include "../core/usb_transport.h"
#include "../core/config_service.h"
#include "../core/pac_extract.h"  // FindFDLInExtFloder & Stages
#include "../i18n.h"
#include "ui/ui_common.h"
#include <string>
#include <thread>
#include <cstdio>
#include <vector>

extern AppState g_app_state;
extern spdio_t*& io;



// 当用户点击复选框时，切换存储中的布尔值
static void on_cell_toggled(GtkCellRendererToggle *renderer,
                            gchar *path_str,
                            gpointer user_data)
{
    (void)renderer;
    GtkTreeModel *model = GTK_TREE_MODEL(user_data);
    GtkTreePath *path = gtk_tree_path_new_from_string(path_str);
    GtkTreeIter iter;

    if (gtk_tree_model_get_iter(model, &iter, path)) {
        gboolean old_value = FALSE;
        gtk_tree_model_get(model, &iter, 0, &old_value, -1);
        gtk_list_store_set(GTK_LIST_STORE(model), &iter, 0, !old_value, -1);
    }

    gtk_tree_path_free(path);
}

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

std::vector<std::string> getSelectedPartitions(GtkWidgetHelper helper)
{
    std::vector<std::string> selected;

    GtkWidget* treeView = helper.getWidget("pac_list");
    if (!treeView) return selected;

    GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(treeView));
    if (!model) return selected;

    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(model, &iter);
    while (valid) {
        gboolean is_selected = FALSE;
        gtk_tree_model_get(model, &iter, 0, &is_selected, -1);
        if (is_selected) {
            gchar* partition_name = NULL;
            gtk_tree_model_get(model, &iter, 1, &partition_name, -1);
            if (partition_name) {
                selected.push_back(std::string(partition_name));
                g_free(partition_name);
            }
        }
        valid = gtk_tree_model_iter_next(model, &iter);
    }

    return selected;
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
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("File does not exist."));
		}, GTK_WINDOW(helper.getWidget("main_window")));
		return;
	}

	bool i_is = pac_extract(pac_path, "pac_unpack_output");
	if (i_is)
	{
		gui_idle_call_wait_drag([helper]() {
			showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Success"), _("PAC unpacked successfully."));
		}, GTK_WINDOW(helper.getWidget("main_window")));
	}
	else
	{
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("Failed to unpack PAC."));
		}, GTK_WINDOW(helper.getWidget("main_window")));
		return;
	}

}

void on_button_clicked_pac_flash_start(GtkWidgetHelper helper) {
	ensure_device_attached_or_exit(helper);
	pac_flash(io, "pac_unpack_output");
}

// ===== UI 构建 =====

GtkWidget* PacFlashPage::init(GtkWidgetHelper& helper, GtkWidget* notebook) {
    GtkWidget* outerScroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(outerScroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(outerScroll, TRUE);
    gtk_widget_set_vexpand(outerScroll, TRUE);
    helper.addWidget("pac_flash_page", outerScroll, "scrolledwindow");

    GtkWidget* mainBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_hexpand(mainBox, TRUE);
	gtk_widget_set_vexpand(mainBox, TRUE);
    gtk_widget_set_margin_start(mainBox, 16);
    gtk_widget_set_margin_end(mainBox, 16);
    gtk_widget_set_margin_top(mainBox, 10);
    gtk_widget_set_margin_bottom(mainBox, 10);
    helper.addWidget("mainBox", mainBox, "box");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(outerScroll), mainBox);

    helper.addNotebookPage(notebook, outerScroll, _("PAC Flash"));

    auto makeCardBox = [](int pad_h, int pad_v) -> GtkWidget* {
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gtk_widget_set_margin_start(box, pad_h);
        gtk_widget_set_margin_end(box, pad_h);
        gtk_widget_set_margin_top(box, pad_v);
        gtk_widget_set_margin_bottom(box, pad_v);
        return box;
    };

    // ---- 选择 PAC 文件 ----
    GtkWidget* fileFrame = gtk_frame_new(NULL);
    gtk_widget_set_margin_bottom(fileFrame, 16);
    GtkWidget* fileTitleLabel = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(fileTitleLabel), (std::string("<span size='large'><b>") + _("PAC File") + "</b></span>").c_str());
    helper.addWidget("pac_file_title_label", fileTitleLabel, "label");
    gtk_frame_set_label_widget(GTK_FRAME(fileFrame), fileTitleLabel);
    gtkFrameSetLabelAlign(fileFrame, 0.5, 0.5);

    GtkWidget* fileCardBox = makeCardBox(12, 10);
    helper.addWidget("fileCardBox", fileCardBox, "box");
    gtkContainerAdd(fileFrame, fileCardBox);

    GtkWidget* fileRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    helper.addWidget("fileRow", fileRow, "box");

    GtkWidget* pacFileLabel = gtk_label_new(_("PAC File Path"));
    helper.addWidget("pac_file_label", pacFileLabel, "label");

    GtkWidget* pacFilePath = gtk_entry_new();
    gtk_widget_set_hexpand(pacFilePath, TRUE);
    helper.addWidget("pac_file_path", pacFilePath, "entry");

    GtkWidget* pacSelectBtn = gtk_button_new_with_label("...");
    gtk_widget_set_size_request(pacSelectBtn, 40, 32);
    helper.addWidget("pac_select", pacSelectBtn, "button");

    GtkWidget* pacUnpackBtn = gtk_button_new_with_label(_("Unpack PAC"));
    gtk_widget_set_size_request(pacUnpackBtn, -1, 32);
    helper.addWidget("pac_unpack", pacUnpackBtn, "button");

    gtkBoxPackStart(fileRow, pacFileLabel, FALSE, FALSE, 0);
    gtkBoxPackStart(fileRow, pacFilePath, TRUE, TRUE, 0);
    gtkBoxPackStart(fileRow, pacSelectBtn, FALSE, FALSE, 0);
    gtkBoxPackStart(fileRow, pacUnpackBtn, FALSE, FALSE, 0);
    gtkBoxPackStart(fileCardBox, fileRow, FALSE, FALSE, 0);

    gtkBoxPackStart(mainBox, fileFrame, FALSE, FALSE, 0);

    // ---- 分区列表 ----
    GtkWidget* listTitle = gtk_label_new(_("Please check partitions to flash"));
    helper.addWidget("pac_list_title", listTitle, "label");
    gtk_widget_set_halign(listTitle, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_bottom(listTitle, 6);
    gtkBoxPackStart(mainBox, listTitle, FALSE, FALSE, 0);

    GtkWidget* listScroll = gtk_scrolled_window_new();
    gtk_widget_set_size_request(listScroll, -1, 260);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(listScroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_add_css_class(listScroll, "frame");
    gtk_widget_set_hexpand(listScroll, TRUE);
	gtk_widget_set_vexpand(listScroll, TRUE);
    helper.addWidget("listScroll", listScroll, "scrolledwindow");

    // ListStore with 4 columns
    GtkListStore* p_store = gtk_list_store_new(4, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget* p_treeView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(p_store));
    g_object_unref(p_store);
    gtk_widget_set_name(p_treeView, "pac_list");
    helper.addWidget("pac_list", p_treeView, "treeview");

    GtkCellRenderer* p_toggle_renderer = gtk_cell_renderer_toggle_new();
    g_signal_connect(p_toggle_renderer, "toggled", G_CALLBACK(on_cell_toggled), p_store);
    GtkTreeViewColumn* p_toggle_column = gtk_tree_view_column_new_with_attributes(_("Select"), p_toggle_renderer, "active", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(p_treeView), p_toggle_column);

    GtkCellRenderer* p_renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(p_treeView), -1, _("Partition Name"), p_renderer, "text", 1, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(p_treeView), -1, _("Size"), p_renderer, "text", 2, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(p_treeView), -1, _("Type"), p_renderer, "text", 3, NULL);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(listScroll), p_treeView);
    gtkBoxPackStart(mainBox, listScroll, FALSE, FALSE, 0);

    // ---- Flash Operation ----
    GtkWidget* flashFrame = gtk_frame_new(NULL);
    gtk_widget_set_margin_top(flashFrame, 16);
    gtk_widget_set_margin_bottom(flashFrame, 16);
    GtkWidget* flashTitleLabel = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(flashTitleLabel), (std::string("<span size='large'><b>") + _("Flash Operation") + "</b></span>").c_str());
    helper.addWidget("pac_flash_op_label", flashTitleLabel, "label");
    gtk_frame_set_label_widget(GTK_FRAME(flashFrame), flashTitleLabel);
    gtkFrameSetLabelAlign(flashFrame, 0.5, 0.5);

    GtkWidget* flashCardBox = makeCardBox(12, 10);
    helper.addWidget("flashCardBox", flashCardBox, "box");
    gtkContainerAdd(flashFrame, flashCardBox);

    GtkWidget* abDescLabel = gtk_label_new(_("Select slot mode:"));
    helper.addWidget("pac_ab_desc_label", abDescLabel, "label");
    gtk_widget_set_halign(abDescLabel, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(abDescLabel, 6);
    gtkBoxPackStart(flashCardBox, abDescLabel, FALSE, FALSE, 0);

    GtkWidget* abpart_auto = gtk_button_new_with_label(_("Not VAB (Non-AB)"));
    helper.addWidget("abpart_auto", abpart_auto, "button");
    GtkWidget* abpart_a = gtk_button_new_with_label(_("Slot A"));
    helper.addWidget("abpart_a", abpart_a, "button");
    GtkWidget* abpart_b = gtk_button_new_with_label(_("Slot B"));
    helper.addWidget("abpart_b", abpart_b, "button");

    GtkWidget* abRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_bottom(abRow, 10);
    gtkBoxPackStart(abRow, abpart_auto, TRUE, TRUE, 0);
    gtkBoxPackStart(abRow, abpart_a, TRUE, TRUE, 0);
    gtkBoxPackStart(abRow, abpart_b, TRUE, TRUE, 0);
    gtkBoxPackStart(flashCardBox, abRow, FALSE, FALSE, 0);

    GtkWidget* pacFlashBtn = gtk_button_new_with_label(_("START PAC Flash"));
    gtk_widget_set_hexpand(pacFlashBtn, TRUE);
    helper.addWidget("pac_flash_start", pacFlashBtn, "button");
    gtkBoxPackStart(flashCardBox, pacFlashBtn, TRUE, TRUE, 0);

    gtkBoxPackStart(mainBox, flashFrame, FALSE, FALSE, 0);

    // 返回 outerScroll
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
