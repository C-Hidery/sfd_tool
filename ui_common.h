#pragma once
#include "GtkWidgetHelper.hpp"
#include <functional>
#include <atomic>

// 初始化允许控件
void Enable_Startup(GtkWidgetHelper helper);
// 启用/禁用全部控件
void EnableWidgets(GtkWidgetHelper helper);
void DisableWidgets(GtkWidgetHelper helper);

// Root 权限检查
void check_root_permission(GtkWidgetHelper helper);

// 底部控制栏
GtkWidget* create_bottom_controls(GtkWidgetHelper& helper);

// 底部控制栏信号绑定
void bind_bottom_signals(GtkWidgetHelper& helper, GtkWidget* bottomContainer);

// 统一的设备连接状态检查：未连接时弹框并退出
void ensure_device_attached_or_exit(GtkWidgetHelper helper);

// 统一的设备连接状态检查：未连接时弹框但不退出
// 返回 true 表示设备未连接（已弹框），调用方应中止后续操作；false 表示设备已连接。
bool ensure_device_attached_or_warn(GtkWidgetHelper helper);

// 日志 UI 操作：在日志标签页追加一行文本
// type 通常对应 common.h 中的 msg_type（I/W/E/OP/DE），message 为已格式化好的单行文本
void append_log_to_ui(int type, const char* message);

// 长任务执行封装：统一线程创建、取消标志与开始/结束回调
struct LongTaskConfig {
    GtkWidgetHelper& helper;

    // 后台执行的任务体，在工作线程中运行
    std::function<void(std::atomic_bool& cancel_flag)> worker;

    // 可选：在 GUI 线程中的生命周期钩子
    std::function<void()> on_started;   // 例如：禁用按钮、更新状态文本
    std::function<void()> on_finished;  // 例如：恢复按钮、重置状态
};

// 在后台启动一个长任务，并在开始/结束时在 GUI 线程执行回调
void run_long_task(const LongTaskConfig& cfg);

// 显示提示后在若干秒后退出 GTK 主循环（用于 PAC 烧录等场景）
void showExitAfterDelayDialog(GtkWindow* parent,
                              const char* title,
                              const char* message,
                              int seconds);
