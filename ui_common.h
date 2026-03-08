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
