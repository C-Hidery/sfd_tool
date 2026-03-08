#pragma once
#include "../GtkWidgetHelper.hpp"

// 创建 Debug Options 标签页 UI 并添加到 notebook
GtkWidget* create_debug_page(GtkWidgetHelper& helper, GtkWidget* notebook);

// 绑定 Debug 页面信号
void bind_debug_signals(GtkWidgetHelper& helper);
