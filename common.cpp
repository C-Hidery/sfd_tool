#include "common.h"
int isCancel = 0;
bool isHelperInit = false;
GtkWidgetHelper helper;
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
				delete[](ports);
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
				delete[](ports);
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

#ifdef _MSC_VER
void usleep(unsigned int us) {
	Sleep(us / 1000);
}
#endif


//Print message
void DEG_LOG(int type, const char* format, ...) {
    va_list args;
    va_start(args, format);
    // 首先格式化日志消息
    char buffer[1024];
    const char* prefix;
    
    switch(type) {
        case I: prefix = "[INFO] "; break;
        case W: prefix = "[WARN] "; break;
        case E: prefix = "[ERROR] "; break;
        case OP: prefix = "[OPERATION] "; break;
        case DE: prefix = "[DEBUG] "; break;
        default: prefix = "[UNKNOWN] "; break;
    }
    
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    // 输出到控制台
    if (type == I || type == W || type == OP) {
        fprintf(stdout, "%s%s\n", prefix, buffer);
    } else {
        fprintf(stderr, "%s%s\n", prefix, buffer);
    }
    
    // 输出到GUI日志框
    if (isHelperInit) {
        GtkWidget* txtOutput = helper.getWidget("txtOutput");
        if (txtOutput && GTK_IS_TEXT_VIEW(txtOutput)) {
            // 在主线程中更新GUI
            g_idle_add([](gpointer data) -> gboolean {
                char* message = (char*)data;
                
                // 获取文本缓冲区
                GtkWidget* txtOutput = helper.getWidget("txtOutput");
                if (txtOutput && GTK_IS_TEXT_VIEW(txtOutput)) {
                    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(txtOutput));
                    
                    // 获取当前文本
                    GtkTextIter end;
                    gtk_text_buffer_get_end_iter(buffer, &end);
                    
                    // 追加新日志（包含时间戳）
                    time_t now = time(nullptr);
                    struct tm* local_time = localtime(&now);
                    char timestamp[64];
                    strftime(timestamp, sizeof(timestamp), "[%H:%M:%S] ", local_time);
                    
                    gtk_text_buffer_insert(buffer, &end, timestamp, -1);
                    gtk_text_buffer_insert(buffer, &end, message, -1);
                    gtk_text_buffer_insert(buffer, &end, "\n", 1);
                    
                    // 自动滚动到底部
                    GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(
                        GTK_SCROLLED_WINDOW(gtk_widget_get_parent(txtOutput)));
                    gtk_adjustment_set_value(adj, gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj));
                }
                
                free(message);
                return G_SOURCE_REMOVE;
            }, strdup(buffer));
        }
    }
}
extern int bListenLibusb;
extern int m_bOpened;

void print_mem(FILE *f, const uint8_t *buf, size_t len) {
	size_t i; int a, j, n;
	for (i = 0; i < len; i += 16) {
		n = len - i;
		if (n > 16) n = 16;
		for (j = 0; j < n; j++) fprintf(f, "%02x ", buf[i + j]);
		for (; j < 16; j++) fprintf(f, " ");
		fprintf(f, " |");
		for (j = 0; j < n; j++) {
			a = buf[i + j];
			fprintf(f, "%c", a > 0x20 && a < 0x7f ? a : '.');
		}
		fprintf(f, "|\n");
	}
}

void print_string(FILE *f, const void *src, size_t n) {
	size_t i; int a, b = 0;
	const uint8_t *buf = (const uint8_t *)src;
	fprintf(f, "\"");
	for (i = 0; i < n; i++) {
		a = buf[i]; b = 0;
		switch (a) {
		case '"': case '\\': b = a; break;
		case 0: b = '0'; break;
		case '\b': b = 'b'; break;
		case '\t': b = 't'; break;
		case '\n': b = 'n'; break;
		case '\f': b = 'f'; break;
		case '\r': b = 'r'; break;
		}
		if (b) {
			fprintf(f, "\\%c", b);
			
		}
		else if (a >= 32 && a < 127) {
			fprintf(f, "%c", a);
			
		}
		else fprintf(f, "\\x%02x", a);
	}
	fprintf(f, "\"\n");
	
}
int print_to_string(char* dest, size_t dest_size, 
                   const void* src, size_t n, int mode) {
    if (!dest || dest_size == 0 || (!src && n > 0)) {
        return -1;
    }

    const uint8_t* buf = (const uint8_t*)src;
    size_t offset = 0;
    
    // 保留一个字节给结尾的\0
    size_t remaining = dest_size - 1;

    for (size_t i = 0; i < n && remaining > 0; i++) {
        uint8_t c = buf[i];
        int needed = 0;
        
        // 检查需要多少空间
        if (c == '"' || c == '\\') {
            needed = 2;  // \ + 字符
        } else if (c >= 32 && c < 127) {
            needed = 1;  // 可打印字符
        } else {
            switch (c) {
                case 0: case '\b': case '\t': 
                case '\n': case '\f': case '\r':
                    needed = 2;  // 转义序列
                    break;
                default:
                    // 非打印字符
                    switch (mode) {
                        case 0:  // 跳过
                            continue;
                        case 1:  // 十六进制
                            needed = 4;  // \xHH
                            break;
                        case 2:  // 八进制
                            needed = 4;  // \ooo
                            break;
                        default:
                            needed = 1;  // 默认用点号
                            break;
                    }
            }
        }

        // 检查空间是否足够
        if (needed > remaining) {
            // 空间不足，部分写入
            dest[offset] = '\0';
            return offset;
        }

        // 写入字符
        if (c == '"' || c == '\\') {
            dest[offset++] = '\\';
            dest[offset++] = c;
            remaining -= 2;
        } else if (c >= 32 && c < 127) {
            dest[offset++] = c;
            remaining--;
        } else {
            switch (c) {
                case 0:   dest[offset++] = '\\'; dest[offset++] = '0'; break;
                case '\b': dest[offset++] = '\\'; dest[offset++] = 'b'; break;
                case '\t': dest[offset++] = '\\'; dest[offset++] = 't'; break;
                case '\n': dest[offset++] = '\\'; dest[offset++] = 'n'; break;
                case '\f': dest[offset++] = '\\'; dest[offset++] = 'f'; break;
                case '\r': dest[offset++] = '\\'; dest[offset++] = 'r'; break;
                default:
                    if (mode == 1) {
                        // 十六进制格式
                        int written = snprintf(dest + offset, remaining, "\\x%02x", c);
                        if (written > 0) {
                            offset += written;
                            remaining -= written;
                        }
                    } else if (mode == 2) {
                        // 八进制格式
                        int written = snprintf(dest + offset, remaining, "\\%03o", c);
                        if (written > 0) {
                            offset += written;
                            remaining -= written;
                        }
                    } else {
                        // 用点号代替
                        dest[offset++] = '.';
                        remaining--;
                    }
                    break;
            }
        }
    }

    // 添加换行和结束符
    if (remaining >= 2) {
        dest[offset++] = '\n';
        dest[offset] = '\0';
    } else if (remaining == 1) {
        dest[offset] = '\0';
    }
    
    return offset;
}
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

#define RECV_BUF_LEN (0x8000)

char fn_partlist[40] = { 0 };
char savepath[ARGV_LEN] = { 0 };

DA_INFO_T Da_Info;
partition_t gPartInfo;

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
#if _WIN32
	if (!bListenLibusb) {
		PostThreadMessage(io->iThread, WM_QUIT, 0, 0);
		WaitForSingleObject(io->hThread, INFINITE);
		CloseHandle(io->hThread);
	}
#endif
#if USE_LIBUSB
	if (bListenLibusb) stopUsbEventHandle();
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

int spd_transcode(uint8_t *dst, uint8_t *src, int len) {
	int i, a, n = 0;
	for (i = 0; i < len; i++) {
		a = src[i];
		if (a == HDLC_HEADER || a == HDLC_ESCAPE) {
			if (dst) dst[n] = HDLC_ESCAPE;
			n++;
			a ^= 0x20;
		}
		if (dst) dst[n] = a;
		n++;
	}
	return n;
}

int spd_transcode_max(uint8_t *src, int len, int n) {
	int i, a;
	for (i = 0; i < len; i++) {
		a = src[i];
		a = a == HDLC_HEADER || a == HDLC_ESCAPE ? 2 : 1;
		if (n < a) break;
		n -= a;
	}
	return i;
}

unsigned spd_crc16(unsigned crc, const void *src, unsigned len) {
	uint8_t *s = (uint8_t *)src; int i;
	crc &= 0xffff;
	while (len--) {
		crc ^= *s++ << 8;
		for (i = 0; i < 8; i++)
			crc = crc << 1 ^ ((0 - (crc >> 15)) & 0x11021);
	}
	return crc;
}

#define CHK_FIXZERO 1
#define CHK_ORIG 2

unsigned spd_checksum(unsigned crc, const void *src, int len, int final) {
	uint8_t *s = (uint8_t *)src;

	while (len > 1) {
		crc += s[1] << 8 | s[0]; s += 2;
		len -= 2;
	}
	if (len) crc += *s;
	if (final) {
		crc = (crc >> 16) + (crc & 0xffff);
		crc += crc >> 16;
		crc = ~crc & 0xffff;
		if (len < final)
			crc = crc >> 8 | (crc & 0xff) << 8;
	}
	return crc;
}

void encode_msg(spdio_t *io, int type, const void *data, size_t len) {
	uint8_t *p, *p0; unsigned chk;

	if (len > 0xffff)
		ERR_EXIT("message too long\n");

	io->send_buf = io->enc_buf;
	if (type == BSL_CMD_CHECK_BAUD) {
		memset(io->enc_buf, HDLC_HEADER, len);
		io->enc_len = len;
		*(io->untranscode_buf + 1) = HDLC_HEADER;
		return;
	}

	p = p0 = io->untranscode_buf + 1;
	WRITE16_BE(p, type); p += 2;
	WRITE16_BE(p, len); p += 2;
	memcpy(p, data, len); p += len;

	len = p - p0;
	if (io->flags & FLAGS_CRC16)
		chk = spd_crc16(0, p0, len);
	else {
		// if (len & 1) *p++ = 0;
		chk = spd_checksum(0, p0, len, CHK_FIXZERO);
	}
	WRITE16_BE(p, chk); p += 2;

	io->raw_len = len = p - p0;

	if (io->flags & FLAGS_TRANSCODE) {
		p = io->enc_buf;
		*p++ = HDLC_HEADER;
		len = spd_transcode(p, p0, len);
	}
	else {
		p = io->untranscode_buf;
		*p++ = HDLC_HEADER;
		io->send_buf = io->untranscode_buf;
	}
	p[len] = HDLC_HEADER;
	io->enc_len = len + 2;
}

void encode_msg_nocpy(spdio_t *io, int type, size_t len) {
	uint8_t *p, *p0; unsigned chk;

	if (len > 0xffff)
		ERR_EXIT("message too long\n");

	io->send_buf = io->enc_buf;
	if (type == BSL_CMD_CHECK_BAUD) {
		memset(io->enc_buf, HDLC_HEADER, len);
		io->enc_len = len;
		*(io->untranscode_buf + 1) = HDLC_HEADER;
		return;
	}

	p = p0 = io->untranscode_buf + 1;
	WRITE16_BE(p, type); p += 2;
	WRITE16_BE(p, len); p += 2;
	p += len;

	len = p - p0;
	if (io->flags & FLAGS_CRC16)
		chk = spd_crc16(0, p0, len);
	else {
		// if (len & 1) *p++ = 0;
		chk = spd_checksum(0, p0, len, CHK_FIXZERO);
	}
	WRITE16_BE(p, chk); p += 2;

	io->raw_len = len = p - p0;

	if (io->flags & FLAGS_TRANSCODE) {
		p = io->enc_buf;
		*p++ = HDLC_HEADER;
		len = spd_transcode(p, p0, len);
	}
	else {
		p = io->untranscode_buf;
		*p++ = HDLC_HEADER;
		io->send_buf = io->untranscode_buf;
	}
	p[len] = HDLC_HEADER;
	io->enc_len = len + 2;
}
int set_bootloader_status(spdio_t* io,int status) {
	if (check_confirm("set bootloader status")) {
		encode_msg_nocpy(io, status ? YCC_CMD_LOCK_BOOTLOADER : YCC_CMD_UNLOCK_BOOTLOADER, 0);
		send_msg(io);
		recv_msg(io);
		unsigned int ret = recv_type(io);
		if (ret == YCC_REP_SET_BOOTLOADER_SUCCESS) { DEG_LOG(I, "Bootloader status set successfully"); return 1; }
		else { DEG_LOG(E, "Can not set bootloader status."); return 0;}
	}
	return 0;
}
int send_msg(spdio_t *io) {
	int ret;
	if (!io->enc_len)
		ERR_EXIT("empty message\n");

	if (m_bOpened == -1) {
		spdio_free(io);
		ERR_EXIT("device unattached, exiting...\n");
	}
	if (io->verbose >= 2) {
		DEG_LOG(OP,"send (%d):", io->enc_len);
		print_mem(stderr, io->send_buf, io->enc_len);
	}
	else if (io->verbose >= 1) {
		if (*(io->untranscode_buf + 1) == HDLC_HEADER)
			DEG_LOG(OP,"send: check baud");
		else if (io->raw_len >= 4) {
			DEG_LOG(OP,"send: 0x%02x, size: %d",
				READ16_BE(io->untranscode_buf + 1), READ16_BE(io->untranscode_buf + 3));
		}
		else DEG_LOG(E,"send: unknown message");
	}

#if USE_LIBUSB
	int err = libusb_bulk_transfer(io->dev_handle, io->endp_out, io->send_buf, io->enc_len, &ret, io->timeout);
	if (err < 0)
		ERR_EXIT("usb_send failed : %s\n", libusb_error_name(err));
	// UMS9117 waits too long after a Integer multiple byte block.
	if (io->endp_out_blk > 0 && !((unsigned)io->enc_len % io->endp_out_blk)) {
		int dummy;
		// signal end of transfer
		libusb_bulk_transfer(io->dev_handle,
			io->endp_out, NULL, 0, &dummy, io->timeout);
	}
#else
	ret = call_Write(io->handle, io->send_buf, io->enc_len);
#endif
	if (ret != io->enc_len)
		ERR_EXIT("usb_send failed (%d / %d)\n", ret, io->enc_len);

	return ret;
}
extern const char* CommonPartitions[] = {
	"splloader", "prodnv", "miscdata", "recovery", "misc", "trustos", "trustos_bak",
	"sml", "sml_bak", "uboot", "uboot_bak", "logo", "logo_1", "logo_2", "logo_3",
	"logo_4", "logo_5", "logo_6", "fbootlogo",
	"l_fixnv1", "l_fixnv2", "l_runtimenv1", "l_runtimenv2",
	"gpsgl", "gpsbd", "wcnmodem", "persist", "l_modem",
	"l_deltanv", "l_gdsp", "l_ldsp", "pm_sys", "boot",
	"system", "cache", "vendor", "uboot_log", "dtb", "socko",
	"vbmeta", "vbmeta_bak", "vbmeta_system",
	"trustos_a", "trustos_b", "sml_a", "sml_b", "teecfg", "teecfg_a", "teecfg_b",
	"uboot_a", "uboot_b", "gnssmodem_a", "gnssmodem_b", "wcnmodem_a",
	"wcnmodem_b", "l_modem_a", "l_modem_b", "l_deltanv_a", "l_deltanv_b",
	"l_gdsp_a", "l_gdsp_b", "l_ldsp_a", "l_ldsp_b", "l_agdsp_a", "l_agdsp_b",
	"l_cdsp_a", "l_cdsp_b", "pm_sys_a", "pm_sys_b", "boot_a", "boot_b",
	"vendor_boot_a", "vendor_boot_b", "dtb_a", "dtb_b", "dtbo_a", "dtbo_b",
	"super", "socko_a", "socko_b", "odmko_a", "odmko_b", "vbmeta_a", "vbmeta_b",
	"metadata", "sysdumpdb", "vbmeta_system_a", "vbmeta_system_b",
	"vbmeta_vendor_a", "vbmeta_vendor_b", "vbmeta_system_ext_a",
	"vbmeta_system_ext_b", "vbmeta_product_a", "nr_fixnv1", "nr_fixnv2",
	"nr_runtimenv1", "nr_runtimenv2", "nr_pmsys", "nr_agdsp", "nr_modem",
	"nr_v3phy", "nr_nrphy", "nr_nrdsp1", "nr_nrdsp2", "nr_deltanv", "m_raw",
	"m_data", "m_webui", "ubipac", "vbmeta_product_b", "user_partition","userdata"
};

