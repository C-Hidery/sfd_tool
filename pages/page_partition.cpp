#include "page_partition.h"
#include "../common.h"
#include "../main.h"
#include "../i18n.h"
#include <thread>
#include <iostream>

extern spdio_t* io;
extern int ret;
extern int m_bOpened;
extern int blk_size;
extern int isCMethod;
extern int gpt_failed;
extern AppState g_app_state;

void populatePartitionList(GtkWidgetHelper& helper, const std::vector<partition_t>& partitions) {
	GtkWidget* part_list = helper.getWidget("part_list");
	if (!part_list || !GTK_IS_TREE_VIEW(part_list)) {
		std::cerr << "part_list not found or not a TreeView" << std::endl;
		return;
	}

	GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(part_list));
	if (!model) {
		std::cerr << "TreeView model not found" << std::endl;
		return;
	}

	GtkListStore* store = GTK_LIST_STORE(model);
	gtk_list_store_clear(store);

	int index = 1;
	GtkTreeIter iter_spl;
	gtk_list_store_append(store, &iter_spl);
	long long spl_size = g_spl_size > 0 ? g_spl_size : 0;
	std::string display_name = std::to_string(index) + ". splloader";
	std::string size_str;
	if (spl_size < 1024) {
		size_str = std::to_string(spl_size) + " B";
	} else {
		size_str = std::to_string(spl_size / 1024) + " KB";
	}
	gtk_list_store_set(store, &iter_spl,
	                   0, display_name.c_str(),
	                   1, size_str.c_str(),
	                   2, "splloader",
	                   -1);

	index++;
	for (const auto& partition : partitions) {
		GtkTreeIter iter;
		gtk_list_store_append(store, &iter);

		std::string display_name = std::to_string(index) + ". " + partition.name;

		std::string size_str;
		if (partition.size < 1024) {
			size_str = std::to_string(partition.size) + " B";
		} else if (partition.size < 1024 * 1024) {
			size_str = std::to_string(partition.size / 1024) + " KB";
		} else if (partition.size < 1024 * 1024 * 1024) {
			size_str = std::to_string(partition.size / (1024 * 1024)) + " MB";
		} else {
			size_str = std::to_string(partition.size / (1024 * 1024 * 1024.0)) + " GB";
		}

		gtk_list_store_set(store, &iter,
		                   0, display_name.c_str(),
		                   1, size_str.c_str(),
		                   2, partition.name,
		                   -1);

		index++;
	}

	gtk_widget_queue_draw(part_list);
}

std::string getSelectedPartitionName(GtkWidgetHelper& helper) {
	GtkWidget* part_list = helper.getWidget("part_list");
	if (!part_list || !GTK_IS_TREE_VIEW(part_list)) {
		return "";
	}

	GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(part_list));
	GtkTreeModel* model;
	GtkTreeIter iter;

	if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
		gchar* original_name = nullptr;
		gtk_tree_model_get(model, &iter, 2, &original_name, -1);

		if (original_name) {
			std::string name = original_name;
			g_free(original_name);
			return name;
		}
	}

	return "";
}

void update_partition_size(spdio_t* io) {
    if(!isCMethod) {
        for(int i = 0; i < io->part_count; i++) {
            int v1 = io->verbose;
            io->verbose = -1;
            (*(io->ptable + i)).size = check_partition(io, (*(io->ptable + i)).name, 1);
            io->verbose = v1;
        }
    }
    else {
        for(int i = 0; i < io->part_count_c; i++) {
            int v1 = io->verbose;
            io->verbose = -1;
            (*(io->Cptable + i)).size = check_partition(io, (*(io->Cptable + i)).name, 1);
            io->verbose = v1;
        }
    }

}

void on_button_clicked_list_write(GtkWidgetHelper helper) {
	GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
	std::string filename = showFileChooser(parent, true);
	std::string part_name = getSelectedPartitionName(helper);
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));

	}
	if (filename.empty()) {
		showErrorDialog(parent, _(_(_("Error"))), _("No partition list file selected!"));
		return;
	}
	if (io->part_count == 0 && io->part_count_c == 0) {
		showErrorDialog(parent, _(_(_("Error"))), _("No partition table loaded, cannot write partition list!"));
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
	std::thread([filename, parent,helper]() {
		load_partition_unify(io, gPartInfo.name, filename.c_str(), blk_size ? blk_size : DEFAULT_BLK_SIZE, isCMethod);
		gui_idle_call_wait_drag([parent,helper]() mutable {
			showInfoDialog(GTK_WINDOW(parent), _(_(_("Completed"))), _("Partition write completed!"));
			helper.setLabelText(helper.getWidget("con"), "Ready");
		},GTK_WINDOW(helper.getWidget("main_window")));
	}).detach();

}

void on_button_clicked_list_force_write(GtkWidgetHelper helper) {
	GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
	std::string filename = showFileChooser(parent, true);
	std::string part_name = getSelectedPartitionName(helper);
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));

	}
	if (filename.empty()) {
		showErrorDialog(parent, _(_(_("Error"))), _("No partition list file selected!"));
		return;
	}
	if (io->part_count == 0 && io->part_count_c == 0) {
		showErrorDialog(parent, _(_(_("Error"))), _("No partition table loaded, cannot write partition list!"));
		return;
	}
	helper.setLabelText(helper.getWidget("con"), "Force Writing partition");
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
	bool i_op = showConfirmDialog(parent, _(_(_("Confirm"))), _("Force writing partitions may brick the device, do you want to continue?"));
	if (!i_op) return;
	if (!strncmp(gPartInfo.name, "splloader", 9)) {
		showErrorDialog(parent, _(_(_("Error"))), _("Force write mode does not allow writing to splloader partition!"));
		return;
	}
	if (isCMethod) {
		bool i_is = showConfirmDialog(parent, _(_(_("Warning"))), _("Currently in compatibility-method-PartList mode, force writing may brick the device!"));
		if (!i_is) return;
		if (io->part_count_c) {
			std::thread([filename, parent, helper]() {
				for (int i = 0; i < io->part_count_c; i++)
					if (!strcmp(gPartInfo.name, (*(io->Cptable + i)).name)) {
						load_partition_force(io, i, filename.c_str(), blk_size ? blk_size : DEFAULT_BLK_SIZE, 1);
						break;
					}

				gui_idle_call_wait_drag([parent]() {
					showInfoDialog(GTK_WINDOW(parent), _(_(_("Completed"))), _("Partition force write completed!"));
				},GTK_WINDOW(helper.getWidget("main_window")));
			}).detach();
		}
	} else {
		std::thread([filename, parent, helper]() {
			for (int i = 0; i < io->part_count; i++)
				if (!strcmp(gPartInfo.name, (*(io->ptable + i)).name)) {
					load_partition_force(io, i, filename.c_str(), blk_size ? blk_size : DEFAULT_BLK_SIZE, 0);
					break;
				}
			gui_idle_call_wait_drag([parent, helper]() mutable {
				showInfoDialog(GTK_WINDOW(parent), _(_(_("Completed"))), _("Partition force write completed!"));
				helper.setLabelText(helper.getWidget("con"), "Ready");
			},GTK_WINDOW(helper.getWidget("main_window")));
		}).detach();
	}

}

