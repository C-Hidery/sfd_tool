/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#include "page_advanced_op.h"
#include "../common.h"
#include "../main.h"
#include "../i18n.h"
#include "ui/ui_common.h"
#include "page_partition.h"
#include "../core/flash_service.h"
#include "ui/ui_common.h"
#include <thread>
#include <memory>
#include <vector>

extern spdio_t*& io;
extern int ret;
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

static void refresh_partition_list(GtkWidgetHelper& helper) {
	auto* svc = ensure_flash_service();
	std::vector<sfd::DevicePartitionInfo> partitions;
	sfd::FlashStatus st = svc->refreshDevicePartitions(partitions);
	if (!st.success) {
		DEG_LOG(E, "refreshDevicePartitions failed: %s", st.message.c_str());
		return;
	}
	populatePartitionList(helper, partitions);
}

static void on_button_clicked_set_active_a(GtkWidgetHelper helper) {
	ensure_device_attached_or_exit(helper);
	if(!g_app_state.flash.selected_ab) {
		gui_idle_call_wait_drag([helper](){
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_(("Error")))), _("Device is not using VAB!"));
		},GTK_WINDOW(helper.getWidget("main_window")));
		return;
	}
	set_active(io, "a", isCMethod);
	showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Tips"), _("Current active partition set to Slot A!"));
	gui_idle_call([helper]() mutable {
		helper.setLabelText(helper.getWidget("slot_mode"),"Slot A");
	});
}

static void on_button_clicked_set_active_b(GtkWidgetHelper helper) {
	ensure_device_attached_or_exit(helper);
	if(!g_app_state.flash.selected_ab) {
		gui_idle_call_wait_drag([helper](){
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_(("Error")))), _("Device is not using VAB!"));
		},GTK_WINDOW(helper.getWidget("main_window")));
		return;
	}
	set_active(io, "b", isCMethod);
	showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Tips"), _("Current active partition set to Slot B!"));
	gui_idle_call([helper]() mutable {
		helper.setLabelText(helper.getWidget("slot_mode"),"Slot B");
	});
}

static void on_button_clicked_start_repart(GtkWidgetHelper helper) {
	ensure_device_attached_or_exit(helper);
	GtkWidget *parent = helper.getWidget("main_window");
	std::string filePath = helper.getEntryText(helper.getWidget("xml_path"));
	EnhancedFile fi = oxfopen_enhanced(filePath.c_str(), "r");
	if (!fi) {
		DEG_LOG(E, "File does not exist.");
		return;
	}
	fi.close();
	repartition(io, filePath.c_str());
	showInfoDialog(GTK_WINDOW(parent), _(_(_(("Completed")))), _("Repartition completed!"));
	refresh_partition_list(helper);
}

static void on_button_clicked_read_xml(GtkWidgetHelper helper) {
	ensure_device_attached_or_exit(helper);
	GtkWidget* parent = helper.getWidget("main_window");
	std::string savePath = showSaveFileDialog(GTK_WINDOW(parent), "partition_table.xml", { {_("XML files (*.xml)"), "*.xml"} });
	if (savePath.empty()) {
		showErrorDialog(GTK_WINDOW(parent), _(_(_(("Error")))), _("No save path selected!"));
		return;
	}

	auto* svc = ensure_flash_service();
	sfd::FlashStatus st = svc->exportPartitionTableToXml(savePath);
	if (!st.success) {
		DEG_LOG(E, "exportPartitionTableToXml failed: %s", st.message.c_str());
		showErrorDialog(GTK_WINDOW(parent), _(_(_(("Error")))), st.message.c_str());
		return;
	}

	showInfoDialog(GTK_WINDOW(parent), _(_(_(("Completed")))), _("Partition table export completed!"));
}

static void on_button_clicked_select_xml(GtkWidgetHelper helper) {
	GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
	std::string filename = showFileChooser(parent, true);
	if (!filename.empty()) {
		helper.setEntryText(helper.getWidget("xml_path"), filename);
	}
}


