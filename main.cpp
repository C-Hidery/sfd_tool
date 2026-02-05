#include <iostream>
#include <cstring>
#include "common.h"
#include "main.h"
#include "GtkWidgetHelper.hpp"
#include <thread>
#include <chrono>
#include <gtk/gtk.h>
const char *AboutText = "SFD Tool GUI\n\nVersion 1.7.1.0\n\nBy Ryan Crepa    QQ:3285087232    @Bilibili RyanCrepa\n\nVersion logs:\n\n---v 1.7.1.0---\nFirst GUI Version\n--v 1.7.1.1---\nFix check_confirm issue\n---v 1.7.1.2---\nAdd Force write function when partition list is available";
const char* Version = "[1.2.0.0@_250726]";
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
int ret, wait = 30 * REOPEN_FREQ;
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
// 选择文件
std::string showFileChooser(GtkWindow* parent, bool open = true) {
    GtkWidget* dialog;
    
    if (open) {
        dialog = gtk_file_chooser_dialog_new("选择文件",
                                           parent,
                                           GTK_FILE_CHOOSER_ACTION_OPEN,
                                           "_取消", GTK_RESPONSE_CANCEL,
                                           "_打开", GTK_RESPONSE_ACCEPT,
                                           NULL);
    } else {
        dialog = gtk_file_chooser_dialog_new("保存文件",
                                           parent,
                                           GTK_FILE_CHOOSER_ACTION_SAVE,
                                           "_取消", GTK_RESPONSE_CANCEL,
                                           "_保存", GTK_RESPONSE_ACCEPT,
                                           NULL);
    }
    
    // 设置过滤器
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "所有文件 (*.*)");
    gtk_file_filter_add_pattern(filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    
    gint result = gtk_dialog_run(GTK_DIALOG(dialog));
    std::string filename;
    
    if (result == GTK_RESPONSE_ACCEPT) {
        char* file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (file) {
            filename = file;
            g_free(file);
        }
    }
    
    gtk_widget_destroy(dialog);
    return filename;
}

// 选择文件夹
std::string showFolderChooser(GtkWindow* parent) {
    GtkWidget* dialog = gtk_file_chooser_dialog_new("选择文件夹",
                                                   parent,
                                                   GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                                   "_取消", GTK_RESPONSE_CANCEL,
                                                   "_选择", GTK_RESPONSE_ACCEPT,
                                                   NULL);
    
    gint result = gtk_dialog_run(GTK_DIALOG(dialog));
    std::string folder;
    
    if (result == GTK_RESPONSE_ACCEPT) {
        char* dir = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (dir) {
            folder = dir;
            g_free(dir);
        }
    }
    
    gtk_widget_destroy(dialog);
    return folder;
}
// 信息对话框
void showInfoDialog(GtkWindow* parent, const char* title, const char* message) {
    GtkWidget* dialog = gtk_message_dialog_new(parent,
                                              GTK_DIALOG_MODAL,
                                              GTK_MESSAGE_INFO,
                                              GTK_BUTTONS_OK,
                                              "%s", message);
    gtk_window_set_title(GTK_WINDOW(dialog), title);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

// 警告对话框
void showWarningDialog(GtkWindow* parent, const char* title, const char* message) {
    GtkWidget* dialog = gtk_message_dialog_new(parent,
                                              GTK_DIALOG_MODAL,
                                              GTK_MESSAGE_WARNING,
                                              GTK_BUTTONS_OK,
                                              "%s", message);
    gtk_window_set_title(GTK_WINDOW(dialog), title);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

// 错误对话框
void showErrorDialog(GtkWindow* parent, const char* title, const char* message) {
    GtkWidget* dialog = gtk_message_dialog_new(parent,
                                              GTK_DIALOG_MODAL,
                                              GTK_MESSAGE_ERROR,
                                              GTK_BUTTONS_OK,
                                              "%s", message);
    gtk_window_set_title(GTK_WINDOW(dialog), title);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

// 确认对话框（是/否）
bool showConfirmDialog(GtkWindow* parent, const char* title, const char* message) {
    GtkWidget* dialog = gtk_message_dialog_new(parent,
                                              GTK_DIALOG_MODAL,
                                              GTK_MESSAGE_QUESTION,
                                              GTK_BUTTONS_YES_NO,
                                              "%s", message);
    gtk_window_set_title(GTK_WINDOW(dialog), title);
    
    gint result = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    return (result == GTK_RESPONSE_YES);
}
// 文件选择对话框函数
std::string showSaveFileDialog(GtkWindow* parent, 
                               const std::string& default_filename = "",
                               const std::vector<std::pair<std::string, std::string>>& filters = {}) {
    GtkWidget* dialog = gtk_file_chooser_dialog_new("保存文件",
                                                   parent,
                                                   GTK_FILE_CHOOSER_ACTION_SAVE,
                                                   "_取消", GTK_RESPONSE_CANCEL,
                                                   "_保存", GTK_RESPONSE_ACCEPT,
                                                   NULL);
    
    // 设置默认文件名
    if (!default_filename.empty()) {
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), default_filename.c_str());
    }
    
    // 添加文件过滤器
    for (const auto& filter_pair : filters) {
        GtkFileFilter* filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, filter_pair.first.c_str());
        gtk_file_filter_add_pattern(filter, filter_pair.second.c_str());
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    }
    
    // 默认添加"所有文件"过滤器
    GtkFileFilter* all_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(all_filter, "所有文件 (*.*)");
    gtk_file_filter_add_pattern(all_filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), all_filter);
    
    std::string filename;
    gint result = gtk_dialog_run(GTK_DIALOG(dialog));
    
    if (result == GTK_RESPONSE_ACCEPT) {
        char* file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (file) {
            filename = file;
            g_free(file);
        }
    }
    
    gtk_widget_destroy(dialog);
    return filename;
}

