#include "ui_common.h"
#include "common.h"
#include "main.h"
#include "i18n.h"

extern spdio_t*& io;
extern AppState g_app_state;
extern int& m_bOpened;
// 兼容旧逻辑：isCMethod 映射到 AppState::flash.isCMethod
static int& isCMethod = g_app_state.flash.isCMethod;

// 前向声明回调函数（来自其他页面模块）
void on_button_clicked_poweroff(GtkWidgetHelper helper);
void on_button_clicked_reboot(GtkWidgetHelper helper);
void on_button_clicked_recovery(GtkWidgetHelper helper);
void on_button_clicked_fastboot(GtkWidgetHelper helper);

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
	helper.enableWidget("pac_flash_start");
}

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
	helper.disableWidget("pac_flash_start");
}

void ensure_device_attached_or_exit(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_(("Error")))), _("Device unattached, exiting..."));
			exit(1);
		},GTK_WINDOW(helper.getWidget("main_window")));
	}
}

bool ensure_device_attached_or_warn(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Error"))), _("Device unattached, exiting..."));
		},GTK_WINDOW(helper.getWidget("main_window")));
		return true;
	}
	return false;
}

void append_log_to_ui(int type, const char* message) {
	(void)type; // 目前仅用于保持接口一致，后续可根据等级做样式区分
	if (!isHelperInit || !message) return;

	GtkWidget* txtOutput = helper.getWidget("txtOutput");
	if (!txtOutput || !GTK_IS_TEXT_VIEW(txtOutput)) {
		return;
	}

	char* msg_copy = strdup(message);
	if (!msg_copy) return;

	g_idle_add([](gpointer data) -> gboolean {
		char* msg = static_cast<char*>(data);

		GtkWidget* txtOutputInner = helper.getWidget("txtOutput");
		if (txtOutputInner && GTK_IS_TEXT_VIEW(txtOutputInner)) {
			GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(txtOutputInner));

			GtkTextIter end;
			gtk_text_buffer_get_end_iter(buffer, &end);

			time_t now = time(nullptr);
			struct tm* local_time = localtime(&now);
			char timestamp[64];
			strftime(timestamp, sizeof(timestamp), "[%H:%M:%S] ", local_time);

			gtk_text_buffer_insert(buffer, &end, timestamp, -1);
			gtk_text_buffer_insert(buffer, &end, msg, -1);
			gtk_text_buffer_insert(buffer, &end, "\n", 1);

			GtkWidget* parent = gtk_widget_get_parent(txtOutputInner);
			if (parent) {
				GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(
					GTK_SCROLLED_WINDOW(parent));
				gtk_adjustment_set_value(adj,
					gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj));
			}
		}

		free(msg);
		return G_SOURCE_REMOVE;
	}, msg_copy);
}

void run_long_task(const LongTaskConfig& cfg) {
    // 在 GUI 线程执行 on_started（如果提供）
    if (cfg.on_started) {
        gui_idle_call([&cfg]() {
            cfg.on_started();
        });
    }

    // 取消标志在线程间共享
    std::thread([cfg]() mutable {
        std::atomic_bool cancel_flag(false);

        // 执行后台任务
        if (cfg.worker) {
            cfg.worker(cancel_flag);
        }

        // 任务完成后在 GUI 线程执行 on_finished（如果提供）
        if (cfg.on_finished) {
            gui_idle_call([cfg]() mutable {
                cfg.on_finished();
            });
        }
    }).detach();
}

GtkWidget* create_bottom_controls(GtkWidgetHelper& helper) {
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
	
	// 顶部带有一根横贯长线
	GtkWidget* statSeparatorTop = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_box_pack_start(GTK_BOX(midProgressBox), statSeparatorTop, FALSE, FALSE, 0);

	// 长进度条
	GtkWidget* progressBar = gtk_progress_bar_new();
	gtk_widget_set_name(progressBar, "progressBar_1");
	helper.addWidget("progressBar_1", progressBar);
	gtk_widget_set_hexpand(progressBar, TRUE);
	gtk_widget_set_size_request(progressBar, -1, 4); 
	gtk_widget_set_margin_top(progressBar, 5);
	gtk_box_pack_start(GTK_BOX(midProgressBox), progressBar, FALSE, FALSE, 0);

	// 【第三行】: 状态栏和进度数
	GtkWidget* bottomStatusBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);

	// 各状态子 Box
	GtkWidget* statusLabel = helper.getWidget("status_label");
	GtkWidget* conStatus = helper.getWidget("con");
	GtkWidget* modeLabel = helper.getWidget("mode_label");
	GtkWidget* modeStatus = helper.getWidget("mode");

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

	GtkWidget* sltBoxLabel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	GtkWidget* slotLabel = helper.createLabel("Slot:", "", 0, 0, 40, 20);
	GtkWidget* slotMode = helper.createLabel("Unknown", "slot_mode", 0, 0, 100, 20);
	gtk_label_set_xalign(GTK_LABEL(slotLabel), 0.0);
	gtk_label_set_xalign(GTK_LABEL(slotMode), 0.0);
	gtk_box_pack_start(GTK_BOX(sltBoxLabel), slotLabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(sltBoxLabel), slotMode, FALSE, FALSE, 0);

	// 将左侧四段状态添加至最底层横行
	gtk_box_pack_start(GTK_BOX(bottomStatusBox), stBoxLabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(bottomStatusBox), mdBoxLabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(bottomStatusBox), stgBoxLabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(bottomStatusBox), sltBoxLabel, FALSE, FALSE, 0);

	// 将中间弹性撑开
	GtkWidget* stSpacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(bottomStatusBox), stSpacer, TRUE, TRUE, 0);

	// 右侧进度文字
	GtkWidget* prgTextHBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	GtkWidget* progressLabel = helper.createLabel(_("Progress:"), "progress_label", 0, 0, 60, 20);
	GtkWidget* percentLabel = helper.createLabel("0%", "percent", 0, 0, 40, 20);
	gtk_label_set_xalign(GTK_LABEL(progressLabel), 1.0);
	gtk_label_set_xalign(GTK_LABEL(percentLabel), 1.0);
	gtk_box_pack_start(GTK_BOX(prgTextHBox), progressLabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(prgTextHBox), percentLabel, FALSE, FALSE, 0);

	gtk_box_pack_end(GTK_BOX(bottomStatusBox), prgTextHBox, FALSE, FALSE, 0);

	// 将三层组装
	gtk_box_pack_start(GTK_BOX(bottomContainer), topActionBox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(bottomContainer), midProgressBox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(bottomContainer), bottomStatusBox, FALSE, FALSE, 0);

	return bottomContainer;
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


void bind_bottom_signals(GtkWidgetHelper& helper, GtkWidget* bottomContainer) {
	(void)bottomContainer;
	helper.bindClick(helper.getWidget("poweroff"), [&]() {
		on_button_clicked_poweroff(helper);
	});
	helper.bindClick(helper.getWidget("reboot"), [&]() {
		on_button_clicked_reboot(helper);
	});
	helper.bindClick(helper.getWidget("recovery"), [&]() {
		on_button_clicked_recovery(helper);
	});
	helper.bindClick(helper.getWidget("fastboot"), [&]() {
		on_button_clicked_fastboot(helper);
	});
}
