/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * 
 * Copyright (C) 2026 Ryan Crepa
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 * 
 * This project contains code originally from spreadtrum_flash
 * (MIT License, Copyright (c) TomKing062)
 * See spreadtrum_flash/spd_dump.c for details.
 */

/*
 * [sfd_tool] - Low-level device communication module
 * 
 * This module contains code that works with certain UNISOC bootrom behaviors.
 * The implementation is based on publicly disclosed information from 2026.
 * 
 * USE RESTRICTION: This tool is designed for legal device maintenance only.
 * The author does not authorize its use for any illegal purpose, including
 * but not limited to unlocking stolen devices or circumventing paid services.
 * 
 * Users assume all legal responsibility for their usage.
 */
#include <iostream>
#include <cstring>
#include <string>
#include <cstdlib>
#include "common.h"
#include "main.h"
#include "ui/GtkWidgetHelper.hpp"
#include "i18n.h"
#include "ui/ui_common.h"
#include "pages/page_connect.h"
#include "pages/page_partition.h"
#include "pages/page_manual.h"
#include "pages/page_advanced_op.h"
#include "pages/page_advanced_set.h"
#include "pages/page_debug.h"
#include "pages/page_about.h"
#include "pages/page_log.h"
#include "pages/page_pac_flash.h"
#include <thread>
#include <chrono>
#include <gtk/gtk.h>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include "version.h"
#include "core/config_service.h"
#ifdef __linux__
#include <unistd.h>
#include <execinfo.h>
#include <limits.h>
#include <sys/stat.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#include <dbghelp.h>
#include <sys/stat.h>
#endif
#include "ui/layout/bottom_bar.h"

#ifdef _WIN32
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#endif

std::string g_about_text;
extern guint g_drag_check_timeout;
namespace {

static std::string get_effective_lc_all_from_ui_language(const std::string& ui_language) {
    if (ui_language.empty() || ui_language == "auto") {
        return std::string();
    }
    if (ui_language == "zh_CN") {
#ifndef _WIN32
        return "zh_CN.UTF-8";
#else
		return "zh_CN";
#endif
	}
    if (ui_language == "en_US") {
#ifndef _WIN32
        return "en_US.UTF-8";
#else
		return "en_US";
#endif
    }
    // 未知值：退回系统默认
    return std::string();
}

static std::string get_executable_dir() {
#if defined(__linux__)
    char path[PATH_MAX] = {0};
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len <= 0) {
        return std::string();
    }
    path[len] = '\0';
    std::string p(path);
    auto pos = p.find_last_of('/');
    if (pos == std::string::npos) {
        return std::string();
    }
    return p.substr(0, pos);
#elif defined(__APPLE__)
    char path[PATH_MAX] = {0};
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0) {
        return std::string();
    }
    std::string p(path);
    auto pos = p.find_last_of('/');
    if (pos == std::string::npos) {
        return std::string();
    }
    return p.substr(0, pos);
#elif defined(_WIN32)
    wchar_t path[MAX_PATH] = {0};
    DWORD len = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (len == 0 || len == MAX_PATH) {
        return std::string();
    }
    
    char utf8_path[MAX_PATH] = {0};
    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, path, len, 
                                        utf8_path, sizeof(utf8_path) - 1, 
                                        NULL, NULL);
    if (utf8_len <= 0) {
        return std::string();
    }
    utf8_path[utf8_len] = '\0';  // 确保终止
    
    std::string p(utf8_path);
    
    // 支持 \ 和 / 两种分隔符
    size_t pos = p.find_last_of("\\/");
    if (pos == std::string::npos) {
        return std::string();
    }
    
    return p.substr(0, pos);
#else
    return std::string();
#endif
}

static bool dir_exists(const std::string& path) {
#if defined(__linux__) || defined(__APPLE__)
    struct stat st;
    return (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
#elif defined(_WIN32)
    // 将 UTF-8 路径转换为 UTF-16
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return false;
    
    std::vector<wchar_t> wpath(wlen);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);
    
    DWORD attr = GetFileAttributesW(wpath.data());
    return (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY);
#else
    (void)path;
    return false;
#endif
}