// ��������Ԫ������
extern const size_t CommonPartitionsCount = sizeof(CommonPartitions) / sizeof(CommonPartitions[0]);
int recv_read_data(spdio_t *io) {
	int len;

	if (m_bOpened == -1) {
		spdio_free(io);
		ERR_EXIT("device unattached, exiting...\n");
	}
#if USE_LIBUSB
	int err = libusb_bulk_transfer(io->dev_handle, io->endp_in, io->recv_buf, RECV_BUF_LEN, &len, io->timeout);
	if (err == LIBUSB_ERROR_NO_DEVICE)
		ERR_EXIT("connection closed\n");
	else if (err < 0) {
		DEG_LOG(E,"usb_recv failed : %s", libusb_error_name(err)); return 0;
	}
#else
	len = call_Read(io->handle, io->recv_buf, RECV_BUF_LEN, io->timeout);
#endif
	if (len < 0) {
		DEG_LOG(E,"usb_recv failed, ret = %d", len); return 0;
	}

	if (io->verbose >= 2) {
		DEG_LOG(OP,"recv (%d):", len);
		print_mem(stderr, io->recv_buf, len);
	}
	io->recv_len = len;
	return len;
}

int recv_transcode(spdio_t *io, const uint8_t *buf, int buf_len, int *plen) {
	int a, pos = 0, nread = io->raw_len, head_found = 0;
	static int esc = 0;
	if (*plen == 6) nread = 0;
	if (nread) head_found = 1;

	while (pos < buf_len) {
		a = buf[pos++];
		if (io->flags & FLAGS_TRANSCODE) {
			if (esc && a != (HDLC_HEADER ^ 0x20) &&
				a != (HDLC_ESCAPE ^ 0x20)) {
				DEG_LOG(E,"excepted escaped byte (0x%02x)", a); return 0;
			}
			if (a == HDLC_HEADER) {
				if (!head_found) head_found = 1;
				else if (!nread) continue;
				else if (nread < *plen) {
					DEG_LOG(E,"received message too short"); return 0;
				}
				else break;
			}
			else if (a == HDLC_ESCAPE) {
				esc = 0x20;
			}
			else {
				if (!head_found) continue;
				if (nread >= *plen) {
					DEG_LOG(E,"received message too long"); return 0;
				}
				io->raw_buf[nread++] = a ^ esc;
				esc = 0;
			}
		}
		else {
			if (!head_found && a == HDLC_HEADER) {
				head_found = 1;
				continue;
			}
			if (nread == *plen) {
				if (a != HDLC_HEADER) {
					DEG_LOG(I,"expected end of message"); return 0;
				}
				break;
			}
			io->raw_buf[nread++] = a;
		}
		if (nread == 4) {
			a = READ16_BE(io->raw_buf + 2); // len
			*plen = a + 6;
		}
	}
	io->raw_len = nread;
	return nread;
}

extern int fdl1_loaded;
int recv_check_crc(spdio_t *io) {
	int a, nread = io->raw_len, plen = READ16_BE(io->raw_buf + 2) + 6;

	if (nread < 6) {
		DEG_LOG(E,"received message too short"); return 0;
	}

	if (nread != plen) {
		DEG_LOG(E,"incorrect length (%d, expected %d)", nread, plen); return 0;
	}

	a = READ16_BE(io->raw_buf + plen - 2);
	if (fdl1_loaded == 0 && !(io->flags & FLAGS_CRC16)) {
		int chk1, chk2;
		chk1 = spd_crc16(0, io->raw_buf, plen - 2);
		if (a == chk1) io->flags |= FLAGS_CRC16;
		else {
			chk2 = spd_checksum(0, io->raw_buf, plen - 2, CHK_ORIG);
			if (a == chk2) fdl1_loaded = 1;
			else {
				DEG_LOG(E,"incorrect checksum (0x%04x, expected 0x%04x or 0x%04x)", a, chk1, chk2);
				return 0;
			}
		}
	}
	else {
		int chk = (io->flags & FLAGS_CRC16) ?
			spd_crc16(0, io->raw_buf, plen - 2) :
			spd_checksum(0, io->raw_buf, plen - 2, CHK_ORIG);
		if (a != chk) {
			DEG_LOG(E,"incorrect checksum (0x%04x, expected 0x%04x)", a, chk);
			return 0;
		}
	}

	if (io->verbose == 1)
		DEG_LOG(OP,"recv: 0x%02x, size: %d",
			READ16_BE(io->raw_buf), READ16_BE(io->raw_buf + 2));

	return nread;
}

int recv_msg_orig(spdio_t *io) {
	int plen = 6;
	memset(io->recv_buf, 0, 8);
	while (1) {
		if (!recv_read_data(io)) return 0;
		if (!recv_transcode(io, io->recv_buf, io->recv_len, &plen)) return 0;
		if (plen == io->raw_len) break;
	}
	return recv_check_crc(io);
}

#if !USE_LIBUSB
int recv_msg_async(spdio_t *io) {
	DWORD bWaitCode = WaitForSingleObject(io->m_hOprEvent, io->timeout);
	if (bWaitCode != WAIT_OBJECT_0) {
		return 0;
	}
	else {
		ResetEvent(io->m_hOprEvent);
		return io->raw_len;
	}
}
#else
int recv_msg_async(spdio_t *io) {
	return 0;
}
#endif

extern int fdl2_executed;
int recv_msg(spdio_t *io) {
	int ret;
	for (;;) {
		if (io->m_dwRecvThreadID) ret = recv_msg_async(io);
		else ret = recv_msg_orig(io);
		// only retry in fdl2 stage
		if (!ret) {
			if (fdl2_executed) {
#if !USE_LIBUSB
				if (io->raw_len) { call_Clear(io->handle); io->raw_len = 0; }
#endif
				send_msg(io);
				if (io->m_dwRecvThreadID) ret = recv_msg_async(io);
				else ret = recv_msg_orig(io);
				if (!ret) break;
			}
			else break;
		}
		if (recv_type(io) != BSL_REP_LOG) break;
		DEG_LOG(I,"Response(BSL_REP_LOG): ");
		print_string(stderr, io->raw_buf + 4, READ16_BE(io->raw_buf + 2));
	}
	return ret;
}

int recv_msg_timeout(spdio_t *io, int timeout) {
	int old = io->timeout, ret;
	io->timeout = old > timeout ? old : timeout;
	ret = recv_msg(io);
	io->timeout = old;
	return ret;
}

unsigned recv_type(spdio_t *io) {
	//if (io->raw_len < 6) return -1;
	return READ16_BE(io->raw_buf);
}

int send_and_check(spdio_t *io) {
	int ret;
	send_msg(io);
	ret = recv_msg(io);
	if (!ret) ERR_EXIT("timeout reached\n");
	ret = recv_type(io);
	if (ret != BSL_REP_ACK) {
		const char* name = get_bsl_enum_name(ret);
		DEG_LOG(E,"excepted response (%s : 0x%04x)",name, ret);
		return -1;
	}
	return 0;
}

int check_confirm(const char *name) {
	if (isHelperInit) return 1;
	char c;
	DEG_LOG(OP,"Answer \"y\" to confirm the \"%s\" command: ", name);
	fflush(stdout);
	if (scanf(" %c", &c) != 1) return 0;
	while (getchar() != '\n');
	if (tolower(c) == 'y') return 1;
	return 0;
}

uint8_t *loadfile(const char *fn, size_t *num, size_t extra) {
	size_t n, j = 0; uint8_t *buf = 0;
	FILE *fi = fopen(fn, "rb");
	if (fi) {
		fseek(fi, 0, SEEK_END);
		n = ftell(fi);
		if (n) {
			fseek(fi, 0, SEEK_SET);
			buf = NEWN uint8_t[n + extra];
			if (buf) j = fread(buf, 1, n, fi);
		}
		fclose(fi);
	}
	if (num) *num = j;
	return buf;
}

void send_buf(spdio_t *io,
	uint32_t start_addr, int end_data,
	unsigned step, uint8_t *mem, unsigned size) {
	uint32_t i, n;
	uint32_t *data = (uint32_t *)io->temp_buf;
	WRITE32_BE(data, start_addr);
	WRITE32_BE(data + 1, size);

	encode_msg_nocpy(io, BSL_CMD_START_DATA, 4 * 2);
	if (send_and_check(io)) return;
	for (i = 0; i < size; i += n) {
		n = size - i;
		// n = spd_transcode_max(mem + i, size - i, 2048 - 2 - 6);
		if (n > step) n = step;
		encode_msg(io, BSL_CMD_MIDST_DATA, mem + i, n);
		if (send_and_check(io)) return;
	}
	if (end_data) {
		encode_msg_nocpy(io, BSL_CMD_END_DATA, 0);
		send_and_check(io);
	}
}
/*
void send_buf_1(spdio_t* io,
	uint32_t start_addr, int end_data,
	unsigned step, uint8_t* mem, unsigned size) {

	static unsigned long long start_time = 0;
	static uint64_t total_sent = 0;
	static uint64_t total_size = 0;

	// ������µĴ����������ü�ʱ���ͼ�����
	if (start_time == 0) {
		start_time = GetTickCount64();
		total_sent = 0;
		total_size = size;
	}

	uint32_t i, n;
	uint32_t* data = (uint32_t*)io->temp_buf;
	WRITE32_BE(data, start_addr);
	WRITE32_BE(data + 1, size);

	encode_msg_nocpy(io, BSL_CMD_START_DATA, 4 * 2);
	if (send_and_check(io)) {
		// ����ʱ��ʾ��ǰ����
		print_progress_bar(total_sent, total_size, start_time);
		printf("\n"); // ����
		start_time = 0; // �����Ա��´�ʹ��
		return;
	}

	for (i = 0; i < size; i += n) {
		n = size - i;
		if (n > step) n = step;

		encode_msg(io, BSL_CMD_MIDST_DATA, mem + i, n);
		if (send_and_check(io)) {
			// ����ʱ��ʾ��ǰ����
			print_progress_bar(total_sent, total_size, start_time);
			printf("\n"); // ����
			start_time = 0; // �����Ա��´�ʹ��
			return;
		}

		// �����ѷ����ֽ�������ʾ����
		total_sent += n;
		print_progress_bar(total_sent, total_size, start_time);
	}

	if (end_data) {
		encode_msg_nocpy(io, BSL_CMD_END_DATA, 0);
		if (send_and_check(io)) {
			// ����ʱ��ʾ��ǰ����
			print_progress_bar(total_sent, total_size, start_time);
			printf("\n"); // ����
			start_time = 0; // �����Ա��´�ʹ��
			return;
		}
	}

	// ��ɴ��䣬��ʾ100%����
	print_progress_bar(total_size, total_size, start_time);
	printf("\n"); // ��ɻ���

	// ���þ�̬�����Ա��´�ʹ��
	start_time = 0;
}
*/
size_t send_file(spdio_t *io, const char *fn,
	uint32_t start_addr, int end_data, unsigned step,
	unsigned src_offs, unsigned src_size) {
	uint8_t *mem; size_t size = 0;
	mem = loadfile(fn, &size, 0);
	if (!mem) ERR_EXIT("load file(\"%s\") failed\n", fn);
	if ((uint64_t)size >> 32) ERR_EXIT("file too big\n");
	if (size < src_offs) ERR_EXIT("required offset larger than file size\n");
	size -= src_offs;
	if (src_size) {
		if (size < src_size) DEG_LOG(W,"required size larger than file size");
		else size = src_size;
	}
	send_buf(io, start_addr, end_data, step, mem + src_offs, size);
	delete[](mem);
	DEG_LOG(OP,"Sent %s to 0x%x", fn, start_addr);
	return size;
}
int GetStage(int mode) {	
	if (fdl2_executed > 0) return FDL2;
	else if (fdl1_loaded > 0) return FDL1;
	else return BROM;
}

FILE *my_fopen(const char *fn, const char *mode) {
	if (savepath && savepath[0]) {
		char fix_fn[1024];
		char* ch;
		if ((ch = const_cast<char*>(strrchr(fn, '/')))) sprintf(fix_fn, "%s/%s", savepath, ch + 1);
		else if ((ch == strrchr(fn, '\\'))) sprintf(fix_fn, "%s/%s", savepath, ch + 1);
		else sprintf(fix_fn, "%s/%s", savepath, fn);
		return fopen(fix_fn, mode);
	}
	else return fopen(fn, mode);
}

unsigned dump_flash(spdio_t *io,
	uint32_t addr, uint32_t start, uint32_t len,
	const char *fn, unsigned step) {
	uint32_t n, offset, nread;
	int ret;
	FILE *fo = my_fopen(fn, "wb");
	if (!fo) ERR_EXIT("fopen(dump) failed\n");

	for (offset = start; offset < start + len; ) {
		uint32_t *data = (uint32_t *)io->temp_buf;
		n = start + len - offset;
		if (n > step) n = step;

		WRITE32_BE(data, addr);
		WRITE32_BE(data + 1, n);
		WRITE32_BE(data + 2, offset);

		encode_msg_nocpy(io, BSL_CMD_READ_FLASH, 4 * 3);
		send_msg(io);
		ret = recv_msg(io);
		if (!ret) ERR_EXIT("timeout reached\n");
		if ((ret = recv_type(io)) != BSL_REP_READ_FLASH) {
			const char* name = get_bsl_enum_name(ret);
			DEG_LOG(E,"excepted response (%s : 0x%04x)",name, ret);
			break;
		}
		nread = READ16_BE(io->raw_buf + 2);
		if (n < nread)
			ERR_EXIT("excepted length\n");
		if (fwrite(io->raw_buf + 4, 1, nread, fo) != nread)
			ERR_EXIT("fwrite(dump) failed\n");
		offset += nread;
		if (n != nread) break;
	}
	DEG_LOG(I,"Read flash successfully: 0x%08x+0x%x, target: 0x%x, read: 0x%x", addr, start, len, offset - start);
	fclose(fo);
	return offset;
}

