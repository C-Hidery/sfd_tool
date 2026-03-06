#include <iostream>
#include <cstring>
#include "common.h"
#include "main.h"
#include "nlohmann/json.hpp" // json for auto sending FDL
#include "GtkWidgetHelper.hpp"
#include "i18n.h"
#include <thread>
#include <chrono>
#include <gtk/gtk.h>
#include "GenTosNoAvb.h"
#ifdef __linux__
#include <unistd.h>
#include <execinfo.h>
#elif defined(_WIN32)
#include <windows.h>
#include <dbghelp.h>
#endif
const char *AboutText = "SFD Tool GUI\n\nVersion 1.7.5.2 LTV Edition\n\nCopyright 2026 Ryan Crepa    QQ:3285087232    @Bilibili RyanCrepa\n\nVersion logs:\n\n---v 1.7.1.0---\nFirst GUI Version\n--v 1.7.1.1---\nFix check_confirm issue\n---v 1.7.1.2---\nAdd Force write function when partition list is available\n---v 1.7.2.0---\nAdd debug options\n---v 1.7.2.1---\nAdd root permission check for Linux\n---v 1.7.2.2---\nAdd dis_avb function\n---v 1.7.2.3---\nFix some bugs\n---v 1.7.3.0---\nAdd some advanced settings\n---v 1.7.3.1---\nAdd SPRD4 one-time kick mode\n---v 1.7.3.2---\nFix some bugs\n---v 1.7.3.3---\nFix dis_avb func\n---v 1.7.3.4---\nFix some bugs, improved UI\n---v 1.7.3.5---\nFix some bugs\n---v 1.7.4.0---\nAdd window dragging detection for Windows dialog-showing issue\n---v 1.7.4.1---\nAdd CVE v2 function, fix some bugs\n---v 1.7.4.2---\nFix some bugs, add crash info displaying\n---v 1.7.4.3---\nFix some bugs\n---v 1.7.5.0---\nFix some bugs, improved console\n---v 1.7.5.1---\nFix some bugs, add partition table modify function, add DHTB Signature read for ums9117\n---v 1.7.5.2---\nAdd slot flash/read manually set, add storage/slot showing\n\n\nUnder GPL v3 License\nGithub: C-Hidery/sfd_tool\nLTV means Long-time-version";
const char* Version = "[1.2.2.0@_250726]";
int bListenLibusb = -1;
int gpt_failed = 1;
int m_bOpened = 0;
int fdl1_loaded = 0;
int fdl2_executed = 0;
int isKickMode = 0;
int isCMethod = 0;
int selected_ab = -1;
int no_fdl_mode = 0;
uint64_t fblk_size = 0;
uint64_t g_spl_size;
bool isUseCptable = false;
const char* o_exception;
int init_stage = -1;
int device_stage = Nothing, device_mode = Nothing;
//sfd_tool protocol
char** str2;
char mode_str[256];
int in_quote;
char* temp;
char str1[(ARGC_MAX - 1) * ARGV_LEN];
spdio_t* io = nullptr;
int ret, conn_wait = 30 * REOPEN_FREQ;
int keep_charge = 1, end_data = 0, blk_size = 0, skip_confirm = 1, highspeed = 0, cve_v2 = 0;
int nand_info[3];
int argcount = 0, stage = -1, nand_id = DEFAULT_NAND_ID;
unsigned exec_addr = 0, baudrate = 0;
int bootmode = -1, at = 0, async = 1;
//Set up environment
#if !USE_LIBUSB
extern DWORD curPort;
DWORD* ports;
//Channel9 init(Windows platform)
#else
//libsub init(Linux/Android-termux)
extern libusb_device* curPort;
libusb_device** ports;
#endif
// Moved initialization into gtk_kmain()
using nlohmann::json;

#ifdef __linux__
void check_root_permission(GtkWidgetHelper helper) {
	if (geteuid() != 0) {
		// not root
		showWarningDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Warning"))), _("You are running this tool without root permission!\nIt may cause device connecting issue\nRecommanded to open this tool with root permission!\n\nsudo -E /path/to/sfd_tool"));
	}
}
#endif

bool isCrashed = false;
void crash_handler(int sig) {
	if (isCrashed) return;
	isCrashed = true;
	if (isHelperInit){
		gui_idle_call_wait_drag([]() {
			showErrorDialog(helper.getWidget("main_window") ? GTK_WINDOW(helper.getWidget("main_window")) : nullptr, _("Program Crash"), _("The program encountered an unhandled exception, which may be caused by device connection issues or a bug in the program.\n\nIt is recommended to check the device connection, ensure the correct options are used, and try running the tool again."));
		},helper.getWidget("main_window") ? GTK_WINDOW(helper.getWidget("main_window")) : nullptr);
	}
#ifdef __linux__
    void* array[20];
    size_t size;
    
    // 获取回溯信息
    size = backtrace(array, 20);
    
    // 打印错误信息
    fprintf(stderr, "Error: signal %d:\n", sig);
    
    // 打印堆栈
    backtrace_symbols_fd(array, size, STDERR_FILENO);
#elif defined(_WIN32)
	fprintf(stderr, "Error: signal %d:\n", sig);
	void* stack[100];
    unsigned short frames;
    SYMBOL_INFO* symbol;
    HANDLE process;
    
    process = GetCurrentProcess();
    SymInitialize(process, NULL, TRUE);
    
    frames = CaptureStackBackTrace(0, 100, stack, NULL);
    symbol = (SYMBOL_INFO*)calloc(sizeof(SYMBOL_INFO) + 256 * sizeof(char), 1);
    symbol->MaxNameLen = 255;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    
    for (int i = 0; i < frames; i++) {
        SymFromAddr(process, (DWORD64)stack[i], 0, symbol);
        printf("%i: %s - 0x%0llX\n", frames - i - 1, symbol->Name, symbol->Address);
    }
    
    free(symbol);

#endif
    // 退出
	std::thread([](){
#ifdef _WIN32
		system("pause");
#else
		sleep(5); // 5 seconds
#endif
		exit(1);
	}).detach();
    
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
void EnableWidgets(GtkWidgetHelper helper) {
	helper.enableWidget("poweroff");
	helper.enableWidget("reboot");
	helper.enableWidget("recovery");
	helper.enableWidget("fastboot");
	helper.enableWidget("list_read");
	helper.enableWidget("list_write");
	helper.enableWidget("list_erase");
	helper.enableWidget("m_write");
	helper.enableWidget("m_read");
	helper.enableWidget("m_erase");
	helper.enableWidget("set_active_a");
	helper.enableWidget("set_active_b");
	helper.enableWidget("start_repart");
	helper.enableWidget("blk_size");
	helper.enableWidget("read_xml");
	helper.enableWidget("dmv_enable");
	helper.enableWidget("dmv_disable");
	helper.enableWidget("backup_all");
	helper.enableWidget("list_cancel");
	helper.enableWidget("m_cancel");
	helper.enableWidget("list_force_write");
	helper.enableWidget("chip_uid");
	helper.enableWidget("pac_time");
	helper.enableWidget("check_nand");
	helper.enableWidget("dis_avb");
	helper.enableWidget("modify_part");
	helper.enableWidget("modify_new_part");
	helper.enableWidget("modify_rm_part");
	helper.enableWidget("modify_ren_part");
	helper.enableWidget("xml_get");
	helper.enableWidget("abpart_auto");
	helper.enableWidget("abpart_a");
	helper.enableWidget("abpart_b");
}
void Enable_Startup() {
	helper.enableWidget("transcode_en");
	helper.enableWidget("transcode_dis");
	helper.enableWidget("end_data_dis");
	helper.enableWidget("end_data_en");
	helper.enableWidget("charge_en");
	helper.enableWidget("charge_dis");
	helper.enableWidget("raw_data_en");
	helper.enableWidget("raw_data_dis");
}
void populatePartitionList(GtkWidgetHelper& helper, const std::vector<partition_t>& partitions) {
	// 获取列表视图
	GtkWidget* part_list = helper.getWidget("part_list");
	if (!part_list || !GTK_IS_TREE_VIEW(part_list)) {
		std::cerr << "part_list not found or not a TreeView" << std::endl;
		return;
	}

	// 获取列表存储模型
	GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(part_list));
	if (!model) {
		std::cerr << "TreeView model not found" << std::endl;
		return;
	}

	// 清空现有数据
	GtkListStore* store = GTK_LIST_STORE(model);
	gtk_list_store_clear(store);

	// 添加分区数据
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
	                   0, display_name.c_str(),   // 显示名称（带序号）
	                   1, size_str.c_str(),       // 格式化的大小
	                   2, "splloader",            // 原始分区名
	                   -1);

	index++;  // 递增序号
	for (const auto& partition : partitions) {
		GtkTreeIter iter;
		gtk_list_store_append(store, &iter);

		// 格式化显示文本
		std::string display_name = std::to_string(index) + ". " + partition.name;

		// 格式化大小显示
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

		// 设置行数据
		gtk_list_store_set(store, &iter,
		                   0, display_name.c_str(),  // 显示名称（带序号）
		                   1, size_str.c_str(),      // 格式化的大小
		                   2, partition.name,        // 原始分区名（隐藏列，可选）
		                   -1);

		index++;
	}

	// 更新显示
	gtk_widget_queue_draw(part_list);
}
void on_button_clicked_select_cve(GtkWidgetHelper helper) {
	GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
	std::string filename = showFileChooser(parent, true);
	if (!filename.empty()) {
		helper.setEntryText(helper.getWidget("cve_addr"), filename);
	}
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
		// 获取第2列（原始分区名，隐藏列）
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
	//dump_partition(io, "splloader", 0, g_spl_size, "splloader.bin", blk_size ? blk_size : DEFAULT_BLK_SIZE);
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
void on_button_clicked_poweroff(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));
		
	}
	encode_msg_nocpy(io, BSL_CMD_POWER_OFF, 0);
	if (!send_and_check(io)) {
		spdio_free(io);
		exit(0);
	}
}
void on_button_clicked_reboot(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));
		
	}
	encode_msg_nocpy(io, BSL_CMD_NORMAL_RESET, 0);
	if (!send_and_check(io)) {
		spdio_free(io);
		exit(0);
	}
}
void on_button_clicked_recovery(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));
		
	}
	char* miscbuf = NEWN char[0x800];
	if (!miscbuf) ERR_EXIT("malloc failed\n");
	memset(miscbuf, 0, 0x800);
	strcpy(miscbuf, "boot-recovery");
	w_mem_to_part_offset(io, "misc", 0, (uint8_t*)miscbuf, 0x800, 0x1000, isCMethod);
	delete[](miscbuf);
	encode_msg_nocpy(io, BSL_CMD_NORMAL_RESET, 0);
	if (!send_and_check(io)) {
		spdio_free(io);
		exit(0);
	}
}
void on_button_clicked_fastboot(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));
		
	}
	char* miscbuf = NEWN char[0x800];
	if (!miscbuf) ERR_EXIT("malloc failed\n");
	memset(miscbuf, 0, 0x800);
	strcpy(miscbuf, "boot-recovery");
	strcpy(miscbuf + 0x40, "recovery\n--fastboot\n");
	w_mem_to_part_offset(io, "misc", 0, (uint8_t*)miscbuf, 0x800, 0x1000, isCMethod);
	delete[](miscbuf);
	encode_msg_nocpy(io, BSL_CMD_NORMAL_RESET, 0);
	if (!send_and_check(io)) {
		spdio_free(io);
		exit(0);
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
				if (selected_ab == 1 && namelen > 2 && 0 == strcmp((*(io->ptable + i)).name + namelen - 2, "_b")) continue;
				else if (selected_ab == 2 && namelen > 2 && 0 == strcmp((*(io->ptable + i)).name + namelen - 2, "_a")) continue;
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
				if (selected_ab == 1 && namelen > 2 && 0 == strcmp((*(io->Cptable + i)).name + namelen - 2, "_b")) continue;
				else if (selected_ab == 2 && namelen > 2 && 0 == strcmp((*(io->Cptable + i)).name + namelen - 2, "_a")) continue;
				snprintf(dfile, sizeof(dfile), "%s.bin", (*(io->Cptable + i)).name);
				dump_partition(io, (*(io->Cptable + i)).name, 0, (*(io->Cptable + i)).size, dfile, blk_size ? blk_size : DEFAULT_BLK_SIZE);
			}
		}
	}).detach();
    helper.setLabelText(helper.getWidget("con"), "Ready");
}
void on_button_clicked_m_select(GtkWidgetHelper helper) {
	GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
	std::string filename = showFileChooser(parent, true);
	if (!filename.empty()) {
		helper.setEntryText(helper.getWidget("m_file_path"), filename);
	}
}
void on_button_clicked_m_write(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));
		
	}
	GtkWidget *parent = helper.getWidget("main_window");
	std::string filename = helper.getEntryText(helper.getWidget("m_file_path"));
	std::string part_name = helper.getEntryText(helper.getWidget("m_part_flash"));
	if (filename.empty()) {
		showErrorDialog(GTK_WINDOW(parent), _(_(_("Error"))), _("No partition image file selected!"));
		return;
	}
	if (part_name.empty()) {
		showErrorDialog(GTK_WINDOW(parent), _(_(_("Error"))), _("No partition name specified!"));
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
			showInfoDialog(GTK_WINDOW(parent), _(_(_("Completed"))), _("Partition write completed!"));
			helper.setLabelText(helper.getWidget("con"), "Ready");
		},GTK_WINDOW(helper.getWidget("main_window")));
	}).detach();
}
void on_button_clicked_m_read(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));
		
	}
	GtkWidget *parent = helper.getWidget("main_window");
	std::string part_name = helper.getEntryText(helper.getWidget("m_part_read"));
	std::string savePath = showSaveFileDialog(GTK_WINDOW(parent), part_name + ".img");
	if (savePath.empty()) {
		showErrorDialog(GTK_WINDOW(parent), _(_(_("Error"))), _("No save path selected!"));
		return;
	}
	if (part_name.empty()) {
		showErrorDialog(GTK_WINDOW(parent), _(_(_("Error"))), _("No partition name specified!"));
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
			showInfoDialog(GTK_WINDOW(parent), _(_(_("Completed"))), _("Partition read completed!"));
			helper.setLabelText(helper.getWidget("con"), "Ready");
		},GTK_WINDOW(helper.getWidget("main_window")));
	}).detach();
}
void on_button_clicked_m_erase(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));
		
	}
	GtkWidget *parent = helper.getWidget("main_window");
	std::string part_name = helper.getEntryText(helper.getWidget("m_part_erase"));
	if (part_name.empty()) {
		showErrorDialog(GTK_WINDOW(parent), _(_(_("Error"))), _("No partition name specified!"));
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
			showInfoDialog(GTK_WINDOW(parent), _(_(_("Completed"))), _("Partition erase completed!"));
			helper.setLabelText(helper.getWidget("con"), "Ready");
		},GTK_WINDOW(helper.getWidget("main_window")));
	}).detach();
}
void on_button_clicked_m_cancel(GtkWidgetHelper helper) {
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
void on_button_clicked_set_active_a(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));
		
	}
	if(!selected_ab) {
		gui_idle_call_wait_drag([helper](){
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device is not using VAB!"));
		},GTK_WINDOW(helper.getWidget("main_window")));
		return;
	}
	set_active(io, "a", isCMethod);
	showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Tips"), _("Current active partition set to Slot A!"));
	gui_idle_call([helper]() mutable {
		helper.setLabelText(helper.getWidget("slot_mode"),"Slot A");
	});
}
void on_button_clicked_set_active_b(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));
		
	}
	if(!selected_ab) {
		gui_idle_call_wait_drag([helper](){
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device is not using VAB!"));
		},GTK_WINDOW(helper.getWidget("main_window")));
		return;
	}
	set_active(io, "b", isCMethod);
	showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Tips"), _("Current active partition set to Slot B!"));
	gui_idle_call([helper]() mutable {
		helper.setLabelText(helper.getWidget("slot_mode"),"Slot B");
	});
}
void on_button_clicked_start_repart(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device unattached, exiting..."));
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
	showInfoDialog(GTK_WINDOW(parent), _(_(_("Completed"))), _("Repartition completed!"));
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
void on_button_clicked_read_xml(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));
		
	}
	GtkWidget* parent = helper.getWidget("main_window");
	std::string savePath = showSaveFileDialog(GTK_WINDOW(parent), "partition_table.xml", { {_("XML files (*.xml)"), "*.xml"} });
	if (savePath.empty()) {
		showErrorDialog(GTK_WINDOW(parent), _(_(_("Error"))), _("No save path selected!"));
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
	showInfoDialog(GTK_WINDOW(parent), _(_(_("Completed"))), _("Partition table export completed!"));

}
void on_button_clicked_dmv_enable(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));
		
	}
	GtkWidget *parent = helper.getWidget("main_window");
	dm_avb_enable(io, blk_size ? blk_size : DEFAULT_BLK_SIZE, isCMethod);
	showInfoDialog(GTK_WINDOW(parent), _(_(_("Completed"))), _("DM-Verity and AVB protection enabled!"));
}
void on_button_clicked_dmv_disable(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));
		
	}
	GtkWidget *parent = helper.getWidget("main_window");
	avb_dm_disable(io, blk_size ? blk_size : DEFAULT_BLK_SIZE, isCMethod);
	showInfoDialog(GTK_WINDOW(parent), _(_(_("Completed"))), _("DM-Verity and AVB protection disabled!"));
}
void on_button_clicked_select_xml(GtkWidgetHelper helper) {
	GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
	std::string filename = showFileChooser(parent, true);
	if (!filename.empty()) {
		helper.setEntryText(helper.getWidget("xml_path"), filename);
	}
}
void on_button_clicked_exp_log(GtkWidgetHelper helper) {
	GtkWidget* parent = helper.getWidget("main_window");
	GtkWidget *txtOutput = helper.getWidget("txtOutput");
	std::string savePath = showSaveFileDialog(GTK_WINDOW(parent), "sfd_tool_log.txt", { {_("Text files (*.txt)"), "*.txt"} });
	if (savePath.empty()) {
		showErrorDialog(GTK_WINDOW(parent), _(_(_("Error"))), _("No save path selected!"));
		return;
	}
	const char* txt = helper.getTextAreaText(txtOutput);
	FILE* fo = oxfopen(savePath.c_str(), "w");
	if (!fo) {
		showErrorDialog(GTK_WINDOW(parent), _(_(_("Error"))), _("Failed to save log file!"));
		return;
	}
	fprintf(fo, "%s", txt);
	fclose(fo);
	showInfoDialog(GTK_WINDOW(parent), _(_(_("Completed"))), _("Log export completed!"));
}
void on_button_clicked_log_clear(GtkWidgetHelper helper) {
	GtkWidget* txtOutput = helper.getWidget("txtOutput");
	helper.setTextAreaText(txtOutput, "");
}
void on_button_clicked_chip_uid(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));
		
	}
	encode_msg_nocpy(io, BSL_CMD_READ_CHIP_UID, 0);
	send_msg(io);
	ret = recv_msg(io);
	if (!ret) ERR_EXIT("timeout reached\n");
	if ((ret = recv_type(io)) != BSL_REP_READ_CHIP_UID) {
		const char* name = get_bsl_enum_name(ret);
		DEG_LOG(E, "excepted response (%s : 0x%04x)\n", name, ret);
		return;
	}
	DEG_LOG(I, "Response: chip_uid:");
	print_string(stderr, io->raw_buf + 4, READ16_BE(io->raw_buf + 2));
	char* str = NEWN char[1024];
	if (!str) ERR_EXIT("malloc failed");
	print_to_string(str, 1024, io->raw_buf + 4, READ16_BE(io->raw_buf + 2), 0);
	showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Info"), str);
	delete[] str;
}

