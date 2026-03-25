#pragma once
#include "GtkWidgetHelper.hpp"

// 统一的页面接口：封装页面创建与信号绑定两个生命周期
class IPage {
public:
    virtual ~IPage() = default;

    // 初始化页面 UI，将页面挂入 notebook 并返回页面根控件
    virtual GtkWidget* init(GtkWidgetHelper& helper, GtkWidget* notebook) = 0;

    // 绑定页面相关信号。对于纯展示页面可以为空实现
    virtual void bindSignals(GtkWidgetHelper& helper) = 0;
};
