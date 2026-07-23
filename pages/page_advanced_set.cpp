/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#include "page_advanced_set.h"
#include "../common.h"
#include "../main.h"
#include "../i18n.h"
#include "ui/ui_common.h"
#include "../core/config_service.h"
#include <algorithm>

#ifdef _WIN32
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif
#endif

extern spdio_t*& io;
extern int blk_size;
extern int keep_charge;
extern int end_data;
extern AppState g_app_state;

namespace {

constexpr int LANG_INDEX_SYSTEM = 0;
constexpr int LANG_INDEX_ZH_CN  = 1;
constexpr int LANG_INDEX_EN_US  = 2;

int ui_language_to_index(const std::string& lang) {
	if (lang.empty() || lang == "auto") {
		return LANG_INDEX_SYSTEM;
	}
	if (lang == "zh_CN") {
		return LANG_INDEX_ZH_CN;
	}
	if (lang == "en_US") {
		return LANG_INDEX_EN_US;
	}
	// 未知值：退回系统默认
	return LANG_INDEX_SYSTEM;
}

std::string index_to_ui_language(int index) {
	switch (index) {
	case LANG_INDEX_ZH_CN:
		return "zh_CN";
	case LANG_INDEX_EN_US:
		return "en_US";
	case LANG_INDEX_SYSTEM:
	default:
		return "auto";
	}
}

} // namespace

static void on_button_clicked_raw_data_en(GtkWidgetHelper helper) {
	int rawdatay = atoi(helper.getEntryText(helper.getWidget("raw_data_v")));
	if (rawdatay) {
		Da_Info.bSupportRawData = rawdatay;
	}
	if (Da_Info.bSupportRawData) showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Info"), _("Successfully enabled raw data mode"));
	else showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Info"), _("Failed to enable raw data mode, please set value!"));
}

static void on_button_clicked_raw_data_dis(GtkWidgetHelper helper) {
	Da_Info.bSupportRawData = 0;
	showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Info"), _("Successfully disabled raw data mode"));
}

static void on_button_clicked_transcode_en(GtkWidgetHelper helper) {
	unsigned a, f;
	a = 1;
	f = (io->flags & ~FLAGS_TRANSCODE);
	io->flags = f | (a ? FLAGS_TRANSCODE : 0);
	showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Info"), _("Enabled transcode successfully"));
}

static void on_button_clicked_transcode_dis(GtkWidgetHelper helper) {
	unsigned a = 0;
	encode_msg_nocpy(io, BSL_CMD_DISABLE_TRANSCODE, 0);
	if (!send_and_check(io)) io->flags &= ~FLAGS_TRANSCODE;
	showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Info"), _("Disabled transcode successfully"));
}

static void on_button_clicked_charge_en(GtkWidgetHelper helper) {
	keep_charge = 1;
	showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Info"), _("Set successfully"));
}

static void on_button_clicked_charge_dis(GtkWidgetHelper helper) {
	keep_charge = 0;
	showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Info"), _("Set successfully"));
}

static void on_button_clicked_end_data_en(GtkWidgetHelper helper) {
	end_data = 1;
	showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Info"), _("Set successfully"));
}

static void on_button_clicked_end_data_dis(GtkWidgetHelper helper) {
	end_data = 0;
	showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Info"), _("Set successfully"));
}

static void on_button_clicked_abpart_auto(GtkWidgetHelper helper) {
	(void)helper;
	g_app_state.flash.selected_ab = 0;
}

static void on_button_clicked_abpart_a(GtkWidgetHelper helper) {
	(void)helper;
	g_app_state.flash.selected_ab = 1;
}

static void on_button_clicked_abpart_b(GtkWidgetHelper helper) {
	(void)helper;
	g_app_state.flash.selected_ab = 2;
}
static void on_button_clicked_force_flash_en(GtkWidgetHelper helper) {
	(void)helper;
	g_app_state.flash.g_w_force = 1;
	showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Info"), _("Force flash enabled"));
}
static void on_button_clicked_force_flash_dis(GtkWidgetHelper helper) {
	(void)helper;
	g_app_state.flash.g_w_force = 0;
	showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), _("Info"), _("Force flash disabled"));
}


