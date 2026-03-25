#pragma once

#include <string>
#include <gtk/gtk.h>

class GtkWidgetHelper;

// 创建底部状态栏，并返回其根容器（只创建一次，多次调用返回同一实例）
GtkWidget* bottom_bar_create(GtkWidgetHelper& helper);

// 可选：获取已创建的底部状态栏根控件，未创建时返回 nullptr
GtkWidget* bottom_bar_get_root();

// 左侧 Status 区域
void bottom_bar_set_status(const std::string& text);

// Mode 区域（BROM/FDL1/FDL2 等）
void bottom_bar_set_mode(const std::string& text);

// Storage 区域（Emmc/Ufs/Nand 等）
void bottom_bar_set_storage(const std::string& text);

// Slot 区域（Slot A/B/Not VAB）
void bottom_bar_set_slot(const std::string& text);

// Progress 进度条与百分比
// fraction: [0.0, 1.0]，会在实现中进行 clamp
void bottom_bar_set_progress(double fraction, const std::string& percent_text);

// 读写速率等 IO 状态文本，例如 "read: XX MB | YY MB/s"
void bottom_bar_set_io_status(const std::string& text);
