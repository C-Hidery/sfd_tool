#include "page_debug.h"
#include "../common.h"
#include "../main.h"
#include "../i18n.h"
#include <thread>

extern spdio_t* io;
extern int ret;
extern int m_bOpened;

static void on_button_clicked_pac_time(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_(("Error")))), _("Device unattached, exiting..."));
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

static void on_button_clicked_chip_uid(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_(("Error")))), _("Device unattached, exiting..."));
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

static void on_button_clicked_check_nand(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_(("Error")))), _("Device unattached, exiting..."));
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
	}
	if (Da_Info.dwStorageType == 0x101) {
		DEG_LOG(I, "Device storage is nand");
		showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Info"), "Storage is nand.");
	} else {
		DEG_LOG(I, "Device storage is not nand");
		showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Info"), "Storage is not nand.");
	}
}

GtkWidget* create_debug_page(GtkWidgetHelper& helper, GtkWidget* notebook) {
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
	gtk_box_pack_start(GTK_BOX(row1Box), gtk_label_new(""), TRUE, TRUE, 0);

	// 添加第二个按钮行
	GtkWidget* row2Box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_box_pack_start(GTK_BOX(row2Box), gtk_label_new(""), TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(row2Box), chipuid, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(row2Box), gtk_label_new(""), TRUE, TRUE, 0);

	// 添加第三个按钮行
	GtkWidget* row3Box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_box_pack_start(GTK_BOX(row3Box), gtk_label_new(""), TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(row3Box), ReadNand, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(row3Box), gtk_label_new(""), TRUE, TRUE, 0);

	// 将各行添加到主盒子
	gtk_box_pack_start(GTK_BOX(mainBox), gtk_label_new(""), TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(mainBox), row1Box, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(mainBox), gtk_label_new(""), FALSE, FALSE, 20);
	gtk_box_pack_start(GTK_BOX(mainBox), row2Box, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(mainBox), gtk_label_new(""), FALSE, FALSE, 20);
	gtk_box_pack_start(GTK_BOX(mainBox), row3Box, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(mainBox), gtk_label_new(""), TRUE, TRUE, 0);

	// 添加到网格布局
	helper.addToGrid(dbgOptPage, mainBox, 0, 0, 4, 6);

	// 添加说明标签
	GtkWidget* infoLabel = gtk_label_new(_("Debug Options Page\nThis page contains device debugging functions"));
	gtk_label_set_justify(GTK_LABEL(infoLabel), GTK_JUSTIFY_CENTER);
	gtk_widget_set_margin_top(infoLabel, 50);
	gtk_widget_set_margin_bottom(infoLabel, 20);

	helper.addToGrid(dbgOptPage, infoLabel, 0, 6, 4, 1);

	return dbgOptPage;
}

void bind_debug_signals(GtkWidgetHelper& helper) {
	helper.bindClick(helper.getWidget("pac_time"), []() {
		on_button_clicked_pac_time(helper);
	});
	helper.bindClick(helper.getWidget("chip_uid"), []() {
		on_button_clicked_chip_uid(helper);
	});
	helper.bindClick(helper.getWidget("check_nand"), []() {
		on_button_clicked_check_nand(helper);
	});
}
