#pragma once
#include "ui/GtkWidgetHelper.hpp"
#include "ui/ui_page.h"

// Debug 页面实现 IPage 接口
class DebugPage : public IPage {
public:
    GtkWidget* init(GtkWidgetHelper& helper, GtkWidget* notebook) override;
    void bindSignals(GtkWidgetHelper& helper) override;
};

// 保持原有 C 风格接口，内部转调 DebugPage
GtkWidget* create_debug_page(GtkWidgetHelper& helper, GtkWidget* notebook);

// 绑定 Debug 页面信号
void bind_debug_signals(GtkWidgetHelper& helper);
