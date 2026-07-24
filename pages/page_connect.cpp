/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#include "ui/layout/bottom_bar.h"
#include "page_connect.h"
#include "../common.h"
#include "../main.h"
#include "../i18n.h"
#include "ui/ui_common.h"
#include "../core/device_service.h"
#include "../core/config_service.h"
#include "page_partition.h"
#include <thread>
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <iomanip>

namespace fs = std::filesystem;


#ifdef _WIN32
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif
#endif

extern spdio_t*& io;
extern int ret;
extern int& m_bOpened;
extern int blk_size;
extern int keep_charge;
extern int end_data;
extern int highspeed;
extern unsigned exec_addr, baudrate;
extern int no_fdl_mode;
extern AppState g_app_state;
extern int nand_info[3];
extern int nand_id;
extern int conn_wait;
extern int fdl1_loaded;
extern int fdl2_executed;
extern int isKickMode;
extern bool isUseCptable;
extern int stage;
extern int bootmode;
extern int at;
extern int async;
extern uint64_t g_spl_size;
extern int waitFDL1;
extern int autoFDL1Suc;
extern std::string fdl1_path_json;
extern std::string fdl2_path_json;
extern uint32_t fdl1_addr_json;
extern uint32_t fdl2_addr_json;
#if !USE_LIBUSB
extern DWORD curPort;
extern DWORD* ports;
#else
extern libusb_device* curPort;
extern libusb_device** ports;
#endif

// 兼容旧逻辑：isCMethod 始终映射到 AppState::flash.isCMethod
static int& isCMethod = g_app_state.flash.isCMethod;

using nlohmann::json;

// 通过 Service 层封装设备与配置访问
static std::unique_ptr<sfd::DeviceService> g_device_service;
static std::unique_ptr<sfd::ConfigService> g_config_service;

static sfd::DeviceService* ensure_device_service() {
    if (!g_device_service) {
        g_device_service = sfd::createDeviceService();
    }
    g_device_service->setContext(io, &g_app_state);
    return g_device_service.get();
}

static sfd::ConfigService* ensure_config_service() {
    if (!g_config_service) {
        g_config_service = sfd::createConfigService();
    }
    return g_config_service.get();
}

// 根据 DeviceService 当前视图刷新 UI 上的 mode 文案
static void update_mode_label_from_device_service(GtkWidgetHelper& helper) {
    auto* devSvc = ensure_device_service();
    if (!devSvc) return;

    sfd::DeviceStage st = devSvc->getCurrentStage();
    const char* mode_text = "Unknown";
    switch (st) {
    case sfd::DeviceStage::BootRom:  mode_text = "BROM"; break;
    case sfd::DeviceStage::Fdl1:     mode_text = "FDL1"; break;
    case sfd::DeviceStage::Fdl2:     mode_text = "FDL2"; break;
    default: break;
    }
    helper.setLabelText(helper.getWidget("mode"), mode_text);
}

// 前向声明 — 这些回调定义在本文件中
extern void on_button_clicked_connect(GtkWidgetHelper helper, int argc, char** argv);
static void on_button_clicked_select_fdl(GtkWidgetHelper helper);
extern void on_button_clicked_fdl_exec(GtkWidgetHelper helper, char* execfile);
static void on_button_clicked_select_exec_addr(GtkWidgetHelper helper);

static void on_button_clicked_select_exec_addr(GtkWidgetHelper helper) {
	GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
	std::string filename = showFileChooser(parent, true);
	if (!filename.empty()) {
		helper.setEntryText(helper.getWidget("exec_addr_file"), filename);
	}
}