void EnableWidgets(GtkWidgetHelper helper){
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
void on_button_clicked_list_write(GtkWidgetHelper helper){
    GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
    std::string filename = showFileChooser(parent, true);
    std::string part_name = getSelectedPartitionName(helper);
    if (filename.empty()) {
        showErrorDialog(parent, "错误 Error", "未选择分区列表文件！\nNo partition list file selected!");
        return;
    }
    if (io->part_count == 0 && io->part_count_c == 0) {
        showErrorDialog(parent, "错误 Error", "当前未加载分区表，无法写入分区列表！\nNo partition table loaded, cannot write partition list!");
        return;
    }
    FILE* fi;
    fi = fopen(filename.c_str(), "r");
	if (fi == nullptr) { DEG_LOG(E,"File does not exist.\n"); return; }
	else fclose(fi);
    get_partition_info(io, part_name.c_str(), 0);
	if (!gPartInfo.size) { DEG_LOG(E,"Partition does not exist\n");return;}
    std::thread([filename,parent](){load_partition_unify(io, gPartInfo.name, filename.c_str(), blk_size ? blk_size : DEFAULT_BLK_SIZE, isCMethod);showInfoDialog(parent, "完成 Completed", "分区写入完成！\nPartition write completed!");}).detach();
    
}
void on_button_clicked_list_force_write(GtkWidgetHelper helper){
    GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
    std::string filename = showFileChooser(parent, true);
    std::string part_name = getSelectedPartitionName(helper);
    if (filename.empty()) {
        showErrorDialog(parent, "错误 Error", "未选择分区列表文件！\nNo partition list file selected!");
        return;
    }
    if (io->part_count == 0 && io->part_count_c == 0) {
        showErrorDialog(parent, "错误 Error", "当前未加载分区表，无法写入分区列表！\nNo partition table loaded, cannot write partition list!");
        return;
    }
    FILE* fi;
    fi = fopen(filename.c_str(), "r");
    if (fi == nullptr) { DEG_LOG(E,"File does not exist.\n"); return; }
    else fclose(fi);
    get_partition_info(io, part_name.c_str(), 0);
    if (!gPartInfo.size) { DEG_LOG(E,"Partition does not exist\n");return;}
    bool i_op = showConfirmDialog(parent, "确认 Confirm", "强制写入分区可能会导致设备变砖，是否继续？\nForce writing partitions may brick the device, do you want to continue?");
    if (!i_op) return;
    if (!strncmp(gPartInfo.name, "splloader", 9)) {
        showErrorDialog(parent, "错误 Error", "强制写入模式下不允许写入splloader分区！\nForce write mode does not allow writing to splloader partition!");
        return;
    }
    if(isCMethod){
        bool i_is = showConfirmDialog(parent, "警告 Warning", "当前处于兼容分区表模式，强制写入可能会导致设备变砖！\nCurrently in compatibility-method-PartList mode, force writing may brick the device!");
        if (!i_is) return;
        if(io->part_count_c){
            std::thread([filename,parent](){
                for (int i = 0; i < io->part_count_c; i++)
					if (!strcmp(gPartInfo.name, (*(io->Cptable + i)).name)) {
						load_partition_force(io, i, filename.c_str(), blk_size ? blk_size : DEFAULT_BLK_SIZE,1);
						break;
					}
                showInfoDialog(parent, "完成 Completed", "分区强制写入完成！\nPartition force write completed!");
            }).detach();
        }
    }
    else{
        std::thread([filename,parent](){
            for (int i = 0; i < io->part_count; i++)
                if (!strcmp(gPartInfo.name, (*(io->ptable + i)).name)) {
                    load_partition_force(io, i, filename.c_str(), blk_size ? blk_size : DEFAULT_BLK_SIZE,0);
                    break;
                }
            showInfoDialog(parent, "完成 Completed", "分区强制写入完成！\nPartition force write completed!");
        }).detach();
    }

}
void on_button_clicked_list_read(GtkWidgetHelper helper){
    GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
    std::string part_name = getSelectedPartitionName(helper);
    std::string savePath = showSaveFileDialog(parent, part_name + ".img");
    if (savePath.empty()) {
        showErrorDialog(parent, "错误 Error", "未选择保存路径！\nNo save path selected!");
        return;
    }
    if (io->part_count == 0 && io->part_count_c == 0) {
        showErrorDialog(parent, "错误 Error", "当前未加载分区表，无法写入分区列表！\nNo partition table loaded, cannot write partition list!");
        return;
    }
    //dump_partition(io, "splloader", 0, g_spl_size, "splloader.bin", blk_size ? blk_size : DEFAULT_BLK_SIZE);
    get_partition_info(io, part_name.c_str(), 1);
    if (!gPartInfo.size) {
		DEG_LOG(E,"Partition not exist\n");
		return;
	}
    std::thread([savePath,parent](){dump_partition(io, gPartInfo.name, 0, gPartInfo.size, savePath.c_str(), blk_size ? blk_size : DEFAULT_BLK_SIZE);showInfoDialog(parent, "完成 Completed", "分区读取完成！\nPartition read completed!");}).detach();
    
}
void on_button_clicked_list_erase(GtkWidgetHelper helper){
    GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
    std::string part_name = getSelectedPartitionName(helper);
    get_partition_info(io, part_name.c_str(), 0);
    if (!gPartInfo.size) {
		DEG_LOG(E,"Partition not exist\n");
		return;
	}
    std::thread([parent](){erase_partition(io, gPartInfo.name, isCMethod);showInfoDialog(parent, "完成 Completed", "分区擦除完成！\nPartition erase completed!");}).detach();
    
}
void on_button_clicked_poweroff(GtkWidgetHelper helper){
    encode_msg_nocpy(io, BSL_CMD_POWER_OFF, 0);
    if(!send_and_check(io)){ spdio_free(io); exit(0);}
}
void on_button_clicked_reboot(GtkWidgetHelper helper){
    encode_msg_nocpy(io, BSL_CMD_NORMAL_RESET, 0);
    if (!send_and_check(io)) { spdio_free(io); exit(0); }
}
void on_button_clicked_recovery(GtkWidgetHelper helper){
    char* miscbuf = NEWN char[0x800];
	if (!miscbuf) ERR_EXIT("malloc failed\n");
	memset(miscbuf, 0, 0x800);
	strcpy(miscbuf, "boot-recovery");
	w_mem_to_part_offset(io, "misc", 0, (uint8_t*)miscbuf, 0x800, 0x1000, isCMethod);
	delete[](miscbuf);
	encode_msg_nocpy(io, BSL_CMD_NORMAL_RESET, 0);
	if (!send_and_check(io)) { spdio_free(io); exit(0); }
}
void on_button_clicked_fastboot(GtkWidgetHelper helper){
    char* miscbuf = NEWN char[0x800];
	if (!miscbuf) ERR_EXIT("malloc failed\n");
	memset(miscbuf, 0, 0x800);
	strcpy(miscbuf, "boot-recovery");
	strcpy(miscbuf + 0x40, "recovery\n--fastboot\n");
	w_mem_to_part_offset(io, "misc", 0, (uint8_t*)miscbuf, 0x800, 0x1000, isCMethod);
	delete[](miscbuf);
	encode_msg_nocpy(io, BSL_CMD_NORMAL_RESET, 0);
	if (!send_and_check(io)) { spdio_free(io); exit(0); }
}
void on_button_clicked_list_cancel(GtkWidgetHelper helper){
    signal_handler(0);
    showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), "提示 Tips", "已取消当前分区操作！\nCurrent partition operation cancelled!");
}
void on_button_clicked_backup_all(GtkWidgetHelper helper){
    if (!isCMethod) {
		if (gpt_failed == 1) io->ptable = partition_list(io, fn_partlist, &io->part_count);
		if (!io->part_count) { DEG_LOG(E, "Partition table not available\n"); return; }
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
	}
	else {
		if(!io->part_count_c) { DEG_LOG(E, "Partition table not available\n"); return; }
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
}
void on_button_clicked_m_select(GtkWidgetHelper helper) {
    GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
    std::string filename = showFileChooser(parent, true);
    if (!filename.empty()) {
        helper.setEntryText(helper.getWidget("m_file_path"), filename);
    }
}
void on_button_clicked_m_write(GtkWidgetHelper helper){
    GtkWidget *parent = helper.getWidget("main_window");
    std::string filename = helper.getEntryText(helper.getWidget("m_file_path"));
    std::string part_name = helper.getEntryText(helper.getWidget("m_part_flash"));
    if (filename.empty()) {
        showErrorDialog(GTK_WINDOW(parent), "错误 Error", "未选择分区镜像文件！\nNo partition image file selected!");
        return;
    }
    if (part_name.empty()) {
        showErrorDialog(GTK_WINDOW(parent), "错误 Error", "未指定分区名称！\nNo partition name specified!");
        return; 
    }
    FILE* fi;
    fi = fopen(filename.c_str(), "r");
    if (fi == nullptr) { DEG_LOG(E,"File does not exist.\n"); return; }
    else fclose(fi);
    get_partition_info(io, part_name.c_str(), 0);
    if (!gPartInfo.size) { DEG_LOG(E,"Partition does not exist\n");return;}
    std::thread([parent,filename](){load_partition_unify(io, gPartInfo.name, filename.c_str(), blk_size ? blk_size : DEFAULT_BLK_SIZE, isCMethod);showInfoDialog(GTK_WINDOW(parent), "完成 Completed", "分区写入完成！\nPartition write completed!");}).detach();
}
void on_button_clicked_m_read(GtkWidgetHelper helper){
    GtkWidget *parent = helper.getWidget("main_window");
    std::string part_name = helper.getEntryText(helper.getWidget("m_part_read"));
    std::string savePath = showSaveFileDialog(GTK_WINDOW(parent), part_name + ".img");
    if (part_name.empty()) {
        showErrorDialog(GTK_WINDOW(parent), "错误 Error", "未指定分区名称！\nNo partition name specified!");
        return; 
    }
    get_partition_info(io, part_name.c_str(), 0);
    if (!gPartInfo.size) { DEG_LOG(E,"Partition does not exist\n");return;}

    std::thread([parent,savePath](){dump_partition(io, gPartInfo.name, 0, gPartInfo.size, savePath.c_str(), blk_size ? blk_size : DEFAULT_BLK_SIZE);showInfoDialog(GTK_WINDOW(parent), "完成 Completed", "分区读取完成！\nPartition read completed!");}).detach();   
}
void on_button_clicked_m_erase(GtkWidgetHelper helper){
    GtkWidget *parent = helper.getWidget("main_window");
    std::string part_name = helper.getEntryText(helper.getWidget("m_part_erase"));
    if (part_name.empty()) {
        showErrorDialog(GTK_WINDOW(parent), "错误 Error", "未指定分区名称！\nNo partition name specified!");
        return; 
    }
    get_partition_info(io, part_name.c_str(), 0);
    if (!gPartInfo.size) { DEG_LOG(E,"Partition does not exist\n");return;}
    std::thread([parent](){erase_partition(io, gPartInfo.name, isCMethod);showInfoDialog(GTK_WINDOW(parent), "完成 Completed", "分区擦除完成！\nPartition erase completed!");}).detach();
}
void on_button_clicked_m_cancel(GtkWidgetHelper helper){
    signal_handler(0);
    showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), "提示 Tips", "已取消当前分区操作！\nCurrent partition operation cancelled!");
}
void on_button_clicked_set_active_a(GtkWidgetHelper helper){
    set_active(io,"a", isCMethod);
    showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), "提示 Tips", "已设置当前分区为A槽！\nCurrent active partition set to Slot A!");
}
void on_button_clicked_set_active_b(GtkWidgetHelper helper){
    set_active(io,"b", isCMethod);
    showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), "提示 Tips", "已设置当前分区为B槽！\nCurrent active partition set to Slot B!");
}
void on_button_clicked_start_repart(GtkWidgetHelper helper){
    GtkWidget *parent = helper.getWidget("main_window");
    std::string filePath = helper.getEntryText(helper.getWidget("xml_path")); 
    FILE *fi = fopen(filePath.c_str(), "r");
	if (fi == nullptr) { DEG_LOG(E,"File does not exist."); return;}
	else fclose(fi);
    repartition(io, filePath.c_str());
    showInfoDialog(GTK_WINDOW(parent), "完成 Completed", "重新分区完成！\nRepartition completed!");
}
void on_button_clicked_read_xml(GtkWidgetHelper helper){
    GtkWidget* parent = helper.getWidget("main_window");
    std::string savePath = showSaveFileDialog(GTK_WINDOW(parent), "partition_table.xml", { {"XML文件 (*.xml)", "*.xml"} });
    if (savePath.empty()) {
        showErrorDialog(GTK_WINDOW(parent), "错误 Error", "未选择保存路径！\nNo save path selected!");
        return;
    }
    if (!isCMethod) {
		if (gpt_failed == 1) io->ptable = partition_list(io, savePath.c_str(), &io->part_count);
		if (!io->part_count) { DEG_LOG(E, "Partition table not available"); return; }
		else {
			DBG_LOG("  0 %36s     %lldKB\n", "splloader",(long long)g_spl_size / 1024);
			FILE* fo = my_fopen(savePath.c_str(), "wb");
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
	}
	else {
		int c = io->part_count_c;
		if (!c) { DEG_LOG(E, "Partition table not available"); return; }
		else {
			DBG_LOG("  0 %36s     %lldKB\n", "splloader",(long long)g_spl_size / 1024);
			FILE* fo = my_fopen(savePath.c_str(), "wb");
			if (!fo) ERR_EXIT("Failed to open file\n");
			fprintf(fo, "<Partitions>\n");
			char* name;
			int o = io->verbose;
			io->verbose = -1;
			for (int i = 0; i < c; i++) {
				name = (*(io->Cptable + i)).name;
				DBG_LOG("%3d %36s %7lldMB\n", i + 1, name, ((*(io->Cptable + i)).size >> 20));
				fprintf(fo, "    <Partition id=\"%s\" size=\"", name);
				if (check_partition(io,"userdata",0) != 0 && i + 1 == io->part_count_c) fprintf(fo, "0x%x\"/>\n", ~0);
				else fprintf(fo, "%lld\"/>\n", ((*(io->Cptable + i)).size >> 20));			
			}
			fprintf(fo, "</Partitions>");
			fclose(fo);
			io->verbose = o;
			DEG_LOG(I, "Partition table saved to %s", savePath.c_str());
        }
	}
	showInfoDialog(GTK_WINDOW(parent), "完成 Completed", "分区表导出完成！\nPartition table export completed!");
			
}
void on_button_clicked_dmv_enable(GtkWidgetHelper helper){
    GtkWidget *parent = helper.getWidget("main_window");
    dm_enable(io, blk_size ? blk_size : DEFAULT_BLK_SIZE, isCMethod);
    showInfoDialog(GTK_WINDOW(parent), "完成 Completed", "已启用DM-Verity保护！\nDM-Verity protection enabled!");
}
void on_button_clicked_dmv_disable(GtkWidgetHelper helper){
    GtkWidget *parent = helper.getWidget("main_window");
    dm_disable(io, blk_size ? blk_size : DEFAULT_BLK_SIZE, isCMethod);
    showInfoDialog(GTK_WINDOW(parent), "完成 Completed", "已禁用DM-Verity保护！\nDM-Verity protection disabled!"); 
}
void on_button_clicked_select_xml(GtkWidgetHelper helper){
    GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
    std::string filename = showFileChooser(parent, true);
    if (!filename.empty()) {
        helper.setEntryText(helper.getWidget("xml_path"), filename);
    }
}
void on_button_clicked_exp_log(GtkWidgetHelper helper){
    GtkWidget* parent = helper.getWidget("main_window");
    GtkWidget *txtOutput = helper.getWidget("txtOutput");
    std::string savePath = showSaveFileDialog(GTK_WINDOW(parent), "sfd_tool_log.txt", { {"文本文件 (*.txt)", "*.txt"} });
    if (savePath.empty()) {
        showErrorDialog(GTK_WINDOW(parent), "错误 Error", "未选择保存路径！\nNo save path selected!");
        return;
    }
    const char* txt = helper.getTextAreaText(txtOutput);
    FILE* fo = fopen(savePath.c_str(), "w");
    if (!fo) {
        showErrorDialog(GTK_WINDOW(parent), "错误 Error", "无法保存日志文件！\nFailed to save log file!");
        return;
    }
    fprintf(fo, "%s", txt);
    fclose(fo);
    showInfoDialog(GTK_WINDOW(parent), "完成 Completed", "日志导出完成！\nLog export completed!");
}
void on_button_clicked_log_clear(GtkWidgetHelper helper){
    GtkWidget* txtOutput = helper.getWidget("txtOutput");
    helper.setTextAreaText(txtOutput, "");
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
void confirm_partition_c(GtkWidgetHelper helper){
    bool i_is = showConfirmDialog(GTK_WINDOW(helper.getWidget("main_window")), "Confirm 确认", "No partition table found on current device, read partition list through compatibility method?\nWarn: This mode may not find all partitions on your device, use caution with force write!\n当前设备未找到分区表，是否通过兼容方式读取分区列表？\n警告：此模式可能无法找到设备上的所有分区，强制写入时请谨慎使用！");
    if (i_is) {
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
}

void on_button_clicked_connect(GtkWidgetHelper helper, int argc, char** argv) {
    GtkWidget* waitBox = helper.getWidget("wait_con");
    GtkWidget* sprd4Switch = helper.getWidget("sprd4");
    GtkWidget* cveSwitch = helper.getWidget("exec_addr");
    GtkWidget* cveAddr = helper.getWidget("cve_addr");
    GtkWidget* cveAddrC = helper.getWidget("cve_addr_c");
    if (argc > 1 && !strcmp(argv[1],"--reconnect")){
        stage = 99;
        showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), "提示 Tips", "你已启动重连模式，重连模式下只能兼容获取分区列表！\nYou have entered Reconnect Mode, which only supports compatibility-method partition list retrieval!");
    }
	helper.disableWidget("connect_1");
    double wait_time = helper.getSpinValue(waitBox);
    bool isSprd4 = helper.getSwitchState(sprd4Switch);
    bool isCve = helper.getSwitchState(cveSwitch);
    const char* cve_path = helper.getEntryText(cveAddr);
    const char* cve_addr = helper.getEntryText(cveAddrC);
    DEG_LOG(I,"Begin to boot...(%fs)", wait_time);
	wait = static_cast<int>(wait_time * REOPEN_FREQ);
    if (isSprd4){
        DEG_LOG(I,"Using SPRD4 mode to kick device.");
		isKickMode = 1;
		bootmode = strtol("2", nullptr, 0); at = 0;
    }
    if (isCve){
        DEG_LOG(I,"Using CVE binary: %s at address: %s", cve_path, cve_addr);
    }
	
#if !USE_LIBUSB
	bListenLibusb = 0;
	if (at || bootmode >= 0) {
		io->hThread = CreateThread(nullptr, 0, ThrdFunc, nullptr, 0, &io->iThread);
		if (io->hThread == nullptr) return;
		ChangeMode(io, wait / REOPEN_FREQ * 1000, bootmode, at);
		wait = 30 * REOPEN_FREQ;
		stage = -1;
	}
#else
	if (!libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) { DBG_LOG("hotplug unsupported on this platform\n"); bListenLibusb = 0; bootmode = -1; at = 0; }
	if (at || bootmode >= 0) {
		startUsbEventHandle();
		ChangeMode(io, wait / REOPEN_FREQ * 1000, bootmode, at);
		wait = 30 * REOPEN_FREQ;
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
			DEG_LOG(E,"Create Receive Thread Fail.");
		}
	}
#endif
#endif
	if (!m_bOpened) {
		DBG_LOG("<waiting for connection,mode:dl,%ds>\n", wait / REOPEN_FREQ);
		
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
			if (i >= wait)
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
			if (i >= wait) {
				ERR_EXIT("%s: Failed to find port.\n",o_exception);
		}
#endif
			usleep(1000000 / REOPEN_FREQ);
		}
	}
	io->flags |= FLAGS_TRANSCODE;
	if (stage != -1) {
		io->flags &= ~FLAGS_CRC16;
		encode_msg_nocpy(io, BSL_CMD_CONNECT, 0);
	}
	else encode_msg(io, BSL_CMD_CHECK_BAUD, nullptr, 1);
	//handshake
	for (int i = 0; ; ++i) {
		//check if device is connected correctly.
		if (io->recv_buf[2] == BSL_REP_VER) {
			ret = BSL_REP_VER;
			memcpy(io->raw_buf + 4, io->recv_buf + 5, 5);
			io->raw_buf[2] = 0;
			io->raw_buf[3] = 5;
			io->recv_buf[2] = 0;
		}
		else if (io->recv_buf[2] == BSL_REP_VERIFY_ERROR ||
			io->recv_buf[2] == BSL_REP_UNSUPPORTED_COMMAND) {
			if (!fdl1_loaded) {
				ret = io->recv_buf[2];
				io->recv_buf[2] = 0;
			}
			else ERR_EXIT("Failed to connect to device: %s, please reboot your phone by pressing POWER and VOLUME_UP for 7-10 seconds.\n", o_exception);
		}
		else {
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
				}
				else {
					device_stage = BROM;
					DEG_LOG(OP, "Check baud BROM");
					if (!memcmp(io->raw_buf + 4, "SPRD4", 5) && no_fdl_mode) {
						fdl1_loaded = -1; 
						fdl2_executed = -1;
					}
				}
				DBG_LOG("[INFO] Device mode version: ");
				print_string(stdout, io->raw_buf + 4, READ16_BE(io->raw_buf + 2));
				print_to_string(mode_str,sizeof(mode_str),io->raw_buf + 4, READ16_BE(io->raw_buf + 2),0);
				
				encode_msg_nocpy(io, BSL_CMD_CONNECT, 0);
				if (send_and_check(io)) exit(1);
			}
			else if (ret == BSL_REP_VERIFY_ERROR) {
				encode_msg_nocpy(io, BSL_CMD_CONNECT, 0);
				if (fdl1_loaded != 1) {
					if (send_and_check(io)) exit(1);
				}
				else { i = -1; continue; }
			}
			if (fdl1_loaded == 1) {
				DEG_LOG(OP, "FDL1 connected."); 
				device_stage = FDL1;
				if (keep_charge) {
					encode_msg_nocpy(io, BSL_CMD_KEEP_CHARGE, 0);
					if (!send_and_check(io)) DEG_LOG(OP, "Keep charge FDL1.");
				}
				break;
			}
			else {
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
				if(isUseCptable){
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
			if (stage != -1) { ERR_EXIT("Failed to connect: %s, please reboot your phone by pressing POWER and VOLUME_UP for 7-10 seconds.\n", o_exception); }
			else { encode_msg_nocpy(io, BSL_CMD_CONNECT, 0); stage++; i = -1; }
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
	DEG_LOG(I,"SPRD3 Current : %d",found);
	if(!found && isKickMode) device_mode = SPRD4;
	else device_mode = SPRD3;
	if(!found && isKickMode) device_mode = SPRD4;
	else device_mode = SPRD3;
	if (fdl2_executed > 0) {
		if (device_mode == SPRD3) {
			DEG_LOG(I, "Device stage: FDL2/SPRD3");
		}
		else DEG_LOG(I, "Device stage: FDL2/SPRD4(AutoD)");
	}
	else if(fdl1_loaded > 0) {
		if (device_mode == SPRD3) {
			DEG_LOG(I, "Device stage: FDL1/SPRD3");
		}
		else DEG_LOG(I, "Device stage: FDL1/SPRD4(AutoD)");
	}
	else if (device_stage == BROM) {
		if (device_mode == SPRD3) {
			DEG_LOG(I, "Device stage: BROM/SPRD3");
		}
		else DEG_LOG(I, "Device stage: BROM/SPRD4(AutoD)");
	}
	else { 
		if(device_mode == SPRD3) DEG_LOG(I, "Device stage: Unknown/SPRD3");
		else DEG_LOG(I, "Device stage: Unknown/SPRD4(AutoD)");
	}
	showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), "Successfully connected 连接成功", "Device already connected!\n设备已成功连接！");
    if (!fdl2_executed) helper.enableWidget("fdl_exec");
    helper.setLabelText(helper.getWidget("con"), "Connected");
    if (device_stage == BROM) helper.setLabelText(helper.getWidget("mode"), "BROM");
    else if (device_stage == FDL1) helper.setLabelText(helper.getWidget("mode"), "FDL1");
    else if (device_stage == FDL2) helper.setLabelText(helper.getWidget("mode"), "FDL2");
}

