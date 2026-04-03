#include "ui/layout/bottom_bar.h"
#include "ui/ui_common.h"
#include "common.h"
#include "main.h"
#include "i18n.h"
#include <thread>
#include <chrono>

extern spdio_t*& io;
extern AppState g_app_state;
extern int& m_bOpened;
extern int blk_size;
extern uint64_t fblk_size;
#if defined(__APPLE__)
extern bool g_is_macos_bundle;
#endif
// 兼容旧逻辑：isCMethod 映射到 AppState::flash.isCMethod
static int& isCMethod = g_app_state.flash.isCMethod;

void LogBlkState(const char* where) {
    auto& s = GetGuiIoSettings();
    DEG_LOG(I,
            "[blk] %s: mode=%s manual=%u blk_size=%d fblk_size=%llu mac_bundle=%d",
            where,
            s.mode == sfd::BlockSizeMode::AUTO_DEFAULT ? "AUTO" : "MANUAL",
            (unsigned)s.manual_block_size,
            blk_size,
            (unsigned long long)fblk_size,
#if defined(__APPLE__)
            g_is_macos_bundle ? 1 : 0
#else
            0
#endif
    );
}

GuiIoSettings& GetGuiIoSettings() {
    static GuiIoSettings s_settings{
        sfd::BlockSizeMode::AUTO_DEFAULT,
        DEFAULT_BLK_SIZE
    };
    return s_settings;
}

uint32_t GetEffectiveManualBlockSize() {
    auto& s = GetGuiIoSettings();
    return s.manual_block_size ? s.manual_block_size : DEFAULT_BLK_SIZE;
}

sfd::BlockSizeConfig MakeBlockSizeConfigFromGui() {
    auto& s = GetGuiIoSettings();
    sfd::BlockSizeConfig cfg;
    cfg.mode = s.mode;
    if (s.mode == sfd::BlockSizeMode::MANUAL_BLOCK_SIZE) {
        // 手动模式：直接使用用户设定的块大小（已经经过对齐和裁剪）
        cfg.manual_block_size = s.manual_block_size;
    } else {
        // AUTO_DEFAULT 模式：优先使用握手阶段探测出来的默认块大小
        // （例如 FDL2 之后 UFS 设备的 0xF800），只有在没有握手默认值时
        // 才退回协议层 DEFAULT_BLK_SIZE（0x1000）。
        uint32_t step = g_default_blk_size > 0 ? (uint32_t)g_default_blk_size : DEFAULT_BLK_SIZE;
        cfg.manual_block_size = step;
    }
    cfg.use_compat_chain = true;
    return cfg;
}

void ResetBlockSizeToDefault() {
    auto& s = GetGuiIoSettings();
    // 回到 AUTO_DEFAULT 模式，并将 GUI 侧记录的“默认步长”
    // 重置为当前握手得到的默认块大小（例如 0xF800）。
    s.mode = sfd::BlockSizeMode::AUTO_DEFAULT;
    s.manual_block_size = g_default_blk_size > 0 ? (uint32_t)g_default_blk_size : DEFAULT_BLK_SIZE;

    // legacy 全局 blk_size 置 0，触发所有旧代码路径中的
    // `blk_size ? blk_size : DEFAULT_BLK_SIZE` 使用握手默认值。
    blk_size = 0;
    LogBlkState("reset_blk_size");
}

// 前向声明回调函数（来自其他页面模块）
void on_button_clicked_poweroff(GtkWidgetHelper helper);
void on_button_clicked_reboot(GtkWidgetHelper helper);
void on_button_clicked_recovery(GtkWidgetHelper helper);
void on_button_clicked_fastboot(GtkWidgetHelper helper);

void Enable_Startup(GtkWidgetHelper helper) {
	helper.enableWidget("transcode_en");
	helper.enableWidget("transcode_dis");
	helper.enableWidget("end_data_dis");
	helper.enableWidget("end_data_en");
	helper.enableWidget("charge_en");
	helper.enableWidget("charge_dis");
	helper.enableWidget("raw_data_en");
	helper.enableWidget("raw_data_dis");
	if (g_app_state.device.device_stage == BROM) helper.enableWidget("pac_flash_start"); //BROM下允许使用PAC烧录功能
	// 启动时禁止修改数据块大小，只有在成功连接设备并刷新分区表后才允许操作
	helper.disableWidget("blk_size");
	helper.disableWidget("blk_reset");
}

void EnableWidgets(GtkWidgetHelper helper) {
	helper.enableWidget("poweroff");
	helper.enableWidget("reboot");
	helper.enableWidget("recovery");
	helper.enableWidget("fastboot");
	helper.enableWidget("list_read");
	helper.enableWidget("list_write");
	helper.enableWidget("list_erase");
	helper.enableWidget("erase_all_partitions");
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
	helper.enableWidget("check_backup_integrity");
	helper.enableWidget("restore_from_folder");
	helper.enableWidget("export_part_xml");
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
	helper.disableWidget("pac_flash_start"); // PAC烧录功能仅支持BROM下进行操作
	helper.enableWidget("export_part_xml");
	helper.enableWidget("force_flash_en");
	helper.enableWidget("force_flash_dis");
	helper.enableWidget("blk_reset");
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
	helper.disableWidget("erase_all_partitions");
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
	helper.disableWidget("check_backup_integrity");
	helper.disableWidget("restore_from_folder");
	helper.disableWidget("export_part_xml");
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
	helper.disableWidget("export_part_xml");
	helper.disableWidget("force_flash_en");
	helper.disableWidget("force_flash_dis");
}

void ensure_device_attached_or_exit(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("Device unattached, exiting..."));
		}, GTK_WINDOW(helper.getWidget("main_window")));

        // 在单独线程中等待 5 秒后退出 GTK 主循环
        std::thread([]() {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            gui_idle_call([]() {
                gtk_main_quit();
            });
        }).detach();
	}
}

bool ensure_device_attached_or_warn(GtkWidgetHelper helper) {
	if (m_bOpened == -1) {
		DEG_LOG(E, "device unattached, exiting...");
		gui_idle_call_wait_drag([helper]() {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("Device unattached, exiting..."));
		}, GTK_WINDOW(helper.getWidget("main_window")));
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
        gui_idle_call([cfg]() mutable {
            if (cfg.on_started) {
                cfg.on_started();
            }
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

void showExitAfterDelayDialog(GtkWindow* parent,
                              const char* title,
                              const char* message,
                              int seconds) {
    // 在 GUI 线程弹出提示对话框
    gui_idle_call_wait_drag([parent, title, message]() {
        showInfoDialog(parent, title, message);
    }, parent);

    // 后台等待指定时间后退出 GTK 主循环
    std::thread([seconds]() {
        if (seconds > 0) {
            std::this_thread::sleep_for(std::chrono::seconds(seconds));
        }
        gui_idle_call([]() {
            gtk_main_quit();
        });
    }).detach();
}

GtkWidget* create_bottom_controls(GtkWidgetHelper& helper) {
	return bottom_bar_create(helper);
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
