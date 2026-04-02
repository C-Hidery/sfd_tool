#include "logging.h"
#include "../common.h"
#include "../ui/ui_common.h"
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

extern bool Err_Showed;
extern bool isHelperInit;
extern GtkWidgetHelper helper;

void ERR_EXIT(const char* format, ...) {
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	if (Err_Showed) return;
	Err_Showed = true;
	if (isHelperInit){
		gui_idle_call_wait_drag([]() {
			showErrorDialog(helper.getWidget("main_window") ? GTK_WINDOW(helper.getWidget("main_window")) : nullptr, "Error", "An error occurred. The application will now exit.\n监测到错误，应用程序将退出。");
		}, helper.getWidget("main_window") ? GTK_WINDOW(helper.getWidget("main_window")) : nullptr);
		helper.disableWidget("fdl_exec");
    	helper.disableWidget("poweroff");
    	helper.disableWidget("reboot");
    	helper.disableWidget("recovery");
    	helper.disableWidget("fastboot");
    	helper.disableWidget("list_read");
    	helper.disableWidget("list_write");
    	helper.disableWidget("list_erase");
    	helper.disableWidget("m_write");
    	helper.disableWidget("m_read");
    	helper.disableWidget("m_erase");
    	helper.disableWidget("set_active_a");
    	helper.disableWidget("set_active_b");
    	helper.disableWidget("start_repart");
    	helper.disableWidget("blk_size");
    	helper.disableWidget("read_xml");
    	helper.disableWidget("dmv_enable");
    	helper.disableWidget("dmv_disable");
    	helper.disableWidget("backup_all");
    	helper.disableWidget("list_cancel");
    	helper.disableWidget("m_cancel");
    	helper.disableWidget("list_force_write");
    	helper.disableWidget("check_nand");
    	helper.disableWidget("pac_time");
    	helper.disableWidget("chip_uid");
    	helper.disableWidget("dis_avb");
    	helper.disableWidget("transcode_en");
    	helper.disableWidget("transcode_dis");
    	helper.disableWidget("end_data_dis");
    	helper.disableWidget("end_data_en");
    	helper.disableWidget("charge_en");
    	helper.disableWidget("charge_dis");
    	helper.disableWidget("raw_data_en");
    	helper.disableWidget("raw_data_dis");
    	helper.disableWidget("modify_part");
    	helper.disableWidget("modify_new_part");
    	helper.disableWidget("modify_rm_part");
    	helper.disableWidget("modify_ren_part");
    	helper.disableWidget("xml_get");
    	helper.disableWidget("abpart_auto");
    	helper.disableWidget("abpart_a");
    	helper.disableWidget("abpart_b");
		helper.disableWidget("pac_flash_start");
	}
	std::thread([](){
#ifdef _WIN32
		call_DisconnectChannel(g_app_state.transport.io->handle);
		if (g_app_state.transport.io->m_dwRecvThreadID) DestroyRecvThread(g_app_state.transport.io);
		call_Uninitialize(g_app_state.transport.io->handle);
		destroyClass(g_app_state.transport.io->handle);
		system("pause");
#else
		sleep(5);
#endif
		
		exit(EXIT_FAILURE);
	}).detach();
}

// 内部：统一的实际输出函数，保持原有格式/路由行为
static void logMessageInternal(int type, const char* message) {
	time_t now = time(nullptr);
	struct tm* local_time = localtime(&now);
	char timestamp[64];
	strftime(timestamp, sizeof(timestamp), "[%H:%M:%S] ", local_time);

	const char* prefix;
	switch(type) {
		case I:  prefix = "[i] ";  break;
		case W:  prefix = "[!] ";  break;
		case E:  prefix = "[x] ";  break;
		case OP: prefix = "[=] ";  break;
		case DE: prefix = "[DE] "; break;
		default: prefix = "[UN] "; break;
	}

	if (type == I || type == W || type == OP) {
		fprintf(stdout, "%s%s%s\n", timestamp, prefix, message);
	} else {
		fprintf(stderr, "%s%s%s\n", timestamp, prefix, message);
	}

	// GUI: 仍然通过 ui_common 里的封装追加到日志视图
	append_log_to_ui(type, message);
}

void DEG_LOG(int type, const char* format, ...) {
	va_list args;
	va_start(args, format);
	char buffer[1024];
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	// 维持原有等级/时间戳/路由行为
	logMessageInternal(type, buffer);
}

int logLevelToType(LogLevel level) {
	switch (level) {
	case LogLevel::Info:    return I;
	case LogLevel::Warning: return W;
	case LogLevel::Error:   return E;
	case LogLevel::Op:      return OP;
	case LogLevel::Debug:   return DE;
	}
	// 理论上不会到达这里，fallback 为 Error
	return E;
}

void logMessage(LogLevel level, const char* module, const char* format, ...) {
	// 先把原始 format+参数格式化到一个中间缓冲区
	char msg_buf[1024];
	va_list args;
	va_start(args, format);
	vsnprintf(msg_buf, sizeof(msg_buf), format, args);
	va_end(args);

	// 若提供了模块名，则在消息前加上 [module] 前缀
	char final_buf[1024];
	const char* to_send = msg_buf;
	if (module && *module) {
		// 预留简单空间，必要时截断
		int written = snprintf(final_buf, sizeof(final_buf), "[%s] %s", module, msg_buf);
		if (written > 0) {
			to_send = final_buf;
		}
	}

	int type = logLevelToType(level);
	logMessageInternal(type, to_send);
}

void print_mem(FILE *f, const uint8_t *buf, size_t len) {
	size_t i; int a, j, n;
	for (i = 0; i < len; i += 16) {
		n = len - i;
		if (n > 16) n = 16;
		for (j = 0; j < n; j++) fprintf(f, "%02x ", buf[i + j]);
		for (; j < 16; j++) fprintf(f, "   ");
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
        if ((size_t)needed > remaining) {
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