unsigned dump_mem(spdio_t *io,
	uint32_t start, uint32_t len, const char *fn, unsigned step) {
	uint32_t n, offset, nread;
	int ret;
	FILE *fo = my_fopen(fn, "wb");
	if (!fo) ERR_EXIT("fopen(dump) failed\n");

	for (offset = start; offset < start + len; ) {
		uint32_t *data = (uint32_t *)io->temp_buf;
		n = start + len - offset;
		if (n > step) n = step;

		WRITE32_BE(data, offset);
		WRITE32_BE(data + 1, n);
		WRITE32_BE(data + 2, 0); // unused

		encode_msg_nocpy(io, BSL_CMD_READ_FLASH, 12);
		send_msg(io);
		ret = recv_msg(io);
		if (!ret) ERR_EXIT("timeout reached\n");
		if ((ret = recv_type(io)) != BSL_REP_READ_FLASH) {
			const char* name = get_bsl_enum_name(ret);
			DEG_LOG(E,"excepted response (%s : 0x%04x)",name, ret);
			break;
		}
		nread = READ16_BE(io->raw_buf + 2);
		if (n < nread)
			ERR_EXIT("excepted length\n");
		if (fwrite(io->raw_buf + 4, 1, nread, fo) != nread)
			ERR_EXIT("fwrite(dump) failed\n");
		offset += nread;
		if (n != nread) break;
	}
	DEG_LOG(I,"Read mem successfully: 0x%08x, target: 0x%x, read: 0x%x", start, len, offset - start);
	fclose(fo);
	return offset;
}

int copy_to_wstr(uint16_t *d, size_t n, const char *s) {
	size_t i; int a = -1;
	for (i = 0; a && i < n; i++) { a = s[i]; WRITE16_LE(d + i, a); }
	return a;
}

int copy_from_wstr(char *d, size_t n, const uint16_t *s) {
	size_t i; int a = -1;
	for (i = 0; a && i < n; i++) { d[i] = a = s[i]; if (a >> 8) break; }
	return a;
}

void select_partition(spdio_t *io, const char *name,
	uint64_t size, int mode64, int cmd) {
	uint32_t t32; uint64_t n64;
	struct pkt {
		uint16_t name[36];
		uint32_t size, size_hi; uint64_t dummy;
	} *pkt_ptr;
	int ret;
	pkt_ptr = (struct pkt *) io->temp_buf;
	ret = copy_to_wstr(pkt_ptr->name, 36, name);
	if (ret) ERR_EXIT("name too long\n");
	n64 = size;
	WRITE32_LE(&pkt_ptr->size, n64);
	if (mode64) {
		t32 = n64 >> 32;
		WRITE32_LE(&pkt_ptr->size_hi, t32);
	}

	encode_msg_nocpy(io, cmd, 72 + (mode64 ? 16 : 4));
}

#if !_WIN32
unsigned long long GetTickCount64() {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
#endif

#define PROGRESS_BAR_WIDTH 40

void print_progress_bar(spdio_t* io, uint64_t done, uint64_t total, unsigned long long time0) {
    static int completed0 = 0;
    static uint64_t done0 = 0;
    
    unsigned long long time = GetTickCount64();
    if (completed0 == PROGRESS_BAR_WIDTH) { 
        completed0 = 0; 
        done0 = 0; 
    }
    
    int completed = (int)(PROGRESS_BAR_WIDTH * done / (double)total);
    if (completed != completed0 && isCancel == 0) {
        int remaining = PROGRESS_BAR_WIDTH - completed;
        DBG_LOG("[");
        for (int i = 0; i < completed; i++) {
            DBG_LOG("=");
        }
        for (int i = 0; i < remaining; i++) {
            DBG_LOG(" ");
        }
        DBG_LOG("]%6.1f%% Speed:%6.2fMb/s\r", 100 * done / (double)total, (double)1000 * done / (time - time0) / 1024 / 1024);
        if(io->nor_bar) DBG_LOG("\n");
        completed0 = completed;
        done0 = done;
        
        // 更新UI进度条和百分比标签
        if (isHelperInit) {
            // 计算进度百分比
            double progress_percent = done / (double)total;
            
            // 在主线程中更新UI
            g_idle_add([](gpointer data) -> gboolean {
                auto* progress_data = static_cast<std::pair<double, uint64_t>*>(data);
                double percent = progress_data->first;
                uint64_t done_value = progress_data->second;
                
                if (isHelperInit) {
                    // 更新进度条
                    GtkWidget* progressBar = helper.getWidget("progressBar_1");
                    if (progressBar && GTK_IS_PROGRESS_BAR(progressBar)) {
                        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progressBar), percent);
                        /*
                        // 可选：在进度条上显示文本
                        char progress_text[32];
                        snprintf(progress_text, sizeof(progress_text), "%.1f%%", percent * 100);
                        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progressBar), progress_text);
                        gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(progressBar), TRUE);
						*/
                    }
                    
                    // 更新百分比标签
                    GtkWidget* percentLabel = helper.getWidget("percent");
                    if (percentLabel && GTK_IS_LABEL(percentLabel)) {
                        char percent_text[16];
                        snprintf(percent_text, sizeof(percent_text), "%.1f%%", percent * 100);
                        gtk_label_set_text(GTK_LABEL(percentLabel), percent_text);
                    }
                }
                
                delete progress_data;
                return G_SOURCE_REMOVE;
            }, new std::pair<double, uint64_t>(progress_percent, done));
        }
    }
}

extern uint64_t fblk_size;
uint64_t dump_partition(spdio_t *io,
	const char *name, uint64_t start, uint64_t len,
	const char *fn, unsigned step) {
	uint32_t n, nread, t32; uint64_t offset, n64, saved_size = 0;
	int ret, mode64 = (start + len) >> 32;
	char name_tmp[36];
	double rtime = get_time();
	DEG_LOG(OP,"Start to read partition %s",name);
	DEG_LOG(I,"Type CTRL + C to cancel...");
	start_signal();
	if (!strcmp(name, "super")) dump_partition(io, "metadata", 0, check_partition(io, "metadata", 1), "metadata.bin", step);
	else if (!strncmp(name, "userdata", 8)) { if (!check_confirm("read userdata")) return 0; }
	else if (strstr(name, "nv1")) {
		strcpy(name_tmp, name);
		char *dot = strrchr(name_tmp, '1');
		if (dot != nullptr) *dot = '2';
		name = name_tmp;
		start = 512;
		if (len > 512)
			len -= 512;
	}
	if (isCancel) {  return 0; }
	select_partition(io, name, start + len, mode64, BSL_CMD_READ_START);
	if (send_and_check(io)) {
		encode_msg_nocpy(io, BSL_CMD_READ_END, 0);
		send_and_check(io);
		return 0;
	}
	if (isCancel) {   return 0; }
	FILE *fo = my_fopen(fn, "wb");
	if (!fo) ERR_EXIT("fopen(dump) failed\n");

	unsigned long long time_start = GetTickCount64();
	for (offset = start; (n64 = start + len - offset); ) {
		uint32_t *data = (uint32_t *)io->temp_buf;
		n = (uint32_t)(n64 > step ? step : n64);
		if (isCancel) { fclose(fo); return offset - start; }
		WRITE32_LE(data, n);
		WRITE32_LE(data + 1, offset);
		t32 = offset >> 32;
		WRITE32_LE(data + 2, t32);
		//if (isCancel) { isCancel = 0;signal(SIGINT, SIG_DFL); return; }
		encode_msg_nocpy(io, BSL_CMD_READ_MIDST, mode64 ? 12 : 8);
		send_msg(io);
		ret = recv_msg(io);
		if (!ret) ERR_EXIT("timeout reached\n");
		if ((ret = recv_type(io)) != BSL_REP_READ_FLASH) {
			const char* name = get_bsl_enum_name(ret);
			DEG_LOG(E,"excepted response (%s : 0x%04x)",name, ret);
			break;
		}
		nread = READ16_BE(io->raw_buf + 2);
		if (n < nread)
			ERR_EXIT("excepted length\n");
		if (fwrite(io->raw_buf + 4, 1, nread, fo) != nread)
			ERR_EXIT("fwrite(dump) failed\n");
		print_progress_bar(io,offset + nread - start, len, time_start);
		offset += nread;
		if (n != nread) break;

		if (fblk_size) {
			saved_size += nread;
			if (saved_size >= fblk_size) { usleep(1000000); saved_size = 0; }
		}
	}
	double etime = get_time();
	double time_spent = etime - rtime;
	
	DEG_LOG(I,"Read partition %s(+0x%llx) successfully, target: 0x%llx, read: 0x%llx",
		name, (long long)start, (long long)len,
		(long long)(offset - start));
	DEG_LOG(I, "Cost time %.6f seconds", time_spent);
	fclose(fo);	

	encode_msg_nocpy(io, BSL_CMD_READ_END, 0);
	send_and_check(io);
	return offset - start;
}

uint64_t read_pactime(spdio_t *io) {
	uint32_t n, offset = 0x81400, len = 8;
	int ret; uint32_t *data = (uint32_t *)io->temp_buf;
	unsigned long long time, unix1;

	select_partition(io, "miscdata", offset + len, 0, BSL_CMD_READ_START);
	if (send_and_check(io)) {
		encode_msg_nocpy(io, BSL_CMD_READ_END, 0);
		send_and_check(io);
		return 0;
	}

	WRITE32_LE(data, len);
	WRITE32_LE(data + 1, offset);
	encode_msg_nocpy(io, BSL_CMD_READ_MIDST, 8);
	send_msg(io);
	ret = recv_msg(io);
	if (!ret) ERR_EXIT("timeout reached\n");
	if ((ret = recv_type(io)) != BSL_REP_READ_FLASH) {
		const char* name = get_bsl_enum_name(ret);
		DEG_LOG(E,"excepted response (%s : 0x%04x)",name, ret);
		encode_msg_nocpy(io, BSL_CMD_READ_END, 0);
		send_and_check(io);
		return 0;
	}
	n = READ16_BE(io->raw_buf + 2);
	if (n != len) ERR_EXIT("excepted length\n");

	time = (uint32_t)READ32_LE(io->raw_buf + 4);
	time |= (uint64_t)READ32_LE(io->raw_buf + 8) << 32;

	unix1 = time ? time / 10000000 - 11644473600 : 0;
	// $ date -d @unixtime
	DEG_LOG(I,"pactime = 0x%llx (unix = %llu)", time, unix1);

	encode_msg_nocpy(io, BSL_CMD_READ_END, 0);
	send_and_check(io);
	return time;
}

int scan_xml_partitions(spdio_t *io, const char *fn, uint8_t *buf, size_t buf_size) {
	const char *part1 = "Partitions>";
	char *src, *p; size_t fsize = 0;
	int part1_len = strlen(part1), found = 0, stage = 0;
	if (io->ptable == nullptr) io->ptable = NEWN partition_t[128 * sizeof(partition_t)];
	src = (char *)loadfile(fn, &fsize, 1);
	if (!src) ERR_EXIT("loadfile failed\n");
	src[fsize] = 0;
	p = src;
	for (;;) {
		int i, a = *p++, n; char c; long long size;
		if (a == ' ' || a == '\t' || a == '\n' || a == '\r') continue;
		if (a != '<') {
			if (!a) break;
			if (stage != 1) continue;
			ERR_EXIT("xml: unexpected symbol\n");
		}
		if (!memcmp(p, "!--", 3)) {
			p = strstr(p + 3, "--");
			if (!p || !((p[-1] - '!') | (p[-2] - '<')) || p[2] != '>')
				ERR_EXIT("xml: unexpected syntax\n");
			p += 3;
			continue;
		}
		if (stage != 1) {
			stage += !memcmp(p, part1, part1_len);
			if (stage > 2)
				ERR_EXIT("xml: more than one partition lists\n");
			p = strchr(p, '>');
			if (!p) ERR_EXIT("xml: unexpected syntax\n");
			p++;
			continue;
		}
		if (*p == '/' && !memcmp(p + 1, part1, part1_len)) {
			p = p + 1 + part1_len;
			stage++;
			continue;
		}
		i = sscanf(p, "Partition id=\"%35[^\"]\" size=\"%lli\"/%n%c", (*(io->ptable + found)).name, &size, &n, &c);
		if (i != 3 || c != '>')
			ERR_EXIT("xml: unexpected syntax\n");
		p += n + 1;
		if (buf_size < 0x4c)
			ERR_EXIT("xml: too many partitions\n");
		buf_size -= 0x4c;
		memset(buf, 0, 36 * 2);
		for (i = 0; (a = (*(io->ptable + found)).name[i]); i++) buf[i * 2] = a;
		if (!i) ERR_EXIT("empty partition name\n");
		WRITE32_LE(buf + 0x48, size);
		buf += 0x4c;
		DBG_LOG("[%d] %s, %d\n", found + 1, (*(io->ptable + found)).name, (int)size);
		(*(io->ptable + found)).size = size << 20;
		found++;
	}
	io->part_count = found;
	if (p - 1 != src + fsize) ERR_EXIT("xml: zero byte");
	if (stage != 2) ERR_EXIT("xml: unexpected syntax\n");
	delete[] src;
	return found;
}

#define SECTOR_SIZE 512
#define MAX_SECTORS 32

extern int selected_ab;
int gpt_info(partition_t *ptable, const char *fn_xml, int *part_count_ptr) {
	FILE *fp = my_fopen("pgpt.bin", "rb");
	if (fp == nullptr) {
		return -1;
	}
	efi_header header;
	int bytes_read;
	uint8_t buffer[SECTOR_SIZE];
	int sector_index = 0;
	int found = 0;

	while (sector_index < MAX_SECTORS) {
		bytes_read = fread(buffer, 1, SECTOR_SIZE, fp);
		if (bytes_read != SECTOR_SIZE) {
			fclose(fp);
			return -1;
		}
		if (memcmp(buffer, "EFI PART", 8) == 0) {
			memcpy(&header, buffer, sizeof(header));
			found = 1;
			break;
		}
		sector_index++;
	}

	if (found == 0) {
		fclose(fp);
		return -1;
	}
	else {
		if (sector_index == 1) Da_Info.dwStorageType = 0x102;
		else Da_Info.dwStorageType = 0x103;
	}
	int real_SECTOR_SIZE = SECTOR_SIZE * sector_index;
	efi_entry *entries = NEWN efi_entry[header.number_of_partition_entries * sizeof(efi_entry)];
	if (entries == nullptr) {
		fclose(fp);
		return -1;
	}
	fseek(fp, (long)header.partition_entry_lba * real_SECTOR_SIZE, SEEK_SET);
	bytes_read = fread(entries, 1, header.number_of_partition_entries * sizeof(efi_entry), fp);
	if (bytes_read != (int)(header.number_of_partition_entries * sizeof(efi_entry)))
		DEG_LOG(I,"read %d/%d only.", bytes_read, (int)(header.number_of_partition_entries * sizeof(efi_entry)));
	FILE *fo = nullptr;
	if (strcmp(fn_xml, "-")) {
		fo = my_fopen(fn_xml, "wb");
		if (!fo) ERR_EXIT("fopen failed\n");
		fprintf(fo, "<Partitions>\n");
	}
	int n = 0;
	for (int i = 0; i < header.number_of_partition_entries; i++) {
		efi_entry entry = *(entries + i);
		if (entry.starting_lba == 0 && entry.ending_lba == 0) {
			n = i;
			break;
		}
	}
	DBG_LOG("  0 %36s     %lldKB\n", "splloader",(long long)g_spl_size / 1024);
	for (int i = 0; i < n; i++) {
		efi_entry entry = *(entries + i);
		copy_from_wstr((*(ptable + i)).name, 36, (uint16_t *)entry.partition_name);
		uint64_t lba_count = entry.ending_lba - entry.starting_lba + 1;
		(*(ptable + i)).size = lba_count * real_SECTOR_SIZE;
		DBG_LOG("%3d %36s %7lldMB\n", i + 1, (*(ptable + i)).name, ((*(ptable + i)).size >> 20));
		if (fo) {
			fprintf(fo, "    <Partition id=\"%s\" size=\"", (*(ptable + i)).name);
			if (i + 1 == n) fprintf(fo, "0x%x\"/>\n", ~0);
			else fprintf(fo, "%lld\"/>\n", ((*(ptable + i)).size >> 20));
		}
		if (!selected_ab) {
			size_t namelen = strlen((*(ptable + i)).name);
			if (namelen > 2 && 0 == strcmp((*(ptable + i)).name + namelen - 2, "_a")) selected_ab = 1;
		}
	}
	if (fo) {
		fprintf(fo, "</Partitions>");
		fclose(fo);
	}
	delete[](entries);
	fclose(fp);
	*part_count_ptr = n;
	DEG_LOG(I,"standard gpt table saved to pgpt.bin");
	DEG_LOG(I,"skip saving sprd partition list packet");
	return 0;
}

