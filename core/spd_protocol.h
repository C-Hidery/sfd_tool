#pragma once
#include <stdint.h>
#include <stddef.h>

struct spdio_t; // Forward declaration
struct IUsbTransport; // 来自 core/usb_transport.h，用于解耦协议与具体传输实现

enum Stages {
	Nothing = -1,
	BROM = 0,
	FDL1 = 1,
	FDL2 = 2,
	SPRD3 = 3,
	SPRD4 = 4
};

// Checksum finalization modes for spd_checksum
#define CHK_FIXZERO 1
#define CHK_ORIG 2

int spd_transcode(uint8_t *dst, uint8_t *src, int len);
int spd_transcode_max(uint8_t *src, int len, int n);
unsigned spd_crc16(unsigned crc, const void *src, unsigned len);
unsigned spd_checksum(unsigned crc, const void *src, int len, int final);
void encode_msg(spdio_t *io, int type, const void *data, size_t len);
void encode_msg_nocpy(spdio_t *io, int type, size_t len);
int send_msg(spdio_t *io);
int recv_transcode(spdio_t *io, const uint8_t *buf, int buf_len, int *plen);
int recv_check_crc(spdio_t *io);
int recv_msg(spdio_t *io);
int recv_msg_timeout(spdio_t *io, int timeout);
unsigned recv_type(spdio_t *io);
int send_and_check(spdio_t *io);

// Boot/握手阶段：根据初始响应更新 CRC 模式和 FDL1 状态
// 返回值：
//  - 1 表示响应类型为 BSL_REP_VER/BSL_REP_VERIFY_ERROR/BSL_REP_UNSUPPORTED_COMMAND 且已完成校验与状态更新
//  - 0 表示响应类型不在上述集合内，调用方可继续其他分支逻辑
int spd_boot_update_crc_and_stage(spdio_t *io, int bytes_read);
