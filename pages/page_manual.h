#pragma once
#include "../GtkWidgetHelper.hpp"
#include "../ui_page.h"

// Manual 页面实现 IPage 接口
class ManualPage : public IPage {
public:
    GtkWidget* init(GtkWidgetHelper& helper, GtkWidget* notebook) override;
    void bindSignals(GtkWidgetHelper& helper) override;
};

// 保持原有 C 风格接口
GtkWidget* create_manual_page(GtkWidgetHelper& helper, GtkWidget* notebook);

// 绑定 Manual 页面信号
void bind_manual_signals(GtkWidgetHelper& helper);
