#pragma once
#include "ui/GtkWidgetHelper.hpp"

// 创建 Connect 标签页 UI 并添加到 notebook
// 返回 connectPage 中创建的状态标签用于底部控制栏
GtkWidget* create_connect_page(GtkWidgetHelper& helper, GtkWidget* notebook);

// 绑定 Connect 页面信号
void bind_connect_signals(GtkWidgetHelper& helper, int argc, char** argv);

// 连接按钮回调
void on_button_clicked_connect(GtkWidgetHelper helper, int argc, char** argv);

// FDL 执行回调
void on_button_clicked_fdl_exec(GtkWidgetHelper helper, char* execfile);
