#pragma once
#include <stdint.h>
#include <stddef.h>

struct spdio_t; // Forward declaration

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
int recv_msg(spdio_t *io);
int recv_msg_timeout(spdio_t *io, int timeout);
unsigned recv_type(spdio_t *io);
int send_and_check(spdio_t *io);
