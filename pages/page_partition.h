#pragma once
#include "../GtkWidgetHelper.hpp"
#include "../common.h"
#include <vector>

// 填充分区列表到 TreeView
void populatePartitionList(GtkWidgetHelper& helper, const std::vector<partition_t>& partitions);

// 获取选中的分区名称
std::string getSelectedPartitionName(GtkWidgetHelper& helper);

// 创建 Partition Operation 标签页 UI 并添加到 notebook
GtkWidget* create_partition_page(GtkWidgetHelper& helper, GtkWidget* notebook);

// 绑定 Partition Operation 页面信号
void bind_partition_signals(GtkWidgetHelper& helper);
