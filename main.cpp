#include <iostream>
#include <cstring>
#include <string>
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
#ifdef __linux__
#include <unistd.h>
#include <execinfo.h>
#elif defined(_WIN32)
#include <windows.h>
#include <dbghelp.h>
#endif

std::string g_about_text;

std::string load_about_text() {
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

	// Window Setup
	GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), "SFD Tool GUI By Ryan Crepa");
	gtk_window_set_default_size(GTK_WINDOW(window), 1174, 765);

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
	setlocale(LC_ALL, "");
	bindtextdomain("sfd_tool", "./locale");
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