GtkWidget* AdvancedSetPage::init(GtkWidgetHelper& helper, GtkWidget* notebook) {
    GtkWidget* advSetPage = gtk_grid_new();
    gtk_widget_set_name(advSetPage, "adv_set_page");
    helper.addWidget("adv_set_page", advSetPage);
    helper.addNotebookPage(notebook, advSetPage, _("Advanced Settings"));

    GtkWidget* advScroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(advScroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(advScroll, TRUE);
    gtk_widget_set_vexpand(advScroll, TRUE);

    GtkWidget* mainBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 32);
    gtk_widget_set_margin_start(mainBox, 40);
    gtk_widget_set_margin_end(mainBox, 40);
    gtk_widget_set_margin_top(mainBox, 40);
    gtk_widget_set_margin_bottom(mainBox, 40);
    gtk_widget_set_halign(mainBox, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(mainBox, 520, -1);

    auto makeCardBox = [](int pad_h, int pad_v) -> GtkWidget* {
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
        gtk_widget_set_margin_start(box, pad_h);
        gtk_widget_set_margin_end(box, pad_h);
        gtk_widget_set_margin_top(box, pad_v);
        gtk_widget_set_margin_bottom(box, pad_v);
        return box;
    };

    // 0. 界面语言设置部分
    GtkWidget* langFrame = gtk_frame_new(NULL);
    GtkWidget* langTitle = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(langTitle), (std::string("<b>") + _("UI language") + "</b>").c_str());
    gtk_widget_set_halign(langTitle, GTK_ALIGN_CENTER);
    gtk_frame_set_label_widget(GTK_FRAME(langFrame), langTitle);
    gtkFrameSetLabelAlign(langFrame, 0.5, 0.5);

    GtkWidget* langBox = makeCardBox(32, 16);
    gtk_frame_set_child(GTK_FRAME(langFrame), langBox);

    GtkWidget* langLabel = gtk_label_new(_("UI language"));
    GtkWidget* combo_ui_language = gtk_combo_box_text_new();
    gtk_widget_set_name(combo_ui_language, "ui_language_combo");
    helper.addWidget("ui_language_combo", combo_ui_language);
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_ui_language), _("System default"));
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_ui_language), _("Simplified Chinese"));
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_ui_language), _("English"));

    GtkWidget* langRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_halign(langRow, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(langRow), langLabel);
    gtk_box_append(GTK_BOX(langRow), combo_ui_language);

    GtkWidget* langApplyBtn = gtk_button_new_with_label(_("Apply"));
    gtk_widget_set_name(langApplyBtn, "ui_language_apply");
    gtk_widget_set_size_request(langApplyBtn, 80, 32);
    helper.addWidget("ui_language_apply", langApplyBtn);
    gtk_box_append(GTK_BOX(langRow), langApplyBtn);

    gtk_box_append(GTK_BOX(langBox), langRow);
    gtk_box_append(GTK_BOX(mainBox), langFrame);

    // 1. 数据块大小设置部分（滑块拉伸修复重点）
    GtkWidget* blkFrame = gtk_frame_new(NULL);
    GtkWidget* blkTitle = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(blkTitle), (std::string("<b>") + _("Data block size") + "</b>").c_str());
    gtk_widget_set_halign(blkTitle, GTK_ALIGN_CENTER);
    gtk_frame_set_label_widget(GTK_FRAME(blkFrame), blkTitle);
    gtkFrameSetLabelAlign(blkFrame, 0.5, 0.5);
    helper.addWidget("blk_label_title", blkTitle);

    GtkWidget* blkBox = makeCardBox(32, 16);
    gtk_frame_set_child(GTK_FRAME(blkFrame), blkBox);

    // 从当前 GUI 配置初始化滑条和数值显示
    auto cfg = MakeBlockSizeConfigFromGui();
    uint32_t effective_step = cfg.manual_block_size;
    gdouble slider_min = 10000.0;
    gdouble slider_max = 60000.0;
    uint32_t slider_step = effective_step ? effective_step : static_cast<uint32_t>(slider_min);
    if (slider_step < static_cast<uint32_t>(slider_min)) slider_step = static_cast<uint32_t>(slider_min);
    if (slider_step > static_cast<uint32_t>(slider_max)) slider_step = static_cast<uint32_t>(slider_max);

    // 创建滑块并设置扩展属性使其填满父容器
    GtkWidget* blkSlider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, slider_min, slider_max, 10000);
    gtk_range_set_value(GTK_RANGE(blkSlider), slider_step);
    gtk_scale_set_draw_value(GTK_SCALE(blkSlider), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(blkSlider), GTK_POS_RIGHT);
    gtk_widget_set_name(blkSlider, "blk_size");
    // ★ 关键：让滑块横向扩展并填充
    gtk_widget_set_hexpand(blkSlider, TRUE);
    gtk_widget_set_halign(blkSlider, GTK_ALIGN_FILL);
    helper.addWidget("blk_size", blkSlider);

    GtkWidget* sizeConLabel = gtk_label_new(_("Value:"));
    helper.addWidget("blk_label", sizeConLabel);
    GtkWidget* sizeCon = gtk_label_new(std::to_string(slider_step).c_str());
    gtk_widget_set_name(sizeCon, "size_con");
    gtk_widget_set_size_request(sizeCon, 60, 20);
    helper.addWidget("size_con", sizeCon);

    // ★ 将滑块放入一个横向 Box，该 Box 也设置为扩展
    GtkWidget* sliderBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_hexpand(sliderBox, TRUE);
    gtk_widget_set_halign(sliderBox, GTK_ALIGN_FILL);
    gtk_box_append(GTK_BOX(sliderBox), blkSlider);
    gtk_box_append(GTK_BOX(sliderBox), sizeConLabel);
    gtk_box_append(GTK_BOX(sliderBox), sizeCon);

    gtk_box_append(GTK_BOX(blkBox), sliderBox);

    GtkWidget* blkResetBtn = gtk_button_new_with_label(_("Reset to default block size"));
    gtk_widget_set_name(blkResetBtn, "blk_reset");
    gtk_widget_set_size_request(blkResetBtn, 220, 32);
    gtk_widget_set_sensitive(blkResetBtn, FALSE);
    helper.addWidget("blk_reset", blkResetBtn);
    gtk_box_append(GTK_BOX(blkBox), blkResetBtn);

    gtk_box_append(GTK_BOX(mainBox), blkFrame);

    // 2. Rawdata模式设置部分
    GtkWidget* rawFrame = gtk_frame_new(NULL);
    GtkWidget* rawTitle = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(rawTitle), (std::string("<b>") + _("Rawdata Mode --- Value support: {1, 2}") + "</b>").c_str());
    gtk_widget_set_halign(rawTitle, GTK_ALIGN_CENTER);
    gtk_frame_set_label_widget(GTK_FRAME(rawFrame), rawTitle);
    gtkFrameSetLabelAlign(rawFrame, 0.5, 0.5);
    helper.addWidget("raw_label", rawTitle);

    GtkWidget* rawBox = makeCardBox(32, 16);
    gtk_frame_set_child(GTK_FRAME(rawFrame), rawBox);

    GtkWidget* rawDataEn = gtk_button_new_with_label(_("Enable Rawdata mode"));
    gtk_widget_set_name(rawDataEn, "raw_data_en");
    gtk_widget_set_size_request(rawDataEn, 210, 36);
    helper.addWidget("raw_data_en", rawDataEn);

    GtkWidget* rawDataDis = gtk_button_new_with_label(_("Disable Rawdata mode"));
    gtk_widget_set_name(rawDataDis, "raw_data_dis");
    gtk_widget_set_size_request(rawDataDis, 210, 36);
    helper.addWidget("raw_data_dis", rawDataDis);

    GtkWidget* rlabel = gtk_label_new(_("Value:"));
    gtk_widget_set_name(rlabel, "rawLabel");
    gtk_widget_set_size_request(rlabel, 48, 36);
    helper.addWidget("rawLabel", rlabel);

    GtkWidget* rawDataMk = gtk_entry_new();
    gtk_widget_set_name(rawDataMk, "raw_data_v");
    gtk_widget_set_size_request(rawDataMk, 48, 36);
    helper.addWidget("raw_data_v", rawDataMk);

    GtkWidget* rawDataButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_halign(rawDataButtonBox, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(rawDataButtonBox), rawDataEn);
    gtk_box_append(GTK_BOX(rawDataButtonBox), rawDataDis);

    GtkWidget* rawValLinked = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class((rawValLinked), "linked");
    gtk_box_append(GTK_BOX(rawValLinked), rlabel);
    gtk_box_append(GTK_BOX(rawValLinked), rawDataMk);

    gtk_box_append(GTK_BOX(rawDataButtonBox), rawValLinked);
    gtk_box_append(GTK_BOX(rawBox), rawDataButtonBox);
    gtk_box_append(GTK_BOX(mainBox), rawFrame);

    // 3. 转码设置部分
    GtkWidget* transcodeFrame = gtk_frame_new(NULL);
    GtkWidget* transcodeTitle = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(transcodeTitle), (std::string("<b>") + _("Transcode --- FDL1/2") + "</b>").c_str());
    gtk_widget_set_halign(transcodeTitle, GTK_ALIGN_CENTER);
    gtk_frame_set_label_widget(GTK_FRAME(transcodeFrame), transcodeTitle);
    gtkFrameSetLabelAlign(transcodeFrame, 0.5, 0.5);
    helper.addWidget("transcode_label", transcodeTitle);

    GtkWidget* transcodeBox = makeCardBox(32, 16);
    gtk_frame_set_child(GTK_FRAME(transcodeFrame), transcodeBox);

    GtkWidget* transcode_en = gtk_button_new_with_label(_("Enable transcode - FDL1"));
    gtk_widget_set_name(transcode_en, "transcode_en");
    gtk_widget_set_size_request(transcode_en, 272, 36);
    helper.addWidget("transcode_en", transcode_en);

    GtkWidget* transcode_dis = gtk_button_new_with_label(_("Disable transcode --- FDL2"));
    gtk_widget_set_name(transcode_dis, "transcode_dis");
    gtk_widget_set_size_request(transcode_dis, 272, 36);
    helper.addWidget("transcode_dis", transcode_dis);

    GtkWidget* transcodeButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_halign(transcodeButtonBox, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(transcodeButtonBox), transcode_en);
    gtk_box_append(GTK_BOX(transcodeButtonBox), transcode_dis);

    gtk_box_append(GTK_BOX(transcodeBox), transcodeButtonBox);
    gtk_box_append(GTK_BOX(mainBox), transcodeFrame);

    // 4. 充电模式部分
    GtkWidget* chargeFrame = gtk_frame_new(NULL);
    GtkWidget* chargeTitle = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(chargeTitle), (std::string("<b>") + _("Charging Mode --- BROM") + "</b>").c_str());
    gtk_widget_set_halign(chargeTitle, GTK_ALIGN_CENTER);
    gtk_frame_set_label_widget(GTK_FRAME(chargeFrame), chargeTitle);
    gtkFrameSetLabelAlign(chargeFrame, 0.5, 0.5);
    helper.addWidget("charge_label", chargeTitle);

    GtkWidget* chargeBox = makeCardBox(32, 16);
    gtk_frame_set_child(GTK_FRAME(chargeFrame), chargeBox);

    GtkWidget* charge_en = gtk_button_new_with_label(_("Enable Charging mode --- BROM"));
    gtk_widget_set_name(charge_en, "charge_en");
    gtk_widget_set_size_request(charge_en, 272, 36);
    helper.addWidget("charge_en", charge_en);

    GtkWidget* charge_dis = gtk_button_new_with_label(_("Disable Charging mode --- BROM"));
    gtk_widget_set_name(charge_dis, "charge_dis");
    gtk_widget_set_size_request(charge_dis, 272, 36);
    helper.addWidget("charge_dis", charge_dis);

    GtkWidget* chargeButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_halign(chargeButtonBox, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(chargeButtonBox), charge_en);
    gtk_box_append(GTK_BOX(chargeButtonBox), charge_dis);

    gtk_box_append(GTK_BOX(chargeBox), chargeButtonBox);
    gtk_box_append(GTK_BOX(mainBox), chargeFrame);

    // 5. 发送结束数据部分
    GtkWidget* endDataFrame = gtk_frame_new(NULL);
    GtkWidget* endDataTitle = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(endDataTitle), (std::string("<b>") + _("Send End Data") + "</b>").c_str());
    gtk_widget_set_halign(endDataTitle, GTK_ALIGN_CENTER);
    gtk_frame_set_label_widget(GTK_FRAME(endDataFrame), endDataTitle);
    gtkFrameSetLabelAlign(endDataFrame, 0.5, 0.5);
    helper.addWidget("end_data_label", endDataTitle);

    GtkWidget* endDataBoxItem = makeCardBox(32, 16);
    gtk_frame_set_child(GTK_FRAME(endDataFrame), endDataBoxItem);

    GtkWidget* end_data_en_btn = gtk_button_new_with_label(_("Enable sending end data"));
    gtk_widget_set_name(end_data_en_btn, "end_data_en");
    gtk_widget_set_size_request(end_data_en_btn, 272, 36);
    helper.addWidget("end_data_en", end_data_en_btn);

    GtkWidget* end_data_dis_btn = gtk_button_new_with_label(_("Disable sending end data"));
    gtk_widget_set_name(end_data_dis_btn, "end_data_dis");
    gtk_widget_set_size_request(end_data_dis_btn, 272, 36);
    helper.addWidget("end_data_dis", end_data_dis_btn);

    GtkWidget* endDataButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_halign(endDataButtonBox, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(endDataButtonBox), end_data_en_btn);
    gtk_box_append(GTK_BOX(endDataButtonBox), end_data_dis_btn);

    gtk_box_append(GTK_BOX(endDataBoxItem), endDataButtonBox);
    gtk_box_append(GTK_BOX(mainBox), endDataFrame);

    // 6. 操作超时时间部分
    GtkWidget* timeoutFrame = gtk_frame_new(NULL);
    GtkWidget* timeoutTitle = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(timeoutTitle), (std::string("<b>") + _("Operation timeout") + "</b>").c_str());
    gtk_widget_set_halign(timeoutTitle, GTK_ALIGN_CENTER);
    gtk_frame_set_label_widget(GTK_FRAME(timeoutFrame), timeoutTitle);
    gtkFrameSetLabelAlign(timeoutFrame, 0.5, 0.5);
    helper.addWidget("timeout_label", timeoutTitle);

    GtkWidget* timeoutBox = makeCardBox(32, 16);
    gtk_frame_set_child(GTK_FRAME(timeoutFrame), timeoutBox);

    GtkWidget* timeout_op = gtk_spin_button_new_with_range(3000, 300000, 1);
    gtk_widget_set_name(timeout_op, "timeout");
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(timeout_op), 3000);
    gtk_widget_set_size_request(timeout_op, 160, 36);
    helper.addWidget("timeout", timeout_op);

    GtkWidget* timeoutWrap = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_halign(timeoutWrap, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(timeoutWrap), timeout_op);

    gtk_box_append(GTK_BOX(timeoutBox), timeoutWrap);
    gtk_box_append(GTK_BOX(mainBox), timeoutFrame);

    // 7. A/B分区设置部分
    GtkWidget* abpartFrame = gtk_frame_new(NULL);
    GtkWidget* abpartTitle = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(abpartTitle), (std::string("<b>") + _("A/B Part read/flash manually set --- FDL2") + "</b>").c_str());
    gtk_widget_set_halign(abpartTitle, GTK_ALIGN_CENTER);
    gtk_frame_set_label_widget(GTK_FRAME(abpartFrame), abpartTitle);
    gtkFrameSetLabelAlign(abpartFrame, 0.5, 0.5);
    helper.addWidget("abpart_label", abpartTitle);

    GtkWidget* abpartBox = makeCardBox(32, 16);
    gtk_frame_set_child(GTK_FRAME(abpartFrame), abpartBox);

    GtkWidget* abpart_auto = gtk_button_new_with_label(_("Not VAB --- FDL2"));
    gtk_widget_set_name(abpart_auto, "abpart_auto");
    gtk_widget_set_size_request(abpart_auto, 176, 36);
    helper.addWidget("abpart_auto", abpart_auto);

    GtkWidget* abpart_a = gtk_button_new_with_label(_("A Parts --- FDL2"));
    gtk_widget_set_name(abpart_a, "abpart_a");
    gtk_widget_set_size_request(abpart_a, 176, 36);
    helper.addWidget("abpart_a", abpart_a);

    GtkWidget* abpart_b = gtk_button_new_with_label(_("B Parts --- FDL2"));
    gtk_widget_set_name(abpart_b, "abpart_b");
    gtk_widget_set_size_request(abpart_b, 176, 36);
    helper.addWidget("abpart_b", abpart_b);

    GtkWidget* abpartButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_halign(abpartButtonBox, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(abpartButtonBox), abpart_auto);
    gtk_box_append(GTK_BOX(abpartButtonBox), abpart_a);
    gtk_box_append(GTK_BOX(abpartButtonBox), abpart_b);

    gtk_box_append(GTK_BOX(abpartBox), abpartButtonBox);
    gtk_box_append(GTK_BOX(mainBox), abpartFrame);

    // 8. 强制刷写设置部分
    GtkWidget* forceFlashFrame = gtk_frame_new(NULL);
    GtkWidget* forceFlashTitle = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(forceFlashTitle), (std::string("<b>") + _("Auto Force flash Settings") + "</b>").c_str());
    gtk_widget_set_halign(forceFlashTitle, GTK_ALIGN_CENTER);
    gtk_frame_set_label_widget(GTK_FRAME(forceFlashFrame), forceFlashTitle);
    gtkFrameSetLabelAlign(forceFlashFrame, 0.5, 0.5);
    helper.addWidget("force_flash_label", forceFlashTitle);

    GtkWidget* forceFlashBox = makeCardBox(32, 16);
    gtk_frame_set_child(GTK_FRAME(forceFlashFrame), forceFlashBox);

    GtkWidget* forceFlashEn = gtk_button_new_with_label(_("Enable Auto Force Flash"));
    gtk_widget_set_name(forceFlashEn, "force_flash_en");
    gtk_widget_set_size_request(forceFlashEn, 210, 36);
    helper.addWidget("force_flash_en", forceFlashEn);

    GtkWidget* forceFlashDis = gtk_button_new_with_label(_("Disable Auto Force Flash"));
    gtk_widget_set_name(forceFlashDis, "force_flash_dis");
    gtk_widget_set_size_request(forceFlashDis, 210, 36);
    helper.addWidget("force_flash_dis", forceFlashDis);

    GtkWidget* forceFlashButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_halign(forceFlashButtonBox, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(forceFlashButtonBox), forceFlashEn);
    gtk_box_append(GTK_BOX(forceFlashButtonBox), forceFlashDis);

    gtk_box_append(GTK_BOX(forceFlashBox), forceFlashButtonBox);
    gtk_box_append(GTK_BOX(mainBox), forceFlashFrame);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(advScroll), mainBox);
    gtk_grid_attach(GTK_GRID(advSetPage), advScroll, 0, 0, 4, 6);

    // 读取配置并设置界面语言下拉框当前值
    auto cfgSvc = sfd::createConfigService();
    if (cfgSvc) {
        GtkWidget* combo_ui_language = helper.getWidget("ui_language_combo");
        sfd::AppConfig cfg{};
        if (!sfd::loadAppConfigOrDefault(cfg)) {
            // 已填充默认值
        }
        int idx = ui_language_to_index(cfg.ui_language);
        gtk_combo_box_set_active(GTK_COMBO_BOX(combo_ui_language), idx);
    }

    return advSetPage;
}

