#include "spd_protocol.h"
#include "device_attach_helpers.h"
#include "../common.h"
#include "result.h"

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
	WRITE16_BE(p, (uint16_t)len); p += 2;
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
	WRITE16_BE(p, (uint16_t)len); p += 2;
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

	if (is_device_unattached_and_log(io)) {
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

	IUsbTransport *t = spdio_get_transport(io);
	ret = usb_transport_send(t, io->send_buf, io->enc_len, io->timeout);

	if (ret < 0) {
		spdio_free(io);
		ERR_EXIT("device unattached, exiting...\n");
	}

	if (ret != io->enc_len)
		ERR_EXIT("usb_send failed (%d / %d)\n", ret, io->enc_len);

	return ret;
}

const char* CommonPartitions[] = {
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

// Boot/握手阶段：根据初始响应更新 CRC 模式和 FDL1 状态
// bytes_read 为当前 recv_buf 内有效字节数
// 返回 1 表示响应类型在 BSL_REP_VER/BSL_REP_VERIFY_ERROR/BSL_REP_UNSUPPORTED_COMMAND 范围内且已完成校验与状态更新
// 返回 0 表示响应类型不在上述集合内，由调用方继续处理
int spd_boot_update_crc_and_stage(spdio_t *io, int bytes_read) {
	if (io->recv_buf[2] != BSL_REP_VER &&
		io->recv_buf[2] != BSL_REP_VERIFY_ERROR &&
		io->recv_buf[2] != BSL_REP_UNSUPPORTED_COMMAND) {
		return 0;
	}

	int chk1, chk2;
	int a = READ16_BE(io->recv_buf + bytes_read - 3);
	chk1 = spd_crc16(0, io->recv_buf + 1, bytes_read - 4);
	if (a == chk1) {
		io->flags |= FLAGS_CRC16;
	} else {
		chk2 = spd_checksum(0, io->recv_buf + 1, bytes_read - 4, CHK_ORIG);
		if (a == chk2) {
			fdl1_loaded = 1;
		} else {
			ERR_EXIT("bad checksum (0x%04x, expected 0x%04x or 0x%04x)\n", a, chk1, chk2);
		}
	}
	return 1;
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
	(void)io;
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
				IUsbTransport *t = spdio_get_transport(io);
#if !USE_LIBUSB
				if (io->raw_len) { usb_transport_clear(t); io->raw_len = 0; }
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

static sfd::Result<void> send_and_check_result(spdio_t *io) {
	int ret;
	send_msg(io);
	ret = recv_msg(io);
	if (!ret) {
		ERR_EXIT("timeout reached\n");
		// 按当前行为依然视为 fatal，Result 仅用于统一错误模型
		return sfd::Result<void>::error(sfd::ErrorCode::Timeout, "timeout reached");
	}

	ret = recv_type(io);
	if (ret != BSL_REP_ACK) {
		const char* name = get_bsl_enum_name(ret);
		DEG_LOG(E,"excepted response (%s : 0x%04x)",name, ret);
		return sfd::Result<void>::error(sfd::ErrorCode::ProtocolError, "expected BSL_REP_ACK");
	}

	return sfd::Result<void>::ok();
}

int send_and_check(spdio_t *io) {
	auto r = send_and_check_result(io);
	if (!r) return -1;
	return 0;
}
