#include "usb_transport.h"
#include "../common.h"

#if !USE_LIBUSB

DWORD curPort = 0;

DWORD *FindPort(const char *USB_DL) {
	const GUID GUID_DEVCLASS_PORTS = { 0x4d36e978, 0xe325, 0x11ce,{0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18} };
	HDEVINFO DeviceInfoSet;
	SP_DEVINFO_DATA DeviceInfoData;
	DWORD dwIndex = 0;
	DWORD count = 0;
	DWORD *ports = nullptr;

	DeviceInfoSet = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);

	if (DeviceInfoSet == INVALID_HANDLE_VALUE) {
		DEG_LOG(E,"Falied to get device info set: %s",GetLastError());
		return 0;
	}

	DeviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

	while (SetupDiEnumDeviceInfo(DeviceInfoSet, dwIndex, &DeviceInfoData)) {
		char friendlyName[MAX_PATH];
		DWORD dataType = 0;
		DWORD dataSize = sizeof(friendlyName);

		SetupDiGetDeviceRegistryPropertyA(DeviceInfoSet, &DeviceInfoData, SPDRP_FRIENDLYNAME, &dataType, (BYTE *)friendlyName, dataSize, &dataSize);
		char *result = strstr(friendlyName, USB_DL);
		if (result != nullptr) {
			char portNum_str[4];
			strncpy(portNum_str, result + strlen(USB_DL) + 5, 3);
			portNum_str[3] = 0;

			DWORD portNum = strtoul(portNum_str, nullptr, 0);
			DWORD *temp = (DWORD *)realloc(ports, (count + 2) * sizeof(DWORD));
			if (temp == nullptr) {
				DEG_LOG(E,"Memory allocation failed.");
				SetupDiDestroyDeviceInfoList(DeviceInfoSet);
				free(ports);
				ports = nullptr;
				return nullptr;
			}
			ports = temp;
			ports[count] = portNum;
			count++;
		}
		++dwIndex;
	}

	SetupDiDestroyDeviceInfoList(DeviceInfoSet);
	if (count > 0) ports[count] = 0;
	return ports;
}
#else
libusb_device *curPort = nullptr;
libusb_device **FindPort(int pid) {
	libusb_device **devs;
	int usb_cnt, count = 0;
	libusb_device **ports = nullptr;

	usb_cnt = libusb_get_device_list(nullptr, &devs);
	if (usb_cnt < 0) {
		DEG_LOG(E,"Get device list error");
		return nullptr;
	}
	for (int i = 0; i < usb_cnt; i++) {
		libusb_device *dev = devs[i];
		struct libusb_device_descriptor desc;
		int r = libusb_get_device_descriptor(dev, &desc);
		if (r < 0) {
			DEG_LOG(E,"Failed to get device descriptor");
			continue;
		}
		if (desc.idVendor == 0x1782 && (pid == 0 || desc.idProduct == pid)) {
			libusb_device **temp = (libusb_device **)realloc(ports, (count + 2) * sizeof(libusb_device *));
			if (temp == nullptr) {
				DEG_LOG(E,"Memory allocation failed.");
				libusb_free_device_list(devs, 1);
				free(ports);
				ports = nullptr;
				return nullptr;
			}
			ports = temp;
			ports[count++] = dev;
			libusb_ref_device(dev);
		}
	}
	libusb_free_device_list(devs, 11);
	if (count > 0) ports[count] = nullptr;
	return ports;
}
#endif

