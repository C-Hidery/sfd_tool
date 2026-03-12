#pragma once
#include "GtkWidgetHelper.hpp"

// 启用/禁用全部控件
void EnableWidgets(GtkWidgetHelper helper);
void DisableWidgets(GtkWidgetHelper helper);

// Root 权限检查
void check_root_permission(GtkWidgetHelper helper);

// 底部控制栏
GtkWidget* create_bottom_controls(GtkWidgetHelper& helper);

// 底部控制栏信号绑定
void bind_bottom_signals(GtkWidgetHelper& helper, GtkWidget* bottomContainer);

// 日志 UI 操作：在日志标签页追加一行文本
// type 通常对应 common.h 中的 msg_type（I/W/E/OP/DE），message 为已格式化好的单行文本
void append_log_to_ui(int type, const char* message);
