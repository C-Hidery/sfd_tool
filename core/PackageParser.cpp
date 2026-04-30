#include "PackageParser.hpp"
#include "../common.h"
#include "../main.h"
#include "../i18n.h"
#include "../ui/ui_common.h"
#include "../core/device_service.h"
#include "../core/config_service.h"
#include <thread>
#include <chrono>
#include <algorithm>
#include "app_state.h"
#include "logging.h"
#include "flash_service.h"

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

int PackageParser::CI_boot_device(int waitTime, bool isKick, bool isOnce) 
{
#ifdef __linux__
	check_root_permission(helper);
#endif
    int conn_wait = static_cast<int>(waitTime * REOPEN_FREQ);
    if (isKick) {
		if (isOnce) {
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
    return 0;
}
PackageInfo PackageParser::parsePackage() 
{
    PackageInfo info;
    std::ifstream file(package_path);
    if (!file.is_open()) {
        DEG_LOG(E, "Failed to open package file: %s", package_path.c_str());
        return info;
    }
    try {
        json j;
        file >> j;
        info.name = j.value("name", "");
        info.author = j.value("author", "");
        info.author_contact = j.value("author_contact", "");
        info.version = j.value("version", "");
        info.partition_list = j.value("partition_list", std::vector<std::string>{});
        info.images = j.value("images", std::vector<std::string>{});
        info.isUseCI = j.value("isUseCI", false);
        info.isPac = j.value("isPac", false);
    } catch (const std::exception& e) {
        DEG_LOG(E, "Error parsing package JSON: %s", e.what());
    }
    return info;
}

PackageParser::PackageParser(std::string path) : package_path(path) 
{
    info = parsePackage();
}

PackageParser::~PackageParser() {}

PackageInfo PackageParser::getPackageInfo() 
{
    return info;
}

CmdReturn PackageParser::CICmdExcecuter(CICommands cmd, std::vector<std::string> args)
{
    CmdReturn result;
    switch (cmd)
    {
        case CMD_BOOT:
            int waitTime = 30;
            waitTime = args.size() > 0 ? std::stoi(args[0]) : waitTime;
            result.success = CI_boot_device(waitTime, false, false) == 0;
            result.message = result.success ? "Boot successful" : "Boot failed";
            break;
        case CMD_KICK_BOOT:
            int waitTime = 30;
            waitTime = args.size() > 0 ? std::stoi(args[0]) : waitTime;
            result.success = CI_boot_device(waitTime, true, false) == 0;
            result.message = result.success ? "Kick boot successful" : "Kick boot failed";
            break;
        case CMD_ONCE_KICK_BOOT:
            int waitTime = 30;
            waitTime = args.size() > 0 ? std::stoi(args[0]) : waitTime;
            result.success = CI_boot_device(waitTime, true, true) == 0;
            result.message = result.success ? "One-time kick boot successful" : "One-time kick boot failed";
            break;
        
        default:
            result.success = false;
            result.message = "Unknown command";
    }
    return result;
}