static void on_button_clicked_select_fdl(GtkWidgetHelper helper) {
	GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
	std::string filename = showFileChooser(parent, true);
	if (!filename.empty()) {
		helper.setEntryText(helper.getWidget("fdl_file_path"), filename);
	}
}
void on_button_clicked_fdl_exec(GtkWidgetHelper helper) {
	GtkWidget *fdlEntry = helper.getWidget("fdl_file_path");
	GtkWidget *addrEntry = helper.getWidget("fdl_addr");
	const char* fdl_path = helper.getEntryText(fdlEntry);
	const char* fdl_addr_str = helper.getEntryText(addrEntry);
	uint32_t fdl_addr = strtoul(fdl_addr_str, nullptr, 0);
	if (ensure_device_attached_or_warn(helper)) {
		return;
	}
	if (fdl1_loaded > 0) {
		DEG_LOG(I, "Executing FDL file: %s at address: 0x%X", fdl_path, fdl_addr);
		fdl2_path_json = fdl_path;
		fdl2_addr_json = fdl_addr;
		std::string dtxt = helper.getLabelText(helper.getWidget("con"));
		bottom_bar_set_status(dtxt + " -> FDL Executing");
		//Send fdl2
		if (g_app_state.device.device_mode != SPRD4) {
			EnhancedFile fi = oxfopen_enhanced(fdl_path, "r");
			if (!fi) {
				DEG_LOG(W, "File does not exist.");
				showErrorDialogSyncInThread(GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("File does not exist."));
				return;
			}
			fi.close();
			if (g_app_state.device.device_mode == SPRD3) send_file(io, fdl_path, fdl_addr, end_data, blk_size ? blk_size : 528, 0, 0);
			else send_file(io, fdl_path, fdl_addr, 0, 528, 0, 0);
		} else {
			bool result = showConfirmDialogSyncInThread(GTK_WINDOW(helper.getWidget("main_window")), _("Confirm"), _("Device can be booted without FDL in SPRD4 mode, continue?"));
			if (result) {
				DEG_LOG(I, "Skipping FDL send in SPRD4 mode.");
			} else {
				EnhancedFile fi = oxfopen_enhanced(fdl_path, "r");
				if (!fi) {
					DEG_LOG(W, "File does not exist.");
					showErrorDialogSyncInThread(GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("File does not exist."));
					return;
				}
				fi.close();
				send_file(io, fdl_path, fdl_addr, 0, 528, 0, 0);
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
			ERR_EXIT("%s: unexpected response (%s : 0x%04x)\n", name, o_exception, ret);
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
		    if (ret != BSL_REP_READ_FLASH_INFO) DEG_LOG(E,"unexpected response (0x%04x)\n", ret);
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
			g_default_blk_size = blk_size;
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
			g_default_blk_size = blk_size;
			io->ptable = partition_list(io, fn_partlist, &io->part_count);
		} else if (Da_Info.dwStorageType == 0x102) { // emmc
			io->ptable = partition_list(io, fn_partlist, &io->part_count);
		} else if (Da_Info.dwStorageType == 0x101) {
			DEG_LOG(I, "Storage is nand.");
			gui_idle_call([&]() mutable {
				helper.setLabelText(helper.getWidget("storage_mode"),"Nand");
			});
		}

		// 如果已经探测到设备默认块大小（如 UFS 上的 0xF800），同步更新高级设置页中的显示，
		// 让“数据块大小”滑块与数值直接反映统一后的默认步长（例如 63488）。
		// 如果已经探测到设备默认块大小并成功刷新分区表，则允许用户调整数据块大小
		if (io->part_count > 0 || io->part_count_c > 0) {
			gui_idle_call([&]() mutable {
				auto cfg = MakeBlockSizeConfigFromGui();
				uint32_t effective_step = cfg.manual_block_size;

				GtkWidget* sizeCon = helper.getWidget("size_con");
				if (sizeCon && GTK_IS_LABEL(sizeCon)) {
					gtk_label_set_text(GTK_LABEL(sizeCon), std::to_string(effective_step).c_str());
				}

				GtkWidget* slider = helper.getWidget("blk_size");
				if (slider && GTK_IS_RANGE(slider)) {
					gdouble min = 10000.0;
					gdouble max = std::max(min, static_cast<gdouble>(effective_step));
					gtk_range_set_range(GTK_RANGE(slider), min, max);

					gdouble value = static_cast<gdouble>(effective_step);
					if (value < min) value = min;
					if (value > max) value = max;
					gtk_range_set_value(GTK_RANGE(slider), value);
				}

				helper.enableWidget("blk_size");
				helper.enableWidget("blk_reset");

				LogBlkState("connect_update_blk_ui");
			});
		}
		if (g_app_state.flash.gpt_failed != 1) {
			if (g_app_state.flash.selected_ab == 2) {

				DEG_LOG(I, "Device is using slot b\n");
				gui_idle_call([helper]() mutable {
					helper.setLabelText(helper.getWidget("slot_mode"),"Slot B");
				});
			}
			else if (g_app_state.flash.selected_ab == 1) {

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
			std::vector<sfd::DevicePartitionInfo> partitions;
			partitions.reserve(io->part_count);
			for (int i = 0; i < io->part_count; i++) {
				sfd::DevicePartitionInfo info{};
				info.name = io->ptable[i].name;
				info.size = (std::uint64_t)io->ptable[i].size;
				info.readable = true;
				info.writable = true;
				partitions.push_back(info);
			}
			gui_idle_call_wait_drag([helper, partitions]() mutable {
				populatePartitionList(helper, partitions);
			},GTK_WINDOW(helper.getWidget("main_window")));
		}
		if (!io->part_count && !io->part_count_c) {
			DEG_LOG(W, "No partition table found on current device");
			confirm_partition_c(helper);
		}
		if (nand_id == DEFAULT_NAND_ID) {
			nand_info[0] = (uint8_t)pow(2, nand_id & 3); //page size
			nand_info[1] = 32 / (uint8_t)pow(2, (nand_id >> 2) & 3); //spare area size
			nand_info[2] = 64 * (uint8_t)pow(2, (nand_id >> 4) & 3); //block size
		}
		fdl2_executed = 1;
		g_app_state.device.device_stage = FDL2;
		gui_idle_call_wait_drag([helper]() mutable {
			showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("FDL2 Executed"), _("FDL2 executed successfully!"));
			EnableWidgets(helper);
			helper.disableWidget("fdl_exec");
			helper.setLabelText(helper.getWidget("mode"), "FDL2");
		bottom_bar_set_status("Ready");
		},GTK_WINDOW(helper.getWidget("main_window")));

		if(!(helper.getSwitchState(helper.getWidget("exec_addr"))) && g_app_state.device.device_mode == SPRD3)
		{

			// 同步写入 AppConfig，交由 ConfigService 管理“最近使用的 FDL”
			auto* cfgSvc = ensure_config_service();
			if (cfgSvc) {
				sfd::AppConfig cfg{};
				// NotFound 时返回错误码，但我们可以继续使用默认配置
				cfgSvc->loadAppConfig(cfg);
				cfg.last_fdl1_path = fdl1_path_json;
				cfg.last_fdl2_path = fdl2_path_json;
				cfg.last_fdl1_addr = std::to_string(fdl1_addr_json);
				cfg.last_fdl2_addr = std::to_string(fdl2_addr_json);
				cfgSvc->saveAppConfig(cfg);
			}
		}

	} else {
		fdl1_path_json = fdl_path;
		fdl1_addr_json = fdl_addr;
		DEG_LOG(I, "Executing FDL file: %s at address: 0x%X", fdl_path, fdl_addr);
		std::string dtxt = helper.getLabelText(helper.getWidget("con"));
		bottom_bar_set_status(dtxt + " -> FDL Executing");
		std::thread([helper, fdl_path, fdl_addr]() mutable {
			EnhancedFile fi = oxfopen_enhanced(fdl_path, "r");
			GtkWidget* execSwitch = helper.getWidget("exec_addr");
			GtkWidget* execAddr = helper.getWidget("exec_addr_file");
			GtkWidget* execAddrC = helper.getWidget("exec_addr_c");
			bool isExecAddr = helper.getSwitchState(execSwitch);
			const char* execfile = helper.getEntryText(execAddr);
			const char* exec_addr_addr = helper.getEntryText(execAddrC);

			if (g_app_state.device.device_mode != SPRD4) {
				if (!fi) {
					DEG_LOG(W, "File does not exist.\n");
					showErrorDialogSyncInThread(GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("File does not exist."));
					waitFDL1 = 1;
					return;
				}
				fi.close();
				bool isExecAddrV2 = helper.getSwitchState(helper.getWidget("exec_addr_v2"));
				if(isExecAddrV2 && isExecAddr){
					DEG_LOG(I, "Using EXECv2 binary: %s at address: %s", execfile, exec_addr_addr);
					uint32_t exec_addr_val = strtoul(exec_addr_addr, nullptr, 0);
					size_t execsize = send_file(io, fdl_path, fdl_addr, 0, 528, 0, 0);
					int n, gapsize = exec_addr - exec_addr_val - execsize;
					for (int i = 0; i < gapsize; i += n) {
						n = gapsize - i;
						if (n > 528) n = 528;
						encode_msg_nocpy(io, BSL_CMD_MIDST_DATA, n);
						if (send_and_check(io)) ERR_EXIT("CVE v2 failed");;
					}
					EnhancedFile fi = oxfopen_enhanced(execfile, "rb");
					if (fi) {
						fi.seek(0, SEEK_END);
						n = fi.tell();
						fi.seek(0, SEEK_SET);
						execsize = fi.read(io->temp_buf, 1, n);
						fi.close();
					}
					encode_msg_nocpy(io, BSL_CMD_MIDST_DATA, execsize);
					if (send_and_check(io)) ERR_EXIT("CVE v2 failed");
				}
				else {
					send_file(io, fdl_path, fdl_addr, end_data, 528, 0, 0);
					if (isExecAddr)
					{
						DEG_LOG(I, "Using EXEC binary: %s at address: %s", execfile, exec_addr_addr);
						uint32_t exec_addr_val = strtoul(exec_addr_addr, nullptr, 0);
						send_file(io, execfile, exec_addr_val, 0, 528, 0, 0);
					}
					else
					{
						encode_msg_nocpy(io, BSL_CMD_EXEC_DATA, 0);
						if (send_and_check(io)) ERR_EXIT("FDL exec failed");
					}
				}
					
				
			} else {		
				bool result = showConfirmDialogSyncInThread(GTK_WINDOW(helper.getWidget("main_window")), _("Confirm"), _("Device can be booted without FDL in SPRD4 mode, continue?"));
				if (result) {
					DEG_LOG(I, "Skipping FDL send in SPRD4 mode.");
					encode_msg_nocpy(io, BSL_CMD_EXEC_DATA, 0);
					if (send_and_check(io)) ERR_EXIT("FDL exec failed");
				} else {
					if (fi == nullptr) {
						DEG_LOG(W, "File does not exist.\n");
						showErrorDialogSyncInThread(GTK_WINDOW(helper.getWidget("main_window")), _("Error"), _("File does not exist."));
						waitFDL1 = 1;
						return;
					}
					bool isExecAddrV2 = helper.getSwitchState(helper.getWidget("exec_addr_v2"));
					if(isExecAddrV2 && isExecAddr){
						DEG_LOG(I, "Using EXECv2 binary: %s at address: %s", execfile, exec_addr_addr);
						uint32_t exec_addr_val = strtoul(exec_addr_addr, nullptr, 0);
						size_t execsize = send_file(io, fdl_path, fdl_addr, 0, 528, 0, 0);
						int n, gapsize = exec_addr - exec_addr_val - execsize;
						for (int i = 0; i < gapsize; i += n) {
							n = gapsize - i;
							if (n > 528) n = 528;
							encode_msg_nocpy(io, BSL_CMD_MIDST_DATA, n);
							if (send_and_check(io)) ERR_EXIT("CVE v2 failed");;
						}
						EnhancedFile fi = oxfopen_enhanced(execfile, "rb");
						if (fi) {
							fi.seek(0, SEEK_END);
							n = fi.tell();
							fi.seek(0, SEEK_SET);
							execsize = fi.read(io->temp_buf, 1, n);
							fi.close();
						}
						encode_msg_nocpy(io, BSL_CMD_MIDST_DATA, execsize);
						if (send_and_check(io)) ERR_EXIT("CVE v2 failed");
					}
					else {
						send_file(io, fdl_path, fdl_addr, end_data, 528, 0, 0);
						if (isExecAddr)
						{
							DEG_LOG(I, "Using EXEC binary: %s at address: %s", execfile, exec_addr_addr);
							uint32_t exec_addr_val = strtoul(exec_addr_addr, nullptr, 0);
							send_file(io, execfile, exec_addr_val, 0, 528, 0, 0);
						}
						else
						{
							encode_msg_nocpy(io, BSL_CMD_EXEC_DATA, 0);
							if (send_and_check(io)) ERR_EXIT("FDL exec failed");
						}
					}
				}
			}
			// if (execfile) delete[](execfile);
			DEG_LOG(OP, "Execute FDL1");

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
			if (send_and_check(io)) ERR_EXIT("FDL connect failed\n");
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
			g_app_state.device.device_stage = FDL1;
			if(waitFDL1 == -1){
				gui_idle_call_wait_drag([helper]() mutable {
					showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("FDL1 Executed"), _("FDL1 executed successfully!"));
					helper.setLabelText(helper.getWidget("mode"), "FDL1");
					bottom_bar_set_status("Connected");
				},GTK_WINDOW(helper.getWidget("main_window")));
			}
			waitFDL1 = 1;
			autoFDL1Suc = 1;

		}).detach();

	}
	
}
std::string uint32_to_hex_string(uint32_t value) {
    std::stringstream ss;
    ss << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
    return ss.str();
}
void on_button_clicked_connect(GtkWidgetHelper helper, int argc, char** argv) {
	GtkWidget* waitBox = helper.getWidget("wait_con");
	GtkWidget* sprd4Switch = helper.getWidget("sprd4");
	GtkWidget* execSwitch = helper.getWidget("exec_addr");
	GtkWidget* execAddr = helper.getWidget("exec_addr_file");
	GtkWidget* execAddrC = helper.getWidget("exec_addr_c");
	GtkWidget* sprd4OneMode = helper.getWidget("sprd4_one_mode");
	bottom_bar_set_status("Waiting for connection...");
	if (argc > 1 && !strcmp(argv[1], "--reconnect")) {
		stage = 99;
		showInfoDialogSyncInThread(GTK_WINDOW(helper.getWidget("main_window")), _("Tips"), _("You have entered Reconnect Mode, which only supports compatibility-method partition list retrieval, and [storage mode/slot mode] can not be gotten!"));
	}
#ifndef _WIN32
	check_root_permission(helper);
#endif
	helper.disableWidget("connect_1");
	double wait_time = 30.0;
	if (waitBox && GTK_IS_ENTRY(waitBox)) {
		const char* text = helper.getEntryText(waitBox);
		if (text && *text) {
			char* endptr = nullptr;
			long value = strtol(text, &endptr, 10);
			if (endptr != text) {
				if (value < 1) value = 1;
				if (value > 65535) value = 65535;
				wait_time = static_cast<double>(value);
			}
		}
	}
	bool isSprd4 = helper.getSwitchState(sprd4Switch);
	bool isOneMode = helper.getSwitchState(sprd4OneMode);
	bool isExec = helper.getSwitchState(execSwitch);
	const char* exec_path = helper.getEntryText(execAddr);
	const char* exec_addr = helper.getEntryText(execAddrC);
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
	if (isExec) {
		DEG_LOG(I, "Using EXEC binary: %s at address: %s", exec_path, exec_addr);
	}

#if !USE_LIBUSB
	g_app_state.transport.bListenLibusb = 0;
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
		g_app_state.transport.bListenLibusb = 0;
		bootmode = -1;
		at = 0;
	}
	if (at || bootmode >= 0) {
		startUsbEventHandle();
		ChangeMode(io, conn_wait / REOPEN_FREQ * 1000, bootmode, at);
		conn_wait = 30 * REOPEN_FREQ;
		stage = -1;
	}
	if (!g_app_state.transport.bListenLibusb) startUsbEventHandle();