void on_button_clicked_list_read(GtkWidgetHelper helper) {
	GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
	std::string part_name = getSelectedPartitionName(helper);
	std::string savePath = showSaveFileDialog(parent, part_name + ".img");
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));

	}
	if (savePath.empty()) {
		showErrorDialog(parent, _(_(_("Error"))), _("No save path selected!"));
		return;
	}
	if (io->part_count == 0 && io->part_count_c == 0) {
		showErrorDialog(parent, _(_(_("Error"))), _("No partition table loaded, cannot write partition list!"));
		return;
	}
	helper.setLabelText(helper.getWidget("con"), "Reading partition");
	//dump_partition(io, "splloader", 0, g_spl_size, "splloader.bin, blk_size ? blk_size : DEFAULT_BLK_SIZE);
	get_partition_info(io, part_name.c_str(), 1);
	if (!gPartInfo.size) {
		DEG_LOG(E, "Partition not exist\n");
		return;
	}
	std::thread([savePath, parent, helper]() {
		dump_partition(io, gPartInfo.name, 0, gPartInfo.size, savePath.c_str(), blk_size ? blk_size : DEFAULT_BLK_SIZE);
		gui_idle_call_wait_drag([parent, helper]() mutable {
			showInfoDialog(GTK_WINDOW(parent), _(_(_("Completed"))), _("Partition read completed!"));
			helper.setLabelText(helper.getWidget("con"), "Ready");
		},GTK_WINDOW(helper.getWidget("main_window")));
	}).detach();

}

void on_button_clicked_list_erase(GtkWidgetHelper helper) {
	GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
	std::string part_name = getSelectedPartitionName(helper);
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));

	}
	helper.setLabelText(helper.getWidget("con"), "Erase partition");
	get_partition_info(io, part_name.c_str(), 0);
	if (!gPartInfo.size) {
		DEG_LOG(E, "Partition not exist\n");
		return;
	}
	std::thread([parent, helper]() {
		erase_partition(io, gPartInfo.name, isCMethod);
		gui_idle_call_wait_drag([parent, helper]() mutable {
			showInfoDialog(GTK_WINDOW(parent), _(_(_("Completed"))), _("Partition erase completed!"));
			helper.setLabelText(helper.getWidget("con"), "Ready");
		},GTK_WINDOW(helper.getWidget("main_window")));
	}).detach();

}

