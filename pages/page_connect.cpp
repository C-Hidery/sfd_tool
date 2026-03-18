#include "page_connect.h"
#include "../common.h"
#include "../main.h"
#include "../i18n.h"
#include "../ui_common.h"
#include "../core/device_service.h"
#include "../core/config_service.h"
#include "page_partition.h"
#include <thread>
#include <chrono>

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
					break;
				} else {
					g_app_state.device.device_stage = BROM;
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
	if (!found && isKickMode) g_app_state.device.device_mode = SPRD4;
	else g_app_state.device.device_mode = SPRD3;

	// 使用 DeviceService 视图统一记录阶段/模式信息

	if (fdl2_executed > 0) {
		if (g_app_state.device.device_mode == SPRD3) {
			DEG_LOG(I, "Device stage: FDL2/SPRD3");
		} else DEG_LOG(I, "Device stage: FDL2/SPRD4(AutoD)");
	} else if (fdl1_loaded > 0) {
		if (g_app_state.device.device_mode == SPRD3) {
			DEG_LOG(I, "Device stage: FDL1/SPRD3");
		} else DEG_LOG(I, "Device stage: FDL1/SPRD4(AutoD)");
	} else if (g_app_state.device.device_stage == BROM) {
		if (g_app_state.device.device_mode == SPRD3) {
			DEG_LOG(I, "Device stage: BROM/SPRD3");
		} else DEG_LOG(I, "Device stage: BROM/SPRD4(AutoD)");
	} else {
		if (g_app_state.device.device_mode == SPRD3) DEG_LOG(I, "Device stage: Unknown/SPRD3");
		else DEG_LOG(I, "Device stage: Unknown/SPRD4(AutoD)");
	}
	gui_idle_call_wait_drag([helper]() mutable {
		showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Successfully connected"), _("Device already connected! Some advanced settings opened!"));
		if (!fdl2_executed) {
			helper.enableWidget("fdl_exec");
			showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Tips"), _("Please execute FDL file to continue!"));
			if (g_app_state.device.device_mode == SPRD4 && isKickMode) {
				showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Tips"), _("Since your device is in SPRD4 mode, you can choose to skip FDL setting and directly execute FDL, but not all devices support that, please proceed with caution!"));
			}
		}
		else if (g_app_state.device.device_stage == FDL2) helper.setLabelText(helper.getWidget("con"), "Ready");
		helper.setLabelText(helper.getWidget("con"), "Connected");
		update_mode_label_from_device_service(helper);
	},GTK_WINDOW(helper.getWidget("main_window")));

}


