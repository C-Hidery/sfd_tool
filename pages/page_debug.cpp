#include "page_debug.h"
#include "../common.h"
#include "../main.h"
#include "../i18n.h"
#include <thread>

extern spdio_t*& io;
extern int ret;
extern int& m_bOpened;

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

	GtkWidget* dbgScroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(dbgScroll),
	                               GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_hexpand(dbgScroll, TRUE);
	gtk_widget_set_vexpand(dbgScroll, TRUE);

	GtkWidget* mainBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 32);
	gtk_widget_set_margin_start(mainBox, 40);
	gtk_widget_set_margin_end(mainBox, 40);
	gtk_widget_set_margin_top(mainBox, 40);
	gtk_widget_set_margin_bottom(mainBox, 40);
	gtk_widget_set_halign(mainBox, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(mainBox, GTK_ALIGN_CENTER);
	gtk_widget_set_size_request(mainBox, 600, -1);

	auto makeCardBox = [](int pad_h, int pad_v) -> GtkWidget* {
		GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
		gtk_widget_set_margin_start(box, pad_h);
		gtk_widget_set_margin_end(box, pad_h);
		gtk_widget_set_margin_top(box, pad_v);
		gtk_widget_set_margin_bottom(box, pad_v);
		return box;
	};

	// 1. 获取 Pactime
	GtkWidget* pactimeFrame = gtk_frame_new(NULL);
	GtkWidget* pactimeTitle = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(pactimeTitle), (std::string("<b>") + _("Pactime") + "</b>").c_str());
	gtk_widget_set_halign(pactimeTitle, GTK_ALIGN_CENTER);
	gtk_frame_set_label_widget(GTK_FRAME(pactimeFrame), pactimeTitle);
	gtk_frame_set_label_align(GTK_FRAME(pactimeFrame), 0.5, 0.5);
	helper.addWidget("pactime_label", pactimeTitle);

	GtkWidget* pactimeBox = makeCardBox(32, 16);
	gtk_container_add(GTK_CONTAINER(pactimeFrame), pactimeBox);

	GtkWidget* pactime = helper.createButton(_("Get pactime"), "pac_time", nullptr, 0, 0, 560, 36);
	gtk_widget_set_halign(pactime, GTK_ALIGN_CENTER);

	gtk_box_pack_start(GTK_BOX(pactimeBox), pactime, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(mainBox), pactimeFrame, FALSE, FALSE, 0);

	// 2. 获取芯片 UID
	GtkWidget* uidFrame = gtk_frame_new(NULL);
	GtkWidget* uidTitle = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(uidTitle), (std::string("<b>") + _("Chip UID") + "</b>").c_str());
	gtk_widget_set_halign(uidTitle, GTK_ALIGN_CENTER);
	gtk_frame_set_label_widget(GTK_FRAME(uidFrame), uidTitle);
	gtk_frame_set_label_align(GTK_FRAME(uidFrame), 0.5, 0.5);
	helper.addWidget("uid_label", uidTitle);

	GtkWidget* uidBox = makeCardBox(32, 16);
	gtk_container_add(GTK_CONTAINER(uidFrame), uidBox);

	GtkWidget* chipuid = helper.createButton(_("Get chip UID"), "chip_uid", nullptr, 0, 0, 560, 36);
	gtk_widget_set_halign(chipuid, GTK_ALIGN_CENTER);

	gtk_box_pack_start(GTK_BOX(uidBox), chipuid, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(mainBox), uidFrame, FALSE, FALSE, 0);

	// 3. NAND 检测
	GtkWidget* nandFrame = gtk_frame_new(NULL);
	GtkWidget* nandTitle = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(nandTitle), (std::string("<b>") + _("Storage Check") + "</b>").c_str());
	gtk_widget_set_halign(nandTitle, GTK_ALIGN_CENTER);
	gtk_frame_set_label_widget(GTK_FRAME(nandFrame), nandTitle);
	gtk_frame_set_label_align(GTK_FRAME(nandFrame), 0.5, 0.5);
	helper.addWidget("nand_label", nandTitle);

	GtkWidget* nandBox = makeCardBox(32, 16);
	gtk_container_add(GTK_CONTAINER(nandFrame), nandBox);

	GtkWidget* ReadNand = helper.createButton(_("Check if NAND Storage"), "check_nand", nullptr, 0, 0, 560, 36);
	gtk_widget_set_halign(ReadNand, GTK_ALIGN_CENTER);

	gtk_box_pack_start(GTK_BOX(nandBox), ReadNand, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(mainBox), nandFrame, FALSE, FALSE, 0);

	// 添加说明文本
	GtkWidget* infoLabel = gtk_label_new(_("Debug Options Page\nThis page contains device debugging functions"));
	gtk_label_set_justify(GTK_LABEL(infoLabel), GTK_JUSTIFY_CENTER);
	gtk_widget_set_margin_top(infoLabel, 20);
	gtk_box_pack_start(GTK_BOX(mainBox), infoLabel, FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(dbgScroll), mainBox);
	helper.addToGrid(dbgOptPage, dbgScroll, 0, 0, 5, 5);

	return dbgOptPage;
}

void bind_debug_signals(GtkWidgetHelper& helper) {
	helper.bindClick(helper.getWidget("pac_time"), [&]() {
		on_button_clicked_pac_time(helper);
	});
	helper.bindClick(helper.getWidget("chip_uid"), [&]() {
		on_button_clicked_chip_uid(helper);
	});
	helper.bindClick(helper.getWidget("check_nand"), [&]() {
		on_button_clicked_check_nand(helper);
	});
}
