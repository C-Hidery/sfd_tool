#include "page_about.h"
#include "../i18n.h"

extern const char *AboutText;

GtkWidget* create_about_page(GtkWidgetHelper& helper, GtkWidget* notebook) {
	GtkWidget* aboutPage = helper.createGrid("about_page", 5, 5);
	helper.addNotebookPage(notebook, aboutPage, _("About"));

	GtkWidget* scrolledAbout = gtk_scrolled_window_new(NULL, NULL);
	gtk_widget_set_hexpand(scrolledAbout, TRUE);
	gtk_widget_set_vexpand(scrolledAbout, TRUE);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledAbout),
	                               GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	GtkWidget* aboutTextView = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(aboutTextView), FALSE);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(aboutTextView), GTK_WRAP_WORD);
	gtk_widget_set_name(aboutTextView, "about_text");
	helper.addWidget("about_text", aboutTextView);
	GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(aboutTextView));
	gtk_text_buffer_set_text(buffer, AboutText, -1);

	gtk_container_add(GTK_CONTAINER(scrolledAbout), aboutTextView);
	helper.addToGrid(aboutPage, scrolledAbout, 0, 0, 1, 1);

	return aboutPage;
}
