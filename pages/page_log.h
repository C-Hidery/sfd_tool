#pragma once
#include "../GtkWidgetHelper.hpp"
#include "ui/ui_page.h"

// Log 页面实现 IPage 接口
class LogPage : public IPage {
public:
    GtkWidget* init(GtkWidgetHelper& helper, GtkWidget* notebook) override;
    void bindSignals(GtkWidgetHelper& helper) override;
};

// 保持原有接口
GtkWidget* create_log_page(GtkWidgetHelper& helper, GtkWidget* notebook);
void bind_log_signals(GtkWidgetHelper& helper);