static std::string choose_locale_dir() {
    std::string exe_dir = get_executable_dir();
#if defined(__APPLE__)
    g_is_macos_bundle = (!exe_dir.empty() && exe_dir.find(".app/Contents/MacOS") != std::string::npos);
#endif

#if defined(__APPLE__)
    // macOS .app Bundle: 优先从 Contents/Resources/locale 查找
    if (!exe_dir.empty()) {
        // exe_dir 形如 /.../SFD Tool.app/Contents/MacOS
        std::string bundle_locale = exe_dir + "/../Resources/locale";
        if (dir_exists(bundle_locale)) {
            return bundle_locale;
        }
    }
#endif

    if (!exe_dir.empty()) {
        std::string exe_locale = exe_dir + "/locale";
        if (dir_exists(exe_locale)) {
            return exe_locale;
        }
    }
    if (dir_exists("./locale")) {
        return "./locale";
    }
    // 都不存在时，返回空字符串，让 gettext 使用系统默认路径
    return std::string();
}

static std::string load_about_from_path(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return std::string();
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static std::string load_installed_about_text() {
    // 1) 优先使用编译期指定的文档目录（Linux 安装包）
#ifdef SFD_TOOL_DOC_DIR
    {
        std::string doc_path = std::string(SFD_TOOL_DOC_DIR) + "/VERSION_LOG.md";
        std::string content = load_about_from_path(doc_path);
        if (!content.empty()) {
            return content;
        }
    }
#endif

    // 2) 其次使用可执行文件所在目录（macOS DMG、Windows 目录运行等）
    std::string exe_dir = get_executable_dir();
#if defined(__APPLE__)
    g_is_macos_bundle = (!exe_dir.empty() && exe_dir.find(".app/Contents/MacOS") != std::string::npos);
#endif
    if (!exe_dir.empty()) {
        {
            std::string path = exe_dir + "/VERSION_LOG.md";
            std::string content = load_about_from_path(path);
            if (!content.empty()) {
                return content;
            }
        }
        {
            std::string path = exe_dir + "/docs/VERSION_LOG.md";
            std::string content = load_about_from_path(path);
            if (!content.empty()) {
                return content;
            }
        }
#if defined(__APPLE__)
        // 预留给 .app Bundle 的资源路径：Contents/MacOS/../Resources
        {
            std::string path = exe_dir + "/../Resources/VERSION_LOG.md";
            std::string content = load_about_from_path(path);
            if (!content.empty()) {
                return content;
            }
        }
#endif
    }

    return std::string();
}

} // namespace

std::string load_about_text() {
	// 1) 开发环境：当前工作目录中的相对路径
	const char* candidates[] = {
		"docs/VERSION_LOG.md",
		"VERSION_LOG.md",
        "../docs/VERSION_LOG.md"
	};

	for (auto path : candidates) {
		std::ifstream in(path);
		if (in) {
			std::ostringstream ss;
			ss << in.rdbuf();
			return ss.str();
		}
	}

	// 2) 已安装环境：使用编译期/可执行文件路径推导出的版本记录文件
	std::string installed = load_installed_about_text();
	if (!installed.empty()) {
		return installed;
	}

	// 3) 仍然找不到时，回退到原有提示
	return "SFD Tool GUI\n\nBy Ryan Crepa\n\nAbout information file missing.\n";
}

AppState g_app_state; // 全局应用状态实例
int& m_bOpened = g_app_state.device.m_bOpened;
int fdl1_loaded = 0;
int fdl2_executed = 0;
int isKickMode = 0;
int& selected_ab = g_app_state.flash.selected_ab;
int no_fdl_mode = 0;
uint64_t fblk_size = 0;
uint64_t g_spl_size;
extern bool isUseCptable;
const char* o_exception;
int init_stage = -1;
int& device_stage = g_app_state.device.device_stage;
int& device_mode = g_app_state.device.device_mode;
//sfd_tool protocol
char mode_str[256];
char* temp;
spdio_t*& io = g_app_state.transport.io;
int ret;
int conn_wait = 30 * REOPEN_FREQ;
int keep_charge = 1, end_data = 0, blk_size = 0, skip_confirm = 1, highspeed = 0, cve_v2 = 0;
int g_default_blk_size = 0;
int nand_info[3];
int argcount = 0, stage = -1, nand_id = DEFAULT_NAND_ID;
unsigned exec_addr = 0, baudrate = 0;
int bootmode = -1, at = 0, async = 1;
int waitFDL1 = -1;
int autoFDL1Suc = 0;
//Set up environment
#if !USE_LIBUSB
extern DWORD curPort;
DWORD* ports;
//Channel9 init(Windows platform)
#else
//libsub init(Linux/Android-termux)
extern libusb_device* curPort;
libusb_device** ports;
#endif
// Moved initialization into gtk_kmain()

