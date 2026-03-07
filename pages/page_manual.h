#pragma once
#include "../GtkWidgetHelper.hpp"

// 创建 Manually Operate 标签页 UI 并添加到 notebook
GtkWidget* create_manual_page(GtkWidgetHelper& helper, GtkWidget* notebook);

// 绑定 Manual 页面信号
void bind_manual_signals(GtkWidgetHelper& helper);