#endif
#if _WIN32
	if (!g_app_state.transport.bListenLibusb) {
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
			if (g_app_state.transport.bListenLibusb) {
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
					g_app_state.device.device_stage = FDL1;
					DEG_LOG(OP, "FDL1 connected.");
					if (!memcmp(io->raw_buf + 4, "SPRD4", 5) && no_fdl_mode) fdl2_executed = -1;
					if (!memcmp(io->raw_buf + 4, "SPRD4", 5)) g_app_state.device.device_mode = SPRD4;
					break;
				} else {
					g_app_state.device.device_stage = BROM;
					DEG_LOG(OP, "Check baud BROM");
					if (!memcmp(io->raw_buf + 4, "SPRD4", 5) && no_fdl_mode) {
						fdl1_loaded = -1;
						fdl2_executed = -1;
					}
					if (!memcmp(io->raw_buf + 4, "SPRD4", 5)) g_app_state.device.device_mode = SPRD4;
				}
				DBG_LOG("Device mode version: ");
				print_string(stdout, io->raw_buf + 4, READ16_BE(io->raw_buf + 2));
				print_to_string(mode_str, sizeof(mode_str), io->raw_buf + 4, READ16_BE(io->raw_buf + 2), 0);

				encode_msg_nocpy(io, BSL_CMD_CONNECT, 0);
				if (send_and_check(io)) ERR_EXIT("FDL connect failed");
			} else if (ret == BSL_REP_VERIFY_ERROR) {
				encode_msg_nocpy(io, BSL_CMD_CONNECT, 0);
				if (fdl1_loaded != 1) {
					if (send_and_check(io)) ERR_EXIT("FDL connect failed\n");
				} else {
					i = -1;
					continue;
				}
			}
			if (fdl1_loaded == 1) {
				DEG_LOG(OP, "FDL1 connected.");
				g_app_state.device.device_stage = FDL1;
				if (keep_charge) {
					encode_msg_nocpy(io, BSL_CMD_KEEP_CHARGE, 0);
					if (!send_and_check(io)) DEG_LOG(OP, "Keep charge FDL1.");
				}
				break;
			} else {
				DEG_LOG(OP, "BROM connected.");
				g_app_state.device.device_stage = BROM;
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
				g_app_state.device.device_stage = FDL2;
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
	if (found && g_app_state.device.device_mode != SPRD4) g_app_state.device.device_mode = SPRD3;
	else if (g_app_state.device.device_mode != SPRD4) g_app_state.device.device_mode = Nothing;

	// 使用 DeviceService 视图统一记录阶段/模式信息

	if (fdl2_executed > 0) {
		if (g_app_state.device.device_mode == SPRD3) {
			DEG_LOG(I, "Device status: FDL2/SPRD3");
		} 
		else if (g_app_state.device.device_mode == SPRD4) DEG_LOG(I, "Device status: FDL2/SPRD4(AutoD)");
		else DEG_LOG(I, "Device status: FDL2/Unknown");
	} else if (fdl1_loaded > 0) {
		if (g_app_state.device.device_mode == SPRD3) {
			DEG_LOG(I, "Device status: FDL1/SPRD3");
		} 
		else if (g_app_state.device.device_mode == SPRD4) DEG_LOG(I, "Device status: FDL1/SPRD4(AutoD)");
		else DEG_LOG(I, "Device status: FDL1/Unknown");
	} else if (g_app_state.device.device_stage == BROM) {
		if (g_app_state.device.device_mode == SPRD3) {
			DEG_LOG(I, "Device status: BROM/SPRD3");
		} 
		else if (g_app_state.device.device_mode == SPRD4) DEG_LOG(I, "Device status: BROM/SPRD4(AutoD)");
		else DEG_LOG(I, "Device status: BROM/Unknown");
	} else {
		if (g_app_state.device.device_mode == SPRD3) DEG_LOG(I, "Device status: Unknown/SPRD3");
		else if (g_app_state.device.device_mode == SPRD4) DEG_LOG(I, "Device status: Unknown/SPRD4(AutoD)");
		else DEG_LOG(I, "Device status: Unknown/Unknown");
	}
	showInfoDialogSyncInThread(GTK_WINDOW(helper.getWidget("main_window")), _("Successfully connected"), _("Device already connected! Some advanced settings opened!"));	
	gui_idle_call_wait_drag([=]() mutable {
		if (g_app_state.device.device_stage == FDL2) bottom_bar_set_status("Ready");
		else bottom_bar_set_status("Connected");
		Enable_Startup(helper);
		update_mode_label_from_device_service(helper);
	},GTK_WINDOW(helper.getWidget("main_window")));
	if (!fdl2_executed) {
		helper.enableWidget("fdl_exec");
		showInfoDialogSyncInThread(GTK_WINDOW(helper.getWidget("main_window")), _("Tips"), _("Please execute FDL file to continue!"));
		if (g_app_state.device.device_mode == SPRD4) {
			showInfoDialogSyncInThread(GTK_WINDOW(helper.getWidget("main_window")), _("Tips"), _("Since your device is in SPRD4 mode, you can choose to skip FDL setting and directly execute FDL, but not all devices support that, please proceed with caution!"));
		}
		auto* cfgSvc = ensure_config_service();
		sfd::AppConfig cfg{};
		sfd::ConfigStatus status = cfgSvc->loadAppConfig(cfg);
		if(status.success && !cfg.last_fdl1_path.empty() && !cfg.last_fdl2_path.empty() && !cfg.last_fdl1_addr.empty() && !cfg.last_fdl2_addr.empty() && g_app_state.device.device_stage == BROM && !isKickMode && !isExec)
		{
			bool i_is = false;
			i_is = showConfirmDialogSyncInThread(GTK_WINDOW(helper.getWidget("main_window")), _("Confirm"),_("Last FDL path and addr Info detected, do you want to load FDL to FDL2 mode automatically?"));
			if (i_is)
			{
				helper.setEntryText(helper.getWidget("fdl_file_path"), cfg.last_fdl1_path);
				helper.setEntryText(helper.getWidget("fdl_addr"), uint32_to_hex_string(static_cast<uint32_t>(std::stoul(cfg.last_fdl1_addr))));
				DEG_LOG(I, "Loaded FDL info: %s at address: %s", cfg.last_fdl1_path.c_str(), uint32_to_hex_string(static_cast<uint32_t>(std::stoul(cfg.last_fdl1_addr))).c_str());
				waitFDL1 = 0;
				ensure_device_attached_or_exit(helper);
				std::thread([helper]() mutable {
					on_button_clicked_fdl_exec(helper);
				}).detach();
				while(1)
				{
					if(waitFDL1 == 1) break;
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
					g_main_context_iteration(NULL, FALSE);
				}
				if (autoFDL1Suc)
				{
					ensure_device_attached_or_exit(helper);
					helper.setEntryText(helper.getWidget("fdl_file_path"), cfg.last_fdl2_path);
					helper.setEntryText(helper.getWidget("fdl_addr"), uint32_to_hex_string(static_cast<uint32_t>(std::stoul(cfg.last_fdl2_addr))));
					DEG_LOG(I, "Loaded FDL info: %s at address: %s", cfg.last_fdl2_path.c_str(), uint32_to_hex_string(static_cast<uint32_t>(std::stoul(cfg.last_fdl2_addr))).c_str());
					std::thread([helper]() mutable {
						on_button_clicked_fdl_exec(helper);
					}).detach();
				}
			}
		}
	}
}

