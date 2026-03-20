#pragma once
#include "../GtkWidgetHelper.hpp"
#include "../common.h"
#include "../core/flash_service.h"
#include "../ui_page.h"
#include <vector>

// Partition 页面实现 IPage 接口，同时对外保留原有辅助函数
class PartitionPage : public IPage {
public:
    GtkWidget* init(GtkWidgetHelper& helper, GtkWidget* notebook) override;
    void bindSignals(GtkWidgetHelper& helper) override;
};

// 填充分区列表到 TreeView（使用 Service 层的 DevicePartitionInfo）
void populatePartitionList(GtkWidgetHelper& helper, const std::vector<sfd::DevicePartitionInfo>& partitions);

// 获取选中的分区名称
std::string getSelectedPartitionName(GtkWidgetHelper& helper);

// 分区大小刷新
void update_partition_size(spdio_t* io);

// 兼容模式分区表确认
void confirm_partition_c(GtkWidgetHelper helper);

// 创建 Partition Operation 标签页 UI 并添加到 notebook
GtkWidget* create_partition_page(GtkWidgetHelper& helper, GtkWidget* notebook);

// 绑定 Partition Operation 页面信号
void bind_partition_signals(GtkWidgetHelper& helper);

// 从文件夹批量刷入分区入口
void on_button_clicked_restore_from_folder(GtkWidgetHelper helper);