extern int gpt_failed;
partition_t *partition_list(spdio_t *io, const char *fn, int *part_count_ptr) {
	long size;
	unsigned i, n = 0;
	int ret; FILE *fo = nullptr; uint8_t *p;
	partition_t *ptable = NEWN partition_t[128 * sizeof(partition_t)];
	if (ptable == nullptr) return nullptr;

	DEG_LOG(OP,"Reading partition table...\n");
	if (selected_ab < 0) select_ab(io);
	int verbose = io->verbose;
	io->verbose = 0;
	size = dump_partition(io, "user_partition", 0, 32 * 1024, "pgpt.bin", 4096);
	io->verbose = verbose;
	if (32 * 1024 == size)
		gpt_failed = gpt_info(ptable, fn, part_count_ptr);
	if (gpt_failed) {
		remove("pgpt.bin");
		encode_msg_nocpy(io, BSL_CMD_READ_PARTITION, 0);
		send_msg(io);
		ret = recv_msg(io);
		if (!ret) ERR_EXIT("timeout reached\n");
		ret = recv_type(io);
		if (ret != BSL_REP_READ_PARTITION) {
			const char* name = get_bsl_enum_name(ret);
			DEG_LOG(E,"excepted response (%s : 0x%04x)",name, ret);
			gpt_failed = -1;
			delete[](ptable);
			return nullptr;
		}
		size = READ16_BE(io->raw_buf + 2);
		if (size % 0x4c) {
			DEG_LOG(I,"Not divisible by struct size (0x%04lx)", size);
			gpt_failed = -1;
			delete[](ptable);
			return nullptr;
		}
		FILE *fpkt = my_fopen("sprdpart.bin", "wb");
		if (!fpkt) ERR_EXIT("fopen failed\n");
		fwrite(io->raw_buf + 4, 1, size, fpkt);
		fclose(fpkt);
		n = size / 0x4c;
		if (strcmp(fn, "-")) {
			fo = my_fopen(fn, "wb");
			if (!fo) ERR_EXIT("fopen failed\n");
			fprintf(fo, "<Partitions>\n");
		}
		int divisor = 10;
		DEG_LOG(OP,"detecting sector size");
		p = io->raw_buf + 4;
		for (i = 0; i < n; i++, p += 0x4c) {
			size = READ32_LE(p + 0x48);
			while (!(size >> divisor)) divisor--;
		}
		if (Da_Info.dwStorageType == 0) { 
			if (divisor == 10) Da_Info.dwStorageType = 0x102; //emmc
			else Da_Info.dwStorageType = 0x103; //ufs
		}
		
		p = io->raw_buf + 4;
		DBG_LOG("  0 %36s     %lldKB\n", "splloader",(long long)g_spl_size / 1024);
		for (i = 0; i < n; i++, p += 0x4c) {
			ret = copy_from_wstr((*(ptable + i)).name, 36, (uint16_t *)p);
			if (ret) ERR_EXIT("bad partition name\n");
			size = READ32_LE(p + 0x48);
			(*(ptable + i)).size = (long long)size << (20 - divisor);
			DBG_LOG("%3d %36s %7lldMB\n", i + 1, (*(ptable + i)).name, ((*(ptable + i)).size >> 20));
			if (fo) {
				fprintf(fo, "    <Partition id=\"%s\" size=\"", (*(ptable + i)).name);
				if (i + 1 == n) fprintf(fo, "0x%x\"/>\n", ~0);
				else fprintf(fo, "%lld\"/>\n", ((*(ptable + i)).size >> 20));
			}
			if (!selected_ab) {
				size_t namelen = strlen((*(ptable + i)).name);
				if (namelen > 2 && 0 == strcmp((*(ptable + i)).name + namelen - 2, "_a")) selected_ab = 1;
			}
		}
		if (fo) {
			fprintf(fo, "</Partitions>\n");
			fclose(fo);
		}
		*part_count_ptr = n;
		DEG_LOG(W,"Unable to get standard gpt table");
		DEG_LOG(I,"Sprd partition list packet saved to sprdpart.bin");
		gpt_failed = 0;
	}
	if (*part_count_ptr) {
		if (strcmp(fn, "-")) DEG_LOG(I,"Partition list saved to %s\n", fn);
		DEG_LOG(I,"Total number of partitions: %d\n", *part_count_ptr);
		if (Da_Info.dwStorageType == 0x102) DEG_LOG(I,"Storage is emmc\n");
		else if (Da_Info.dwStorageType == 0x103) DEG_LOG(I,"Storage is ufs\n");
		return ptable;
	}
	else {
		gpt_failed = -1;
		delete[](ptable);
		return nullptr;
	}
}
const char* get_bsl_enum_name(unsigned int value) {
	switch (value) {
	case 0x00: return "BSL_CMD_CONNECT";
	case 0x01: return "BSL_CMD_START_DATA";
	case 0x02: return "BSL_CMD_MIDST_DATA";
	case 0x03: return "BSL_CMD_END_DATA";
	case 0x04: return "BSL_CMD_EXEC_DATA";
	case 0x05: return "BSL_CMD_NORMAL_RESET";
	case 0x06: return "BSL_CMD_READ_FLASH";
	case 0x07: return "BSL_CMD_READ_CHIP_TYPE";
	case 0x08: return "BSL_CMD_READ_NVITEM";
	case 0x09: return "BSL_CMD_CHANGE_BAUD";
	case 0x0A: return "BSL_CMD_ERASE_FLASH";
	case 0x0B: return "BSL_CMD_REPARTITION";
	case 0x0C: return "BSL_CMD_READ_FLASH_TYPE";
	case 0x0D: return "BSL_CMD_READ_FLASH_INFO";
	case 0x0F: return "BSL_CMD_READ_SECTOR_SIZE";
	case 0x10: return "BSL_CMD_READ_START";
	case 0x11: return "BSL_CMD_READ_MIDST";
	case 0x12: return "BSL_CMD_READ_END";
	case 0x13: return "BSL_CMD_KEEP_CHARGE";
	case 0x14: return "BSL_CMD_EXTTABLE";
	case 0x15: return "BSL_CMD_READ_FLASH_UID";
	case 0x16: return "BSL_CMD_READ_SOFTSIM_EID";
	case 0x17: return "BSL_CMD_POWER_OFF";
	case 0x19: return "BSL_CMD_CHECK_ROOT/YCC_CMD_UNLOCK_BOOTLOADER";
	case 0x1A: return "BSL_CMD_READ_CHIP_UID/YCC_CMD_LOCK_BOOTLOADER";
	case 0x1B: return "BSL_CMD_ENABLE_WRITE_FLASH";
	case 0x1C: return "BSL_CMD_ENABLE_SECUREBOOT";
	case 0x1D: return "BSL_CMD_IDENTIFY_START";
	case 0x1E: return "BSL_CMD_IDENTIFY_END";
	case 0x1F: return "BSL_CMD_READ_CU_REF";
	case 0x20: return "BSL_CMD_READ_REFINFO";
	case 0x21: return "BSL_CMD_DISABLE_TRANSCODE";
	case 0x22: return "BSL_CMD_WRITE_APR_INFO";
	case 0x23: return "BSL_CMD_CUST_DUMMY";
	case 0x24: return "BSL_CMD_READ_RF_TRANSCEIVER_TYPE";
	case 0x25: return "BSL_CMD_ENABLE_DEBUG_MODE";
	case 0x26: return "BSL_CMD_DDR_CHECK";
	case 0x27: return "BSL_CMD_SELF_REFRESH";
	case 0x28: return "BSL_CMD_ENABLE_RAW_DATA";
	case 0x29: return "BSL_CMD_READ_NAND_BLOCK_INFO";
	case 0x2A: return "BSL_CMD_SET_FIRST_MODE";
	case 0x2B: return "BSL_CMD_SET_RANDOM_DATA";
	case 0x2C: return "BSL_CMD_SET_TIME_STAMP";
	case 0x2D: return "BSL_CMD_READ_PARTITION";
	case 0x2E: return "BSL_CMD_READ_VCUR_DATA";
	case 0x2F: return "BSL_CMD_WRITE_VPAC_DATA";
	case 0x31: return "BSL_CMD_MIDST_RAW_START";
	case 0x32: return "BSL_CMD_FLUSH_DATA";
	case 0x33: return "BSL_CMD_MIDST_RAW_START2";
	case 0x34: return "BSL_CMD_ENABLE_UBOOT_LOG";
	case 0x35: return "BSL_CMD_DUMP_UBOOT_LOG";
	case 0x40: return "BSL_CMD_DISABLE_SELINUX";
	case 0x41: return "BSL_CMD_AUTH_BEGIN";
	case 0x42: return "BSL_CMD_AUTH_END";
	case 0x43: return "BSL_CMD_EMMC_CID";
	case 0x44: return "BSL_CMD_OPEN_WATCH_DOG";
	case 0x45: return "BSL_CMD_CLOSE_WATCH_DOG";
	case 0x46: return "BSL_CMD_POWEROFF_NOKEY";
	case 0x47: return "BSL_CMD_WRITE_EFUSE";
	case 0x48: return "BSL_CMD_READ_PARTITION_VALUE";
	case 0x49: return "BSL_CMD_WRITE_PARTITION_VALUE";
	case 0x50: return "BSL_CMD_WRITE_DOWNLOAD_TIMESTAMP";
	case 0x51: return "BSL_CMD_PARTITION_SIGNATURE";
	case 0xCC: return "BSL_CMD_SEND_FLAG/YCC_REP_SET_BOOTLOADER_SUCCESS";
	case 0x7E: return "BSL_CMD_CHECK_BAUD";
	case 0x7F: return "BSL_CMD_END_PROCESS";

		// Response codes
	case 0x80: return "BSL_REP_ACK";
	case 0x81: return "BSL_REP_VER";
	case 0x82: return "BSL_REP_INVALID_CMD";
	case 0x83: return "BSL_REP_UNKNOW_CMD";
	case 0x84: return "BSL_REP_OPERATION_FAILED";
	case 0x85: return "BSL_REP_NOT_SUPPORT_BAUDRATE";
	case 0x86: return "BSL_REP_DOWN_NOT_START";
	case 0x87: return "BSL_REP_DOWN_MULTI_START";
	case 0x88: return "BSL_REP_DOWN_EARLY_END";
	case 0x89: return "BSL_REP_DOWN_DEST_ERROR";
	case 0x8A: return "BSL_REP_DOWN_SIZE_ERROR";
	case 0x8B: return "BSL_REP_VERIFY_ERROR";
	case 0x8C: return "BSL_REP_NOT_VERIFY";
	case 0x8D: return "BSL_PHONE_NOT_ENOUGH_MEMORY";
	case 0x8E: return "BSL_PHONE_WAIT_INPUT_TIMEOUT";
	case 0x8F: return "BSL_PHONE_SUCCEED";
	case 0x90: return "BSL_PHONE_VALID_BAUDRATE";
	case 0x91: return "BSL_PHONE_REPEAT_CONTINUE";
	case 0x92: return "BSL_PHONE_REPEAT_BREAK";
	case 0x93: return "BSL_REP_READ_FLASH";
	case 0x94: return "BSL_REP_READ_CHIP_TYPE";
	case 0x95: return "BSL_REP_READ_NVITEM";
	case 0x96: return "BSL_REP_INCOMPATIBLE_PARTITION";
	case 0x97: return "BSL_REP_UNKNOWN_DEVICE";
	case 0x98: return "BSL_REP_INVALID_DEVICE_SIZE";
	case 0x99: return "BSL_REP_ILLEGAL_SDRAM";
	case 0x9A: return "BSL_WRONG_SDRAM_PARAMETER";
	case 0x9B: return "BSL_REP_READ_FLASH_INFO";
	case 0x9C: return "BSL_REP_READ_SECTOR_SIZE";
	case 0x9D: return "BSL_REP_READ_FLASH_TYPE";
	case 0x9E: return "BSL_REP_READ_FLASH_UID";
	case 0x9F: return "BSL_REP_READ_SOFTSIM_EID";
	case 0xA0: return "BSL_ERROR_CHECKSUM";
	case 0xA1: return "BSL_CHECKSUM_DIFF";
	case 0xA2: return "BSL_WRITE_ERROR";
	case 0xA3: return "BSL_CHIPID_NOT_MATCH";
	case 0xA4: return "BSL_FLASH_CFG_ERROR";
	case 0xA5: return "BSL_REP_DOWN_STL_SIZE_ERROR";
	case 0xA7: return "BSL_REP_PHONE_IS_ROOTED";
	case 0xAA: return "BSL_REP_SEC_VERIFY_ERROR";
	case 0xAB: return "BSL_REP_READ_CHIP_UID";
	case 0xAC: return "BSL_REP_NOT_ENABLE_WRITE_FLASH";
	case 0xAD: return "BSL_REP_ENABLE_SECUREBOOT_ERROR";
	case 0xAE: return "BSL_REP_IDENTIFY_START";
	case 0xAF: return "BSL_REP_IDENTIFY_END";
	case 0xB0: return "BSL_REP_READ_CU_REF";
	case 0xB1: return "BSL_REP_READ_REFINFO";
	case 0xB2: return "BSL_REP_CUST_DUMMY";
	case 0xB3: return "BSL_REP_FLASH_WRITTEN_PROTECTION";
	case 0xB4: return "BSL_REP_FLASH_INITIALIZING_FAIL";
	case 0xB5: return "BSL_REP_RF_TRANSCEIVER_TYPE";
	case 0xB6: return "BSL_REP_DDR_CHECK_ERROR";
	case 0xB7: return "BSL_REP_SELF_REFRESH_ERROR";
	case 0xB8: return "BSL_REP_READ_NAND_BLOCK_INFO";
	case 0xB9: return "BSL_REP_RANDOM_DATA_ERROR";
	case 0xBA: return "BSL_REP_READ_PARTITION";
	case 0xBB: return "BSL_REP_DUMP_UBOOT_LOG";
	case 0xBC: return "BSL_REP_READ_VCUR_DATA";
	case 0xBD: return "BSL_REP_AUTH_M1_DATA";
	case 0xBE: return "BSL_REP_READ_PARTITION_VALUE";
	case 0xBF: return "BSL_REP_UNSUPPORT_PARTITION";
	case 0xC0: return "BSL_REP_EMMC_CID_DATA";
	case 0xD0: return "BSL_REP_MAGIC_ERROR";
	case 0xD1: return "BSL_REP_REPARTITION_ERROR";
	case 0xD2: return "BSL_REP_READ_FLASH_ERROR";
	case 0xD3: return "BSL_REP_MALLOC_ERROR";
	case 0xFE: return "BSL_REP_UNSUPPORTED_COMMAND";
	case 0xFF: return "BSL_REP_LOG";

		// YCC commands
	//case 0x19: return "YCC_CMD_UNLOCK_BOOTLOADER";  // Note: conflicts with BSL_CMD_CHECK_ROOT
	//case 0x1A: return "YCC_CMD_LOCK_BOOTLOADER";    // Note: conflicts with BSL_CMD_READ_CHIP_UID
	//case 0xCC: return "YCC_REP_SET_BOOTLOADER_SUCCESS"; // Note: conflicts with BSL_CMD_SEND_FLAG

	default: return "UNKNOWN_COMMAND";
	}
}

