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
#include "ui/gtk_row_view.hpp"
#include <string>
#include <thread>
#include <cstdio>
#include <vector>

#include "page_connect.h"

extern AppState g_app_state;
extern spdio_t*& io;



std::vector<std::string> getSelectedPartitions(GtkWidgetHelper& helper)
{
    std::vector<std::string> selected;

    GtkWidget* view = helper.getWidget("pac_list");
    if (!view || !GTK_IS_COLUMN_VIEW(view)) return selected;

    const guint n = sfd_row_view_count(view);
    for (guint i = 0; i < n; ++i)
    {
        SfdRow* row = sfd_row_view_get(view, i);
        if (!row) continue;
        if (sfd_row_get_checked(row))
        {
            const char* partition_name = sfd_row_get_cell(row, 3); // 原始分区名
            if (partition_name) selected.emplace_back(partition_name);
        }
        g_object_unref(row);
    }

    return selected;
}
// ===== 按钮回调函数 =====
void on_button_clicked_pac_select(GtkWidgetHelper helper) {
	GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
	std::string filename = showFileChooser(parent, true);
	if (!filename.empty()) {
		helper.setEntryText(helper.getWidget("pac_file_path"), filename);
		auto cfgSvc = ensure_config_service();
    	if (cfgSvc)
    	{
    		sfd::AppConfig cfg{};
    		sfd::ConfigStatus status = cfgSvc->loadAppConfig(cfg);
    		if (status.success)
    		{
    			cfg.last_pac_path = filename;
    			cfgSvc->saveAppConfig(cfg);
    		}
    	}
	}
}
bool isUnpacked = false;
void on_button_clicked_pac_unpack(GtkWidgetHelper helper) {
	ensure_device_attached_or_exit(helper);
	const char* pac_path = helper.getEntryText(helper.getWidget("pac_file_path"));
	if (!pac_path || !*pac_path) {
		showErrorDialogSyncInThread(GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("File does not exist."));
		return;
	}
	showInfoDialogSyncInThread(GTK_WINDOW(helper.getWidget("main_window")), _("Info"), _("Please select a folder to unpack the PAC file."));
	g_app_state.flash.pac_folder = showFolderChooserSyncInThread(GTK_WINDOW(helper.getWidget("main_window")));
	if (g_app_state.flash.pac_folder.empty()) {
		showErrorDialogSyncInThread(GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("No folder selected."));
		return;
	}
	std::thread([pac_path, helper]()
	{
		bool i_is = pac_extract(pac_path, g_app_state.flash.pac_folder.c_str());
		if (i_is)
		{
			gui_idle_call_wait_drag([helper]() {
				showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Success"), _("PAC unpacked successfully."));
			}, GTK_WINDOW(helper.getWidget("main_window")));
			isUnpacked = true;
		}
		else
		{
			gui_idle_call_wait_drag([helper]() {
				showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("Failed to unpack PAC."));
			}, GTK_WINDOW(helper.getWidget("main_window")));
			return;
		}
	}).detach();
}

void on_button_clicked_pac_flash_start(GtkWidgetHelper helper) {
	ensure_device_attached_or_exit(helper);
	if (!isUnpacked)
	{
		showErrorDialogSyncInThread(GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("Please unpack the PAC file first."));
		return;
	}
	g_app_state.flash.pacptable = getSelectedPartitions(helper);
	pac_flash(io, g_app_state.flash.pac_folder.c_str());
}

// ===== UI 构建 =====