#if USE_LIBUSB
void find_endpoints(libusb_device_handle *dev_handle, int result[4]) {
	int endp_in = -1, endp_out = -1, endp_in_blk = 0, endp_out_blk = 0;
	int i, k, err;
	//struct libusb_device_descriptor desc;
	struct libusb_config_descriptor *config;
	libusb_device *device = libusb_get_device(dev_handle);
	if (!device)
		ERR_EXIT("libusb_get_device failed\n");
	//if (libusb_get_device_descriptor(device, &desc) < 0)
	// ERR_EXIT("libusb_get_device_descriptor failed");
	err = libusb_get_config_descriptor(device, 0, &config);
	if (err < 0)
		ERR_EXIT("libusb_get_config_descriptor failed : %s\n", libusb_error_name(err));

	for (k = 0; k < config->bNumInterfaces; k++) {
		const struct libusb_interface *interface;
		const struct libusb_interface_descriptor *interface_desc;
		int claim = 0;
		interface = config->interface + k;
		if (interface->num_altsetting < 1) continue;
		interface_desc = interface->altsetting + 0;
		for (i = 0; i < interface_desc->bNumEndpoints; i++) {
			const struct libusb_endpoint_descriptor *endpoint;
			endpoint = interface_desc->endpoint + i;
			if (endpoint->bmAttributes == 2) {
				int addr = endpoint->bEndpointAddress;
				err = 0;
				if (addr & 0x80) {
					if (endp_in >= 0) ERR_EXIT("more than one endp_in\n");
					endp_in = addr;
					endp_in_blk = endpoint->wMaxPacketSize;
					claim = 1;
				}
				else {
					if (endp_out >= 0) ERR_EXIT("more than one endp_out\n");
					endp_out = addr;
					endp_out_blk = endpoint->wMaxPacketSize;
					claim = 1;
				}
			}
		}
		if (claim) {
			i = interface_desc->bInterfaceNumber;
#if LIBUSB_DETACH
			err = libusb_kernel_driver_active(dev_handle, i);
			if (err > 0) {
				DEG_LOG(OP,"kernel driver is active, trying to detach");
				err = libusb_detach_kernel_driver(dev_handle, i);
				if (err < 0)
					ERR_EXIT("libusb_detach_kernel_driver failed : %s\n", libusb_error_name(err));
			}
#endif
			err = libusb_claim_interface(dev_handle, i);
			if (err < 0)
				ERR_EXIT("libusb_claim_interface failed : %s\n", libusb_error_name(err));
			break;
		}
	}
	if (endp_in < 0) ERR_EXIT("endp_in not found\n");
	if (endp_out < 0) ERR_EXIT("endp_out not found\n");
	libusb_free_config_descriptor(config);

	//DBG_LOG("USB endp_in=%02x, endp_out=%02x\n", endp_in, endp_out);

	result[0] = endp_in;
	result[1] = endp_out;
	result[2] = endp_in_blk;
	result[3] = endp_out_blk;
}
#endif

namespace {

// 基于 spdio_t 的默认传输实现，承载现有 USB 读写逻辑。
class SpdioUsbTransport : public IUsbTransport {
public:
	explicit SpdioUsbTransport(spdio_t *io) : io_(io) {}

	int send(const uint8_t *buf, int len, int timeout_ms) override;
	int recv(uint8_t *buf, int max_len, int timeout_ms) override;
	int clear() override;

private:
	spdio_t *io_;
};

int SpdioUsbTransport::send(const uint8_t *buf, int len, int timeout_ms) {
	if (!io_ || !buf || len <= 0) return 0;
	if (m_bOpened == -1)
		return -1;

#if USE_LIBUSB
	int transferred = 0;
	unsigned char *data = const_cast<unsigned char *>(buf);
	int err = libusb_bulk_transfer(io_->dev_handle, io_->endp_out,
		data, len, &transferred, timeout_ms);
	if (err < 0) {
		DEG_LOG(E,"usb_send failed : %s", libusb_error_name(err));
		return err;
	}
	// UMS9117 waits太长 after an integer multiple byte block.
	if (io_->endp_out_blk > 0 && ((unsigned)len % io_->endp_out_blk) == 0) {
		int dummy = 0;
		(void)libusb_bulk_transfer(io_->dev_handle, io_->endp_out, nullptr, 0, &dummy, timeout_ms);
	}
	return transferred;
#else
	return call_Write(io_->handle, buf, len);
#endif
}

int SpdioUsbTransport::recv(uint8_t *buf, int max_len, int timeout_ms) {
	if (!io_ || !buf || max_len <= 0) return 0;
	if (m_bOpened == -1)
		return -1;

#if USE_LIBUSB
	int transferred = 0;
	int err = libusb_bulk_transfer(io_->dev_handle, io_->endp_in,
		buf, max_len, &transferred, timeout_ms);
	if (err == LIBUSB_ERROR_NO_DEVICE) {
		DEG_LOG(W,"connection closed");
		return err;
	} else if (err < 0) {
		DEG_LOG(E,"usb_recv failed : %s", libusb_error_name(err));
		return err;
	}
	return transferred;
#else
	int ret = call_Read(io_->handle, buf, max_len, timeout_ms);
	if (ret < 0) {
		DEG_LOG(E,"usb_recv failed, ret = %d", ret);
	}
	return ret;
#endif
}

int SpdioUsbTransport::clear() {
	if (!io_) return 0;

#if !USE_LIBUSB
	return call_Clear(io_->handle);
#else
	return 0;
#endif
}

} // namespace