void print_all_bsl_commands() {
	printf("=== BSL Commands and Responses ===\n\n");

	printf("Command Codes:\n");
	printf("0x00: BSL_CMD_CONNECT\n");
	printf("0x01: BSL_CMD_START_DATA\n");
	printf("0x02: BSL_CMD_MIDST_DATA\n");
	printf("0x03: BSL_CMD_END_DATA\n");
	printf("0x04: BSL_CMD_EXEC_DATA\n");
	printf("0x05: BSL_CMD_NORMAL_RESET\n");
	printf("0x06: BSL_CMD_READ_FLASH\n");
	printf("0x07: BSL_CMD_READ_CHIP_TYPE\n");
	printf("0x08: BSL_CMD_READ_NVITEM\n");
	printf("0x09: BSL_CMD_CHANGE_BAUD\n");
	printf("0x0A: BSL_CMD_ERASE_FLASH\n");
	printf("0x0B: BSL_CMD_REPARTITION\n");
	printf("0x0C: BSL_CMD_READ_FLASH_TYPE\n");
	printf("0x0D: BSL_CMD_READ_FLASH_INFO\n");
	printf("0x0F: BSL_CMD_READ_SECTOR_SIZE\n");
	printf("0x10: BSL_CMD_READ_START\n");
	printf("0x11: BSL_CMD_READ_MIDST\n");
	printf("0x12: BSL_CMD_READ_END\n");
	printf("0x13: BSL_CMD_KEEP_CHARGE\n");
	printf("0x14: BSL_CMD_EXTTABLE\n");
	printf("0x15: BSL_CMD_READ_FLASH_UID\n");
	printf("0x16: BSL_CMD_READ_SOFTSIM_EID\n");
	printf("0x17: BSL_CMD_POWER_OFF\n");
	printf("0x19: BSL_CMD_CHECK_ROOT / YCC_CMD_UNLOCK_BOOTLOADER\n");
	printf("0x1A: BSL_CMD_READ_CHIP_UID / YCC_CMD_LOCK_BOOTLOADER\n");
	printf("0x1B: BSL_CMD_ENABLE_WRITE_FLASH\n");
	printf("0x1C: BSL_CMD_ENABLE_SECUREBOOT\n");
	printf("0x1D: BSL_CMD_IDENTIFY_START\n");
	printf("0x1E: BSL_CMD_IDENTIFY_END\n");
	printf("0x1F: BSL_CMD_READ_CU_REF\n");
	printf("0x20: BSL_CMD_READ_REFINFO\n");
	printf("0x21: BSL_CMD_DISABLE_TRANSCODE\n");
	printf("0x22: BSL_CMD_WRITE_APR_INFO\n");
	printf("0x23: BSL_CMD_CUST_DUMMY\n");
	printf("0x24: BSL_CMD_READ_RF_TRANSCEIVER_TYPE\n");
	printf("0x25: BSL_CMD_ENABLE_DEBUG_MODE\n");
	printf("0x26: BSL_CMD_DDR_CHECK\n");
	printf("0x27: BSL_CMD_SELF_REFRESH\n");
	printf("0x28: BSL_CMD_ENABLE_RAW_DATA\n");
	printf("0x29: BSL_CMD_READ_NAND_BLOCK_INFO\n");
	printf("0x2A: BSL_CMD_SET_FIRST_MODE\n");
	printf("0x2B: BSL_CMD_SET_RANDOM_DATA\n");
	printf("0x2C: BSL_CMD_SET_TIME_STAMP\n");
	printf("0x2D: BSL_CMD_READ_PARTITION\n");
	printf("0x2E: BSL_CMD_READ_VCUR_DATA\n");
	printf("0x2F: BSL_CMD_WRITE_VPAC_DATA\n");
	printf("0x31: BSL_CMD_MIDST_RAW_START\n");
	printf("0x32: BSL_CMD_FLUSH_DATA\n");
	printf("0x33: BSL_CMD_MIDST_RAW_START2\n");
	printf("0x34: BSL_CMD_ENABLE_UBOOT_LOG\n");
	printf("0x35: BSL_CMD_DUMP_UBOOT_LOG\n");
	printf("0x40: BSL_CMD_DISABLE_SELINUX\n");
	printf("0x41: BSL_CMD_AUTH_BEGIN\n");
	printf("0x42: BSL_CMD_AUTH_END\n");
	printf("0x43: BSL_CMD_EMMC_CID\n");
	printf("0x44: BSL_CMD_OPEN_WATCH_DOG\n");
	printf("0x45: BSL_CMD_CLOSE_WATCH_DOG\n");
	printf("0x46: BSL_CMD_POWEROFF_NOKEY\n");
	printf("0x47: BSL_CMD_WRITE_EFUSE\n");
	printf("0x48: BSL_CMD_READ_PARTITION_VALUE\n");
	printf("0x49: BSL_CMD_WRITE_PARTITION_VALUE\n");
	printf("0x50: BSL_CMD_WRITE_DOWNLOAD_TIMESTAMP\n");
	printf("0x51: BSL_CMD_PARTITION_SIGNATURE\n");
	printf("0xCC: BSL_CMD_SEND_FLAG / YCC_REP_SET_BOOTLOADER_SUCCESS\n");
	printf("0x7E: BSL_CMD_CHECK_BAUD\n");
	printf("0x7F: BSL_CMD_END_PROCESS\n");

	printf("\nResponse Codes:\n");
	printf("0x80: BSL_REP_ACK\n");
	printf("0x81: BSL_REP_VER\n");
	printf("0x82: BSL_REP_INVALID_CMD\n");
	printf("0x83: BSL_REP_UNKNOW_CMD\n");
	printf("0x84: BSL_REP_OPERATION_FAILED\n");
	printf("0x85: BSL_REP_NOT_SUPPORT_BAUDRATE\n");
	printf("0x86: BSL_REP_DOWN_NOT_START\n");
	printf("0x87: BSL_REP_DOWN_MULTI_START\n");
	printf("0x88: BSL_REP_DOWN_EARLY_END\n");
	printf("0x89: BSL_REP_DOWN_DEST_ERROR\n");
	printf("0x8A: BSL_REP_DOWN_SIZE_ERROR\n");
	printf("0x8B: BSL_REP_VERIFY_ERROR\n");
	printf("0x8C: BSL_REP_NOT_VERIFY\n");
	printf("0x8D: BSL_PHONE_NOT_ENOUGH_MEMORY\n");
	printf("0x8E: BSL_PHONE_WAIT_INPUT_TIMEOUT\n");
	printf("0x8F: BSL_PHONE_SUCCEED\n");
	printf("0x90: BSL_PHONE_VALID_BAUDRATE\n");
	printf("0x91: BSL_PHONE_REPEAT_CONTINUE\n");
	printf("0x92: BSL_PHONE_REPEAT_BREAK\n");
	printf("0x93: BSL_REP_READ_FLASH\n");
	printf("0x94: BSL_REP_READ_CHIP_TYPE\n");
	printf("0x95: BSL_REP_READ_NVITEM\n");
	printf("0x96: BSL_REP_INCOMPATIBLE_PARTITION\n");
	printf("0x97: BSL_REP_UNKNOWN_DEVICE\n");
	printf("0x98: BSL_REP_INVALID_DEVICE_SIZE\n");
	printf("0x99: BSL_REP_ILLEGAL_SDRAM\n");
	printf("0x9A: BSL_WRONG_SDRAM_PARAMETER\n");
	printf("0x9B: BSL_REP_READ_FLASH_INFO\n");
	printf("0x9C: BSL_REP_READ_SECTOR_SIZE\n");
	printf("0x9D: BSL_REP_READ_FLASH_TYPE\n");
	printf("0x9E: BSL_REP_READ_FLASH_UID\n");
	printf("0x9F: BSL_REP_READ_SOFTSIM_EID\n");
	printf("0xA0: BSL_ERROR_CHECKSUM\n");
	printf("0xA1: BSL_CHECKSUM_DIFF\n");
	printf("0xA2: BSL_WRITE_ERROR\n");
	printf("0xA3: BSL_CHIPID_NOT_MATCH\n");
	printf("0xA4: BSL_FLASH_CFG_ERROR\n");
	printf("0xA5: BSL_REP_DOWN_STL_SIZE_ERROR\n");
	printf("0xA7: BSL_REP_PHONE_IS_ROOTED\n");
	printf("0xAA: BSL_REP_SEC_VERIFY_ERROR\n");
	printf("0xAB: BSL_REP_READ_CHIP_UID\n");
	printf("0xAC: BSL_REP_NOT_ENABLE_WRITE_FLASH\n");
	printf("0xAD: BSL_REP_ENABLE_SECUREBOOT_ERROR\n");
	printf("0xAE: BSL_REP_IDENTIFY_START\n");
	printf("0xAF: BSL_REP_IDENTIFY_END\n");
	printf("0xB0: BSL_REP_READ_CU_REF\n");
	printf("0xB1: BSL_REP_READ_REFINFO\n");
	printf("0xB2: BSL_REP_CUST_DUMMY\n");
	printf("0xB3: BSL_REP_FLASH_WRITTEN_PROTECTION\n");
	printf("0xB4: BSL_REP_FLASH_INITIALIZING_FAIL\n");
	printf("0xB5: BSL_REP_RF_TRANSCEIVER_TYPE\n");
	printf("0xB6: BSL_REP_DDR_CHECK_ERROR\n");
	printf("0xB7: BSL_REP_SELF_REFRESH_ERROR\n");
	printf("0xB8: BSL_REP_READ_NAND_BLOCK_INFO\n");
	printf("0xB9: BSL_REP_RANDOM_DATA_ERROR\n");
	printf("0xBA: BSL_REP_READ_PARTITION\n");
	printf("0xBB: BSL_REP_DUMP_UBOOT_LOG\n");
	printf("0xBC: BSL_REP_READ_VCUR_DATA\n");
	printf("0xBD: BSL_REP_AUTH_M1_DATA\n");
	printf("0xBE: BSL_REP_READ_PARTITION_VALUE\n");
	printf("0xBF: BSL_REP_UNSUPPORT_PARTITION\n");
	printf("0xC0: BSL_REP_EMMC_CID_DATA\n");
	printf("0xD0: BSL_REP_MAGIC_ERROR\n");
	printf("0xD1: BSL_REP_REPARTITION_ERROR\n");
	printf("0xD2: BSL_REP_READ_FLASH_ERROR\n");
	printf("0xD3: BSL_REP_MALLOC_ERROR\n");
	printf("0xFE: BSL_REP_UNSUPPORTED_COMMAND\n");
	printf("0xFF: BSL_REP_LOG\n");
}
partition_t* partition_list_d(spdio_t* io) {
	long long size;
	unsigned i, n = 0;
	FILE* fo = nullptr;
	partition_t* ptable = NEWN partition_t[128 * sizeof(partition_t)];
	if (ptable == nullptr) return nullptr;
	DEG_LOG(OP, "Reading partition table through compatibility method.");
	if (selected_ab < 0) select_ab(io);
	int verbose = io->verbose;
	io->verbose = -1;
	DBG_LOG("  0 %36s  %lldKB\n", "splloader",(long long)g_spl_size / 1024);
	for (i = 0; i < CommonPartitionsCount && n < 128; ++i) {
		const char* part = CommonPartitions[i];
		long long result = check_partition(io, part, 0);
		//exist
		
		if (result) {
			size = check_partition(io, part, 1);
			
			//����
			if (part != "splloader") {
				strncpy(ptable[n].name, part, sizeof(ptable[n].name) - 1);
				ptable[n].name[sizeof(ptable[n].name) - 1] = '\0'; // ȷ���ַ�����ֹ
				ptable[n].size = size;
				n++;
			}
			if (part != "splloader") { size = size / 1024 / 1024; DBG_LOG("  %d %36s  %lldMB\n", n, part, size); }
			
		}
	}
	io->verbose = verbose;
	//if (strcmp(fn, "-")) DEG_LOG(OP,"Tryed to save partition table to %s", fn);
	DEG_LOG(I,"Compatibility-method mode will not save partition table xml automatically.");
	DEG_LOG(I, "You can get partition xml by `part_table` command manually.");
	DEG_LOG(I,"Total number of partitions: %d", n);
	io->ptable = nullptr;
	io->part_count_c = n;
	return ptable;
}
void add_partition(spdio_t* io, const char* name, long long size) {
	partition_t* ptable = NEWN partition_t[128 * sizeof(partition_t)];
	if (ptable == nullptr) return;
	int k = io->part_count_c;
	for (int i = 0; i < io->part_count_c; i++) {
		strncpy(ptable[i].name, io->Cptable[i].name, sizeof(ptable[i].name) - 1);
		ptable[i].name[sizeof(ptable[i].name) - 1] = '\0'; // ȷ���ַ�����ֹ
		ptable[i].size = io->Cptable[i].size;
	}
	for (int i = 0; i < io->part_count_c; i++) {
		if (strcmp(io->Cptable[i].name, name) == 0) {
			DEG_LOG(W, "Partition %s already exists", name);
			return;
		}
	}
	strncpy(ptable[k].name, name, sizeof(ptable[k].name) - 1);
	ptable[k].name[sizeof(ptable[k].name) - 1] = '\0'; // ȷ���ַ�����ֹ
	ptable[k].size = size;
	io->Cptable = ptable;
	io->part_count_c++;
	DEG_LOG(I,"Partition %s added.",name);
	DEG_LOG(I, "You can get partition xml manually by `part_table` command.");
	DEG_LOG(I,"By default, the partition is added to the last position of the partition table.");
	DEG_LOG(I,"You may need to re-flash the firmware after repartitioning using this partition table.");
}
void repartition(spdio_t *io, const char *fn) {
	uint8_t *buf = io->temp_buf;
	int n = scan_xml_partitions(io, fn, buf, 0xffff);
	// print_mem(stderr, io->temp_buf, n * 0x4c);
	encode_msg_nocpy(io, BSL_CMD_REPARTITION, n * 0x4c);
	if (!send_and_check(io)) gpt_failed = 0;
}