#ifndef _WIN32
void check_root_permission(GtkWidgetHelper helper) {
	if (geteuid() != 0) {
		// not root
		showWarningDialogSyncInThread(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Warning"))), _("You are running this tool without root permission!\nIt may cause device connecting issue without device rule(80-spd.rules)."));
	}
}
#endif

bool isCrashed = false;
void crash_handler(int sig) {
	(void)sig;
	if (isCrashed) return;
	isCrashed = true;
#ifdef __linux__
    void* array[20];
    size_t size;

    // 获取回溯信息
    size = backtrace(array, 20);

    // 打印错误信息
    fprintf(stderr, "Error: signal %d:\n", sig);

    // 打印堆栈
    backtrace_symbols_fd(array, size, STDERR_FILENO);
#elif defined(_WIN32)
	fprintf(stderr, "Error: signal %d:\n", sig);
	void* stack[100];
    unsigned short frames;
    SYMBOL_INFO* symbol;
    HANDLE process;

    process = GetCurrentProcess();
    SymInitialize(process, NULL, TRUE);

    frames = CaptureStackBackTrace(0, 100, stack, NULL);
    symbol = (SYMBOL_INFO*)calloc(sizeof(SYMBOL_INFO) + 256 * sizeof(char), 1);
    symbol->MaxNameLen = 255;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

    for (int i = 0; i < frames; i++) {
        SymFromAddr(process, (DWORD64)stack[i], 0, symbol);
        printf("%i: %s - 0x%0llX\n", frames - i - 1, symbol->Name, symbol->Address);
    }

    free(symbol);

#endif
	if (isHelperInit) {
        // 检测当前是否在主线程
        bool is_main_thread = g_main_context_is_owner(g_main_context_default());
        while (isWindowDragging(helper.getWidget("main_window") ? GTK_WINDOW(helper.getWidget("main_window")) : nullptr)) {
            g_main_context_iteration(g_main_context_default(), FALSE);
            g_usleep(10000); // 10ms
        }
        if (is_main_thread) {
            // 主线程中直接执行，无需等待（因为没有异步）
            // 禁用控件并显示对话框
            DisableWidgets(helper);
            GtkWidget* main_window = helper.getWidget("main_window");
            if (main_window) {
                showErrorDialog(GTK_WINDOW(main_window),
				 _("Program Crash"),
				 _("The program encountered an unhandled exception, which may be caused by device connection issues or a bug in the program.\n\nIt is recommended to check the device connection, ensure the correct options are used, and try running the tool again."));
		
            }
        } else {
			gui_idle_call_wait_drag([](){ DisableWidgets(helper); }, GTK_WINDOW(helper.getWidget("main_window")));
            showErrorDialogSyncInThread(GTK_WINDOW(helper.getWidget("main_window")), 
						_("Program Crash"), 
                        _("The program encountered an unhandled exception, which may be caused by device connection issues or a bug in the program.\n\nIt is recommended to check the device connection, ensure the correct options are used, and try running the tool again."));
        }
    } else {
        // 命令行模式
        fprintf(stderr, "Program crashed. Exiting...\n");
#ifndef _WIN32
        std::this_thread::sleep_for(std::chrono::seconds(3));
        std::exit(EXIT_FAILURE);
#else
		system("pause");
		std::exit(EXIT_FAILURE);
#endif
    }
    
    // 4. 等待用户阅读错误信息
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    // 5. 强制退出
    std::exit(EXIT_FAILURE);
}

// 全局快捷键处理：在 macOS 上支持 Command+Q，其他平台支持 Ctrl+Q 退出
static void on_window_destroy(GtkWidget*, gpointer) {
    if (g_drag_check_timeout != 0) {
        g_source_remove(g_drag_check_timeout);
        g_drag_check_timeout = 0;
    }
	gui_quit_main_loop();
}

#if GTK_CHECK_VERSION(4, 0, 0)
static gboolean on_main_window_key_press_gtk4(GtkEventControllerKey* controller,
                                              guint keyval,
                                              guint keycode,
                                              GdkModifierType state,
                                              gpointer user_data) {
    (void)controller;
    (void)keycode;
    (void)user_data;

    // 统一在退出前清理定时器
    bool should_quit = false;
#if defined(__APPLE__)
    if ((state & GDK_META_MASK) && keyval == GDK_KEY_q) {
        should_quit = true;
    }
#else
    if ((state & GDK_CONTROL_MASK) && keyval == GDK_KEY_q) {
        should_quit = true;
    }
#endif

    if (should_quit) {
        // 先取消定时器，再退出
        if (g_drag_check_timeout != 0) {
            g_source_remove(g_drag_check_timeout);
            g_drag_check_timeout = 0;
        }
        gui_quit_main_loop();
        return TRUE;
    }
    return FALSE;
}
#else
static gboolean on_main_window_key_press(GtkWidget* widget, GdkEventKey* event, gpointer user_data) {
	(void)widget;
	(void)user_data;

#if defined(__APPLE__)
	if ((event->state & GDK_META_MASK) && event->keyval == GDK_KEY_q) {
		gtk_main_quit();
		return TRUE;
	}
#else
	if ((event->state & GDK_CONTROL_MASK) && event->keyval == GDK_KEY_q) {
		gtk_main_quit();
		return TRUE;
	}
#endif
	return FALSE;
}
#endif
 //fdl exec
std::string fdl1_path_json;
std::string fdl2_path_json;
uint32_t fdl1_addr_json;
uint32_t fdl2_addr_json;
bool isMaped = false;

// 窗口尺寸适配回调（在 map 信号中触发）
static void on_window_map_adaptive(GtkWidget *widget, gpointer user_data) {
    (void)user_data;
    GtkWindow *window = GTK_WINDOW(widget);
    if (isMaped == true) return;
    isMaped = true;
    
    // 1. 获取窗口所在的 GdkSurface
    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(window));
    if (!surface) return;
    
    // 2. 获取显示器
    GdkDisplay *display = gdk_display_get_default();
    if (!display) return;
    
    GdkMonitor *monitor = gdk_display_get_monitor_at_surface(display, surface);
    if (!monitor) return;
    
    // 3. 获取显示器尺寸
    GdkRectangle geometry;
    gdk_monitor_get_geometry(monitor, &geometry);
    
    // 4. 计算窗口尺寸（复用逻辑）
    const int target_w = 1174;
    const int target_h = 820;
    const int margin_w = 100;
    const int min_w = 800;
    const int min_h = 600;
    
    int win_w = target_w;
    int win_h = target_h;
    
    if (geometry.width > 0) {
        win_w = std::min(target_w, geometry.width - margin_w);
    }
    if (geometry.height > 0) {
        win_h = std::min(target_h, geometry.height);
    }
    
    win_w = std::max(win_w, min_w);
    win_h = std::max(win_h, min_h);
    
    // 5. 调整窗口大小
    gtk_window_set_default_size(window, win_w, win_h);
    DEG_LOG(I, "win_w: %d", win_w);
    DEG_LOG(I, "win_h: %d", win_h);

    gtk_window_present(window);
}

