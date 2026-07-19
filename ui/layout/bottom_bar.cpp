/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#include "bottom_bar.h"
#include "ui/GtkWidgetHelper.hpp"
#include "common.h"
#include "i18n.h"

extern GtkWidgetHelper helper;

namespace {
    GtkWidget* s_bottom_bar_root        = nullptr;
    GtkWidget* s_status_value_label     = nullptr;
    GtkWidget* s_mode_value_label       = nullptr;
    GtkWidget* s_storage_value_label    = nullptr;
    GtkWidget* s_slot_value_label       = nullptr;
    GtkWidget* s_progress_bar           = nullptr;
    GtkWidget* s_percent_label          = nullptr;
}

GtkWidget* bottom_bar_create(GtkWidgetHelper& helper_ref) {
    (void)helper_ref; // 当前实现直接使用全局 helper，保持风格一致

    if (s_bottom_bar_root) {
        return s_bottom_bar_root;
    }

    // 外层垂直 Box：顶部按钮 + 分隔线/长进度条 + 底部状态栏
    GtkWidget* bottomContainer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(bottomContainer, 15);
    gtk_widget_set_margin_end(bottomContainer, 15);
    gtk_widget_set_margin_top(bottomContainer, 10);
    gtk_widget_set_margin_bottom(bottomContainer, 10);

    // 顶部按钮区域 (水平居中)
    GtkWidget* topActionBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(topActionBox, TRUE); // 容器横向扩展

    // 左侧 spacer（推动按钮居中）
    GtkWidget* cSpacer1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(cSpacer1, TRUE);
    gtk_widget_set_halign(cSpacer1, GTK_ALIGN_FILL);

    // 按钮水平容器
    GtkWidget* buttonsHBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(buttonsHBox, GTK_ALIGN_CENTER);

    // POWEROFF 按钮
    GtkWidget* poweroffBtn = gtk_button_new_with_label(_("POWEROFF"));
    gtk_widget_set_name(poweroffBtn, "poweroff");
    gtk_widget_set_size_request(poweroffBtn, 130, 32);
    helper.addWidget("poweroff", poweroffBtn);

    // REBOOT 按钮
    GtkWidget* rebootBtn = gtk_button_new_with_label(_("REBOOT"));
    gtk_widget_set_name(rebootBtn, "reboot");
    gtk_widget_set_size_request(rebootBtn, 110, 32);
    helper.addWidget("reboot", rebootBtn);

    // BOOT TO RECOVERY 按钮
    GtkWidget* recoveryBtn = gtk_button_new_with_label(_("BOOT TO RECOVERY"));
    gtk_widget_set_name(recoveryBtn, "recovery");
    gtk_widget_set_size_request(recoveryBtn, 180, 32);
    helper.addWidget("recovery", recoveryBtn);

    // BOOT TO FASTBOOT 按钮
    GtkWidget* fastbootBtn = gtk_button_new_with_label(_("BOOT TO FASTBOOT"));
    gtk_widget_set_name(fastbootBtn, "fastboot");
    gtk_widget_set_size_request(fastbootBtn, 180, 32);
    helper.addWidget("fastboot", fastbootBtn);

    gtk_box_append(GTK_BOX(buttonsHBox), poweroffBtn);
    gtk_box_append(GTK_BOX(buttonsHBox), rebootBtn);
    gtk_box_append(GTK_BOX(buttonsHBox), recoveryBtn);
    gtk_box_append(GTK_BOX(buttonsHBox), fastbootBtn);

    // 右侧 spacer（推动按钮居中）
    GtkWidget* cSpacer2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(cSpacer2, TRUE);
    gtk_widget_set_halign(cSpacer2, GTK_ALIGN_FILL);

    // 组装 topActionBox: spacer1 + buttonsHBox + spacer2
    gtk_box_append(GTK_BOX(topActionBox), cSpacer1);
    gtk_box_append(GTK_BOX(topActionBox), buttonsHBox);
    gtk_box_append(GTK_BOX(topActionBox), cSpacer2);

    // 中部进度条区域：分割线 + 长进度条
    GtkWidget* midProgressBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);

    GtkWidget* statSeparatorTop = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(midProgressBox), statSeparatorTop);

    GtkWidget* progressBar = gtk_progress_bar_new();
    gtk_widget_set_name(progressBar, "progressBar_1");
    helper.addWidget("progressBar_1", progressBar);
    gtk_widget_set_hexpand(progressBar, TRUE);
    gtk_widget_set_size_request(progressBar, -1, 4);
    gtk_widget_set_margin_top(progressBar, 5);
    gtk_box_append(GTK_BOX(midProgressBox), progressBar);

    s_progress_bar = progressBar;

    // 底部状态栏：左 Status，右 Mode/Storage/Slot/Progress
    GtkWidget* bottomStatusBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);

    // Status 区域："Status:" + con 文案
    GtkWidget* statusLabel = gtk_label_new(_("Status: "));
    gtk_widget_set_name(statusLabel, "status_label");
    gtk_widget_set_size_request(statusLabel, -1, 24);
    helper.addWidget("status_label", statusLabel);

    GtkWidget* conStatus = gtk_label_new(_("Not connected"));
    gtk_widget_set_name(conStatus, "con");
    gtk_widget_set_size_request(conStatus, -1, 23);
    helper.addWidget("con", conStatus);

    GtkWidget* stBoxLabel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_label_set_xalign(GTK_LABEL(statusLabel), 0.0);
    gtk_label_set_xalign(GTK_LABEL(conStatus), 0.0);
    gtk_box_append(GTK_BOX(stBoxLabel), statusLabel);
    gtk_box_append(GTK_BOX(stBoxLabel), conStatus);

    s_status_value_label = conStatus;

    // Mode 区域
    GtkWidget* modeLabel = gtk_label_new(_("Mode: "));
    gtk_widget_set_name(modeLabel, "mode_label");
    gtk_widget_set_size_request(modeLabel, -1, 19);
    helper.addWidget("mode_label", modeLabel);

    GtkWidget* modeStatus = gtk_label_new(_("BROM Not connected!!!"));
    gtk_widget_set_name(modeStatus, "mode");
    gtk_widget_set_size_request(modeStatus, 140, 19);
    helper.addWidget("mode", modeStatus);

    GtkWidget* mdBoxLabel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_label_set_xalign(GTK_LABEL(modeLabel), 0.0);
    gtk_label_set_xalign(GTK_LABEL(modeStatus), 0.0);
    gtk_box_append(GTK_BOX(mdBoxLabel), modeLabel);
    gtk_box_append(GTK_BOX(mdBoxLabel), modeStatus);

    s_mode_value_label = modeStatus;

    // Storage 区域
    GtkWidget* stgBoxLabel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget* storageLabel = gtk_label_new("Storage:");
    gtk_widget_set_name(storageLabel, "storage_label");
    gtk_widget_set_size_request(storageLabel, -1, 20);
    helper.addWidget("storage_label", storageLabel);

    GtkWidget* storageMode = gtk_label_new("Unknown");
    gtk_widget_set_name(storageMode, "storage_mode");
    gtk_widget_set_size_request(storageMode, 120, 20);
    helper.addWidget("storage_mode", storageMode);

    gtk_label_set_xalign(GTK_LABEL(storageLabel), 0.0);
    gtk_label_set_xalign(GTK_LABEL(storageMode), 0.0);
    gtk_box_append(GTK_BOX(stgBoxLabel), storageLabel);
    gtk_box_append(GTK_BOX(stgBoxLabel), storageMode);

    s_storage_value_label = storageMode;

    // Slot 区域
    GtkWidget* sltBoxLabel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget* slotLabel = gtk_label_new("Slot:");
    gtk_widget_set_name(slotLabel, "slot_label");
    gtk_widget_set_size_request(slotLabel, -1, 20);
    helper.addWidget("slot_label", slotLabel);

    GtkWidget* slotMode = gtk_label_new("Unknown");
    gtk_widget_set_name(slotMode, "slot_mode");
    gtk_widget_set_size_request(slotMode, 120, 20);
    helper.addWidget("slot_mode", slotMode);

    gtk_label_set_xalign(GTK_LABEL(slotLabel), 0.0);
    gtk_label_set_xalign(GTK_LABEL(slotMode), 0.0);
    gtk_box_append(GTK_BOX(sltBoxLabel), slotLabel);
    gtk_box_append(GTK_BOX(sltBoxLabel), slotMode);

    s_slot_value_label = slotMode;

    // 右侧 Progress 文本区域
    GtkWidget* prgTextHBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget* progressLabel = gtk_label_new(_("Progress:"));
    gtk_widget_set_name(progressLabel, "progress_label");
    gtk_widget_set_size_request(progressLabel, -1, 20);
    helper.addWidget("progress_label", progressLabel);

    GtkWidget* percentLabel = gtk_label_new("0%");
    gtk_widget_set_name(percentLabel, "percent");
    gtk_widget_set_size_request(percentLabel, 40, 20);
    helper.addWidget("percent", percentLabel);

    gtk_label_set_xalign(GTK_LABEL(progressLabel), 1.0);
    gtk_label_set_xalign(GTK_LABEL(percentLabel), 1.0);
    gtk_box_append(GTK_BOX(prgTextHBox), progressLabel);
    gtk_box_append(GTK_BOX(prgTextHBox), percentLabel);

    s_percent_label = percentLabel;

    // 左侧状态
    gtk_box_append(GTK_BOX(bottomStatusBox), stBoxLabel);

    // 右侧整体区域：Mode + Storage + Slot + Progress
    GtkWidget* rightGroup = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_box_append(GTK_BOX(rightGroup), mdBoxLabel);
    gtk_box_append(GTK_BOX(rightGroup), stgBoxLabel);
    gtk_box_append(GTK_BOX(rightGroup), sltBoxLabel);
    gtk_box_append(GTK_BOX(rightGroup), prgTextHBox);

    // 中间弹性空白，保证右侧整体一起移动
    GtkWidget* stSpacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(stSpacer, TRUE);
    gtk_box_append(GTK_BOX(bottomStatusBox), stSpacer);
    gtk_box_append(GTK_BOX(bottomStatusBox), rightGroup);

    // 组装三层
    gtk_box_append(GTK_BOX(bottomContainer), topActionBox);
    gtk_box_append(GTK_BOX(bottomContainer), midProgressBox);
    gtk_box_append(GTK_BOX(bottomContainer), bottomStatusBox);

    s_bottom_bar_root = bottomContainer;
    return s_bottom_bar_root;
}