void erase_partition(spdio_t *io, const char *name, int CMethod) {
	double rtime = get_time();
	int timeout0 = io->timeout;
	char name0[36];
	if (!strcmp(name, "userdata")) {
		char *miscbuf = NEWN char[0x800];
		if (!miscbuf) ERR_EXIT("memory alloc failed\n");
		memset(miscbuf, 0, 0x800);
		strcpy(miscbuf, "boot-recovery");
		strcpy(miscbuf + 0x40, "recovery\n--wipe_data\n");
		w_mem_to_part_offset(io, "misc", 0, (uint8_t *)miscbuf, 0x800, 0x1000, CMethod);
		delete[](miscbuf);
		select_partition(io, "persist", 0, 0, BSL_CMD_ERASE_FLASH);
		strcpy(name0, "persist");
	}
	else if (!strcmp(name, "all")) {
		io->timeout = 100000;
		select_partition(io, "erase_all", 0xffffffff, 0, BSL_CMD_ERASE_FLASH);
		strcpy(name0, "erase_all");
	}
	else {
		select_partition(io, name, 0, 0, BSL_CMD_ERASE_FLASH);
		strcpy(name0, name);
	}
	if (!send_and_check(io)) {
		double etime = get_time();
		double time_spent = etime - rtime;
		
		DEG_LOG(I, "Erase partition %s successfully", name0); 
		DEG_LOG(I, "Cost time %.6f seconds", time_spent);
	}
	io->timeout = timeout0;
}

void load_partition(spdio_t *io, const char *name,
	const char *fn, unsigned step, int CMethod) {
	uint64_t offset, len, n64;
	unsigned mode64, n, step0 = step; int ret;
	FILE *fi;
	double rtime = get_time();
	if (strstr(name, "runtimenv")) { erase_partition(io, name, CMethod); return; }
	if (!strcmp(name, "calinv")) { return; } //skip calinv
	DEG_LOG(OP, "Start to write partition %s", name);
	DEG_LOG(I, "Type CTRL + C to cancel...");
	start_signal();
	fi = fopen(fn, "rb");
	if (!fi) ERR_EXIT("fopen(load) failed\n");

	uint8_t header[4], is_simg = 0;
	if (fread(header, 1, 4, fi) != 4)
		ERR_EXIT("fread(load) failed\n");
	if (0xED26FF3A == *(uint32_t *)header) is_simg = 1;
	fseeko(fi, 0, SEEK_END);
	len = ftello(fi);
	fseek(fi, 0, SEEK_SET);
	DEG_LOG(I,"File size : 0x%llx\n", (long long)len);

	mode64 = len >> 32;
	select_partition(io, name, len, mode64, BSL_CMD_START_DATA);
	if (send_and_check(io)) { fclose(fi); return; }
	if (isCancel) { fclose(fi); return; }
	unsigned long long time_start = GetTickCount64();
#if !USE_LIBUSB
	if (Da_Info.bSupportRawData) {
		if (Da_Info.bSupportRawData > 1) {
			encode_msg_nocpy(io, BSL_CMD_MIDST_RAW_START2, 0);
			if (send_and_check(io)) { Da_Info.bSupportRawData = 0; goto fallback_load; }
		}
		step = Da_Info.dwFlushSize << 10;
		uint8_t *rawbuf = NEWN uint8_t[step + 1];
		if (!rawbuf) ERR_EXIT("malloc failed\n");

		for (offset = 0; (n64 = len - offset); offset += n) {
			if (isCancel) {  delete[](rawbuf); return; }
			n = (unsigned)(n64 > step ? step : n64);
			if (Da_Info.bSupportRawData == 1) {
				uint32_t *data = (uint32_t *)io->temp_buf;
				uint32_t t32 = offset >> 32;
				WRITE32_LE(data, offset);
				WRITE32_LE(data + 1, t32);
				WRITE32_LE(data + 2, n);
				encode_msg_nocpy(io, BSL_CMD_MIDST_RAW_START, 12);
				if (send_and_check(io)) {
					if (offset) break;
					else { delete[](rawbuf); step = step0; Da_Info.bSupportRawData = 0; goto fallback_load; }
				}
			}
			// �޸�������������⣬ȷ�� n ������ rawbuf ��ʵ�ʷ����С
			if (n > step + 1) {
				// ���� n �����ֵ����ֹ���
				n = step + 1;
			}
			if (fread(rawbuf, 1, n, fi) != n) ERR_EXIT("fread(load) failed\n");
#if USE_LIBUSB
			int err = libusb_bulk_transfer(io->dev_handle, io->endp_out, rawbuf, n, &ret, io->timeout); //libusb will fail with rawbuf
			if (err < 0) ERR_EXIT("usb_send failed : %s\n", libusb_error_name(err));
#else
			ret = call_Write(io->handle, rawbuf, n);
#endif
			if (io->verbose >= 1) DEG_LOG(OP,"send (%d)", n);
			if (ret != (int)n)
				ERR_EXIT("usb_send failed (%d / %d)\n", ret, n);
			if (is_simg) ret = recv_msg_timeout(io, 100000);
			else ret = recv_msg_timeout(io, 15000);
			if (!ret) {
				if (n == n64) ERR_EXIT("signature verification of \"%s\" failed or timeout reached\n", name);
				else ERR_EXIT("timeout reached\n");
			}
			if ((ret = recv_type(io)) != BSL_REP_ACK) {
				const char* name = get_bsl_enum_name(ret);
				DEG_LOG(E,"excepted response (%s : 0x%04x)",name, ret);
				break;
			}
			print_progress_bar(io,offset + n, len, time_start);
		}
		delete[](rawbuf);
	}
	else {
#endif
fallback_load:
		for (offset = 0; (n64 = len - offset); offset += n) {
			if (isCancel) {fclose(fi); return; }
			n = (unsigned)(n64 > step ? step : n64);
			if (fread(io->temp_buf, 1, n, fi) != n)
				ERR_EXIT("fread(load) failed\n");
			encode_msg_nocpy(io, BSL_CMD_MIDST_DATA, n);
			send_msg(io);
			if (is_simg) ret = recv_msg_timeout(io, 100000);
			else ret = recv_msg_timeout(io, 15000);
			if (!ret) {
				if (n == n64) ERR_EXIT("Signature verification of \"%s\" failed or timeout reached\n", name);
				else ERR_EXIT("timeout reached\n");
			}
			if ((ret = recv_type(io)) != BSL_REP_ACK) {
				const char* name = get_bsl_enum_name(ret);
				DEG_LOG(E,"excepted response (%s : 0x%04x)",name, ret);
				break;
			}
			print_progress_bar(io,offset + n, len, time_start);
		}
#if !USE_LIBUSB
	}
#endif
	fclose(fi);
	encode_msg_nocpy(io, BSL_CMD_END_DATA, 0);
	if (!send_and_check(io)) {
		double etime = get_time();
		double time_spent = etime - rtime;
		
		DEG_LOG(I, "Write partition %s successfully, target: 0x%llx, written: 0x%llx",
			name, (long long)len, (long long)offset);
		DEG_LOG(I, "Cost time %.6f seconds", time_spent);
	}
}

void load_partition_force(spdio_t *io, const int id, const char *fn, unsigned step, int CMethod) {
	int i, j; char a;
	uint8_t *buf = io->temp_buf;
	double rtime = get_time();
	char name[] = "w_force";
	if(CMethod){
		for (i = 0; i < io->part_count; i++) {
			memset(buf, 0, 36 * 2);
			if (i == id)
				for (j = 0; (a = name[j]); j++)
					buf[j * 2] = a;
			else
				for (j = 0; (a = (*(io->ptable + i)).name[j]); j++)
					buf[j * 2] = a;
			if (!j) ERR_EXIT("Empty partition name\n");
			if (i + 1 == io->part_count) WRITE32_LE(buf + 0x48, ~0);
			else WRITE32_LE(buf + 0x48, (*(io->ptable + i)).size >> 20);
			buf += 0x4c;
		}
		encode_msg_nocpy(io, BSL_CMD_REPARTITION, io->part_count * 0x4c);
		if (send_and_check(io)) return; //repart failed
		load_partition(io, name, fn, step, CMethod);
		buf = io->temp_buf;
		for (i = 0; i < io->part_count; i++) {
			memset(buf, 0, 36 * 2);
			for (j = 0; (a = (*(io->ptable + i)).name[j]); j++)
				buf[j * 2] = a;
			if (!j) ERR_EXIT("Empty partition name\n");
			if (i + 1 == io->part_count) WRITE32_LE(buf + 0x48, ~0);
			else WRITE32_LE(buf + 0x48, (*(io->ptable + i)).size >> 20);
			buf += 0x4c;
		}
		encode_msg_nocpy(io, BSL_CMD_REPARTITION, io->part_count * 0x4c);
		if (!send_and_check(io)) { 
			double etime = get_time();
			double time_spent = etime - rtime;
			
			DEG_LOG(I, "Force write %s successfully", (*(io->ptable + id)).name); 
			DEG_LOG(I, "Cost time %.6f seconds", time_spent);
		}
	}
	else{
		for (i = 0; i < io->part_count_c; i++) {
			memset(buf, 0, 36 * 2);
			if (i == id)
				for (j = 0; (a = name[j]); j++)
					buf[j * 2] = a;
			else
				for (j = 0; (a = (*(io->Cptable + i)).name[j]); j++)
					buf[j * 2] = a;
			if (!j) ERR_EXIT("Empty partition name\n");
			if (i + 1 == io->part_count_c) WRITE32_LE(buf + 0x48, ~0);
			else WRITE32_LE(buf + 0x48, (*(io->Cptable + i)).size >> 20);
			buf += 0x4c;
		}
		encode_msg_nocpy(io, BSL_CMD_REPARTITION, io->part_count_c * 0x4c);
		if (send_and_check(io)) return; //repart failed
		load_partition(io, name, fn, step, CMethod);
		buf = io->temp_buf;
		for (i = 0; i < io->part_count_c; i++) {
			memset(buf, 0, 36 * 2);
			for (j = 0; (a = (*(io->Cptable + i)).name[j]); j++)
				buf[j * 2] = a;
			if (!j) ERR_EXIT("Empty partition name\n");
			if (i + 1 == io->part_count_c) WRITE32_LE(buf + 0x48, ~0);
			else WRITE32_LE(buf + 0x48, (*(io->Cptable + i)).size >> 20);
			buf += 0x4c;
		}
		encode_msg_nocpy(io, BSL_CMD_REPARTITION, io->part_count_c * 0x4c);
		if (!send_and_check(io)) { 
			double etime = get_time();
			double time_spent = etime - rtime;
			
			DEG_LOG(I, "Force write %s successfully", (*(io->Cptable + id)).name); 
			DEG_LOG(I, "Cost time %.6f seconds", time_spent);
		}
	}
}

unsigned short const crc16_table[256] = {
	0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
	0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
	0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
	0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
	0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
	0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
	0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
	0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
	0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
	0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
	0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
	0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
	0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
	0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
	0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
	0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
	0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
	0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
	0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
	0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
	0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
	0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
	0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
	0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
	0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
	0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
	0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
	0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
	0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
	0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
	0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
	0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040
};

unsigned short crc16(unsigned short crc, unsigned char const *buffer, unsigned int len) {
	while (len--)
		crc = (unsigned short)((crc >> 8) ^ (crc16_table[(crc ^ (*buffer++)) & 0xff]));
	return crc;
}

void load_nv_partition(spdio_t *io, const char *name,
	const char *fn, unsigned step) {
	double rtime = get_time();
	size_t offset, rsz;
	unsigned n; int ret;
	size_t len = 0;
	uint8_t *mem;
	uint16_t crc = 0;
	uint32_t cs = 0;

	mem = loadfile(fn, &len, 0);
	if (!mem) ERR_EXIT("Load file(\"%s\") failed\n", fn);

	uint8_t *mem0 = mem;
	if (*(uint32_t *)mem == 0x4e56) mem += 0x200;
	len = 0;
	len += sizeof(uint32_t);

	uint16_t tmp[2];
	while (1) {
		tmp[0] = 0;
		tmp[1] = 0;
		memcpy(tmp, mem + len, sizeof(tmp));
		if (!tmp[1]) { DEG_LOG(E,"Broken NV file, skipped!"); return; }
		len += sizeof(tmp);
		len += tmp[1];

		uint32_t doffset = ((len + 3) & 0xFFFFFFFC) - len;
		len += doffset;
		if (*(uint16_t *)(mem + len) == 0xffff) {
			len += 8;
			break;
		}
	}
	crc = crc16(crc, mem + 2, len - 2);
	WRITE16_BE(mem, crc);
	for (offset = 0; offset < len; offset++) cs += mem[offset];
	DEG_LOG(I,"File size : 0x%zx", len);

	struct pkt {
		uint16_t name[36];
		uint32_t size, cs;
	} *pkt_ptr;
	pkt_ptr = (struct pkt *) io->temp_buf;
	ret = copy_to_wstr(pkt_ptr->name, 36, name);
	if (ret) ERR_EXIT("name too long\n");
	WRITE32_LE(&pkt_ptr->size, len);
	WRITE32_LE(&pkt_ptr->cs, cs);
	encode_msg_nocpy(io, BSL_CMD_START_DATA, sizeof(struct pkt));
	if (send_and_check(io)) { delete[](mem0); return; }

	for (offset = 0; (rsz = len - offset); offset += n) {
		if (isCancel) { delete[](mem0); return; }
		n = rsz > step ? step : rsz;
		memcpy(io->temp_buf, &mem[offset], n);
		encode_msg_nocpy(io, BSL_CMD_MIDST_DATA, n);
		send_msg(io);
		ret = recv_msg_timeout(io, 15000);
		if (!ret) ERR_EXIT("timeout reached\n");
		if ((ret = recv_type(io)) != BSL_REP_ACK) {
			const char* name = get_bsl_enum_name(ret);
			DEG_LOG(E,"excepted response (%s : 0x%04x)",name, ret);
			break;
		}
	}
	delete[](mem0);
	encode_msg_nocpy(io, BSL_CMD_END_DATA, 0);
	if (!send_and_check(io)) {
		double etime = get_time();
		double t = etime - rtime;
		
		DEG_LOG(I, "Write NV partition %s successfully, target: 0x%llx, written: 0x%llx\n",
			name, (long long)len, (long long)offset);
		DEG_LOG(I, "Cost time %.6f seconds", t);
	}
}
void signal_handler(int sig) {
	//Cancallation handler
	isCancel = 1;
	signal(SIGINT, SIG_DFL);
	DBG_LOG("\nCancelled.\n");
}
double get_time() {
#if defined(_WIN32) || defined(_WIN64)
	// Windows ʵ��
	static LARGE_INTEGER frequency;
	static int initialized = 0;
	if (!initialized) {
		QueryPerformanceFrequency(&frequency);
		initialized = 1;
	}

	LARGE_INTEGER counter;
	QueryPerformanceCounter(&counter);
	return (double)counter.QuadPart / frequency.QuadPart;
#else
	// Unix-like ϵͳʵ��
	struct timespec ts;
#ifdef CLOCK_MONOTONIC
	clock_gettime(CLOCK_MONOTONIC, &ts);
#else
	// ���÷�����ʹ�ýϵ;��ȵ� clock()
	return (double)clock() / CLOCKS_PER_SEC;
#endif
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
}

