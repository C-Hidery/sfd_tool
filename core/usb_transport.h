#pragma once
#include <stdint.h>
#include <stddef.h>

#define RECV_BUF_LEN (0x8000)
extern int& m_bOpened;

#if _WIN32
#include <Windows.h>
#endif

#if USE_LIBUSB
#ifdef MACOS
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wzero-length-array"
#pragma clang diagnostic ignored "-Wc99-extensions"
#include "../third_party/Lib/libusb-1.0/libusb.h"
#pragma clang diagnostic pop
#else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wzero-length-array"
#pragma clang diagnostic ignored "-Wc99-extensions"
#include <libusb-1.0/libusb.h>
#pragma clang diagnostic pop
#endif
#endif

struct spdio_t; // Forward declaration

// 传输层抽象接口：用于在协议层屏蔽具体平台实现。
struct IUsbTransport {
	virtual ~IUsbTransport() = default;

	// 发送数据缓冲区，返回实际发送字节数或负错误码。
	virtual int send(const uint8_t *buf, int len, int timeout_ms) = 0;

	// 接收数据到缓冲区，返回实际接收字节数或负错误码。
	virtual int recv(uint8_t *buf, int max_len, int timeout_ms) = 0;

	// 清空底层接收缓冲区（如有需要）。
	virtual int clear() = 0;
};

// Get transport view from an spdio_t session
IUsbTransport *spdio_get_transport(spdio_t *io);

// Low-level send/recv/clear operations that hide platform details
int usb_transport_send(IUsbTransport *t, const uint8_t *buf, int len, int timeout_ms);
int usb_transport_recv(IUsbTransport *t, uint8_t *buf, int max_len, int timeout_ms);
int usb_transport_clear(IUsbTransport *t);

#if USE_LIBUSB
libusb_device **FindPort(int pid);
void startUsbEventHandle(void);
void stopUsbEventHandle(void);
void find_endpoints(libusb_device_handle *dev_handle, int result[4]);
void call_Initialize_libusb(spdio_t *io);
#else
DWORD *FindPort(const char *USB_DL);
BOOL CreateRecvThread(spdio_t *io);
void DestroyRecvThread(spdio_t *io);
#endif

spdio_t *spdio_init(int flags);
void spdio_free(spdio_t *io);
int recv_read_data(spdio_t *io);
void ChangeMode(spdio_t *io, int ms, int bootmode, int at);