// select fdl
void on_button_clicked_select_fdl(GtkWidgetHelper helper){
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
    if (fdl1_loaded > 0){
        DEG_LOG(I, "Executing FDL file: %s at address: 0x%X", fdl_path, fdl_addr);
        std::string dtxt = helper.getLabelText(helper.getWidget("con"));
        helper.setLabelText(helper.getWidget("con"), dtxt + " -> FDL Executing");
            //Send fdl2
        if (device_mode == SPRD3){
            FILE *fi = fopen(fdl_path, "r");
            if (fi == nullptr) { DEG_LOG(W,"File does not exist."); return;}
            else fclose(fi);
            if(!isKickMode) send_file(io, fdl_path, fdl_addr, end_data, blk_size ? blk_size : 528, 0, 0);
            else send_file(io, fdl_path, fdl_addr, 0, 528, 0, 0);
        }else{
            if (fdl_path && strlen(fdl_path) > 0 && !fdl_addr && isKickMode) {
                bool i_is = showConfirmDialog(GTK_WINDOW(helper.getWidget("main_window")), "Confirm 确认", "Device can be booted without FDL in SPRD4 mode, continue?\n设备在SPRD4模式下可以无需FDL启动，是否继续？");
                if (i_is) {
                    DEG_LOG(I, "Skipping FDL send in SPRD4 mode.");
                    encode_msg_nocpy(io, BSL_CMD_EXEC_DATA, 0);
                    if (send_and_check(io)) exit(1);
                    return;
                }
                else{
                    FILE *fi = fopen(fdl_path, "r");
                    if (fi == nullptr) { DEG_LOG(W,"File does not exist."); return;}
                    else fclose(fi);
                    send_file(io, fdl_path, fdl_addr, 0, 528, 0, 0);
                }
            }
        }
        
            memset(&Da_Info, 0, sizeof(Da_Info));
            encode_msg_nocpy(io, BSL_CMD_EXEC_DATA, 0);
            send_msg(io);
            // Feature phones respond immediately,
            // but it may take a second for a smartphone to respond.
            ret = recv_msg_timeout(io, 15000);
            if (!ret) { ERR_EXIT("timeout reached\n"); }
            ret = recv_type(io);
            // Is it always bullshit?
            if (ret == BSL_REP_INCOMPATIBLE_PARTITION)
                get_Da_Info(io);
            else if (ret != BSL_REP_ACK) {
                //ThrowExit();
                const char* name = get_bsl_enum_name(ret);
                ERR_EXIT("%s: excepted response (%s : 0x%04x)\n",name, o_exception, ret);
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
            g_spl_size = check_partition(io,"splloader",1);
            io->verbose = o;
            if (Da_Info.bSupportRawData) {
                blk_size = 0xf800;
                io->ptable = partition_list(io, fn_partlist, &io->part_count);
                if (fdl2_executed) {
                    Da_Info.bSupportRawData = 0;
                    DEG_LOG(OP, "Raw data mode disabled for SPRD4.");
                }
                else {
                    encode_msg_nocpy(io, BSL_CMD_ENABLE_RAW_DATA, 0);
                    if (!send_and_check(io)) DEG_LOG(OP, "Raw data mode enabled.");
                }
            }			
            else if (highspeed || Da_Info.dwStorageType == 0x103) {
                blk_size = 0xf800;
                io->ptable = partition_list(io, fn_partlist, &io->part_count);
            }
            else if (Da_Info.dwStorageType == 0x102) {
                io->ptable = partition_list(io, fn_partlist, &io->part_count);
            }
            else if (Da_Info.dwStorageType == 0x101) DEG_LOG(I, "Device storage is nand.");
            if (gpt_failed != 1) {
                if (selected_ab == 2) DEG_LOG(I, "Device is using slot b\n");
                else if (selected_ab == 1) DEG_LOG(I, "Device is using slot a\n");
                else {
                    DEG_LOG(I, "Device is not using VAB\n");
                    if (Da_Info.bSupportRawData) {
                        DEG_LOG(I, "Raw data mode is supported (level is %u) ,but DISABLED for stability, you can set it manually.", (unsigned)Da_Info.bSupportRawData);
                        Da_Info.bSupportRawData = 0;
                    }
                }
            }
            if(io->part_count){
                std::vector<partition_t> partitions;
                partitions.reserve(io->part_count);
                for (int i = 0; i < io->part_count; i++) {
                    partitions.push_back(io->ptable[i]);
                }
                populatePartitionList(helper, partitions);
            }
            else if(isUseCptable) {
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
            showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), "FDL2 Executed FDL2执行成功", "FDL2 executed successfully!\nFDL2已成功执行！"); 
            EnableWidgets(helper);
            helper.disableWidget("fdl_exec");
            helper.setLabelText(helper.getWidget("mode"), "FDL2");
            helper.setLabelText(helper.getWidget("con"), "Connected");
    }
    else {
        DEG_LOG(I, "Executing FDL file: %s at address: 0x%X", fdl_path, fdl_addr);
        std::string dtxt = helper.getLabelText(helper.getWidget("con"));
        helper.setLabelText(helper.getWidget("con"), dtxt + " -> FDL Executing");
        std::thread([helper, fdl_path, fdl_addr,execfile]() mutable {
            FILE* fi = fopen(fdl_path, "r");
            GtkWidget* cveSwitch = helper.getWidget("exec_addr");
            GtkWidget* cveAddr = helper.getWidget("cve_addr");
            GtkWidget* cveAddrC = helper.getWidget("cve_addr_c");
            bool isCve = helper.getSwitchState(cveSwitch);
            const char* cve_path = helper.getEntryText(cveAddr);
            const char* cve_addr = helper.getEntryText(cveAddrC);
            
            if (device_mode == SPRD3){
                if (fi == nullptr) { DEG_LOG(W,"File does not exist.\n"); return; }
                else fclose(fi);
                send_file(io, fdl_path, fdl_addr, end_data, 528, 0, 0);
                if (cve_addr && strlen(cve_addr) > 0 && isCve) {
                    DEG_LOG(I,"Using CVE binary: %s at address: %s", cve_path, cve_addr);
                    uint32_t cve_addr_val = strtoul(cve_addr, nullptr, 0);
                    send_file(io, cve_path, cve_addr_val, 0, 528, 0, 0);
                    delete[](execfile);
                }
                else {
                    encode_msg_nocpy(io, BSL_CMD_EXEC_DATA, 0);
                    if (send_and_check(io)) exit(1);
                }
            }
            else{
                if (fdl_path && strlen(fdl_path) > 0 && !fdl_addr && isKickMode) {
                    bool i_is = showConfirmDialog(GTK_WINDOW(helper.getWidget("main_window")), "Confirm 确认", "Device can be booted without FDL in SPRD4 mode, continue?\n设备在SPRD4模式下可以无需FDL启动，是否继续？");
                    if (i_is) {
                        DEG_LOG(I, "Skipping FDL send in SPRD4 mode.");
                        fclose(fi);
                        encode_msg_nocpy(io, BSL_CMD_EXEC_DATA, 0);
                        if (send_and_check(io)) exit(1);
                        delete[](execfile);
                        return;
                    }
                    else{
                        if (fi == nullptr) { DEG_LOG(W,"File does not exist.\n"); return; }
                        else fclose(fi);
                        send_file(io, fdl_path, fdl_addr, end_data, 528, 0, 0);
                        if (cve_addr && strlen(cve_addr) > 0 && isCve) {
                            DEG_LOG(I,"Using CVE binary: %s at address: %s", cve_path, cve_addr);
                            uint32_t cve_addr_val = strtoul(cve_addr, nullptr, 0);
                            send_file(io, cve_path, cve_addr_val, 0, 528, 0, 0);
                            delete[](execfile);
                        }
                        else {
                            encode_msg_nocpy(io, BSL_CMD_EXEC_DATA, 0);
                            if (send_and_check(io)) exit(1);
                        }
                    }
                }
            }
            DEG_LOG(OP,"Execute FDL1");
			// Tiger 310(0x5500) and Tiger 616(0x65000800) need to change baudrate after FDL1
	
			if (fdl_addr == 0x5500 || fdl_addr == 0x65000800) {
				highspeed = 1;
				if (!baudrate) baudrate = 921600;
                showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), "High Speed Mode Enabled 高速模式已启用", "Do not set block size manually in high speed mode!\n高速模式下请勿手动设置块大小！");
			}

			/* FDL1 (chk = sum) */
			io->flags &= ~FLAGS_CRC16;

			encode_msg(io, BSL_CMD_CHECK_BAUD, nullptr, 1);
			for (int i = 0; ; i++) {
				send_msg(io);
				recv_msg(io);
				if (recv_type(io) == BSL_REP_VER) break;
				DEG_LOG(W,"Failed to check baud, retry...");
				if (i == 4) { o_exception = "Failed to check baud FDL1"; ERR_EXIT("Can not execute FDL: %s,please reboot your phone by pressing POWER and VOL_UP for 7-10 seconds.\n",o_exception); }
				usleep(500000);
			}
			DEG_LOG(I,"Check baud FDL1 done.");

			DEG_LOG(I,"Device REP_Version: ");
			print_string(stderr, io->raw_buf + 4, READ16_BE(io->raw_buf + 2));
            encode_msg_nocpy(io, BSL_CMD_CONNECT, 0);
			if (send_and_check(io)) exit(1);
			DEG_LOG(I,"FDL1 connected.");
#if !USE_LIBUSB
			if (baudrate) {
				uint8_t* data = io->temp_buf;
				WRITE32_BE(data, baudrate);
				encode_msg_nocpy(io, BSL_CMD_CHANGE_BAUD, 4);
				if (!send_and_check(io)) {
					DEG_LOG(OP,"Change baud FDL1 to %d", baudrate);
					call_SetProperty(io->handle, 0, 100, (LPCVOID)&baudrate);
				}
			}
#endif
            if (keep_charge) {
					encode_msg_nocpy(io, BSL_CMD_KEEP_CHARGE, 0);
					if (!send_and_check(io)) DEG_LOG(OP,"Keep charge FDL1.");
				}
			fdl1_loaded = 1;
            showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), "FDL1 Executed FDL1执行成功", "FDL1 executed successfully!\nFDL1已成功执行！");
            helper.setLabelText(helper.getWidget("mode"), "FDL1");
            helper.setLabelText(helper.getWidget("con"), "Connected");
        }).detach();
        
    }
    
}
//disable widget when init
void DisableWidgets(GtkWidgetHelper helper){
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
}