spdio_t *spdio_init(int flags) {
	size_t total_size = sizeof(spdio_t) + RECV_BUF_LEN + (4 + 0x10000 + 2) * 4 + 4;
	uint8_t *p = NEWN uint8_t[total_size];
	if (p == nullptr) {
		DEG_LOG(E, "Memory allocation failed: insufficient memory");
		return nullptr;
	}

	spdio_t* io = reinterpret_cast<spdio_t*>(p);
	memset(io, 0, sizeof(spdio_t));
	p += sizeof(spdio_t);
	io->flags = flags;
	io->transport = NEWN SpdioUsbTransport(io);
	if (!io->transport) {
		DEG_LOG(E, "Memory allocation failed: transport adapter");
		delete[] reinterpret_cast<uint8_t*>(io);
		return nullptr;
	}
	io->recv_buf = p; p += RECV_BUF_LEN;
	io->raw_buf = p; p += 4 + 0x10000 + 2;
	io->temp_buf = p + 5;
	io->untranscode_buf = p; p += 4 + 0x10000 + 4;
	io->enc_buf = p;
	io->timeout = 3000;
	io->nor_bar = 0;
	memset(io->recv_buf, 0, 8);
	return io;
}

void spdio_free(spdio_t *io) {
	if (!io) return;
	if (io->transport) {
		delete io->transport;
		io->transport = nullptr;
	}
#if _WIN32
	if (!g_app_state.transport.bListenLibusb) {
		PostThreadMessage(io->iThread, WM_QUIT, 0, 0);
		WaitForSingleObject(io->hThread, INFINITE);
		CloseHandle(io->hThread);
	}
#endif
#if USE_LIBUSB
	if (g_app_state.transport.bListenLibusb) stopUsbEventHandle();
	libusb_close(io->dev_handle);
	libusb_exit(nullptr);
#else
	call_DisconnectChannel(io->handle);
	if (io->m_dwRecvThreadID) DestroyRecvThread(io);
	call_Uninitialize(io->handle);
	destroyClass(io->handle);
#endif
	delete[](io->ptable);
	delete[](io->Cptable);
	delete[](io);
}

int recv_read_data(spdio_t *io) {
	IUsbTransport *t = spdio_get_transport(io);
	int len = usb_transport_recv(t, io->recv_buf, RECV_BUF_LEN, io->timeout);
	if (len <= 0) {
		if (len < 0) {
			DEG_LOG(E,"usb_recv failed, ret = %d", len);
		}
		return 0;
	}

	if (io->verbose >= 2) {
		DEG_LOG(OP,"recv (%d):", len);
		print_mem(stderr, io->recv_buf, len);
	}
	io->recv_len = len;
	return len;
}

IUsbTransport *spdio_get_transport(spdio_t *io) {
	// T2-01 深化：现在 spdio_t 持有一个真正的 IUsbTransport 实例
	return io ? io->transport : nullptr;
}

int usb_transport_send(IUsbTransport *t, const uint8_t *buf, int len, int timeout_ms) {
	if (!t) return 0;
	return t->send(buf, len, timeout_ms);
}

int usb_transport_recv(IUsbTransport *t, uint8_t *buf, int max_len, int timeout_ms) {
	if (!t) return 0;
	return t->recv(buf, max_len, timeout_ms);
}

int usb_transport_clear(IUsbTransport *t) {
	if (!t) return 0;
	return t->clear();
}

#if _WIN32
const _TCHAR CLASS_NAME[] = _T("Sample Window Class");