int gtk_kmain(int argc, char** argv) {
    DEG_LOG(I, "Starting GUI mode...");
#if GTK_CHECK_VERSION(4, 0, 0)
    gtk_init();
#else
    gtk_init(&argc, &argv);
#endif
    g_set_prgname("sfd_tool");

    g_about_text = load_about_text();

    // 初始化 IO 和全局状态
    io = spdio_init(0);
#if USE_LIBUSB
    ret = libusb_init(nullptr);
    if (ret < 0) ERR_EXIT("libusb_init failed: %s\n", libusb_error_name(ret));
#else
    io->handle = createClass();
    call_Initialize(io->handle);
#endif
    snprintf(fn_partlist, sizeof(fn_partlist), "partition_%lld.xml", (long long)time(nullptr));

#if defined(__APPLE__)
    // macOS 保存路径设置
    {
        std::string exe_dir = get_executable_dir();
        g_is_macos_bundle = (!exe_dir.empty() && exe_dir.find(".app/Contents/MacOS") != std::string::npos);
        if (g_is_macos_bundle) {
            const char* home = std::getenv("HOME");
            if (home && *home) {
                std::string docs_dir = std::string(home) + "/Documents/sfd_tool";
                struct stat st{};
                if (stat(docs_dir.c_str(), &st) != 0) {
                    mkdir(docs_dir.c_str(), 0755);
                }
                if (stat(docs_dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                    if (docs_dir.size() < sizeof(savepath)) {
                        std::snprintf(savepath, sizeof(savepath), "%s", docs_dir.c_str());
                        DEG_LOG(I, "macOS bundle detected, savepath set to %s", savepath);
                    }
                }
            }
        }
    }
#endif

    // GTK4 替代方案
    GtkWidget *window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), "SFD Tool GUI By Ryan Crepa");

    // 先给一个合理的初始尺寸，让窗口能显示出来
    gtk_window_set_default_size(GTK_WINDOW(window), 1024, 768);

    // 快捷键