void AdvancedSetPage::bindSignals(GtkWidgetHelper& helper) {
	GtkWidget* blkSlider = helper.getWidget("blk_size");
	GtkWidget* sizeCon = helper.getWidget("size_con");
	GtkWidget* timeout_op = helper.getWidget("timeout");

	helper.bindValueChanged(blkSlider, [&]() {
		double value = gtk_range_get_value(GTK_RANGE(helper.getWidget("blk_size")));
		int intValue = static_cast<int>(value);
		GtkWidget* sc = helper.getWidget("size_con");
		gtk_label_set_text(GTK_LABEL(sc), std::to_string(intValue).c_str());
		blk_size = intValue;
		auto& s = GetGuiIoSettings();
		s.mode = sfd::BlockSizeMode::MANUAL_BLOCK_SIZE;
		s.manual_block_size = static_cast<uint32_t>(intValue);
		LogBlkState("adv_set slider_changed");
	});
	helper.bindClick(helper.getWidget("blk_reset"), [&]() {
		ResetBlockSizeToDefault();
		auto& s = GetGuiIoSettings();
		GtkWidget* sc = helper.getWidget("size_con");
		gtk_label_set_text(GTK_LABEL(sc), std::to_string(s.manual_block_size).c_str());
		GtkWidget* slider = helper.getWidget("blk_size");
		if (slider && GTK_IS_RANGE(slider)) {
			gdouble min = 10000.0;
			gdouble max = std::max(min, static_cast<gdouble>(s.manual_block_size));
			gtk_range_set_range(GTK_RANGE(slider), min, max);
			gtk_range_set_value(GTK_RANGE(slider), s.manual_block_size);
		}
		LogBlkState("adv_set blk_reset");
	});
	helper.bindClick(helper.getWidget("raw_data_en"), [&]() {
		on_button_clicked_raw_data_en(helper);
	});
	helper.bindClick(helper.getWidget("raw_data_dis"), [&]() {
		on_button_clicked_raw_data_dis(helper);
	});
	helper.bindClick(helper.getWidget("transcode_en"), [&]() {
		on_button_clicked_transcode_en(helper);
	});
	helper.bindClick(helper.getWidget("transcode_dis"), [&]() {
		on_button_clicked_transcode_dis(helper);
	});
	helper.bindClick(helper.getWidget("charge_en"), [&]() {
		on_button_clicked_charge_en(helper);
	});
	helper.bindClick(helper.getWidget("charge_dis"), [&]() {
		on_button_clicked_charge_dis(helper);
	});
	helper.bindClick(helper.getWidget("end_data_en"), [&]() {
		on_button_clicked_end_data_en(helper);
	});
	helper.bindClick(helper.getWidget("end_data_dis"), [&]() {
		on_button_clicked_end_data_dis(helper);
	});
	helper.bindValueChanged(timeout_op, [&]() {
		GtkWidget* to = helper.getWidget("timeout");
		io->timeout = static_cast<int>(helper.getSpinValue(to));
	});
	helper.bindClick(helper.getWidget("abpart_auto"),[&](){
		on_button_clicked_abpart_auto(helper);
	});
	helper.bindClick(helper.getWidget("abpart_a"),[&](){
		on_button_clicked_abpart_a(helper);
	});
	helper.bindClick(helper.getWidget("abpart_b"),[&](){
		on_button_clicked_abpart_b(helper);
	});
	helper.bindClick(helper.getWidget("force_flash_en"),[&](){
		on_button_clicked_force_flash_en(helper);
	});
	helper.bindClick(helper.getWidget("force_flash_dis"),[&](){
		on_button_clicked_force_flash_dis(helper);
	});

	// 语言应用按钮：保存 ui_language 并提示重启后生效
	helper.bindClick(helper.getWidget("ui_language_apply"), [&]() {
		auto cfgSvc = sfd::createConfigService();
		if (!cfgSvc) {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")),
			               _("Error"),
			               _("Failed to create config service."));
			return;
		}

		sfd::AppConfig cfg{};
		sfd::loadAppConfigOrDefault(cfg);

		GtkWidget* combo_ui_language = helper.getWidget("ui_language_combo");
		int idx = gtk_combo_box_get_active(GTK_COMBO_BOX(combo_ui_language));
		cfg.ui_language = index_to_ui_language(idx);

		sfd::ConfigStatus status = cfgSvc->saveAppConfig(cfg);
		if (!status.success) {
			showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")),
			               _("Error"),
			               status.message.c_str());
			return;
		}

		showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")),
		              _("Info"),
		              _("Language will take effect after restart."));
	});
}

GtkWidget* create_advanced_set_page(GtkWidgetHelper& helper, GtkWidget* notebook) {
    AdvancedSetPage page;
    return page.init(helper, notebook);
}

void bind_advanced_set_signals(GtkWidgetHelper& helper) {
    AdvancedSetPage page;
    page.bindSignals(helper);
}
