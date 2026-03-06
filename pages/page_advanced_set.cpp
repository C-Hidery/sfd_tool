#include "page_advanced_set.h"
#include "../common.h"
#include "../main.h"
#include "../i18n.h"

extern spdio_t* io;
extern int blk_size;
extern int keep_charge;
extern int end_data;
extern int selected_ab;

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
	selected_ab = 0;
}

static void on_button_clicked_abpart_a(GtkWidgetHelper helper) {
	selected_ab = 1;
}

static void on_button_clicked_abpart_b(GtkWidgetHelper helper) {
	selected_ab = 2;
}

GtkWidget* create_advanced_set_page(GtkWidgetHelper& helper, GtkWidget* notebook) {
	GtkWidget* advSetPage = helper.createGrid("adv_set_page", 5, 5);
	helper.addNotebookPage(notebook, advSetPage, _("Advanced Settings"));

	// 数据块大小设置部分
	GtkWidget* blkLabel = helper.createLabel(_("Data block size"), "blk_label", 0, 0, 200, 20);

	GtkWidget* blkSlider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 10000, 60000, 10000);
	gtk_range_set_value(GTK_RANGE(blkSlider), 10000);
	gtk_scale_set_draw_value(GTK_SCALE(blkSlider), TRUE);
	gtk_scale_set_value_pos(GTK_SCALE(blkSlider), GTK_POS_RIGHT);
	gtk_widget_set_name(blkSlider, "blk_size");
	helper.addWidget("blk_size", blkSlider);
	gtk_widget_set_size_request(blkSlider, 1036, 30);

	GtkWidget* sizeCon = helper.createLabel("10000", "size_con", 0, 0, 60, 20);

	// Rawdata模式设置部分
	GtkWidget* rawDataEn = helper.createButton(_("Enable Rawdata mode"), "raw_data_en",
	                       nullptr, 0, 0, 250, 32);
	GtkWidget* rawDataDis = helper.createButton(_("Disable Rawdata mode"), "raw_data_dis",
	                        nullptr, 0, 0, 250, 32);
	GtkWidget* rlabel = helper.createLabel(_("Value:"), "rawLabel", 0, 0, 80, 30);
	GtkWidget* rawDataMk = helper.createEntry("raw_data_v", "", false, 0, 0, 50, 30);

	// 转码设置部分
	GtkWidget* transcode_en = helper.createButton(_("Enable transcode - FDL1"), "transcode_en",
	                          nullptr, 0, 0, 220, 32);
	GtkWidget* transcode_dis = helper.createButton(_("Disable transcode --- FDL2"), "transcode_dis",
	                           nullptr, 0, 0, 220, 32);

	// 充电模式设置部分
	GtkWidget* charge_en = helper.createButton(_("Enable Charging mode --- BROM"), "charge_en",
	                       nullptr, 0, 0, 240, 32);
	GtkWidget* charge_dis = helper.createButton(_("Disable Charging mode --- BROM"), "charge_dis",
	                        nullptr, 0, 0, 240, 32);

	// 发送结束数据设置部分
	GtkWidget* end_data_en_btn = helper.createButton(_("Enable sending end data"),
	                         "end_data_en", nullptr, 0, 0, 280, 32);
	GtkWidget* end_data_dis_btn = helper.createButton(_("Disable sending end data"),
	                          "end_data_dis", nullptr, 0, 0, 280, 32);

	// 操作超时时间设置部分
	GtkWidget* timeout_label = helper.createLabel(_("Operation timeout"),
	                           "timeout_label", 0, 0, 200, 20);
	GtkWidget* timeout_op = helper.createSpinButton(3000, 300000, 1, "timeout", 3000, 0, 0, 120, 32);
	
	// A/B分区设置
	GtkWidget* abpart_auto = helper.createButton(_("Not VAB --- FDL2"),"abpart_auto",nullptr,0,0,120,32);
	GtkWidget* abpart_a = helper.createButton(_("A Parts --- FDL2"),"abpart_a",nullptr,0,0,130,32);
	GtkWidget* abpart_b = helper.createButton(_("B Parts --- FDL2"),"abpart_b",nullptr,0,0,130,32);
	
	// 创建主垂直容器
	GtkWidget* emainBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
	gtk_widget_set_margin_start(emainBox, 20);
	gtk_widget_set_margin_end(emainBox, 20);
	gtk_widget_set_margin_top(emainBox, 20);
	gtk_widget_set_margin_bottom(emainBox, 20);

	// 1. 数据块大小部分
	GtkWidget* blockSizeBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	GtkWidget* blockLabelBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	gtk_box_pack_start(GTK_BOX(blockLabelBox), blkLabel, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(blockLabelBox), sizeCon, FALSE, FALSE, 0);

	GtkWidget* sliderBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_box_pack_start(GTK_BOX(sliderBox), blkSlider, TRUE, TRUE, 0);

	gtk_box_pack_start(GTK_BOX(blockSizeBox), blockLabelBox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(blockSizeBox), sliderBox, FALSE, FALSE, 0);

	// 2. Rawdata模式部分
	GtkWidget* rawDataBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	GtkWidget* rawDataLabel = gtk_label_new(_("Rawdata Mode --- Value support: {1, 2}"));
	gtk_label_set_xalign(GTK_LABEL(rawDataLabel), 0.0);

	GtkWidget* rawDataButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_box_pack_start(GTK_BOX(rawDataButtonBox), rawDataEn, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(rawDataButtonBox), rawDataDis, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(rawDataButtonBox), rlabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(rawDataButtonBox), rawDataMk, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(rawDataBox), rawDataLabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(rawDataBox), rawDataButtonBox, FALSE, FALSE, 0);

	// 3. 转码设置部分
	GtkWidget* transcodeBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	GtkWidget* transcodeLabel = gtk_label_new(_("Transcode --- FDL1/2"));
	gtk_label_set_xalign(GTK_LABEL(transcodeLabel), 0.0);

	GtkWidget* transcodeButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_box_pack_start(GTK_BOX(transcodeButtonBox), transcode_en, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(transcodeButtonBox), transcode_dis, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(transcodeBox), transcodeLabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(transcodeBox), transcodeButtonBox, FALSE, FALSE, 0);

	// 4. 充电模式部分
	GtkWidget* chargeBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	GtkWidget* chargeLabel = gtk_label_new(_("Charging Mode --- BROM"));
	gtk_label_set_xalign(GTK_LABEL(chargeLabel), 0.0);

	GtkWidget* chargeButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_box_pack_start(GTK_BOX(chargeButtonBox), charge_en, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(chargeButtonBox), charge_dis, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(chargeBox), chargeLabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(chargeBox), chargeButtonBox, FALSE, FALSE, 0);

	// 5. 发送结束数据部分
	GtkWidget* endDataBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	GtkWidget* endDataLabel = gtk_label_new(_("Send End Data"));
	gtk_label_set_xalign(GTK_LABEL(endDataLabel), 0.0);

	GtkWidget* endDataButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_box_pack_start(GTK_BOX(endDataButtonBox), end_data_en_btn, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(endDataButtonBox), end_data_dis_btn, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(endDataBox), endDataLabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(endDataBox), endDataButtonBox, FALSE, FALSE, 0);

	// 6. 操作超时时间部分
	GtkWidget* timeoutBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	GtkWidget* timeoutTopBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_box_pack_start(GTK_BOX(timeoutTopBox), timeout_label, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(timeoutTopBox), timeout_op, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(timeoutBox), timeoutTopBox, FALSE, FALSE, 0);

	// 7. A/B分区设置部分
	GtkWidget* abpartBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	GtkWidget* abpartLabel = gtk_label_new(_("A/B Part read/flash manually set --- FDL2"));
	gtk_label_set_xalign(GTK_LABEL(abpartLabel), 0.0);
	GtkWidget* abpartButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_box_pack_start(GTK_BOX(abpartButtonBox), abpart_auto,FALSE,FALSE,0);
	gtk_box_pack_start(GTK_BOX(abpartButtonBox), abpart_a,FALSE,FALSE,0);
	gtk_box_pack_start(GTK_BOX(abpartButtonBox), abpart_b, FALSE,FALSE,0);

	gtk_box_pack_start(GTK_BOX(abpartBox), abpartLabel, FALSE,FALSE,0);
	gtk_box_pack_start(GTK_BOX(abpartBox), abpartButtonBox,FALSE,FALSE,0);

	// 将所有部分添加到主容器
	gtk_box_pack_start(GTK_BOX(emainBox), blockSizeBox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(emainBox), rawDataBox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(emainBox), transcodeBox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(emainBox), chargeBox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(emainBox), endDataBox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(emainBox), abpartBox,FALSE,FALSE,0);
	gtk_box_pack_start(GTK_BOX(emainBox), timeoutBox, FALSE, FALSE, 0);

	// 添加弹性空间使内容顶部对齐
	GtkWidget* bottomSpacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_vexpand(bottomSpacer, TRUE);
	gtk_box_pack_end(GTK_BOX(emainBox), bottomSpacer, TRUE, TRUE, 0);

	// 添加到网格
	helper.addToGrid(advSetPage, emainBox, 0, 0, 4, 6);

	return advSetPage;
}

void bind_advanced_set_signals(GtkWidgetHelper& helper) {
	GtkWidget* blkSlider = helper.getWidget("blk_size");
	GtkWidget* sizeCon = helper.getWidget("size_con");
	GtkWidget* timeout_op = helper.getWidget("timeout");

	helper.bindValueChanged(blkSlider, [&]() {
		double value = gtk_range_get_value(GTK_RANGE(helper.getWidget("blk_size")));
		int intValue = static_cast<int>(value);
		GtkWidget* sc = helper.getWidget("size_con");
		gtk_label_set_text(GTK_LABEL(sc), std::to_string(intValue).c_str());
		blk_size = intValue;
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
		io->timeout = helper.getSpinValue(to);
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
}
