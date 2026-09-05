/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#include "common.h"
#include "ui/layout/bottom_bar.h"
#include "pages/page_pac_flash.h"
#include <functional>

#include "i18n.h"
#if defined(__linux__) || defined(__APPLE__)
#include <sys/stat.h>
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <strings.h>
#endif
#include <algorithm>
#include <vector>
#include <string>
#include <cctype>
#include "core/logging.h"
#include "core/app_state.h"
#include "core/XmlParser.hpp"
#include <cstring>
#include <cctype>

int my_strnicmp(const char* s1, const char* s2, size_t len)
{
#ifdef _WIN32
	return _strnicmp(s1, s2, len);
#else
	return strncasecmp(s1, s2, len);
#endif
}

int my_stricmp(const char* s1, const char* s2) {
#ifdef _WIN32
    return _stricmp(s1, s2);
#else
    return strcasecmp(s1, s2);
#endif
}

// 跨平台的不区分大小写子串查找
char* my_stristr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;

    size_t haystack_len = strlen(haystack);
    size_t needle_len = strlen(needle);
    
    if (haystack_len < needle_len) return NULL;

    const char* end = haystack + haystack_len - needle_len;

    for (const char* p = haystack; p <= end; ++p) {
        if (my_strnicmp(p, needle, needle_len) == 0) {
            return (char*)p;
        }
    }
    return NULL;
}
int isCancel = 0;
bool isHelperInit = false;
GtkWidgetHelper helper;
bool Err_Showed = false;
AppState g_app_state; // 全局应用状态实例
static std::string g_progress_desc;
int fdl1_loaded = 0;
int fdl2_executed = 0;
int blk_size = 0;
// common.cpp 添加以下定义（确保只定义一次）
const char* o_exception = nullptr;
int init_stage = -1;
int no_fdl_mode = 0;
int isKickMode = 0;
int conn_wait = 30 * REOPEN_FREQ;
int stage = -1;
int waitFDL1 = -1;
int keep_charge = 1;
int end_data = 0;
int highspeed = 0;
unsigned exec_addr = 0;
uint32_t baudrate = 0;
int nand_id = DEFAULT_NAND_ID;
int nand_info[3] = {0};
int ret;
int bootmode = -1;
int at = 0;
int autoFDL1Suc = 0;
char mode_str[256] = {0};
std::string fdl1_path_json;
std::string fdl2_path_json;
uint32_t fdl1_addr_json = 0;
uint32_t fdl2_addr_json = 0;
int async = 1;

