#pragma once
#include <stdint.h>
#include <stddef.h>

#define RECV_BUF_LEN (0x8000)
extern int m_bOpened;

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
