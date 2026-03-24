#include <stdint.h>
#include "app_state.h"
#include "common.h"
#include "ui_common.h"

// 前向声明，仅用于定义引用/指针，不需要完整类型
struct spdio_t;

// 为测试提供的最小全局变量实现，避免依赖 GUI/main 逻辑
// 这些符号在正常应用中由 main.cpp/common.cpp/ui_common.cpp 提供，这里只给出安全的空壳/默认值。

AppState g_app_state{};        // 全局应用状态实例（测试中不会真正使用设备）
int fdl1_loaded = 0;           // FDL1 加载状态，占位
int fdl2_executed = 0;         // FDL2 执行状态，占位
uint64_t g_spl_size = 0;       // SPL 大小，占位
uint64_t fblk_size = 0;        // dump 时限速用的块大小，占位
int blk_size = 0;              // GUI/控制台共用块大小，占位
int g_default_blk_size = 0;    // 默认块大小，占位

// io/m_bOpened 在 UI 和协议层中以引用形式存在，这里提供测试专用的后备存储
static spdio_t* g_test_io = nullptr;
spdio_t*& io = g_test_io;

static int g_test_m_bOpened = -1;
int& m_bOpened = g_test_m_bOpened;

// ===== 针对 core/logging.cpp & pac_extract.cpp 的 GUI 相关桩实现 =====

// 在测试环境中不弹出对话框，只打印到 stderr，避免依赖 GTK/窗口
// 同时避免与正式实现重复定义，只引入声明，具体实现在 common.cpp / GtkWidgetHelper.cpp / ui_common.cpp

extern bool Err_Showed;
extern bool isHelperInit;
extern GtkWidgetHelper helper;

void append_log_to_ui(int type, const char* message);

std::string showFileChooser(GtkWindow* parent, bool open);
std::string showFolderChooser(GtkWindow* parent);
void showInfoDialog(GtkWindow* parent, const char* title, const char* message);
void showWarningDialog(GtkWindow* parent, const char* title, const char* message);
void showErrorDialog(GtkWindow* parent, const char* title, const char* message);
bool showConfirmDialog(GtkWindow* parent, const char* title, const char* message);
std::string showSaveFileDialog(GtkWindow* parent,
                               const std::string& default_filename,
                               const std::vector<std::pair<std::string, std::string>>& filters);

bool isWindowDragging(GtkWindow* window);
void initDragDetection(GtkWindow* window);
