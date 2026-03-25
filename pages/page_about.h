#pragma once
#include "ui/GtkWidgetHelper.hpp"
#include "ui/ui_page.h"

// About 页面实现 IPage 接口
class AboutPage : public IPage {
public:
    GtkWidget* init(GtkWidgetHelper& helper, GtkWidget* notebook) override;
    void bindSignals(GtkWidgetHelper& helper) override;
};

// 保持原有 C 风格接口，内部转调 AboutPage
GtkWidget* create_about_page(GtkWidgetHelper& helper, GtkWidget* notebook);

