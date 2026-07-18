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
    GtkWidget* logPage = gtk_grid_new();
    gtk_widget_set_hexpand(logPage, TRUE);
    gtk_widget_set_vexpand(logPage, TRUE);
    helper.addWidget("log_page", logPage, "grid");
    helper.addNotebookPage(notebook, logPage, _("Log"));

    GtkWidget* pageScroll = gtk_scrolled_window_new();
    gtk_widget_set_hexpand(pageScroll, TRUE);
    gtk_widget_set_vexpand(pageScroll, TRUE);
    helper.addWidget("pageScroll", pageScroll, "scrolledwindow");

    GtkWidget* mainBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
  	gtk_widget_set_hexpand(mainBox, TRUE);
	gtk_widget_set_vexpand(mainBox, TRUE);
	gtk_widget_set_halign(mainBox, GTK_ALIGN_FILL);   // 改为 FILL 以填满
	gtk_widget_set_valign(mainBox, GTK_ALIGN_FILL);
    gtk_widget_set_margin_start(mainBox, 40);
    gtk_widget_set_margin_end(mainBox, 40);
    gtk_widget_set_margin_top(mainBox, 40);
    gtk_widget_set_margin_bottom(mainBox, 40);
    helper.addWidget("mainBox", mainBox, "box");

    // 日志显示区域
    GtkWidget* logFrame = gtk_frame_new(NULL);
	gtk_widget_set_hexpand(logFrame, TRUE);
	gtk_widget_set_vexpand(logFrame, TRUE);	
    helper.addWidget("logFrame", logFrame, "frame");

    GtkWidget* scrolledLog = gtk_scrolled_window_new();
    gtk_widget_set_size_request(scrolledLog, -1, 500);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledLog), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_hexpand(scrolledLog, TRUE);
	gtk_widget_set_vexpand(scrolledLog, TRUE);
    helper.addWidget("scrolledLog", scrolledLog, "scrolledwindow");

    GtkWidget* logTextView = gtk_text_view_new();
	gtk_widget_set_hexpand(logTextView, TRUE);
	gtk_widget_set_vexpand(logTextView, TRUE);	
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(logTextView), GTK_WRAP_WORD);
    gtk_widget_set_name(logTextView, "txtOutput");
    helper.addWidget("txtOutput", logTextView, "textview");

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolledLog), logTextView);
    gtkContainerAdd(logFrame, scrolledLog);

    GtkWidget* expLogBtn = gtk_button_new_with_label(_("Export"));
    helper.addWidget("exp_log", expLogBtn, "button");
    GtkWidget* logClearBtn = gtk_button_new_with_label(_("Clear"));
    helper.addWidget("log_clear", logClearBtn, "button");

    GtkWidget* logButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_widget_set_halign(logButtonBox, GTK_ALIGN_START);
    helper.addWidget("logButtonBox", logButtonBox, "box");
    gtkBoxPackStart(logButtonBox, expLogBtn, FALSE, FALSE, 0);
    gtkBoxPackStart(logButtonBox, logClearBtn, FALSE, FALSE, 0);

    gtkBoxPackStart(mainBox, logFrame, FALSE, FALSE, 0);
    gtkBoxPackStart(mainBox, logButtonBox, FALSE, FALSE, 0);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(pageScroll), mainBox);
    helper.addToGrid(logPage, pageScroll, 0, 0, 1, 1);

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
