#pragma once
#include "ui/GtkWidgetHelper.hpp"
#include "../common.h"
#include "ui/ui_page.h"

// PAC Flash 页面，属于操作类页面
class PacFlashPage : public IPage {
public:
    GtkWidget* init(GtkWidgetHelper& helper, GtkWidget* notebook) override;
    void bindSignals(GtkWidgetHelper& helper) override;
};

// 保持原有接口
GtkWidget* create_pac_flash_page(GtkWidgetHelper& helper, GtkWidget* notebook);
void bind_pac_flash_signals(GtkWidgetHelper& helper);
