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

	// A/B partition
	GtkWidget* abLabel = helper.createLabel(_("Toggle the A/B partition boot settings"), "ab_label", 0, 0, 400, 20);
	GtkWidget* setActiveA = helper.createButton(_("Boot A partitons"), "set_active_a", nullptr, 0, 0, 200, 32);
	GtkWidget* setActiveB = helper.createButton(_("Boot B partitions"), "set_active_b", nullptr, 0, 0, 200, 32);

	// Repartition
	GtkWidget* repartLabel = helper.createLabel(_("Repartition"), "repart_label", 0, 0, 200, 20);
	GtkWidget* xmlLabel = helper.createLabel(_("XML part info file path"), "xml_label", 0, 0, 300, 20);
	GtkWidget* xmlPath = helper.createEntry("xml_path", "", false, 0, 0, 374, 32);
	GtkWidget* selectXmlBtn = helper.createButton("...", "select_xml", nullptr, 0, 0, 40, 32);
	GtkWidget* startRepartBtn = helper.createButton(_("START"), "start_repart", nullptr, 0, 0, 120, 32);

	GtkWidget* readXmlBtn = helper.createButton(_("Extract part info to a XML file (if support)"),
	                        "read_xml", nullptr, 0, 0, 500, 32);

	// DM-verify
	GtkWidget* dmvLabel = helper.createLabel(_("DM-verity and AVB Settings (if support)"), "dmv_label", 0, 0, 400, 20);
	GtkWidget* dmvDisable = helper.createButton(_("Disable DM-verity and AVB"), "dmv_disable", nullptr, 0, 0, 220, 32);
	GtkWidget* dmvEnable = helper.createButton(_("Enable DM-verity and AVB"), "dmv_enable", nullptr, 0, 0, 220, 32);

	// No AVB
	GtkWidget* disavbLabel = helper.createLabel(_("Trustos AVB Settings"), "avb_label", 0, 0, 400, 20);
	GtkWidget* dis_avb = helper.createButton(_("[CAUTION] Disable AVB verification by patching trustos(Android 9 and lower)"), "dis_avb", nullptr, 0, 0, 230, 32);

	// Add to grid
	int row = 0;
	helper.addToGrid(advOpPage, abLabel, 0, row++, 3, 1);

	GtkWidget* abButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
	gtk_box_pack_start(GTK_BOX(abButtonBox), setActiveA, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(abButtonBox), setActiveB, FALSE, FALSE, 0);
	helper.addToGrid(advOpPage, abButtonBox, 0, row++, 3, 1);

	row += 2;

	helper.addToGrid(advOpPage, repartLabel, 0, row++, 3, 1);
	helper.addToGrid(advOpPage, xmlLabel, 0, row++, 3, 1);

	GtkWidget* xmlBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_box_pack_start(GTK_BOX(xmlBox), xmlPath, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(xmlBox), selectXmlBtn, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(xmlBox), startRepartBtn, FALSE, FALSE, 0);
	helper.addToGrid(advOpPage, xmlBox, 0, row++, 3, 1);

	helper.addToGrid(advOpPage, readXmlBtn, 0, row++, 3, 1);

	row += 2;

	helper.addToGrid(advOpPage, dmvLabel, 0, row++, 3, 1);

	GtkWidget* dmvButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
	gtk_box_pack_start(GTK_BOX(dmvButtonBox), dmvDisable, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(dmvButtonBox), dmvEnable, FALSE, FALSE, 0);
	helper.addToGrid(advOpPage, dmvButtonBox, 0, row++, 3, 1);

	row += 2;

	helper.addToGrid(advOpPage, disavbLabel, 0, row++, 3, 1);
	helper.addToGrid(advOpPage, dis_avb, 0, row++, 3, 1);

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