void find_partition_size_new(spdio_t *io, const char *name, unsigned long long *offset_ptr) {
	int ret;
	char *name_tmp = NEWN char[strlen(name) + 5 + 1];
	if (name_tmp == nullptr) return;
	sprintf(name_tmp, "%s_size", name);
	select_partition(io, name_tmp, 0x80, 0, BSL_CMD_READ_START);
	delete[](name_tmp);
	if (send_and_check(io)) {
		encode_msg_nocpy(io, BSL_CMD_READ_END, 0);
		send_and_check(io);
		return;
	}

	//uint32_t data[2] = { 0x80,0 };
	uint32_t *data = (uint32_t *)io->temp_buf;
	WRITE32_LE(data, 0x80);
	WRITE32_LE(data + 1, 0);
	encode_msg_nocpy(io, BSL_CMD_READ_MIDST, 8);
	send_msg(io);
	ret = recv_msg(io);
	if (!ret) ERR_EXIT("timeout reached\n");
	if (recv_type(io) == BSL_REP_READ_FLASH) {
		ret = sscanf((char *)(io->raw_buf + 4), "size:%*[^:]: 0x%llx", offset_ptr);
		if (ret != 1) ret = sscanf((char *)(io->raw_buf + 4), "partition %*s total size: 0x%llx", offset_ptr); // new lk
		DEG_LOG(I,"Partition size of device: %s, 0x%llx\n", name, *offset_ptr);
	}
	encode_msg_nocpy(io, BSL_CMD_READ_END, 0);
	send_and_check(io);
}

uint64_t check_partition(spdio_t *io, const char *name, int need_size) {
	uint32_t t32; uint64_t n64;
	unsigned long long offset = 0; //uint64_t differs between platforms
	int ret, i, end = 20;
	char name_tmp[36];

	if (selected_ab > 0 && strcmp(name, "uboot") == 0) return 0;
	if (strstr(name, "fixnv")) {
		if (selected_ab > 0) {
			size_t namelen = strlen(name);
			if (strcmp(name + namelen - 2, "_a") && strcmp(name + namelen - 2, "_b")) return 0;
		}
		strcpy(name_tmp, name);
		char *dot = strrchr(name_tmp, '1');
		if (dot != nullptr) *dot = '2';
		name = name_tmp;
	}
	else if (strstr(name, "runtimenv")) {
		size_t namelen = strlen(name);
		if (0 == strcmp(name + namelen - 2, "_a") || 0 == strcmp(name + namelen - 2, "_b")) return 0;
		strcpy(name_tmp, name);
		char *dot = strrchr(name_tmp, '1');
		if (dot != nullptr) *dot = '2';
		name = name_tmp;
	}

	if (selected_ab > 0) {
		find_partition_size_new(io, name, &offset);
		if (offset) {
			if (need_size) return offset;
			else return 1;
		}
	}

	select_partition(io, name, 0x8, 0, BSL_CMD_READ_START);
	if (send_and_check(io)) {
		encode_msg_nocpy(io, BSL_CMD_READ_END, 0);
		send_and_check(io);
		return 0;
	}

	//uint32_t data[2] = { 0x8, 0 };
	uint32_t *data = (uint32_t *)io->temp_buf;
	WRITE32_LE(data, 8);
	WRITE32_LE(data + 1, 0);
	encode_msg_nocpy(io, BSL_CMD_READ_MIDST, 8);

	send_msg(io);
	ret = recv_msg(io);
	if (!ret) ERR_EXIT("timeout reached\n");
	if (recv_type(io) == BSL_REP_READ_FLASH) ret = 1;
	else ret = 0;
	encode_msg_nocpy(io, BSL_CMD_READ_END, 0);
	send_and_check(io);
	if (0 == ret || 0 == need_size) return ret;

	int incrementing = 1;
	select_partition(io, name, 0xffffffff, 0, BSL_CMD_READ_START);
	if (send_and_check(io)) {
		//NAND flash !!!
		end = 10;
		encode_msg_nocpy(io, BSL_CMD_READ_END, 0);
		send_and_check(io);
		for (i = 21; i >= end;) {
			n64 = offset + (1ll << i) - (1ll << end);
			select_partition(io, name, n64, 0, BSL_CMD_READ_START);
			send_msg(io);
			ret = recv_msg(io);
			if (!ret) ERR_EXIT("timeout reached\n");
			ret = recv_type(io);
			if (incrementing) {
				if (ret != BSL_REP_ACK) {
					offset += 1ll << (i - 1);
					i -= 2;
					incrementing = 0;
				}
				else i++;
			}
			else {
				if (ret == BSL_REP_ACK) offset += (1ll << i);
				i--;
			}
			encode_msg_nocpy(io, BSL_CMD_READ_END, 0);
			send_and_check(io);
		}
		offset -= (1ll << end);
	}
	else {
		for (i = 21; i >= end;) {
			uint32_t *data = (uint32_t *)io->temp_buf;
			n64 = offset + (1ll << i) - (1ll << end);
			WRITE32_LE(data, 4);
			WRITE32_LE(data + 1, n64);
			t32 = n64 >> 32;
			WRITE32_LE(data + 2, t32);

			encode_msg_nocpy(io, BSL_CMD_READ_MIDST, 12);
			send_msg(io);
			ret = recv_msg(io);
			if (!ret) ERR_EXIT("timeout reached\n");
			ret = recv_type(io);
			if (incrementing) {
				if (ret != BSL_REP_READ_FLASH) {
					offset += 1ll << (i - 1);
					i -= 2;
					incrementing = 0;
				}
				else i++;
			}
			else {
				if (ret == BSL_REP_READ_FLASH) offset += (1ll << i);
				i--;
			}
		}
	}
	// NAND detection
	if (end == 10) Da_Info.dwStorageType = 0x101;
	if(io->verbose != -1) DEG_LOG(I,"Partition check: %s, size : 0x%llx", name, offset);
	encode_msg_nocpy(io, BSL_CMD_READ_END, 0);
	send_and_check(io);
	return offset;
}

void get_partition_info(spdio_t *io, const char *name, int need_size) {
	int i;
	char name_ab[36];
	int verbose = io->verbose;
	io->verbose = 0;

	if (isdigit(name[0])) {
		i = atoi(name);
		if (i == 0) {
			strcpy(gPartInfo.name, "splloader");
			gPartInfo.size = (long long)g_spl_size;
			io->verbose = verbose;
			return;
		}
		if (gpt_failed == 1) io->ptable = partition_list(io, fn_partlist, &io->part_count);
		if (i > io->part_count) {
			DEG_LOG(E,"part not exist");
			gPartInfo.size = 0;
			io->verbose = verbose;
			return;
		}
		strcpy(gPartInfo.name, (*(io->ptable + i - 1)).name);
		gPartInfo.size = (*(io->ptable + i - 1)).size;
		io->verbose = verbose;
		return;
	}

	if (!strncmp(name, "splloader", 9)) {
		strcpy(gPartInfo.name, name);
		gPartInfo.size = (long long)g_spl_size;
		io->verbose = verbose;
		return;
	}
	if (io->part_count) {
		if (selected_ab > 0) snprintf(name_ab, sizeof(name_ab), "%s_%c", name, 96 + selected_ab);
		for (i = 0; i < io->part_count; i++) {
			if (!strcmp(name, (*(io->ptable + i)).name)) break;
			if (selected_ab > 0 && !strcmp(name_ab, (*(io->ptable + i)).name)) {
				name = name_ab;
				break;
			}
		}
		if (i < io->part_count) {
			strcpy(gPartInfo.name, name);
			gPartInfo.size = (*(io->ptable + i)).size;
		}
		else gPartInfo.size = 0;
		io->verbose = verbose;
		return;
	}

	if (selected_ab < 0) select_ab(io);
	gPartInfo.size = check_partition(io, name, need_size);
	if (!gPartInfo.size && selected_ab > 0) {
		snprintf(name_ab, sizeof(name_ab), "%s_%c", name, 96 + selected_ab);
		gPartInfo.size = check_partition(io, name_ab, need_size);
		name = name_ab;
	}
	if (!gPartInfo.size) {
		DEG_LOG(E,"part not exist");
		io->verbose = verbose;
		return;
	}
	strcpy(gPartInfo.name, name);
	io->verbose = verbose;
}

uint64_t str_to_size(const char *str) {
	char *end; int shl = 0; uint64_t n;
	n = strtoull(str, &end, 0);
	if (*end) {
		char suffix = tolower(*end);
		if (suffix == 'k') shl = 10;
		else if (suffix == 'm') shl = 20;
		else if (suffix == 'g') shl = 30;
		else ERR_EXIT("unknown size suffix\n");
	}
	if (shl) {
		int64_t tmp = n;
		tmp >>= 63 - shl;
		if (tmp && ~tmp)
			ERR_EXIT("size overflow on multiply\n");
	}
	return n << shl;
}

uint64_t str_to_size_ubi(const char *str, int *nand_info) {
	if (strncmp(str, "ubi", 3)) return str_to_size(str);
	else {
		char *end;
		uint64_t n;
		n = strtoull(&str[3], &end, 0);
		if (*end) {
			char suffix = tolower(*end);
			if (suffix == 'm') {
				int block = (int)(n * (1024 / nand_info[2]) + n * (1024 / nand_info[2]) / (512 / nand_info[1]) + 1);
				return 1024 * (nand_info[2] - 2 * nand_info[0]) * block;
			}
			else {
				DEG_LOG(W,"Only support mb as unit, will not treat kb/gb as ubi size");
				return str_to_size(&str[3]);
			}
		}
		else return n;
	}
}
void start_signal() {
	signal(SIGINT, signal_handler);
	isCancel = 0;
}
void dump_partitions(spdio_t *io, const char *fn, int *nand_info, unsigned step) {
	const char *part1 = "Partitions>";
	char *src, *p;
	int part1_len = strlen(part1), found = 0, stage = 0, ubi = 0;
	size_t size = 0;
	partition_t *partitions = NEWN partition_t[128 * sizeof(partition_t)];
	if (partitions == nullptr) return;
	DEG_LOG(OP, "Start to read partitions");
	DEG_LOG(I, "Type CTRL + C to cancel...");
	start_signal();
	if (!strncmp(fn, "ubi", 3)) ubi = 1;
	src = (char *)loadfile(fn, &size, 1);
	if (!src) ERR_EXIT("Load file failed\n");
	src[size] = 0;
	p = src;

	for (;;) {
		int i, a = *p++, n;
		char c;

		if (a == ' ' || a == '\t' || a == '\n' || a == '\r') continue;

		if (a != '<') {
			if (!a) break;
			if (stage != 1) continue;
			ERR_EXIT("xml: unexpected symbol\n");
		}

		if (!memcmp(p, "!--", 3)) {
			p = strstr(p + 3, "--");
			if (!p || !((p[-1] - '!') | (p[-2] - '<')) || p[2] != '>')
				ERR_EXIT("xml: unexpected syntax\n");
			p += 3;
			continue;
		}

		if (stage != 1) {
			stage += !memcmp(p, part1, part1_len);
			if (stage > 2)
				ERR_EXIT("xml: more than one partition lists\n");
			p = strchr(p, '>');
			if (!p) ERR_EXIT("xml: unexpected syntax\n");
			p++;
			continue;
		}

		if (*p == '/' && !memcmp(p + 1, part1, part1_len)) {
			p = p + 1 + part1_len;
			stage++;
			continue;
		}

		i = sscanf(p, "Partition id=\"%35[^\"]\" size=\"%lli\"/%n%c", partitions[found].name, &partitions[found].size, &n, &c);
		if (i != 3 || c != '>')
			ERR_EXIT("xml: unexpected syntax\n");
		p += n + 1;
		found++;
		if (found >= 128) break;
	}
	if (p - 1 != src + size) ERR_EXIT("xml: zero byte");
	if (stage != 2) ERR_EXIT("xml: unexpected syntax\n");

	for (int i = 0; i < found; i++) {
		if (isCancel) { delete[](src);delete[](partitions); return; }
		DBG_LOG("Partition %d: name=%s, size=%llim\n", i + 1, partitions[i].name, partitions[i].size);
		if (!strncmp(partitions[i].name, "userdata", 8)) continue;

		get_partition_info(io, partitions[i].name, 0);
		if (!gPartInfo.size) continue;
		if (!strncmp(partitions[i].name, "splloader", 9)) gPartInfo.size = (long long)g_spl_size;
		else if (0xffffffff == partitions[i].size) gPartInfo.size = check_partition(io, gPartInfo.name, 1);
		else if (ubi) {
			int block = (int)(partitions[i].size * (1024 / nand_info[2]) + partitions[i].size * (1024 / nand_info[2]) / (512 / nand_info[1]) + 1);
			gPartInfo.size = 1024 * (nand_info[2] - 2 * nand_info[0]) * block;
		}
		else gPartInfo.size = partitions[i].size << 20;

		char dfile[40];
		snprintf(dfile, sizeof(dfile), "%s.bin", partitions[i].name);
		dump_partition(io, gPartInfo.name, 0, gPartInfo.size, dfile, step);
	}
	if (selected_ab > 0) { DBG_LOG("saving slot info\n"); dump_partition(io, "misc", 0, 1048576, "misc.bin", step); }

	if (savepath && savepath[0]) {
		DEG_LOG(OP,"Saving dump list");
		FILE *fo = my_fopen(fn, "wb");
		if (fo) { fwrite(src, 1, size, fo); fclose(fo); }
		else DEG_LOG(W,"Create dump list failed, skipped.");
	}
	delete[](src);
	delete[](partitions);
}

