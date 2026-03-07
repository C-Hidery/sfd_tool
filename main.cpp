#include <iostream>
#include <cstring>
#include "common.h"
#include "main.h"
#include "nlohmann/json.hpp" // json for auto sending FDL
#include "GtkWidgetHelper.hpp"
#include "i18n.h"
#include "ui_common.h"
#include "pages/page_connect.h"
#include "pages/page_partition.h"
#include "pages/page_manual.h"
#include "pages/page_advanced_op.h"
#include "pages/page_advanced_set.h"
#include "pages/page_debug.h"
#include "pages/page_about.h"
#include "pages/page_log.h"
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
	(void)sig;
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
	snprintf(fn_partlist, sizeof(fn_partlist), "partition_%lld.xml", (long long)time(nullptr));

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
		// ========== 模块化页面创建 ==========
		create_connect_page(helper, notebook);
		create_partition_page(helper, notebook);
		create_manual_page(helper, notebook);
		create_advanced_op_page(helper, notebook);
		create_advanced_set_page(helper, notebook);
		create_debug_page(helper, notebook);
		create_log_page(helper, notebook);
		create_about_page(helper, notebook);

		// ========== 底部控制栏 ==========
		GtkWidget* bottomContainer = create_bottom_controls(helper);

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
		
		// 强制默认选中第一个标签页（“连接”页）
		gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), 0);

		// ========== 模块化信号绑定 ==========
		bind_connect_signals(helper, argc, argv);

		// fdl_exec 需要 execfile 参数，单独绑定
		helper.bindClick(helper.getWidget("fdl_exec"), [execfile]() {
			std::thread([execfile]() {
				on_button_clicked_fdl_exec(helper, execfile);
			}).detach();
		});

		bind_partition_signals(helper);
		bind_manual_signals(helper);
		bind_advanced_op_signals(helper);
		bind_advanced_set_signals(helper);
		bind_debug_signals(helper);
		bind_log_signals(helper);
		bind_bottom_signals(helper, bottomContainer);
	}
	DisableWidgets(helper);
	// 启动GTK主循环
	gtk_main();

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