void on_button_clicked_pac_time(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));
		
	}
	uint32_t n, offset = 0x81400, len = 8;
	int ret;
	uint32_t *data = (uint32_t *)io->temp_buf;
	unsigned long long time, unix1;

	select_partition(io, "miscdata", offset + len, 0, BSL_CMD_READ_START);
	if (send_and_check(io)) {
		encode_msg_nocpy(io, BSL_CMD_READ_END, 0);
		send_and_check(io);
		return;
	}

	WRITE32_LE(data, len);
	WRITE32_LE(data + 1, offset);
	encode_msg_nocpy(io, BSL_CMD_READ_MIDST, 8);
	send_msg(io);
	ret = recv_msg(io);
	if (!ret) ERR_EXIT("timeout reached\n");
	if ((ret = recv_type(io)) != BSL_REP_READ_FLASH) {
		const char* name = get_bsl_enum_name(ret);
		DEG_LOG(E, "excepted response (%s : 0x%04x)", name, ret);
		encode_msg_nocpy(io, BSL_CMD_READ_END, 0);
		send_and_check(io);
		return;
	}
	n = READ16_BE(io->raw_buf + 2);
	if (n != len) ERR_EXIT("excepted length\n");

	time = (uint32_t)READ32_LE(io->raw_buf + 4);
	time |= (uint64_t)READ32_LE(io->raw_buf + 8) << 32;

	unix1 = time ? time / 10000000 - 11644473600 : 0;
	// $ date -d @unixtime
	DEG_LOG(I, "pactime = 0x%llx (unix = %llu)", time, unix1);
	int need = snprintf(nullptr, 0, "pactime = 0x%llx (unix = %llu)", time, unix1);
	char* text = NEWN char[need + 1];
	if (!text) ERR_EXIT("malloc failed");
	snprintf(text, need + 1, "pactime = 0x%llx (unix = %llu)", time, unix1);
	encode_msg_nocpy(io, BSL_CMD_READ_END, 0);
	send_and_check(io);
	showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Info"), text);
	delete[] text;
}
void on_button_clicked_check_nand(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));
		
	}
	encode_msg_nocpy(io, BSL_CMD_READ_FLASH_INFO, 0);
	send_msg(io);
	ret = recv_msg(io);
	if (ret) {
		ret = recv_type(io);
		const char* name = get_bsl_enum_name(ret);
		if (ret != BSL_REP_READ_FLASH_INFO) DEG_LOG(E, "excepted response (%s : 0x%04x)\n", name, ret);
		else Da_Info.dwStorageType = 0x101;
		// need more samples to cover BSL_REP_READ_MCP_TYPE packet to nand_id/nand_info
		// for nand_id 0x15, packet is 00 9b 00 0c 00 00 00 00 00 02 00 00 00 00 08 00
	}
	if (Da_Info.dwStorageType == 0x101) {
		DEG_LOG(I, "Device storage is nand");
		showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Info"), "Storage is nand.");
	} else {
		DEG_LOG(I, "Device storage is not nand");
		showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Info"), "Storage is not nand.");
	}
}
void on_button_clicked_dis_avb(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device unattached, exiting..."));
		    exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));
		
	}
	helper.setLabelText(helper.getWidget("con"), "Patching trustos");
	TosPatcher patcher;
	bool i_is = showConfirmDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Warning"))), _("This operation may break your device, and not all devices support this, if your device is broken, flash backup in backup_tos, continue?"));
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
					showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Disabled AVB failed, go to console window to see why"));
				},GTK_WINDOW(helper.getWidget("main_window")));
			}
		}).detach();
	}
    helper.setLabelText(helper.getWidget("con"), "Ready");
}
void on_button_clicked_raw_data_en(GtkWidgetHelper helper) {
	int rawdatay = atoi(helper.getEntryText(helper.getWidget("raw_data_v")));
	if (rawdatay) {
		Da_Info.bSupportRawData = rawdatay;
	}
	if (Da_Info.bSupportRawData) showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Info"), _("Successfully enabled raw data mode"));
	else showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Info"), _("Failed to enable raw data mode, please set value!"));
}
void on_button_clicked_raw_data_dis(GtkWidgetHelper helper) {
	Da_Info.bSupportRawData = 0;
	showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Info"), _("Successfully disabled raw data mode"));
}
void on_button_clicked_transcode_en(GtkWidgetHelper helper) {
	unsigned a, f;
	a = 1;
	f = (io->flags & ~FLAGS_TRANSCODE);
	io->flags = f | (a ? FLAGS_TRANSCODE : 0);
	showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Info"), _("Enabled transcode successfully"));
}
void on_button_clicked_transcode_dis(GtkWidgetHelper helper) {
	unsigned a = 0;
	encode_msg_nocpy(io, BSL_CMD_DISABLE_TRANSCODE, 0);
	if (!send_and_check(io)) io->flags &= ~FLAGS_TRANSCODE;
	showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Info"), _("Disabled transcode successfully"));
}
void on_button_clicked_charge_en(GtkWidgetHelper helper) {
	keep_charge = 1;
	showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Info"), _("Set successfully"));
}
void on_button_clicked_charge_dis(GtkWidgetHelper helper) {
	keep_charge = 0;
	showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Info"), _("Set successfully"));
}
void on_button_clicked_end_data_en(GtkWidgetHelper helper) {
	end_data = 1;
	showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Info"), _("Set successfully"));
}
void on_button_clicked_end_data_dis(GtkWidgetHelper helper) {
	end_data = 0;
	showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Info"), _("Set successfully"));
}
void on_button_clicked_abpart_auto(GtkWidgetHelper helper) {
	selected_ab = 0;
}
void on_button_clicked_abpart_a(GtkWidgetHelper helper) {
	selected_ab = 1;
}
void on_button_clicked_abpart_b(GtkWidgetHelper helper) {
	selected_ab = 2;
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

void on_button_clicked_connect(GtkWidgetHelper helper, int argc, char** argv) {
	GtkWidget* waitBox = helper.getWidget("wait_con");
	GtkWidget* sprd4Switch = helper.getWidget("sprd4");
	GtkWidget* cveSwitch = helper.getWidget("exec_addr");
	GtkWidget* cveAddr = helper.getWidget("cve_addr");
	GtkWidget* cveAddrC = helper.getWidget("cve_addr_c");
	GtkWidget* sprd4OneMode = helper.getWidget("sprd4_one_mode");
	helper.setLabelText(helper.getWidget("con"), "Waiting for connection...");
	if (argc > 1 && !strcmp(argv[1], "--reconnect")) {
		stage = 99;
		gui_idle_call_wait_drag([helper]() {
			showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Tips"), _("You have entered Reconnect Mode, which only supports compatibility-method partition list retrieval, and [storage mode/slot mode] can not be gotten!"));
		},GTK_WINDOW(helper.getWidget("main_window")));

	}
#ifdef __linux__
	check_root_permission(helper);
#endif
	helper.disableWidget("connect_1");
	double wait_time = helper.getSpinValue(waitBox);
	bool isSprd4 = helper.getSwitchState(sprd4Switch);
	bool isOneMode = helper.getSwitchState(sprd4OneMode);
	bool isCve = helper.getSwitchState(cveSwitch);
	const char* cve_path = helper.getEntryText(cveAddr);
	const char* cve_addr = helper.getEntryText(cveAddrC);
	DEG_LOG(I, "Begin to boot...(%fs)", wait_time);
	conn_wait = static_cast<int>(wait_time * REOPEN_FREQ);
	if (isSprd4) {
		if (isOneMode) {
			DEG_LOG(I, "Using SPRD4 one-step mode to kick device.");
			isKickMode = 1;
			bootmode = strtol("2", nullptr, 0);
			at = 0;
		} else {
			DEG_LOG(I, "Using SPRD4 mode to kick device.");
			isKickMode = 1;
			at = 1;
		}
	}
	if (isCve) {
		DEG_LOG(I, "Using CVE binary: %s at address: %s", cve_path, cve_addr);
	}

#if !USE_LIBUSB
	bListenLibusb = 0;
	if (at || bootmode >= 0) {
		io->hThread = CreateThread(nullptr, 0, ThrdFunc, nullptr, 0, &io->iThread);
		if (io->hThread == nullptr) return;
		ChangeMode(io, conn_wait / REOPEN_FREQ * 1000, bootmode, at);
		conn_wait = 30 * REOPEN_FREQ;
		stage = -1;
	}
#else
	if (!libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) {
		DBG_LOG("hotplug unsupported on this platform\n");
		bListenLibusb = 0;
		bootmode = -1;
		at = 0;
	}
	if (at || bootmode >= 0) {
		startUsbEventHandle();
		ChangeMode(io, conn_wait / REOPEN_FREQ * 1000, bootmode, at);
		conn_wait = 30 * REOPEN_FREQ;
		stage = -1;
	}
	if (bListenLibusb < 0) startUsbEventHandle();
#endif
#if _WIN32
	if (!bListenLibusb) {
		if (io->hThread == nullptr) io->hThread = CreateThread(nullptr, 0, ThrdFunc, nullptr, 0, &io->iThread);
		if (io->hThread == nullptr) return;
	}
#if !USE_LIBUSB
	if (!m_bOpened && async) {
		if (FALSE == CreateRecvThread(io)) {
			io->m_dwRecvThreadID = 0;
			DEG_LOG(E, "Create Receive Thread Fail.");
		}
	}
#endif
#endif
	if (!m_bOpened) {
		DBG_LOG("<waiting for connection,mode:dl,%ds>\n", conn_wait / REOPEN_FREQ);

		for (int i = 0; ; i++) {
#if USE_LIBUSB
			if (bListenLibusb) {
				if (curPort) {
					if (libusb_open(curPort, &io->dev_handle) >= 0) call_Initialize_libusb(io);
					else ERR_EXIT("Failed to connect\n");
					break;
				}
			}
			if (!(i % 4)) {
				if ((ports = FindPort(0x4d00))) {
					for (libusb_device** port = ports; *port != nullptr; port++) {
						if (libusb_open(*port, &io->dev_handle) >= 0) {
							call_Initialize_libusb(io);
							curPort = *port;
							break;
						}
					}
					libusb_free_device_list(ports, 1);
					ports = nullptr;
					if (m_bOpened) break;
				}
			}
			if (i >= conn_wait)
				ERR_EXIT("libusb_open_device failed\n");
#else
			if (io->verbose) DBG_LOG("Cost: %.1f, Found: %d\n", (float)i / REOPEN_FREQ, curPort);
			if (curPort) {
				if (!call_ConnectChannel(io->handle, curPort, WM_RCV_CHANNEL_DATA, io->m_dwRecvThreadID)) ERR_EXIT("Connection failed\n");
				break;
			}
			if (!(i % 4)) {
				if ((ports = FindPort("SPRD U2S Diag"))) {
					for (DWORD* port = ports; *port != 0; port++) {
						if (call_ConnectChannel(io->handle, *port, WM_RCV_CHANNEL_DATA, io->m_dwRecvThreadID)) {
							curPort = *port;
							break;
						}
					}
					delete[](ports);
					ports = nullptr;
					if (m_bOpened) break;
				}
			}
			if (i >= conn_wait) {
				ERR_EXIT("%s: Failed to find port.\n", o_exception);
			}
#endif
			usleep(1000000 / REOPEN_FREQ);
		}
	}
	io->flags |= FLAGS_TRANSCODE;
	if (stage != -1) {
		io->flags &= ~FLAGS_CRC16;
		encode_msg_nocpy(io, BSL_CMD_CONNECT, 0);
	} else encode_msg(io, BSL_CMD_CHECK_BAUD, nullptr, 1);
	//handshake
	for (int i = 0; ; ++i) {
		//check if device is connected correctly.
		if (io->recv_buf[2] == BSL_REP_VER) {
			ret = BSL_REP_VER;
			memcpy(io->raw_buf + 4, io->recv_buf + 5, 5);
			io->raw_buf[2] = 0;
			io->raw_buf[3] = 5;
			io->recv_buf[2] = 0;
		} else if (io->recv_buf[2] == BSL_REP_VERIFY_ERROR ||
		           io->recv_buf[2] == BSL_REP_UNSUPPORTED_COMMAND) {
			if (!fdl1_loaded) {
				ret = io->recv_buf[2];
				io->recv_buf[2] = 0;
			} else ERR_EXIT("Failed to connect to device: %s, please reboot your phone by pressing POWER and VOLUME_UP for 7-10 seconds.\n", o_exception);
		} else {
			//device correct, handshake operation
			send_msg(io);
			recv_msg(io);
			ret = recv_type(io);
		}
		//device can only recv BSL_REP_ACK or BSL_REP_VER or BSL_REP_VERIFY_ERROR
		init_stage = 1;
		if (ret == BSL_REP_ACK || ret == BSL_REP_VER || ret == BSL_REP_VERIFY_ERROR) {
			//check stage
			if (ret == BSL_REP_VER) {
				if (fdl1_loaded == 1) {
					device_stage = FDL1;
					DEG_LOG(OP, "FDL1 connected.");
					if (!memcmp(io->raw_buf + 4, "SPRD4", 5) && no_fdl_mode) fdl2_executed = -1;
					break;
				} else {
					device_stage = BROM;
					DEG_LOG(OP, "Check baud BROM");
					if (!memcmp(io->raw_buf + 4, "SPRD4", 5) && no_fdl_mode) {
						fdl1_loaded = -1;
						fdl2_executed = -1;
					}
				}
				DBG_LOG("[INFO] Device mode version: ");
				print_string(stdout, io->raw_buf + 4, READ16_BE(io->raw_buf + 2));
				print_to_string(mode_str, sizeof(mode_str), io->raw_buf + 4, READ16_BE(io->raw_buf + 2), 0);

				encode_msg_nocpy(io, BSL_CMD_CONNECT, 0);
				if (send_and_check(io)) ERR_EXIT("FDL connect failed");
			} else if (ret == BSL_REP_VERIFY_ERROR) {
				encode_msg_nocpy(io, BSL_CMD_CONNECT, 0);
				if (fdl1_loaded != 1) {
					if (send_and_check(io)) ERR_EXIT("FDL connect failed");;
				} else {
					i = -1;
					continue;
				}
			}
			if (fdl1_loaded == 1) {
				DEG_LOG(OP, "FDL1 connected.");
				device_stage = FDL1;
				if (keep_charge) {
					encode_msg_nocpy(io, BSL_CMD_KEEP_CHARGE, 0);
					if (!send_and_check(io)) DEG_LOG(OP, "Keep charge FDL1.");
				}
				break;
			} else {
				DEG_LOG(OP, "BROM connected.");
				device_stage = BROM;
				break;
			}
		}
		//FDL2 response:UNSUPPORTED
		else if (ret == BSL_REP_UNSUPPORTED_COMMAND) {
			encode_msg_nocpy(io, BSL_CMD_DISABLE_TRANSCODE, 0);
			if (!send_and_check(io)) {
				io->flags &= ~FLAGS_TRANSCODE;
				DEG_LOG(OP, "Try to disable transcode 0x7D.");
				helper.disableWidget("fdl_exec");
				EnableWidgets(helper);
				fdl2_executed = 1;
				device_stage = FDL2;
				int o = io->verbose;
				io->verbose = -1;
				g_spl_size = check_partition(io, "splloader", 1);
				io->verbose = o;
				if (isUseCptable) {
					io->Cptable = partition_list_d(io);
					isCMethod = 1;
				}
				if (!isUseCptable && !io->part_count) {
					DEG_LOG(W, "No partition table found on current device");
					confirm_partition_c(helper);
				}
				if (Da_Info.dwStorageType == 0x101) DEG_LOG(I, "Device storage is nand.");
				if (nand_id == DEFAULT_NAND_ID) {
					nand_info[0] = (uint8_t)pow(2, nand_id & 3); //page size
					nand_info[1] = 32 / (uint8_t)pow(2, (nand_id >> 2) & 3); //spare area size
					nand_info[2] = 64 * (uint8_t)pow(2, (nand_id >> 4) & 3); //block size
				}
				break;
			}
		}

		//fail
		else if (i == 4) {
			init_stage = 1;
			if (stage != -1) {
				ERR_EXIT("Failed to connect: %s, please reboot your phone by pressing POWER and VOLUME_UP for 7-10 seconds.\n", o_exception);
			} else {
				encode_msg_nocpy(io, BSL_CMD_CONNECT, 0);
				stage++;
				i = -1;
			}
		}

	}
	size_t sub_len = strlen("SPRD3");
	size_t str_len = strlen(mode_str);
	int found = 0;
	if (str_len >= sub_len) {
		for (size_t i = 0; i <= str_len - sub_len; i++) {
			if (strncmp(mode_str + i, "SPRD3", sub_len) == 0) {
				found = 1;
				break;
			}
		}
	}
	DEG_LOG(I, "SPRD3 Current : %d", found);
	if (!found && isKickMode) device_mode = SPRD4;
	else device_mode = SPRD3;
	
	if (fdl2_executed > 0) {
		if (device_mode == SPRD3) {
			DEG_LOG(I, "Device stage: FDL2/SPRD3");
		} else DEG_LOG(I, "Device stage: FDL2/SPRD4(AutoD)");
	} else if (fdl1_loaded > 0) {
		if (device_mode == SPRD3) {
			DEG_LOG(I, "Device stage: FDL1/SPRD3");
		} else DEG_LOG(I, "Device stage: FDL1/SPRD4(AutoD)");
	} else if (device_stage == BROM) {
		if (device_mode == SPRD3) {
			DEG_LOG(I, "Device stage: BROM/SPRD3");
		} else DEG_LOG(I, "Device stage: BROM/SPRD4(AutoD)");
	} else {
		if (device_mode == SPRD3) DEG_LOG(I, "Device stage: Unknown/SPRD3");
		else DEG_LOG(I, "Device stage: Unknown/SPRD4(AutoD)");
	}
	gui_idle_call_wait_drag([helper]() mutable {
		showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Successfully connected"), _("Device already connected! Some advanced settings opened!"));
		if (!fdl2_executed) {
			helper.enableWidget("fdl_exec");
			showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Tips"), _("Please execute FDL file to continue!"));
			if (device_mode == SPRD4 && isKickMode) {
				showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Tips"), _("Since your device is in SPRD4 mode, you can choose to skip FDL setting and directly execute FDL, but not all devices support that, please proceed with caution!"));
			}
		}
		else if (device_stage == FDL2) helper.setLabelText(helper.getWidget("con"), "Ready");
		Enable_Startup();
		helper.setLabelText(helper.getWidget("con"), "Connected");
		if (device_stage == BROM) helper.setLabelText(helper.getWidget("mode"), "BROM");
		else if (device_stage == FDL1) helper.setLabelText(helper.getWidget("mode"), "FDL1");
		else if (device_stage == FDL2) helper.setLabelText(helper.getWidget("mode"), "FDL2");
	},GTK_WINDOW(helper.getWidget("main_window")));

}

// select fdl
void on_button_clicked_select_fdl(GtkWidgetHelper helper) {
	GtkWindow* parentWindow = GTK_WINDOW(helper.getWidget("main_window"));
	std::string fdlPath = showFileChooser(parentWindow, true);
	if (!fdlPath.empty()) {
		helper.setEntryText(helper.getWidget("fdl_file_path"), fdlPath);
		DEG_LOG(I, "Selected FDL file: %s", fdlPath.c_str());
	} else {
		DEG_LOG(W, "No FDL file selected.");
	}
}
//fdl exec
void on_button_clicked_fdl_exec(GtkWidgetHelper helper, char* execfile) {
	GtkWidget *fdlEntry = helper.getWidget("fdl_file_path");
	GtkWidget *addrEntry = helper.getWidget("fdl_addr");
	const char* fdl_path = helper.getEntryText(fdlEntry);
	const char* fdl_addr_str = helper.getEntryText(addrEntry);
	uint32_t fdl_addr = strtoul(fdl_addr_str, nullptr, 0);
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device unattached, exiting..."));
		},GTK_WINDOW(helper.getWidget("main_window")));
		exit(1);
	}
	if (fdl1_loaded > 0) {
		DEG_LOG(I, "Executing FDL file: %s at address: 0x%X", fdl_path, fdl_addr);
		std::string dtxt = helper.getLabelText(helper.getWidget("con"));
		helper.setLabelText(helper.getWidget("con"), dtxt + " -> FDL Executing");
		//Send fdl2
		if (device_mode == SPRD3) {
			FILE *fi = oxfopen(fdl_path, "r");
			if (fi == nullptr) {
				DEG_LOG(W, "File does not exist.");
				return;
			} else fclose(fi);
			if (!isKickMode) send_file(io, fdl_path, fdl_addr, end_data, blk_size ? blk_size : 528, 0, 0);
			else send_file(io, fdl_path, fdl_addr, 0, 528, 0, 0);
		} else {
			if (device_mode == SPRD4 && isKickMode) {
				gui_idle_call_with_callback(
					[helper]() -> bool {
						return showConfirmDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Confirm"), _("Device can be booted without FDL in SPRD4 mode, continue?"));
					},
					[helper, fdl_path, fdl_addr](bool result) {
						if (result) {
							DEG_LOG(I, "Skipping FDL send in SPRD4 mode.");
							encode_msg_nocpy(io, BSL_CMD_EXEC_DATA, 0);
							if (send_and_check(io)) ERR_EXIT("FDL exec failed");;
							return;
						} else {
							FILE *fi = oxfopen(fdl_path, "r");
							if (fi == nullptr) {
								DEG_LOG(W, "File does not exist.");
								return;
							} else fclose(fi);
							send_file(io, fdl_path, fdl_addr, 0, 528, 0, 0);
						}
					},
					GTK_WINDOW(helper.getWidget("main_window"))
				);
				
			}
		}

		memset(&Da_Info, 0, sizeof(Da_Info));
		encode_msg_nocpy(io, BSL_CMD_EXEC_DATA, 0);
		send_msg(io);
		// Feature phones respond immediately,
		// but it may take a second for a smartphone to respond.
		ret = recv_msg_timeout(io, 15000);
		if (!ret) {
			ERR_EXIT("timeout reached\n");
		}
		ret = recv_type(io);
		// Is it always bullshit?
		if (ret == BSL_REP_INCOMPATIBLE_PARTITION)
			get_Da_Info(io);
		else if (ret != BSL_REP_ACK) {
			//ThrowExit();
			const char* name = get_bsl_enum_name(ret);
			ERR_EXIT("%s: excepted response (%s : 0x%04x)\n", name, o_exception, ret);
		}
		DEG_LOG(OP, "Execute FDL2");
		//remove 0d detection for nand device
		//This is not supported on certain devices.
		/*
		encode_msg_nocpy(io, BSL_CMD_READ_FLASH_INFO, 0);
		send_msg(io);
		ret = recv_msg(io);
		if (ret) {
		    ret = recv_type(io);
		    if (ret != BSL_REP_READ_FLASH_INFO) DEG_LOG(E,"excepted response (0x%04x)\n", ret);
		    else Da_Info.dwStorageType = 0x101;
		    // need more samples to cover BSL_REP_READ_MCP_TYPE packet to nand_id/nand_info
		    // for nand_id 0x15, packet is 00 9b 00 0c 00 00 00 00 00 02 00 00 00 00 08 00
		}
		*/
		if (Da_Info.bDisableHDLC) {
			encode_msg_nocpy(io, BSL_CMD_DISABLE_TRANSCODE, 0);
			if (!send_and_check(io)) {
				io->flags &= ~FLAGS_TRANSCODE;
				DEG_LOG(OP, "Try to disable transcode 0x7D.");
			}
		}
		int o = io->verbose;
		io->verbose = -1;
		g_spl_size = check_partition(io, "splloader", 1);
		io->verbose = o;
		if (Da_Info.bSupportRawData) {
			blk_size = 0xf800;
			io->ptable = partition_list(io, fn_partlist, &io->part_count);
			if (fdl2_executed) {
				Da_Info.bSupportRawData = 0;
				DEG_LOG(OP, "Raw data mode disabled for SPRD4.");
			} else {
				encode_msg_nocpy(io, BSL_CMD_ENABLE_RAW_DATA, 0);
				if (!send_and_check(io)) DEG_LOG(OP, "Raw data mode enabled.");
			}
		} else if (highspeed || Da_Info.dwStorageType == 0x103) { // ufs
			blk_size = 0xf800;
			io->ptable = partition_list(io, fn_partlist, &io->part_count);
		} else if (Da_Info.dwStorageType == 0x102) { // emmc
			io->ptable = partition_list(io, fn_partlist, &io->part_count);
		} else if (Da_Info.dwStorageType == 0x101) {
			DEG_LOG(I, "Device storage is nand.");
			gui_idle_call([helper]() mutable {
				helper.setLabelText(helper.getWidget("storage_mode"),"Nand");
			});
		}
		if (gpt_failed != 1) {
			if (selected_ab == 2) {
				DEG_LOG(I, "Device is using slot b\n");
				gui_idle_call([helper]() mutable {
					helper.setLabelText(helper.getWidget("slot_mode"),"Slot B");
				});
			}
			else if (selected_ab == 1) {
				DEG_LOG(I, "Device is using slot a\n");
				gui_idle_call([helper]() mutable {
					helper.setLabelText(helper.getWidget("slot_mode"),"Slot A");
				});
			}
			else {
				DEG_LOG(I, "Device is not using VAB\n");
				gui_idle_call([helper]() mutable {
					helper.setLabelText(helper.getWidget("slot_mode"),"Not VAB");
				});
				if (Da_Info.bSupportRawData) {
					DEG_LOG(I, "Raw data mode is supported (level is %u) ,but DISABLED for stability, you can set it manually.", (unsigned)Da_Info.bSupportRawData);
					Da_Info.bSupportRawData = 0;
				}
			}
		}
		if (io->part_count) {
			std::vector<partition_t> partitions;
			partitions.reserve(io->part_count);
			for (int i = 0; i < io->part_count; i++) {
				partitions.push_back(io->ptable[i]);
			}
			gui_idle_call_wait_drag([helper, partitions]() mutable {
				populatePartitionList(helper, partitions);
			},GTK_WINDOW(helper.getWidget("main_window")));

		} else if (isUseCptable) {
			io->Cptable = partition_list_d(io);
			isCMethod = 1;
		}
		if (!io->part_count) {
			DEG_LOG(W, "No partition table found on current device");
			confirm_partition_c(helper);
		}
		if (nand_id == DEFAULT_NAND_ID) {
			nand_info[0] = (uint8_t)pow(2, nand_id & 3); //page size
			nand_info[1] = 32 / (uint8_t)pow(2, (nand_id >> 2) & 3); //spare area size
			nand_info[2] = 64 * (uint8_t)pow(2, (nand_id >> 4) & 3); //block size
		}
		fdl2_executed = 1;
		gui_idle_call_wait_drag([helper]() mutable {
			showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("FDL2 Executed"), _("FDL2 executed successfully!"));
			EnableWidgets(helper);
			helper.disableWidget("fdl_exec");
			helper.setLabelText(helper.getWidget("mode"), "FDL2");
			helper.setLabelText(helper.getWidget("con"), "Ready");
		},GTK_WINDOW(helper.getWidget("main_window")));

	} else {
		DEG_LOG(I, "Executing FDL file: %s at address: 0x%X", fdl_path, fdl_addr);
		std::string dtxt = helper.getLabelText(helper.getWidget("con"));
		helper.setLabelText(helper.getWidget("con"), dtxt + " -> FDL Executing");
		std::thread([helper, fdl_path, fdl_addr, execfile]() mutable {
			FILE* fi = oxfopen(fdl_path, "r");
			GtkWidget* cveSwitch = helper.getWidget("exec_addr");
			GtkWidget* cveAddr = helper.getWidget("cve_addr");
			GtkWidget* cveAddrC = helper.getWidget("cve_addr_c");
			bool isCve = helper.getSwitchState(cveSwitch);
			const char* cve_path = helper.getEntryText(cveAddr);
			const char* cve_addr = helper.getEntryText(cveAddrC);

			if (device_mode == SPRD3) {
				if (fi == nullptr) {
					DEG_LOG(W, "File does not exist.\n");
					return;
				} else fclose(fi);
				send_file(io, fdl_path, fdl_addr, end_data, 528, 0, 0);
				if (cve_addr && strlen(cve_addr) > 0 && isCve) {
					bool isCVEv2 = helper.getSwitchState(helper.getWidget("cve_v2"));
					if(!isCVEv2){
						DEG_LOG(I, "Using CVE binary: %s at address: %s", cve_path, cve_addr);
						uint32_t cve_addr_val = strtoul(cve_addr, nullptr, 0);
						send_file(io, cve_path, cve_addr_val, 0, 528, 0, 0);
					}
					else{
						DEG_LOG(I, "Using CVEv2 binary: %s at address: %s", cve_path, cve_addr);
						uint32_t cve_addr_val = strtoul(cve_addr, nullptr, 0);
						size_t execsize = send_file(io, cve_path, cve_addr_val, 0, 528, 0, 0);
						int n, gapsize = exec_addr - cve_addr_val - execsize;
						for (int i = 0; i < gapsize; i += n) {
							n = gapsize - i;
							if (n > 528) n = 528;
							encode_msg_nocpy(io, BSL_CMD_MIDST_DATA, n);
							if (send_and_check(io)) ERR_EXIT("CVE v2 failed");;
						}
						FILE* fi = oxfopen(execfile, "rb");
						if (fi) {
							fseek(fi, 0, SEEK_END);
							n = ftell(fi);
							fseek(fi, 0, SEEK_SET);
							execsize = fread(io->temp_buf, 1, n, fi);
							fclose(fi);
						}
						encode_msg_nocpy(io, BSL_CMD_MIDST_DATA, execsize);
						if (send_and_check(io)) ERR_EXIT("CVE v2 failed");;
					}
					delete[](execfile);
				} else {
					encode_msg_nocpy(io, BSL_CMD_EXEC_DATA, 0);
					if (send_and_check(io)) ERR_EXIT("FDL exec failed");;
				}
			} else {
				if (device_mode == SPRD4 && isKickMode) {
					gui_idle_call_with_callback(
						[helper]() -> bool {
							return showConfirmDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Confirm"), _("Device can be booted without FDL in SPRD4 mode, continue?"));
						},
						[execfile,fdl_path,fdl_addr,isCve,cve_addr,cve_path,fi, helper](bool result) {
							if (result) {
								DEG_LOG(I, "Skipping FDL send in SPRD4 mode.");
								fclose(fi);
								encode_msg_nocpy(io, BSL_CMD_EXEC_DATA, 0);
								if (send_and_check(io)) ERR_EXIT("FDL exec failed");
								delete[](execfile);
								return;
							} else {
								if (fi == nullptr) {
									DEG_LOG(W, "File does not exist.\n");
									return;
								} else fclose(fi);
								send_file(io, fdl_path, fdl_addr, end_data, 528, 0, 0);
								if (cve_addr && strlen(cve_addr) > 0 && isCve) {
									bool isCVEv2 = helper.getSwitchState(helper.getWidget("cve_v2"));
									if(!isCVEv2){
										DEG_LOG(I, "Using CVE binary: %s at address: %s", cve_path, cve_addr);
										uint32_t cve_addr_val = strtoul(cve_addr, nullptr, 0);
										send_file(io, cve_path, cve_addr_val, 0, 528, 0, 0);
									}
									else{
										DEG_LOG(I, "Using CVEv2 binary: %s at address: %s", cve_path, cve_addr);
										uint32_t cve_addr_val = strtoul(cve_addr, nullptr, 0);
										size_t execsize = send_file(io, cve_path, cve_addr_val, 0, 528, 0, 0);
										int n, gapsize = exec_addr - cve_addr_val - execsize;
										for (int i = 0; i < gapsize; i += n) {
											n = gapsize - i;
											if (n > 528) n = 528;
											encode_msg_nocpy(io, BSL_CMD_MIDST_DATA, n);
											if (send_and_check(io)) ERR_EXIT("CVE V2 failed");
										}
										FILE* fi = oxfopen(execfile, "rb");
										if (fi) {
											fseek(fi, 0, SEEK_END);
											n = ftell(fi);
											fseek(fi, 0, SEEK_SET);
											execsize = fread(io->temp_buf, 1, n, fi);
											fclose(fi);
										}
										encode_msg_nocpy(io, BSL_CMD_MIDST_DATA, execsize);
										if (send_and_check(io)) ERR_EXIT("CVE V2 failed");;
									}
									delete[](execfile);
								} else {
									encode_msg_nocpy(io, BSL_CMD_EXEC_DATA, 0);
									if (send_and_check(io)) ERR_EXIT("FDL exec failed");;
								}
							}	
						},
						GTK_WINDOW(helper.getWidget("main_window"))
					);
					
				}
			}
			DEG_LOG(OP, "Execute FDL1");

			// Tiger 310(0x5500) and Tiger 616(0x65000800) need to change baudrate after FDL1
			if (fdl_addr == 0x5500 || fdl_addr == 0x65000800) {
				highspeed = 1;
				if (!baudrate) baudrate = 921600;
				gui_idle_call_wait_drag([helper]() {
					showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("High Speed Mode Enabled"), _("Do not set block size manually in high speed mode!"));
				},GTK_WINDOW(helper.getWidget("main_window")));
			}

			/* FDL1 (chk = sum) */
			io->flags &= ~FLAGS_CRC16;

			encode_msg(io, BSL_CMD_CHECK_BAUD, nullptr, 1);
			for (int i = 0; ; i++) {
				send_msg(io);
				recv_msg(io);
				if (recv_type(io) == BSL_REP_VER) break;
				DEG_LOG(W, "Failed to check baud, retry...");
				if (i == 4) {
					o_exception = "Failed to check baud FDL1";
					ERR_EXIT("Can not execute FDL: %s,please reboot your phone by pressing POWER and VOL_UP for 7-10 seconds.\n", o_exception);
				}
				usleep(500000);
			}
			DEG_LOG(I, "Check baud FDL1 done.");

			DEG_LOG(I, "Device REP_Version: ");
			print_string(stderr, io->raw_buf + 4, READ16_BE(io->raw_buf + 2));
			encode_msg_nocpy(io, BSL_CMD_CONNECT, 0);
			if (send_and_check(io)) ERR_EXIT("FDL connect failed");;
			DEG_LOG(I, "FDL1 connected.");
#if !USE_LIBUSB
			if (baudrate) {
				uint8_t* data = io->temp_buf;
				WRITE32_BE(data, baudrate);
				encode_msg_nocpy(io, BSL_CMD_CHANGE_BAUD, 4);
				if (!send_and_check(io)) {
					DEG_LOG(OP, "Change baud FDL1 to %d", baudrate);
					call_SetProperty(io->handle, 0, 100, (LPCVOID)&baudrate);
				}
			}
#endif
			if (keep_charge) {
				encode_msg_nocpy(io, BSL_CMD_KEEP_CHARGE, 0);
				if (!send_and_check(io)) DEG_LOG(OP, "Keep charge FDL1.");
			}
			fdl1_loaded = 1;
			gui_idle_call_wait_drag([helper]() mutable {
				showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("FDL1 Executed"), _("FDL1 executed successfully!"));
				helper.setLabelText(helper.getWidget("mode"), "FDL1");
				helper.setLabelText(helper.getWidget("con"), "Connected");
			},GTK_WINDOW(helper.getWidget("main_window")));

		}).detach();

	}

}
//disable widget when init
void DisableWidgets(GtkWidgetHelper helper) {
	helper.disableWidget("fdl_exec");
	helper.disableWidget("poweroff");
	helper.disableWidget("reboot");
	helper.disableWidget("recovery");
	helper.disableWidget("fastboot");
	helper.disableWidget("list_read");
	helper.disableWidget("list_write");
	helper.disableWidget("list_erase");
	helper.disableWidget("m_write");
	helper.disableWidget("m_read");
	helper.disableWidget("m_erase");
	helper.disableWidget("set_active_a");
	helper.disableWidget("set_active_b");
	helper.disableWidget("start_repart");
	helper.disableWidget("blk_size");
	helper.disableWidget("read_xml");
	helper.disableWidget("dmv_enable");
	helper.disableWidget("dmv_disable");
	helper.disableWidget("backup_all");
	helper.disableWidget("list_cancel");
	helper.disableWidget("m_cancel");
	helper.disableWidget("list_force_write");
	helper.disableWidget("check_nand");
	helper.disableWidget("pac_time");
	helper.disableWidget("chip_uid");
	helper.disableWidget("dis_avb");
	helper.disableWidget("transcode_en");
	helper.disableWidget("transcode_dis");
	helper.disableWidget("end_data_dis");
	helper.disableWidget("end_data_en");
	helper.disableWidget("charge_en");
	helper.disableWidget("charge_dis");
	helper.disableWidget("raw_data_en");
	helper.disableWidget("raw_data_dis");
	helper.disableWidget("modify_part");
	helper.disableWidget("modify_new_part");
	helper.disableWidget("modify_rm_part");
	helper.disableWidget("modify_ren_part");
	helper.disableWidget("xml_get");
	helper.disableWidget("abpart_auto");
	helper.disableWidget("abpart_a");
	helper.disableWidget("abpart_b");
}