GtkWidget* PacFlashPage::init(GtkWidgetHelper& helper, GtkWidget* notebook) {
    // ── 可滚动外层容器 ──
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
    gtkFrameSetLabelAlign(fileFrame, 0.5, 0.5);

    GtkWidget* fileCardBox = makeCardBox(12, 10);
    gtk_frame_set_child(GTK_FRAME(fileFrame), fileCardBox);

    GtkWidget* fileRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    GtkWidget* pacFileLabel = gtk_label_new(_("PAC File Path"));
    helper.addWidget("pac_file_label", pacFileLabel);

    GtkWidget* pacFilePath = gtk_entry_new();
    gtk_widget_set_name(pacFilePath, "pac_file_path");
    gtk_widget_set_hexpand(pacFilePath, TRUE);
    gtk_widget_set_size_request(pacFilePath, 360, 32);
    helper.addWidget("pac_file_path", pacFilePath);

    // 从配置中恢复最近使用的 PAC 路径（如果有）
    auto cfgSvc = sfd::createConfigService();
    if (cfgSvc) {
        sfd::AppConfig cfg{};
        sfd::ConfigStatus status = cfgSvc->loadAppConfig(cfg);
        if (status.success && !cfg.last_pac_path.empty()) {
            gtk_editable_set_text(GTK_EDITABLE(pacFilePath), cfg.last_pac_path.c_str());
        }
    }

    GtkWidget* pacSelectBtn = gtk_button_new_with_label("...");
    gtk_widget_set_name(pacSelectBtn, "pac_select");
    gtk_widget_set_size_request(pacSelectBtn, 40, 32);
    helper.addWidget("pac_select", pacSelectBtn);

    GtkWidget* pacUnpackBtn = gtk_button_new_with_label(_("Unpack PAC"));
    gtk_widget_set_name(pacUnpackBtn, "pac_unpack");
    gtk_widget_set_size_request(pacUnpackBtn, -1, 32);
    helper.addWidget("pac_unpack", pacUnpackBtn);

    gtk_box_append(GTK_BOX(fileRow), pacFileLabel);
    gtk_box_append(GTK_BOX(fileRow), pacFilePath);
    gtk_box_append(GTK_BOX(fileRow), pacSelectBtn);
    gtk_box_append(GTK_BOX(fileRow), pacUnpackBtn);

    gtk_box_append(GTK_BOX(fileCardBox), fileRow);
    gtk_box_append(GTK_BOX(mainBox), fileFrame);

    //  第二部分：PAC 分区列表
    GtkWidget* listTitle = gtk_label_new(_("Please check partitions to flash"));
    helper.addWidget("pac_list_title", listTitle);
    gtk_widget_set_halign(listTitle, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_bottom(listTitle, 6);
    gtk_box_append(GTK_BOX(mainBox), listTitle);

    GtkWidget* listScroll = gtk_scrolled_window_new();
    gtk_widget_set_size_request(listScroll, -1, 260);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(listScroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    // 移除阴影
    gtk_widget_set_hexpand(listScroll, TRUE);

    // GtkColumnView：第 0 列是勾选框，后 3 列是分区名 / 大小 / 类型（原始名）
    GtkWidget* p_treeView = sfd_row_view_new(FALSE);
    gtk_widget_set_name(p_treeView, "pac_list");
    helper.addWidget("pac_list", p_treeView);

    sfd_row_view_add_toggle_column(p_treeView, _("Select"));
    sfd_row_view_add_text_column(p_treeView, _("Partition Name"), 1, FALSE);
    sfd_row_view_add_text_column(p_treeView, _("Size"), 2, FALSE);
    sfd_row_view_add_text_column(p_treeView, _("Type"), 3, FALSE);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(listScroll), p_treeView);
    gtk_box_append(GTK_BOX(mainBox), listScroll);

    //  第三部分：烧录操作卡片
    GtkWidget* flashFrame = gtk_frame_new(NULL);
    gtk_widget_set_margin_top(flashFrame, 16);
    gtk_widget_set_margin_bottom(flashFrame, 16);
    GtkWidget* flashTitleLabel = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(flashTitleLabel),
        (std::string("<span size='large'><b>") + _("Flash Operation") + "</b></span>").c_str());
    helper.addWidget("pac_flash_op_label", flashTitleLabel);
    gtk_frame_set_label_widget(GTK_FRAME(flashFrame), flashTitleLabel);
    gtkFrameSetLabelAlign(flashFrame, 0.5, 0.5);

    GtkWidget* flashCardBox = makeCardBox(12, 10);
    gtk_frame_set_child(GTK_FRAME(flashFrame), flashCardBox);

    GtkWidget* mergeNvCheck = gtk_check_button_new_with_label(_("Merge NV if supported"));
    gtk_widget_set_name(mergeNvCheck, "pac_merge_nv");
    helper.addWidget("pac_merge_nv", mergeNvCheck);
    gtk_box_append(GTK_BOX(flashCardBox), mergeNvCheck);

    GtkWidget* repartitionCheck = gtk_check_button_new_with_label(_("Repartition if needed"));
    gtk_widget_set_name(repartitionCheck, "pac_repartition");
    helper.addWidget("pac_repartition", repartitionCheck);
    gtk_box_append(GTK_BOX(flashCardBox), repartitionCheck);

    GtkWidget* pacFlashBtn = gtk_button_new_with_label(_("START PAC Flash"));
    gtk_widget_set_name(pacFlashBtn, "pac_flash_start");
    gtk_widget_set_size_request(pacFlashBtn, -1, 36);
    gtk_widget_set_hexpand(pacFlashBtn, TRUE);
    helper.addWidget("pac_flash_start", pacFlashBtn);
    gtk_box_append(GTK_BOX(flashCardBox), pacFlashBtn);

    gtk_box_append(GTK_BOX(mainBox), flashFrame);

    gtk_widget_set_visible(outerScroll, TRUE);
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
