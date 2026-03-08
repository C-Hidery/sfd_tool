#pragma once
#include "../GtkWidgetHelper.hpp"

// 创建 Advanced Operation 标签页 UI 并添加到 notebook
GtkWidget* create_advanced_op_page(GtkWidgetHelper& helper, GtkWidget* notebook);

// 绑定 Advanced Operation 页面信号
void bind_advanced_op_signals(GtkWidgetHelper& helper);