void on_button_clicked_modify_part(GtkWidgetHelper helper) {
	if (io->part_count == 0 && io->part_count_c == 0) {
		showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("No partition table loaded, cannot modify partition size!"));
		return;
	}
	GtkWindow* window = GTK_WINDOW(helper.getWidget("main_window"));
	std::string part_name = getSelectedPartitionName(helper);
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));

	}
	const char* secondPartName = gtk_entry_get_text(GTK_ENTRY(helper.getWidget("modify_second_part")));
	const char* newSizeStr = gtk_entry_get_text(GTK_ENTRY(helper.getWidget("modify_new_size")));
	if (strlen(secondPartName) == 0 || strlen(newSizeStr) == 0) {
		showErrorDialog(window, _(_(_("Error"))), _("Please fill in complete modification info!"));
		return;
	}
	int newSizeMB = atoi(newSizeStr);
	if (newSizeMB <= 0) {
		showErrorDialog(window, _(_(_("Error"))), _("Please enter a valid new size!"));
		return;
	}
	helper.setLabelText(helper.getWidget("con"), _(_("Modify partition table")));
	bool i_is = false;
	if(isCMethod) i_is = showConfirmDialog(window, _(_(_("Warning"))), _("Currently in compatibility-method-PartList mode, modifying partition may brick the device!"));
	std::thread([secondPartName, newSizeMB, window, helper, part_name, i_is](){
		int i_part = 0;
		int i_se_part = 0;
		if (!isCMethod) {
			for (i_part = 0; i_part < io->part_count; i_part++) {
				if (!strcmp(part_name.c_str(), (*(io->ptable + i_part)).name)) {
					break;
				}
			}
			if (i_part == io->part_count) {
				DEG_LOG(E, "Partition not exist\n");
				gui_idle_call_wait_drag([window]() {
					showErrorDialog(window, _(_(_("Error"))), _("Partition does not exist!"));
				},GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			for (i_se_part = 0; i_se_part < io->part_count; i_se_part++) {
				if (!strcmp(secondPartName, (*(io->ptable + i_se_part)).name)) {
					break;
				}
			}
			if (i_se_part == io->part_count) {
				DEG_LOG(E, "Second partition not exist\n");
				gui_idle_call_wait_drag([window]() {
					showErrorDialog(window, _(_(_("Error"))), _("Second partition does not exist!"));
				},GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			long long k = (*(io->ptable + i_part)).size << 20;
			(*(io->ptable + i_part)).size = (long long)newSizeMB << 20;
			(*(io->ptable + i_se_part)).size = (*(io->ptable + i_se_part)).size + k - ((long long)newSizeMB << 20);
			FILE* fo = my_oxfopen("partition_temp.xml", "wb");
			if (!fo) ERR_EXIT("Failed to open file\n");
			fprintf(fo, "<Partitions>\n");
			for (int i = 0; i < io->part_count; i++) {
				fprintf(fo, "    <Partition id=\"%s\" size=\"", (*(io->ptable + i)).name);
				if (i + 1 == io->part_count) fprintf(fo, "0x%x\"/>\n", ~0);
				else fprintf(fo, "%lld\"/>\n", ((*(io->ptable + i)).size >> 20));
			}
			fprintf(fo, "</Partitions>");
			fclose(fo);
			uint8_t* buf = io->temp_buf;
			int n = scan_xml_partitions(io, "partition_temp.xml", buf, 0xffff);
			if (n <= 0) {
				DEG_LOG(E, "Failed to parse modified partition table\n");
				gui_idle_call_wait_drag([window]() {
					showErrorDialog(window, _(_(_("Error"))), _("Failed to parse modified partition table!"));
				},GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			encode_msg_nocpy(io, BSL_CMD_REPARTITION, n * 0x4c);
			if (!send_and_check(io)) gpt_failed = 0;
		}
		else {

		    if(!i_is) return;

			for (i_part = 0; i_part < io->part_count_c; i_part++) {
				if (!strcmp(part_name.c_str(), (*(io->Cptable + i_part)).name)) {
					break;
				}
			}
			if (i_part == io->part_count_c) {
				DEG_LOG(E, "Partition not exist\n");
				gui_idle_call_wait_drag([window]() {
					showErrorDialog(window, _(_(_("Error"))), _("Partition does not exist!"));
				},GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			for (i_se_part = 0; i_se_part < io->part_count_c; i_se_part++) {
				if (!strcmp(secondPartName, (*(io->Cptable + i_se_part)).name)) {
					break;
				}
			}
			if (i_se_part == io->part_count_c) {
				DEG_LOG(E, "Second partition not exist\n");
				gui_idle_call_wait_drag([window]() {
					showErrorDialog(window, _(_(_("Error"))), _("Second partition does not exist!"));
				},GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			long long k = (*(io->Cptable + i_part)).size << 20;
			(*(io->Cptable + i_part)).size = (long long)newSizeMB << 20;
			(*(io->Cptable + i_se_part)).size = (*(io->Cptable + i_se_part)).size + k - ((long long)newSizeMB << 20);
			FILE* fo = my_oxfopen("partition_temp.xml", "wb");
			if (!fo) ERR_EXIT("Failed to open file\n");
			fprintf(fo, "<Partitions>\n");
			for (int i = 0; i < io->part_count_c; i++) {
				fprintf(fo, "    <Partition id=\"%s\" size=\"", (*(io->Cptable + i)).name);
				if (i + 1 == io->part_count_c) fprintf(fo, "0x%x\"/>\n", ~0);
				else fprintf(fo, "%lld\"/>\n", ((*(io->Cptable + i)).size >> 20));
			}
			fprintf(fo, "</Partitions>");
			fclose(fo);
			uint8_t* buf = io->temp_buf;
			int n = scan_xml_partitions(io, "partition_temp.xml", buf, 0xffff);
			if (n <= 0) {
				DEG_LOG(E, "Failed to parse modified partition table\n");
				gui_idle_call_wait_drag([window]() {
					showErrorDialog(window, _(_(_("Error"))), _("Failed to parse modified partition table!"));
				},GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			encode_msg_nocpy(io, BSL_CMD_REPARTITION, n * 0x4c);
			if (!send_and_check(io)) gpt_failed = 0;
		}
		gui_idle_call_wait_drag([window, helper]() mutable {
			showInfoDialog(window, _(_(_("Completed"))), _("Partition modification completed!"));
			std::vector<partition_t> partitions;
			partitions.reserve(io->part_count);

			for (int i = 0; i < io->part_count; i++) {
				partitions.push_back(io->ptable[i]);
			}
			update_partition_size(io);
			populatePartitionList(helper, partitions);
			helper.setLabelText(helper.getWidget("con"), "Ready");
		}, window);
	}).detach();
		if(isCMethod){
			delete[] io->Cptable;
			io->Cptable = nullptr;
			io->part_count_c = 0;
			isCMethod = 0;
	    }
}

void on_button_clicked_xml_get(GtkWidgetHelper helper) {
    GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));

	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));

	}
	std::string filename = showFileChooser(parent, true);
	if(filename.empty()){return;}
	uint8_t* buf = io->temp_buf;
	int n = scan_xml_partitions(io, filename.c_str(), buf, 0xffff);
	if(n <= 0) return;
	std::vector<partition_t> partitions;
	partitions.reserve(io->part_count);

	for (int i = 0; i < io->part_count; i++) {
		partitions.push_back(io->ptable[i]);
	}
	populatePartitionList(helper, partitions);
    if(isCMethod){
		delete[] io->Cptable;
		io->Cptable = nullptr;
		io->part_count_c = 0;
		isCMethod = 0;
	}
}

void on_button_clicked_modify_new_part(GtkWidgetHelper helper) {
	if (io->part_count == 0 && io->part_count_c == 0) {
		showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("No partition table loaded, cannot modify partition size!"));
		return;
	}
	GtkWindow* window = GTK_WINDOW(helper.getWidget("main_window"));
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));

	}
	std::string newPartName = helper.getEntryText(helper.getWidget("new_part"));
	const char* newSizeText = helper.getEntryText(helper.getWidget("modify_add_size"));
	const char* beforePart = helper.getEntryText(helper.getWidget("before_new_part"));
	if(newPartName.empty()){
		showErrorDialog(window, _(_(_("Error"))), _("Please fill in complete modification info!"));
		return;
	}
	long long newPartSize = strtoll(newSizeText,nullptr,0);
	if(newPartSize <= 0){
		showErrorDialog(window, _(_(_("Error"))), _("Please enter a valid new size!"));
		return;
	}
	helper.setLabelText(helper.getWidget("con"), _(_("Modify partition table")));
	bool i_is = false;
	if(isCMethod) i_is = showConfirmDialog(window, _(_(_("Warning"))), _("Currently in compatibility-method-PartList mode, modifying partition may brick the device!"));
	std::thread([window, newPartName, helper, newPartSize, beforePart, i_is]() mutable {
		if(!isCMethod) {
			partition_t* ptable = NEWN partition_t[128 * sizeof(partition_t)];
			if (ptable == nullptr) return;
			int k = io->part_count;

			for (int i = 0; i < io->part_count; i++) {
				if (strcmp(io->ptable[i].name, newPartName.c_str()) == 0) {
					DEG_LOG(W, "Partition %s already exists", newPartName.c_str());
					showErrorDialog(window, _(_(_("Error"))), _("Partition already exists!"));
					return;
				}
			}
			int i_o = 0;
			for (i_o=0;i_o < io->part_count;i_o++){
				if(strcmp(beforePart,(*(io->ptable + i_o)).name) == 0){
					break;
				}
			}
			if(i_o == io->part_count){
				showErrorDialog(window, _(_(_("Error"))), _("Partition after does not exist!"));
				return;
			}
			int i_op = 0;
			for (i_op = 0; i_op < io->part_count; i_op++) {
				if(strcmp(beforePart,(*(io->ptable + i_op)).name) != 0){
					strncpy(ptable[i_op].name, io->ptable[i_op].name, sizeof(ptable[i_op].name) - 1);
					ptable[i_op].name[sizeof(ptable[i_op].name) - 1] = '\0';
					ptable[i_op].size = io->ptable[i_op].size;
				}
				else{
					break;
				}
			}
			strncpy(ptable[i_op].name, newPartName.c_str(), sizeof(ptable[i_op].name) - 1);
			ptable[i_op].name[sizeof(ptable[i_op].name) - 1] = '\0';
			ptable[i_op].size = newPartSize << 20;
			for (i_op; i_op < io->part_count; i_op++) {
				strncpy(ptable[i_op + 1].name, io->ptable[i_op].name, sizeof(ptable[i_op + 1].name) - 1);
				ptable[i_op + 1].name[sizeof(ptable[i_op + 1].name) - 1] = '\0';
				ptable[i_op + 1].size = io->ptable[i_op].size;
			}
			io->ptable = ptable;
			io->part_count++;
			FILE* fo = my_oxfopen("partition_temp.xml", "wb");
			if (!fo) ERR_EXIT("Failed to open file\n");
			fprintf(fo, "<Partitions>\n");
			for (int i = 0; i < io->part_count; i++) {
				fprintf(fo, "    <Partition id=\"%s\" size=\"", (*(io->ptable + i)).name);
				if (i + 1 == io->part_count) fprintf(fo, "0x%x\"/>\n", ~0);
				else fprintf(fo, "%lld\"/>\n", ((*(io->ptable + i)).size >> 20));
			}
			fprintf(fo, "</Partitions>");
			fclose(fo);
			uint8_t* buf = io->temp_buf;
			int n = scan_xml_partitions(io, "partition_temp.xml", buf, 0xffff);
			if (n <= 0) {
				DEG_LOG(E, "Failed to parse modified partition table\n");
				gui_idle_call_wait_drag([window]() {
					showErrorDialog(window, _(_(_("Error"))), _("Failed to parse modified partition table!"));
				},GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			encode_msg_nocpy(io, BSL_CMD_REPARTITION, n * 0x4c);
			if (!send_and_check(io)) gpt_failed = 0;
		}
		else {
		    if(!i_is) return;
			partition_t* ptable = NEWN partition_t[128 * sizeof(partition_t)];
			if (ptable == nullptr) return;
			int k = io->part_count_c;
			for (int i = 0; i < io->part_count_c; i++) {
				if (strcmp(io->Cptable[i].name, newPartName.c_str()) == 0) {
					DEG_LOG(W, "Partition %s already exists", newPartName.c_str());
					showErrorDialog(window, _(_(_("Error"))), _("Partition already exists!"));
					return;
				}
			}
			int i_o = 0;
			for (i_o=0;i_o < io->part_count_c;i_o++){
				if(strcmp(beforePart,(*(io->Cptable + i_o)).name) == 0){
					break;
				}
			}
			if(i_o == io->part_count_c){
				showErrorDialog(window, _(_(_("Error"))), _("Partition after does not exist!"));
				return;
			}
			int i_op = 0;
			for (i_op = 0; i_op < io->part_count_c; i_op++) {
				if(strcmp(beforePart,(*(io->Cptable + i_op)).name) != 0){
					strncpy(ptable[i_op].name, io->Cptable[i_op].name, sizeof(ptable[i_op].name) - 1);
					ptable[i_op].name[sizeof(ptable[i_op].name) - 1] = '\0';
					ptable[i_op].size = io->Cptable[i_op].size;
				}
				else{
					break;
				}
			}
			strncpy(ptable[i_op].name, newPartName.c_str(), sizeof(ptable[i_op].name) - 1);
			ptable[i_op].name[sizeof(ptable[i_op].name) - 1] = '\0';
			ptable[i_op].size = newPartSize << 20;
			for (i_op; i_op < io->part_count_c; i_op++) {
				strncpy(ptable[i_op + 1].name, io->ptable[i_op].name, sizeof(ptable[i_op + 1].name) - 1);
				ptable[i_op + 1].name[sizeof(ptable[i_op + 1].name) - 1] = '\0';
				ptable[i_op + 1].size = io->ptable[i_op].size;
			}
			io->Cptable = ptable;
			io->part_count_c++;
			FILE* fo = my_oxfopen("partition_temp.xml", "wb");
			if (!fo) ERR_EXIT("Failed to open file\n");
			fprintf(fo, "<Partitions>\n");
			for (int i = 0; i < io->part_count_c; i++) {
				fprintf(fo, "    <Partition id=\"%s\" size=\"", (*(io->Cptable + i)).name);
				if (i + 1 == io->part_count_c) fprintf(fo, "0x%x\"/>\n", ~0);
				else fprintf(fo, "%lld\"/>\n", ((*(io->Cptable + i)).size >> 20));
			}
			fprintf(fo, "</Partitions>");
			fclose(fo);
			uint8_t* buf = io->temp_buf;
			int n = scan_xml_partitions(io, "partition_temp.xml", buf, 0xffff);
			if (n <= 0) {
				DEG_LOG(E, "Failed to parse modified partition table\n");
				gui_idle_call_wait_drag([window]() {
					showErrorDialog(window, _(_(_("Error"))), _("Failed to parse modified partition table!"));
				},GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			encode_msg_nocpy(io, BSL_CMD_REPARTITION, n * 0x4c);
			if (!send_and_check(io)) gpt_failed = 0;
		}
		gui_idle_call_wait_drag([window, helper]() mutable {
			showInfoDialog(window, _(_(_("Completed"))), _("Partition modification completed!"));
			std::vector<partition_t> partitions;
			partitions.reserve(io->part_count);

			for (int i = 0; i < io->part_count; i++) {
				partitions.push_back(io->ptable[i]);
			}
			update_partition_size(io);
			populatePartitionList(helper, partitions);
			helper.setLabelText(helper.getWidget("con"), "Ready");
		}, window);
	}).detach();
		if(isCMethod){
			delete[] io->Cptable;
			io->Cptable = nullptr;
			io->part_count_c = 0;
			isCMethod = 0;
		}
}

void on_button_clicked_modify_rm_part(GtkWidgetHelper helper) {
	if (io->part_count == 0 && io->part_count_c == 0) {
		showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("No partition table loaded, cannot modify partition size!"));
		return;
	}
	GtkWindow* window = GTK_WINDOW(helper.getWidget("main_window"));
	std::string part_name = getSelectedPartitionName(helper);
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));
	}
	helper.setLabelText(helper.getWidget("con"), _(_("Modify partition table")));
	bool i_is = false;
	if(isCMethod) i_is = showConfirmDialog(window, _(_(_("Warning"))), _("Currently in compatibility-method-PartList mode, modifying partition may brick the device!"));
	std::thread([part_name, helper, window, i_is]() mutable {
		int i = 0;
		if(!isCMethod){
			for(i = 0;i < io->part_count; i++) {
				if(strcmp((*(io->ptable + i)).name, part_name.c_str()) == 0){
					break;
				}
			}
			if(i == io->part_count){
				DEG_LOG(E, "Partition not exist\n");
				gui_idle_call_wait_drag([window]() {
					showErrorDialog(window, _(_(_("Error"))), _("Second partition does not exist!"));
				},GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			partition_t* ptable = NEWN partition_t[128 * sizeof(partition_t)];
			int new_index = 0;
			for (int j = 0; j < io->part_count; j++) {
				// 使用 strcmp 比较字符串内容
				if (strcmp(io->ptable[j].name, part_name.c_str()) != 0) {
					// 复制不需要删除的分区到新表
					strncpy(ptable[new_index].name, io->ptable[j].name, sizeof(ptable[new_index].name) - 1);
					ptable[new_index].name[sizeof(ptable[new_index].name) - 1] = '\0';
					ptable[new_index].size = io->ptable[j].size;
					new_index++;
				}
			}

			// 更新 io 结构
			io->part_count--;
			// 注意：需要释放原来的 ptable 内存
			delete[] (io->ptable);
			io->ptable = ptable;
			FILE* fo = my_oxfopen("partition_temp.xml", "wb");
			if (!fo) ERR_EXIT("Failed to open file\n");
			fprintf(fo, "<Partitions>\n");
			for (int i = 0; i < io->part_count; i++) {
				fprintf(fo, "    <Partition id=\"%s\" size=\"", (*(io->ptable + i)).name);
				if (i + 1 == io->part_count) fprintf(fo, "0x%x\"/>\n", ~0);
				else fprintf(fo, "%lld\"/>\n", ((*(io->ptable + i)).size >> 20));
			}
			fprintf(fo, "</Partitions>");
			fclose(fo);
			uint8_t* buf = io->temp_buf;
			int n = scan_xml_partitions(io, "partition_temp.xml", buf, 0xffff);
			if (n <= 0) {
				DEG_LOG(E, "Failed to parse modified partition table\n");
				gui_idle_call_wait_drag([window]() {
					showErrorDialog(window, _(_(_("Error"))), _("Failed to parse modified partition table!"));
				},GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			encode_msg_nocpy(io, BSL_CMD_REPARTITION, n * 0x4c);
			if (!send_and_check(io)) gpt_failed = 0;
		}
		else{

		    if(!i_is) return;
			for(i = 0;i < io->part_count_c; i++) {
				if(strcmp((*(io->Cptable + i)).name, part_name.c_str())){
					break;
				}
			}
			if(i == io->part_count_c){
				DEG_LOG(E, "Partition not exist\n");
				gui_idle_call_wait_drag([window]() {
					showErrorDialog(window, _(_(_("Error"))), _("Second partition does not exist!"));
				},GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			partition_t* ptable = NEWN partition_t[128 * sizeof(partition_t)];
			int new_index = 0;
			for (int j = 0; j < io->part_count_c; j++) {
				// 使用 strcmp 比较字符串内容
				if (strcmp(io->ptable[j].name, part_name.c_str()) != 0) {
					// 复制不需要删除的分区到新表
					strncpy(ptable[new_index].name, io->Cptable[j].name, sizeof(ptable[new_index].name) - 1);
					ptable[new_index].name[sizeof(ptable[new_index].name) - 1] = '\0';
					ptable[new_index].size = io->Cptable[j].size;
					new_index++;
				}
			}

			// 更新 io 结构
			io->part_count_c--;
			// 注意：需要释放原来的 ptable 内存
			delete[] (io->Cptable);
			io->Cptable = ptable;
			FILE* fo = my_oxfopen("partition_temp.xml", "wb");
			if (!fo) ERR_EXIT("Failed to open file\n");
			fprintf(fo, "<Partitions>\n");
			for (int i = 0; i < io->part_count_c; i++) {
				fprintf(fo, "    <Partition id=\"%s\" size=\"", (*(io->Cptable + i)).name);
				if (i + 1 == io->part_count_c) fprintf(fo, "0x%x\"/>\n", ~0);
				else fprintf(fo, "%lld\"/>\n", ((*(io->Cptable + i)).size >> 20));
			}
			fprintf(fo, "</Partitions>");
			fclose(fo);
			uint8_t* buf = io->temp_buf;
			int n = scan_xml_partitions(io, "partition_temp.xml", buf, 0xffff);
			if (n <= 0) {
				DEG_LOG(E, "Failed to parse modified partition table\n");
				gui_idle_call_wait_drag([window]() {
					showErrorDialog(window, _(_(_("Error"))), _("Failed to parse modified partition table!"));
				},GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			encode_msg_nocpy(io, BSL_CMD_REPARTITION, n * 0x4c);
			if (!send_and_check(io)) gpt_failed = 0;
		}
		gui_idle_call_wait_drag([window, helper]() mutable {
			showInfoDialog(window, _(_(_("Completed"))), _("Partition modification completed!"));
			std::vector<partition_t> partitions;
			partitions.reserve(io->part_count);

			for (int i = 0; i < io->part_count; i++) {
				partitions.push_back(io->ptable[i]);
			}
			update_partition_size(io);
			populatePartitionList(helper, partitions);
			helper.setLabelText(helper.getWidget("con"), "Ready");
		}, window);
	}).detach();
		if(isCMethod){
			delete[] io->Cptable;
			io->Cptable = nullptr;
			io->part_count_c = 0;
			isCMethod = 0;
		}
}

void on_button_clicked_modify_ren_part(GtkWidgetHelper helper) {
	if (io->part_count == 0 && io->part_count_c == 0) {
		showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("No partition table loaded, cannot modify partition size!"));
		return;
	}
	GtkWindow* window = GTK_WINDOW(helper.getWidget("main_window"));
	std::string part_name = getSelectedPartitionName(helper);
	std::string new_part_name = helper.getEntryText(helper.getWidget("modify_rename_part"));
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));

	}
	if(part_name.empty() || new_part_name.empty()){
		showErrorDialog(window, _(_(_("Error"))), _("Please fill in complete modification info!"));
		return;
	}
	helper.setLabelText(helper.getWidget("con"), _(_("Modify partition table")));
	bool i_is = false;
	if(isCMethod) i_is = showConfirmDialog(window, _(_(_("Warning"))), _("Currently in compatibility-method-PartList mode, modifying partition may brick the device!"));
	std::thread([part_name, new_part_name, helper, window, i_is]() mutable {
		int i = 0;
		if(!isCMethod){
			for(i = 0;i < io->part_count; i++) {
				if(strcmp((*(io->ptable + i)).name, part_name.c_str()) == 0){
					break;
				}
			}
			if(i == io->part_count){
				DEG_LOG(E, "Partition not exist\n");
				gui_idle_call_wait_drag([window]() {
					showErrorDialog(window, _(_(_("Error"))), _("Second partition does not exist!"));
				},GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}

			strncpy(io->ptable[i].name, new_part_name.c_str(), sizeof(io->ptable[i].name) - 1);
			io->ptable[i].name[sizeof(io->ptable[i].name) - 1] = '\0';

			FILE* fo = my_oxfopen("partition_temp.xml", "wb");
			if (!fo) ERR_EXIT("Failed to open file\n");
			fprintf(fo, "<Partitions>\n");
			for (int i = 0; i < io->part_count; i++) {
				fprintf(fo, "    <Partition id=\"%s\" size=\"", (*(io->ptable + i)).name);
				if (i + 1 == io->part_count) fprintf(fo, "0x%x\"/>\n", ~0);
				else fprintf(fo, "%lld\"/>\n", ((*(io->ptable + i)).size >> 20));
			}
			fprintf(fo, "</Partitions>");
			fclose(fo);
			uint8_t* buf = io->temp_buf;
			int n = scan_xml_partitions(io, "partition_temp.xml", buf, 0xffff);
			if (n <= 0) {
				DEG_LOG(E, "Failed to parse modified partition table\n");
				gui_idle_call_wait_drag([window]() {
					showErrorDialog(window, _(_(_("Error"))), _("Failed to parse modified partition table!"));
				},GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			encode_msg_nocpy(io, BSL_CMD_REPARTITION, n * 0x4c);
			if (!send_and_check(io)) gpt_failed = 0;
		}
		else{


		    if(!i_is) return;
			for(i = 0;i < io->part_count_c; i++) {
				if(strcmp((*(io->Cptable + i)).name, part_name.c_str())){
					break;
				}
			}
			if(i == io->part_count_c){
				DEG_LOG(E, "Partition not exist\n");
				gui_idle_call_wait_drag([window]() {
					showErrorDialog(window, _(_(_("Error"))), _("Second partition does not exist!"));
				},GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}

			strncpy(io->Cptable[i].name, part_name.c_str(), sizeof(io->Cptable[i].name) - 1);
			io->Cptable[i].name[sizeof(io->ptable[i].name) - 1] = '\0';

			FILE* fo = my_oxfopen("partition_temp.xml", "wb");
			if (!fo) ERR_EXIT("Failed to open file\n");
			fprintf(fo, "<Partitions>\n");
			for (int i = 0; i < io->part_count_c; i++) {
				fprintf(fo, "    <Partition id=\"%s\" size=\"", (*(io->Cptable + i)).name);
				if (i + 1 == io->part_count_c) fprintf(fo, "0x%x\"/>\n", ~0);
				else fprintf(fo, "%lld\"/>\n", ((*(io->Cptable + i)).size >> 20));
			}
			fprintf(fo, "</Partitions>");
			fclose(fo);
			uint8_t* buf = io->temp_buf;
			int n = scan_xml_partitions(io, "partition_temp.xml", buf, 0xffff);
			if (n <= 0) {
				DEG_LOG(E, "Failed to parse modified partition table\n");
				gui_idle_call_wait_drag([window]() {
					showErrorDialog(window, _(_(_("Error"))), _("Failed to parse modified partition table!"));
				},GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			encode_msg_nocpy(io, BSL_CMD_REPARTITION, n * 0x4c);
			if (!send_and_check(io)) gpt_failed = 0;
		}
		gui_idle_call_wait_drag([window, helper]() mutable {
			showInfoDialog(window, _(_(_("Completed"))), _("Partition modification completed!"));
			std::vector<partition_t> partitions;
			partitions.reserve(io->part_count);

			for (int i = 0; i < io->part_count; i++) {
				partitions.push_back(io->ptable[i]);
			}
			// update_partition_size(io);
			populatePartitionList(helper, partitions);
			helper.setLabelText(helper.getWidget("con"), "Ready");
		}, window);
	}).detach();
		if(isCMethod){
			delete[] io->Cptable;
			io->Cptable = nullptr;
			io->part_count_c = 0;
			isCMethod = 0;
		}
}

void on_button_clicked_list_cancel(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));

	}
	signal_handler(0);
	showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Tips"), _("Current partition operation cancelled!"));
}

void on_button_clicked_backup_all(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));

	}
	helper.setLabelText(helper.getWidget("con"), "Backup partitions");
	std::thread([helper]() {
		if (!isCMethod) {
			if (gpt_failed == 1) io->ptable = partition_list(io, fn_partlist, &io->part_count);
			if (!io->part_count) {
				DEG_LOG(E, "Partition table not available\n");
				gui_idle_call_wait_drag([helper]() {
					showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Partition table not available!"));
				},GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			dump_partition(io, "splloader", 0, g_spl_size, "splloader.bin", blk_size ? blk_size : DEFAULT_BLK_SIZE);
			for (int i = 0; i < io->part_count; i++) {
				if (isCancel) break;
				char dfile[40];
				size_t namelen = strlen((*(io->ptable + i)).name);
				if (!strncmp((*(io->ptable + i)).name, "blackbox", 8)) continue;
				else if (!strncmp((*(io->ptable + i)).name, "cache", 5)) continue;
				else if (!strncmp((*(io->ptable + i)).name, "userdata", 8)) continue;
				if (g_app_state.selected_ab == 1 && namelen > 2 && 0 == strcmp((*(io->ptable + i)).name + namelen - 2, "_b")) continue;
				else if (g_app_state.selected_ab == 2 && namelen > 2 && 0 == strcmp((*(io->ptable + i)).name + namelen - 2, "_a")) continue;
				snprintf(dfile, sizeof(dfile), "%s.bin", (*(io->ptable + i)).name);
				dump_partition(io, (*(io->ptable + i)).name, 0, (*(io->ptable + i)).size, dfile, blk_size ? blk_size : DEFAULT_BLK_SIZE);
			}
		} else {
			if (!io->part_count_c) {
				DEG_LOG(E, "Partition table not available\n");
				gui_idle_call_wait_drag([helper]() {
					showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Partition table not available!"));
				},GTK_WINDOW(helper.getWidget("main_window")));
				return;
			}
			dump_partition(io, "splloader", 0, g_spl_size, "splloader.bin", blk_size ? blk_size : DEFAULT_BLK_SIZE);
			for (int i = 0; i < io->part_count_c; i++) {
				if (isCancel) break;
				char dfile[40];
				size_t namelen = strlen((*(io->Cptable + i)).name);
				if (!strncmp((*(io->Cptable + i)).name, "blackbox", 8)) continue;
				else if (!strncmp((*(io->Cptable + i)).name, "cache", 5)) continue;
				else if (!strncmp((*(io->Cptable + i)).name, "userdata", 8)) continue;
				if (g_app_state.selected_ab == 1 && namelen > 2 && 0 == strcmp((*(io->Cptable + i)).name + namelen - 2, "_b")) continue;
				else if (g_app_state.selected_ab == 2 && namelen > 2 && 0 == strcmp((*(io->Cptable + i)).name + namelen - 2, "_a")) continue;
				snprintf(dfile, sizeof(dfile), "%s.bin", (*(io->Cptable + i)).name);
				dump_partition(io, (*(io->Cptable + i)).name, 0, (*(io->Cptable + i)).size, dfile, blk_size ? blk_size : DEFAULT_BLK_SIZE);
			}
		}
	}).detach();
    helper.setLabelText(helper.getWidget("con"), "Ready");
}

void confirm_partition_c(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));

	}
	gui_idle_call_with_callback(
		[helper]() -> bool {
			return showConfirmDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Confirm"), _("No partition table found on current device, read partition list through compatibility method?\nWarn: This mode may not find all partitions on your device, use caution with force write or editing partition table!"));
		},
			[helper](bool result) mutable {
			if (result) {
				isUseCptable = 1;
				io->Cptable = partition_list_d(io);
				isCMethod = 1;
				std::vector<partition_t> partitions;
				partitions.reserve(io->part_count_c);
				for (int i = 0; i < io->part_count_c; i++) {
					partitions.push_back(io->Cptable[i]);
				}
				populatePartitionList(helper, partitions);
			} else {
				DEG_LOG(W, "Partition table not read.");
			}
		},
		GTK_WINDOW(helper.getWidget("main_window"))
	);
}

GtkWidget* create_partition_page(GtkWidgetHelper& helper, GtkWidget* notebook) {
	// ── 页面根容器（垂直 Box，整体可滚动） ──
	GtkWidget* outerScroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(outerScroll),
	                               GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_hexpand(outerScroll, TRUE);
	gtk_widget_set_vexpand(outerScroll, TRUE);

	GtkWidget* mainBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_margin_start(mainBox, 16);
	gtk_widget_set_margin_end(mainBox, 16);
	gtk_widget_set_margin_top(mainBox, 10);
	gtk_widget_set_margin_bottom(mainBox, 10);
	gtk_container_add(GTK_CONTAINER(outerScroll), mainBox);

	// 把 outerScroll 作为 notebook 的 page
	helper.addWidget("part_page", outerScroll);
	helper.addNotebookPage(notebook, outerScroll, _("Partition Operation"));

	// ── 区块辅助 lambda：创建带内边距的垂直内容 box ──
	auto makeCardBox = [](int pad_h, int pad_v) -> GtkWidget* {
		GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
		gtk_widget_set_margin_start(box, pad_h);
		gtk_widget_set_margin_end(box, pad_h);
		gtk_widget_set_margin_top(box, pad_v);
		gtk_widget_set_margin_bottom(box, pad_v);
		return box;
	};

	// ══════════════════════════════════════════════
	//  第一部分：请选择一个分区
	// ══════════════════════════════════════════════
	GtkWidget* selectTitle = gtk_label_new(_("Please check a partition"));
	helper.addWidget("part_instruction", selectTitle);
	gtk_widget_set_halign(selectTitle, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_bottom(selectTitle, 6);
	gtk_box_pack_start(GTK_BOX(mainBox), selectTitle, FALSE, FALSE, 0);

	GtkWidget* listScroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_widget_set_size_request(listScroll, -1, 220);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(listScroll),
	                               GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	// 添加边框(阴影)使其看起来像表格区块
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(listScroll), GTK_SHADOW_IN);
	gtk_widget_set_hexpand(listScroll, TRUE);

	GtkListStore* store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	GtkWidget* treeView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	gtk_widget_set_name(treeView, "part_list");
	helper.addWidget("part_list", treeView);
	GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeView), -1,
	        _("Partition Name"), renderer, "text", 0, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeView), -1,
	        _("Size"), renderer, "text", 1, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeView), -1,
	        _("Type"), renderer, "text", 2, NULL);
	gtk_container_add(GTK_CONTAINER(listScroll), treeView);
	gtk_box_pack_start(GTK_BOX(mainBox), listScroll, FALSE, FALSE, 0);

	// ── 包含操作按钮的外框 ──
	GtkWidget* opFrame = gtk_frame_new(NULL);
	gtk_widget_set_margin_top(opFrame, 16);
	gtk_widget_set_margin_bottom(opFrame, 16);
	GtkWidget* opTitleLabel = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(opTitleLabel),
	    (std::string("<span size='large'><b>") + _("Operation") + "</b></span>").c_str());
	helper.addWidget("op_label", opTitleLabel);
	gtk_frame_set_label_widget(GTK_FRAME(opFrame), opTitleLabel);
	gtk_frame_set_label_align(GTK_FRAME(opFrame), 0.5, 0.5);

	GtkWidget* opContainerBox = makeCardBox(12, 10);
	gtk_container_add(GTK_CONTAINER(opFrame), opContainerBox);

	// ── 按钮行 1：五个主操作按钮 ──
	GtkWidget* writeBtn    = helper.createButton(_("WRITE"),       "list_write",       nullptr, 0, 0, -1, 32);
	GtkWidget* writeFBtn   = helper.createButton(_("FORCE WRITE"), "list_force_write", nullptr, 0, 0, -1, 32);
	GtkWidget* readBtn     = helper.createButton(_("EXTRACT"),     "list_read",        nullptr, 0, 0, -1, 32);
	GtkWidget* eraseBtn    = helper.createButton(_("ERASE"),       "list_erase",       nullptr, 0, 0, -1, 32);
	GtkWidget* backupAllBtn = helper.createButton(_("Backup All"), "backup_all",       nullptr, 0, 0, -1, 32);

	GtkWidget* btnRow1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_halign(btnRow1, GTK_ALIGN_FILL);
	gtk_box_pack_start(GTK_BOX(btnRow1), writeBtn,     TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(btnRow1), writeFBtn,    TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(btnRow1), readBtn,      TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(btnRow1), eraseBtn,     TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(btnRow1), backupAllBtn, TRUE, TRUE, 0);
	gtk_widget_set_margin_bottom(btnRow1, 8);
	gtk_box_pack_start(GTK_BOX(opContainerBox), btnRow1, FALSE, FALSE, 0);

	// ── 按钮行 2：取消 + XML获取 ──
	GtkWidget* cancelBtn = helper.createButton(_("Cancel"), "list_cancel", nullptr, 0, 0, 100, 32);
	GtkWidget* xmlGetBtn = helper.createButton(_("Get partition table through scanning an Xml file"), "xml_get", nullptr, 0, 0, -1, 32);

	GtkWidget* btnRow2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_pack_start(GTK_BOX(btnRow2), cancelBtn, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(btnRow2), xmlGetBtn, TRUE,  TRUE,  0);
	gtk_widget_set_margin_bottom(btnRow2, 16);
	gtk_box_pack_start(GTK_BOX(opContainerBox), btnRow2, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(mainBox), opFrame, FALSE, FALSE, 0);

	// 设置按钮初始状态
	gtk_widget_set_sensitive(writeBtn,    FALSE);
	gtk_widget_set_sensitive(readBtn,     FALSE);
	gtk_widget_set_sensitive(eraseBtn,    FALSE);
	gtk_widget_set_sensitive(backupAllBtn, TRUE);
	gtk_widget_set_sensitive(cancelBtn,   TRUE);

	// ══════════════════════════════════════════════
	//  第二部分：包含修改分区的最大外框
	// ══════════════════════════════════════════════
	GtkWidget* modifyPageFrame = gtk_frame_new(NULL);
	gtk_widget_set_margin_bottom(modifyPageFrame, 16);
	GtkWidget* modifyPageTitle = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(modifyPageTitle),
	    (std::string("<span size='large'><b>") + _("Modify Partition Table") + "</b></span>").c_str());
	helper.addWidget("modify_label", modifyPageTitle);
	gtk_frame_set_label_widget(GTK_FRAME(modifyPageFrame), modifyPageTitle);
	gtk_frame_set_label_align(GTK_FRAME(modifyPageFrame), 0.5, 0.5);

	GtkWidget* modifyPageBox = makeCardBox(12, 10);
	gtk_container_add(GTK_CONTAINER(modifyPageFrame), modifyPageBox);

	// ══════════════════════════════════════════════
	//  卡片1：修改分区大小
	// ══════════════════════════════════════════════
	GtkWidget* sizeFrame = gtk_frame_new(NULL);
	gtk_widget_set_margin_bottom(sizeFrame, 10);
	GtkWidget* sizeTitleLabel = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(sizeTitleLabel),
	    (std::string("<b>") + _("Change size") + "</b>").c_str());
	helper.addWidget("fff_label", sizeTitleLabel);
	gtk_frame_set_label_widget(GTK_FRAME(sizeFrame), sizeTitleLabel);
	gtk_frame_set_label_align(GTK_FRAME(sizeFrame), 0.5, 0.5);

	GtkWidget* sizeCardBox = makeCardBox(12, 10);
	gtk_container_add(GTK_CONTAINER(sizeFrame), sizeCardBox);

	// 卡片说明
	GtkWidget* sizeDescLabel = gtk_label_new(_("Please check a partition you want to change"));
	gtk_widget_set_halign(sizeDescLabel, GTK_ALIGN_START);
	gtk_widget_set_margin_bottom(sizeDescLabel, 6);
	gtk_box_pack_start(GTK_BOX(sizeCardBox), sizeDescLabel, FALSE, FALSE, 0);

	// 控件行
	GtkWidget* sizeCtrlRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget* SeLabel     = gtk_label_new(_("Second-change partition"));
	helper.addWidget("second_part_label", SeLabel);
	GtkWidget* secondPart  = helper.createEntry("modify_second_part", "", false, 0, 0, 180, 32);
	GtkWidget* newSizeLabel = gtk_label_new(_("New size (MB)"));
	helper.addWidget("new_size_label", newSizeLabel);
	GtkWidget* newSizeEntry = helper.createEntry("modify_new_size", "", false, 0, 0, 120, 32);
	GtkWidget* modifyBtn    = helper.createButton(_("Modify"), "modify_part", nullptr, 0, 0, 80, 32);
	gtk_box_pack_start(GTK_BOX(sizeCtrlRow), SeLabel,     FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(sizeCtrlRow), secondPart,  FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(sizeCtrlRow), newSizeLabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(sizeCtrlRow), newSizeEntry, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(sizeCtrlRow), modifyBtn,   FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(sizeCardBox), sizeCtrlRow, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(modifyPageBox), sizeFrame, FALSE, FALSE, 0);

	// ══════════════════════════════════════════════
	//  卡片2：添加分区
	// ══════════════════════════════════════════════
	GtkWidget* addFrame = gtk_frame_new(NULL);
	gtk_widget_set_margin_bottom(addFrame, 10);
	GtkWidget* addTitleLabel = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(addTitleLabel),
	    (std::string("<b>") + _("Add partition") + "</b>").c_str());
	helper.addWidget("ff_label", addTitleLabel);
	gtk_frame_set_label_widget(GTK_FRAME(addFrame), addTitleLabel);
	gtk_frame_set_label_align(GTK_FRAME(addFrame), 0.5, 0.5);

	GtkWidget* addCardBox = makeCardBox(12, 10);
	gtk_container_add(GTK_CONTAINER(addFrame), addCardBox);

	GtkWidget* addDescLabel = gtk_label_new(_("Enter partition name:"));
	gtk_widget_set_halign(addDescLabel, GTK_ALIGN_START);
	gtk_widget_set_margin_bottom(addDescLabel, 6);
	gtk_box_pack_start(GTK_BOX(addCardBox), addDescLabel, FALSE, FALSE, 0);

	GtkWidget* addCtrlRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget* Add2Label   = gtk_label_new(_("Part name:"));
	helper.addWidget("f_label", Add2Label);
	GtkWidget* newPartEntry = helper.createEntry("new_part", "", false, 0, 0, 160, 32);
	GtkWidget* new2SizeLabel = gtk_label_new(_("Size (MB):"));
	helper.addWidget("f3_label", new2SizeLabel);
	GtkWidget* newPartSize = helper.createEntry("modify_add_size", "", false, 0, 0, 100, 32);
	GtkWidget* afterPartLabel = gtk_label_new(_("Part after this new part:"));
	helper.addWidget("after_part_label", afterPartLabel);
	GtkWidget* afterPart      = helper.createEntry("before_new_part", "", false, 0, 0, 120, 32);
	GtkWidget* addNewPartBtn  = helper.createButton(_("Modify"), "modify_new_part", nullptr, 0, 0, 80, 32);
	gtk_box_pack_start(GTK_BOX(addCtrlRow), Add2Label,     FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(addCtrlRow), newPartEntry,  FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(addCtrlRow), new2SizeLabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(addCtrlRow), newPartSize,   FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(addCtrlRow), afterPartLabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(addCtrlRow), afterPart,     FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(addCtrlRow), addNewPartBtn, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(addCardBox), addCtrlRow, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(modifyPageBox), addFrame, FALSE, FALSE, 0);

	// ══════════════════════════════════════════════
	//  卡片3：移除分区
	// ══════════════════════════════════════════════
	GtkWidget* rmFrame = gtk_frame_new(NULL);
	gtk_widget_set_margin_bottom(rmFrame, 10);
	GtkWidget* rmTitleLabel = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(rmTitleLabel),
	    (std::string("<b>") + _("Remove partition") + "</b>").c_str());
	helper.addWidget("ffff_label", rmTitleLabel);
	gtk_frame_set_label_widget(GTK_FRAME(rmFrame), rmTitleLabel);
	gtk_frame_set_label_align(GTK_FRAME(rmFrame), 0.5, 0.5);

	GtkWidget* rmCardBox = makeCardBox(12, 10);
	gtk_container_add(GTK_CONTAINER(rmFrame), rmCardBox);

	GtkWidget* rmDescLabel = gtk_label_new(_("Please check a partition you want to remove"));
	gtk_widget_set_halign(rmDescLabel, GTK_ALIGN_START);
	gtk_widget_set_margin_bottom(rmDescLabel, 6);
	gtk_box_pack_start(GTK_BOX(rmCardBox), rmDescLabel, FALSE, FALSE, 0);

	// 删除按钮（红色样式）
	GtkWidget* RemvPartBtn = helper.createButton(_("Delete"), "modify_rm_part", nullptr, 0, 0, 80, 32);
	gtk_widget_set_name(RemvPartBtn, "danger_button");
	gtk_style_context_add_class(gtk_widget_get_style_context(RemvPartBtn), "destructive-action");
	gtk_widget_set_halign(RemvPartBtn, GTK_ALIGN_START);
	gtk_box_pack_start(GTK_BOX(rmCardBox), RemvPartBtn, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(modifyPageBox), rmFrame, FALSE, FALSE, 0);

	// ══════════════════════════════════════════════
	//  卡片4：重命名分区
	// ══════════════════════════════════════════════
	GtkWidget* renFrame = gtk_frame_new(NULL);
	gtk_widget_set_margin_bottom(renFrame, 10);
	GtkWidget* renTitleLabel = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(renTitleLabel),
	    (std::string("<b>") + _("Rename partition") + "</b>").c_str());
	helper.addWidget("f2_label", renTitleLabel);
	gtk_frame_set_label_widget(GTK_FRAME(renFrame), renTitleLabel);
	gtk_frame_set_label_align(GTK_FRAME(renFrame), 0.5, 0.5);

	GtkWidget* renCardBox = makeCardBox(12, 10);
	gtk_container_add(GTK_CONTAINER(renFrame), renCardBox);

	GtkWidget* renDescLabel = gtk_label_new(_("Please check a partition you want to rename"));
	gtk_widget_set_halign(renDescLabel, GTK_ALIGN_START);
	gtk_widget_set_margin_bottom(renDescLabel, 6);
	gtk_box_pack_start(GTK_BOX(renCardBox), renDescLabel, FALSE, FALSE, 0);

	GtkWidget* renCtrlRow   = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget* RenmPartLabel = gtk_label_new(_("New name"));
	helper.addWidget("f4_label", RenmPartLabel);
	GtkWidget* RenmPartEntry = helper.createEntry("modify_rename_part", "", false, 0, 0, 200, 32);
	GtkWidget* RenmPartBtn   = helper.createButton(_("Modify"), "modify_ren_part", nullptr, 0, 0, 80, 32);
	gtk_box_pack_start(GTK_BOX(renCtrlRow), RenmPartLabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(renCtrlRow), RenmPartEntry, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(renCtrlRow), RenmPartBtn,   FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(renCardBox), renCtrlRow, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(modifyPageBox), renFrame, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(mainBox), modifyPageFrame, FALSE, FALSE, 0);

	gtk_widget_show_all(outerScroll);
	return outerScroll;
}

