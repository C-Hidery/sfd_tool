#pragma once
#include "../GtkWidgetHelper.hpp"
#include "../ui_page.h"

// Advanced Settings 页面实现 IPage 接口
class AdvancedSetPage : public IPage {
public:
    GtkWidget* init(GtkWidgetHelper& helper, GtkWidget* notebook) override;
    void bindSignals(GtkWidgetHelper& helper) override;
};

// 保持原有接口
GtkWidget* create_advanced_set_page(GtkWidgetHelper& helper, GtkWidget* notebook);

// 绑定 Advanced Settings 页面信号
void bind_advanced_set_signals(GtkWidgetHelper& helper);
