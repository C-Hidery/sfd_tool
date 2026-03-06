#include "page_manual.h"
#include "../common.h"
#include "../main.h"
#include "../i18n.h"
#include <thread>

extern spdio_t* io;
extern int m_bOpened;
extern int blk_size;
extern int isCMethod;

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
	helper.setLabelText(helper.getWidget("con"), "Writing partition");
	FILE* fi;
	fi = oxfopen(filename.c_str(), "r");
	if (fi == nullptr) {
		DEG_LOG(E, "File does not exist.\n");
		return;
	} else fclose(fi);
	get_partition_info(io, part_name.c_str(), 0);
	if (!gPartInfo.size) {
		DEG_LOG(E, "Partition does not exist\n");
		return;
	}
	std::thread([parent, filename, helper]() {
		load_partition_unify(io, gPartInfo.name, filename.c_str(), blk_size ? blk_size : DEFAULT_BLK_SIZE, isCMethod);
		gui_idle_call_wait_drag([parent, helper]() mutable {
			showInfoDialog(GTK_WINDOW(parent), _(_(_(("Completed")))), _("Partition write completed!"));
			helper.setLabelText(helper.getWidget("con"), "Ready");
		},GTK_WINDOW(helper.getWidget("main_window")));
	}).detach();
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
	get_partition_info(io, part_name.c_str(), 1);
	if (!gPartInfo.size) {
		DEG_LOG(E, "Partition does not exist\n");
		return;
	}
    helper.setLabelText(helper.getWidget("con"), "Reading partition");
	std::thread([parent, savePath, helper]() {
		dump_partition(io, gPartInfo.name, 0, gPartInfo.size, savePath.c_str(), blk_size ? blk_size : DEFAULT_BLK_SIZE);
		gui_idle_call_wait_drag([parent, helper]() mutable {
			showInfoDialog(GTK_WINDOW(parent), _(_(_(("Completed")))), _("Partition read completed!"));
			helper.setLabelText(helper.getWidget("con"), "Ready");
		},GTK_WINDOW(helper.getWidget("main_window")));
	}).detach();
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
	get_partition_info(io, part_name.c_str(), 0);
	if (!gPartInfo.size) {
		DEG_LOG(E, "Partition does not exist\n");
		return;
	}
	helper.setLabelText(helper.getWidget("con"), "Erase partition");
	std::thread([parent, helper]() {
		erase_partition(io, gPartInfo.name, isCMethod);
		gui_idle_call_wait_drag([parent, helper]() mutable{
			showInfoDialog(GTK_WINDOW(parent), _(_(_(("Completed")))), _("Partition erase completed!"));
			helper.setLabelText(helper.getWidget("con"), "Ready");
		},GTK_WINDOW(helper.getWidget("main_window")));
	}).detach();
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

GtkWidget* create_manual_page(GtkWidgetHelper& helper, GtkWidget* notebook) {
	GtkWidget* manualPage = helper.createGrid("manual_page", 5, 5);
	helper.addNotebookPage(notebook, manualPage, _("Manually Operate"));

	// Write partition section
	GtkWidget* writeLabel = helper.createLabel(_("Write partition"), "write_label", 0, 0, 200, 20);
	GtkWidget* writePartLabel = helper.createLabel(_("Partition name:"), "write_part_label", 0, 0, 150, 20);
	GtkWidget* mPartFlash = helper.createEntry("m_part_flash", "", false, 0, 0, 155, 32);

	GtkWidget* filePathLabel = helper.createLabel(_("Image file path:"), "file_path_label", 0, 0, 200, 20);
	GtkWidget* mFilePath = helper.createEntry("m_file_path", "", false, 0, 0, 245, 32);
	GtkWidget* mSelectBtn = helper.createButton("...", "m_select", nullptr, 0, 0, 40, 32);

	GtkWidget* mWriteBtn = helper.createButton(_("WRITE"), "m_write", nullptr, 0, 0, 120, 32);

	// Separator
	GtkWidget* sep1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);

	// Extract partition section
	GtkWidget* extractLabel = helper.createLabel(_("Extract partition"), "extract_label", 0, 0, 200, 20);
	GtkWidget* extractPartLabel = helper.createLabel(_("Partition name:"), "extract_part_label", 0, 0, 150, 20);
	GtkWidget* mPartRead = helper.createEntry("m_part_read", "", false, 0, 0, 145, 32);

	GtkWidget* mReadBtn = helper.createButton(_("EXTRACT"), "m_read", nullptr, 0, 0, 120, 32);

	// Separator
	GtkWidget* sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);

	// Erase partition section
	GtkWidget* eraseLabel = helper.createLabel(_("Erase partition"), "erase_label", 0, 0, 200, 20);
	GtkWidget* erasePartLabel = helper.createLabel(_("Partition name:"), "erase_part_label", 0, 0, 150, 20);
	GtkWidget* mPartErase = helper.createEntry("m_part_erase", "", false, 0, 0, 150, 32);

	GtkWidget* mEraseBtn = helper.createButton(_("ERASE"), "m_erase", nullptr, 0, 0, 120, 32);

	// Cancel button
	GtkWidget* mCancelBtn = helper.createButton(_("Cancel"), "m_cancel", nullptr, 0, 0, 120, 32);

	// Add to grid
	int row = 0;
	helper.addToGrid(manualPage, writeLabel, 0, row++, 3, 1);
	helper.addToGrid(manualPage, writePartLabel, 0, row, 1, 1);
	helper.addToGrid(manualPage, mPartFlash, 1, row++, 2, 1);

	helper.addToGrid(manualPage, filePathLabel, 0, row, 1, 1);
	helper.addToGrid(manualPage, mFilePath, 1, row, 2, 1);
	helper.addToGrid(manualPage, mSelectBtn, 3, row++, 1, 1);

	helper.addToGrid(manualPage, mWriteBtn, 0, row++, 3, 1);

	// Add separator
	row++;
	helper.addToGrid(manualPage, sep1, 0, row++, 4, 1);
	row++;

	helper.addToGrid(manualPage, extractLabel, 0, row++, 3, 1);
	helper.addToGrid(manualPage, extractPartLabel, 0, row, 1, 1);
	helper.addToGrid(manualPage, mPartRead, 1, row++, 2, 1);

	helper.addToGrid(manualPage, mReadBtn, 0, row++, 3, 1);

	// Add separator
	row++;
	helper.addToGrid(manualPage, sep2, 0, row++, 4, 1);
	row++;

	helper.addToGrid(manualPage, eraseLabel, 0, row++, 3, 1);
	helper.addToGrid(manualPage, erasePartLabel, 0, row, 1, 1);
	helper.addToGrid(manualPage, mPartErase, 1, row++, 2, 1);

	helper.addToGrid(manualPage, mEraseBtn, 0, row++, 3, 1);

	// Add Cancel button
	row += 2;
	helper.addToGrid(manualPage, mCancelBtn, 0, row++, 3, 1);

	return manualPage;
}

void bind_manual_signals(GtkWidgetHelper& helper) {
	helper.bindClick(helper.getWidget("m_select"), []() {
		on_button_clicked_m_select(helper);
	});
	helper.bindClick(helper.getWidget("m_write"), []() {
		on_button_clicked_m_write(helper);
	});
	helper.bindClick(helper.getWidget("m_read"), []() {
		on_button_clicked_m_read(helper);
	});
	helper.bindClick(helper.getWidget("m_erase"), []() {
		on_button_clicked_m_erase(helper);
	});
	helper.bindClick(helper.getWidget("m_cancel"), []() {
		on_button_clicked_m_cancel(helper);
	});
}
