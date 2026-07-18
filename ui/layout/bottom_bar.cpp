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
    (void)helper_ref; // 使用全局 helper

    if (s_bottom_bar_root) {
        return s_bottom_bar_root;
    }

    // 外层垂直 Box
    GtkWidget* bottomContainer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(bottomContainer, 15);
    gtk_widget_set_margin_end(bottomContainer, 15);
    gtk_widget_set_margin_top(bottomContainer, 10);
    gtk_widget_set_margin_bottom(bottomContainer, 10);

    // 顶部按钮区域
    GtkWidget* topActionBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget* buttonsHBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);

    // 创建按钮（原生创建，手动添加到 buttonsHBox）
    GtkWidget* poweroffBtn = gtk_button_new_with_label(_("POWEROFF"));
    gtk_widget_set_size_request(poweroffBtn, 130, 32);
    helper.addWidget("poweroff", poweroffBtn, "button");

    GtkWidget* rebootBtn = gtk_button_new_with_label(_("REBOOT"));
    gtk_widget_set_size_request(rebootBtn, 110, 32);
    helper.addWidget("reboot", rebootBtn, "button");

    GtkWidget* recoveryBtn = gtk_button_new_with_label(_("BOOT TO RECOVERY"));
    gtk_widget_set_size_request(recoveryBtn, 180, 32);
    helper.addWidget("recovery", recoveryBtn, "button");

    GtkWidget* fastbootBtn = gtk_button_new_with_label(_("BOOT TO FASTBOOT"));
    gtk_widget_set_size_request(fastbootBtn, 180, 32);
    helper.addWidget("fastboot", fastbootBtn, "button");

    gtkBoxPackStart(buttonsHBox, poweroffBtn, FALSE, FALSE, 0);
    gtkBoxPackStart(buttonsHBox, rebootBtn, FALSE, FALSE, 0);
    gtkBoxPackStart(buttonsHBox, recoveryBtn, FALSE, FALSE, 0);
    gtkBoxPackStart(buttonsHBox, fastbootBtn, FALSE, FALSE, 0);

    GtkWidget* cSpacer1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget* cSpacer2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtkBoxPackStart(topActionBox, cSpacer1, TRUE, TRUE, 0);
    gtkBoxPackStart(topActionBox, buttonsHBox, FALSE, FALSE, 0);
    gtkBoxPackStart(topActionBox, cSpacer2, TRUE, TRUE, 0);

    // 中部进度条区域
    GtkWidget* midProgressBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);

    GtkWidget* statSeparatorTop = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtkBoxPackStart(midProgressBox, statSeparatorTop, FALSE, FALSE, 0);

    GtkWidget* progressBar = gtk_progress_bar_new();
    gtk_widget_set_hexpand(progressBar, TRUE);
    gtk_widget_set_size_request(progressBar, -1, 4);
    gtk_widget_set_margin_top(progressBar, 5);
    helper.addWidget("progressBar_1", progressBar, "progressbar");
    gtkBoxPackStart(midProgressBox, progressBar, FALSE, FALSE, 0);
    s_progress_bar = progressBar;

    // 底部状态栏
    GtkWidget* bottomStatusBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);

    // Status 区域
    GtkWidget* statusLabel = gtk_label_new(_("Status: "));
    helper.addWidget("status_label", statusLabel, "label");
    GtkWidget* conStatus = gtk_label_new(_("Not connected"));
    helper.addWidget("con", conStatus, "label");

    GtkWidget* stBoxLabel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_label_set_xalign(GTK_LABEL(statusLabel), 0.0);
    gtk_label_set_xalign(GTK_LABEL(conStatus), 0.0);
    gtkBoxPackStart(stBoxLabel, statusLabel, FALSE, FALSE, 0);
    gtkBoxPackStart(stBoxLabel, conStatus, FALSE, FALSE, 0);
    s_status_value_label = conStatus;

    // Mode 区域
    GtkWidget* modeLabel = gtk_label_new(_("Mode: "));
    helper.addWidget("mode_label", modeLabel, "label");
    GtkWidget* modeStatus = gtk_label_new(_("BROM Not connected!!!"));
    helper.addWidget("mode", modeStatus, "label");

    GtkWidget* mdBoxLabel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_label_set_xalign(GTK_LABEL(modeLabel), 0.0);
    gtk_label_set_xalign(GTK_LABEL(modeStatus), 0.0);
    gtkBoxPackStart(mdBoxLabel, modeLabel, FALSE, FALSE, 0);
    gtkBoxPackStart(mdBoxLabel, modeStatus, FALSE, FALSE, 0);
    s_mode_value_label = modeStatus;

    // Storage 区域
    GtkWidget* storageLabel = gtk_label_new("Storage:");
    helper.addWidget("storage_label", storageLabel, "label");
    GtkWidget* storageMode = gtk_label_new("Unknown");
    helper.addWidget("storage_mode", storageMode, "label");

    GtkWidget* stgBoxLabel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_label_set_xalign(GTK_LABEL(storageLabel), 0.0);
    gtk_label_set_xalign(GTK_LABEL(storageMode), 0.0);
    gtkBoxPackStart(stgBoxLabel, storageLabel, FALSE, FALSE, 0);
    gtkBoxPackStart(stgBoxLabel, storageMode, FALSE, FALSE, 0);
    s_storage_value_label = storageMode;

    // Slot 区域
    GtkWidget* slotLabel = gtk_label_new("Slot:");
    helper.addWidget("slot_label", slotLabel, "label");
    GtkWidget* slotMode = gtk_label_new("Unknown");
    helper.addWidget("slot_mode", slotMode, "label");

    GtkWidget* sltBoxLabel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_label_set_xalign(GTK_LABEL(slotLabel), 0.0);
    gtk_label_set_xalign(GTK_LABEL(slotMode), 0.0);
    gtkBoxPackStart(sltBoxLabel, slotLabel, FALSE, FALSE, 0);
    gtkBoxPackStart(sltBoxLabel, slotMode, FALSE, FALSE, 0);
    s_slot_value_label = slotMode;

    // 右侧 Progress 文本区域
    GtkWidget* progressLabel = gtk_label_new(_("Progress:"));
    helper.addWidget("progress_label", progressLabel, "label");
    GtkWidget* percentLabel = gtk_label_new("0%");
    helper.addWidget("percent", percentLabel, "label");

    GtkWidget* prgTextHBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_label_set_xalign(GTK_LABEL(progressLabel), 1.0);
    gtk_label_set_xalign(GTK_LABEL(percentLabel), 1.0);
    gtkBoxPackStart(prgTextHBox, progressLabel, FALSE, FALSE, 0);
    gtkBoxPackStart(prgTextHBox, percentLabel, FALSE, FALSE, 0);
    s_percent_label = percentLabel;

    // 左侧状态
    gtkBoxPackStart(bottomStatusBox, stBoxLabel, FALSE, FALSE, 0);

    // 右侧整体区域
    GtkWidget* rightGroup = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtkBoxPackStart(rightGroup, mdBoxLabel, FALSE, FALSE, 0);
    gtkBoxPackStart(rightGroup, stgBoxLabel, FALSE, FALSE, 0);
    gtkBoxPackStart(rightGroup, sltBoxLabel, FALSE, FALSE, 0);
    gtkBoxPackStart(rightGroup, prgTextHBox, FALSE, FALSE, 0);

    GtkWidget* stSpacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtkBoxPackStart(bottomStatusBox, stSpacer, TRUE, TRUE, 0);
    gtkBoxPackStart(bottomStatusBox, rightGroup, FALSE, FALSE, 0);

    // 组装三层
    gtkBoxPackStart(bottomContainer, topActionBox, FALSE, FALSE, 0);
    gtkBoxPackStart(bottomContainer, midProgressBox, FALSE, FALSE, 0);
    gtkBoxPackStart(bottomContainer, bottomStatusBox, FALSE, FALSE, 0);

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