GtkWidget* bottom_bar_get_root() {
    return s_bottom_bar_root;
}

void bottom_bar_set_status(const std::string& text) {
    if (!s_status_value_label) return;
    gtk_label_set_text(GTK_LABEL(s_status_value_label), text.c_str());
}

void bottom_bar_set_mode(const std::string& text) {
    if (!s_mode_value_label) return;
    gtk_label_set_text(GTK_LABEL(s_mode_value_label), text.c_str());
}

void bottom_bar_set_storage(const std::string& text) {
    if (!s_storage_value_label) return;
    gtk_label_set_text(GTK_LABEL(s_storage_value_label), text.c_str());
}

void bottom_bar_set_slot(const std::string& text) {
    if (!s_slot_value_label) return;
    gtk_label_set_text(GTK_LABEL(s_slot_value_label), text.c_str());
}

void bottom_bar_set_progress(double fraction, const std::string& percent_text) {
    if (!s_progress_bar || !s_percent_label) return;

    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;

    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(s_progress_bar), fraction);
    gtk_label_set_text(GTK_LABEL(s_percent_label), percent_text.c_str());
}

void bottom_bar_set_io_status(const std::string& text) {
    if (!s_status_value_label) return;
    gtk_label_set_text(GTK_LABEL(s_status_value_label), text.c_str());
}