int gtk_kmain(int argc, char** argv) {
    DEG_LOG(I, "Starting GUI mode...");
    gtk_init(&argc, &argv);

    // Initialization previously at file scope
    char* execfile = NEWN char[ARGV_LEN];
    if (!execfile) { ERR_EXIT("malloc failed\n"); }
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
    // 创建Notebook（标签页控件）
    GtkWidget* notebook = helper.createNotebook("main_notebook", 0, 0, 1174, 672);
{
    // ========== Connect Page ==========

    GtkWidget* connectPage = helper.createGrid("connect_page", 5, 5);
    helper.addNotebookPage(notebook, connectPage, "Connect  连接");
    
    // Welcome labels
    GtkWidget* welcomeLabel1 = helper.createLabel("Welcome to SFD Tool GUI!", "welcome_en", 0, 0, 467, 28);
    GtkWidget* welcomeLabel2 = helper.createLabel("欢迎使用SFD Tool GUI!", "welcome_cn", 0, 30, 400, 28);
    GtkWidget* ti_c = helper.createLabel("请将你的设备连接到BROM模式", "ti_c", 0, 60, 400, 28);
    GtkWidget* ti_e = helper.createLabel("Please connect your device with BROM mode", "ti_e", 0, 90, 500, 28);
    
    // 设置字体大小
    PangoAttrList* attr_list = pango_attr_list_new();
    PangoAttribute* attr = pango_attr_size_new(20 * PANGO_SCALE);
    pango_attr_list_insert(attr_list, attr);
    gtk_label_set_attributes(GTK_LABEL(welcomeLabel1), attr_list);
    gtk_label_set_attributes(GTK_LABEL(welcomeLabel2), attr_list);
    gtk_label_set_attributes(GTK_LABEL(ti_c), attr_list);
    gtk_label_set_attributes(GTK_LABEL(ti_e), attr_list);
    
    // 连接说明
    GtkWidget* instruction1 = helper.createLabel("Press and hold the volume up or down keys and the power key to connect", 
                                                "instruction1", 0, 120, 600, 20);
    GtkWidget* instruction2 = helper.createLabel("按住音量增大或减小键和电源键进行连接", 
                                                "instruction2", 0, 140, 400, 20);
    
    // FDL Settings section
    GtkWidget* fdlSettings = helper.createLabel("FDL Send Settings", "fdl_settings", 0, 170, 150, 20);
    GtkWidget* fdlSettingsCn = helper.createLabel("FDL发送设置", "fdl_settings_cn", 0, 190, 150, 20);
    
    // FDL File Path
    GtkWidget* fdlLabel = helper.createLabel("FDL File Path  FDL文件路径 :", "fdl_label", 0, 220, 200, 20);
    GtkWidget* fdlFilePath = helper.createEntry("fdl_file_path", "", false, 200, 215, 275, 32);
    GtkWidget* selectFdlBtn = helper.createButton("...", "select_fdl", nullptr, 485, 215, 40, 32);
    
    // FDL Address
    GtkWidget* fdlAddrLabel = helper.createLabel("FDL Send Address  FDL发送地址 ：", "fdl_addr_label", 0, 260, 220, 20);
    GtkWidget* fdlAddr = helper.createEntry("fdl_addr", "", false, 220, 255, 185, 32);
    
    // Execute button
    GtkWidget* fdlExecBtn = helper.createButton("Execute   执行", "fdl_exec", nullptr, 0, 300, 157, 32);
    
    // Advanced Options - 放在左边
    GtkWidget* advLabel = helper.createLabel("Advanced   高级选项", "adv_label", 0, 350, 150, 20);
    
    // CVE Toggle Switch - 放在左边
    GtkWidget* cveSwitchBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* cveSwitch = gtk_switch_new();
    gtk_widget_set_name(cveSwitch, "exec_addr");
	helper.addWidget("exec_addr",cveSwitch);
    gtk_box_pack_start(GTK_BOX(cveSwitchBox), cveSwitch, FALSE, FALSE, 0);
    GtkWidget* cveSwitchLabel = helper.createLabel("Try to use CVE to skip FDL verification(brom stage only)   利用漏洞绕过FDL签名验证(仅BROM模式)", 
                                                   "exec_addr_label", 0, 0, 500, 20);
    gtk_box_pack_start(GTK_BOX(cveSwitchBox), cveSwitchLabel, FALSE, FALSE, 0);
    
    // CVE Binary File - 放在左边，标签在右边，输入框在左边
    GtkWidget* cveAddrBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* cveAddr = helper.createEntry("cve_addr", "", false, 0, 0, 295, 32);
    GtkWidget* cveLabel = helper.createLabel("CVE Binary File Address CVE可执行镜像", "cve_label", 0, 0, 270, 20);
    gtk_box_pack_start(GTK_BOX(cveAddrBox), cveAddr, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(cveAddrBox), cveLabel, FALSE, FALSE, 0);
    GtkWidget* selectCveBtn = helper.createButton("...", "select_cve", nullptr, 0, 0, 40, 32);
    gtk_box_pack_start(GTK_BOX(cveAddrBox), selectCveBtn, FALSE, FALSE, 0);
    
    // SPRD4 Toggle Switch - 放在右边
    GtkWidget* sprd4SwitchBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* sprd4Switch = gtk_switch_new();
    gtk_widget_set_name(sprd4Switch, "sprd4");
	helper.addWidget("sprd4",sprd4Switch);
    gtk_box_pack_start(GTK_BOX(sprd4SwitchBox), sprd4Switch, FALSE, FALSE, 0);
    GtkWidget* sprd4Label = helper.createLabel("Kick device to SPRD4  使用SPRD4模式", 
                                               "sprd4_label", 0, 0, 250, 20);
    gtk_box_pack_start(GTK_BOX(sprd4SwitchBox), sprd4Label, FALSE, FALSE, 0);
    
    // Addr 地址 - 放在右边，在SPRD4开关下面
    GtkWidget* addrBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* cveAddrLabel2 = helper.createLabel("Addr  地址", "cve_addr_label2", 0, 0, 70, 20);
    GtkWidget* cveAddrC = helper.createEntry("cve_addr_c", "", false, 0, 0, 120, 32);
    gtk_box_pack_start(GTK_BOX(addrBox), cveAddrLabel2, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(addrBox), cveAddrC, FALSE, FALSE, 0);
    
    // Connect Button - 放在右边
    GtkWidget* connectBtn = helper.createButton("CONNECT  连接", "connect_1", nullptr, 0, 0, 143, 52);
    
    // Wait connection time - 放在右边
    GtkWidget* waitBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* waitLabel = helper.createLabel("Wait connection time  连接等待时间 (s):", "wait_label", 0, 0, 250, 20);
    GtkWidget* waitCon = helper.createSpinButton(1, 65535, 1, "wait_con", 30, 0, 0, 120, 32);
    gtk_box_pack_start(GTK_BOX(waitBox), waitLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(waitBox), waitCon, FALSE, FALSE, 0);
    
    // Status labels - 放在底部
    GtkWidget* statusLabel = helper.createLabel("Status : ", "status_label", 0, 0, 70, 24);
    GtkWidget* conStatus = helper.createLabel("Not connected", "con", 0, 0, 150, 23);
    GtkWidget* modeLabel = helper.createLabel("  Mode : ", "mode_label", 0, 0, 50, 19);
    GtkWidget* modeStatus = helper.createLabel("BROM Not connected!!!", "mode", 0, 0, 200, 19);
    
    // Add all widgets to connect page grid
    // 使用4列网格：0-3列
    // 左边区域：0-2列，右边区域：3列
    
    // 欢迎信息（横跨所有列）
    helper.addToGrid(connectPage, welcomeLabel1, 0, 0, 4, 1);
    helper.addToGrid(connectPage, welcomeLabel2, 0, 1, 4, 1);
    helper.addToGrid(connectPage, ti_c, 0, 2, 4, 1);
    helper.addToGrid(connectPage, ti_e, 0, 3, 4, 1);
    
    // 连接说明
    helper.addToGrid(connectPage, instruction1, 0, 4, 4, 1);
    helper.addToGrid(connectPage, instruction2, 0, 5, 4, 1);
    
    // FDL设置部分
    helper.addToGrid(connectPage, fdlSettings, 0, 6, 2, 1);
    helper.addToGrid(connectPage, fdlSettingsCn, 0, 7, 2, 1);
    
    // FDL文件路径
    helper.addToGrid(connectPage, fdlLabel, 0, 8, 1, 1);
    helper.addToGrid(connectPage, fdlFilePath, 1, 8, 1, 1);
    helper.addToGrid(connectPage, selectFdlBtn, 2, 8, 1, 1);
    
    // FDL地址
    helper.addToGrid(connectPage, fdlAddrLabel, 0, 9, 1, 1);
    helper.addToGrid(connectPage, fdlAddr, 1, 9, 2, 1);
    
    // 执行按钮
    helper.addToGrid(connectPage, fdlExecBtn, 0, 10, 2, 1);
    
    // 高级选项标题
    helper.addToGrid(connectPage, advLabel, 0, 11, 2, 1);
    
    // CVE开关
    helper.addToGrid(connectPage, cveSwitchBox, 0, 12, 3, 1);
    
    // CVE文件地址 - 注意：输入框在标签左边
    helper.addToGrid(connectPage, cveAddrBox, 0, 13, 3, 1);
    
    // ========== 右边区域 ==========
    // SPRD4开关
    helper.addToGrid(connectPage, sprd4SwitchBox, 3, 12, 1, 1);
    
    // Addr 地址标签和输入框
    helper.addToGrid(connectPage, addrBox, 3, 13, 1, 1);
    
    // 连接按钮
    helper.addToGrid(connectPage, connectBtn, 3, 14, 1, 1);
    
    // 等待时间
    helper.addToGrid(connectPage, waitBox, 3, 15, 1, 1);
    
    // ========== 底部状态信息 ==========
    // 状态行
    GtkWidget* statusBox1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(statusBox1), statusLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(statusBox1), conStatus, FALSE, FALSE, 0);
    helper.addToGrid(connectPage, statusBox1, 0, 16, 2, 1);
    
    // 模式行
    GtkWidget* statusBox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(statusBox2), modeLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(statusBox2), modeStatus, FALSE, FALSE, 0);
    helper.addToGrid(connectPage, statusBox2, 0, 17, 2, 1);
    
    // ========== Partition Operation Page ==========

    GtkWidget* partPage = helper.createGrid("part_page", 5, 5);
    helper.addNotebookPage(notebook, partPage, "Partition Operation  分区操作");
    
    GtkWidget* instruction = helper.createLabel("Please check a partition        请选择一个分区", "part_instruction", 0, 0, 300, 20);
    
    // ListView for partitions
    GtkWidget* scrolledWindow = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scrolledWindow, 1000, 450);
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
    GtkWidget* opLabel = helper.createLabel("Operation    操作：", "op_label", 0, 0, 150, 20);
    GtkWidget* writeBtn = helper.createButton("WRITE  刷写", "list_write", nullptr, 0, 0, 117, 32);
    GtkWidget* writeFBtn = helper.createButton("FORCE WRITE 强制刷写", "list_force_write", nullptr, 0, 0, 162, 32);
    GtkWidget* readBtn = helper.createButton("EXTRACT  读取分区", "list_read", nullptr, 0, 0, 162, 32);
    GtkWidget* eraseBtn = helper.createButton("ERASE  擦除分区", "list_erase", nullptr, 0, 0, 170, 32);
    GtkWidget* backupAllBtn = helper.createButton("Backup All  备份分区", "backup_all", nullptr, 0, 0, 180, 32);
    GtkWidget* cancelBtn = helper.createButton("Cancel  取消", "list_cancel", nullptr, 0, 0, 117, 32);
    
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
    // 添加占位空间使Cancel按钮对齐到刷写按钮下方
    GtkWidget* placeholder1 = gtk_label_new("");
    GtkWidget* placeholder2 = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(cancelButtonBox), placeholder1, FALSE, FALSE, 117);
    gtk_box_pack_start(GTK_BOX(cancelButtonBox), placeholder2, FALSE, FALSE, 0);
    helper.addToGrid(partPage, cancelButtonBox, 0, 11, 5, 1);
    

    

    
    
    // ========== Manually Operate Page ==========

    GtkWidget* manualPage = helper.createGrid("manual_page", 5, 5);
    helper.addNotebookPage(notebook, manualPage, "Manually Operate  手动操作");
    
    // Write partition section
    GtkWidget* writeLabel = helper.createLabel("Write partition   刷写分区", "write_label", 0, 0, 200, 20);
    GtkWidget* writePartLabel = helper.createLabel("Partition name  分区名：", "write_part_label", 0, 0, 150, 20);
    GtkWidget* mPartFlash = helper.createEntry("m_part_flash", "", false, 0, 0, 155, 32);
    
    GtkWidget* filePathLabel = helper.createLabel("Image file path  镜像文件地址：", "file_path_label", 0, 0, 200, 20);
    GtkWidget* mFilePath = helper.createEntry("m_file_path", "", false, 0, 0, 245, 32);
    GtkWidget* mSelectBtn = helper.createButton("...", "m_select", nullptr, 0, 0, 40, 32);
    
    GtkWidget* mWriteBtn = helper.createButton("WRITE   刷写", "m_write", nullptr, 0, 0, 120, 32);
    
    // Separator
    GtkWidget* sep1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    
    // Extract partition section
    GtkWidget* extractLabel = helper.createLabel("Extract partition  读取分区", "extract_label", 0, 0, 200, 20);
    GtkWidget* extractPartLabel = helper.createLabel("Partition name  分区名：", "extract_part_label", 0, 0, 150, 20);
    GtkWidget* mPartRead = helper.createEntry("m_part_read", "", false, 0, 0, 145, 32);
    
    GtkWidget* mReadBtn = helper.createButton("EXTRACT  读取", "m_read", nullptr, 0, 0, 120, 32);
    
    // Separator
    GtkWidget* sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    
    // Erase partition section
    GtkWidget* eraseLabel = helper.createLabel("Erase partition  擦除分区", "erase_label", 0, 0, 200, 20);
    GtkWidget* erasePartLabel = helper.createLabel("Partition name  分区名：", "erase_part_label", 0, 0, 150, 20);
    GtkWidget* mPartErase = helper.createEntry("m_part_erase", "", false, 0, 0, 150, 32);
    
    GtkWidget* mEraseBtn = helper.createButton("ERASE  擦除", "m_erase", nullptr, 0, 0, 120, 32);
    
    // Cancel button - 在Erase按钮下方两行处
    GtkWidget* mCancelBtn = helper.createButton("Cancel  取消", "m_cancel", nullptr, 0, 0, 120, 32);
    
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
        helper.addNotebookPage(notebook, advOpPage, "Advanced Operation  高级操作");
        
        // A/B partition
        GtkWidget* abLabel = helper.createLabel("Toggle the A/B partition boot settings  切换A/B分区启动设置", "ab_label", 0, 0, 400, 20);
        GtkWidget* setActiveA = helper.createButton("Boot A partitons  启动A分区", "set_active_a", nullptr, 0, 0, 200, 32);
        GtkWidget* setActiveB = helper.createButton("Boot B partitions  启动B分区", "set_active_b", nullptr, 0, 0, 200, 32);
        
        // Repartition
        GtkWidget* repartLabel = helper.createLabel("Repartition  重新分区", "repart_label", 0, 0, 200, 20);
        GtkWidget* xmlLabel = helper.createLabel("XML part info file path  XML分区表文件路径", "xml_label", 0, 0, 300, 20);
        GtkWidget* xmlPath = helper.createEntry("xml_path", "", false, 0, 0, 374, 32);
        GtkWidget* selectXmlBtn = helper.createButton("...", "select_xml", nullptr, 0, 0, 40, 32);
        GtkWidget* startRepartBtn = helper.createButton("START  开始", "start_repart", nullptr, 0, 0, 120, 32);
        
        GtkWidget* readXmlBtn = helper.createButton("Extract part info to a XML file (if support)  备份分区表到XML文件（如果支持）", 
                                                   "read_xml", nullptr, 0, 0, 500, 32);
        
        // DM-verify
        GtkWidget* dmvLabel = helper.createLabel("DM-verify Settings (if support)  DM-verify设置（如果支持）", "dmv_label", 0, 0, 400, 20);
        GtkWidget* dmvDisable = helper.createButton("Disable DM-verify  禁用DM-verify", "dmv_disable", nullptr, 0, 0, 200, 32);
        GtkWidget* dmvEnable = helper.createButton("Enable DM-verify  启用DM-verify", "dmv_enable", nullptr, 0, 0, 200, 32);
        
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
    
    
    // ========== Advanced Settings Page ==========
    
        GtkWidget* advSetPage = helper.createGrid("adv_set_page", 5, 5);
        helper.addNotebookPage(notebook, advSetPage, "Advanced Settings  高级设置");
        
        GtkWidget* blkLabel = helper.createLabel("Data block size  数据块大小", "blk_label", 0, 0, 200, 20);
        
        GtkWidget* blkSlider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 10000, 60000, 10000);
        gtk_range_set_value(GTK_RANGE(blkSlider), 10000);
        gtk_scale_set_draw_value(GTK_SCALE(blkSlider), TRUE);
        gtk_scale_set_value_pos(GTK_SCALE(blkSlider), GTK_POS_RIGHT);
        gtk_widget_set_name(blkSlider, "blk_size");
		helper.addWidget("blk_size",blkSlider);
        gtk_widget_set_size_request(blkSlider, 1036, 30);
        
        GtkWidget* sizeCon = helper.createLabel("10000", "size_con", 0, 0, 60, 20);
        
        // Add to grid
        helper.addToGrid(advSetPage, blkLabel, 0, 0, 2, 1);
        
        GtkWidget* sliderBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_box_pack_start(GTK_BOX(sliderBox), blkSlider, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(sliderBox), sizeCon, FALSE, FALSE, 0);
        helper.addToGrid(advSetPage, sliderBox, 0, 1, 2, 1);
    
    
    // ========== About Page ==========
    
        GtkWidget* aboutPage = helper.createGrid("about_page", 5, 5);
        helper.addNotebookPage(notebook, aboutPage, "About  关于");
        
        GtkWidget* scrolledAbout = gtk_scrolled_window_new(NULL, NULL);
        gtk_widget_set_size_request(scrolledAbout, 1084, 557);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledAbout),
                                      GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        
        GtkWidget* aboutTextView = gtk_text_view_new();
        gtk_text_view_set_editable(GTK_TEXT_VIEW(aboutTextView), FALSE);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(aboutTextView), GTK_WRAP_WORD);
        gtk_widget_set_name(aboutTextView, "about_text");
        helper.addWidget("about_text",aboutTextView);
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(aboutTextView));
        gtk_text_buffer_set_text(buffer, AboutText, -1);
        
        gtk_container_add(GTK_CONTAINER(scrolledAbout), aboutTextView);
        helper.addToGrid(aboutPage, scrolledAbout, 0, 0, 1, 1);
    
    
    // ========== Log Page ==========
    
        GtkWidget* logPage = helper.createGrid("log_page", 5, 5);
        helper.addNotebookPage(notebook, logPage, "Log  日志");
        
        GtkWidget* scrolledLog = gtk_scrolled_window_new(NULL, NULL);
        gtk_widget_set_size_request(scrolledLog, 1124, 500);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledLog),
                                      GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        
        GtkWidget* logTextView = gtk_text_view_new();
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(logTextView), GTK_WRAP_WORD);
        gtk_widget_set_name(logTextView, "txtOutput");
        helper.addWidget("txtOutput",logTextView);
        gtk_container_add(GTK_CONTAINER(scrolledLog), logTextView);
        
        GtkWidget* expLogBtn = helper.createButton("Export  导出", "exp_log", nullptr, 0, 0, 120, 32);
        GtkWidget* logClearBtn = helper.createButton("Clear  清空", "log_clear", nullptr, 0, 0, 120, 32);
        
        // Add to grid
        helper.addToGrid(logPage, scrolledLog, 0, 0, 4, 8);
        
        GtkWidget* logButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 30);
        gtk_box_pack_start(GTK_BOX(logButtonBox), expLogBtn, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(logButtonBox), logClearBtn, FALSE, FALSE, 0);
        helper.addToGrid(logPage, logButtonBox, 0, 9, 4, 1);
    
    
    // ========== Bottom Controls ==========
    
        // Progress section
        GtkWidget* progressLabel = helper.createLabel("Progress  进度:", "progress_label", 0, 0, 100, 20);
        GtkWidget* progressBar = gtk_progress_bar_new();
        gtk_widget_set_name(progressBar, "progressBar_1");
		helper.addWidget("progressBar_1",progressBar);
        gtk_widget_set_size_request(progressBar, 345, 9);
        
        GtkWidget* percentLabel = helper.createLabel("0%", "percent", 0, 0, 30, 20);
        
        // Control buttons
        GtkWidget* poweroffBtn = helper.createButton("POWEROFF  关机", "poweroff", nullptr, 0, 0, 130, 32);
        GtkWidget* rebootBtn = helper.createButton("REBOOT  重启", "reboot", nullptr, 0, 0, 110, 32);
        GtkWidget* recoveryBtn = helper.createButton("BOOT TO RECOVERY  重启到恢复模式", "recovery", nullptr, 0, 0, 260, 32);
        GtkWidget* fastbootBtn = helper.createButton("BOOT TO FASTBOOT  重启到线刷模式", "fastboot", nullptr, 0, 0, 260, 32);
        
        // Create bottom grid
        GtkWidget* bottomGrid = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(bottomGrid), 5);
        gtk_grid_set_column_spacing(GTK_GRID(bottomGrid), 10);
        
        // Add to bottom grid
        gtk_grid_attach(GTK_GRID(bottomGrid), progressLabel, 0, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(bottomGrid), progressBar, 0, 1, 1, 1);
        
        gtk_grid_attach(GTK_GRID(bottomGrid), percentLabel, 1, 0, 1, 1);
        
        gtk_grid_attach(GTK_GRID(bottomGrid), poweroffBtn, 4, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(bottomGrid), rebootBtn, 5, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(bottomGrid), recoveryBtn, 6, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(bottomGrid), fastbootBtn, 7, 0, 1, 1);
        
        // Add notebook and bottom grid to main grid
        gtk_grid_attach(GTK_GRID(mainGrid), notebook, 0, 0, 10, 1);
        gtk_grid_attach(GTK_GRID(mainGrid), bottomGrid, 0, 1, 10, 1);
    
    
    // 创建CSS样式
    GtkCssProvider* provider = gtk_css_provider_new();
    const gchar* css = 
        "label.big-label { font-size: 20px; }"
        "progressbar { min-height: 9px; }";
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                             GTK_STYLE_PROVIDER(provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    
    gtk_container_add(GTK_CONTAINER(window), mainGrid);
    
    // 显示所有组件
    gtk_widget_show_all(window);
    // Bind signals
	
	helper.bindClick(connectBtn, [argc,argv]() {
		std::thread([argc,argv]() {
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
}
    DisableWidgets(helper);
    // 启动GTK主循环
    gtk_main();
    
    //Disable
    
    return 0;
}
int main(int argc, char** argv) {
    if (argc > 1 && !strcmp(argv[1], "--no-gui")) {
        // Call the console version of main
        return main_console(argc-1, argv+1); // Skip the first argument
    }
    else {
        return gtk_kmain(argc, argv);
    }
}