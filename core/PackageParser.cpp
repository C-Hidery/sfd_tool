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

bool isCVE = false;
bool isCVEv2 = false;
uint32_t cve_addr = 0;
bool isNoFDLMode = false;


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

PackageParser::~PackageParser() 
{
    if (execfile) delete[] execfile;
}

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
        case CMD_REBOOT:
            encode_msg_nocpy(io, BSL_CMD_NORMAL_RESET, 0);
            if (!send_and_check(io)) {
                result.success = true;
                result.message = "Reboot command sent successfully";
            } else {
                result.success = false;
                result.message = "Reboot command failed";
            }
            break;
        case CMD_POWER_OFF:
            encode_msg_nocpy(io, BSL_CMD_POWER_OFF, 0);
            if (!send_and_check(io)) {
                result.success = true;
                result.message = "Power off command sent successfully";
            } else {
                result.success = false;
                result.message = "Power off command failed";
            }
            break;
        case CMD_SEND_FILE:
            if (args.size() < 2) {
                result.success = false;
                result.message = "SEND_FILE command requires 2 arguments: file path and address";
            } else {
                const std::string& filePath = args[0];
                FILE* file = fopen(filePath.c_str(), "rb");
                if (!file) {
                    result.success = false;
                    result.message = "Failed to open file: " + filePath;
                    break;
                }
                fclose(file);
                uint32_t address = std::stoul(args[1], nullptr, 0);
                size_t sentSize = send_file(io, filePath.c_str(), address, 0, 528, 0, 0);
                if (sentSize > 0) {
                    result.success = true;
                    result.message = "File sent successfully";
                } else {
                    result.success = false;
                    result.message = "Failed to send file";
                }
            }
            break;
        case CMD_EXECUTE:
            uint32_t execAddr = std::stoul(args[0], nullptr, 0);
            if (!isNoFDLMode)
            {
                if (GetStage() == BROM)
                {
                    if (args.size() < 1) {
                        result.success = false;
                        result.message = "EXECUTE command requires 1 argument: address";
                    }
                    if (isCVE)
                    {
                        if (!execfile) {
                            result.success = false;
                            result.message = "CVE file path not set";
                            break;
                        }
                        if (!cve_addr) {
                            result.success = false;
                            result.message = "CVE address not set";
                            break;
                        }
                        if (isCVEv2)
                        {
                            DEG_LOG(I, "Using CVEv2 binary: %s at address: %s", execfile, cve_addr);
                            
                            size_t execsize = send_file(io, execfile, cve_addr, 0, 528, 0, 0);
                            int n, gapsize = exec_addr - cve_addr - execsize;
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
                            result.success = true;
                            result.message = "CVE v2 sent successfully";
                            break;
                        }
                        else
                        {
                            DEG_LOG(I, "Using CVE binary: %s at address: %s", execfile, cve_addr);
                            size_t sentSize = send_file(io, execfile, cve_addr, 0, 528, 0, 0);
                            if (sentSize == 0) {
                                result.success = false;
                                result.message = "Failed to send CVE file";
                                break;
                            }
                            else
                            {
                                result.success = true;
                                result.message = "CVE file sent successfully";
                                break;
                            }
                        }
                    }
                    else
                    {
                        encode_msg_nocpy(io, BSL_CMD_EXEC_DATA, 0);
                        if (send_and_check(io)) ERR_EXIT("FDL exec failed\n");
                    }
        
                    
                    if (execAddr == 0x5500 || execAddr == 0x65000800) {
                        highspeed = 1;
                        if (!baudrate) baudrate = 921600;
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
                    result.success = true;
                    result.message = "FDL1 executed successfully";
                    fdl1_loaded = 1;
                }
                else if (GetStage() == FDL1)
                {
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
                        DEG_LOG(I, "Device storage is nand.");
                    }
                    if (g_app_state.flash.gpt_failed != 1) {
                        if (g_app_state.flash.selected_ab == 2) {

                            DEG_LOG(I, "Device is using slot b\n");
                            
                        }
                        else if (g_app_state.flash.selected_ab == 1) {

                            DEG_LOG(I, "Device is using slot a\n");
                            
                        }
                        else {
                            DEG_LOG(I, "Device is not using VAB\n");
                            
                            if (Da_Info.bSupportRawData) {
                                DEG_LOG(I, "Raw data mode is supported (level is %u) ,but DISABLED for stability, you can set it manually.", (unsigned)Da_Info.bSupportRawData);
                                Da_Info.bSupportRawData = 0;
                            }
                        }
                    }
                    if (isUseCptable) {
                        io->Cptable = partition_list_d(io);
                        isCMethod = 1;
                    }
                    if (!io->part_count && !io->part_count_c) {
                        DEG_LOG(W, "No partition table found on current device");
                    }
                    if (nand_id == DEFAULT_NAND_ID) {
                        nand_info[0] = (uint8_t)pow(2, nand_id & 3); //page size
                        nand_info[1] = 32 / (uint8_t)pow(2, (nand_id >> 2) & 3); //spare area size
                        nand_info[2] = 64 * (uint8_t)pow(2, (nand_id >> 4) & 3); //block size
                    }
                    fdl2_executed = 1;
                }
            }
            else
            {
                if (g_app_state.device.device_mode == SPRD3) { DEG_LOG(E, "Direct execute not supported in SPRD3 mode"); result.success = false; result.message = "Direct execute not supported in SPRD3 mode"; break; }
                if (GetStage() == BROM)
                {
                    encode_msg_nocpy(io, BSL_CMD_EXEC_DATA, 0);
                    if (send_and_check(io)) ERR_EXIT("FDL exec failed\n");
        
                    
                    if (execAddr == 0x5500 || execAddr == 0x65000800) {
                        highspeed = 1;
                        if (!baudrate) baudrate = 921600;
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
                        DEG_LOG(I, "Device storage is nand.");
                    }
                    if (g_app_state.flash.gpt_failed != 1) {
                        if (g_app_state.flash.selected_ab == 2) {

                            DEG_LOG(I, "Device is using slot b\n");
                            
                        }
                        else if (g_app_state.flash.selected_ab == 1) {

                            DEG_LOG(I, "Device is using slot a\n");
                            
                        }
                        else {
                            DEG_LOG(I, "Device is not using VAB\n");
                            
                            if (Da_Info.bSupportRawData) {
                                DEG_LOG(I, "Raw data mode is supported (level is %u) ,but DISABLED for stability, you can set it manually.", (unsigned)Da_Info.bSupportRawData);
                                Da_Info.bSupportRawData = 0;
                            }
                        }
                    }
                    if (isUseCptable) {
                        io->Cptable = partition_list_d(io);
                        isCMethod = 1;
                    }
                    if (!io->part_count && !io->part_count_c) {
                        DEG_LOG(W, "No partition table found on current device");
                    }
                    if (nand_id == DEFAULT_NAND_ID) {
                        nand_info[0] = (uint8_t)pow(2, nand_id & 3); //page size
                        nand_info[1] = 32 / (uint8_t)pow(2, (nand_id >> 2) & 3); //spare area size
                        nand_info[2] = 64 * (uint8_t)pow(2, (nand_id >> 4) & 3); //block size
                    }
                    fdl2_executed = 1;
            }
            break;
        case CMD_SET_BAUDRATE:
            if (args.size() < 1) {
                result.success = false;
                result.message = "SET_BAUDRATE command requires 1 argument: baudrate";
            } else {
#if !USE_LIBUSB
                if (std::stoul(args[0], nullptr, 0) == 0) {
                    result.success = false;
                    result.message = "Baudrate cannot be zero";
                    break;
                }
                baudrate = std::stoul(args[0], nullptr, 0);
				if (fdl2_executed) call_SetProperty(io->handle, 0, 100, (LPCVOID)&baudrate);
			    DEG_LOG(I, "Baudrate is %u", baudrate);
                result.success = true;
                result.message = "Baudrate set successfully";
#else
                result.success = false;
                result.message = "Changing baudrate is not supported in libusb mode";
#endif
            }
            break;
        case CMD_SET_EXEC_ADDR:
            if (args.size() < 1) {
                result.success = false;
                result.message = "SET_EXEC_ADDR command requires 1 argument: address";
            } else {
                cve_addr = std::stoul(args[0], nullptr, 0);
                isCVE = true;
                result.success = true;
                result.message = "Execution address set successfully";
            }
            break;
        case CMD_SET_EXEC_ADDR_V2:
            if (args.size() < 1) {
                result.success = false;
                result.message = "SET_EXEC_ADDR_V2 command requires 1 argument: address";
            } else {
                cve_addr = std::stoul(args[0], nullptr, 0);
                isCVE = true;
                isCVEv2 = true;
                result.success = true;
                result.message = "Execution address for CVEv2 set successfully";
            }
            break;
        case CMD_REPARTITION:
            if (args.size() < 1) {
                result.success = false;
                result.message = "REPARTITION command requires 1 argument: partition table path";
            } else {
                FILE *partFile = fopen(args[0].c_str(), "rb");
                if (!partFile) {
                    result.success = false;
                    result.message = "Failed to open partition table file: " + args[0];
                    break;
                }
                fclose(partFile);
                repartition(io, args[0].c_str());
                result.success = true;
                result.message = "Repartition command executed successfully";
            }
            break;
        default:
            result.success = false;
            result.message = "Unknown command";
    }
    return result;
}