void bind_partition_signals(GtkWidgetHelper& helper) {
	helper.bindClick(helper.getWidget("list_write"), [&]() {
		on_button_clicked_list_write(helper);
	});
	helper.bindClick(helper.getWidget("list_read"), [&]() {
		on_button_clicked_list_read(helper);
	});
	helper.bindClick(helper.getWidget("list_erase"), [&]() {
		on_button_clicked_list_erase(helper);
	});
	helper.bindClick(helper.getWidget("backup_all"),[&](){
		on_button_clicked_backup_all(helper);
	});
	helper.bindClick(helper.getWidget("list_cancel"), [&]() {
		on_button_clicked_list_cancel(helper);
	});
	helper.bindClick(helper.getWidget("list_force_write"), [&]() {
		on_button_clicked_list_force_write(helper);
	});
	helper.bindClick(helper.getWidget("modify_part"),[&]() {
		on_button_clicked_modify_part(helper);
	});
	helper.bindClick(helper.getWidget("modify_new_part"),[&](){
		on_button_clicked_modify_new_part(helper);
	});
	helper.bindClick(helper.getWidget("modify_rm_part"),[&](){
		on_button_clicked_modify_rm_part(helper);
	});
	helper.bindClick(helper.getWidget("modify_ren_part"),[&](){
		on_button_clicked_modify_ren_part(helper);
	});
	helper.bindClick(helper.getWidget("xml_get"),[&](){
		on_button_clicked_xml_get(helper);
	});
}