HWND g_hWnd;

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	static BOOL interface_checked = FALSE;
	static BOOL is_diag = FALSE;
	switch (message) {
	case WM_DEVICECHANGE:
		if (DBT_DEVICEARRIVAL == wParam || DBT_DEVICEREMOVECOMPLETE == wParam) {
			PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR)lParam;
			PDEV_BROADCAST_DEVICEINTERFACE pDevInf;
			PDEV_BROADCAST_PORT pDevPort;
			switch (pHdr->dbch_devicetype) {
			case DBT_DEVTYP_DEVICEINTERFACE:
				pDevInf = (PDEV_BROADCAST_DEVICEINTERFACE)pHdr;
#if USE_LIBUSB
				if (DBT_DEVICEREMOVECOMPLETE == wParam) {
					libusb_device **currentports = FindPort(0);
					if (currentports == nullptr) m_bOpened = -1;
					else {
						libusb_device **port = currentports;
						while (*port != nullptr) {
							if (curPort == *port) break;
							port++;
						}
						if (*port == nullptr) m_bOpened = -1;
						libusb_free_device_list(currentports, 1);
						currentports = nullptr;
					}
				}
#else
				if (my_strstr(pDevInf->dbcc_name, _T("VID_1782&PID_4D00"))) interface_checked = TRUE;
				else if (my_strstr(pDevInf->dbcc_name, _T("VID_1782&PID_4D03"))) {
					interface_checked = TRUE;
					is_diag = TRUE;
				}
#endif
				break;
#if !USE_LIBUSB
			case DBT_DEVTYP_PORT:
				if (interface_checked) {
					pDevPort = (PDEV_BROADCAST_PORT)pHdr;
					DWORD changedPort = my_strtoul(pDevPort->dbcp_name + 3, nullptr, 0);
					if (DBT_DEVICEARRIVAL == wParam) {
						if (!curPort) {
							if (is_diag) {
								DWORD *currentports = FindPort("SPRD DIAG");
								if (currentports) {
									for (DWORD *port = currentports; *port != 0; port++) {
										if (changedPort == *port) {
											curPort = changedPort;
											break;
										}
									}
									delete[](currentports);
									currentports = nullptr;
								}
							}
							else {
								curPort = changedPort;
							}
						}
					}
					else {
						if (curPort == changedPort) m_bOpened = -1; // no need to judge changedPort for DBT_DEVICEREMOVECOMPLETE
					}
					interface_checked = FALSE;
					is_diag = FALSE;
				}
				break;
#endif
			}
		}
	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}

