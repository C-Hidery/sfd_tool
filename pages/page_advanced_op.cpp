#include "page_advanced_op.h"
#include "../common.h"
#include "../main.h"
#include "../i18n.h"
#include "../GenTosNoAvb.h"
#include "page_partition.h"
#include <thread>

extern spdio_t* io;
extern int ret;
extern int m_bOpened;
extern int blk_size;
extern int isCMethod;
extern int gpt_failed;
extern int selected_ab;

static void on_button_clicked_set_active_a(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_(("Error")))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));
		
	}
	if(!selected_ab) {
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
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_(("Error")))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));
		
	}
	if(!selected_ab) {
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
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_(("Error")))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));
		
	}
	GtkWidget *parent = helper.getWidget("main_window");
	std::string filePath = helper.getEntryText(helper.getWidget("xml_path"));
	FILE *fi = oxfopen(filePath.c_str(), "r");
	if (fi == nullptr) {
		DEG_LOG(E, "File does not exist.");
		return;
	} else fclose(fi);
	repartition(io, filePath.c_str());
	showInfoDialog(GTK_WINDOW(parent), _(_(_(("Completed")))), _("Repartition completed!"));
	std::vector<partition_t> partitions;
	partitions.reserve(io->part_count);
	if(!isCMethod){
		for (int i = 0; i < io->part_count; i++) {
			partitions.push_back(io->ptable[i]);
		}
	}
	else {
		for (int i = 0; i < io->part_count_c; i++) {
			partitions.push_back(io->Cptable[i]);
		}
	}
	populatePartitionList(helper, partitions);
}

static void on_button_clicked_read_xml(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_(("Error")))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));
		
	}
	GtkWidget* parent = helper.getWidget("main_window");
	std::string savePath = showSaveFileDialog(GTK_WINDOW(parent), "partition_table.xml", { {_("XML files (*.xml)"), "*.xml"} });
	if (savePath.empty()) {
		showErrorDialog(GTK_WINDOW(parent), _(_(_(("Error")))), _("No save path selected!"));
		return;
	}
	if (!isCMethod) {
		if (gpt_failed == 1) io->ptable = partition_list(io, savePath.c_str(), &io->part_count);
		if (!io->part_count) {
			DEG_LOG(E, "Partition table not available");
			return;
		} else {
			DBG_LOG("  0 %36s     %lldKB\n", "splloader", (long long)g_spl_size / 1024);
			FILE* fo = my_oxfopen(savePath.c_str(), "wb");
			if (!fo) ERR_EXIT("Failed to open file\n");
			fprintf(fo, "<Partitions>\n");
			for (int i = 0; i < io->part_count; i++) {
				DBG_LOG("%3d %36s %7lldMB\n", i + 1, (*(io->ptable + i)).name, ((*(io->ptable + i)).size >> 20));
				fprintf(fo, "    <Partition id=\"%s\" size=\"", (*(io->ptable + i)).name);
				if (i + 1 == io->part_count) fprintf(fo, "0x%x\"/>\n", ~0);
				else fprintf(fo, "%lld\"/>\n", ((*(io->ptable + i)).size >> 20));
			}
			fprintf(fo, "</Partitions>");
			fclose(fo);
			DEG_LOG(I, "Partition table saved to %s", savePath.c_str());
		}
	} else {
		int c = io->part_count_c;
		if (!c) {
			DEG_LOG(E, "Partition table not available");
			return;
		} else {
			DBG_LOG("  0 %36s     %lldKB\n", "splloader", (long long)g_spl_size / 1024);
			FILE* fo = my_oxfopen(savePath.c_str(), "wb");
			if (!fo) ERR_EXIT("Failed to open file\n");
			fprintf(fo, "<Partitions>\n");
			char* name;
			int o = io->verbose;
			io->verbose = -1;
			for (int i = 0; i < c; i++) {
				name = (*(io->Cptable + i)).name;
				DBG_LOG("%3d %36s %7lldMB\n", i + 1, name, ((*(io->Cptable + i)).size >> 20));
				fprintf(fo, "    <Partition id=\"%s\" size=\"", name);
				if (check_partition(io, "userdata", 0) != 0 && i + 1 == io->part_count_c) fprintf(fo, "0x%x\"/>\n", ~0);
				else fprintf(fo, "%lld\"/>\n", ((*(io->Cptable + i)).size >> 20));
			}
			fprintf(fo, "</Partitions>");
			fclose(fo);
			io->verbose = o;
			DEG_LOG(I, "Partition table saved to %s", savePath.c_str());
		}
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

static void on_button_clicked_dmv_enable(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_(("Error")))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));
		
	}
	GtkWidget *parent = helper.getWidget("main_window");
	dm_avb_enable(io, blk_size ? blk_size : DEFAULT_BLK_SIZE, isCMethod);
	showInfoDialog(GTK_WINDOW(parent), _(_(_(("Completed")))), _("DM-Verity and AVB protection enabled!"));
}

