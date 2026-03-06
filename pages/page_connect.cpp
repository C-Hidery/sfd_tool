#include "page_connect.h"
#include "../common.h"
#include "../main.h"
#include "../i18n.h"
#include "../ui_common.h"
#include "../nlohmann/json.hpp"
#include "page_partition.h"
#include <thread>
#include <chrono>

extern spdio_t* io;
extern int ret;
extern int m_bOpened;
extern int blk_size;
extern int isCMethod;
extern int keep_charge;
extern int end_data;
extern int highspeed;
extern unsigned exec_addr, baudrate;
extern int no_fdl_mode;
extern int gpt_failed;
extern int selected_ab;
extern int nand_info[3];
extern int nand_id;
extern int conn_wait;
extern int fdl1_loaded;
extern int fdl2_executed;
extern int isKickMode;
extern int device_stage, device_mode;
extern bool isUseCptable;

using nlohmann::json;

// 前向声明 — 这些回调定义在本文件中
static void on_button_clicked_connect(GtkWidgetHelper helper, int argc, char** argv);
static void on_button_clicked_select_fdl(GtkWidgetHelper helper);
static void on_button_clicked_fdl_exec(GtkWidgetHelper helper, char* execfile);
static void on_button_clicked_select_cve(GtkWidgetHelper helper);

static void on_button_clicked_select_cve(GtkWidgetHelper helper) {
	GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
	std::string filename = showFileChooser(parent, true);
	if (!filename.empty()) {
		helper.setEntryText(helper.getWidget("cve_addr"), filename);
	}
}

static void on_button_clicked_select_fdl(GtkWidgetHelper helper) {
	GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
	std::string filename = showFileChooser(parent, true);
	if (!filename.empty()) {
		helper.setEntryText(helper.getWidget("fdl_file_path"), filename);
	}
}

// on_button_clicked_connect 和 on_button_clicked_fdl_exec 保留在 main.cpp 中
// （因为这两个函数过于复杂且与全局状态高度耦合，暂不做搬迁）
// 此文件只包含 Connect 页面的 UI 构建和简单回调

GtkWidget* create_connect_page(GtkWidgetHelper& helper, GtkWidget* notebook) {
	GtkWidget* connectPage = helper.createGrid("connect_page", 5, 5);
	helper.addNotebookPage(notebook, connectPage, _("Connect"));

	// 创建连接页的根垂直盒子
	GtkWidget* mainConnectBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
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
	
	GtkWidget* instruction = helper.createLabel(_("Press and hold the volume up or down keys and the power key to connect"),
	                          "instruction", 0, 0, 600, 20);

	gtk_box_pack_start(GTK_BOX(headerBox), welcomeLabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(headerBox), tiLabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(headerBox), instruction, FALSE, FALSE, 0);
	
	gtk_box_pack_start(GTK_BOX(mainConnectBox), headerBox, FALSE, FALSE, 10);

	// 2. FDL Settings section
	GtkWidget* fdlCenterBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_halign(fdlCenterBox, GTK_ALIGN_CENTER);

	GtkWidget* fdlFrame = gtk_frame_new(_("FDL Send Settings"));
	gtk_widget_set_size_request(fdlFrame, 600, -1);

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
	gtk_grid_attach(GTK_GRID(fdlGrid), fdlAddr, 1, 1, 2, 1);

	// Execute button
	GtkWidget* fdlExecBtn = helper.createButton(_("Execute"), "fdl_exec", nullptr, 0, 0, 150, 32);
	gtk_widget_set_hexpand(fdlExecBtn, TRUE);
	gtk_grid_attach(GTK_GRID(fdlGrid), fdlExecBtn, 0, 2, 3, 1);

	gtk_container_add(GTK_CONTAINER(fdlFrame), fdlGrid);
	gtk_box_pack_start(GTK_BOX(fdlCenterBox), fdlFrame, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(mainConnectBox), fdlCenterBox, FALSE, FALSE, 0);

	// 3. Advanced Options Containers
	GtkWidget* advContainer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
	gtk_box_set_homogeneous(GTK_BOX(advContainer), TRUE);

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

	// Connect Button
	GtkWidget* connectBtn = helper.createButton(_("CONNECT"), "connect_1", nullptr, 0, 0, 300, 48);
	
	// Wait connection time
	GtkWidget* waitBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_widget_set_halign(waitBox, GTK_ALIGN_CENTER);
	GtkWidget* waitLabel = helper.createLabel(_("Wait connection time (s):"), "wait_label", 0, 0, 150, 20);
	
	// 自定义 SpinBox
	GtkWidget* customSpinBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	GtkStyleContext* styleCtx = gtk_widget_get_style_context(customSpinBox);
	gtk_style_context_add_class(styleCtx, "linked");

	GtkWidget* waitCon = helper.createSpinButton(1, 65535, 1, "wait_con", 30, 0, 0, 60, 32);
	gtk_widget_set_name(waitCon, "wait_con_no_arrow");
	
	GtkWidget* btnMinus = gtk_button_new_with_label("-");
	GtkWidget* btnPlus = gtk_button_new_with_label("+");
	gtk_widget_set_size_request(btnMinus, 32, 32);
	gtk_widget_set_size_request(btnPlus, 32, 32);

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

	gtk_box_pack_start(GTK_BOX(mainConnectBox), actionBox, TRUE, FALSE, 15);

	// Status labels（这些在底部控制栏也需要用到）
	GtkWidget* statusLabel = helper.createLabel(_("Status : "), "status_label", 0, 0, 70, 24);
	GtkWidget* conStatus = helper.createLabel(_("Not connected"), "con", 0, 0, 150, 23);
	GtkWidget* modeLabel = helper.createLabel(_("   Mode : "), "mode_label", 0, 0, 50, 19);
	GtkWidget* modeStatus = helper.createLabel(_("BROM Not connected!!!"), "mode", 0, 0, 200, 19);

	// 把整合完毕的 mainConnectBox 添加到 connectPage
	gtk_grid_attach(GTK_GRID(connectPage), mainConnectBox, 0, 0, 5, 5);

	return connectPage;
}

void bind_connect_signals(GtkWidgetHelper& helper, int argc, char** argv) {
	helper.bindClick(helper.getWidget("connect_1"), [argc, argv]() {
		std::thread([argc, argv]() {
			on_button_clicked_connect(helper, argc, argv);
		}).detach();
	});
	helper.bindClick(helper.getWidget("select_fdl"), []() {
		on_button_clicked_select_fdl(helper);
	});
	helper.bindClick(helper.getWidget("select_cve"), []() {
		on_button_clicked_select_cve(helper);
	});
	// fdl_exec 信号绑定在 main.cpp 中处理，因为需要 execfile 参数
}