GtkWidget* create_connect_page(GtkWidgetHelper& helper, GtkWidget* notebook) {
    // 1. 创建页面顶层 Grid（代替 helper.createGrid）
    GtkWidget* connectPage = gtk_grid_new();
    gtk_widget_set_name(connectPage, "connect_page");
    helper.addWidget("connect_page", connectPage);
    helper.addNotebookPage(notebook, connectPage, _("Connect"));

    // 外层滚动窗口
    GtkWidget* connectScroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(connectScroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(connectScroll, TRUE);
    gtk_widget_set_vexpand(connectScroll, TRUE);

    // 主垂直容器
    GtkWidget* mainConnectBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_widget_set_margin_start(mainConnectBox, 20);
    gtk_widget_set_margin_end(mainConnectBox, 20);
    gtk_widget_set_margin_top(mainConnectBox, 20);
    gtk_widget_set_margin_bottom(mainConnectBox, 20);

    // ========== 1. Header ==========
    GtkWidget* headerBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_halign(headerBox, GTK_ALIGN_CENTER);

    // 欢迎标签
    GtkWidget* welcomeLabel = gtk_label_new(_("Welcome to SFD Tool GUI!"));
    gtk_widget_set_name(welcomeLabel, "welcome");
    gtk_widget_set_size_request(welcomeLabel, 467, 28);
    PangoAttrList* attr_list = pango_attr_list_new();
    PangoAttribute* attr = pango_attr_size_new(20 * PANGO_SCALE);
    pango_attr_list_insert(attr_list, attr);
    gtk_label_set_attributes(GTK_LABEL(welcomeLabel), attr_list);
    helper.addWidget("welcome", welcomeLabel);

    GtkWidget* tiLabel = gtk_label_new(_("Please connect your device with BROM mode"));
    gtk_widget_set_name(tiLabel, "ti");
    gtk_widget_set_size_request(tiLabel, 400, 28);
    gtk_label_set_attributes(GTK_LABEL(tiLabel), attr_list);
    helper.addWidget("ti", tiLabel);
    // 释放 attr_list（但 pango_attr_list_unref 在 GTK4 中仍可用）
    pango_attr_list_unref(attr_list);

    GtkWidget* instruction = gtk_label_new(_("Press and hold the volume up or down keys and the power key to connect"));
    gtk_widget_set_name(instruction, "instruction");
    gtk_widget_set_size_request(instruction, 600, 20);
    helper.addWidget("instruction", instruction);

    gtk_box_append(GTK_BOX(headerBox), welcomeLabel);
    gtk_box_append(GTK_BOX(headerBox), tiLabel);
    gtk_box_append(GTK_BOX(headerBox), instruction);

    gtk_box_append(GTK_BOX(mainConnectBox), headerBox);

    // ========== 2. FDL Settings ==========
    GtkWidget* fdlCenterBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign(fdlCenterBox, GTK_ALIGN_CENTER);

    GtkWidget* fdlFrame = gtk_frame_new(NULL);
    gtk_widget_set_size_request(fdlFrame, 600, -1);
    GtkWidget* fdlLabelTitle = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(fdlLabelTitle),
                         (std::string("<b>") + _("FDL Send Settings") + "</b>").c_str());
    gtk_frame_set_label_widget(GTK_FRAME(fdlFrame), fdlLabelTitle);
    gtkFrameSetLabelAlign(fdlFrame, 0.5, 0.5);  // 辅助函数，GTK4 只使用 xalign

    GtkWidget* fdlGrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(fdlGrid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(fdlGrid), 10);
    gtk_widget_set_margin_start(fdlGrid, 15);
    gtk_widget_set_margin_end(fdlGrid, 15);
    gtk_widget_set_margin_top(fdlGrid, 15);
    gtk_widget_set_margin_bottom(fdlGrid, 15);

    // FDL 文件路径
    GtkWidget* fdlLabel = gtk_label_new(_("FDL File Path :"));
    gtk_widget_set_name(fdlLabel, "fdl_label");
    gtk_widget_set_size_request(fdlLabel, 100, 20);
    gtk_widget_set_halign(fdlLabel, GTK_ALIGN_START);
    helper.addWidget("fdl_label", fdlLabel);

    GtkWidget* fdlFilePath = gtk_entry_new();
    gtk_widget_set_name(fdlFilePath, "fdl_file_path");
    gtk_widget_set_size_request(fdlFilePath, 300, 32);
    gtk_widget_set_hexpand(fdlFilePath, TRUE);
    helper.addWidget("fdl_file_path", fdlFilePath);

    GtkWidget* selectFdlBtn = gtk_button_new_with_label("...");
    gtk_widget_set_name(selectFdlBtn, "select_fdl");
    gtk_widget_set_size_request(selectFdlBtn, 40, 32);
    helper.addWidget("select_fdl", selectFdlBtn);

    // 恢复最近路径
    auto* cfgSvc = ensure_config_service();
    if (cfgSvc) {
        sfd::AppConfig cfg{};
        sfd::ConfigStatus status = cfgSvc->loadAppConfig(cfg);
        if (status.success && !cfg.last_fdl1_path.empty()) {
            gtk_editable_set_text(GTK_EDITABLE(fdlFilePath), cfg.last_fdl1_path.c_str());
        }
    }

    gtk_grid_attach(GTK_GRID(fdlGrid), fdlLabel, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(fdlGrid), fdlFilePath, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(fdlGrid), selectFdlBtn, 2, 0, 1, 1);

    // FDL 地址
    GtkWidget* fdlAddrLabel = gtk_label_new(_("FDL Send Address :"));
    gtk_widget_set_name(fdlAddrLabel, "fdl_addr_label");
    gtk_widget_set_size_request(fdlAddrLabel, 120, 20);
    gtk_widget_set_halign(fdlAddrLabel, GTK_ALIGN_START);
    helper.addWidget("fdl_addr_label", fdlAddrLabel);

    GtkWidget* fdlAddr = gtk_entry_new();
    gtk_widget_set_name(fdlAddr, "fdl_addr");
    gtk_widget_set_size_request(fdlAddr, 300, 32);
    gtk_widget_set_hexpand(fdlAddr, TRUE);
    helper.addWidget("fdl_addr", fdlAddr);

    if (cfgSvc) {
        sfd::AppConfig cfg{};
        sfd::ConfigStatus status = cfgSvc->loadAppConfig(cfg);
        if (status.success && !cfg.last_fdl1_addr.empty()) {
            gtk_editable_set_text(GTK_EDITABLE(fdlAddr),
                                  uint32_to_hex_string(static_cast<uint32_t>(std::stoul(cfg.last_fdl1_addr))).c_str());
        }
    }

    gtk_grid_attach(GTK_GRID(fdlGrid), fdlAddrLabel, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(fdlGrid), fdlAddr, 1, 1, 2, 1);

    // Execute 按钮
    GtkWidget* fdlExecBtn = gtk_button_new_with_label(_("Execute"));
    gtk_widget_set_name(fdlExecBtn, "fdl_exec");
    gtk_widget_set_size_request(fdlExecBtn, 150, 32);
    gtk_widget_set_hexpand(fdlExecBtn, TRUE);
    helper.addWidget("fdl_exec", fdlExecBtn);

    gtk_grid_attach(GTK_GRID(fdlGrid), fdlExecBtn, 0, 2, 3, 1);

    // 将 fdlGrid 放入 frame
    gtk_frame_set_child(GTK_FRAME(fdlFrame), fdlGrid);
    gtk_box_append(GTK_BOX(fdlCenterBox), fdlFrame);
    gtk_box_append(GTK_BOX(mainConnectBox), fdlCenterBox);

    // ========== 3. Advanced Options (左右两列) ==========
    GtkWidget* advContainer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_box_set_homogeneous(GTK_BOX(advContainer), TRUE);

    // ---- 左列：CVE Options ----
    GtkWidget* cveFrame = gtk_frame_new(NULL);
    GtkWidget* cveLabelTitle = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(cveLabelTitle),
                         (std::string("<b>") + _("EXEC_ADDR Options") + "</b>").c_str());
    gtk_frame_set_label_widget(GTK_FRAME(cveFrame), cveLabelTitle);
    gtkFrameSetLabelAlign(cveFrame, 0.5, 0.5);

    GtkWidget* cveGrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(cveGrid), 15);
    gtk_grid_set_column_spacing(GTK_GRID(cveGrid), 10);
    gtk_widget_set_margin_start(cveGrid, 15);
    gtk_widget_set_margin_end(cveGrid, 15);
    gtk_widget_set_margin_top(cveGrid, 15);
    gtk_widget_set_margin_bottom(cveGrid, 15);
    gtk_frame_set_child(GTK_FRAME(cveFrame), cveGrid);

    // 开关：exec_addr
    GtkWidget* cveSwitchLabel = gtk_label_new(_("Try to use EXEC_ADDR"));
    gtk_widget_set_name(cveSwitchLabel, "exec_addr_label");
    gtk_widget_set_size_request(cveSwitchLabel, 200, 20);
    gtk_widget_set_halign(cveSwitchLabel, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(cveSwitchLabel), 0.0);
    helper.addWidget("exec_addr_label", cveSwitchLabel);

    GtkWidget* cveSwitch = gtk_switch_new();
    gtk_widget_set_name(cveSwitch, "exec_addr");
    gtk_widget_set_halign(cveSwitch, GTK_ALIGN_END);
    gtk_widget_set_hexpand(cveSwitch, TRUE);
    helper.addWidget("exec_addr", cveSwitch);

    gtk_grid_attach(GTK_GRID(cveGrid), cveSwitchLabel, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(cveGrid), cveSwitch, 1, 0, 1, 1);

    // 开关：exec_addr_v2
    GtkWidget* execAddrV2Label = gtk_label_new(_("Enable EXEC_ADDR v2"));
    gtk_widget_set_name(execAddrV2Label, "exec_addr_v2_label");
    gtk_widget_set_size_request(execAddrV2Label, 100, 20);
    gtk_widget_set_halign(execAddrV2Label, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(execAddrV2Label), 0.0);
    helper.addWidget("exec_addr_v2_label", execAddrV2Label);

    GtkWidget* execAddrV2Switch = gtk_switch_new();
    gtk_widget_set_name(execAddrV2Switch, "exec_addr_v2");
    gtk_widget_set_halign(execAddrV2Switch, GTK_ALIGN_END);
    gtk_widget_set_hexpand(execAddrV2Switch, TRUE);
    helper.addWidget("exec_addr_v2", execAddrV2Switch);

    gtk_grid_attach(GTK_GRID(cveGrid), execAddrV2Label, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(cveGrid), execAddrV2Switch, 1, 1, 1, 1);

    // 文件选择行
    GtkWidget* execAddrLabel = gtk_label_new(_("EXEC_ADDR Binary File Address"));
    gtk_widget_set_name(execAddrLabel, "exec_addr_label");
    gtk_widget_set_size_request(execAddrLabel, 150, 20);
    gtk_widget_set_halign(execAddrLabel, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(execAddrLabel), 0.0);
    helper.addWidget("exec_addr_label", execAddrLabel);

    GtkWidget* execAddrInputBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_halign(execAddrInputBox, GTK_ALIGN_END);
    gtk_widget_set_hexpand(execAddrInputBox, TRUE);

    GtkWidget* execAddr = gtk_entry_new();
    gtk_widget_set_name(execAddr, "exec_addr_file");
    gtk_widget_set_size_request(execAddr, 150, 32);
    helper.addWidget("exec_addr_file", execAddr);

    GtkWidget* selectExecAddrBtn = gtk_button_new_with_label("...");
    gtk_widget_set_name(selectExecAddrBtn, "select_exec_addr");
    gtk_widget_set_size_request(selectExecAddrBtn, 40, 32);
    helper.addWidget("select_exec_addr", selectExecAddrBtn);

    gtk_box_append(GTK_BOX(execAddrInputBox), execAddr);
    gtk_box_append(GTK_BOX(execAddrInputBox), selectExecAddrBtn);

    gtk_grid_attach(GTK_GRID(cveGrid), execAddrLabel, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(cveGrid), execAddrInputBox, 1, 2, 1, 1);

    // ---- 右列：SPRD4 Options ----
    GtkWidget* sprdFrame = gtk_frame_new(NULL);
    GtkWidget* sprdLabelTitle = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(sprdLabelTitle),
                         (std::string("<b>") + _("SPRD4 Options") + "</b>").c_str());
    gtk_frame_set_label_widget(GTK_FRAME(sprdFrame), sprdLabelTitle);
    gtkFrameSetLabelAlign(sprdFrame, 0.5, 0.5);

    GtkWidget* sprdGrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(sprdGrid), 15);
    gtk_grid_set_column_spacing(GTK_GRID(sprdGrid), 10);
    gtk_widget_set_margin_start(sprdGrid, 15);
    gtk_widget_set_margin_end(sprdGrid, 15);
    gtk_widget_set_margin_top(sprdGrid, 15);
    gtk_widget_set_margin_bottom(sprdGrid, 15);
    gtk_frame_set_child(GTK_FRAME(sprdFrame), sprdGrid);

    // 开关：sprd4
    GtkWidget* sprd4Label = gtk_label_new(_("Kick device to SPRD4"));
    gtk_widget_set_name(sprd4Label, "sprd4_label");
    gtk_widget_set_size_request(sprd4Label, 150, 20);
    gtk_widget_set_halign(sprd4Label, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(sprd4Label), 0.0);
    helper.addWidget("sprd4_label", sprd4Label);

    GtkWidget* sprd4Switch = gtk_switch_new();
    gtk_widget_set_name(sprd4Switch, "sprd4");
    gtk_widget_set_halign(sprd4Switch, GTK_ALIGN_END);
    gtk_widget_set_hexpand(sprd4Switch, TRUE);
    helper.addWidget("sprd4", sprd4Switch);

    gtk_grid_attach(GTK_GRID(sprdGrid), sprd4Label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(sprdGrid), sprd4Switch, 1, 0, 1, 1);

    // 开关：sprd4_one_mode
    GtkWidget* sprd4OneLabel = gtk_label_new(_("Kick One-time Mode"));
    gtk_widget_set_name(sprd4OneLabel, "sprd4_one_label");
    gtk_widget_set_size_request(sprd4OneLabel, 150, 20);
    gtk_widget_set_halign(sprd4OneLabel, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(sprd4OneLabel), 0.0);
    helper.addWidget("sprd4_one_label", sprd4OneLabel);

    GtkWidget* sprd4OneMode = gtk_switch_new();
    gtk_widget_set_name(sprd4OneMode, "sprd4_one_mode");
    gtk_widget_set_halign(sprd4OneMode, GTK_ALIGN_END);
    gtk_widget_set_hexpand(sprd4OneMode, TRUE);
    helper.addWidget("sprd4_one_mode", sprd4OneMode);

    gtk_grid_attach(GTK_GRID(sprdGrid), sprd4OneLabel, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(sprdGrid), sprd4OneMode, 1, 1, 1, 1);

    // 地址输入
    GtkWidget* execAddrLabel2 = gtk_label_new(_("EXEC_ADDR executable Addr"));
    gtk_widget_set_name(execAddrLabel2, "exec_addr_label2");
    gtk_widget_set_size_request(execAddrLabel2, 100, 20);
    gtk_widget_set_halign(execAddrLabel2, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(execAddrLabel2), 0.0);
    helper.addWidget("exec_addr_label2", execAddrLabel2);

    GtkWidget* execAddrC = gtk_entry_new();
    gtk_widget_set_name(execAddrC, "exec_addr_c");
    gtk_widget_set_size_request(execAddrC, 200, 32);
    gtk_widget_set_halign(execAddrC, GTK_ALIGN_END);
    gtk_widget_set_hexpand(execAddrC, TRUE);
    helper.addWidget("exec_addr_c", execAddrC);

    gtk_grid_attach(GTK_GRID(sprdGrid), execAddrLabel2, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(sprdGrid), execAddrC, 1, 2, 1, 1);

    // 将左右 frame 加入 advContainer
    gtk_box_append(GTK_BOX(advContainer), cveFrame);
    gtk_box_append(GTK_BOX(advContainer), sprdFrame);

    gtk_box_append(GTK_BOX(mainConnectBox), advContainer);

    // ========== 4. Bottom Action Area ==========
    GtkWidget* actionBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_halign(actionBox, GTK_ALIGN_CENTER);

    // CONNECT 按钮
    GtkWidget* connectBtn = gtk_button_new_with_label(_("CONNECT"));
    gtk_widget_set_name(connectBtn, "connect_1");
    gtk_widget_set_size_request(connectBtn, 300, 48);
    helper.addWidget("connect_1", connectBtn);

    // 等待时间设置
    GtkWidget* waitBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(waitBox, GTK_ALIGN_CENTER);

    GtkWidget* waitLabel = gtk_label_new(_("Wait connection time (s):"));
    gtk_widget_set_name(waitLabel, "wait_label");
    gtk_widget_set_size_request(waitLabel, 150, 20);
    helper.addWidget("wait_label", waitLabel);

    // 自定义 spinbox (entry + 按钮)
    GtkWidget* customSpinBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(customSpinBox, "linked");

    GtkWidget* waitCon = gtk_entry_new();
    gtk_widget_set_name(waitCon, "wait_con");
    gtk_widget_set_size_request(waitCon, 60, 32);
    gtk_entry_set_alignment(GTK_ENTRY(waitCon), 0.5);
    gtk_editable_set_text(GTK_EDITABLE(waitCon), "30");
    helper.addWidget("wait_con", waitCon);

    GtkWidget* btnMinus = gtk_button_new_with_label("-");
    gtk_widget_set_size_request(btnMinus, 32, 32);
    GtkWidget* btnPlus  = gtk_button_new_with_label("+");
    gtk_widget_set_size_request(btnPlus, 32, 32);

    g_signal_connect(btnMinus, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        GtkWidget* entry = GTK_WIDGET(data);
        const char* text = gtk_editable_get_text(GTK_EDITABLE(entry));
        char* endptr = nullptr;
        long value = strtol(text, &endptr, 10);
        if (endptr == text) value = 30;
        if (value > 1) --value;
        else value = 1;
        char buf[32];
        snprintf(buf, sizeof(buf), "%ld", value);
        gtk_editable_set_text(GTK_EDITABLE(entry), buf);
    }), waitCon);

    g_signal_connect(btnPlus, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        GtkWidget* entry = GTK_WIDGET(data);
        const char* text = gtk_editable_get_text(GTK_EDITABLE(entry));
        char* endptr = nullptr;
        long value = strtol(text, &endptr, 10);
        if (endptr == text) value = 30;
        if (value < 65535) ++value;
        else value = 65535;
        char buf[32];
        snprintf(buf, sizeof(buf), "%ld", value);
        gtk_editable_set_text(GTK_EDITABLE(entry), buf);
    }), waitCon);

    gtk_box_append(GTK_BOX(customSpinBox), waitCon);
    gtk_box_append(GTK_BOX(customSpinBox), btnMinus);
    gtk_box_append(GTK_BOX(customSpinBox), btnPlus);

    gtk_box_append(GTK_BOX(waitBox), waitLabel);
    gtk_box_append(GTK_BOX(waitBox), customSpinBox);

    gtk_box_append(GTK_BOX(actionBox), connectBtn);
    gtk_box_append(GTK_BOX(actionBox), waitBox);

    gtk_box_append(GTK_BOX(mainConnectBox), actionBox);

    GtkWidget* statusLabel = gtk_label_new(_("Status: "));
    gtk_widget_set_name(statusLabel, "status_label");
    gtk_widget_set_size_request(statusLabel, 70, 24);
    helper.addWidget("status_label", statusLabel);

    GtkWidget* conStatus = gtk_label_new(_("Not connected"));
    gtk_widget_set_name(conStatus, "con");
    gtk_widget_set_size_request(conStatus, 150, 23);
    helper.addWidget("con", conStatus);

    GtkWidget* modeLabel = gtk_label_new(_("Mode: "));
    gtk_widget_set_name(modeLabel, "mode_label");
    gtk_widget_set_size_request(modeLabel, 50, 19);
    helper.addWidget("mode_label", modeLabel);

    GtkWidget* modeStatus = gtk_label_new(_("BROM Not connected!!!"));
    gtk_widget_set_name(modeStatus, "mode");
    gtk_widget_set_size_request(modeStatus, 140, 19);
    helper.addWidget("mode", modeStatus);

    // 将 mainConnectBox 放入滚动窗口
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(connectScroll), mainConnectBox);
    // 将滚动窗口放入 connectPage 网格
    gtk_grid_attach(GTK_GRID(connectPage), connectScroll, 0, 0, 5, 5);

    return connectPage;
}

void bind_connect_signals(GtkWidgetHelper& helper, int argc, char** argv) {
	helper.bindClick(helper.getWidget("connect_1"), [&, argc, argv]() {
		std::thread([&, argc, argv]() {
			on_button_clicked_connect(helper, argc, argv);
		}).detach();
	});
	helper.bindClick(helper.getWidget("select_fdl"), [&]() {
		on_button_clicked_select_fdl(helper);
	});
	helper.bindClick(helper.getWidget("select_exec_addr"), [&]() {
		on_button_clicked_select_exec_addr(helper);
	});
	helper.bindClick(helper.getWidget("fdl_exec"), [&]() {
		std::thread([&]() {
			on_button_clicked_fdl_exec(helper);
		}).detach();
	});
}