static void on_button_clicked_dmv_disable(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_(("Error")))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));
		
	}
	GtkWidget *parent = helper.getWidget("main_window");
	avb_dm_disable(io, blk_size ? blk_size : DEFAULT_BLK_SIZE, isCMethod);
	showInfoDialog(GTK_WINDOW(parent), _(_(_(("Completed")))), _("DM-Verity and AVB protection disabled!"));
}

static void on_button_clicked_dis_avb(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_(("Error")))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));
		
	}
	helper.setLabelText(helper.getWidget("con"), "Patching trustos");
	TosPatcher patcher;
	bool i_is = showConfirmDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_(("Warning")))), _("This operation may break your device, and not all devices support this, if your device is broken, flash backup in backup_tos, continue?"));
	if (i_is) {
		std::thread([helper, patcher]() mutable {
			get_partition_info(io, "trustos", 1);
			if (!gPartInfo.size) {
				DEG_LOG(E, "Partition not exist\n");
				return;
			}
			dump_partition(io, gPartInfo.name, 0, gPartInfo.size, "trustos.bin", blk_size ? blk_size : DEFAULT_BLK_SIZE);
			int o = patcher.patcher("trustos.bin");
			if (!o) load_partition_unify(io, "trustos", "tos-noavb.bin",blk_size ? blk_size : DEFAULT_BLK_SIZE, isCMethod);
			if (!o) {
				gui_idle_call_wait_drag([helper]() {
					showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Info"), _("Disabled AVB successfully, the backup trustos is tos_bak.bin"));
				},GTK_WINDOW(helper.getWidget("main_window")));
			} else {
				gui_idle_call_wait_drag([helper]() {
					showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_(("Error")))), _("Disabled AVB failed, go to console window to see why"));
				},GTK_WINDOW(helper.getWidget("main_window")));
			}
		}).detach();
	}
    helper.setLabelText(helper.getWidget("con"), "Ready");
}

