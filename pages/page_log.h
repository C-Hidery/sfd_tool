#pragma once
#include "../GtkWidgetHelper.hpp"

// 创建 Log 标签页 UI 并添加到 notebook
GtkWidget* create_log_page(GtkWidgetHelper& helper, GtkWidget* notebook);

// 绑定 Log 页面信号
void bind_log_signals(GtkWidgetHelper& helper);