DWORD WINAPI ThrdFunc(LPVOID lpParam) {
	WNDCLASS wc = { 0 };
	wc.lpfnWndProc = WndProc;
	wc.hInstance = GetModuleHandle(nullptr);
	wc.lpszClassName = CLASS_NAME;
	if (0 == RegisterClass(&wc)) return -1;

	g_hWnd = CreateWindowEx(0, CLASS_NAME, _T(""), WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		nullptr, // Parent window
		nullptr, // Menu
		GetModuleHandle(nullptr), // Instance handle
		nullptr // Additional application data
	);
	if (g_hWnd == nullptr) return -1;

	DEV_BROADCAST_DEVICEINTERFACE NotificationFilter;
#if USE_LIBUSB
	const GUID GUID_DEVINTERFACE = { 0xa5dcbf10, 0x6530, 0x11d2, { 0x90, 0x1f, 0x00, 0xc0, 0x4f, 0xb9, 0x51, 0xed } };
#else
	const GUID GUID_DEVINTERFACE = { 0x86e0d1e0, 0x8089, 0x11d0, { 0x9c, 0xe4, 0x08, 0x00, 0x3e, 0x30, 0x1f, 0x73 } };
#endif
	ZeroMemory(&NotificationFilter, sizeof(NotificationFilter));
	NotificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
	NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
	NotificationFilter.dbcc_classguid = GUID_DEVINTERFACE;
	if (RegisterDeviceNotification(g_hWnd, &NotificationFilter, DEVICE_NOTIFY_WINDOW_HANDLE) == nullptr) return -1;

	MSG msg;
	while (GetMessage(&msg, nullptr, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return 0;
}
#endif

#if !USE_LIBUSB
DWORD WINAPI RcvDataThreadProc(LPVOID lpParam) {
	spdio_t *io = (spdio_t *)lpParam;
	static int plen = 6;

	MSG msg;
	PeekMessage(&msg, nullptr, 0, 0, PM_NOREMOVE);

	SetEvent(io->m_hRecvThreadState);

	while (GetMessage(&msg, nullptr, 0, 0)) {
		switch (msg.message) {
		case WM_RCV_CHANNEL_DATA:
			if (io->verbose >= 2) {
				DEG_LOG(OP,"recv (%d):", (int)msg.lParam);
				print_mem(stderr, (const uint8_t *)msg.wParam, (int)msg.lParam);
			}
			if (recv_transcode(io, (const uint8_t *)msg.wParam, (int)msg.lParam, &plen)) {
				if (plen == io->raw_len) {
					if (recv_check_crc(io)) {
						plen = 6;
						SetEvent(io->m_hOprEvent);
					}
				}
			}
			call_FreeMem(io->handle, (LPVOID)msg.wParam);
			break;
		default:
			break;
		}
	}

	SetEvent(io->m_hRecvThreadState);

	return 0;
}

BOOL CreateRecvThread(spdio_t *io) {
	io->m_hRecvThreadState = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	io->m_hOprEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	io->m_hRecvThread = CreateThread(nullptr, 0, RcvDataThreadProc, io, 0, &io->m_dwRecvThreadID);
	if (io->m_hRecvThreadState == nullptr || io->m_hOprEvent == nullptr || io->m_hRecvThread == nullptr) {
		return FALSE;
	}

	DWORD bWaitCode = WaitForSingleObject(io->m_hRecvThreadState, 5000);
	if (bWaitCode != WAIT_OBJECT_0) {
		return FALSE;
	}
	else {
		ResetEvent(io->m_hRecvThreadState);
	}
	return TRUE;
}

void DestroyRecvThread(spdio_t *io) {
	if (io->m_hRecvThread == nullptr) {
		return;
	}

	PostThreadMessage(io->m_dwRecvThreadID, WM_QUIT, 0, 0);

	WaitForSingleObject(io->m_hRecvThreadState, INFINITE);
	ResetEvent(io->m_hRecvThreadState);

	if (io->m_hRecvThread) {
		CloseHandle(io->m_hRecvThread);
		io->m_hRecvThread = nullptr;
	}

	if (io->m_hRecvThreadState) {
		CloseHandle(io->m_hRecvThreadState);
		io->m_hRecvThreadState = nullptr;
	}

	if (io->m_hOprEvent) {
		CloseHandle(io->m_hOprEvent);
		io->m_hOprEvent = nullptr;
	}

	io->m_dwRecvThreadID = 0;
}
#else
#ifndef _MSC_VER
pthread_t gUsbEventThrd;
libusb_hotplug_callback_handle gHotplugCbHandle = 0;

// SPRD DIAG, bInterfaceNumber 0
// SPRD LOG, bInterfaceNumber 1
// Since find_endpoints() ignored bInterfaceNumber 1, 0x4d03 works in HotplugCbFunc()
int HotplugCbFunc(libusb_context *ctx, libusb_device *device, libusb_hotplug_event event, void *user_data) {
	(void)ctx; (void)user_data;
	if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED) { if (!curPort) curPort = device; }
	else { if (curPort == device) m_bOpened = -1; }
	return 0;
}

void *UsbThrdFunc(void *param) {
	(void)param;
	int ret;
	while (g_app_state.transport.bListenLibusb) {
		ret = libusb_handle_events(nullptr);
		if (ret < 0)
			DEG_LOG(E,"libusb_handle_events() failed: %s", libusb_error_name(ret));
	}
	return nullptr;
}

void startUsbEventHandle(void) {
	int ret = libusb_hotplug_register_callback(
		nullptr,
		LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED | LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT,
		LIBUSB_HOTPLUG_NO_FLAGS,
		0x1782,
		LIBUSB_HOTPLUG_MATCH_ANY,
		LIBUSB_HOTPLUG_MATCH_ANY,
		HotplugCbFunc,
		nullptr,
		&gHotplugCbHandle);
	if (ret != LIBUSB_SUCCESS) ERR_EXIT("libusb_hotplug_register_callback failed, error: %d\n", ret);

	ret = pthread_create(&gUsbEventThrd, nullptr, UsbThrdFunc, nullptr);
	if (ret != 0) {
		libusb_hotplug_deregister_callback(nullptr, gHotplugCbHandle);
		ERR_EXIT("Failed to create thread, error: %d\n", ret);
	}

	g_app_state.transport.bListenLibusb = 1;
}

void stopUsbEventHandle(void) {
	g_app_state.transport.bListenLibusb = 0;
	libusb_hotplug_deregister_callback(nullptr, gHotplugCbHandle);

	int ret = pthread_join(gUsbEventThrd, nullptr);
	if (ret != 0) DEG_LOG(E,"Failed to join thread, error: %d", ret);
}
#else
void startUsbEventHandle(void) {
	DEG_LOG(I,"startUsbEventHandle() is not supported in MSVC. Please use MSYS2 if you need it.");
}

void stopUsbEventHandle(void) {
	DEG_LOG(I,"stopUsbEventHandle() is not supported in MSVC. Please use MSYS2 if you need it.");
}
#endif

void call_Initialize_libusb(spdio_t *io) {
	int endpoints[4];
	find_endpoints(io->dev_handle, endpoints);
	io->endp_in = endpoints[0];
	io->endp_out = endpoints[1];
	io->endp_in_blk = endpoints[2];
	io->endp_out_blk = endpoints[3];
	int ret = libusb_control_transfer(io->dev_handle, 0x21, 34, 0x601, 0, nullptr, 0, io->timeout);
	if (ret < 0) ERR_EXIT("libusb_control_transfer failed : %s\n", libusb_error_name(ret));
	DEG_LOG(I,"libusb_control_transfer ok");
	m_bOpened = 1;
}
#endif

#if !USE_LIBUSB
void ChangeMode(spdio_t *io, int ms, int bootmode, int at) {
	if (bootmode >= 0x80) ERR_EXIT("mode not exist\n");
	DWORD bytes_written, bytes_read;
	int done = 0;

	while (done != 1) {
		DBG_LOG("<waiting for connection,mode:cali/boot/dl,%ds>\n", ms / 1000);
		for (int i = 0; ; i++) {
			if (curPort) {
				if (!call_ConnectChannel(io->handle, curPort, WM_RCV_CHANNEL_DATA, io->m_dwRecvThreadID)) ERR_EXIT("Connection failed\n");
				break;
			}
			if (100 * i >= ms) ERR_EXIT("find port failed\n");
			usleep(100000);
		}

		uint8_t payload[10] = { 0x7e,0,0,0,0,8,0,0xfe,0,0x7e };
		if (!bootmode) {
			uint8_t hello[10] = { 0x7e,0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e };

			// 这里是串口直写 hello 包，属于 Boot/握手逻辑。
			// 后续可考虑改为使用 spd_protocol 提供的握手 API。
			if (!(bytes_written = call_Write(io->handle, hello, sizeof(hello)))) ERR_EXIT("Error writing to serial port\n");
			if (io->verbose >= 2) {
				DEG_LOG(OP,"send (%d):", (int)sizeof(hello));
				print_mem(stderr, hello, sizeof(hello));
			}
			if (!(bytes_read = call_Read(io->handle, io->recv_buf, RECV_BUF_LEN, io->timeout))) ERR_EXIT("read response from boot mode failed\n");
			if (io->verbose >= 2) {
				DEG_LOG(OP,"read (%d):", bytes_read);
				print_mem(stderr, io->recv_buf, bytes_read);
			}
			if (spd_boot_update_crc_and_stage(io, bytes_read)) {
				return;
			}
			payload[8] = 0x82;
		}
		else if (at) payload[8] = 0x81;
		else payload[8] = bootmode + 0x80;

		if (!(bytes_written = call_Write(io->handle, payload, sizeof(payload)))) ERR_EXIT("Error writing to serial port\n");
		if (io->verbose >= 2) {
			DEG_LOG(OP,"send (%d):", (int)sizeof(payload));
			print_mem(stderr, payload, sizeof(payload));
		}
		if ((bytes_read = call_Read(io->handle, io->recv_buf, RECV_BUF_LEN, io->timeout))) {
			if (io->verbose >= 2) {
				DEG_LOG(OP,"read (%d):", bytes_read);
				print_mem(stderr, io->recv_buf, bytes_read);
			}
			if (spd_boot_update_crc_and_stage(io, bytes_read)) {
				if (io->recv_buf[2] == BSL_REP_VER) { if (io->recv_buf[9] < '4') return; }
				else return;
			}
			else if (io->recv_buf[2] != 0x7e) {
				uint8_t autod[] = { 0x7e,0,0,0,0,0x20,0,0x68,0,0x41,0x54,0x2b,0x53,0x50,0x52,0x45,0x46,0x3d,0x22,0x41,0x55,0x54,0x4f,0x44,0x4c,0x4f,0x41,0x44,0x45,0x52,0x22,0xd,0xa,0x7e };
				usleep(500000);
				if ((bytes_written = call_Write(io->handle, autod, sizeof(autod)))) {
					if (io->verbose >= 2) {
						DEG_LOG(OP,"send (%d):", (int)sizeof(autod));
						print_mem(stderr, autod, sizeof(autod));
					}
					if ((bytes_read = call_Read(io->handle, io->recv_buf, RECV_BUF_LEN, io->timeout))) {
						if (io->verbose >= 2) {
							DEG_LOG(OP,"read (%d):", bytes_read);
							print_mem(stderr, io->recv_buf, bytes_read);
						}
						done = -1;
					}
				}
			}
		}

#else
void ChangeMode(spdio_t *io, int ms, int bootmode, int at) {
	int err, bytes_written, bytes_read;
	if (bootmode >= 0x80) ERR_EXIT("mode not exist\n");
	int done = 0;

	while (done != 1) {
		DBG_LOG("<waiting for connection,mode:cali/boot/dl,%ds>\n", ms / 1000);
		for (int i = 0; ; i++) {
			if (curPort) {
				if (libusb_open(curPort, &io->dev_handle) < 0) ERR_EXIT("Connection falied");
				call_Initialize_libusb(io);
				break;
			}
			if (100 * i >= ms) ERR_EXIT("find port failed\n");
			usleep(100000);
		}
		uint8_t payload[10] = { 0x7e,0,0,0,0,8,0,0xfe,0,0x7e };
		if (!bootmode) {
			uint8_t hello[10] = { 0x7e,0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e };

			err = libusb_bulk_transfer(io->dev_handle, io->endp_out, hello, sizeof(hello), &bytes_written, io->timeout);
			if (err < 0)
				ERR_EXIT("usb_send failed : %s\n", libusb_error_name(err));
			if (io->verbose >= 2) {
				DEG_LOG(OP,"send (%d):", (int)sizeof(hello));
				print_mem(stderr, hello, sizeof(hello));
			}
			err = libusb_bulk_transfer(io->dev_handle, io->endp_in, io->recv_buf, RECV_BUF_LEN, &bytes_read, io->timeout);
			if (err == LIBUSB_ERROR_NO_DEVICE)
				ERR_EXIT("connection closed\n");
			else if (err < 0)
				ERR_EXIT("usb_recv failed : %s\n", libusb_error_name(err));
			if (!bytes_read) ERR_EXIT("read response from boot mode failed\n");
			if (io->verbose >= 2) {
				DEG_LOG(OP,"read (%d):", bytes_read);
				print_mem(stderr, io->recv_buf, bytes_read);
			}
			if (io->recv_buf[2] == BSL_REP_VER ||
				io->recv_buf[2] == BSL_REP_VERIFY_ERROR ||
				io->recv_buf[2] == BSL_REP_UNSUPPORTED_COMMAND) {
				int chk1, chk2, a = READ16_BE(io->recv_buf + bytes_read - 3);
				chk1 = spd_crc16(0, io->recv_buf + 1, bytes_read - 4);
				if (a == chk1) io->flags |= FLAGS_CRC16;
				else {
					chk2 = spd_checksum(0, io->recv_buf + 1, bytes_read - 4, CHK_ORIG);
					if (a == chk2) fdl1_loaded = 1;
					else ERR_EXIT("bad checksum (0x%04x, expected 0x%04x or 0x%04x)\n", a, chk1, chk2);
				}
				return;
			}
			payload[8] = 0x82;
		}
		else if (at) payload[8] = 0x81;
		else payload[8] = bootmode + 0x80;

		err = libusb_bulk_transfer(io->dev_handle, io->endp_out, payload, sizeof(payload), &bytes_written, io->timeout);
		if (err < 0)
			ERR_EXIT("usb_send failed : %s\n", libusb_error_name(err));
		if (io->verbose >= 2) {
			DEG_LOG(OP,"send (%d):", (int)sizeof(payload));
			print_mem(stderr, payload, sizeof(payload));
		}
		err = libusb_bulk_transfer(io->dev_handle, io->endp_in, io->recv_buf, RECV_BUF_LEN, &bytes_read, io->timeout);
		if (err == LIBUSB_ERROR_NO_DEVICE)
			DEG_LOG(W,"connection closed");
		else if (err < 0)
			ERR_EXIT("usb_recv failed : %s\n", libusb_error_name(err));
		else if (bytes_read) {
			if (io->verbose >= 2) {
				DBG_LOG("read (%d):", bytes_read);
				print_mem(stderr, io->recv_buf, bytes_read);
			}
			if (io->recv_buf[2] == BSL_REP_VER ||
				io->recv_buf[2] == BSL_REP_VERIFY_ERROR ||
				io->recv_buf[2] == BSL_REP_UNSUPPORTED_COMMAND) {
				int chk1, chk2, a = READ16_BE(io->recv_buf + bytes_read - 3);
				chk1 = spd_crc16(0, io->recv_buf + 1, bytes_read - 4);
				if (a == chk1) io->flags |= FLAGS_CRC16;
				else {
					chk2 = spd_checksum(0, io->recv_buf + 1, bytes_read - 4, CHK_ORIG);
					if (a == chk2) fdl1_loaded = 1;
					else ERR_EXIT("bad checksum (0x%04x, expected 0x%04x or 0x%04x)\n", a, chk1, chk2);
				}
				if (io->recv_buf[2] == BSL_REP_VER) { if (io->recv_buf[9] < '4') return; }
				else return;
			}
			else if (io->recv_buf[2] != 0x7e) {
				uint8_t autod[] = { 0x7e,0,0,0,0,0x20,0,0x68,0,0x41,0x54,0x2b,0x53,0x50,0x52,0x45,0x46,0x3d,0x22,0x41,0x55,0x54,0x4f,0x44,0x4c,0x4f,0x41,0x44,0x45,0x52,0x22,0xd,0xa,0x7e };
				usleep(500000);
				err = libusb_bulk_transfer(io->dev_handle, io->endp_out, autod, sizeof(autod), &bytes_written, io->timeout);
				if (err >= 0) {
					if (io->verbose >= 2) {
						DEG_LOG(OP,"send (%d):", (int)sizeof(autod));
						print_mem(stderr, autod, sizeof(autod));
					}
					err = libusb_bulk_transfer(io->dev_handle, io->endp_in, io->recv_buf, RECV_BUF_LEN, &bytes_read, io->timeout);
					if (err == LIBUSB_ERROR_NO_DEVICE)
						DEG_LOG(W,"connection closed");
					else if (err < 0)
						ERR_EXIT("usb_recv failed : %s\n", libusb_error_name(err));
					else if (bytes_read) {
						if (io->verbose >= 2) {
							DEG_LOG(OP,"read (%d):", bytes_read);
							print_mem(stderr, io->recv_buf, bytes_read);
						}
						done = -1;
					}
				}
			}
		}
		// TODO(T2-01): 下方逻辑混合了传输层与 SPD Boot/Checksum 逻辑，后续应抽到协议/Session 层。
		for (int i = 0; ; i++) {
			if (m_bOpened == -1) {
				libusb_close(io->dev_handle);
				io->recv_buf[2] = 0;
				curPort = 0;
				m_bOpened = 0;
				if (done == -1) done = 1;
				break;
			}
			if (i >= 100) {
				if (io->recv_buf[2] == BSL_REP_VER) return;
				else ERR_EXIT("kick reboot timeout, reboot your phone by pressing POWER and VOL_UP for 7-10 seconds.\n");
			}
			usleep(100000);
		}
		if (!at) done = 1;
	}
}
#endif