#if GTK_CHECK_VERSION(4, 0, 0)
    GtkEventController* controller = gtk_event_controller_key_new();
    g_signal_connect(controller, "key-pressed", G_CALLBACK(on_main_window_key_press_gtk4), NULL);
    gtk_widget_add_controller(window, controller);
#else
    gtk_widget_add_events(window, GDK_KEY_PRESS_MASK);
    g_signal_connect(window, "key-press-event", G_CALLBACK(on_main_window_key_press), NULL);
#endif
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), NULL);

    // 连接 map 信号，窗口映射后自动适配屏幕
    g_signal_connect(window, "map", G_CALLBACK(on_window_map_adaptive), nullptr);

    // 创建主网格
    GtkWidget* mainGrid = gtk_grid_new();

    // 初始化 helper（不再需要 setParent，因为不再使用自动添加）
    helper = GtkWidgetHelper(window);
    isHelperInit = true;
    helper.addWidget("main_window", window);
    initDragDetection(GTK_WINDOW(window));

    // ---- 创建 Notebook ----
    GtkWidget* notebook = gtk_notebook_new();
    helper.addWidget("main_notebook", notebook, "notebook");
    gtk_widget_set_hexpand(notebook, TRUE);
    gtk_widget_set_vexpand(notebook, TRUE);

    // ---- 创建各页面（使用页面模块提供的 create 函数） ----
    create_connect_page(helper, notebook);
    create_partition_page(helper, notebook);
    create_manual_page(helper, notebook);
    create_advanced_op_page(helper, notebook);
    create_pac_flash_page(helper, notebook);
    create_advanced_set_page(helper, notebook);
    create_debug_page(helper, notebook);
    create_log_page(helper, notebook);
    create_about_page(helper, notebook);

    // ---- 底部栏 ----
    GtkWidget* bottomContainer = bottom_bar_create(helper);

    // 将 notebook 和 bottomContainer 添加到 mainGrid
    gtk_grid_attach(GTK_GRID(mainGrid), notebook, 0, 0, 10, 1);
    gtk_grid_attach(GTK_GRID(mainGrid), bottomContainer, 0, 1, 10, 1);

    // CSS 样式（保持不变）
    GtkCssProvider* provider = gtk_css_provider_new();
    const gchar* css =
        "label.big-label { font-size: 20px; }"
        "progressbar { min-height: 9px; }";
