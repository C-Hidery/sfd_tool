#pragma once
#include "../GtkWidgetHelper.hpp"
#include "../common.h"

// 创建 PAC Flash 标签页 UI 并添加到 notebook
GtkWidget* create_pac_flash_page(GtkWidgetHelper& helper, GtkWidget* notebook);

// 绑定 PAC Flash 页面信号
void bind_pac_flash_signals(GtkWidgetHelper& helper);