GtkWidget* create_advanced_op_page(GtkWidgetHelper& helper, GtkWidget* notebook) {
    GtkWidget* advOpPage = gtk_grid_new();
	gtk_widget_set_hexpand(advOpPage, TRUE);
	gtk_widget_set_vexpand(advOpPage, TRUE);
	helper.addWidget("adv_op_page", advOpPage, "grid");
    helper.addNotebookPage(notebook, advOpPage, _("Advanced Operation"));

    GtkWidget* advScroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(advScroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(advScroll, TRUE);
    gtk_widget_set_vexpand(advScroll, TRUE);
    helper.addWidget("advScroll", advScroll, "scrolledwindow");

    GtkWidget* mainBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 32);
	gtk_widget_set_hexpand(mainBox, TRUE);
    gtk_widget_set_vexpand(mainBox, TRUE);
    gtk_widget_set_margin_start(mainBox, 40);
    gtk_widget_set_margin_end(mainBox, 40);
    gtk_widget_set_margin_top(mainBox, 40);
    gtk_widget_set_margin_bottom(mainBox, 40);
    gtk_widget_set_halign(mainBox, GTK_ALIGN_FILL);
	gtk_widget_set_valign(mainBox, GTK_ALIGN_FILL);
    helper.addWidget("mainBox", mainBox, "box");

    auto makeCardBox = [](int pad_h, int pad_v) -> GtkWidget* {
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
        gtk_widget_set_margin_start(box, pad_h);
        gtk_widget_set_margin_end(box, pad_h);
        gtk_widget_set_margin_top(box, pad_v);
        gtk_widget_set_margin_bottom(box, pad_v);
        return box;
    };

    // ---- A/B partition ----
    GtkWidget* abFrame = gtk_frame_new(NULL);
	gtk_widget_set_hexpand(abFrame, TRUE);
    GtkWidget* abLabel = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(abLabel), (std::string("<b>") + _("Toggle the A/B partition boot settings") + "</b>").c_str());
    gtk_widget_set_halign(abLabel, GTK_ALIGN_CENTER);
    gtk_frame_set_label_widget(GTK_FRAME(abFrame), abLabel);
    gtkFrameSetLabelAlign(abFrame, 0.5, 0.5);
    helper.addWidget("ab_label", abLabel, "label");

    GtkWidget* abBox = makeCardBox(32, 16);
    helper.addWidget("abBox", abBox, "box");
    gtkContainerAdd(abFrame, abBox);

    GtkWidget* abButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_halign(abButtonBox, GTK_ALIGN_CENTER);
    helper.addWidget("abButtonBox", abButtonBox, "box");

    GtkWidget* setActiveA = gtk_button_new_with_label(_("Boot A partitons"));
    gtk_widget_set_size_request(setActiveA, 272, 36);
    helper.addWidget("set_active_a", setActiveA, "button");
    GtkWidget* setActiveB = gtk_button_new_with_label(_("Boot B partitions"));
    gtk_widget_set_size_request(setActiveB, 272, 36);
    helper.addWidget("set_active_b", setActiveB, "button");

    gtkBoxPackStart(abButtonBox, setActiveA, FALSE, FALSE, 0);
    gtkBoxPackStart(abButtonBox, setActiveB, FALSE, FALSE, 0);
    gtkBoxPackStart(abBox, abButtonBox, FALSE, FALSE, 0);
    gtkBoxPackStart(mainBox, abFrame, FALSE, FALSE, 0);

    // ---- Repartition ----
    GtkWidget* repartFrame = gtk_frame_new(NULL);
	gtk_widget_set_hexpand(repartFrame, TRUE);
    GtkWidget* repartLabel = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(repartLabel), (std::string("<b>") + _("Repartition") + "</b>").c_str());
    gtk_widget_set_halign(repartLabel, GTK_ALIGN_CENTER);
    gtk_frame_set_label_widget(GTK_FRAME(repartFrame), repartLabel);
    gtkFrameSetLabelAlign(repartFrame, 0.5, 0.5);
    helper.addWidget("repart_label", repartLabel, "label");

    GtkWidget* repartBox = makeCardBox(32, 16);
    helper.addWidget("repartBox", repartBox, "box");
    gtkContainerAdd(repartFrame, repartBox);

    GtkWidget* xmlLabel = gtk_label_new(_("XML part info file path"));
    gtk_widget_set_halign(xmlLabel, GTK_ALIGN_CENTER);
    helper.addWidget("xml_label", xmlLabel, "label");

    GtkWidget* xmlInputWrap = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_halign(xmlInputWrap, GTK_ALIGN_CENTER);
    helper.addWidget("xmlInputWrap", xmlInputWrap, "box");

    GtkWidget* xmlInputLinked = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(xmlInputLinked, "linked");
    helper.addWidget("xmlInputLinked", xmlInputLinked, "box");

    GtkWidget* xmlPath = gtk_entry_new();
    gtk_widget_set_size_request(xmlPath, 400, 36);
    helper.addWidget("xml_path", xmlPath, "entry");
    GtkWidget* selectXmlBtn = gtk_button_new_with_label("...");
    gtk_widget_set_size_request(selectXmlBtn, 48, 36);
    helper.addWidget("select_xml", selectXmlBtn, "button");

    gtkBoxPackStart(xmlInputLinked, xmlPath, FALSE, FALSE, 0);
    gtkBoxPackStart(xmlInputLinked, selectXmlBtn, FALSE, FALSE, 0);

    GtkWidget* startRepartBtn = gtk_button_new_with_label(_("START"));
    gtk_widget_set_size_request(startRepartBtn, 96, 36);
    helper.addWidget("start_repart", startRepartBtn, "button");

    gtkBoxPackStart(xmlInputWrap, xmlInputLinked, FALSE, FALSE, 0);
    gtkBoxPackStart(xmlInputWrap, startRepartBtn, FALSE, FALSE, 0);

    GtkWidget* readXmlBtn = gtk_button_new_with_label(_("Extract part info to a XML file (if support)"));
    gtk_widget_set_size_request(readXmlBtn, 560, 36);
    gtk_widget_set_halign(readXmlBtn, GTK_ALIGN_CENTER);
    helper.addWidget("read_xml", readXmlBtn, "button");

    gtkBoxPackStart(repartBox, xmlLabel, FALSE, FALSE, 0);
    gtkBoxPackStart(repartBox, xmlInputWrap, FALSE, FALSE, 0);
    gtkBoxPackStart(repartBox, readXmlBtn, FALSE, FALSE, 0);
    gtkBoxPackStart(mainBox, repartFrame, FALSE, FALSE, 0);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(advScroll), mainBox);
    helper.addToGrid(advOpPage, advScroll, 0, 0, 5, 5);

    return advOpPage;
}

void bind_advanced_op_signals(GtkWidgetHelper& helper) {
	helper.bindClick(helper.getWidget("set_active_a"), [&]() {
		on_button_clicked_set_active_a(helper);
	});
	helper.bindClick(helper.getWidget("set_active_b"), [&]() {
		on_button_clicked_set_active_b(helper);
	});
	helper.bindClick(helper.getWidget("select_xml"), [&]() {
		on_button_clicked_select_xml(helper);
	});
	helper.bindClick(helper.getWidget("start_repart"), [&]() {
		on_button_clicked_start_repart(helper);
	});
	helper.bindClick(helper.getWidget("read_xml"), [&]() {
		on_button_clicked_read_xml(helper);
	});
	
}
