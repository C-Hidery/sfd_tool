/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#include "page_about.h"
#include "../i18n.h"
#include <string>

extern std::string g_about_text;

GtkWidget* AboutPage::init(GtkWidgetHelper& helper, GtkWidget* notebook) {
    GtkWidget* aboutPage = gtk_grid_new();
	gtk_widget_set_hexpand(aboutPage, TRUE);
	gtk_widget_set_vexpand(aboutPage, TRUE);
	helper.addWidget("about_page", aboutPage, "grid");
    helper.addNotebookPage(notebook, aboutPage, _("About"));

    GtkWidget* scrolledAbout = gtk_scrolled_window_new();
    gtk_widget_set_hexpand(scrolledAbout, TRUE);
    gtk_widget_set_vexpand(scrolledAbout, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledAbout), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    helper.addWidget("scrolledAbout", scrolledAbout, "scrolledwindow");

    GtkWidget* aboutTextView = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(aboutTextView), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(aboutTextView), GTK_WRAP_WORD);
    gtk_widget_set_name(aboutTextView, "about_text");
    helper.addWidget("about_text", aboutTextView, "textview");
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(aboutTextView));
    gtk_text_buffer_set_text(buffer, g_about_text.c_str(), -1);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolledAbout), aboutTextView);
    helper.addToGrid(aboutPage, scrolledAbout, 0, 0, 1, 1);

    return aboutPage;
}

void AboutPage::bindSignals(GtkWidgetHelper& helper) {
	// About 页当前没有特定信号需要绑定
	(void)helper;
}

GtkWidget* create_about_page(GtkWidgetHelper& helper, GtkWidget* notebook) {
    AboutPage page;
    return page.init(helper, notebook);
}
