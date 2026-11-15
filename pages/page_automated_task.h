/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#pragma once

#include "ui/GtkWidgetHelper.hpp"
#include "ui/ui_page.h"

#include <string>
#include <vector>

// 包元数据。未知字段统一使用 "Unknown"，与页面初始状态保持一致。
struct AutomatedTaskPackageInfo {
    std::string name = "Unknown";
    std::string author = "Unknown";
    std::string version = "Unknown";
    std::string contact = "Unknown";
    std::string type = "Unknown";
};

// 左侧分区行：显示名（序号 + 分区名）、大小、原始分区名/类型、设备实际大小。
struct AutomatedTaskPartitionRow {
    std::string name;
    std::string size;
    std::string raw_name;
    std::string device_size;
};

// 任务选择行。
struct AutomatedTaskItem {
    std::string name;
    bool selected = false;
};

// Automated task 页面实现统一的 IPage 生命周期接口。
class AutomatedTaskPage : public IPage {
public:
    GtkWidget* init(GtkWidgetHelper& helper, GtkWidget* notebook) override;
    void bindSignals(GtkWidgetHelper& helper) override;
};

// 保持与其它页面一致的 C 风格创建/信号接口。
GtkWidget* create_automated_task_page(GtkWidgetHelper& helper, GtkWidget* notebook);
void bind_automated_task_signals(GtkWidgetHelper& helper);

// ── 页面数据接口 ──────────────────────────────────────────────
// 供包解析/任务服务在加载完成后填充页面，避免 UI 直接依赖具体包格式。
void automated_task_set_package_info(GtkWidgetHelper& helper,
                                     const AutomatedTaskPackageInfo& info);
void automated_task_clear_package_info(GtkWidgetHelper& helper);

void automated_task_set_partitions(GtkWidgetHelper& helper,
                                   const std::vector<AutomatedTaskPartitionRow>& rows);

void automated_task_set_tasks(GtkWidgetHelper& helper,
                              const std::vector<AutomatedTaskItem>& tasks);

// 返回当前勾选的任务名。
std::vector<std::string> automated_task_selected_tasks(GtkWidgetHelper& helper);
