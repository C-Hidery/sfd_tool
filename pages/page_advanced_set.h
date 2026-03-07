#pragma once
#include "../GtkWidgetHelper.hpp"

// 创建 Advanced Settings 标签页 UI 并添加到 notebook
GtkWidget* create_advanced_set_page(GtkWidgetHelper& helper, GtkWidget* notebook);

// 绑定 Advanced Settings 页面信号
void bind_advanced_set_signals(GtkWidgetHelper& helper);
