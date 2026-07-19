/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#include "page_log.h"
#include "../common.h"
#include "../i18n.h"

static void on_button_clicked_exp_log(GtkWidgetHelper helper) {
	GtkWidget* parent = helper.getWidget("main_window");
	GtkWidget *txtOutput = helper.getWidget("txtOutput");
	std::string savePath = showSaveFileDialog(GTK_WINDOW(parent), "sfd_tool_log.txt", { {_("Text files (*.txt)"), "*.txt"} });
	if (savePath.empty()) {
		showErrorDialog(GTK_WINDOW(parent), _(_(_(("Error")))), _("No save path selected!"));
		return;
	}
	std::string txt_orig = helper.getTextAreaText(txtOutput);
	const char* txt = txt_orig.c_str();     
	EnhancedFile fo = oxfopen_enhanced(savePath.c_str(), "w");
	if (!fo) {
		showErrorDialog(GTK_WINDOW(parent), _(_(_(("Error")))), _("Failed to save log file!"));
		return;
	}
	fo << txt;
	showInfoDialog(GTK_WINDOW(parent), _(_(_(("Completed")))), _("Log export completed!"));
}

static void on_button_clicked_log_clear(GtkWidgetHelper helper) {
	GtkWidget* txtOutput = helper.getWidget("txtOutput");
	helper.setTextAreaText(txtOutput, "");
}

GtkWidget* LogPage::init(GtkWidgetHelper& helper, GtkWidget* notebook) {
    // 创建页面顶层 Grid
    GtkWidget* logPage = gtk_grid_new();
    gtk_widget_set_name(logPage, "log_page");
    helper.addWidget("log_page", logPage);
    helper.addNotebookPage(notebook, logPage, _("Log"));

    // 最外层滚动以适应极小窗口
    GtkWidget* pageScroll = gtk_scrolled_window_new();
    gtk_widget_set_hexpand(pageScroll, TRUE);
    gtk_widget_set_vexpand(pageScroll, TRUE);

    // 主居中盒子
    GtkWidget* mainBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
    gtk_widget_set_halign(mainBox, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(mainBox, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(mainBox, 40);
    gtk_widget_set_margin_end(mainBox, 40);
    gtk_widget_set_margin_top(mainBox, 40);
    gtk_widget_set_margin_bottom(mainBox, 40);
    // 设定最小宽度，避免在小屏上撑死高度
    gtk_widget_set_size_request(mainBox, 700, -1);

    // 外框包裹日志显示区域
    GtkWidget* logFrame = gtk_frame_new(NULL);

    GtkWidget* scrolledLog = gtk_scrolled_window_new();
    gtk_widget_set_size_request(scrolledLog, -1, 500);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledLog),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    GtkWidget* logTextView = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(logTextView), GTK_WRAP_WORD);
    gtk_widget_set_name(logTextView, "txtOutput");
    helper.addWidget("txtOutput", logTextView);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolledLog), logTextView);
    gtk_frame_set_child(GTK_FRAME(logFrame), scrolledLog);

    GtkWidget* expLogBtn = gtk_button_new_with_label(_("Export"));
    gtk_widget_set_name(expLogBtn, "exp_log");
    gtk_widget_set_size_request(expLogBtn, 120, 32);
    helper.addWidget("exp_log", expLogBtn);

    GtkWidget* logClearBtn = gtk_button_new_with_label(_("Clear"));
    gtk_widget_set_name(logClearBtn, "log_clear");
    gtk_widget_set_size_request(logClearBtn, 120, 32);
    helper.addWidget("log_clear", logClearBtn);

    GtkWidget* logButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_widget_set_halign(logButtonBox, GTK_ALIGN_START); // 左对齐按钮
    gtk_box_append(GTK_BOX(logButtonBox), expLogBtn);
    gtk_box_append(GTK_BOX(logButtonBox), logClearBtn);

    gtk_box_append(GTK_BOX(mainBox), logFrame);
    gtk_box_append(GTK_BOX(mainBox), logButtonBox);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(pageScroll), mainBox);
    gtk_grid_attach(GTK_GRID(logPage), pageScroll, 0, 0, 1, 1);

    return logPage;
}

void LogPage::bindSignals(GtkWidgetHelper& helper) {
	helper.bindClick(helper.getWidget("exp_log"), [&]() {
		on_button_clicked_exp_log(helper);
	});
	helper.bindClick(helper.getWidget("log_clear"), [&]() {
		on_button_clicked_log_clear(helper);
	});
}

GtkWidget* create_log_page(GtkWidgetHelper& helper, GtkWidget* notebook) {
    LogPage page;
    return page.init(helper, notebook);
}

void bind_log_signals(GtkWidgetHelper& helper) {
    LogPage page;
    page.bindSignals(helper);
}
