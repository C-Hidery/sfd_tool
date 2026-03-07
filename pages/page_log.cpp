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
	const char* txt = helper.getTextAreaText(txtOutput);
	FILE* fo = oxfopen(savePath.c_str(), "w");
	if (!fo) {
		showErrorDialog(GTK_WINDOW(parent), _(_(_(("Error")))), _("Failed to save log file!"));
		return;
	}
	fprintf(fo, "%s", txt);
	fclose(fo);
	showInfoDialog(GTK_WINDOW(parent), _(_(_(("Completed")))), _("Log export completed!"));
}

static void on_button_clicked_log_clear(GtkWidgetHelper helper) {
	GtkWidget* txtOutput = helper.getWidget("txtOutput");
	helper.setTextAreaText(txtOutput, "");
}

GtkWidget* create_log_page(GtkWidgetHelper& helper, GtkWidget* notebook) {
	GtkWidget* logPage = helper.createGrid("log_page", 5, 5);
	helper.addNotebookPage(notebook, logPage, _("Log"));

    // 最外层滚动以适应极小窗口
	GtkWidget* pageScroll = gtk_scrolled_window_new(NULL, NULL);
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
    // 设定固定宽高与截图比例一致
    gtk_widget_set_size_request(mainBox, 900, -1);

    // 外框包裹日志显示区域
    GtkWidget* logFrame = gtk_frame_new(NULL);

	GtkWidget* scrolledLog = gtk_scrolled_window_new(NULL, NULL);
	gtk_widget_set_size_request(scrolledLog, -1, 500); 
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledLog),
	                               GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	GtkWidget* logTextView = gtk_text_view_new();
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(logTextView), GTK_WRAP_WORD);
	gtk_widget_set_name(logTextView, "txtOutput");
	helper.addWidget("txtOutput", logTextView);
	
    gtk_container_add(GTK_CONTAINER(scrolledLog), logTextView);
    gtk_container_add(GTK_CONTAINER(logFrame), scrolledLog);

	GtkWidget* expLogBtn = helper.createButton(_("Export"), "exp_log", nullptr, 0, 0, 120, 32);
	GtkWidget* logClearBtn = helper.createButton(_("Clear"), "log_clear", nullptr, 0, 0, 120, 32);

	GtkWidget* logButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
	gtk_widget_set_halign(logButtonBox, GTK_ALIGN_START); // 左对齐按钮
	gtk_box_pack_start(GTK_BOX(logButtonBox), expLogBtn, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(logButtonBox), logClearBtn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(mainBox), logFrame, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(mainBox), logButtonBox, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(pageScroll), mainBox);
	helper.addToGrid(logPage, pageScroll, 0, 0, 1, 1);

	return logPage;
}

void bind_log_signals(GtkWidgetHelper& helper) {
	helper.bindClick(helper.getWidget("exp_log"), [&]() {
		on_button_clicked_exp_log(helper);
	});
	helper.bindClick(helper.getWidget("log_clear"), [&]() {
		on_button_clicked_log_clear(helper);
	});
}
