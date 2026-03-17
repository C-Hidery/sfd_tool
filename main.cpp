#include <iostream>
#include <cstring>
#include <string>
#include <cstdlib>
#include "common.h"
#include "main.h"
#include "GtkWidgetHelper.hpp"
#include "i18n.h"
#include "ui_common.h"
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

std::string g_about_text;

namespace {

static std::string get_effective_lc_all_from_ui_language(const std::string& ui_language) {
    if (ui_language.empty() || ui_language == "auto") {
        return std::string();
    }
    if (ui_language == "zh_CN") {
        return "zh_CN.UTF-8";
    }
    if (ui_language == "en_US") {
        return "en_US.UTF-8";
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
    char path[MAX_PATH] = {0};
    DWORD len = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (len == 0 || len == MAX_PATH) {
        return std::string();
    }
    std::string p(path);
    // Windows 路径用反斜杠分隔
    auto pos = p.find_last_of('\\');
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
    if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        return true;
    }
    return false;
#elif defined(_WIN32)
    DWORD attr = GetFileAttributesA(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        return true;
    }
    return false;
#else
    // 简单实现：其他平台暂不检查，直接使用给定路径
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
	return "SFD Tool GUI\n\nAbout information file missing.\n";
}

const char* Version = SFD_TOOL_VERSION;
AppState g_app_state; // 全局应用状态实例
int& m_bOpened   = g_app_state.device.m_bOpened;
int fdl1_loaded = 0;
int fdl2_executed = 0;
int isKickMode = 0;
int& selected_ab = g_app_state.flash.selected_ab;
int no_fdl_mode = 0;
uint64_t fblk_size = 0;
uint64_t g_spl_size;
bool isUseCptable = false;
const char* o_exception;
int init_stage = -1;
int& device_stage = g_app_state.device.device_stage;
int& device_mode = g_app_state.device.device_mode;
//sfd_tool protocol
char** str2;
char mode_str[256];
int in_quote;
char* temp;
char str1[(ARGC_MAX - 1) * ARGV_LEN];
spdio_t*& io = g_app_state.transport.io;
int ret;
int conn_wait = 30 * REOPEN_FREQ;
int keep_charge = 1, end_data = 0, blk_size = 0, skip_confirm = 1, highspeed = 0, cve_v2 = 0;
int nand_info[3];
int argcount = 0, stage = -1, nand_id = DEFAULT_NAND_ID;
unsigned exec_addr = 0, baudrate = 0;
int bootmode = -1, at = 0, async = 1;
int waitFDL1 = -1;
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

#ifdef __linux__
void check_root_permission(GtkWidgetHelper helper) {
	if (geteuid() != 0) {
		// not root
		showWarningDialog(GTK_WINDOW(helper.getWidget("main_window")), _(_(_("Warning"))), _("You are running this tool without root permission!\nIt may cause device connecting issue\nRecommanded to open this tool with root permission!\n\nsudo -E /path/to/sfd_tool"));
	}
}
#endif

bool isCrashed = false;
void crash_handler(int sig) {
	(void)sig;
	if (isCrashed) return;
	isCrashed = true;
	if (isHelperInit){
		gui_idle_call_wait_drag([]() {
			showErrorDialog(helper.getWidget("main_window") ? GTK_WINDOW(helper.getWidget("main_window")) : nullptr, _("Program Crash"), _("The program encountered an unhandled exception, which may be caused by device connection issues or a bug in the program.\n\nIt is recommended to check the device connection, ensure the correct options are used, and try running the tool again."));
		},helper.getWidget("main_window") ? GTK_WINDOW(helper.getWidget("main_window")) : nullptr);
	}
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
    // 退出
	std::thread([](){
#ifdef _WIN32
		system("pause");
#else
		sleep(5); // 5 seconds
#endif
		exit(1);
	}).detach();

}

// 全局快捷键处理：在 macOS 上支持 Command+Q，其他平台支持 Ctrl+Q 退出
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
 //fdl exec
std::string fdl1_path_json;
std::string fdl2_path_json;
uint32_t fdl1_addr_json;
uint32_t fdl2_addr_json;
int gtk_kmain(int argc, char** argv) {
	DEG_LOG(I, "Starting GUI mode...");
	gtk_init(&argc, &argv);

	g_about_text = load_about_text();

	// Initialization previously at file scope
	char* execfile = NEWN char[ARGV_LEN];
	if (!execfile) {
		ERR_EXIT("malloc failed\n");
	}
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
	// macOS: 如果通过 .app Bundle 启动，默认将备份文件保存到 ~/Documents/sfd_tool
	{
		std::string exe_dir = get_executable_dir();
#if defined(__APPLE__)
    g_is_macos_bundle = (!exe_dir.empty() && exe_dir.find(".app/Contents/MacOS") != std::string::npos);
#endif
		if (!exe_dir.empty() && exe_dir.find(".app/Contents/MacOS") != std::string::npos) {
			const char* home = std::getenv("HOME");
			if (home && *home) {
				std::string docs_dir = std::string(home) + "/Documents/sfd_tool";
				struct stat st{};
				if (stat(docs_dir.c_str(), &st) != 0) {
					// 目录不存在则尝试创建，失败时静默忽略，退回到当前工作目录策略
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

	// Window Setup
	GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), "SFD Tool GUI By Ryan Crepa");
	gtk_window_set_default_size(GTK_WINDOW(window), 1174, 765);

	// 启用键盘事件（用于快捷键）
	gtk_widget_add_events(window, GDK_KEY_PRESS_MASK);
	g_signal_connect(window, "key-press-event", G_CALLBACK(on_main_window_key_press), NULL);

	// 设置关闭信号
	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	// 创建主网格布局
	GtkWidget* mainGrid = gtk_grid_new();

	// 创建 GtkWidgetHelper
	helper = GtkWidgetHelper(window);
	isHelperInit = true;
	helper.setParent(window, LayoutType::GRID);
	helper.addWidget("main_window", window);
	initDragDetection(GTK_WINDOW(window));

	// 创建Notebook（标签页控件）
	GtkWidget* notebook = helper.createNotebook("main_notebook", 0, 0, 1174, 672);
	{
		// ========== 模块化页面创建 ==========
		create_connect_page(helper, notebook);
		create_partition_page(helper, notebook);
		create_manual_page(helper, notebook);
		create_advanced_op_page(helper, notebook);
		create_pac_flash_page(helper, notebook);
		create_advanced_set_page(helper, notebook);
		create_debug_page(helper, notebook);
		create_log_page(helper, notebook);
		create_about_page(helper, notebook);

		// ========== 底部控制栏 ==========
		GtkWidget* bottomContainer = create_bottom_controls(helper);

		// Add notebook and bottom container to main grid
		gtk_grid_attach(GTK_GRID(mainGrid), notebook, 0, 0, 10, 1);
		gtk_grid_attach(GTK_GRID(mainGrid), bottomContainer, 0, 1, 10, 1);

		// 创建CSS样式
		GtkCssProvider* provider = gtk_css_provider_new();
		const gchar* css =
		    "label.big-label { font-size: 20px; }"
		    "progressbar { min-height: 9px; }"
		    "#wait_con_no_arrow button { min-width: 0px; padding: 0px; border: none; background: transparent; -gtk-icon-source: none; color: transparent; opacity: 0; }";
		gtk_css_provider_load_from_data(provider, css, -1, NULL);
		gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
		        GTK_STYLE_PROVIDER(provider),
		        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

		gtk_container_add(GTK_CONTAINER(window), mainGrid);

		// 显示所有组件
		gtk_widget_show_all(window);

		// 强制默认选中第一个标签页（“连接”页）
		gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), 0);

		// ========== 模块化信号绑定 ==========
		bind_connect_signals(helper, argc, argv);

		// fdl_exec 需要 execfile 参数，单独绑定
		helper.bindClick(helper.getWidget("fdl_exec"), [execfile]() {
			std::thread([execfile]() {
				on_button_clicked_fdl_exec(helper, execfile);
			}).detach();
		});

		bind_partition_signals(helper);
		bind_manual_signals(helper);
		bind_advanced_op_signals(helper);
		bind_pac_flash_signals(helper);
		bind_advanced_set_signals(helper);
		bind_debug_signals(helper);
		bind_log_signals(helper);
		bind_bottom_signals(helper, bottomContainer);
	}
	DisableWidgets(helper);
	// 启动GTK主循环
	gtk_main();

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
	if (!lc_all.empty()) {
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
#ifdef __linux__
	signal(SIGKILL, crash_handler);   // 杀死进程(Linux)
	signal(SIGIOT, crash_handler);    // IOT Trap (Linux)
#endif
	signal(SIGTERM, crash_handler);   // 终止信号
	if (argc > 1 && !strcmp(argv[1], "--no-gui")) {
		// Call the console version of main
		return main_console(argc - 1, argv + 1); // Skip the first argument
	} else {
		return gtk_kmain(argc, argv);
	}
}