#if USE_LIBUSB
libusb_device** ports = nullptr;
#else
DWORD* ports = nullptr;
#endif
uint64_t g_spl_size;
int g_default_blk_size = 0;
uint64_t fblk_size = 0;
int& m_bOpened = g_app_state.device.m_bOpened;
char* temp;
#ifndef _WIN32
void check_root_permission(GtkWidgetHelper helper) {
	if (geteuid() != 0) {
		// not root
		showWarningDialogSyncInThread(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Warning"))), _("You are running this tool without root permission!\nIt may cause device connecting issue without device rule(80-spd.rules)."));
	}
}
#endif
#ifdef _MSC_VER
void usleep(unsigned int us) {
	Sleep(us / 1000);
}
#endif
#if defined(__APPLE__)
bool g_is_macos_bundle = false;
#endif

DA_INFO_T Da_Info;
partition_t gPartInfo;
bool isUseCptable = false;



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
    size_t n = 0, j = 0;
    uint8_t *buf = nullptr;
    EnhancedFile fi = oxfopen_enhanced(fn, "rb");
    if (fi) {
        if (fi.seek(0, SEEK_END) == 0) {
            long n_long = fi.tell();
            if (n_long > 0) { 
                n = static_cast<size_t>(n_long);
                if (n <= SIZE_MAX - extra) {
                    fi.rewind();
                    buf = NEWN uint8_t[n + extra];
                    if (buf) {
                        j = fi.read(buf, 1, n);
                    }
                }
            }
        }
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
int GetStage() {	
	if (fdl2_executed > 0) return FDL2;
	else if (fdl1_loaded > 0) return FDL1;
	else return BROM;
}
unsigned read_flash(spdio_t *io,
		uint32_t addr, uint32_t start, uint32_t len,
		uint8_t *mem, FILE *fo, unsigned step) {
	uint32_t n, offset, nread;
	int ret;

	for (offset = start; offset < start + len; ) {
		uint32_t data[3];
		n = start + len - offset;
		if (n > step) n = step;

		WRITE32_BE(data, addr);
		WRITE32_BE(data + 1, n);
		WRITE32_BE(data + 2, offset);

		encode_msg(io, BSL_CMD_READ_FLASH, data, 4 * 3);
		send_msg(io);
		ret = recv_msg(io);
		if ((ret = recv_type(io)) != BSL_REP_READ_FLASH) {
			const char* name = get_bsl_enum_name(ret);
			DEG_LOG(E,"unexpected response (%s : 0x%04x)",name, ret);
			break;
		}
		nread = READ16_BE(io->raw_buf + 2);
		if (n < nread)
			ERR_EXIT("unexpected length\n");
		if (mem) {
			memcpy(mem, io->raw_buf + 4, nread);
			mem += nread;
		}
		if (fo && fwrite(io->raw_buf + 4, 1, nread, fo) != nread) {
			ERR_EXIT("fwrite(dump) failed\n");
		}
		offset += nread;
		if (n != nread) break;
	}
	return offset - start;
}

unsigned dump_flash(spdio_t *io,
		uint32_t addr, uint32_t start, uint32_t len,
		const char *fn, unsigned step, int mode) {
	uint32_t nread = 0;
	EnhancedFile fo = my_oxfopen_enhanced(fn, "wb");
	if (!fo) ERR_EXIT("fopen(dump) failed, please check if program has permission to write file in current dir.\n");

	if (mode == 1) {
		uint8_t buf[0x34];
		len = sizeof(buf);
		nread = read_flash(io, addr, start, len, buf, fo.get(), step);
		if (nread != len)
			ERR_EXIT("can't read DHTB header\n");
		// "DHTB" -> "BTHD", Boot Header
		if (READ32_LE(buf) != 0x42544844 || READ32_LE(buf + 4) != 1)
			ERR_EXIT("unexpected DHTB header\n");
		len = READ32_LE(buf + 0x30);
		if (len >> 31) ERR_EXIT("unexpected DHTB size (0x%x)\n", len);
		len += 0x200;
	}
	nread += read_flash(io, addr, start + nread, len - nread, NULL, fo.get(), step);
	if (mode == 1 && len == nread) do {	// read DHTB signature
		uint8_t buf[0x60];
		uint32_t nread2, nread1;
		nread2 = read_flash(io, addr, start + nread, 0x60, buf, NULL, step);
		// can be unsigned
		if (nread2 != 0x60) break;
		if (!READ32_LE(buf + 0x10)) break; // all zeros
		if (!~READ32_LE(buf + 0x10)) break; // all ones

		if (fo.write(buf, 1, nread2) != nread2)
			ERR_EXIT("fwrite(dump) failed\n");

		nread1 = nread; len = nread += nread2;
		if (READ32_LE(buf + 0x10) != (int)nread1 - 0x200 || // data size
				READ32_LE(buf + 0x18) != 0x200 || // data offset
				READ32_LE(buf + 0x20) >> 12 || // sign data size (0x254, 0x234)
				READ32_LE(buf + 0x28) != (int)nread1 + 0x60) { // sign data offset
			DEG_LOG(E,"unexpected DHTB signature\n");
			break;
		}
		len += nread2 = READ32_LE(buf + 0x20);
		nread += read_flash(io, addr, start + nread, nread2, NULL, fo.get(), step);
	} while (0);
	DEG_LOG(I,"Read flash successfully: 0x%08x+0x%x, target: 0x%x, read: 0x%x\n", addr, start, len, nread);
	return nread;
}
uint8_t* dump_flash_to_mem(spdio_t *io,
                           uint32_t addr, uint32_t start, uint32_t len,
                           unsigned step, int mode,
                           uint64_t *out_size) {
    if (out_size) *out_size = 0;

    uint32_t total_read = 0;
    uint8_t* mem = nullptr;

    if (mode == 1) {
        uint8_t header[0x34];
        uint32_t n = read_flash(io, addr, start, sizeof(header), header, nullptr, step);
        if (n != sizeof(header))
            ERR_EXIT("can't read DHTB header\n");

        if (READ32_LE(header) != 0x42544844 || READ32_LE(header + 4) != 1)
            ERR_EXIT("unexpected DHTB header\n");

        uint32_t data_len = READ32_LE(header + 0x30);
        if (data_len >> 31)
            ERR_EXIT("unexpected DHTB size (0x%x)\n", data_len);

        uint32_t base_size = data_len + 0x200;
        mem = new (std::nothrow) uint8_t[base_size];
        if (!mem)
            ERR_EXIT("memory allocation failed\n");

        memcpy(mem, header, sizeof(header));
        total_read = sizeof(header);

        uint32_t remaining = base_size - total_read;
        if (remaining > 0) {
            uint32_t r = read_flash(io, addr, start + total_read, remaining,
                                    mem + total_read, nullptr, step);
            total_read += r;
        }

        // ---- 读取签名 ----
        uint8_t sig_hdr[0x60];
        uint32_t nread2 = read_flash(io, addr, start + total_read, sizeof(sig_hdr),
                                     sig_hdr, nullptr, step);
        if (nread2 == sizeof(sig_hdr)) {
            uint32_t sig_val = READ32_LE(sig_hdr + 0x10);
            // 检查既不是全0也不是全1
            if (!(sig_val == 0 || sig_val == 0xFFFFFFFFU)) {
                uint32_t sig_data_size = READ32_LE(sig_hdr + 0x20);
                uint32_t new_size = base_size + sizeof(sig_hdr) + sig_data_size;
                uint8_t* new_mem = NEWN uint8_t[new_size];
                if (!new_mem) {
                    delete[] mem;
                    ERR_EXIT("memory reallocation failed\n");
                }
                memcpy(new_mem, mem, total_read);
                delete[] mem;
                mem = new_mem;

                memcpy(mem + total_read, sig_hdr, sizeof(sig_hdr));
                total_read += sizeof(sig_hdr);

                uint32_t sig_read = read_flash(io, addr, start + total_read,
                                               sig_data_size, mem + total_read,
                                               nullptr, step);
                total_read += sig_read;
            }
        }

        if (out_size) *out_size = total_read;
        return mem;
    }

    // ---- mode != 1 ----
    mem = new (std::nothrow) uint8_t[len];
    if (!mem)
        ERR_EXIT("memory allocation failed\n");

    uint32_t nread = read_flash(io, addr, start, len, mem, nullptr, step);
    if (out_size) *out_size = nread;
    return mem;
}
unsigned dump_mem(spdio_t *io,
	uint32_t start, uint32_t len, const char *fn, unsigned step) {
	uint32_t n, offset, nread;
	int ret;
	EnhancedFile fo = my_oxfopen_enhanced(fn, "wb");
	if (!fo) ERR_EXIT("fopen(dump) failed, please check if program has permission to write file in current dir.\n");

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
			DEG_LOG(E,"unexpected response (%s : 0x%04x)",name, ret);
			break;
		}
		nread = READ16_BE(io->raw_buf + 2);
		if (n < nread)
			ERR_EXIT("unexpected length\n");
		if (fo.write(io->raw_buf + 4, 1, nread) != nread)
			ERR_EXIT("fwrite(dump) failed\n");
		offset += nread;
		if (n != nread) break;
	}
	DEG_LOG(I,"Read mem successfully: 0x%08x, target: 0x%x, read: 0x%x", start, len, offset - start);
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

struct UiProgressData {
    double   percent;
    uint64_t done;
    double   speed_mb_s;
};

void print_progress_bar(spdio_t* io, uint64_t done, uint64_t total, unsigned long long time0) {
    unsigned long long time = GetTickCount64();

    if (isCancel) {
        return;
    }

    // 计算进度百分比（0.0 ~ 1.0）
    double percent = total ? (done / (double)total) : 0.0;
    if (percent < 0.0) percent = 0.0;
    if (percent > 1.0) percent = 1.0;

    // 终端文本进度条：每次调用都刷新一行，视觉上连续前进
    int completed = (int)(PROGRESS_BAR_WIDTH * percent);
    if (completed < 0) completed = 0;
    if (completed > PROGRESS_BAR_WIDTH) completed = PROGRESS_BAR_WIDTH;
    int remaining = PROGRESS_BAR_WIDTH - completed;

	fprintf(stderr, "Progress: |");
    for (int i = 0; i < completed; i++) {
        fprintf(stderr, "#");
    }
    for (int i = 0; i < remaining; i++) {
        fprintf(stderr, "-");
    }

    double speed_mb_s = (time > time0)
        ? (double)1000 * done / (double)(time - time0) / 1024.0 / 1024.0
        : 0.0;

    fprintf(stderr, "|%6.1f%% Speed:%6.2fMb/s\r", percent * 100.0, speed_mb_s);
    if (io->nor_bar || percent >= 1.0) fprintf(stderr, "\n");

    // GUI 进度条和状态文本：每个数据块触发一次更新，保证视觉连续
    if (isHelperInit) {
        g_main_context_invoke(nullptr, [](gpointer data) -> gboolean {
            auto* progress_data = static_cast<UiProgressData*>(data);
            double   percent_val = progress_data->percent;
            uint64_t done_value  = progress_data->done;
            double   speed_val   = progress_data->speed_mb_s;

            if (isHelperInit) {
                // 更新进度条 + 百分比
                char percent_text[16];
                snprintf(percent_text, sizeof(percent_text), "%.1f%%", percent_val * 100.0);
                bottom_bar_set_progress(percent_val, percent_text);

                // 更新底部连接状态文本，展示当前进度与速度
                char status_text[160];
                double mb_done = done_value / (1024.0 * 1024.0);

                if (!g_progress_desc.empty()) {
                    snprintf(status_text, sizeof(status_text),
                             "%s | %.1f MB | %.2f MB/s",
                             g_progress_desc.c_str(),
                             mb_done,
                             speed_val);
                } else {
                    snprintf(status_text, sizeof(status_text),
                             "read: %.1f MB | %.2f MB/s",
                             mb_done,
                             speed_val);
                }

                bottom_bar_set_io_status(status_text);
            }

            delete progress_data;
            return G_SOURCE_REMOVE;
        }, new UiProgressData{percent, done, speed_mb_s});
    }
}

void set_progress_desc(const char* desc) {
    if (desc) g_progress_desc = desc;
    else g_progress_desc.clear();
}

uint64_t dump_partition(spdio_t *io,
	const char *name, uint64_t start, uint64_t len,
	const char *fn, unsigned step) {
	uint32_t n, nread, t32; uint64_t offset, n64, saved_size = 0;
	int ret, mode64 = (start + len) >> 32;
	char name_tmp[36];
	DEG_LOG(OP, "dump_partition: name=%s start=0x%llx len=0x%llx step=%u fblk_size=%llu",
	        name,
	        (unsigned long long)start,
	        (unsigned long long)len,
	        step,
	        (unsigned long long)fblk_size);
	double rtime = get_time();
	DEG_LOG(OP,"Start to read partition %s",name);
	DEG_LOG(I,"Type CTRL + C to cancel...");
	start_signal();

	set_progress_desc(name);

	if (!strcmp(name, "super")) {
		dump_partition(io, "metadata", 0, check_partition(io, "metadata", 1), "metadata.bin", step);
		set_progress_desc(name);
	}
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
	else if (strstr(name, "downloadnv") || strstr(name, "factorynv")) {
		start = 0;
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
	EnhancedFile fo = my_oxfopen_enhanced(fn, "wb");
	if (!fo) ERR_EXIT("fopen(dump) failed, please check if program has permission to write file in current dir.\n");

	unsigned long long time_start = GetTickCount64();
	for (offset = start; (n64 = start + len - offset); ) {
		uint32_t *data = (uint32_t *)io->temp_buf;
		n = (uint32_t)(n64 > step ? step : n64);
		if (isCancel) {return offset - start; }
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
			DEG_LOG(E,"unexpected response (%s : 0x%04x)",name, ret);
			break;
		}
		nread = READ16_BE(io->raw_buf + 2);
		if (n < nread)
			ERR_EXIT("unexpected length\n");
		if (fo.write(io->raw_buf + 4, 1, nread) != nread)
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

	double mb = len / (1024.0 * 1024.0);
	double speed = time_spent > 0 ? (mb / time_spent) : 0.0;
	DEG_LOG(I, "dump_partition done: name=%s len=%.1fMB time=%.3fs speed=%.2fMB/s",
	        name,
	        mb,
	        time_spent,
	        speed);
	DEG_LOG(I,"Read partition %s(+0x%llx) successfully, target: 0x%llx, read: 0x%llx",
		name, (long long)start, (long long)len,
		(long long)(offset - start));
	DEG_LOG(I, "Cost time %.6f seconds", time_spent);

	set_progress_desc(nullptr);

	encode_msg_nocpy(io, BSL_CMD_READ_END, 0);
	send_and_check(io);
	return offset - start;
}
uint8_t* dump_partition_to_mem(spdio_t *io,
                               const char *name,
                               uint64_t start,
                               uint64_t len,
                               unsigned step,
                               uint64_t *out_size) {
    uint32_t n, nread, t32;
    uint64_t offset, n64, saved_size = 0;
    int ret, mode64 = (start + len) >> 32;
    char name_tmp[36];

    DEG_LOG(OP, "dump_partition_to_mem: name=%s start=0x%llx len=0x%llx step=%u fblk_size=%llu",
            name,
            (unsigned long long)start,
            (unsigned long long)len,
            step,
            (unsigned long long)fblk_size);
    double rtime = get_time();
    DEG_LOG(OP, "Start to read partition %s", name);
    DEG_LOG(I, "Type CTRL + C to cancel...");
    start_signal();

    set_progress_desc(name);

    if (!strncmp(name, "userdata", 8)) {
        if (!check_confirm("read userdata")) {
            *out_size = 0;
            return nullptr;
        }
    }
    else if (strstr(name, "nv1")) {
        strcpy(name_tmp, name);
        char *dot = strrchr(name_tmp, '1');
        if (dot != nullptr) *dot = '2';
        name = name_tmp;
        start = 512;
        if (len > 512)
            len -= 512;
    }
    else if (strstr(name, "downloadnv") || strstr(name, "factorynv")) {
        start = 0;
        if (len > 512)
            len -= 512;
    }

    if (isCancel) {
        *out_size = 0;
        return nullptr;
    }

    // ---- 分配内存 ----
    uint8_t* mem = NEWN uint8_t[len];
    if (!mem) {
        ERR_EXIT("memory allocation failed\n");
        return nullptr;
    }

    // ---- 发送读取开始命令 ----
    select_partition(io, name, start + len, mode64, BSL_CMD_READ_START);
    if (send_and_check(io)) {
        encode_msg_nocpy(io, BSL_CMD_READ_END, 0);
        send_and_check(io);
        delete[] mem;
        *out_size = 0;
        return nullptr;
    }
    if (isCancel) {
        delete[] mem;
        *out_size = 0;
        return nullptr;
    }

    // ---- 读取循环 ----
    unsigned long long time_start = GetTickCount64();
    for (offset = start; (n64 = start + len - offset); ) {
        uint32_t *data = (uint32_t *)io->temp_buf;
        n = (uint32_t)(n64 > step ? step : n64);

        if (isCancel) {
            // 取消时返回已读数据
            *out_size = offset - start;
            set_progress_desc(nullptr);
            encode_msg_nocpy(io, BSL_CMD_READ_END, 0);
            send_and_check(io);
            return mem;   // 调用者需释放 mem，但读入的数据长度由 out_size 指示
        }

        WRITE32_LE(data, n);
        WRITE32_LE(data + 1, offset);
        t32 = offset >> 32;
        WRITE32_LE(data + 2, t32);

        encode_msg_nocpy(io, BSL_CMD_READ_MIDST, mode64 ? 12 : 8);
        send_msg(io);

        ret = recv_msg(io);
        if (!ret) ERR_EXIT("timeout reached\n");

        if ((ret = recv_type(io)) != BSL_REP_READ_FLASH) {
            const char* name_enum = get_bsl_enum_name(ret);
            DEG_LOG(E, "unexpected response (%s : 0x%04x)", name_enum, ret);
            break;
        }

        nread = READ16_BE(io->raw_buf + 2);
        if (n < nread)
            ERR_EXIT("unexpected length\n");

        // 写入内存（而非文件）
        memcpy(mem + (offset - start), io->raw_buf + 4, nread);

        print_progress_bar(io, offset + nread - start, len, time_start);
        offset += nread;
        if (n != nread) break;

        if (fblk_size) {
            saved_size += nread;
            if (saved_size >= fblk_size) {
                usleep(1000000);
                saved_size = 0;
            }
        }
    }

    // ---- 结束 ----
    double etime = get_time();
    double time_spent = etime - rtime;
    double mb = len / (1024.0 * 1024.0);
    double speed = time_spent > 0 ? (mb / time_spent) : 0.0;
    DEG_LOG(I, "dump_partition_to_mem done: name=%s len=%.1fMB time=%.3fs speed=%.2fMB/s",
            name, mb, time_spent, speed);
    DEG_LOG(I, "Read partition %s(+0x%llx) successfully, target: 0x%llx, read: 0x%llx",
            name, (long long)start, (long long)len, (long long)(offset - start));
    DEG_LOG(I, "Cost time %.6f seconds", time_spent);

    set_progress_desc(nullptr);

    encode_msg_nocpy(io, BSL_CMD_READ_END, 0);
    send_and_check(io);

    *out_size = offset - start;
    return mem;
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
		DEG_LOG(E,"unexpected response (%s : 0x%04x)",name, ret);
		encode_msg_nocpy(io, BSL_CMD_READ_END, 0);
		send_and_check(io);
		return 0;
	}
	n = READ16_BE(io->raw_buf + 2);
	if (n != len) ERR_EXIT("unexpected length\n");

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
    // 1. 读取文件内容
    size_t fsize = 0;
    char *src = (char *)loadfile(fn, &fsize, 1);
    if (!src) ERR_EXIT("loadfile failed\n");
    src[fsize] = 0;

    // 2. 解析 XML
    XmlParser parser;
    auto root = parser.parseString(src);
    if (!root) {
        delete[] src;
        ERR_EXIT("Failed to parse XML\n");
    }

    // 3. 查找 <Partitions> 节点（唯一）
    auto partitionsNodes = root->getDescendants("Partitions");
    if (partitionsNodes.empty()) {
        delete[] src;
        ERR_EXIT("No <Partitions> element\n");
    }
    if (partitionsNodes.size() > 1) {
        delete[] src;
        ERR_EXIT("xml: more than one partition lists\n");
    }
    auto partitions = partitionsNodes[0];

    // 4. 获取所有 <Partition> 子节点
    auto partitionNodes = partitions->getChildren("Partition");

    // 5. 分配 ptable 如果需要
    if (io->ptable == nullptr)
        io->ptable = NEWN partition_t[128];

    // 6. 遍历分区，填充 buf 和 ptable
    uint8_t *buf_ptr = buf;
    size_t remaining = buf_size;
    int found = 0;

    for (auto& partNode : partitionNodes) {
        // 提取 id 属性
        std::string id;
        auto it_id = partNode->attributes.find("id");
        if (it_id != partNode->attributes.end())
            id = it_id->second;
        if (id.empty()) {
            delete[] src;
            ERR_EXIT("Partition missing id attribute\n");
        }

        // 提取 size 属性（支持十进制和十六进制）
        std::string sizeStr;
        auto it_size = partNode->attributes.find("size");
        if (it_size != partNode->attributes.end())
            sizeStr = it_size->second;
        if (sizeStr.empty()) {
            delete[] src;
            ERR_EXIT("Partition missing size attribute\n");
        }
        char *endptr;
        long long size = strtoll(sizeStr.c_str(), &endptr, 0);  // 自动识别 0x 前缀
        if (*endptr != '\0') {
            delete[] src;
            ERR_EXIT("Invalid size value\n");
        }

        // 检查缓冲区剩余空间
        if (remaining < 0x4c) {
            delete[] src;
            ERR_EXIT("xml: too many partitions\n");
        }
        remaining -= 0x4c;

        // 清空名称区域（36个16位字符，即72字节）
        memset(buf_ptr, 0, 36 * 2);

        // 交错写入名称 ASCII（每个字符占用低字节）
        for (size_t i = 0; i < id.size() && i < 36; ++i)
            buf_ptr[i * 2] = static_cast<uint8_t>(id[i]);

        if (id.empty()) {
            delete[] src;
            ERR_EXIT("empty partition name\n");
        }

        // 写入原始 size（小端，偏移 0x48）
        WRITE32_LE(buf_ptr + 0x48, static_cast<uint32_t>(size));

        // 记录到 ptable
        strncpy(io->ptable[found].name, id.c_str(), sizeof(io->ptable[found].name) - 1);
        io->ptable[found].name[sizeof(io->ptable[found].name) - 1] = '\0';
        io->ptable[found].size = size << 20;   // 左移 20 位（与原函数一致）

        DBG_LOG("[%d] %s, %d\n", found + 1, io->ptable[found].name, (int)size);

        buf_ptr += 0x4c;
        ++found;
    }

    io->part_count = found;
    delete[] src;
    return found;
}
int scan_xml_partitions_from_string(spdio_t *io, const std::string& xml_text,
                                     uint8_t *buf, size_t buf_size) {
    // 1. 检查输入
    if (xml_text.empty()) {
        ERR_EXIT("XML text is empty\n");
        return -1;
    }

    // 2. 解析 XML
    XmlParser parser;
    auto root = parser.parseString(xml_text);
    if (!root) {
        ERR_EXIT("Failed to parse XML\n");
        return -1;
    }

    // 3. 查找 <Partitions> 节点（唯一）
    auto partitionsNodes = root->getDescendants("Partitions");
    if (partitionsNodes.empty()) {
        ERR_EXIT("No <Partitions> element\n");
        return -1;
    }
    if (partitionsNodes.size() > 1) {
        ERR_EXIT("xml: more than one partition lists\n");
        return -1;
    }
    auto partitions = partitionsNodes[0];

    // 4. 获取所有 <Partition> 子节点
    auto partitionNodes = partitions->getChildren("Partition");

    // 5. 分配 ptable 如果需要
    if (io->ptable == nullptr)
        io->ptable = NEWN partition_t[128];

    // 6. 遍历分区，填充 buf 和 ptable
    uint8_t *buf_ptr = buf;
    size_t remaining = buf_size;
    int found = 0;

    for (auto& partNode : partitionNodes) {
        // 提取 id 属性
        std::string id;
        auto it_id = partNode->attributes.find("id");
        if (it_id != partNode->attributes.end())
            id = it_id->second;
        if (id.empty()) {
            ERR_EXIT("Partition missing id attribute\n");
            return -1;
        }

        // 提取 size 属性（支持十进制和十六进制）
        std::string sizeStr;
        auto it_size = partNode->attributes.find("size");
        if (it_size != partNode->attributes.end())
            sizeStr = it_size->second;
        if (sizeStr.empty()) {
            ERR_EXIT("Partition missing size attribute\n");
            return -1;
        }
        char *endptr;
        long long size = strtoll(sizeStr.c_str(), &endptr, 0);  // 自动识别 0x 前缀
        if (*endptr != '\0') {
            ERR_EXIT("Invalid size value\n");
            return -1;
        }

        // 检查缓冲区剩余空间
        if (remaining < 0x4c) {
            ERR_EXIT("xml: too many partitions\n");
            return -1;
        }
        remaining -= 0x4c;

        // 清空名称区域（36个16位字符，即72字节）
        memset(buf_ptr, 0, 36 * 2);

        // 交错写入名称 ASCII（每个字符占用低字节）
        for (size_t i = 0; i < id.size() && i < 36; ++i)
            buf_ptr[i * 2] = static_cast<uint8_t>(id[i]);

        // 写入原始 size（小端，偏移 0x48）
        WRITE32_LE(buf_ptr + 0x48, static_cast<uint32_t>(size));

        // 记录到 ptable
        strncpy(io->ptable[found].name, id.c_str(), sizeof(io->ptable[found].name) - 1);
        io->ptable[found].name[sizeof(io->ptable[found].name) - 1] = '\0';
        io->ptable[found].size = size << 20;   // 左移 20 位（与原函数一致）

        DBG_LOG("[%d] %s, %d\n", found + 1, io->ptable[found].name, (int)size);

        buf_ptr += 0x4c;
        ++found;
    }

    io->part_count = found;
    return found;
}

#define SECTOR_SIZE 512
#define MAX_SECTORS 32

int& selected_ab = g_app_state.flash.selected_ab;

std::vector<int> w_force_ids;

/*
   Derive the original _a/_b name of leftover "w_force" partition from its
   id-1...id+1 neighbours. When w_force forces VAB partitions, so:
  		w_force replaced X_a -> right neighbours is X_b.
  		w_force replaced X_b -> left neighbours is X_a.
   returns 1 and fills out on success, 0 if the original name cannot be
   determined (not VAB partitions or ambiguous).
 */
int w_force_derive_name(const char *left, const char *cur, const char *right, char *out, size_t out_size)
{
	size_t left_len = left ? strlen(left) : 0;
	size_t right_len = right ? strlen(right) : 0;
	char cand[2][36] = {
		{0}, {0}
	};
	int n = 0;
	if (!cur || strcmp(cur, "w_force") != 0) return 0;
	if (right && right_len > 2 && strcmp(right + right_len - 2, "_b") == 0)
	{
		snprintf(cand[n++], sizeof(cand[0]), "%.*s_a", (int)(right_len - 2), right);
	}
	if (left && left_len > 2 && strcmp(left + left_len - 2, "_a") == 0)
	{
		snprintf(cand[n++], sizeof(cand[0]), "%.*s_b", (int)(left_len - 2), left);
	}
	if (n == 0) return 0;
	if (n == 2 && strcmp(cand[0], cand[1]) != 0) return 0; // ambiguous
	snprintf(out, out_size, "%s", cand[0]);
	return 1;
}
/*
   called inside the parse loop right after ptable[i] has been filled
   (the id+1 iteration): if the previous partition ptable[i-1] is a leftover
   "w_force", repair it in-place from its id-1...id+1 neighbours.
 */
void w_force_repair_prev(partition_t *ptable, int part_count, int i)
{
	if (!ptable) return;
	char orig[36] = { 0 };
	if (i <= 0 || i > part_count || memchr(ptable[i - 1].name, 0, 36) == nullptr) return;
	if (strcmp(ptable[i - 1].name, "w_force") != 0) return;
	if (w_force_derive_name(i > 1 ? ptable[i - 2].name : nullptr, ptable[i - 1].name, i < part_count ? ptable[i].name : nullptr, orig, sizeof(orig)))
	{
		bool is_allowed_to_repair = false;
		if (isHelperInit)
		{
			is_allowed_to_repair = showConfirmDialogSyncInThread(GTK_WINDOW(helper.getWidget("main_window")), _("Confirm"), _("Do you want to repair the leftover w_force partition name?"));
		}
		else
		{
			std::string n;
			std::cout << "Do you want to repair the leftover w_force partition name (Y/n)?" << std::endl;
			std::cin >> n;
			is_allowed_to_repair = (n == "y" || n == "Y");
		}
		if (is_allowed_to_repair)
		{
			DEG_LOG(I, "Repairing leftover w_force partition #%u name to: %s", i - 1, orig);
			snprintf(ptable[i - 1].name, sizeof(ptable[i - 1].name), "%s", orig);
			w_force_ids.emplace_back(i - 1);
		}
	}
	else
	{
		DEG_LOG(W, "Cannot parse the original name at partition #%u.", i - 1);
	}
}

int gpt_info(partition_t *ptable, uint8_t *mem, int *part_count_ptr) {
    efi_header header;
    uint8_t buffer[SECTOR_SIZE];
    int sector_index = 0;
    int found = 0;

    // 在内存中逐扇区查找 "EFI PART" 签名
    while (sector_index < MAX_SECTORS) {
        memcpy(buffer, mem + sector_index * SECTOR_SIZE, SECTOR_SIZE);
        if (memcmp(buffer, "EFI PART", 8) == 0) {
            memcpy(&header, buffer, sizeof(header));
            found = 1;
            break;
        }
        sector_index++;
    }

    if (found == 0) {
        return -1;
    }
    else {
        if (sector_index == 1) Da_Info.dwStorageType = 0x102;
        else Da_Info.dwStorageType = 0x103;
    }

    int real_SECTOR_SIZE = SECTOR_SIZE * sector_index;

    efi_entry *entries = NEWN efi_entry[header.number_of_partition_entries * sizeof(efi_entry)];
    if (entries == nullptr) {
        return -1;
    }

    // 直接从内存中读取分区条目表
    size_t entry_offset = (size_t)header.partition_entry_lba * real_SECTOR_SIZE;
    size_t entry_size = (size_t)header.number_of_partition_entries * sizeof(efi_entry);
    memcpy(entries, mem + entry_offset, entry_size);

    // 计算有效分区个数（遇到起始和结束LBA均为0的条目为止）
    int n = 0;
    for (int i = 0; i < header.number_of_partition_entries; i++) {
        efi_entry entry = *(entries + i);
        if (entry.starting_lba == 0 && entry.ending_lba == 0) {
            n = i;
            break;
        }
    }

    DBG_LOG("  0 %36s     %lldKB\n", "splloader", (long long)g_spl_size / 1024);

    for (int i = 0; i < n; i++) {
        efi_entry entry = *(entries + i);
        copy_from_wstr((*(ptable + i)).name, 36, (uint16_t *)entry.partition_name);
        uint64_t lba_count = entry.ending_lba - entry.starting_lba + 1;
        (*(ptable + i)).size = lba_count * real_SECTOR_SIZE;

        DBG_LOG("%3d %36s %7lldMB\n", i + 1, (*(ptable + i)).name,
                ((*(ptable + i)).size >> 20));

        if (!selected_ab) {
            size_t namelen = strlen((*(ptable + i)).name);
            if (namelen > 2 && 0 == strcmp((*(ptable + i)).name + namelen - 2, "_a")) {
                selected_ab = 1;
            }
        }
        if (i > 0 && strcmp((*(ptable + i - 1)).name, "w_force") == 0) {
            w_force_repair_prev(ptable, n, i);
        }
    }

    delete[] entries;
    *part_count_ptr = n;
    return 0;
}

partition_t *partition_list(spdio_t *io, int *part_count_ptr) {
	uint64_t size;
	unsigned i, n = 0;
	int ret; uint8_t *p;
	partition_t *ptable = NEWN partition_t[128];
	if (ptable == nullptr) return nullptr;
	w_force_ids.clear();

	DEG_LOG(OP,"Reading partition table...\n");
	if (selected_ab < 0) select_ab(io);
	int verbose = io->verbose;
	io->verbose = 0;
	uint8_t *read_mem = dump_partition_to_mem(io, "user_partition", 0, 32 * 1024, 4096, &size);
	io->verbose = verbose;
	if (32 * 1024 == size)
		g_app_state.flash.gpt_failed = gpt_info(ptable, read_mem, part_count_ptr);
	if (read_mem) delete[] read_mem;
	if (g_app_state.flash.gpt_failed) {
		g_app_state.flash.is_pgpt = false;
		encode_msg_nocpy(io, BSL_CMD_READ_PARTITION, 0);
		send_msg(io);
		ret = recv_msg(io);
		if (!ret) ERR_EXIT("timeout reached\n");
		ret = recv_type(io);
		if (ret != BSL_REP_READ_PARTITION) {
			const char* name = get_bsl_enum_name(ret);
			DEG_LOG(E,"unexpected response (%s : 0x%04x)",name, ret);
			g_app_state.flash.gpt_failed = -1;
			delete[](ptable);
			return nullptr;
		}
		size = READ16_BE(io->raw_buf + 2);
		if (size % 0x4c) {
			DEG_LOG(I,"Not divisible by struct size (0x%04lx)", size);
			g_app_state.flash.gpt_failed = -1;
			delete[](ptable);
			return nullptr;
		}
		n = size / 0x4c;
		int divisor = 10;
		DEG_LOG(OP, "detecting sector size");
		p = io->raw_buf + 4;
		for (i = 0; i < n; i++, p += 0x4c) {
			size = READ32_LE(p + 0x48);
			while (!(size >> divisor)) divisor--;
		}

		if (Da_Info.dwStorageType == 0) { 
			if (divisor == 10) Da_Info.dwStorageType = 0x102; // emmc
			else Da_Info.dwStorageType = 0x103; // ufs
		}

		p = io->raw_buf + 4;
		DBG_LOG("  0 %36s     %lldKB\n", "splloader", (long long)g_spl_size / 1024);

		for (i = 0; i < n; i++, p += 0x4c) {
			ret = copy_from_wstr((*(ptable + i)).name, 36, (uint16_t *)p);
			if (ret) ERR_EXIT("bad partition name\n");
			size = READ32_LE(p + 0x48);
			(*(ptable + i)).size = (long long)size << (20 - divisor);

			DBG_LOG("%3d %36s %7lldMB\n", i + 1, (*(ptable + i)).name, 
					((*(ptable + i)).size >> 20));
			
			if (!selected_ab) {
				size_t namelen = strlen((*(ptable + i)).name);
				if (namelen > 2 && 0 == strcmp((*(ptable + i)).name + namelen - 2, "_a")) {
					selected_ab = 1;
				}
			}
			if (i > 0 && strcmp((*(ptable + i - 1)).name, "w_force") == 0)
			{
				w_force_repair_prev(ptable, n, i);
			}
		}

		*part_count_ptr = n;
		DEG_LOG(W, "Unable to get standard gpt table");
		g_app_state.flash.gpt_failed = 0;
	}
	if (*part_count_ptr) {
		DEG_LOG(I,"Total number of partitions: %d", *part_count_ptr);
		DEG_LOG(I, "Use `part_table` to save partition list");
		if (Da_Info.dwStorageType == 0x102) {
			DEG_LOG(I,"Storage is emmc");
		    if (isHelperInit) gui_idle_call([]() mutable {
				helper.setLabelText(helper.getWidget("storage_mode"),"Emmc");
			});
		}
		else if (Da_Info.dwStorageType == 0x103) {
			DEG_LOG(I,"Storage is ufs");
		    if (isHelperInit) gui_idle_call([]() mutable {
				helper.setLabelText(helper.getWidget("storage_mode"),"Ufs");
			});
		}
		get_partition_info(io, "w_force", 1);
		if (gPartInfo.size)
		{
			DEG_LOG(W,"w_force partition detected, please check the partitions!");
			if (!w_force_ids.empty())
			{
				DEG_LOG(I, "Program automatically repaired w_force partition(s), id: ");
				for (int& ids : w_force_ids)
				{
					DEG_LOG(I, "%u", ids);
				}
			}
		}
		return ptable;
	}
	else {
		g_app_state.flash.gpt_failed = -1;
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
	case 0x19: return "BSL_CMD_CHECK_ROOT/YCC_CMD_SET_BL_A";
	case 0x1A: return "BSL_CMD_READ_CHIP_UID/YCC_CMD_SET_BL_B";
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
	case 0xCC: return "BSL_CMD_SEND_FLAG/YCC_REP_SET_BL_SUCCESS";
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
	printf("0x19: BSL_CMD_CHECK_ROOT / YCC_CMD_SET_BL_A\n");
	printf("0x1A: BSL_CMD_READ_CHIP_UID / YCC_CMD_SET_BL_B\n");
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
	printf("0xCC: BSL_CMD_SEND_FLAG / YCC_REP_SET_BL_SUCCESS\n");
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
	partition_t* ptable = NEWN partition_t[128];
	if (ptable == nullptr) return nullptr;
	DEG_LOG(OP, "Reading partition table through compatibility method.");
	if (selected_ab < 0) select_ab(io);
	int verbose = io->verbose;
	io->verbose = -1;
	DBG_LOG("  0 %36s  %lldKB\n", "splloader",(long long)g_spl_size >> 10);
	for (i = 0; i < CommonPartitionsCount && n < 128; ++i) {
		const char* part = CommonPartitions[i];
		long long result = check_partition(io, part, 0);
		//exist
		
		if (result) {
			size = check_partition(io, part, 1);
			
		
			if (strcmp(part, "splloader") != 0) {
				strncpy(ptable[n].name, part, sizeof(ptable[n].name) - 1);
				ptable[n].name[sizeof(ptable[n].name) - 1] = '\0'; 
				ptable[n].size = size;
				n++;
			}
			if (strcmp(part, "splloader") != 0) { size = size >> 20; DBG_LOG("  %d %36s  %lldMB\n", n, part, size); }
			
		}
	}
	io->verbose = verbose;
	for (unsigned j = 0; j < n; j++)
	{
		if (!strcmp(ptable[j].name, "user_partition"))
		{
			DEG_LOG(I, "Normal partition list found on device, try to parse...");
			io->ptable = partition_list(io, &io->part_count);
			if (io->part_count) 
			{
				DEG_LOG(I, "Parsed successfully, Compatibility-method mode disabled.");
				if (ptable) delete[] ptable;
				return nullptr;
			}
			else
			{
				DEG_LOG(I, "Parse failed, fall back to Compatibility-method mode.");
			}
		}
	}
	get_partition_info(io, "w_force", 1);
	if (gPartInfo.size)
	{
		DEG_LOG(W, "w_force partition detected in Compatibility-method mode, please check the partitions!");
		if (isHelperInit)
		{
			showWarningDialogSyncInThread(GTK_WINDOW(helper.getWidget("main_window")), _("Warning"), _("w_force partition detected in Compatibility-method mode, please check the partitions!"));
		}
	}
	DEG_LOG(I,"Compatibility-method mode will not save partition table xml automatically.");
	DEG_LOG(I, "You can get partition xml by `part_table` command manually.");
	DEG_LOG(I,"Total number of partitions: %d", n);
	DEG_LOG(W, "Compatibility-method may NOT list the partitions on device completely, please use with caution!");
	if(io->ptable) delete[] io->ptable;
	io->ptable = nullptr;
	io->part_count = 0;
	io->part_count_c = n;
	return ptable;
}
void add_partition(spdio_t* io, const char* name, long long size) {
	partition_t* ptable = NEWN partition_t[128];
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
	if (!send_and_check(io)) g_app_state.flash.gpt_failed = 0;
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
	
	get_partition_info(io, name, 1);
	if (!gPartInfo.size && strcmp(name, "w_force") != 0) return;
	uint64_t offset, len, n64;
	unsigned mode64, n, step0 = step; int ret;
	EnhancedFile fi;
	double rtime = get_time();
	if (strstr(name, "runtimenv")) { erase_partition(io, name, CMethod); return; }
	if (!strcmp(name, "calinv")) { return; } //skip calinv
	if (!strcmp(name, "factorynv")) return; // skip factorynv
	DEG_LOG(OP, "Start to write partition %s", name);
	DEG_LOG(I, "Type CTRL + C to cancel...");
	start_signal();
	fi = oxfopen_enhanced(fn, "rb");
	if (!fi) ERR_EXIT("fopen(load) failed, please check if program has permission to read file in current dir.\n");

	uint8_t header[4], is_simg = 0;
	if (fi.read(header, 1, 4) != 4)
		ERR_EXIT("fread(load) failed\n");
	if (0xED26FF3A == *(uint32_t *)header) is_simg = 1;
	fi.seeko(0, SEEK_END);
	len = fi.tello();
	fi.seek(0, SEEK_SET);
	DEG_LOG(I,"File Size : 0x%llx", (long long)len);

	mode64 = len >> 32;
	select_partition(io, name, len, mode64, BSL_CMD_START_DATA);
	if (send_and_check(io)) { return; }
	if (isCancel) { return; }
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
			if (fi.read(rawbuf, 1, n) != n) ERR_EXIT("fread(load) failed\n");
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
				DEG_LOG(E,"unexpected response (%s : 0x%04x)",name, ret);
				break;
			}
			print_progress_bar(io, offset + n, len, time_start);
		}
		delete[](rawbuf);
	}
	else {
#endif
fallback_load:
		for (offset = 0; (n64 = len - offset); offset += n) {
			if (isCancel) { return; }
			n = (unsigned)(n64 > step ? step : n64);
			if (fi.read(io->temp_buf, 1, n) != n)
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
				DEG_LOG(E,"unexpected response (%s : 0x%04x)",name, ret);
				break;
			}
			print_progress_bar(io,offset + n, len, time_start);
		}