int ab_compare_slots(const slot_metadata *a, const slot_metadata *b);
void load_partitions(spdio_t *io, const char *path, unsigned step, int force_ab, int CMethod) {
	DEG_LOG(OP,"Start to write partitions");
	DEG_LOG(I,"Type CTRL + C to cancel...");
	start_signal();
	typedef struct {
		char name[36];
		char file_path[1024];
		int written_flag;
	} partition_info_t;
	size_t namelen;
	char miscname[1024] = { 0 };
	int VAB = 0; // slot_in_name
	int partition_count = 0;
	partition_info_t *partitions = NEWN partition_info_t[128 * sizeof(partition_info_t)];
	if (partitions == nullptr) return;
	char *fn;
#if _WIN32
	char searchPath[ARGV_LEN];
	snprintf(searchPath, ARGV_LEN, "%s\\*", path);

	WIN32_FIND_DATAA findData;
	HANDLE hFind = FindFirstFileA(searchPath, &findData);

	if (hFind == INVALID_HANDLE_VALUE) {
		DEG_LOG(E,"Failed to open directory.\n");
		return;
	}
	for (fn = findData.cFileName; FindNextFileA(hFind, &findData); fn = findData.cFileName) {
		if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
		namelen = strlen(fn);
		if (namelen >= 4) {
			if (!strcmp(fn + namelen - 4, ".xml") ||
				!strcmp(fn + namelen - 4, ".exe") ||
				!strcmp(fn + namelen - 4, ".txt")) continue;
		}
		if (!strncmp(fn, "pgpt", 4) ||
			!strncmp(fn, "sprdpart", 8) ||
			!strncmp(fn, "fdl", 3) ||
			!strncmp(fn, "lk", 2) ||
			!strncmp(fn, "0x", 2) ||
			!strncmp(fn, "custom_exec", 11)) continue;

		snprintf(partitions[partition_count].file_path, sizeof(partitions[partition_count].file_path), "%s/%s", path, fn);
		char *dot = strrchr(fn, '.');
		if (dot != nullptr) *dot = '\0';
		namelen = strlen(fn);
		if (namelen >= 4 && strcmp(fn + namelen - 4, "_bak") == 0) continue;
		if (!strcmp(fn, "misc")) snprintf(miscname, 1024, "%s", partitions[partition_count].file_path);
		if (namelen > 2) {
			if (!strcmp(fn + namelen - 2, "_a")) VAB |= 1;
			else if (!strcmp(fn + namelen - 2, "_b")) VAB |= 2;
		}

		strcpy(partitions[partition_count].name, fn);
		partitions[partition_count].written_flag = 0;
		partition_count++;
	}
	FindClose(hFind);
#else
	DIR *dir;
	struct dirent *entry;

	if ((dir = opendir(path)) == nullptr || (entry = readdir(dir)) == nullptr) {
		DEG_LOG(E,"Failed to open directory.\n");
		return;
	}
	for (fn = entry->d_name; (entry = readdir(dir)); fn = entry->d_name) {
		if (entry->d_type == DT_DIR) continue;
		namelen = strlen(fn);
		if (namelen >= 4) {
			if (!strcmp(fn + namelen - 4, ".xml") ||
				!strcmp(fn + namelen - 4, ".exe") ||
				!strcmp(fn + namelen - 4, ".txt")) continue;
		}
		if (!strncmp(fn, "pgpt", 4) ||
			!strncmp(fn, "sprdpart", 8) ||
			!strncmp(fn, "fdl", 3) ||
			!strncmp(fn, "lk", 2) ||
			!strncmp(fn, "0x", 2) ||
			!strncmp(fn, "custom_exec", 11)) continue;

		snprintf(partitions[partition_count].file_path, sizeof(partitions[partition_count].file_path), "%s/%s", path, fn);
		char *dot = strrchr(fn, '.');
		if (dot != nullptr) *dot = '\0';
		namelen = strlen(fn);
		if (namelen >= 4 && strcmp(fn + namelen - 4, "_bak") == 0) continue;
		if (!strcmp(fn, "misc")) snprintf(miscname, 1024, "%s", partitions[partition_count].file_path);
		if (namelen > 2) {
			if (!strcmp(fn + namelen - 2, "_a")) VAB |= 1;
			else if (!strcmp(fn + namelen - 2, "_b")) VAB |= 2;
		}

		strcpy(partitions[partition_count].name, fn);
		partitions[partition_count].written_flag = 0;
		partition_count++;
	}
	closedir(dir);
#endif
	if (selected_ab < 0) select_ab(io);
	int selected_ab_bak = selected_ab;
	bootloader_control *abc = nullptr;
	size_t misclen = 0;
	if (force_ab && (force_ab & VAB)) selected_ab = force_ab;
	else {
		if (miscname[0]) {
			uint8_t *mem = loadfile(miscname, &misclen, 0);
			if (misclen >= 0x820) {
				abc = (bootloader_control *)(mem + 0x800);
				if (abc->nb_slot != 2) selected_ab = 0;
				if (ab_compare_slots(&abc->slot_info[1], &abc->slot_info[0]) < 0) selected_ab = 2;
				else selected_ab = 1;
			}
			delete[](mem);
		}
		if (!selected_ab) {
			if (VAB & 1) selected_ab = 1;
			else if (VAB & 2) selected_ab = 2;
			else if (selected_ab_bak > 0) selected_ab = selected_ab_bak;
		}
	}
	if (selected_ab) DEG_LOG(I,"Flashing to slot %c.\n", 96 + selected_ab);
	for (int i = 0; i < partition_count; i++) {
		if (isCancel) {  delete[](partitions); return; }
		fn = partitions[i].name;
		namelen = strlen(fn);
		if (selected_ab == 1 && namelen > 2 && 0 == strcmp(fn + namelen - 2, "_b")) { partitions[i].written_flag = 1; continue; }
		else if (selected_ab == 2 && namelen > 2 && 0 == strcmp(fn + namelen - 2, "_a")) { partitions[i].written_flag = 1; continue; }
		if (!strcmp(fn, "splloader") ||
			!strcmp(fn, "uboot_a") ||
			!strcmp(fn, "uboot_b") ||
			!strcmp(fn, "vbmeta_a") ||
			!strcmp(fn, "vbmeta_b")) {
			load_partition(io, fn, partitions[i].file_path, step, CMethod);
			partitions[i].written_flag = 1;
			continue;
		}
		if (strcmp(fn, "uboot") == 0 || strcmp(fn, "vbmeta") == 0) {
			get_partition_info(io, fn, 0);
			if (!gPartInfo.size) continue;

			load_partition_unify(io, gPartInfo.name, partitions[i].file_path, step, CMethod);
			partitions[i].written_flag = 1;
			continue;
		}
		if (strncmp(fn, "vbmeta_", 7) == 0) {
			get_partition_info(io, fn, 0);
			if (!gPartInfo.size) continue;

			load_partition_unify(io, gPartInfo.name, partitions[i].file_path, step, CMethod);
			partitions[i].written_flag = 1;
			continue;
		}
	}
	int metadata_in_dump = 0, super_in_dump = 0, metadata_id = -1, super_id = -1;
	for (int i = 0; i < partition_count; i++) {
		if (isCancel) { delete[](partitions); return; }
		if (!partitions[i].written_flag) {
			fn = partitions[i].name;
			get_partition_info(io, fn, 0);
			if (!gPartInfo.size) continue;
			if (!strcmp(gPartInfo.name, "metadata")) { metadata_in_dump = 1; metadata_id = i; continue; }
			if (!strcmp(gPartInfo.name, "super")) { super_in_dump = 1; super_id = i; continue; }
			load_partition_unify(io, gPartInfo.name, partitions[i].file_path, step, CMethod);
		}
	}
	if (super_in_dump) {
		load_partition(io, "super", partitions[super_id].file_path, step, CMethod);
		if (metadata_in_dump) load_partition(io, "metadata", partitions[metadata_id].file_path, step, CMethod);
		else erase_partition(io, "metadata", CMethod);
	}
	delete[](partitions);
	if (selected_ab == 1) set_active(io, "a", CMethod);
	else if (selected_ab == 2) set_active(io, "b", CMethod);
	selected_ab = selected_ab_bak;
}

void get_Da_Info(spdio_t *io) {
	if (io->raw_len > 6) {
		if (0x7477656e == *(uint32_t *)(io->raw_buf + 4)) {
			int len = 8;
			uint16_t tmp[2];
			while (len + 2 < io->raw_len) {
				tmp[0] = 0;
				tmp[1] = 0;
				memcpy(tmp, io->raw_buf + len, sizeof(tmp));

				len += sizeof(tmp);
				if (tmp[0] == 0) Da_Info.bDisableHDLC = *(uint32_t *)(io->raw_buf + len);
				else if (tmp[0] == 2) Da_Info.bSupportRawData = *(uint8_t *)(io->raw_buf + len);
				else if (tmp[0] == 3) Da_Info.dwFlushSize = *(uint32_t *)(io->raw_buf + len);
				else if (tmp[0] == 6) Da_Info.dwStorageType = *(uint32_t *)(io->raw_buf + len);
				len += tmp[1];
			}
		}
		else memcpy(&Da_Info, io->raw_buf + 4, io->raw_len - 6);
	}
	DEG_LOG(I,"FDL2: Incompatible partition");
}

int ab_compare_slots(const slot_metadata *a, const slot_metadata *b) {
	if (a->priority != b->priority)
		return b->priority - a->priority;
	if (a->successful_boot != b->successful_boot)
		return b->successful_boot - a->successful_boot;
	if (a->tries_remaining != b->tries_remaining)
		return b->tries_remaining - a->tries_remaining;
	return 0;
}

void select_ab(spdio_t *io) {
	bootloader_control *abc = nullptr;
	int ret;

	select_partition(io, "misc", 0x820, 0, BSL_CMD_READ_START);
	if (send_and_check(io)) {
		encode_msg_nocpy(io, BSL_CMD_READ_END, 0);
		send_and_check(io);
		selected_ab = 0;
		return;
	}

	//uint32_t data[2] = { 0x20,0x800 };
	uint32_t *data = (uint32_t *)io->temp_buf;
	WRITE32_LE(data, 0x20);
	WRITE32_LE(data + 1, 0x800);
	encode_msg_nocpy(io, BSL_CMD_READ_MIDST, 8);
	send_msg(io);
	ret = recv_msg(io);
	if (!ret) ERR_EXIT("timeout reached\n");
	if (recv_type(io) == BSL_REP_READ_FLASH) abc = (bootloader_control *)(io->raw_buf + 4);
	encode_msg_nocpy(io, BSL_CMD_READ_END, 0);
	send_and_check(io);

	if (abc == nullptr) { selected_ab = 0; return; }
	if (abc->nb_slot != 2) { selected_ab = 0; return; }
	if (ab_compare_slots(&abc->slot_info[1], &abc->slot_info[0]) < 0) selected_ab = 2;
	else selected_ab = 1;

	if (selected_ab > 0 && check_partition(io, "uboot_a", 0) == 0) selected_ab = 0;
}

void dm_disable(spdio_t *io, unsigned step, int CMethod) {
	char ch = '\1';
	w_mem_to_part_offset(io, "vbmeta", 0x7B, (uint8_t *)&ch, 1, step, CMethod);
}

void dm_enable(spdio_t *io, unsigned step, int CMethod) {
	const char *list[] = { "vbmeta", "vbmeta_system", "vbmeta_vendor", "vbmeta_system_ext", "vbmeta_product", "vbmeta_odm", nullptr };
	char ch = '\0';
	for (int i = 0; list[i] != nullptr; i++) w_mem_to_part_offset(io, list[i], 0x7B, (uint8_t *)&ch, 1, step, CMethod);
}

uint32_t const crc32_tab[] = {
	0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
	0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
	0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
	0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
	0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
	0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
	0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
	0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
	0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
	0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
	0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
	0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
	0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
	0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
	0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
	0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
	0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
	0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
	0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
	0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
	0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
	0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
	0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236, 0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
	0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
	0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
	0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
	0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
	0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
	0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
	0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
	0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
	0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

uint32_t crc32(uint32_t crc_in, const uint8_t *buf, int size) {
	const uint8_t *p = buf;
	uint32_t crc;

	crc = crc_in ^ ~0U;
	while (size--)
		crc = crc32_tab[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
	return crc ^ ~0U;
}

void w_mem_to_part_offset(spdio_t *io, const char *name, size_t offset, uint8_t *mem, size_t length, unsigned step, int CMethod) {
	get_partition_info(io, name, 1);
	if (!gPartInfo.size) { DEG_LOG(E,"part not exist"); return; }
	else if (gPartInfo.size > 0xffffffff) { DEG_LOG(E,"part too large"); return; }

	char dfile[40];
	snprintf(dfile, sizeof(dfile), "%s.bin", name);

	char fix_fn[1024];
	if (savepath && savepath[0]) sprintf(fix_fn, "%s/%s", savepath, dfile);
	else strcpy(fix_fn, dfile);

	FILE *fi;
	if (offset == 0) fi = fopen(fix_fn, "wb");
	else {
		if (gPartInfo.size != (long long)dump_partition(io, gPartInfo.name, 0, gPartInfo.size, fix_fn, step)) {
			remove(fix_fn);
			return;
		}
		fi = fopen(fix_fn, "rb+");
	}
	if (!fi) ERR_EXIT("fopen %s failed\n", fix_fn);
	if (fseek(fi, offset, SEEK_SET) != 0) ERR_EXIT("fseek failed\n");
	if (fwrite(mem, 1, length, fi) != length) ERR_EXIT("fwrite failed\n");
	fclose(fi);
	load_partition_unify(io, gPartInfo.name, fix_fn, step, CMethod);
}

// 1 main written and _bak not written, 2 both written
int load_partition_unify(spdio_t *io, const char *name, const char *fn, unsigned step, int CMethod) {
	char name0[36], name1[40];
	unsigned size0, size1;
	if (strstr(name, "fixnv1")) { load_nv_partition(io, name, fn, 4096); return 1; }
	if (selected_ab > 0 ||
		Da_Info.dwStorageType == 0x101 ||
		io->part_count == 0 ||
		strncmp(name, "splloader", 9) == 0) {
		load_partition(io, name, fn, step, CMethod);
		return 1;
	}

	strcpy(name0, name);
	if (strlen(name0) >= sizeof(name0) - 4) { load_partition(io, name0, fn, step, CMethod); return 1; }
	snprintf(name1, sizeof(name1), "%s_bak", name0);
	get_partition_info(io, name1, 1);
	if (!gPartInfo.size) { load_partition(io, name0, fn, step, CMethod); return 1; }
	size1 = gPartInfo.size;
	size0 = check_partition(io, name0, 1);

	for (int i = 0; i < io->part_count; i++)
		if (!strcmp(name0, (*(io->ptable + i)).name)) {
			load_partition_force(io, i, fn, step, CMethod);
			break;
		}
	if (size0 == size1) {
		if (!strcmp(name0, "vbmeta")) {
			char ch = '\0';
			FILE *fi = fopen(fn, "rb+");
			if (!fi) { DEG_LOG(E,"fopen %s failed\n", fn); return 1; }
			if (fseek(fi, 0x7B, SEEK_SET) != 0) { DEG_LOG(E,"fseek failed"); fclose(fi); return 1; }
			if (fwrite(&ch, 1, 1, fi) != 1) { DEG_LOG(E,"fwrite failed\n"); fclose(fi); return 1; }
			fclose(fi);
		}
		load_partition(io, name1, fn, step, CMethod);
		return 2;
	}
	return 1;
}

void set_active(spdio_t *io, const char *arg, int CMethod) {
	uint32_t tmp[8] = { 0x5F, 0x42414342, 0x201, 0, 0, 0, 0, 0 };
	bootloader_control *abc = (bootloader_control *)tmp;
	int slot = *arg - 'a';

	abc->slot_info[slot].priority = 15;
	abc->slot_info[slot].tries_remaining = 6;
	abc->slot_info[slot].successful_boot = 0;
	abc->slot_info[1 - slot].priority = 14;
	abc->slot_info[1 - slot].tries_remaining = 1;
	abc->slot_suffix[1] = *arg;
	abc->crc32_le = crc32(0, (const uint8_t*)abc, 0x1C);

	w_mem_to_part_offset(io, "misc", 0x800, (uint8_t *)abc, sizeof(bootloader_control), 0x1000, CMethod);
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
		for (int i = 0; ; i++) {
			if (m_bOpened == -1) {
				call_DisconnectChannel(io->handle);
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
	if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED) { if (!curPort) curPort = device; }
	else { if (curPort == device) m_bOpened = -1; }
	return 0;
}

void *UsbThrdFunc(void *param) {
	int ret;
	while (bListenLibusb) {
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

	bListenLibusb = 1;
}

void stopUsbEventHandle(void) {
	bListenLibusb = 0;
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