void on_button_clicked_fdl_exec(GtkWidgetHelper helper, char* execfile) {
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
		helper.setLabelText(helper.getWidget("con"), dtxt + " -> FDL Executing");
		//Send fdl2
		if (g_app_state.device.device_mode == SPRD3) {
			FILE *fi = oxfopen(fdl_path, "r");
			if (fi == nullptr) {
				DEG_LOG(W, "File does not exist.");
				return;
			} else fclose(fi);
			if (!isKickMode) send_file(io, fdl_path, fdl_addr, end_data, blk_size ? blk_size : 528, 0, 0);
			else send_file(io, fdl_path, fdl_addr, 0, 528, 0, 0);
		} else {
			if (g_app_state.device.device_mode == SPRD4 && isKickMode) {
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
		} else if (isUseCptable) {
			io->Cptable = partition_list_d(io);
			isCMethod = 1;
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
		gui_idle_call_wait_drag([helper]() mutable {
			showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("FDL2 Executed"), _("FDL2 executed successfully!"));
			EnableWidgets(helper);
			helper.disableWidget("fdl_exec");
			helper.setLabelText(helper.getWidget("mode"), "FDL2");
			helper.setLabelText(helper.getWidget("con"), "Ready");
		},GTK_WINDOW(helper.getWidget("main_window")));

		// FDL2 执行完成后，再通过 DeviceService 探测一次设备信息
		auto* devSvc = ensure_device_service();
		if (devSvc) {
			sfd::DeviceInfo info{};
			sfd::DeviceStatus st = devSvc->probeDevice(info);
			if (st.success) {
				DEG_LOG(I, "Device stage(view): %d, mode(view): %d", (int)info.stage, (int)info.mode);
			} else {
				DEG_LOG(W, "DeviceService::probeDevice after FDL2 failed: %s", st.message.c_str());
			}
		}
		if(!(helper.getSwitchState(helper.getWidget("exec_addr"))) && g_app_state.device.device_mode == SPRD3)
		{
			// 1) 保留原有 fdl_info.json 写入逻辑，兼容旧行为
			FILE* json_file = oxfopen("fdl_info.json", "w");
			if (json_file)
			{
				json j = {
					{"fdl1_path", fdl1_path_json},
					{"fdl1_addr", fdl1_addr_json},
					{"fdl2_path", fdl2_path_json},
					{"fdl2_addr", fdl2_addr_json}
				};
				fprintf(json_file, "%s\n", j.dump().c_str());
				fclose(json_file);
			}

			// 2) 同步写入 AppConfig，交由 ConfigService 管理“最近使用的 FDL”
			auto* cfgSvc = ensure_config_service();
			if (cfgSvc) {
				sfd::AppConfig cfg{};
				// NotFound 时返回错误码，但我们可以继续使用默认配置
				cfgSvc->loadAppConfig(cfg);
				cfg.last_fdl1_path = fdl1_path_json;
				cfg.last_fdl2_path = fdl2_path_json;
				cfgSvc->saveAppConfig(cfg);
			}
		}

	} else {
		fdl1_path_json = fdl_path;
		fdl1_addr_json = fdl_addr;
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

			if (g_app_state.device.device_mode == SPRD3) {
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
				if (g_app_state.device.device_mode == SPRD4 && isKickMode) {
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
			if(waitFDL1 == -1){
				gui_idle_call_wait_drag([helper]() mutable {
					showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("FDL1 Executed"), _("FDL1 executed successfully!"));
					helper.setLabelText(helper.getWidget("mode"), "FDL1");
					helper.setLabelText(helper.getWidget("con"), "Connected");
				},GTK_WINDOW(helper.getWidget("main_window")));
			}
			waitFDL1 = 1;

		}).detach();

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

	GtkWidget* fdlFrame = gtk_frame_new(NULL);
	GtkWidget* fdlLabelTitle = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(fdlLabelTitle), (std::string("<b>") + _("FDL Send Settings") + "</b>").c_str());
	gtk_frame_set_label_widget(GTK_FRAME(fdlFrame), fdlLabelTitle);
	gtk_frame_set_label_align(GTK_FRAME(fdlFrame), 0.5, 0.5);
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

	// 从配置中恢复最近使用的 FDL 路径（如果有）
	auto* cfgSvc = ensure_config_service();
	if (cfgSvc) {
		sfd::AppConfig cfg{};
		sfd::ConfigStatus status = cfgSvc->loadAppConfig(cfg);
		if (status.success && !cfg.last_fdl1_path.empty()) {
			helper.setEntryText(fdlFilePath, cfg.last_fdl1_path.c_str());
		}
	}

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
	GtkWidget* cveFrame = gtk_frame_new(NULL);
	GtkWidget* cveLabelTitle = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(cveLabelTitle), (std::string("<b>") + _("CVE Bypass Options") + "</b>").c_str());
	gtk_frame_set_label_widget(GTK_FRAME(cveFrame), cveLabelTitle);
	gtk_frame_set_label_align(GTK_FRAME(cveFrame), 0.5, 0.5);
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
	GtkWidget* sprdFrame = gtk_frame_new(NULL);
	GtkWidget* sprdLabelTitle = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(sprdLabelTitle), (std::string("<b>") + _("SPRD4 Options") + "</b>").c_str());
	gtk_frame_set_label_widget(GTK_FRAME(sprdFrame), sprdLabelTitle);
	gtk_frame_set_label_align(GTK_FRAME(sprdFrame), 0.5, 0.5);
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
		(void)btn;
		gtk_spin_button_spin(GTK_SPIN_BUTTON(data), GTK_SPIN_STEP_BACKWARD, 1);
	}), waitCon);
	g_signal_connect(btnPlus, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer data) {
		(void)btn;
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
	helper.bindClick(helper.getWidget("connect_1"), [&, argc, argv]() {
		std::thread([&, argc, argv]() {
			on_button_clicked_connect(helper, argc, argv);
		}).detach();
	});
	helper.bindClick(helper.getWidget("select_fdl"), [&]() {
		on_button_clicked_select_fdl(helper);
	});
	helper.bindClick(helper.getWidget("select_cve"), [&]() {
		on_button_clicked_select_cve(helper);
	});
	// fdl_exec 信号绑定在 main.cpp 中处理，因为需要 execfile 参数
}