#if !USE_LIBUSB
	}
#endif
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
	const char* part_name = CMethod ? (*(io->Cptable + id)).name : (*(io->ptable + id)).name;
	get_partition_info(io, part_name, 1);
	if (!gPartInfo.size) return;
	if (strstr(part_name, "calinv") || strstr(part_name, "factorynv"))
	{
		DEG_LOG(W, "Partition %s is skipped for force write due to potential risks.", part_name);
		return;
	} //skip calinv and factorynv
	if (g_app_state.flash.selected_ab > 0)
	{
		size_t namelen = strlen(part_name);
		bool has_ab = (namelen > 2 && (strcmp(part_name + namelen - 2, "_a") == 0 || strcmp(part_name + namelen - 2, "_b") == 0));
		if (!has_ab)
		{
			load_partition(io, part_name, fn, step, CMethod);
			DEG_LOG(W, "Partition %s is skipped for force write as it's not an A/B partition.", part_name);
			return;
		}
	}
	else
	{
		const char* force_writelist[] = { "system", "super", "userdata" };
		int force_writelist_count = sizeof(force_writelist) / sizeof(force_writelist[0]);
		for (int i = 0; i < force_writelist_count; i++) {
			if (strcmp(part_name, force_writelist[i]) == 0) {
				load_partition(io, part_name, fn, step, CMethod);
				DEG_LOG(W, "Partition %s is skipped for force write as it's not in the force write list.", part_name);
				return;
			}
		}
	}
	if(!CMethod){
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
			
			DEG_LOG(I, "Force write %s successfully", part_name); 
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
			
			DEG_LOG(I, "Force write %s successfully", part_name); 
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
	// factorynv partition flashing removed since it's not safe, and the crc check is not necessary for other nv partitions
	/*
	if (strstr(name, "factorynv")){
		dump_partition(io, name, 0, 16, "nvcrc", 4096);
		uint8_t *crc_mem = loadfile("nvcrc", nullptr, 0);
		mem[0] = crc_mem[0];
		mem[1] = crc_mem[1];
		delete[](crc_mem);
	}
	else {
		crc = crc16(crc, mem + 2, len - 2);
		WRITE16_BE(mem, crc);
	}	
	*/
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
			DEG_LOG(E,"unexpected response (%s : 0x%04x)",name, ret);
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
void load_nv_partition_from_mem(spdio_t *io, const char *name,
                                 uint8_t *mem, unsigned step) {
    if (!mem) {
        DEG_LOG(E, "Invalid memory buffer for NV partition\n");
        return;
    }

    double rtime = get_time();
    size_t offset, rsz;
    unsigned n;
    int ret;
    size_t len = 0;
    uint16_t crc = 0;
    uint32_t cs = 0;

    // 保存原始指针以便最后释放（注意：调用者负责释放，这里不做delete）
    uint8_t *mem0 = mem;

    // 处理可能的 NAND 头（0x4E56 标识）
    if (*(uint32_t *)mem == 0x4E56) mem += 0x200;

    len = 0;
    len += sizeof(uint32_t);

    uint16_t tmp[2];
    while (1) {
        tmp[0] = 0;
        tmp[1] = 0;
        memcpy(tmp, mem + len, sizeof(tmp));
        if (!tmp[1]) {
            DEG_LOG(E, "Broken NV data, skipped!");
            return;
        }
        len += sizeof(tmp);
        len += tmp[1];

        uint32_t doffset = ((len + 3) & 0xFFFFFFFC) - len;
        len += doffset;
        if (*(uint16_t *)(mem + len) == 0xffff) {
            len += 8;
            break;
        }
    }

    // 计算 CRC 和校验和
    crc = crc16(crc, mem + 2, len - 2);
    WRITE16_BE(mem, crc);
    for (offset = 0; offset < len; offset++) cs += mem[offset];

    DEG_LOG(I, "NV data size : 0x%zx", len);

    // 准备协议包
    struct pkt {
        uint16_t name[36];
        uint32_t size, cs;
    } *pkt_ptr;
    pkt_ptr = (struct pkt *) io->temp_buf;
    ret = copy_to_wstr(pkt_ptr->name, 36, name);
    if (ret) ERR_EXIT("name too long\n");
    WRITE32_LE(&pkt_ptr->size, len);
    WRITE32_LE(&pkt_ptr->cs, cs);

    // 发送开始命令
    encode_msg_nocpy(io, BSL_CMD_START_DATA, sizeof(struct pkt));
    if (send_and_check(io)) {
        return;  // 调用者负责释放 mem
    }

    // 分块发送数据
    for (offset = 0; (rsz = len - offset); offset += n) {
        if (isCancel) {
            DEG_LOG(I, "Operation cancelled by user\n");
            return;
        }
        n = rsz > step ? step : rsz;
        memcpy(io->temp_buf, &mem[offset], n);
        encode_msg_nocpy(io, BSL_CMD_MIDST_DATA, n);
        send_msg(io);
        ret = recv_msg_timeout(io, 15000);
        if (!ret) ERR_EXIT("timeout reached\n");
        if ((ret = recv_type(io)) != BSL_REP_ACK) {
            const char* name_enum = get_bsl_enum_name(ret);
            DEG_LOG(E, "unexpected response (%s : 0x%04x)", name_enum, ret);
            break;
        }
    }

    // 发送结束命令
    encode_msg_nocpy(io, BSL_CMD_END_DATA, 0);
    if (!send_and_check(io)) {
        double etime = get_time();
        double t = etime - rtime;

        DEG_LOG(I, "Write NV partition %s successfully, target: 0x%llx, written: 0x%llx\n",
                name, (long long)len, (long long)offset);
        DEG_LOG(I, "Cost time %.6f seconds", t);
    }

    // 注意：这里不释放 mem，由调用者负责
}

void signal_handler(int sig) {
	(void)sig;
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
	snprintf(name_tmp, strlen(name) + 5 + 1, "%s_size", name);
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
	// factorynv has no vab partition, but not supported to flash.
	else if (strstr(name, "factorynv")){
		if (selected_ab > 0) {
			size_t namelen = strlen(name);
			if ((strcmp(name + namelen - 2, "_a") == 0) || (strcmp(name + namelen - 2, "_b") == 0)) return 0;
		}
	}
	else if (strstr(name, "downloadnv")){
		if (selected_ab > 0) {
			size_t namelen = strlen(name);
			if (strcmp(name + namelen - 2, "_a") && strcmp(name + namelen - 2, "_b")) return 0;
		}
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
	if (end == 10) 
	{
		Da_Info.dwStorageType = 0x101;
		DEG_LOG(I, "Storage is nand.");
		gui_idle_call([](){
			helper.setLabelText(helper.getWidget("storage_mode"),"Nand");
		});
	}
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
		if (g_app_state.flash.gpt_failed == 1) io->ptable = partition_list(io, &io->part_count);
		if (i > io->part_count) {
			DEG_LOG(E,"part not exist: ", name);
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
	if (io->part_count && io->ptable && strcmp(name, "w_force") != 0) {
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
	else if (io->part_count_c && io->Cptable && strcmp(name, "w_force") != 0)
	{
		if (selected_ab > 0) snprintf(name_ab, sizeof(name_ab), "%s_%c", name, 96 + selected_ab);
		for (i = 0; i < io->part_count_c; i++) {
			if (!strcmp(name, (*(io->Cptable + i)).name)) break;
			if (selected_ab > 0 && !strcmp(name_ab, (*(io->Cptable + i)).name)) {
				name = name_ab;
				break;
			}
		}
		if (i < io->part_count_c) {
			strcpy(gPartInfo.name, name);
			gPartInfo.size = (*(io->Cptable + i)).size;
		}
		else gPartInfo.size = 0;
		io->verbose = verbose;
		return;
	}

	if (selected_ab < 0) select_ab(io);
	gPartInfo.size = check_partition(io, name, need_size);
	if (!gPartInfo.size && selected_ab > 0 && strcmp(name, "w_force") != 0) {
		snprintf(name_ab, sizeof(name_ab), "%s_%c", name, 96 + selected_ab);
		gPartInfo.size = check_partition(io, name_ab, need_size);
		name = name_ab;
	}
	if (!gPartInfo.size) {
		DEG_LOG(E,"part not exist: %s", name);
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
    // 1. 解析 XML 文件
    XmlParser parser;
    auto root = parser.parseFile(fn);
    if (!root) {
        ERR_EXIT("Failed to parse XML file\n");
    }

    auto partitionsNodes = root->getDescendants("Partitions");
    if (partitionsNodes.empty()) {
        ERR_EXIT("No <Partitions> element found\n");
    }
    if (partitionsNodes.size() > 1) {
        ERR_EXIT("xml: more than one partition lists\n");
    }

    auto partitions = partitionsNodes[0];
    auto partitionNodes = partitions->getChildren("Partition");

    // 2. 动态分配分区数组（与原函数一致）
    partition_t* partitionsArr = NEWN partition_t[128];  
    int found = 0;

    for (auto& partNode : partitionNodes) {
        if (found >= 128) break;
        std::string id = partNode->getAttribute("id");
        if (id.empty()) {
            ERR_EXIT("Partition missing id attribute\n");
        }
        std::string sizeStr = partNode->getAttribute("size");
        if (sizeStr.empty()) {
            ERR_EXIT("Partition missing size attribute\n");
        }
        char* endptr;
        long long size = strtoll(sizeStr.c_str(), &endptr, 0);
        if (*endptr != '\0' && !isspace(*endptr)) {
            ERR_EXIT("Invalid size value\n");
        }
        strncpy(partitionsArr[found].name, id.c_str(), sizeof(partitionsArr[found].name) - 1);
        partitionsArr[found].name[sizeof(partitionsArr[found].name) - 1] = '\0';
        partitionsArr[found].size = size;   // 原代码直接赋值 size，后面再根据情况左移
        found++;
    }

    int ubi = 0;
    if (!strncmp(fn, "ubi", 3)) ubi = 1;

    for (int i = 0; i < found; i++) {
        if (isCancel) { return; }
        DBG_LOG("Partition %d: name=%s, size=%llim\n", i + 1, partitionsArr[i].name, partitionsArr[i].size);
        if (!strncmp(partitionsArr[i].name, "userdata", 8)) continue;

        get_partition_info(io, partitionsArr[i].name, 0);
        if (!gPartInfo.size) continue;

        long long finalSize = 0;
        if (!strncmp(partitionsArr[i].name, "splloader", 9)) {
            finalSize = (long long)g_spl_size;
        } else if (0xffffffff == partitionsArr[i].size) {
            finalSize = check_partition(io, gPartInfo.name, 1);
        } else if (ubi) {
            int block = (int)(partitionsArr[i].size * (1024 / nand_info[2]) + partitionsArr[i].size * (1024 / nand_info[2]) / (512 / nand_info[1]) + 1);
            finalSize = 1024 * (nand_info[2] - 2 * nand_info[0]) * block;
        } else {
            finalSize = partitionsArr[i].size << 20;
        }
        gPartInfo.size = finalSize;

        char dfile[40];
        snprintf(dfile, sizeof(dfile), "%s.bin", partitionsArr[i].name);
        dump_partition(io, gPartInfo.name, 0, gPartInfo.size, dfile, step);
    }

    if (selected_ab > 0) {
        DBG_LOG("saving slot info\n");
        dump_partition(io, "misc", 0, 1048576, "misc.bin", step);
    }

    if (savepath[0]) {
        DEG_LOG(OP,"Saving dump list");
        size_t size = 0;
        char* src = (char*)loadfile(fn, &size, 1);
        if (src) {
            EnhancedFile fo = my_oxfopen_enhanced(savepath, "wb");
            if (fo) {
                fo.write(src, 1, size);
            } else {
                DEG_LOG(W,"Create dump list failed, skipped.");
            }
			delete[] src;
        } else {
            DEG_LOG(W,"Failed to reload original XML for saving.");
        }
    }
	delete[] partitionsArr;
}

int ab_compare_slots(const slot_metadata *a, const slot_metadata *b);
static inline bool iequals(const std::string& a, const std::string& b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(),
               [](char a, char b) {
                   return std::tolower(static_cast<unsigned char>(a)) ==
                          std::tolower(static_cast<unsigned char>(b));
               });
}
static inline bool istartswith(const std::string& str, const std::string& prefix) {
    if (str.size() < prefix.size()) return false;
    return std::equal(str.begin(), str.begin() + prefix.size(),
                      prefix.begin(),
                      [](char a, char b) {
                          return std::tolower(static_cast<unsigned char>(a)) ==
                                 std::tolower(static_cast<unsigned char>(b));
                      });
}

bool hasPartition(const std::vector<std::string>& partitions, const std::string& partitionName)
{
    return std::find_if(partitions.begin(), partitions.end(),
        [&partitionName](const std::string& s) {
            return iequals(s, partitionName);
        }) != partitions.end();
}
std::string case_part(const std::vector<std::string>& partitions, 
                      const std::string& partitionName,
                      spdio_t* io) {
    // 1. 先找精确匹配（区分大小写）
    auto exact_it = std::find(partitions.begin(), partitions.end(), partitionName);
    if (exact_it != partitions.end()) {
        return partitionName;
    }
    
    // 2. 精确匹配找不到，找不区分大小写的匹配
    auto case_insensitive_it = std::find_if(partitions.begin(), partitions.end(),
        [&partitionName](const std::string& s) {
            return iequals(s, partitionName);
        });
    
    if (case_insensitive_it != partitions.end()) {
    	DEG_LOG(W, "Warning: File name '%s' does not match case of existing partition '%s'. Using '%s'.",
				partitionName.c_str(), case_insensitive_it->c_str(), case_insensitive_it->c_str());
        return *case_insensitive_it;
    }
    
    // 3. 从 io->ptable 中查找
    for (int i = 0; i < io->part_count; ++i) {
        if (partitionName == io->ptable[i].name) {
            return partitionName;
        }
    }
    for (int i = 0; i < io->part_count; ++i) {
        if (iequals(partitionName, io->ptable[i].name)) {
        	DEG_LOG(W, "Warning: File name '%s' does not match case of existing partition '%s'. Using '%s'.",
        		partitionName.c_str(), io->ptable[i].name, io->ptable[i].name);
            return io->ptable[i].name;
        }
    }
    
    // 4. 从 io->Cptable 中查找
    for (int i = 0; i < io->part_count_c; ++i) {
        if (partitionName == io->Cptable[i].name) {
            return partitionName;
        }
    }
    for (int i = 0; i < io->part_count_c; ++i) {
        if (iequals(partitionName, io->Cptable[i].name)) {
        	DEG_LOG(W, "Warning: File name '%s' does not match case of existing partition '%s'. Using '%s'.",
				partitionName.c_str(), io->Cptable[i].name, io->Cptable[i].name);
            return io->Cptable[i].name;
        }
    }
    
    return "";
}
int get_nvlist_xml(spdio_t *io, const char *fn) {
    // 1. 解析 XML 文件
    XmlParser parser;
    auto root = parser.parseFile(fn);
    if (!root) {
        if (io->verbose) DEG_LOG(E, "Error: parse XML file %s failed", fn);
        return 0;
    }

    // 2. 分配并清零 nvid_list
    io->nvid_list = NEWN int[0x10000];
    if (!io->nvid_list) {
        DEG_LOG(E,"malloc failed");
        return 0;
    }

    // 3. 查找所有 NVItem 节点（通用 XML 支持任意嵌套）
    auto nvItems = root->getDescendants("NVItem");
    if (nvItems.empty()) {
        if (io->verbose) DEG_LOG(W,"can't find NVItem from input");
    }

    // 遍历每个 NVItem，提取其子节点 <ID> 的值
    for (auto& nvItem : nvItems) {
        auto idNode = nvItem->getFirstChild("ID");
        if (idNode) {
            std::string idText = idNode->getTextContent();
            // 去除首尾空白
            size_t start = idText.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) continue;
            size_t end = idText.find_last_not_of(" \t\r\n");
            std::string trimmed = idText.substr(start, end - start + 1);
            long id = strtol(trimmed.c_str(), nullptr, 0);
            if (id >= 0 && id < 0x10000) {
                io->nvid_list[id] = 1;
                if (io->verbose) printf("saved id 0x%lX to list\n", id);
            }
        }
    }

    // 4. 强制添加固定 ID（与原函数完全一致）
    io->nvid_list[5] = 1;
    io->nvid_list[0x179] = 1;
    io->nvid_list[0x186] = 1;
    io->nvid_list[0x1e4] = 1;
    io->nvid_list[2] = 1;
    io->nvid_list[0x516] = 1;
    io->nvid_list[0x12d] = 1;
    io->nvid_list[0x9c4] = 1;

    return 1;
}

int get_nvlist_cfg(spdio_t *io, char *fn) 
{
	char line[512];
	unsigned int id = 0;
	EnhancedFile cfg_fd = my_oxfopen_enhanced(fn, "rb");

	if (!cfg_fd) return 0;
	io->nvid_list = NEWN int[0x10000];
	if (!io->nvid_list) ERR_EXIT("malloc failed\n");
	memset(io->nvid_list, 0, 0x10000 * sizeof(int));
	while (cfg_fd.gets(line, sizeof(line))) {
		if (line[0] == '#' || line[0] == '\0') continue;
		if (-1 == sscanf(line, "%*s %x", &id)) continue;
		io->nvid_list[id] = 1;
		if (io->verbose) DBG_LOG("saved id 0x%X to list\n", id);
	}
	io->nvid_list[5] = 1;
	io->nvid_list[0x179] = 1;
	io->nvid_list[0x186] = 1;
	io->nvid_list[0x1e4] = 1;
	io->nvid_list[2] = 1;
	io->nvid_list[0x516] = 1;
	io->nvid_list[0x12d] = 1;
	io->nvid_list[0x9c4] = 1;
	return 1;
}

void merge_nv(spdio_t *io, const uint8_t *a, size_t a_size, const uint8_t *b,
              size_t b_size, uint8_t *c, size_t *c_size)
{
    NVEntry *nvid_list_offset = NEWN NVEntry[0x10000];
    if (!nvid_list_offset) ERR_EXIT("malloc failed\n");
    memset(nvid_list_offset, 0, 0x10000 * sizeof(NVEntry));

    // 安全写入 2 字节（避免未对齐）
    auto write16 = [](uint8_t *dst, uint16_t val) {
        memcpy(dst, &val, sizeof(val));
    };

    // 解析 a，构建偏移表
    size_t pos = 4;
    if (*(uint32_t *)a == 0x4e56) pos += 0x200;

    while (pos + 4 <= a_size) {
        uint16_t type = *(uint16_t *)(a + pos);
        uint16_t length = *(uint16_t *)(a + pos + 2);
        pos += 4;

        if (length == 0 || pos + length > a_size) {
            break; // 无效条目，停止解析
        }

        nvid_list_offset[type].length = length;
        nvid_list_offset[type].offset = pos;
        pos += length;

        uint32_t doffset = ((pos + 3) & 0xFFFFFFFC) - pos;
        pos += doffset;

        if (*(uint16_t *)(a + pos) == 0xffff) break;
    }
    // 合并输出
    uint8_t *c_ptr = c;
    pos = 4;
    if (*(uint32_t *)b == 0x4e56) pos += 0x200;
    memcpy(c_ptr, b + pos - 4, 4);
    c_ptr += 4;

    while (pos + 4 <= b_size) {
        uint16_t type = *(uint16_t *)(b + pos);
        uint16_t length = *(uint16_t *)(b + pos + 2);
        pos += 4;
        if (pos + length > b_size) break;

        // 如果 a 中该 type 有效且需要覆盖（io->nvid_list[type] 为真）
        if (io->nvid_list[type] && nvid_list_offset[type].length > 0) {
            // 使用 a 中的条目覆盖 b 的条目
            write16(c_ptr, type);
            write16(c_ptr + 2, nvid_list_offset[type].length);
            memcpy(c_ptr + 4, a + nvid_list_offset[type].offset,
                   nvid_list_offset[type].length);
            c_ptr += 4 + nvid_list_offset[type].length;
        } else {
            // 保留 b 中的条目
            memcpy(c_ptr, b + pos - 4, 4 + length);
            c_ptr += 4 + length;
        }

        nvid_list_offset[type].saved = 1;
        pos += length;

        // 复制 b 中的对齐填充（保持布局不变）
        uint32_t doffset = ((pos + 3) & 0xFFFFFFFC) - pos;
        memcpy(c_ptr, b + pos, doffset);
        pos += doffset;
        c_ptr += doffset;

        if (*(uint16_t *)(b + pos) == 0xffff) break;
    }

    // 追加 a 中独有的条目（无论 a 解析是否出错，已解析的有效条目仍会被追加）
    for (int i = 0; i < 0x10000; i++) {
        if (nvid_list_offset[i].length > 0 && nvid_list_offset[i].saved == 0) {
            write16(c_ptr, i);
            write16(c_ptr + 2, nvid_list_offset[i].length);
            memcpy(c_ptr + 4, a + nvid_list_offset[i].offset,
                   nvid_list_offset[i].length);
            c_ptr += 4 + nvid_list_offset[i].length;
        }
    }

    // 写入结束标记
    uint8_t endbuf[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    memcpy(c_ptr, endbuf, 8);
    *c_size = (c_ptr - c) + 8;   // 原逻辑：c_ptr 尚未自增结束标记的长度

    delete[] nvid_list_offset;
}
void load_partitions(spdio_t *io, const char *path, unsigned step, int force_ab, int CMethod) {
	DEG_LOG(OP,"Start to write partitions");
	DEG_LOG(I,"Type CTRL + C to cancel...");
	start_signal();
	std::vector<std::string>& pac_parts = g_app_state.flash.pacptable;
	bool isHasDownloadNV = false;
	int dlnv_id = 0;
	const char* primary_id = nullptr;
	const char* fallback_id = "SPLLoader";
	if (Da_Info.dwStorageType != 0x103) primary_id = "SPLLoaderEMMC";
	else primary_id = "SPLLoaderUFS";
	int primary_index = -1, fallback_index = -1;
	typedef struct {
		char name[36];
		char file_path[1024];
		int written_flag;
	} partition_info_t;
	size_t namelen;
	char miscname[1024] = { 0 };
	int VAB = 0; // slot_in_name
	int partition_count = 0;
	partition_info_t *partitions = NEWN partition_info_t[128];
	if (partitions == nullptr) return;
	char *fn;
#if _WIN32
    char fn_buffer[MAX_PATH];
    WIN32_FIND_DATAW findDataW;
    WIN32_FIND_DATAA findDataA;
    HANDLE hFind = INVALID_HANDLE_VALUE;
    BOOL useW = FALSE;

    // 1. 尝试 UTF-16 版本（UTF-8 → UTF-16）
    wchar_t wpath[ARGV_LEN * 2];
    int len = MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, ARGV_LEN * 2);
    if (len == 0) {
        DEG_LOG(W, "MultiByteToWideChar conversion failed, fallback to ANSI.\n");
        goto fallback_to_ansi;
    }

    wchar_t wsearchPath[ARGV_LEN * 2];
    swprintf(wsearchPath, ARGV_LEN * 2, L"%ls\\*", wpath);

    hFind = FindFirstFileW(wsearchPath, &findDataW);
    if (hFind != INVALID_HANDLE_VALUE) {
        useW = TRUE;
    } else {
        DWORD err = GetLastError();
        DEG_LOG(W, "FindFirstFileW failed (err=%d), fallback to ANSI.\n", err);
    }

fallback_to_ansi:
    if (!useW) {
        char searchPath[ARGV_LEN];
        snprintf(searchPath, ARGV_LEN, "%s\\*", path);
        hFind = FindFirstFileA(searchPath, &findDataA);
        if (hFind == INVALID_HANDLE_VALUE) {
            DEG_LOG(E, "Both W and A versions failed to open directory.\n");
            return;
        }
    }

    // 2. 遍历目录
    if (useW) {
        do {
            if (findDataW.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            WideCharToMultiByte(CP_UTF8, 0, findDataW.cFileName, -1, fn_buffer, MAX_PATH, NULL, NULL);
            fn = fn_buffer;
            // 以下处理逻辑与原版完全相同（使用窄字符 fn）
            namelen = strlen(fn);
            if (!my_strnicmp(fn, primary_id, strlen(primary_id))) { // SPL
                primary_index = partition_count;
            } else if (!my_strnicmp(fn, fallback_id, strlen(fallback_id))) { // SPL
                fallback_index = partition_count;
            }
            if (namelen >= 4) {
                if (!my_stricmp(fn + namelen - 4, ".xml") ||
                    !my_stricmp(fn + namelen - 4, ".exe") ||
                    !my_stricmp(fn + namelen - 4, ".txt")) continue;
            }
            if (!my_strnicmp(fn, "pgpt", 4) ||
                !my_strnicmp(fn, "sprdpart", 8) ||
                !my_strnicmp(fn, "fdl", 3) ||
                !my_strnicmp(fn, "lk", 2) ||
                !my_strnicmp(fn, "0x", 2) ||
                !my_strnicmp(fn, "custom_exec", 11) ||
                my_stristr(fn, "factorynv")) continue;
            snprintf(partitions[partition_count].file_path, sizeof(partitions[partition_count].file_path), "%s/%s", path, fn);
            char *dot = strrchr(fn, '.');
            if (dot != nullptr) *dot = '\0';
            namelen = strlen(fn);
            if (namelen >= 4 && my_stricmp(fn + namelen - 4, "_bak") == 0) continue;
            if (!my_stricmp(fn, "misc")) snprintf(miscname, 1024, "%s", partitions[partition_count].file_path);
            if (namelen > 2) {
                if (!my_stricmp(fn + namelen - 2, "_a")) VAB |= 1;
                else if (!my_stricmp(fn + namelen - 2, "_b")) VAB |= 2;
            }
            strcpy(partitions[partition_count].name, fn);
            partitions[partition_count].written_flag = 0;
            partition_count++;
        } while (FindNextFileW(hFind, &findDataW));
    } else {
        do {
            if (findDataA.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            fn = findDataA.cFileName;  // 直接获得窄字符
            namelen = strlen(fn);
            if (!my_strnicmp(fn, primary_id, strlen(primary_id))) { // SPL
                primary_index = partition_count;
            } else if (!my_strnicmp(fn, fallback_id, strlen(fallback_id))) { // SPL
                fallback_index = partition_count;
            }
            if (namelen >= 4) {
                if (!my_stricmp(fn + namelen - 4, ".xml") ||
                    !my_stricmp(fn + namelen - 4, ".exe") ||
                    !my_stricmp(fn + namelen - 4, ".txt")) continue;
            }
            if (!my_strnicmp(fn, "pgpt", 4) ||
                !my_strnicmp(fn, "sprdpart", 8) ||
                !my_strnicmp(fn, "fdl", 3) ||
                !my_strnicmp(fn, "lk", 2) ||
                !my_strnicmp(fn, "0x", 2) ||
                !my_strnicmp(fn, "custom_exec", 11) ||
                my_stristr(fn, "factorynv")) continue;
            snprintf(partitions[partition_count].file_path, sizeof(partitions[partition_count].file_path), "%s/%s", path, fn);
            char *dot = strrchr(fn, '.');
            if (dot != nullptr) *dot = '\0';
            namelen = strlen(fn);
            if (namelen >= 4 && my_stricmp(fn + namelen - 4, "_bak") == 0) continue;
            if (!my_stricmp(fn, "misc")) snprintf(miscname, 1024, "%s", partitions[partition_count].file_path);
            if (namelen > 2) {
                if (!my_stricmp(fn + namelen - 2, "_a")) VAB |= 1;
                else if (!my_stricmp(fn + namelen - 2, "_b")) VAB |= 2;
            }
            strcpy(partitions[partition_count].name, fn);
            partitions[partition_count].written_flag = 0;
            partition_count++;
        } while (FindNextFileA(hFind, &findDataA));
    }

    FindClose(hFind);
#else
	DIR *dir;
	struct dirent *entry;

	if ((dir = opendir(path)) == nullptr || (entry = readdir(dir)) == nullptr) {
		DEG_LOG(E,"Failed to open directory.\n");
		return;
	}
	do {
		fn = entry->d_name;
		struct stat st;
		if (stat(fn, &st) == 0 && S_ISDIR(st.st_mode)) continue;
		if (entry->d_type == DT_DIR) continue;
		namelen = strlen(fn);
		if (!my_strnicmp(fn, primary_id, strlen(primary_id))) // SPL
		{
			primary_index = partition_count;
		}
		else if (!my_strnicmp(fn, fallback_id, strlen(fallback_id))) //SPL
		{
			fallback_index = partition_count;
		}
		if (namelen >= 4) {
			if (!my_stricmp(fn + namelen - 4, ".xml") ||
				!my_stricmp(fn + namelen - 4, ".exe") ||
				!my_stricmp(fn + namelen - 4, ".txt")) continue;
		}
		if (!my_strnicmp(fn, "pgpt", 4) ||
			!my_strnicmp(fn, "sprdpart", 8) ||
			!my_strnicmp(fn, "fdl", 3) ||
			!my_strnicmp(fn, "lk", 2) ||
			!my_strnicmp(fn, "0x", 2) ||
			!my_strnicmp(fn, "custom_exec", 11) ||
		    my_stristr(fn, "factorynv")) continue;
		snprintf(partitions[partition_count].file_path, sizeof(partitions[partition_count].file_path), "%s/%s", path, fn);
		char *dot = strrchr(fn, '.');
		if (dot != nullptr) *dot = '\0';
		namelen = strlen(fn);
		if (namelen >= 4 && my_stricmp(fn + namelen - 4, "_bak") == 0) continue;
		if (!my_stricmp(fn, "misc")) snprintf(miscname, 1024, "%s", partitions[partition_count].file_path);
		if (namelen > 2) {
			if (!my_stricmp(fn + namelen - 2, "_a")) VAB |= 1;
			else if (!my_stricmp(fn + namelen - 2, "_b")) VAB |= 2;
		}

		strcpy(partitions[partition_count].name, fn);
		partitions[partition_count].written_flag = 0;
		partition_count++;
	} while ((entry = readdir(dir)));
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
	std::vector<std::string> flashed_parts;
	for (int i = 0; i < partition_count; i++) {
		if (isCancel) {  delete[](partitions); return; }
		fn = partitions[i].name;
		std::string relfn;
		if (g_app_state.flash.isPacFlashing)
		{
			relfn = case_part(pac_parts, fn, io);
		}
		else
		{
			relfn = case_part({}, fn, io);
		}
		if (relfn.empty()) get_partition_info(io, fn, 1);
		else get_partition_info(io, relfn.c_str(), 1);
		if (relfn.empty() == false) fn = const_cast<char*>(relfn.c_str());
		bool isRejected = false;
		for (auto& kv : flashed_parts)
		{
			if (!my_stricmp(kv.c_str(), gPartInfo.name))
			{
				DEG_LOG(W, "Conflicting files were found in the file list, pointing to the same partition: %s", kv.c_str());
				DEG_LOG(I, "To protect this partition, duplicate flashing operations have been rejected.");
				isRejected = true;
				continue;
			}
		}
		if (isRejected)
		{
			partitions[i].written_flag = 1;
			continue;
		}
		namelen = strlen(relfn.empty() ? fn : relfn.c_str());
		if (selected_ab == 1 && namelen > 2 && 0 == my_stricmp(fn + namelen - 2, "_b")) { partitions[i].written_flag = 1; continue; }
		else if (selected_ab == 2 && namelen > 2 && 0 == my_stricmp(fn + namelen - 2, "_a")) { partitions[i].written_flag = 1; continue; }
		if (!my_stricmp(fn, "miscdata") && g_app_state.flash.isPacFlashing)
		{
			flashed_parts.emplace_back(gPartInfo.name);
			partitions[i].written_flag = 1;
			continue;
		}
		if (!my_stricmp(fn, "prodnv") && g_app_state.flash.isPacFlashing)
		{
			flashed_parts.emplace_back(gPartInfo.name);
			partitions[i].written_flag = 1;
			continue;
		}
		if (!my_stricmp(fn, "userdata") && g_app_state.flash.isPacFlashing)
		{
			erase_partition(io, gPartInfo.name, CMethod);
			flashed_parts.emplace_back(gPartInfo.name);
			partitions[i].written_flag = 1;
			continue;
		}
		// NV Merge process for PAC flashing
		if (g_app_state.flash.isPacFlashing && g_app_state.flash.isPacMergingNV)
		{
			if (my_stristr(fn, "fixnv1"))
			{
				if (my_stristr(fn, "nr_fixnv1"))
				{
					get_partition_info(io, "nr_fixnv1", 1);
					if (gPartInfo.size && hasPartition(pac_parts, std::string(gPartInfo.name)))
					{
						if (!g_app_state.pac.nr_fixnv1_mem)
						{
							DEG_LOG(W, "Failed to load old NV data for nr_fixnv1, skipping writing.");
							partitions[i].written_flag = 1;
							continue;
						}
						if (get_nvlist_xml(io, g_app_state.flash.pac_xmlPath.c_str())) {
							size_t a_size = 0, b_size = 0, c_size = 0;
							uint8_t *a = g_app_state.pac.nr_fixnv1_mem;
							uint8_t *b = loadfile(partitions[i].file_path, &b_size, 0);
							uint8_t *c = (uint8_t*)malloc(a_size + b_size);
							merge_nv(io, a, a_size, b, b_size, c, &c_size);
							load_nv_partition_from_mem(io, gPartInfo.name, c, step);
							delete[](a); delete[](b); free(c);
							partitions[i].written_flag = 1;
							flashed_parts.emplace_back(gPartInfo.name);
							continue;
						}
						delete[](io->nvid_list);
						io->nvid_list = NULL;
					}
				}
				else if (my_stristr(fn, "l_fixnv1"))
				{
					get_partition_info(io, "l_fixnv1", 1);
					if (!g_app_state.pac.l_fixnv1_mem)
					{
						DEG_LOG(W, "Failed to load old NV data for l_fixnv1, skipping writing.");
						partitions[i].written_flag = 1;
						continue;
					}
					if (gPartInfo.size && hasPartition(pac_parts, std::string(gPartInfo.name)))
					{
						if (get_nvlist_xml(io, g_app_state.flash.pac_xmlPath.c_str())) {
							size_t a_size = 0, b_size = 0, c_size = 0;
							uint8_t *a = g_app_state.pac.l_fixnv1_mem;
							uint8_t *b = loadfile(partitions[i].file_path, &b_size, 0);
							uint8_t *c = (uint8_t*)malloc(a_size + b_size);
							merge_nv(io, a, a_size, b, b_size, c, &c_size);
							load_nv_partition_from_mem(io, gPartInfo.name, c, step);
							delete[](a); delete[](b); free(c);
							partitions[i].written_flag = 1;
							flashed_parts.emplace_back(gPartInfo.name);
							continue;
						}
						delete[](io->nvid_list);
						io->nvid_list = NULL;
					}
				}
			}
		}
		int spl_index = primary_index > -1 ? primary_index : fallback_index;
		if (spl_index > -1) 
		{
			bool isRejected = false;
			for (auto& kv : flashed_parts)
			{
				if (!my_stricmp(kv.c_str(), "splloader"))
				{
					DEG_LOG(W, "Conflicting files were found in the file list, pointing to the same partition: %s", kv.c_str());
					DEG_LOG(I, "To protect this partition, duplicate flashing operations have been rejected.");
					isRejected = true;
					continue;
				}
			}
			if (isRejected)
			{
				partitions[spl_index].written_flag = 1;
				continue;
			}
			load_partition(io, "splloader", partitions[spl_index].file_path, step, CMethod);
			partitions[spl_index].written_flag = 1;
			for (int j = 0; j < partition_count; j++) {
				if (my_strnicmp(partitions[j].name, "splloader", 9) == 0) {
					partitions[j].written_flag = 1;
					break;
				}
			}
			flashed_parts.emplace_back("splloader");
		}
		else
		{
			// Try "splloader" (following)
		}
		if (!my_stricmp(fn, "splloader") ||
			!my_stricmp(fn, "uboot_a") ||
			!my_stricmp(fn, "uboot_b") ||
			!my_stricmp(fn, "vbmeta_a") ||
			!my_stricmp(fn, "vbmeta_b")) {
			if ((!g_app_state.flash.isPacFlashing || hasPartition(pac_parts, fn)) && partitions[i].written_flag == 0) {
				if (relfn.empty())
					get_partition_info(io, fn, 1);
				else
					get_partition_info(io, relfn.c_str(), 1);
				if (!gPartInfo.size) continue;
				bool isAllowed = true;
				for (auto& kv : flashed_parts)
				{
					if (!my_stricmp(gPartInfo.name, kv.c_str()))
					{
						DEG_LOG(W, "Conflicting files were found in the file list, pointing to the same partition: %s", kv.c_str());
						DEG_LOG(I, "To protect this partition, duplicate flashing operations have been rejected.");
						isAllowed = false;
						continue;
					}
				}
				if (isAllowed)
				{
					load_partition(io, gPartInfo.name, partitions[i].file_path, step, CMethod);
					flashed_parts.emplace_back(gPartInfo.name);
				}
				partitions[i].written_flag = 1;
			}
			continue;
		}
		if (!my_stricmp(fn, "uboot") || !my_stricmp(fn, "vbmeta")) {
			if (relfn.empty())
				get_partition_info(io, fn, 0);
			else
				get_partition_info(io, relfn.c_str(), 0);
			if (!gPartInfo.size) continue;
			bool isAllowed = true;
				for (auto& kv : flashed_parts)
				{
					if (!my_stricmp(gPartInfo.name, kv.c_str()))
					{
						DEG_LOG(W, "Conflicting files were found in the file list, pointing to the same partition: %s", kv.c_str());
						DEG_LOG(I, "To protect this partition, duplicate flashing operations have been rejected.");
						isAllowed = false;
						continue;
					}
				}
			if (isAllowed)
				if (!g_app_state.flash.isPacFlashing || hasPartition(pac_parts, fn)) 
				{
					load_partition_unify(io, gPartInfo.name, partitions[i].file_path, step, CMethod);
					flashed_parts.emplace_back(gPartInfo.name);
				}
			partitions[i].written_flag = 1;
			continue;
		}
		if (!my_strnicmp(fn, "vbmeta_", 7)) {
		    if(g_app_state.flash.isPacFlashing)
		    {
    			auto it = std::find_if(pac_parts.begin(), pac_parts.end(),
				[](const std::string& part) {
					return istartswith(part, "vbmeta_");
				});
				if (it == pac_parts.end()) continue;
			}
			if (relfn.empty())
				get_partition_info(io, fn, 0);
			else
				get_partition_info(io, relfn.c_str(), 0);
			if (!gPartInfo.size) continue;
			bool isAllowed = true;
				for (auto& kv : flashed_parts)
				{
					if (!my_stricmp(gPartInfo.name, kv.c_str()))
					{
						DEG_LOG(W, "Conflicting files were found in the file list, pointing to the same partition: %s", kv.c_str());
						DEG_LOG(I, "To protect this partition, duplicate flashing operations have been rejected.");
						isAllowed = false;
						continue;
					}
				}
			if (isAllowed)
			{
				load_partition_unify(io, gPartInfo.name, partitions[i].file_path, step, CMethod);
				flashed_parts.emplace_back(gPartInfo.name);
			}
			partitions[i].written_flag = 1;
			continue;
		}
	}
	int metadata_in_dump = 0, super_in_dump = 0, metadata_id = -1, super_id = -1;
	for (int i = 0; i < partition_count; i++) {
		if (isCancel) { delete[](partitions); return; }
		if (!partitions[i].written_flag) {
			fn = partitions[i].name;
			std::string relfn;
			if (g_app_state.flash.isPacFlashing)
			{
				relfn = case_part(pac_parts, fn, io);
			}
			else
			{
				relfn = case_part({}, fn, io);
			}
			if (g_app_state.flash.isPacFlashing && !hasPartition(pac_parts, fn)) continue;
			if (relfn.empty())
				get_partition_info(io, fn, 0);
			else
				get_partition_info(io, relfn.c_str(), 0);
			if (!gPartInfo.size) continue;
			if (my_stristr(gPartInfo.name, "downloadnv")) {isHasDownloadNV = true; dlnv_id = i; continue; }
			if (!my_stricmp(gPartInfo.name, "metadata")) { metadata_in_dump = 1; metadata_id = i; continue; }
			if (!my_stricmp(gPartInfo.name, "super")) { super_in_dump = 1; super_id = i; continue; }
			bool isAllowed = true;
			for (auto& kv : flashed_parts)
			{
				if (!my_stricmp(kv.c_str(), gPartInfo.name))
				{
					DEG_LOG(W, "Conflicting files were found in the file list, pointing to the same partition: %s", kv.c_str());
					DEG_LOG(I, "To protect this partition, duplicate flashing operations have been rejected.");
					bool isAllowed = false;
					continue;
				}
			}
			if (isAllowed)
			{
				load_partition_unify(io, gPartInfo.name, partitions[i].file_path, step, CMethod);
				flashed_parts.emplace_back(gPartInfo.name);
			}
			partitions[i].written_flag = 1;
			
		}
	}
	if (super_in_dump) {
		bool isAllowed = true;
			for (auto& kv : flashed_parts)
			{
				if (!my_stricmp("super", kv.c_str()))
				{
					DEG_LOG(W, "Conflicting files were found in the file list, pointing to the same partition: %s", kv.c_str());
					DEG_LOG(I, "To protect this partition, duplicate flashing operations have been rejected.");
					isAllowed = false;
					continue;
				}
			}
		if (isAllowed)
			load_partition(io, "super", partitions[super_id].file_path, step, CMethod);
		isAllowed = true;
			for (auto& kv : flashed_parts)
			{
				if (!my_stricmp("metadata", kv.c_str()))
				{
					DEG_LOG(W, "Conflicting files were found in the file list, pointing to the same partition: %s", kv.c_str());
					DEG_LOG(I, "To protect this partition, duplicate flashing operations have been rejected.");
					isAllowed = false;
					continue;
				}
			}
		if (isAllowed)
		{
			if (metadata_in_dump) load_partition(io, "metadata", partitions[metadata_id].file_path, step, CMethod);
			else erase_partition(io, "metadata", CMethod);
		}
	}
	if (selected_ab == 1) set_active(io, "a", CMethod);
	else if (selected_ab == 2) set_active(io, "b", CMethod);
	selected_ab = selected_ab_bak;
	if (isHasDownloadNV && dlnv_id)
	{
		if (!g_app_state.flash.isPacFlashing || !g_app_state.flash.isPacMergingNV) {
			std::string relfn = case_part({}, std::string(partitions[dlnv_id].name), io);
			if (relfn.empty())
				get_partition_info(io, partitions[dlnv_id].name, 1);
			else
				get_partition_info(io, relfn.c_str(), 1);
			bool isAllowed = true;
			for (auto& kv : flashed_parts)
			{
				if (!my_stricmp(gPartInfo.name, kv.c_str()))
				{
					DEG_LOG(W, "Conflicting files were found in the file list, pointing to the same partition: %s", kv.c_str());
					DEG_LOG(I, "To protect this partition, duplicate flashing operations have been rejected.");
					isAllowed = false;
					continue;
				}
			}
			if (isAllowed)
				load_partition_unify(io, gPartInfo.name, partitions[dlnv_id].file_path, step, CMethod);
		}
		else
		{
			std::string relfn = case_part(pac_parts, std::string(partitions[dlnv_id].name), io);
			if (relfn.empty())
				get_partition_info(io, partitions[dlnv_id].name, 1);
			else
				get_partition_info(io, relfn.c_str(), 1);
			bool isAllowed = true;
			for (auto& kv : flashed_parts)
			{
				if (!my_stricmp(gPartInfo.name, kv.c_str()))
				{
					DEG_LOG(W, "Conflicting files were found in the file list, pointing to the same partition: %s", kv.c_str());
					DEG_LOG(I, "To protect this partition, duplicate flashing operations have been rejected.");
					isAllowed = false;
					continue;
				}
			}
			if (isAllowed)
				if (hasPartition(pac_parts, std::string(gPartInfo.name)))
				{
					if (!g_app_state.pac.downloadnv_mem)
					{
						DEG_LOG(W, "Failed to load old NV data for downloadnv, skipping writing.");
						partitions[dlnv_id].written_flag = 1;
						delete[](partitions);
						return;
					}
					if (get_nvlist_xml(io, g_app_state.flash.pac_xmlPath.c_str())) {
						size_t a_size = 0, b_size = 0, c_size = 0;
						uint8_t *a = g_app_state.pac.downloadnv_mem;
						uint8_t *b = loadfile(partitions[dlnv_id].file_path, &b_size, 0);
						uint8_t *c = (uint8_t*)malloc(a_size + b_size);
						merge_nv(io, a, a_size, b, b_size, c, &c_size);
						load_nv_partition_from_mem(io, gPartInfo.name, c, step);
						delete[](a); delete[](b); free(c);
					}
					delete[](io->nvid_list);
					io->nvid_list = NULL;
				}
		}
	}
	delete[](partitions);
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
	char ch = '\1'; // For SPRD/UNISOC devices, AVB_VBMETA_IMAGE_FLAGS_VERIFICATION_DISABLED is enough.
	w_mem_to_part_offset(io, "vbmeta", 0x7B, (uint8_t *)&ch, 1, step, CMethod); // DHTB
}


void dm_enable(spdio_t *io, unsigned step, int CMethod) {
	const char *list[] = { "vbmeta", "vbmeta_system", "vbmeta_vendor", "vbmeta_system_ext", "vbmeta_product", "vbmeta_odm", nullptr };
	char ch = '\0';
	for (int i = 0; list[i] != nullptr; i++){ w_mem_to_part_offset(io, list[i], 0x7B, (uint8_t *)&ch, 1, step, CMethod);} // DHTB
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
	if (savepath[0]) snprintf(fix_fn, sizeof(fix_fn), "%s/%s", savepath, dfile);
	else strcpy(fix_fn, dfile);

	EnhancedFile fi;
	if (offset == 0) fi = oxfopen_enhanced(fix_fn, "wb");
	else {
		if (gPartInfo.size != (long long)dump_partition(io, gPartInfo.name, 0, gPartInfo.size, fix_fn, step)) {
			remove(fix_fn);
			return;
		}
		fi = oxfopen_enhanced(fix_fn, "rb+");
	}
	if (!fi) ERR_EXIT("fopen %s failed\n", fix_fn);
	if (fi.seek(offset, SEEK_SET) != 0) ERR_EXIT("fseek failed\n");
	if (fi.write(mem, 1, length) != length) ERR_EXIT("fwrite failed\n");
	fi.close();
	load_partition_unify(io, gPartInfo.name, fix_fn, step, CMethod);
}

// 1 main written and _bak not written, 2 both written
int load_partition_unify(spdio_t *io, const char *name, const char *fn, unsigned step, int CMethod) {
	char name0[36], name1[40];
	unsigned size0, size1;

	get_partition_info(io, name, 1);
	if (!gPartInfo.size) return 0;

	if (strstr(name, "fixnv1") || 
		strstr(name, "downloadnv"))
		{
			load_nv_partition(io, name, fn, 4096);
			return 1;
		}
	if (strstr(name, "factorynv"))
		{
			DEG_LOG(W,"factorynv is not supported to flash, skipped.");
			return 0; // (tested) if factorynv flashed, downloadnv will be broken
		}
	if (Da_Info.dwStorageType == 0x101 ||
		io->part_count == 0 ||
		strncmp(name, "splloader", 9) == 0) {
		load_partition(io, name, fn, step, CMethod);
		return 1;
	}
	if (selected_ab > 0 && g_app_state.flash.g_w_force == 0)
	{
		load_partition(io, name, fn, step, CMethod);
		return 1;
	}
	// Not VAB must w_force when _bak partition exists (original partition list needed)

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
			EnhancedFile fi = my_oxfopen_enhanced(fn, "rb+");
			if (!fi) { DEG_LOG(E,"fopen %s failed\n", fn); return 1; }
			if (fi.seek(0x7B, SEEK_SET) != 0) { DEG_LOG(E,"fseek failed"); return 1; }
			if (fi.write(&ch, 1, 1) != 1) { DEG_LOG(E,"fwrite failed\n"); return 1; }
		}
		load_partition(io, name1, fn, step, CMethod);
		return 2;
	}
	return 1;
}

void set_active(spdio_t *io, const char *arg, int CMethod) {
    int slot = *arg - 'a';
    int other = 1 - slot;

    // 在栈上分配完整结构体，自动清零
    bootloader_control abc = {};

    // 1. 初始化所有多字节字段为小端格式（分区规范要求）
    abc.magic = htole32(0x42414342);         // "BCAB"
    abc.version = 1;
    abc.nb_slot = 2;                        // 共 2 个槽位
    abc.recovery_tries_remaining = 0;
    abc.merge_status = 0;

    // 2. 完整初始化 slot_suffix（保证字符串格式）
    abc.slot_suffix[0] = '_';
    abc.slot_suffix[1] = *arg;
    abc.slot_suffix[2] = '\0';             // slot_suffix[3] 保持 0

    // 3. 设置主槽属性（与原逻辑完全一致）
    abc.slot_info[slot].priority = 15;
    abc.slot_info[slot].tries_remaining = 6;
    abc.slot_info[slot].successful_boot = 0;

    // 4. 设置备用槽属性（与原逻辑完全一致）
    abc.slot_info[other].priority = 14;
    abc.slot_info[other].tries_remaining = 1;
    abc.slot_info[other].successful_boot = 0;

    // 槽位 2、3 已通过 {0} 清零，表示不可用

    // 5. 动态计算 CRC 校验范围（不含 crc32_le 字段）
    size_t crc_len = offsetof(bootloader_control, crc32_le);
    uint32_t crc_val = crc32(0, (const uint8_t*)&abc, crc_len);
    abc.crc32_le = htole32(crc_val);        // CRC 结果也转为小端存储

    // 6. 写入 misc 分区
    w_mem_to_part_offset(io, "misc", 0x800,
                         (uint8_t*)&abc, sizeof(abc), 0x1000, CMethod);
}
#ifdef _WIN32
std::wstring utf8_to_utf16(const std::string& utf8) {
	if (utf8.empty()) return L"";
	int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
	if (len <= 0) return L"";
	std::wstring wstr(len, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wstr.data(), len);
	wstr.pop_back();
	return wstr;
}

std::string utf16_to_utf8(const std::wstring& wstr) {
	if (wstr.empty()) return "";
	int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
	if (len <= 0) return "";
	std::string utf8(len, '\0');
	WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, utf8.data(), len, nullptr, nullptr);
	utf8.pop_back();
	return utf8;
}
#endif
