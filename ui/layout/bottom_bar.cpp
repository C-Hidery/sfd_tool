#include "bottom_bar.h"
#include "ui/GtkWidgetHelper.hpp"
#include "common.h"
#include "i18n.h"

// 全局 GtkWidgetHelper 由 common.cpp 提供
extern GtkWidgetHelper helper;

namespace {
    GtkWidget* s_bottom_bar_root        = nullptr; // 整个底部栏根容器

    GtkWidget* s_status_value_label     = nullptr; // "con"
    GtkWidget* s_mode_value_label       = nullptr; // "mode"
    GtkWidget* s_storage_value_label    = nullptr; // "storage_mode"
    GtkWidget* s_slot_value_label       = nullptr; // "slot_mode"

    GtkWidget* s_progress_bar           = nullptr; // "progressBar_1"
    GtkWidget* s_percent_label          = nullptr; // "percent"
    GtkWidget* s_io_status_label        = nullptr; // 复用 "con" 文案，不额外暴露 IO label
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

    // 顶部按钮区域
    GtkWidget* topActionBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget* buttonsHBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);

    GtkWidget* poweroffBtn = helper.createButton(_("POWEROFF"), "poweroff", nullptr, 0, 0, 130, 32);
    GtkWidget* rebootBtn = helper.createButton(_("REBOOT"), "reboot", nullptr, 0, 0, 110, 32);
    GtkWidget* recoveryBtn = helper.createButton(_("BOOT TO RECOVERY"), "recovery", nullptr, 0, 0, 180, 32);
    GtkWidget* fastbootBtn = helper.createButton(_("BOOT TO FASTBOOT"), "fastboot", nullptr, 0, 0, 180, 32);

    gtk_box_pack_start(GTK_BOX(buttonsHBox), poweroffBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttonsHBox), rebootBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttonsHBox), recoveryBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttonsHBox), fastbootBtn, FALSE, FALSE, 0);

    GtkWidget* cSpacer1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget* cSpacer2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(topActionBox), cSpacer1, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(topActionBox), buttonsHBox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(topActionBox), cSpacer2, TRUE, TRUE, 0);

    // 中部进度条区域：分割线 + 长进度条
    GtkWidget* midProgressBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);

    GtkWidget* statSeparatorTop = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(midProgressBox), statSeparatorTop, FALSE, FALSE, 0);

    GtkWidget* progressBar = gtk_progress_bar_new();
    gtk_widget_set_name(progressBar, "progressBar_1");
    helper.addWidget("progressBar_1", progressBar);
    gtk_widget_set_hexpand(progressBar, TRUE);
    gtk_widget_set_size_request(progressBar, -1, 4);
    gtk_widget_set_margin_top(progressBar, 5);
    gtk_box_pack_start(GTK_BOX(midProgressBox), progressBar, FALSE, FALSE, 0);

    s_progress_bar = progressBar;

    // 底部状态栏：左 Status，右 Mode/Storage/Slot/Progress
    GtkWidget* bottomStatusBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);

    // Status 区域："Status:" + con 文案
    GtkWidget* statusLabel = helper.createLabel(_("Status: "), "status_label", 0, 0, -1, 24);
    GtkWidget* conStatus = helper.createLabel(_("Not connected"), "con", 0, 0, -1, 23);

    GtkWidget* stBoxLabel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_label_set_xalign(GTK_LABEL(statusLabel), 0.0);
    gtk_label_set_xalign(GTK_LABEL(conStatus), 0.0);
    gtk_box_pack_start(GTK_BOX(stBoxLabel), statusLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(stBoxLabel), conStatus, FALSE, FALSE, 0);

    s_status_value_label = conStatus;

    // Mode 区域
    GtkWidget* modeLabel = helper.createLabel(_("Mode: "), "mode_label", 0, 0, -1, 19);
    GtkWidget* modeStatus = helper.createLabel(_("BROM Not connected!!!"), "mode", 0, 0, 140, 19);

    GtkWidget* mdBoxLabel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_label_set_xalign(GTK_LABEL(modeLabel), 0.0);
    gtk_label_set_xalign(GTK_LABEL(modeStatus), 0.0);
    gtk_box_pack_start(GTK_BOX(mdBoxLabel), modeLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(mdBoxLabel), modeStatus, FALSE, FALSE, 0);

    s_mode_value_label = modeStatus;

    // Storage 区域
    GtkWidget* stgBoxLabel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget* storageLabel = helper.createLabel("Storage:", "storage_label", 0, 0, -1, 20);
    GtkWidget* storageMode = helper.createLabel("Unknown", "storage_mode", 0, 0, 120, 20);
    gtk_label_set_xalign(GTK_LABEL(storageLabel), 0.0);
    gtk_label_set_xalign(GTK_LABEL(storageMode), 0.0);
    gtk_box_pack_start(GTK_BOX(stgBoxLabel), storageLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(stgBoxLabel), storageMode, FALSE, FALSE, 0);

    s_storage_value_label = storageMode;

    // Slot 区域
    GtkWidget* sltBoxLabel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget* slotLabel = helper.createLabel("Slot:", "slot_label", 0, 0, -1, 20);
    GtkWidget* slotMode = helper.createLabel("Unknown", "slot_mode", 0, 0, 120, 20);
    gtk_label_set_xalign(GTK_LABEL(slotLabel), 0.0);
    gtk_label_set_xalign(GTK_LABEL(slotMode), 0.0);
    gtk_box_pack_start(GTK_BOX(sltBoxLabel), slotLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(sltBoxLabel), slotMode, FALSE, FALSE, 0);

    s_slot_value_label = slotMode;

    // 右侧 Progress 文本区域
    GtkWidget* prgTextHBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget* progressLabel = helper.createLabel(_("Progress:"), "progress_label", 0, 0, -1, 20);
    GtkWidget* percentLabel = helper.createLabel("0%", "percent", 0, 0, 40, 20);
    gtk_label_set_xalign(GTK_LABEL(progressLabel), 1.0);
    gtk_label_set_xalign(GTK_LABEL(percentLabel), 1.0);
    gtk_box_pack_start(GTK_BOX(prgTextHBox), progressLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(prgTextHBox), percentLabel, FALSE, FALSE, 0);

    s_percent_label = percentLabel;

    // 左侧状态
    gtk_box_pack_start(GTK_BOX(bottomStatusBox), stBoxLabel, FALSE, FALSE, 0);

    // 右侧整体区域：Mode + Storage + Slot + Progress
    GtkWidget* rightGroup = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_box_pack_start(GTK_BOX(rightGroup), mdBoxLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rightGroup), stgBoxLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rightGroup), sltBoxLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rightGroup), prgTextHBox, FALSE, FALSE, 0);

    // 中间弹性空白，保证右侧整体一起移动
    GtkWidget* stSpacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(bottomStatusBox), stSpacer, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(bottomStatusBox), rightGroup, FALSE, FALSE, 0);

    // 组装三层
    gtk_box_pack_start(GTK_BOX(bottomContainer), topActionBox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bottomContainer), midProgressBox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bottomContainer), bottomStatusBox, FALSE, FALSE, 0);

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
    // 复用 Status 文本区域展示 IO 状态（保持现有行为：con 上显示读写进度与速率）
    if (!s_status_value_label) return;
    gtk_label_set_text(GTK_LABEL(s_status_value_label), text.c_str());
}