#if GTK_CHECK_VERSION(4, 0, 0)
    gtk_css_provider_load_from_string(provider, css);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
            GTK_STYLE_PROVIDER(provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    gtk_window_set_child(GTK_WINDOW(window), mainGrid);
#else
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
            GTK_STYLE_PROVIDER(provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    gtk_container_add(GTK_CONTAINER(window), mainGrid);
#endif

    // 显示窗口（GTK4 默认可见，但为了兼容 GTK4 保留）
#if GTK_CHECK_VERSION(4, 0, 0)
    gtk_widget_set_visible(window, TRUE);
#else
    gtk_widget_show_all(window);
#endif

    // 默认选中第一个标签页
    gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), 0);

    // ---- 绑定信号（所有页面和底部栏） ----
    bind_connect_signals(helper, argc, argv);
    bind_partition_signals(helper);
    bind_manual_signals(helper);
    bind_advanced_op_signals(helper);
    bind_pac_flash_signals(helper);
    bind_advanced_set_signals(helper);
    bind_debug_signals(helper);
    bind_log_signals(helper);
    bind_bottom_signals(helper, bottomContainer);

    DisableWidgets(helper);
    gui_run_main_loop();

    return 0;
}

int main(int argc, char** argv) {
	// 读取配置并根据 ui_language 设置 gettext 语言
	sfd::AppConfig cfg;
	sfd::loadAppConfigOrDefault(cfg); // 即使失败也会填充默认值（含 ui_language）

	// 用于调试：记录当前配置语言
	LOG_INFO("ui_language at startup: %s", cfg.ui_language.c_str());

	bool locale_from_config = false;

#if defined(__linux__) || defined(__APPLE__)
	if (cfg.ui_language == "zh_CN") {
		setenv("LANGUAGE", "zh_CN", 1);
	} else if (cfg.ui_language == "en_US") {
		setenv("LANGUAGE", "en_US", 1);
	}
	// "auto" 或空字符串：不设置 LANGUAGE，沿用环境／系统默认
#elif defined(_WIN32)
	// Windows 上也设置 LANGUAGE 环境变量，libintl 会读取它来决定翻译语言
	if (cfg.ui_language == "zh_CN") {
		_putenv_s("LANGUAGE", "zh_CN");
	} else if (cfg.ui_language == "en_US") {
		_putenv_s("LANGUAGE", "en_US");
	}
	LOG_INFO("LANGUAGE on Windows after config: %s",
	         std::getenv("LANGUAGE") ? std::getenv("LANGUAGE") : "(null)");

	// 同时设置 C 运行时 locale，便于 std::locale / C API 使用
	std::string lc_all = get_effective_lc_all_from_ui_language(cfg.ui_language);
	if (!lc_all.empty() && lc_all != "auto") {
		LOG_INFO("setlocale(LC_ALL, %s) on Windows", lc_all.c_str());
		setlocale(LC_ALL, lc_all.c_str());
		locale_from_config = true;
	} else {
		LOG_INFO("ui_language is auto/empty, using system default locale");
	}
#endif

	// 如果没有通过配置显式设置 locale，则调用一次 setlocale(LC_ALL, "")，
	// 让 C 库从环境变量或系统默认解析 locale。
	if (!locale_from_config) {
		setlocale(LC_ALL, "");
		LOG_INFO("effective locale after setlocale(\"\"): %s", setlocale(LC_ALL, nullptr));
	} else {
		LOG_INFO("effective locale after config locale: %s", setlocale(LC_ALL, nullptr));
	}

	// 根据可执行文件路径选择 locale 目录
	std::string locale_dir = choose_locale_dir();
	LOG_INFO("chosen locale_dir: %s", locale_dir.c_str());
	if (!locale_dir.empty()) {
		// 开发 / 便携包：使用 exe 同目录或 ./locale
		bindtextdomain("sfd_tool", locale_dir.c_str());
	}
	// 如果 locale_dir 为空：不调用 bindtextdomain，
	// 让 gettext 使用系统默认路径（通常是 /usr/share/locale）

	textdomain("sfd_tool");
	bind_textdomain_codeset("sfd_tool", "UTF-8");

	signal(SIGSEGV, crash_handler);   // 段错误
    signal(SIGABRT, crash_handler);   // 断言失败
	signal(SIGFPE, crash_handler);    // 浮点异常
	signal(SIGILL, crash_handler);    // 非法指令
	if (argc > 1 && !strcmp(argv[1], "--no-gui")) {
		// Call the console version of main
		return main_console(argc - 1, argv + 1); // Skip the first argument
	} else {
		return gtk_kmain(argc, argv);
	}
}