int gtk_kmain(int argc, char** argv) {
	DEG_LOG(I, "Starting GUI mode...");
	gtk_init(&argc, &argv);

	// Initialization previously at file scope
	char* execfile = NEWN char[ARGV_LEN];
	if (!execfile) {
		ERR_EXIT("malloc failed\n");
	}
	io = spdio_init(0);
#if USE_LIBUSB
	ret = libusb_init(nullptr);
	if (ret < 0) ERR_EXIT("libusb_init failed: %s\n", libusb_error_name(ret));
#else
	io->handle = createClass();
	call_Initialize(io->handle);
#endif
	sprintf(fn_partlist, "partition_%lld.xml", (long long)time(nullptr));

	// Window Setup
	GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), "SFD Tool GUI By Ryan Crepa");
	gtk_window_set_default_size(GTK_WINDOW(window), 1174, 765);

	// 设置关闭信号
	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	// 创建主网格布局
	GtkWidget* mainGrid = gtk_grid_new();

	// 创建 GtkWidgetHelper
	helper = GtkWidgetHelper(window);
	isHelperInit = true;
	helper.setParent(window, LayoutType::GRID);
	helper.addWidget("main_window", window);
	initDragDetection(GTK_WINDOW(window));
	// 创建Notebook（标签页控件）
	GtkWidget* notebook = helper.createNotebook("main_notebook", 0, 0, 1174, 672);
	{
		// ========== Connect Page ==========

		GtkWidget* connectPage = helper.createGrid("connect_page", 5, 5);
		helper.addNotebookPage(notebook, connectPage, _("Connect"));

		// 创建连接页的根垂直盒子（完全取代依靠行列Grid布局的方式）
		GtkWidget* mainConnectBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
		// 容器上下左右留出一定边距，使得整个界面不会紧贴边缘
		gtk_widget_set_margin_start(mainConnectBox, 20);
		gtk_widget_set_margin_end(mainConnectBox, 20);
		gtk_widget_set_margin_top(mainConnectBox, 20);
		gtk_widget_set_margin_bottom(mainConnectBox, 20);

		// 1. Header 部分 (居中)
		GtkWidget* headerBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
		gtk_widget_set_halign(headerBox, GTK_ALIGN_CENTER);

		GtkWidget* welcomeLabel = helper.createLabel(_("Welcome to SFD Tool GUI!"), "welcome", 0, 0, 467, 28);
		GtkWidget* tiLabel = helper.createLabel(_("Please connect your device with BROM mode"), "ti", 0, 0, 400, 28);
		
		PangoAttrList* attr_list = pango_attr_list_new();
		PangoAttribute* attr = pango_attr_size_new(20 * PANGO_SCALE);
		pango_attr_list_insert(attr_list, attr);
		gtk_label_set_attributes(GTK_LABEL(welcomeLabel), attr_list);
		gtk_label_set_attributes(GTK_LABEL(tiLabel), attr_list);

		// Color for tiLabel (blueish as in demo), using standard pango markup is better or base style, we skip color hardcoding here unless needed
		
		GtkWidget* instruction = helper.createLabel(_("Press and hold the volume up or down keys and the power key to connect"),
		                          "instruction", 0, 0, 600, 20);

		gtk_box_pack_start(GTK_BOX(headerBox), welcomeLabel, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(headerBox), tiLabel, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(headerBox), instruction, FALSE, FALSE, 0);
		
		gtk_box_pack_start(GTK_BOX(mainConnectBox), headerBox, FALSE, FALSE, 10);

		// 2. FDL Settings section
		GtkWidget* fdlCenterBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		gtk_widget_set_halign(fdlCenterBox, GTK_ALIGN_CENTER); // 居中 fdlFrame

		GtkWidget* fdlFrame = gtk_frame_new(_("FDL Send Settings"));
		gtk_widget_set_size_request(fdlFrame, 600, -1); // 限制宽度约为原来的一半（整个窗口大约1174，左右各减去边距，所以600左右差不多）

		GtkWidget* fdlGrid = gtk_grid_new();
		gtk_grid_set_row_spacing(GTK_GRID(fdlGrid), 10);
		gtk_grid_set_column_spacing(GTK_GRID(fdlGrid), 10);
		gtk_widget_set_margin_start(fdlGrid, 15);
		gtk_widget_set_margin_end(fdlGrid, 15);
		gtk_widget_set_margin_top(fdlGrid, 15);
		gtk_widget_set_margin_bottom(fdlGrid, 15);

		// FDL File Path
		GtkWidget* fdlLabel = helper.createLabel(_("FDL File Path :"), "fdl_label", 0, 0, 100, 20);
		gtk_widget_set_halign(fdlLabel, GTK_ALIGN_START);
		GtkWidget* fdlFilePath = helper.createEntry("fdl_file_path", "", false, 0, 0, 300, 32);
		gtk_widget_set_hexpand(fdlFilePath, TRUE);
		GtkWidget* selectFdlBtn = helper.createButton("...", "select_fdl", nullptr, 0, 0, 40, 32);

		gtk_grid_attach(GTK_GRID(fdlGrid), fdlLabel, 0, 0, 1, 1);
		gtk_grid_attach(GTK_GRID(fdlGrid), fdlFilePath, 1, 0, 1, 1);
		gtk_grid_attach(GTK_GRID(fdlGrid), selectFdlBtn, 2, 0, 1, 1);

		// FDL Address
		GtkWidget* fdlAddrLabel = helper.createLabel(_("FDL Send Address :"), "fdl_addr_label", 0, 0, 120, 20);
		gtk_widget_set_halign(fdlAddrLabel, GTK_ALIGN_START);
		GtkWidget* fdlAddr = helper.createEntry("fdl_addr", "", false, 0, 0, 300, 32);
		gtk_widget_set_hexpand(fdlAddr, TRUE);

		gtk_grid_attach(GTK_GRID(fdlGrid), fdlAddrLabel, 0, 1, 1, 1);
		gtk_grid_attach(GTK_GRID(fdlGrid), fdlAddr, 1, 1, 2, 1); // 占两列

		// Execute button
		GtkWidget* fdlExecBtn = helper.createButton(_("Execute"), "fdl_exec", nullptr, 0, 0, 150, 32);
		gtk_widget_set_hexpand(fdlExecBtn, TRUE);
		gtk_grid_attach(GTK_GRID(fdlGrid), fdlExecBtn, 0, 2, 3, 1);

		gtk_container_add(GTK_CONTAINER(fdlFrame), fdlGrid);
		gtk_box_pack_start(GTK_BOX(fdlCenterBox), fdlFrame, FALSE, FALSE, 0); // 放入居中的盒子
		gtk_box_pack_start(GTK_BOX(mainConnectBox), fdlCenterBox, FALSE, FALSE, 0); // 将居中盒子放入主VBox

		// 3. Advanced Options Containers
		GtkWidget* advContainer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
		gtk_box_set_homogeneous(GTK_BOX(advContainer), TRUE); // 两边等宽

		// Left frame for CVE options
		GtkWidget* cveFrame = gtk_frame_new(_("CVE Bypass Options"));
		GtkWidget* cveGrid = gtk_grid_new();
		gtk_grid_set_row_spacing(GTK_GRID(cveGrid), 15);
		gtk_grid_set_column_spacing(GTK_GRID(cveGrid), 10);
		gtk_widget_set_margin_start(cveGrid, 15);
		gtk_widget_set_margin_end(cveGrid, 15);
		gtk_widget_set_margin_top(cveGrid, 15);
		gtk_widget_set_margin_bottom(cveGrid, 15);
		gtk_container_add(GTK_CONTAINER(cveFrame), cveGrid);

		// 行 0: CVE Switch
		GtkWidget* cveSwitchLabel = helper.createLabel(_("Try to use CVE to skip FDL verification"), "exec_addr_label", 0, 0, 200, 20);
		gtk_widget_set_halign(cveSwitchLabel, GTK_ALIGN_START);
		gtk_label_set_xalign(GTK_LABEL(cveSwitchLabel), 0.0);
		GtkWidget* cveSwitch = gtk_switch_new();
		gtk_widget_set_name(cveSwitch, "exec_addr");
		gtk_widget_set_halign(cveSwitch, GTK_ALIGN_END);
		gtk_widget_set_hexpand(cveSwitch, TRUE);
		helper.addWidget("exec_addr", cveSwitch);

		gtk_grid_attach(GTK_GRID(cveGrid), cveSwitchLabel, 0, 0, 1, 1);
		gtk_grid_attach(GTK_GRID(cveGrid), cveSwitch, 1, 0, 1, 1);

		// 行 1: CVE V2 Switch
		GtkWidget* cveV2Label = helper.createLabel(_("Enable CVE v2"),"cve_v2_label", 0, 0, 100, 20);
		gtk_widget_set_halign(cveV2Label, GTK_ALIGN_START);
		gtk_label_set_xalign(GTK_LABEL(cveV2Label), 0.0);
		GtkWidget* cveV2Switch = gtk_switch_new();
		gtk_widget_set_name(cveV2Switch, "cve_v2");
		gtk_widget_set_halign(cveV2Switch, GTK_ALIGN_END);
		gtk_widget_set_hexpand(cveV2Switch, TRUE);
		helper.addWidget("cve_v2", cveV2Switch);

		gtk_grid_attach(GTK_GRID(cveGrid), cveV2Label, 0, 1, 1, 1);
		gtk_grid_attach(GTK_GRID(cveGrid), cveV2Switch, 1, 1, 1, 1);

		// 行 2: CVE File
		GtkWidget* cveLabel = helper.createLabel(_("CVE Binary File Address"), "cve_label", 0, 0, 150, 20);
		gtk_widget_set_halign(cveLabel, GTK_ALIGN_START);
		gtk_label_set_xalign(GTK_LABEL(cveLabel), 0.0);
		
		GtkWidget* cveInputBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
		gtk_widget_set_halign(cveInputBox, GTK_ALIGN_END);
		gtk_widget_set_hexpand(cveInputBox, TRUE);
		GtkWidget* cveAddr = helper.createEntry("cve_addr", "", false, 0, 0, 150, 32);
		GtkWidget* selectCveBtn = helper.createButton("...", "select_cve", nullptr, 0, 0, 40, 32);
		gtk_box_pack_start(GTK_BOX(cveInputBox), cveAddr, TRUE, TRUE, 0);
		gtk_box_pack_start(GTK_BOX(cveInputBox), selectCveBtn, FALSE, FALSE, 0);

		gtk_grid_attach(GTK_GRID(cveGrid), cveLabel, 0, 2, 1, 1);
		gtk_grid_attach(GTK_GRID(cveGrid), cveInputBox, 1, 2, 1, 1);

		// Right frame for SPRD4 options
		GtkWidget* sprdFrame = gtk_frame_new(_("SPRD4 Options"));
		GtkWidget* sprdGrid = gtk_grid_new();
		gtk_grid_set_row_spacing(GTK_GRID(sprdGrid), 15);
		gtk_grid_set_column_spacing(GTK_GRID(sprdGrid), 10);
		gtk_widget_set_margin_start(sprdGrid, 15);
		gtk_widget_set_margin_end(sprdGrid, 15);
		gtk_widget_set_margin_top(sprdGrid, 15);
		gtk_widget_set_margin_bottom(sprdGrid, 15);
		gtk_container_add(GTK_CONTAINER(sprdFrame), sprdGrid);

		// 行 0: SPRD4 Switch
		GtkWidget* sprd4Label = helper.createLabel(_("Kick device to SPRD4"), "sprd4_label", 0, 0, 150, 20);
		gtk_widget_set_halign(sprd4Label, GTK_ALIGN_START);
		gtk_label_set_xalign(GTK_LABEL(sprd4Label), 0.0);
		GtkWidget* sprd4Switch = gtk_switch_new();
		gtk_widget_set_name(sprd4Switch, "sprd4");
		gtk_widget_set_halign(sprd4Switch, GTK_ALIGN_END);
		gtk_widget_set_hexpand(sprd4Switch, TRUE);
		helper.addWidget("sprd4", sprd4Switch);

		gtk_grid_attach(GTK_GRID(sprdGrid), sprd4Label, 0, 0, 1, 1);
		gtk_grid_attach(GTK_GRID(sprdGrid), sprd4Switch, 1, 0, 1, 1);

		// 行 1: Kick One-time Mode
		GtkWidget* sprd4OneLabel = helper.createLabel(_("Kick One-time Mode"), "sprd4_one_label", 0, 0, 150, 20);
		gtk_widget_set_halign(sprd4OneLabel, GTK_ALIGN_START);
		gtk_label_set_xalign(GTK_LABEL(sprd4OneLabel), 0.0);
		GtkWidget* sprd4OneMode = gtk_switch_new();
		gtk_widget_set_name(sprd4OneMode, "sprd4_one_mode");
		gtk_widget_set_halign(sprd4OneMode, GTK_ALIGN_END);
		gtk_widget_set_hexpand(sprd4OneMode, TRUE);
		helper.addWidget("sprd4_one_mode", sprd4OneMode);

		gtk_grid_attach(GTK_GRID(sprdGrid), sprd4OneLabel, 0, 1, 1, 1);
		gtk_grid_attach(GTK_GRID(sprdGrid), sprd4OneMode, 1, 1, 1, 1);

		// 行 2: Address
		GtkWidget* cveAddrLabel2 = helper.createLabel(_("CVE Addr"), "cve_addr_label2", 0, 0, 100, 20);
		gtk_widget_set_halign(cveAddrLabel2, GTK_ALIGN_START);
		gtk_label_set_xalign(GTK_LABEL(cveAddrLabel2), 0.0);
		GtkWidget* cveAddrC = helper.createEntry("cve_addr_c", "", false, 0, 0, 200, 32);
		gtk_widget_set_halign(cveAddrC, GTK_ALIGN_END);
		gtk_widget_set_hexpand(cveAddrC, TRUE);

		gtk_grid_attach(GTK_GRID(sprdGrid), cveAddrLabel2, 0, 2, 1, 1);
		gtk_grid_attach(GTK_GRID(sprdGrid), cveAddrC, 1, 2, 1, 1);

		gtk_box_pack_start(GTK_BOX(advContainer), cveFrame, TRUE, TRUE, 0);
		gtk_box_pack_start(GTK_BOX(advContainer), sprdFrame, TRUE, TRUE, 0);

		gtk_box_pack_start(GTK_BOX(mainConnectBox), advContainer, FALSE, FALSE, 0);

		// 4. Bottom Action Area
		GtkWidget* actionBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
		gtk_widget_set_halign(actionBox, GTK_ALIGN_CENTER);

		// Connect Button (Wide)
		GtkWidget* connectBtn = helper.createButton(_("CONNECT"), "connect_1", nullptr, 0, 0, 300, 48);
		
		// Wait connection time
		GtkWidget* waitBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
		gtk_widget_set_halign(waitBox, GTK_ALIGN_CENTER);
		GtkWidget* waitLabel = helper.createLabel(_("Wait connection time (s):"), "wait_label", 0, 0, 150, 20);
		
		// 自定义 SpinBox 结合体 [ 30 | - | + ]
		GtkWidget* customSpinBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		GtkStyleContext* styleCtx = gtk_widget_get_style_context(customSpinBox);
		gtk_style_context_add_class(styleCtx, "linked"); // GTK 的联动样式，将子组件连成一个整体无缝框

		// 核心的隐藏了默认箭头的 SpinButton (供逻辑读取)
		GtkWidget* waitCon = helper.createSpinButton(1, 65535, 1, "wait_con", 30, 0, 0, 60, 32);
		gtk_widget_set_name(waitCon, "wait_con_no_arrow");
		
		// 独立渲染的减号和加号按钮
		GtkWidget* btnMinus = gtk_button_new_with_label("-");
		GtkWidget* btnPlus = gtk_button_new_with_label("+");
		gtk_widget_set_size_request(btnMinus, 32, 32);
		gtk_widget_set_size_request(btnPlus, 32, 32);

		// 绑定加减操作
		g_signal_connect(btnMinus, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer data) {
			gtk_spin_button_spin(GTK_SPIN_BUTTON(data), GTK_SPIN_STEP_BACKWARD, 1);
		}), waitCon);
		g_signal_connect(btnPlus, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer data) {
			gtk_spin_button_spin(GTK_SPIN_BUTTON(data), GTK_SPIN_STEP_FORWARD, 1);
		}), waitCon);

		gtk_box_pack_start(GTK_BOX(customSpinBox), waitCon, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(customSpinBox), btnMinus, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(customSpinBox), btnPlus, FALSE, FALSE, 0);

		gtk_box_pack_start(GTK_BOX(waitBox), waitLabel, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(waitBox), customSpinBox, FALSE, FALSE, 0);

		gtk_box_pack_start(GTK_BOX(actionBox), connectBtn, FALSE, FALSE, 5);
		gtk_box_pack_start(GTK_BOX(actionBox), waitBox, FALSE, FALSE, 5);

		// 添加一些弹簧占位，把底部区域推向垂直居中
		gtk_box_pack_start(GTK_BOX(mainConnectBox), actionBox, TRUE, FALSE, 15);

		// Status labels (kept from original logic but usually shouldn't impact grid layout since they are handled elsewhere or placed as children)
		GtkWidget* statusLabel = helper.createLabel(_("Status : "), "status_label", 0, 0, 70, 24);
		GtkWidget* conStatus = helper.createLabel(_("Not connected"), "con", 0, 0, 150, 23);
		GtkWidget* modeLabel = helper.createLabel(_("   Mode : "), "mode_label", 0, 0, 50, 19);
		GtkWidget* modeStatus = helper.createLabel(_("BROM Not connected!!!"), "mode", 0, 0, 200, 19);

		// 把整合完毕的 mainConnectBox 添加到 connectPage 网格的起始位置，占据所有空间
		gtk_grid_attach(GTK_GRID(connectPage), mainConnectBox, 0, 0, 5, 5);



		// ========== Partition Operation Page ==========

		GtkWidget* partPage = helper.createGrid("part_page", 5, 5);
		helper.addNotebookPage(notebook, partPage, _("Partition Operation"));

		GtkWidget* part_instruction_label = helper.createLabel(_("Please check a partition"), "part_instruction", 0, 0, 300, 20);

		// ListView for partitions
		GtkWidget* scrolledWindow = gtk_scrolled_window_new(NULL, NULL);
		gtk_widget_set_size_request(scrolledWindow, 1000, 300);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledWindow),
		                               GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

		GtkListStore* store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
		GtkWidget* treeView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
		gtk_widget_set_name(treeView, "part_list");
		helper.addWidget("part_list", treeView);
		GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
		gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeView), -1,
		        "Partition Name", renderer,
		        "text", 0, NULL);
		gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeView), -1,
		        "Size", renderer,
		        "text", 1, NULL);
		gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeView), -1,
		        "Type", renderer,
		        "text", 2, NULL);

		gtk_container_add(GTK_CONTAINER(scrolledWindow), treeView);

		// Operation buttons
		GtkWidget* opLabel = helper.createLabel(_("Operation"), "op_label", 0, 0, 150, 20);
		GtkWidget* writeBtn = helper.createButton(_("WRITE"), "list_write", nullptr, 0, 0, 117, 32);
		GtkWidget* writeFBtn = helper.createButton(_("FORCE WRITE"), "list_force_write", nullptr, 0, 0, 162, 32);
		GtkWidget* readBtn = helper.createButton(_("EXTRACT"), "list_read", nullptr, 0, 0, 162, 32);
		GtkWidget* eraseBtn = helper.createButton(_("ERASE"), "list_erase", nullptr, 0, 0, 170, 32);
		GtkWidget* backupAllBtn = helper.createButton(_("Backup All"), "backup_all", nullptr, 0, 0, 180, 32);
		GtkWidget* cancelBtn = helper.createButton(_("Cancel"), "list_cancel", nullptr, 0, 0, 117, 32);
		GtkWidget* xmlGetBtn = helper.createButton(_("Get partition table through scanning an Xml file"), "xml_get", nullptr, 0, 0, 250, 32);
		
		// 修改分区表
		GtkWidget* ModifyLabel = helper.createLabel(_("Modify Partition Table"), "modify_label", 0, 0, 200, 20);
		GtkWidget* SedLabel = helper.createLabel(_("[Change size] Please check a partition you want to change"), "fff_label", 0, 0, 100, 20);
		GtkWidget* SeLabel = helper.createLabel(_("Second-change partition"), "second_part_label", 0, 0, 200, 20);
		GtkWidget* secondPart = helper.createEntry("modify_second_part", "", false, 0, 0, 200, 32);
		GtkWidget* newSizeLabel = helper.createLabel(_("New size in MB"), "new_size_label", 0, 0, 100, 20);
		GtkWidget* newSizeEntry = helper.createEntry("modify_new_size", "", false, 0, 0, 150, 32);
		GtkWidget* modifyBtn = helper.createButton(_("Modify"), "modify_part", nullptr, 0, 0, 117, 32);
		GtkWidget* AddLabel = helper.createLabel(_("[Add partition] Enter partition name:"),"ff_label",0,0,100,20);
		GtkWidget* Add2Label = helper.createLabel(_("Part name:"),"f_label",0,0,50,20);
		GtkWidget* newPartEntry = helper.createEntry("new_part","",false,0,0,200,32);
		GtkWidget* new2SizeLabel = helper.createLabel(_("Size(MB):"),"f3_label",0,0,100,20);
		GtkWidget* newPartSize = helper.createEntry("modify_add_size","",false,0,0,150,32);
		GtkWidget* afterPartLabel = helper.createLabel(_("Part after this new part:"),"",0,0,100,20);
		GtkWidget* afterPart = helper.createEntry("before_new_part","",false,0,0,100,32);
		GtkWidget* addNewPartBtn = helper.createButton(_("Modify"),"modify_new_part",nullptr,0,0,117,32);
		GtkWidget* RemvLabel = helper.createLabel(_("[Remove partition] Please check a partition you want to remove"),"ffff_label",0,0,250,20);
		GtkWidget* RemvPartBtn = helper.createButton(_("Modify"),"modify_rm_part",nullptr,0,0,117,32);
		GtkWidget* RenmLabel = helper.createLabel(_("[Rename partition] Please check a partition you want to rename"),"f2_label",0,0,250,20);
		GtkWidget* RenmPartLabel = helper.createLabel(_("New name"),"f4_label",0,0,100,20);
		GtkWidget* RenmPartEntry = helper.createEntry("modify_rename_part","",false,0,0,200,32);
		GtkWidget* RenmPartBtn = helper.createButton(_("Modify"),"modify_ren_part",nullptr,0,0,117,32);

		// 设置按钮初始状态
		gtk_widget_set_sensitive(writeBtn, FALSE);
		gtk_widget_set_sensitive(readBtn, FALSE);
		gtk_widget_set_sensitive(eraseBtn, FALSE);
		gtk_widget_set_sensitive(backupAllBtn, TRUE);
		gtk_widget_set_sensitive(cancelBtn, TRUE);  // Cancel按钮始终可用

		// Add to grid - 主网格
		helper.addToGrid(partPage, instruction, 0, 0, 5, 1);
		helper.addToGrid(partPage, scrolledWindow, 0, 1, 5, 8);
		helper.addToGrid(partPage, opLabel, 0, 9, 5, 1);

		// 第一行按钮 - 主要操作按钮
		GtkWidget* mainButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
		gtk_box_pack_start(GTK_BOX(mainButtonBox), writeBtn, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(mainButtonBox), writeFBtn, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(mainButtonBox), readBtn, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(mainButtonBox), eraseBtn, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(mainButtonBox), backupAllBtn, FALSE, FALSE, 0);
		helper.addToGrid(partPage, mainButtonBox, 0, 10, 5, 1);

		// 第二行按钮 - Cancel按钮单独一行，在刷写按钮正下方
		GtkWidget* cancelButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
		gtk_box_pack_start(GTK_BOX(cancelButtonBox), cancelBtn, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(cancelButtonBox), xmlGetBtn, FALSE, FALSE, 0);
		// 添加占位空间使Cancel按钮对齐到刷写按钮下方
		GtkWidget* placeholder1 = gtk_label_new("");
		GtkWidget* placeholder2 = gtk_label_new("");
		gtk_box_pack_start(GTK_BOX(cancelButtonBox), placeholder1, FALSE, FALSE, 117);
		gtk_box_pack_start(GTK_BOX(cancelButtonBox), placeholder2, FALSE, FALSE, 0);
		helper.addToGrid(partPage, cancelButtonBox, 0, 11, 5, 1);

		//修改分区表所有部分放在cancel下方
		helper.addToGrid(partPage, ModifyLabel, 0, 12, 5, 1);
		helper.addToGrid(partPage, SedLabel, 0, 13, 5, 1);
		GtkWidget* modifySizeBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
		gtk_box_pack_start(GTK_BOX(modifySizeBox), SeLabel, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(modifySizeBox), secondPart, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(modifySizeBox), newSizeLabel, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(modifySizeBox), newSizeEntry, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(modifySizeBox), modifyBtn, FALSE, FALSE, 0);
		helper.addToGrid(partPage, modifySizeBox, 0, 14, 5, 1);
		GtkWidget* modifyAddBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
		helper.addToGrid(partPage, AddLabel, 0,15,5,1);
		gtk_box_pack_start(GTK_BOX(modifyAddBox), Add2Label, FALSE,FALSE,0);
		gtk_box_pack_start(GTK_BOX(modifyAddBox), newPartEntry, FALSE,FALSE,0);
		gtk_box_pack_start(GTK_BOX(modifyAddBox), new2SizeLabel, FALSE,FALSE,0);
		gtk_box_pack_start(GTK_BOX(modifyAddBox), newPartSize, FALSE,FALSE,0);
		gtk_box_pack_start(GTK_BOX(modifyAddBox), afterPartLabel,FALSE,FALSE,0);
		gtk_box_pack_start(GTK_BOX(modifyAddBox), afterPart,FALSE,FALSE,0);
		gtk_box_pack_start(GTK_BOX(modifyAddBox), addNewPartBtn, FALSE,FALSE,0);
		helper.addToGrid(partPage, modifyAddBox, 0, 16, 5, 1);
		GtkWidget* modifyRemvBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
		helper.addToGrid(partPage, RemvLabel, 0,17,5,1);
		gtk_box_pack_start(GTK_BOX(modifyRemvBox), RemvPartBtn, FALSE,FALSE,0);
		helper.addToGrid(partPage, modifyRemvBox, 0,18,5,1);
		GtkWidget* modifyRenmBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,10);
		helper.addToGrid(partPage, RenmLabel, 0,19,5,1);
		gtk_box_pack_start(GTK_BOX(modifyRenmBox), RenmPartLabel, FALSE,FALSE,0);
		gtk_box_pack_start(GTK_BOX(modifyRenmBox), RenmPartEntry, FALSE,FALSE,0);
		gtk_box_pack_start(GTK_BOX(modifyRenmBox), RenmPartBtn, FALSE,FALSE,0);
		helper.addToGrid(partPage, modifyRenmBox, 0,20,5,1);

		// ========== Manually Operate Page ==========

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

		// Cancel button - 在Erase按钮下方两行处
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

		// Add Cancel button - 在Erase按钮下方两行处
		row += 2;  // 空两行
		helper.addToGrid(manualPage, mCancelBtn, 0, row++, 3, 1);





		// ========== Advanced Operation Page ==========

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
		row = 0;
		helper.addToGrid(advOpPage, abLabel, 0, row++, 3, 1);

		GtkWidget* abButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
		gtk_box_pack_start(GTK_BOX(abButtonBox), setActiveA, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(abButtonBox), setActiveB, FALSE, FALSE, 0);
		helper.addToGrid(advOpPage, abButtonBox, 0, row++, 3, 1);

		row += 2; // Add some spacing

		helper.addToGrid(advOpPage, repartLabel, 0, row++, 3, 1);
		helper.addToGrid(advOpPage, xmlLabel, 0, row++, 3, 1);

		GtkWidget* xmlBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
		gtk_box_pack_start(GTK_BOX(xmlBox), xmlPath, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(xmlBox), selectXmlBtn, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(xmlBox), startRepartBtn, FALSE, FALSE, 0);
		helper.addToGrid(advOpPage, xmlBox, 0, row++, 3, 1);

		helper.addToGrid(advOpPage, readXmlBtn, 0, row++, 3, 1);

		row += 2; // Add some spacing

		helper.addToGrid(advOpPage, dmvLabel, 0, row++, 3, 1);

		GtkWidget* dmvButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
		gtk_box_pack_start(GTK_BOX(dmvButtonBox), dmvDisable, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(dmvButtonBox), dmvEnable, FALSE, FALSE, 0);
		helper.addToGrid(advOpPage, dmvButtonBox, 0, row++, 3, 1);

		row += 2; // Add some spacing

		helper.addToGrid(advOpPage, disavbLabel, 0, row++, 3, 1);
		helper.addToGrid(advOpPage, dis_avb, 0, row++, 3, 1);


		// ========== Advanced Settings Page ==========

		GtkWidget* advSetPage = helper.createGrid("adv_set_page", 5, 5);
		helper.addNotebookPage(notebook, advSetPage, _("Advanced Settings"));

		// 数据块大小设置部分
		GtkWidget* blkLabel = helper.createLabel(_("Data block size"), "blk_label", 0, 0, 200, 20);

		GtkWidget* blkSlider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 10000, 60000, 10000);
		gtk_range_set_value(GTK_RANGE(blkSlider), 10000);
		gtk_scale_set_draw_value(GTK_SCALE(blkSlider), TRUE);
		gtk_scale_set_value_pos(GTK_SCALE(blkSlider), GTK_POS_RIGHT);
		gtk_widget_set_name(blkSlider, "blk_size");
		helper.addWidget("blk_size", blkSlider);
		gtk_widget_set_size_request(blkSlider, 1036, 30);

		GtkWidget* sizeCon = helper.createLabel("10000", "size_con", 0, 0, 60, 20);

		// Rawdata模式设置部分
		GtkWidget* rawDataEn = helper.createButton(_("Enable Rawdata mode"), "raw_data_en",
		                       nullptr, 0, 0, 250, 32);
		GtkWidget* rawDataDis = helper.createButton(_("Disable Rawdata mode"), "raw_data_dis",
		                        nullptr, 0, 0, 250, 32);
		GtkWidget* rlabel = helper.createLabel(_("Value:"), "rawLabel", 0, 0, 80, 30);
		GtkWidget* rawDataMk = helper.createEntry("raw_data_v", "", false, 0, 0, 50, 30);

		// 转码设置部分
		GtkWidget* transcode_en = helper.createButton(_("Enable transcode - FDL1"), "transcode_en",
		                          nullptr, 0, 0, 220, 32);
		GtkWidget* transcode_dis = helper.createButton(_("Disable transcode --- FDL2"), "transcode_dis",
		                           nullptr, 0, 0, 220, 32);

		// 充电模式设置部分
		GtkWidget* charge_en = helper.createButton(_("Enable Charging mode --- BROM"), "charge_en",
		                       nullptr, 0, 0, 240, 32);
		GtkWidget* charge_dis = helper.createButton(_("Disable Charging mode --- BROM"), "charge_dis",
		                        nullptr, 0, 0, 240, 32);

		// 发送结束数据设置部分
		GtkWidget* end_data_en = helper.createButton(_("Enable sending end data"),
		                         "end_data_en", nullptr, 0, 0, 280, 32);
		GtkWidget* end_data_dis = helper.createButton(_("Disable sending end data"),
		                          "end_data_dis", nullptr, 0, 0, 280, 32);

		// 操作超时时间设置部分
		GtkWidget* timeout_label = helper.createLabel(_("Operation timeout"),
		                           "timeout_label", 0, 0, 200, 20);
		GtkWidget* timeout_op = helper.createSpinButton(3000, 300000, 1, "timeout", 3000, 0, 0, 120, 32);
		
		// A/B分区设置
		GtkWidget* abpart_auto = helper.createButton(_("Not VAB --- FDL2"),"abpart_auto",nullptr,0,0,120,32);
		GtkWidget* abpart_a = helper.createButton(_("A Parts --- FDL2"),"abpart_a",nullptr,0,0,130,32);
		GtkWidget* abpart_b = helper.createButton(_("B Parts --- FDL2"),"abpart_b",nullptr,0,0,130,32);
		
		// 创建主垂直容器
		GtkWidget* emainBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
		gtk_widget_set_margin_start(emainBox, 20);
		gtk_widget_set_margin_end(emainBox, 20);
		gtk_widget_set_margin_top(emainBox, 20);
		gtk_widget_set_margin_bottom(emainBox, 20);

		// 1. 数据块大小部分
		GtkWidget* blockSizeBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
		GtkWidget* blockLabelBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
		gtk_box_pack_start(GTK_BOX(blockLabelBox), blkLabel, FALSE, FALSE, 0);
		gtk_box_pack_end(GTK_BOX(blockLabelBox), sizeCon, FALSE, FALSE, 0);

		GtkWidget* sliderBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
		gtk_box_pack_start(GTK_BOX(sliderBox), blkSlider, TRUE, TRUE, 0);

		gtk_box_pack_start(GTK_BOX(blockSizeBox), blockLabelBox, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(blockSizeBox), sliderBox, FALSE, FALSE, 0);

		// 2. Rawdata模式部分
		GtkWidget* rawDataBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
		GtkWidget* rawDataLabel = gtk_label_new(_("Rawdata Mode --- Value support: {1, 2}"));
		gtk_label_set_xalign(GTK_LABEL(rawDataLabel), 0.0);

		GtkWidget* rawDataButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
		gtk_box_pack_start(GTK_BOX(rawDataButtonBox), rawDataEn, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(rawDataButtonBox), rawDataDis, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(rawDataButtonBox), rlabel, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(rawDataButtonBox), rawDataMk, FALSE, FALSE, 0);

		gtk_box_pack_start(GTK_BOX(rawDataBox), rawDataLabel, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(rawDataBox), rawDataButtonBox, FALSE, FALSE, 0);

		// 3. 转码设置部分
		GtkWidget* transcodeBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
		GtkWidget* transcodeLabel = gtk_label_new(_("Transcode --- FDL1/2"));
		gtk_label_set_xalign(GTK_LABEL(transcodeLabel), 0.0);

		GtkWidget* transcodeButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
		gtk_box_pack_start(GTK_BOX(transcodeButtonBox), transcode_en, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(transcodeButtonBox), transcode_dis, FALSE, FALSE, 0);

		gtk_box_pack_start(GTK_BOX(transcodeBox), transcodeLabel, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(transcodeBox), transcodeButtonBox, FALSE, FALSE, 0);

		// 4. 充电模式部分
		GtkWidget* chargeBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
		GtkWidget* chargeLabel = gtk_label_new(_("Charging Mode --- BROM"));
		gtk_label_set_xalign(GTK_LABEL(chargeLabel), 0.0);

		GtkWidget* chargeButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
		gtk_box_pack_start(GTK_BOX(chargeButtonBox), charge_en, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(chargeButtonBox), charge_dis, FALSE, FALSE, 0);

		gtk_box_pack_start(GTK_BOX(chargeBox), chargeLabel, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(chargeBox), chargeButtonBox, FALSE, FALSE, 0);

		// 5. 发送结束数据部分
		GtkWidget* endDataBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
		GtkWidget* endDataLabel = gtk_label_new(_("Send End Data"));
		gtk_label_set_xalign(GTK_LABEL(endDataLabel), 0.0);

		GtkWidget* endDataButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
		gtk_box_pack_start(GTK_BOX(endDataButtonBox), end_data_en, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(endDataButtonBox), end_data_dis, FALSE, FALSE, 0);

		gtk_box_pack_start(GTK_BOX(endDataBox), endDataLabel, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(endDataBox), endDataButtonBox, FALSE, FALSE, 0);

		// 6. 操作超时时间部分
		GtkWidget* timeoutBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
		GtkWidget* timeoutTopBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
		gtk_box_pack_start(GTK_BOX(timeoutTopBox), timeout_label, FALSE, FALSE, 0);
		gtk_box_pack_end(GTK_BOX(timeoutTopBox), timeout_op, FALSE, FALSE, 0);

		gtk_box_pack_start(GTK_BOX(timeoutBox), timeoutTopBox, FALSE, FALSE, 0);

		// 7. A/B分区设置部分
		GtkWidget* abpartBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
		GtkWidget* abpartLabel = gtk_label_new(_("A/B Part read/flash manually set --- FDL2"));
		gtk_label_set_xalign(GTK_LABEL(abpartLabel), 0.0);
		GtkWidget* abpartButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
		gtk_box_pack_start(GTK_BOX(abpartButtonBox), abpart_auto,FALSE,FALSE,0);
		gtk_box_pack_start(GTK_BOX(abpartButtonBox), abpart_a,FALSE,FALSE,0);
		gtk_box_pack_start(GTK_BOX(abpartButtonBox), abpart_b, FALSE,FALSE,0);

		gtk_box_pack_start(GTK_BOX(abpartBox), abpartLabel, FALSE,FALSE,0);
		gtk_box_pack_start(GTK_BOX(abpartBox), abpartButtonBox,FALSE,FALSE,0);
		// 将所有部分添加到主容器
		gtk_box_pack_start(GTK_BOX(emainBox), blockSizeBox, FALSE, FALSE, 0);

		// 分隔线占空间已删除
		/*
		GtkWidget* sep01 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
		gtk_widget_set_margin_top(sep01, 10);
		gtk_widget_set_margin_bottom(sep01, 10);
		gtk_box_pack_start(GTK_BOX(emainBox), sep01, FALSE, FALSE, 0);
		*/
		gtk_box_pack_start(GTK_BOX(emainBox), rawDataBox, FALSE, FALSE, 0);
		
		/*
		GtkWidget* sep02 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
		gtk_widget_set_margin_top(sep02, 10);
		gtk_widget_set_margin_bottom(sep02, 10);
		gtk_box_pack_start(GTK_BOX(emainBox), sep02, FALSE, FALSE, 0);
		*/
		gtk_box_pack_start(GTK_BOX(emainBox), transcodeBox, FALSE, FALSE, 0);
		
		/*
		GtkWidget* sep3 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
		gtk_widget_set_margin_top(sep3, 10);
		gtk_widget_set_margin_bottom(sep3, 10);
		gtk_box_pack_start(GTK_BOX(emainBox), sep3, FALSE, FALSE, 0);
		*/
		gtk_box_pack_start(GTK_BOX(emainBox), chargeBox, FALSE, FALSE, 0);

		/*
		GtkWidget* sep4 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
		gtk_widget_set_margin_top(sep4, 10);
		gtk_widget_set_margin_bottom(sep4, 10);
		gtk_box_pack_start(GTK_BOX(emainBox), sep4, FALSE, FALSE, 0);
		*/
		gtk_box_pack_start(GTK_BOX(emainBox), endDataBox, FALSE, FALSE, 0);
		

		gtk_box_pack_start(GTK_BOX(emainBox), abpartBox,FALSE,FALSE,0);
		/*
		GtkWidget* sep5 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
		gtk_widget_set_margin_top(sep5, 10);
		gtk_widget_set_margin_bottom(sep5, 10);
		gtk_box_pack_start(GTK_BOX(emainBox), sep5, FALSE, FALSE, 0);
		*/
		gtk_box_pack_start(GTK_BOX(emainBox), timeoutBox, FALSE, FALSE, 0);

		// 添加弹性空间使内容顶部对齐
		GtkWidget* bottomSpacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		gtk_widget_set_vexpand(bottomSpacer, TRUE);
		gtk_box_pack_end(GTK_BOX(emainBox), bottomSpacer, TRUE, TRUE, 0);

		// 添加到网格
		helper.addToGrid(advSetPage, emainBox, 0, 0, 4, 6);




		// ========== Debug Options Page ==========

		GtkWidget* dbgOptPage = helper.createGrid("dbg_opt_page", 5, 5);
		helper.addNotebookPage(notebook, dbgOptPage, _("Debug Options"));

		// 创建三个按钮
		GtkWidget* pactime = helper.createButton(_("Get pactime"), "pac_time", nullptr, 0, 0, 400, 32);
		GtkWidget* chipuid = helper.createButton(_("Get chip UID"), "chip_uid", nullptr, 0, 0, 400, 32);
		GtkWidget* ReadNand = helper.createButton(_("Check if NAND Storage"), "check_nand", nullptr, 0, 0, 400, 32);

		// 创建垂直盒子布局
		GtkWidget* mainBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
		gtk_box_set_homogeneous(GTK_BOX(mainBox), FALSE);

		// 添加第一个按钮行
		GtkWidget* row1Box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
		gtk_box_pack_start(GTK_BOX(row1Box), pactime, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(row1Box), gtk_label_new(""), TRUE, TRUE, 0); // 弹性空间

		// 添加第二个按钮行
		GtkWidget* row2Box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
		gtk_box_pack_start(GTK_BOX(row2Box), gtk_label_new(""), TRUE, TRUE, 0); // 弹性空间
		gtk_box_pack_start(GTK_BOX(row2Box), chipuid, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(row2Box), gtk_label_new(""), TRUE, TRUE, 0); // 弹性空间

		// 添加第三个按钮行
		GtkWidget* row3Box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
		gtk_box_pack_start(GTK_BOX(row3Box), gtk_label_new(""), TRUE, TRUE, 0); // 弹性空间
		gtk_box_pack_start(GTK_BOX(row3Box), ReadNand, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(row3Box), gtk_label_new(""), TRUE, TRUE, 0); // 弹性空间

		// 将各行添加到主盒子
		gtk_box_pack_start(GTK_BOX(mainBox), gtk_label_new(""), TRUE, TRUE, 0); // 顶部弹性空间
		gtk_box_pack_start(GTK_BOX(mainBox), row1Box, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(mainBox), gtk_label_new(""), FALSE, FALSE, 20); // 间距
		gtk_box_pack_start(GTK_BOX(mainBox), row2Box, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(mainBox), gtk_label_new(""), FALSE, FALSE, 20); // 间距
		gtk_box_pack_start(GTK_BOX(mainBox), row3Box, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(mainBox), gtk_label_new(""), TRUE, TRUE, 0); // 底部弹性空间

		// 添加到网格布局
		helper.addToGrid(dbgOptPage, mainBox, 0, 0, 4, 6);

		// 添加说明标签
		GtkWidget* infoLabel = gtk_label_new(_("Debug Options Page\nThis page contains device debugging functions"));
		gtk_label_set_justify(GTK_LABEL(infoLabel), GTK_JUSTIFY_CENTER);
		gtk_widget_set_margin_top(infoLabel, 50);
		gtk_widget_set_margin_bottom(infoLabel, 20);

		helper.addToGrid(dbgOptPage, infoLabel, 0, 6, 4, 1);

		// ========== About Page ==========

		GtkWidget* aboutPage = helper.createGrid("about_page", 5, 5);
		helper.addNotebookPage(notebook, aboutPage, _("About"));

		GtkWidget* scrolledAbout = gtk_scrolled_window_new(NULL, NULL);
		gtk_widget_set_size_request(scrolledAbout, 1084, 557);
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


		// ========== Log Page ==========

		GtkWidget* logPage = helper.createGrid("log_page", 5, 5);
		helper.addNotebookPage(notebook, logPage, _("Log"));

		GtkWidget* scrolledLog = gtk_scrolled_window_new(NULL, NULL);
		gtk_widget_set_size_request(scrolledLog, 1124, 500);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledLog),
		                               GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

		GtkWidget* logTextView = gtk_text_view_new();
		gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(logTextView), GTK_WRAP_WORD);
		gtk_widget_set_name(logTextView, "txtOutput");
		helper.addWidget("txtOutput", logTextView);
		gtk_container_add(GTK_CONTAINER(scrolledLog), logTextView);

		GtkWidget* expLogBtn = helper.createButton(_("Export"), "exp_log", nullptr, 0, 0, 120, 32);
		GtkWidget* logClearBtn = helper.createButton(_("Clear"), "log_clear", nullptr, 0, 0, 120, 32);

		// Add to grid
		helper.addToGrid(logPage, scrolledLog, 0, 0, 4, 8);

		GtkWidget* logButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 30);
		gtk_box_pack_start(GTK_BOX(logButtonBox), expLogBtn, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(logButtonBox), logClearBtn, FALSE, FALSE, 0);
		helper.addToGrid(logPage, logButtonBox, 0, 9, 4, 1);


		// ========== Bottom Controls ==========

		// ========== Bottom Controls ==========

		// 外层垂直 Box 包裹整个底部控制区
		GtkWidget* bottomContainer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
		gtk_widget_set_margin_start(bottomContainer, 15);
		gtk_widget_set_margin_end(bottomContainer, 15);
		gtk_widget_set_margin_top(bottomContainer, 10);
		gtk_widget_set_margin_bottom(bottomContainer, 10);

		// 【第一行】: 横向排列的控制按钮，整体居中
		GtkWidget* topActionBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		GtkWidget* buttonsHBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
		
		GtkWidget* poweroffBtn = helper.createButton(_("POWEROFF"), "poweroff", nullptr, 0, 0, 130, 32);
		GtkWidget* rebootBtn = helper.createButton(_("REBOOT"), "reboot", nullptr, 0, 0, 110, 32);
		GtkWidget* recoveryBtn = helper.createButton(_("BOOT TO RECOVERY"), "recovery", nullptr, 0, 0, 180, 32);
		GtkWidget* fastbootBtn = helper.createButton(_("BOOT TO FASTBOOT"), "fastboot", nullptr, 0, 0, 180, 32);
		
		gtk_box_pack_start(GTK_BOX(buttonsHBox), poweroffBtn, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(buttonsHBox), rebootBtn, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(buttonsHBox), recoveryBtn, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(buttonsHBox), fastbootBtn, FALSE, FALSE, 0);

		// 占位居中
		GtkWidget* cSpacer1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		GtkWidget* cSpacer2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		gtk_box_pack_start(GTK_BOX(topActionBox), cSpacer1, TRUE, TRUE, 0);
		gtk_box_pack_start(GTK_BOX(topActionBox), buttonsHBox, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(topActionBox), cSpacer2, TRUE, TRUE, 0);


		// 【第二行】: 分界线、进度条与文字
		GtkWidget* midProgressBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
		
		// 顶部带有一根横贯长线 (Demo 呈现上下间隔线效果)
		GtkWidget* statSeparatorTop = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
		gtk_box_pack_start(GTK_BOX(midProgressBox), statSeparatorTop, FALSE, FALSE, 0);

		// 长进度条
		GtkWidget* progressBar = gtk_progress_bar_new();
		gtk_widget_set_name(progressBar, "progressBar_1");
		helper.addWidget("progressBar_1", progressBar);
		gtk_widget_set_hexpand(progressBar, TRUE);
		// Demo中进度条横跨整行，高度极窄
		gtk_widget_set_size_request(progressBar, -1, 4); 
		gtk_widget_set_margin_top(progressBar, 5);
		gtk_box_pack_start(GTK_BOX(midProgressBox), progressBar, FALSE, FALSE, 0);


		// 【第三行】: 状态栏和进度数
		GtkWidget* bottomStatusBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);

		// 各状态子 Box
		GtkWidget* stBoxLabel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
		gtk_label_set_xalign(GTK_LABEL(statusLabel), 0.0);
		gtk_label_set_xalign(GTK_LABEL(conStatus), 0.0);
		gtk_box_pack_start(GTK_BOX(stBoxLabel), statusLabel, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(stBoxLabel), conStatus, FALSE, FALSE, 0);

		GtkWidget* mdBoxLabel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
		gtk_label_set_xalign(GTK_LABEL(modeLabel), 0.0);
		gtk_label_set_xalign(GTK_LABEL(modeStatus), 0.0);
		gtk_box_pack_start(GTK_BOX(mdBoxLabel), modeLabel, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(mdBoxLabel), modeStatus, FALSE, FALSE, 0);

		GtkWidget* stgBoxLabel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
		GtkWidget* storageLabel = helper.createLabel("Storage:", "", 0, 0, 60, 20);
		GtkWidget* storageMode = helper.createLabel("Unknown", "storage_mode", 0, 0, 100, 20);
		gtk_label_set_xalign(GTK_LABEL(storageLabel), 0.0);
		gtk_label_set_xalign(GTK_LABEL(storageMode), 0.0);
		gtk_box_pack_start(GTK_BOX(stgBoxLabel), storageLabel, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(stgBoxLabel), storageMode, FALSE, FALSE, 0);

		// 将左侧三段状态添加至最底层横行
		gtk_box_pack_start(GTK_BOX(bottomStatusBox), stBoxLabel, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(bottomStatusBox), mdBoxLabel, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(bottomStatusBox), stgBoxLabel, FALSE, FALSE, 0);

		// 将中间弹性撑开
		GtkWidget* stSpacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		gtk_box_pack_start(GTK_BOX(bottomStatusBox), stSpacer, TRUE, TRUE, 0);

		// 右侧进度文字
		GtkWidget* prgTextHBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
		GtkWidget* progressLabel = helper.createLabel(_("Progress:"), "progress_label", 0, 0, 60, 20);
		GtkWidget* percentLabel = helper.createLabel("0%", "percent", 0, 0, 40, 20);
		gtk_label_set_xalign(GTK_LABEL(progressLabel), 1.0); // 靠右
		gtk_label_set_xalign(GTK_LABEL(percentLabel), 1.0);
		gtk_box_pack_start(GTK_BOX(prgTextHBox), progressLabel, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(prgTextHBox), percentLabel, FALSE, FALSE, 0);

		gtk_box_pack_end(GTK_BOX(bottomStatusBox), prgTextHBox, FALSE, FALSE, 0);


		// 将三层组装
		gtk_box_pack_start(GTK_BOX(bottomContainer), topActionBox, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(bottomContainer), midProgressBox, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(bottomContainer), bottomStatusBox, FALSE, FALSE, 0);

		// Add notebook and bottom container to main grid
		gtk_grid_attach(GTK_GRID(mainGrid), notebook, 0, 0, 10, 1);
		gtk_grid_attach(GTK_GRID(mainGrid), bottomContainer, 0, 1, 10, 1);



		// 创建CSS样式
		GtkCssProvider* provider = gtk_css_provider_new();
		const gchar* css =
		    "label.big-label { font-size: 20px; }"
		    "progressbar { min-height: 9px; }"
		    "#wait_con_no_arrow button { min-width: 0px; padding: 0px; border: none; background: transparent; -gtk-icon-source: none; color: transparent; opacity: 0; }";
		gtk_css_provider_load_from_data(provider, css, -1, NULL);
		gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
		        GTK_STYLE_PROVIDER(provider),
		        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

		gtk_container_add(GTK_CONTAINER(window), mainGrid);

		// 显示所有组件
		gtk_widget_show_all(window);
		// Bind signals

		helper.bindClick(connectBtn, [argc, argv]() {
			std::thread([argc, argv]() {
				on_button_clicked_connect(helper, argc, argv);
			}).detach();
		});
		helper.bindClick(selectFdlBtn, []() {
			on_button_clicked_select_fdl(helper);
		});
		helper.bindClick(fdlExecBtn, [execfile]() {
			std::thread([execfile]() {
				on_button_clicked_fdl_exec(helper, execfile);
			}).detach();
		});
		helper.bindClick(selectCveBtn, []() {
			on_button_clicked_select_cve(helper);
		});
		helper.bindClick(writeBtn, []() {
			on_button_clicked_list_write(helper);
		});
		helper.bindClick(readBtn, []() {
			on_button_clicked_list_read(helper);
		});
		helper.bindClick(eraseBtn, []() {
			on_button_clicked_list_erase(helper);
		});
		helper.bindClick(backupAllBtn,[](){
			on_button_clicked_backup_all(helper);
		});
		helper.bindClick(poweroffBtn, []() {
			on_button_clicked_poweroff(helper);
		});
		helper.bindClick(rebootBtn, []() {
			on_button_clicked_reboot(helper);
		});
		helper.bindClick(recoveryBtn, []() {
			on_button_clicked_recovery(helper);
		});
		helper.bindClick(fastbootBtn, []() {
			on_button_clicked_fastboot(helper);
		});
		helper.bindClick(cancelBtn, []() {
			on_button_clicked_list_cancel(helper);
		});
		helper.bindValueChanged(blkSlider, [sizeCon]() {
			double value = gtk_range_get_value(GTK_RANGE(helper.getWidget("blk_size")));
			int intValue = static_cast<int>(value);
			gtk_label_set_text(GTK_LABEL(sizeCon), std::to_string(intValue).c_str());
			blk_size = intValue;
		});
		helper.bindClick(mSelectBtn, []() {
			on_button_clicked_m_select(helper);
		});
		helper.bindClick(mWriteBtn, []() {
			on_button_clicked_m_write(helper);
		});
		helper.bindClick(mReadBtn, []() {
			on_button_clicked_m_read(helper);
		});
		helper.bindClick(mEraseBtn, []() {
			on_button_clicked_m_erase(helper);
		});
		helper.bindClick(mCancelBtn, []() {
			on_button_clicked_m_cancel(helper);
		});
		helper.bindClick(setActiveA, []() {
			on_button_clicked_set_active_a(helper);
		});
		helper.bindClick(setActiveB, []() {
			on_button_clicked_set_active_b(helper);
		});
		helper.bindClick(selectXmlBtn, []() {
			on_button_clicked_select_xml(helper);
		});
		helper.bindClick(startRepartBtn, []() {
			on_button_clicked_start_repart(helper);
		});
		helper.bindClick(readXmlBtn, []() {
			on_button_clicked_read_xml(helper);
		});
		helper.bindClick(dmvDisable, []() {
			on_button_clicked_dmv_disable(helper);
		});
		helper.bindClick(dmvEnable, []() {
			on_button_clicked_dmv_enable(helper);
		});
		helper.bindClick(expLogBtn, []() {
			on_button_clicked_exp_log(helper);
		});
		helper.bindClick(logClearBtn, []() {
			on_button_clicked_log_clear(helper);
		});
		helper.bindClick(writeFBtn, []() {
			on_button_clicked_list_force_write(helper);
		});
		helper.bindClick(pactime, []() {
			on_button_clicked_pac_time(helper);
		});
		helper.bindClick(chipuid, []() {
			on_button_clicked_chip_uid(helper);
		});
		helper.bindClick(ReadNand, []() {
			on_button_clicked_check_nand(helper);
		});
		helper.bindClick(dis_avb, []() {
			on_button_clicked_dis_avb(helper);
		});
		helper.bindClick(rawDataEn, []() {
			on_button_clicked_raw_data_en(helper);
		});
		helper.bindClick(rawDataDis, []() {
			on_button_clicked_raw_data_dis(helper);
		});
		helper.bindClick(transcode_en, []() {
			on_button_clicked_transcode_en(helper);
		});
		helper.bindClick(transcode_dis, []() {
			on_button_clicked_transcode_dis(helper);
		});
		helper.bindClick(charge_en, []() {
			on_button_clicked_charge_en(helper);
		});
		helper.bindClick(charge_dis, []() {
			on_button_clicked_charge_dis(helper);
		});
		helper.bindClick(end_data_en, []() {
			on_button_clicked_end_data_en(helper);
		});
		helper.bindClick(end_data_dis, []() {
			on_button_clicked_end_data_dis(helper);
		});
		helper.bindValueChanged(timeout_op, [timeout_op]() {
			io->timeout = helper.getSpinValue(timeout_op);
		});
		helper.bindClick(modifyBtn,[]() {
			on_button_clicked_modify_part(helper);
		});
		helper.bindClick(addNewPartBtn,[](){
			on_button_clicked_modify_new_part(helper);
		});
		helper.bindClick(RemvPartBtn,[](){
			on_button_clicked_modify_rm_part(helper);
		});
		helper.bindClick(RenmPartBtn,[](){
			on_button_clicked_modify_ren_part(helper);
		});
		helper.bindClick(xmlGetBtn,[](){
			on_button_clicked_xml_get(helper);
		});
		helper.bindClick(abpart_auto,[](){
			on_button_clicked_abpart_auto(helper);
		});
		helper.bindClick(abpart_a,[](){
			on_button_clicked_abpart_a(helper);
		});
		helper.bindClick(abpart_b,[](){
			on_button_clicked_abpart_b(helper);
		});
	}
	DisableWidgets(helper);
	// 启动GTK主循环
	gtk_main();

	//Disable

	return 0;
}
int main(int argc, char** argv) {
	setlocale(LC_ALL, "");
	bindtextdomain("sfd_tool", "./locale");
	textdomain("sfd_tool");
	bind_textdomain_codeset("sfd_tool", "UTF-8");

	signal(SIGSEGV, crash_handler);   // 段错误
    signal(SIGABRT, crash_handler);   // 断言失败
	signal(SIGFPE, crash_handler);    // 浮点异常
	signal(SIGILL, crash_handler);    // 非法指令
#ifdef __linux__
	signal(SIGKILL, crash_handler);   // 杀死进程(Linux)
#endif
	signal(SIGTERM, crash_handler);   // 终止信号
	if (argc > 1 && !strcmp(argv[1], "--no-gui")) {
		// Call the console version of main
		return main_console(argc - 1, argv + 1); // Skip the first argument
	} else {
		return gtk_kmain(argc, argv);
	}
}