GtkWidget* create_advanced_op_page(GtkWidgetHelper& helper, GtkWidget* notebook) {
	GtkWidget* advOpPage = helper.createGrid("adv_op_page", 5, 5);
	helper.addNotebookPage(notebook, advOpPage, _("Advanced Operation"));

	GtkWidget* advScroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(advScroll),
	                               GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_hexpand(advScroll, TRUE);
	gtk_widget_set_vexpand(advScroll, TRUE);

	GtkWidget* mainBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 32);
	gtk_widget_set_margin_start(mainBox, 40);
	gtk_widget_set_margin_end(mainBox, 40);
	gtk_widget_set_margin_top(mainBox, 40);
	gtk_widget_set_margin_bottom(mainBox, 40);
	gtk_widget_set_halign(mainBox, GTK_ALIGN_CENTER);
	gtk_widget_set_size_request(mainBox, 600, -1);

	auto makeCardBox = [](int pad_h, int pad_v) -> GtkWidget* {
		GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
		gtk_widget_set_margin_start(box, pad_h);
		gtk_widget_set_margin_end(box, pad_h);
		gtk_widget_set_margin_top(box, pad_v);
		gtk_widget_set_margin_bottom(box, pad_v);
		return box;
	};

	// A/B partition
	GtkWidget* abFrame = gtk_frame_new(NULL);
	GtkWidget* abLabel = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(abLabel), (std::string("<b>") + _("Toggle the A/B partition boot settings") + "</b>").c_str());
	gtk_widget_set_halign(abLabel, GTK_ALIGN_CENTER);
	gtk_frame_set_label_widget(GTK_FRAME(abFrame), abLabel);
	gtk_frame_set_label_align(GTK_FRAME(abFrame), 0.5, 0.5);
	helper.addWidget("ab_label", abLabel);

	GtkWidget* abBox = makeCardBox(32, 16);
	gtk_container_add(GTK_CONTAINER(abFrame), abBox);

	GtkWidget* abButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
	gtk_widget_set_halign(abButtonBox, GTK_ALIGN_CENTER);
	GtkWidget* setActiveA = helper.createButton(_("Boot A partitons"), "set_active_a", nullptr, 0, 0, 272, 36);
	GtkWidget* setActiveB = helper.createButton(_("Boot B partitions"), "set_active_b", nullptr, 0, 0, 272, 36);
	gtk_box_pack_start(GTK_BOX(abButtonBox), setActiveA, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(abButtonBox), setActiveB, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(abBox), abButtonBox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(mainBox), abFrame, FALSE, FALSE, 0);

	// Repartition
	GtkWidget* repartFrame = gtk_frame_new(NULL);
	GtkWidget* repartLabel = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(repartLabel), (std::string("<b>") + _("Repartition") + "</b>").c_str());
	gtk_widget_set_halign(repartLabel, GTK_ALIGN_CENTER);
	gtk_frame_set_label_widget(GTK_FRAME(repartFrame), repartLabel);
	gtk_frame_set_label_align(GTK_FRAME(repartFrame), 0.5, 0.5);
	helper.addWidget("repart_label", repartLabel);

	GtkWidget* repartBox = makeCardBox(32, 16);
	gtk_container_add(GTK_CONTAINER(repartFrame), repartBox);

	GtkWidget* xmlLabel = gtk_label_new(_("XML part info file path"));
	gtk_widget_set_halign(xmlLabel, GTK_ALIGN_CENTER);
	helper.addWidget("xml_label", xmlLabel);

	GtkWidget* xmlInputWrap = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
	gtk_widget_set_halign(xmlInputWrap, GTK_ALIGN_CENTER);

	GtkWidget* xmlInputLinked = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(xmlInputLinked), "linked");
	
	GtkWidget* xmlPath = helper.createEntry("xml_path", "", false, 0, 0, 400, 36);
	GtkWidget* selectXmlBtn = helper.createButton("...", "select_xml", nullptr, 0, 0, 48, 36);
	
	gtk_box_pack_start(GTK_BOX(xmlInputLinked), xmlPath, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(xmlInputLinked), selectXmlBtn, FALSE, FALSE, 0);

	GtkWidget* startRepartBtn = helper.createButton(_("START"), "start_repart", nullptr, 0, 0, 96, 36);

	gtk_box_pack_start(GTK_BOX(xmlInputWrap), xmlInputLinked, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(xmlInputWrap), startRepartBtn, FALSE, FALSE, 0);

	GtkWidget* readXmlBtn = helper.createButton(_("Extract part info to a XML file (if support)"), "read_xml", nullptr, 0, 0, 560, 36);
	gtk_widget_set_halign(readXmlBtn, GTK_ALIGN_CENTER);

	gtk_box_pack_start(GTK_BOX(repartBox), xmlLabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(repartBox), xmlInputWrap, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(repartBox), readXmlBtn, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(mainBox), repartFrame, FALSE, FALSE, 0);

	// DM-verify
	GtkWidget* dmvFrame = gtk_frame_new(NULL);
	GtkWidget* dmvLabel = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(dmvLabel), (std::string("<b>") + _("DM-verity and AVB Settings (if support)") + "</b>").c_str());
	gtk_widget_set_halign(dmvLabel, GTK_ALIGN_CENTER);
	gtk_frame_set_label_widget(GTK_FRAME(dmvFrame), dmvLabel);
	gtk_frame_set_label_align(GTK_FRAME(dmvFrame), 0.5, 0.5);
	helper.addWidget("dmv_label", dmvLabel);

	GtkWidget* dmvBox = makeCardBox(32, 16);
	gtk_container_add(GTK_CONTAINER(dmvFrame), dmvBox);

	GtkWidget* dmvButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
	gtk_widget_set_halign(dmvButtonBox, GTK_ALIGN_CENTER);
	GtkWidget* dmvDisable = helper.createButton(_("Disable DM-verity and AVB"), "dmv_disable", nullptr, 0, 0, 272, 36);
	GtkWidget* dmvEnable = helper.createButton(_("Enable DM-verity and AVB"), "dmv_enable", nullptr, 0, 0, 272, 36);
	gtk_box_pack_start(GTK_BOX(dmvButtonBox), dmvDisable, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(dmvButtonBox), dmvEnable, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(dmvBox), dmvButtonBox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(mainBox), dmvFrame, FALSE, FALSE, 0);

	// No AVB
	GtkWidget* avbFrame = gtk_frame_new(NULL);
	GtkWidget* disavbLabel = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(disavbLabel), (std::string("<b>") + _("Trustos AVB Settings") + "</b>").c_str());
	gtk_widget_set_halign(disavbLabel, GTK_ALIGN_CENTER);
	gtk_frame_set_label_widget(GTK_FRAME(avbFrame), disavbLabel);
	gtk_frame_set_label_align(GTK_FRAME(avbFrame), 0.5, 0.5);
	helper.addWidget("avb_label", disavbLabel);

	GtkWidget* avbBox = makeCardBox(32, 16);
	gtk_container_add(GTK_CONTAINER(avbFrame), avbBox);

	GtkWidget* dis_avb = helper.createButton(_("[CAUTION] Disable AVB verification by patching trustos(Android 9 and lower)"), "dis_avb", nullptr, 0, 0, 560, 36);
	gtk_widget_set_halign(dis_avb, GTK_ALIGN_CENTER);

	gtk_box_pack_start(GTK_BOX(avbBox), dis_avb, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(mainBox), avbFrame, FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(advScroll), mainBox);
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
	helper.bindClick(helper.getWidget("dmv_disable"), [&]() {
		on_button_clicked_dmv_disable(helper);
	});
	helper.bindClick(helper.getWidget("dmv_enable"), [&]() {
		on_button_clicked_dmv_enable(helper);
	});
	helper.bindClick(helper.getWidget("dis_avb"), [&]() {
		on_button_clicked_dis_avb(helper);
	});
}
