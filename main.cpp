#include <iostream>
#include <cstring>
#include "common.h"
#include "main.h"
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
#include "pages/page_pac_flash.h"
#include <thread>
#include <chrono>
#include <gtk/gtk.h>
#include <sstream>  
#include <iomanip>
#include "GenTosNoAvb.h"
#ifdef __linux__
#include <unistd.h>
#include <execinfo.h>
#elif defined(_WIN32)
#include <windows.h>
#include <dbghelp.h>
#endif
const char *AboutText = "SFD Tool GUI\n\nVersion 1.7.6.0 LTV Edition\n\nCopyright 2026 Ryan Crepa    QQ:3285087232    @Bilibili RyanCrepa\n\nVersion logs:\n\n---v 1.7.1.0---\nFirst GUI Version\n--v 1.7.1.1---\nFix check_confirm issue\n---v 1.7.1.2---\nAdd Force write function when partition list is available\n---v 1.7.2.0---\nAdd debug options\n---v 1.7.2.1---\nAdd root permission check for Linux\n---v 1.7.2.2---\nAdd dis_avb function\n---v 1.7.2.3---\nFix some bugs\n---v 1.7.3.0---\nAdd some advanced settings\n---v 1.7.3.1---\nAdd SPRD4 one-time kick mode\n---v 1.7.3.2---\nFix some bugs\n---v 1.7.3.3---\nFix dis_avb func\n---v 1.7.3.4---\nFix some bugs, improved UI\n---v 1.7.3.5---\nFix some bugs\n---v 1.7.4.0---\nAdd window dragging detection for Windows dialog-showing issue\n---v 1.7.4.1---\nAdd CVE v2 function, fix some bugs\n---v 1.7.4.2---\nFix some bugs, add crash info displaying\n---v 1.7.4.3---\nFix some bugs\n---v 1.7.5.0---\nFix some bugs, improved console\n---v 1.7.5.1---\nFix some bugs, add partition table modify function, add DHTB Signature read for ums9117\n---v 1.7.5.2---\nAdd slot flash/read manually set, add storage/slot showing\n---v 1.7.6.0---\nAdd PAC flash func, auto FDL send\n\n\nUnder GPL v3 License\nGithub: C-Hidery/sfd_tool\nLTV means Long-time-version";
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
int ret;
int conn_wait = 30 * REOPEN_FREQ;
int keep_charge = 1, end_data = 0, blk_size = 0, skip_confirm = 1, highspeed = 0, cve_v2 = 0;
int nand_info[3];
int argcount = 0, stage = -1, nand_id = DEFAULT_NAND_ID;
unsigned exec_addr = 0, baudrate = 0;
int bootmode = -1, at = 0, async = 1;
int waitFDL1 = -1;
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
 {
	helper.enableWidget("transcode_en");
	helper.enableWidget("transcode_dis");
	helper.enableWidget("end_data_dis");
	helper.enableWidget("end_data_en");
	helper.enableWidget("charge_en");
	helper.enableWidget("charge_dis");
	helper.enableWidget("raw_data_en");
	helper.enableWidget("raw_data_dis");
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

//fdl exec
std::string fdl1_path_json;
std::string fdl2_path_json;
uint32_t fdl1_addr_json;
uint32_t fdl2_addr_json;
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
		create_pac_flash_page(helper, notebook);
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
		bind_pac_flash_signals(helper);
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
	signal(SIGIOT, crash_handler);    // IOT Trap (Linux)
#endif
	signal(SIGTERM, crash_handler);   // 终止信号
	if (argc > 1 && !strcmp(argv[1], "--no-gui")) {
		// Call the console version of main
		return main_console(argc - 1, argv + 1); // Skip the first argument
	} else {
		return gtk_kmain(argc, argv);
	}
}
