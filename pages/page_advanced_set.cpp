#include "page_advanced_set.h"
#include "../common.h"
#include "../main.h"
#include "../i18n.h"

extern spdio_t*& io;
extern int blk_size;
extern int keep_charge;
extern int end_data;
extern AppState g_app_state;

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

GtkWidget* create_advanced_set_page(GtkWidgetHelper& helper, GtkWidget* notebook) {
	GtkWidget* advSetPage = helper.createGrid("adv_set_page", 5, 5);
	helper.addNotebookPage(notebook, advSetPage, _("Advanced Settings"));

	GtkWidget* advScroll = gtk_scrolled_window_new(NULL, NULL);
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
	gtk_widget_set_size_request(mainBox, 600, -1);

	auto makeCardBox = [](int pad_h, int pad_v) -> GtkWidget* {
		GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
		gtk_widget_set_margin_start(box, pad_h);
		gtk_widget_set_margin_end(box, pad_h);
		gtk_widget_set_margin_top(box, pad_v);
		gtk_widget_set_margin_bottom(box, pad_v);
		return box;
	};

	// 1. 数据块大小设置部分
	GtkWidget* blkFrame = gtk_frame_new(NULL);
	GtkWidget* blkTitle = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(blkTitle), (std::string("<b>") + _("Data block size") + "</b>").c_str());
	gtk_widget_set_halign(blkTitle, GTK_ALIGN_CENTER);
	gtk_frame_set_label_widget(GTK_FRAME(blkFrame), blkTitle);
	gtk_frame_set_label_align(GTK_FRAME(blkFrame), 0.5, 0.5);
	helper.addWidget("blk_label_title", blkTitle);

	GtkWidget* blkBox = makeCardBox(32, 16);
	gtk_container_add(GTK_CONTAINER(blkFrame), blkBox);

	GtkWidget* blkSlider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 10000, 60000, 10000);
	gtk_range_set_value(GTK_RANGE(blkSlider), 10000);
	gtk_scale_set_draw_value(GTK_SCALE(blkSlider), TRUE);
	gtk_scale_set_value_pos(GTK_SCALE(blkSlider), GTK_POS_RIGHT);
	gtk_widget_set_name(blkSlider, "blk_size");
	helper.addWidget("blk_size", blkSlider);
	
	GtkWidget* sizeConLabel = gtk_label_new(_("Value:"));
	helper.addWidget("blk_label", sizeConLabel);
	GtkWidget* sizeCon = helper.createLabel("10000", "size_con", 0, 0, 60, 20);

	GtkWidget* sliderBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
	gtk_box_pack_start(GTK_BOX(sliderBox), blkSlider, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(sliderBox), sizeConLabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(sliderBox), sizeCon, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(blkBox), sliderBox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(mainBox), blkFrame, FALSE, FALSE, 0);

	// 2. Rawdata模式设置部分
	GtkWidget* rawFrame = gtk_frame_new(NULL);
	GtkWidget* rawTitle = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(rawTitle), (std::string("<b>") + _("Rawdata Mode --- Value support: {1, 2}") + "</b>").c_str());
	gtk_widget_set_halign(rawTitle, GTK_ALIGN_CENTER);
	gtk_frame_set_label_widget(GTK_FRAME(rawFrame), rawTitle);
	gtk_frame_set_label_align(GTK_FRAME(rawFrame), 0.5, 0.5);
	helper.addWidget("raw_label", rawTitle);

	GtkWidget* rawBox = makeCardBox(32, 16);
	gtk_container_add(GTK_CONTAINER(rawFrame), rawBox);

	GtkWidget* rawDataEn = helper.createButton(_("Enable Rawdata mode"), "raw_data_en", nullptr, 0, 0, 210, 36);
	GtkWidget* rawDataDis = helper.createButton(_("Disable Rawdata mode"), "raw_data_dis", nullptr, 0, 0, 210, 36);
	GtkWidget* rlabel = helper.createLabel(_("Value:"), "rawLabel", 0, 0, 48, 36);
	GtkWidget* rawDataMk = helper.createEntry("raw_data_v", "", false, 0, 0, 48, 36);

	GtkWidget* rawDataButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
	gtk_widget_set_halign(rawDataButtonBox, GTK_ALIGN_CENTER);
	gtk_box_pack_start(GTK_BOX(rawDataButtonBox), rawDataEn, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(rawDataButtonBox), rawDataDis, FALSE, FALSE, 0);
	
	GtkWidget* rawValLinked = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(rawValLinked), "linked");
	gtk_box_pack_start(GTK_BOX(rawValLinked), rlabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(rawValLinked), rawDataMk, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(rawDataButtonBox), rawValLinked, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(rawBox), rawDataButtonBox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(mainBox), rawFrame, FALSE, FALSE, 0);

	// 3. 转码设置部分
	GtkWidget* transcodeFrame = gtk_frame_new(NULL);
	GtkWidget* transcodeTitle = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(transcodeTitle), (std::string("<b>") + _("Transcode --- FDL1/2") + "</b>").c_str());
	gtk_widget_set_halign(transcodeTitle, GTK_ALIGN_CENTER);
	gtk_frame_set_label_widget(GTK_FRAME(transcodeFrame), transcodeTitle);
	gtk_frame_set_label_align(GTK_FRAME(transcodeFrame), 0.5, 0.5);
	helper.addWidget("transcode_label", transcodeTitle);

	GtkWidget* transcodeBox = makeCardBox(32, 16);
	gtk_container_add(GTK_CONTAINER(transcodeFrame), transcodeBox);

	GtkWidget* transcode_en = helper.createButton(_("Enable transcode - FDL1"), "transcode_en", nullptr, 0, 0, 272, 36);
	GtkWidget* transcode_dis = helper.createButton(_("Disable transcode --- FDL2"), "transcode_dis", nullptr, 0, 0, 272, 36);

	GtkWidget* transcodeButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
	gtk_widget_set_halign(transcodeButtonBox, GTK_ALIGN_CENTER);
	gtk_box_pack_start(GTK_BOX(transcodeButtonBox), transcode_en, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(transcodeButtonBox), transcode_dis, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(transcodeBox), transcodeButtonBox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(mainBox), transcodeFrame, FALSE, FALSE, 0);

	// 4. 充电模式部分
	GtkWidget* chargeFrame = gtk_frame_new(NULL);
	GtkWidget* chargeTitle = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(chargeTitle), (std::string("<b>") + _("Charging Mode --- BROM") + "</b>").c_str());
	gtk_widget_set_halign(chargeTitle, GTK_ALIGN_CENTER);
	gtk_frame_set_label_widget(GTK_FRAME(chargeFrame), chargeTitle);
	gtk_frame_set_label_align(GTK_FRAME(chargeFrame), 0.5, 0.5);
	helper.addWidget("charge_label", chargeTitle);

	GtkWidget* chargeBox = makeCardBox(32, 16);
	gtk_container_add(GTK_CONTAINER(chargeFrame), chargeBox);

	GtkWidget* charge_en = helper.createButton(_("Enable Charging mode --- BROM"), "charge_en", nullptr, 0, 0, 272, 36);
	GtkWidget* charge_dis = helper.createButton(_("Disable Charging mode --- BROM"), "charge_dis", nullptr, 0, 0, 272, 36);

	GtkWidget* chargeButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
	gtk_widget_set_halign(chargeButtonBox, GTK_ALIGN_CENTER);
	gtk_box_pack_start(GTK_BOX(chargeButtonBox), charge_en, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(chargeButtonBox), charge_dis, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(chargeBox), chargeButtonBox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(mainBox), chargeFrame, FALSE, FALSE, 0);

	// 5. 发送结束数据部分
	GtkWidget* endDataFrame = gtk_frame_new(NULL);
	GtkWidget* endDataTitle = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(endDataTitle), (std::string("<b>") + _("Send End Data") + "</b>").c_str());
	gtk_widget_set_halign(endDataTitle, GTK_ALIGN_CENTER);
	gtk_frame_set_label_widget(GTK_FRAME(endDataFrame), endDataTitle);
	gtk_frame_set_label_align(GTK_FRAME(endDataFrame), 0.5, 0.5);
	helper.addWidget("end_data_label", endDataTitle);

	GtkWidget* endDataBoxItem = makeCardBox(32, 16);
	gtk_container_add(GTK_CONTAINER(endDataFrame), endDataBoxItem);

	GtkWidget* end_data_en_btn = helper.createButton(_("Enable sending end data"), "end_data_en", nullptr, 0, 0, 272, 36);
	GtkWidget* end_data_dis_btn = helper.createButton(_("Disable sending end data"), "end_data_dis", nullptr, 0, 0, 272, 36);

	GtkWidget* endDataButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
	gtk_widget_set_halign(endDataButtonBox, GTK_ALIGN_CENTER);
	gtk_box_pack_start(GTK_BOX(endDataButtonBox), end_data_en_btn, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(endDataButtonBox), end_data_dis_btn, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(endDataBoxItem), endDataButtonBox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(mainBox), endDataFrame, FALSE, FALSE, 0);

	// 6. 操作超时时间部分
	GtkWidget* timeoutFrame = gtk_frame_new(NULL);
	GtkWidget* timeoutTitle = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(timeoutTitle), (std::string("<b>") + _("Operation timeout") + "</b>").c_str());
	gtk_widget_set_halign(timeoutTitle, GTK_ALIGN_CENTER);
	gtk_frame_set_label_widget(GTK_FRAME(timeoutFrame), timeoutTitle);
	gtk_frame_set_label_align(GTK_FRAME(timeoutFrame), 0.5, 0.5);
	helper.addWidget("timeout_label", timeoutTitle);

	GtkWidget* timeoutBox = makeCardBox(32, 16);
	gtk_container_add(GTK_CONTAINER(timeoutFrame), timeoutBox);

	GtkWidget* timeout_op = helper.createSpinButton(3000, 300000, 1, "timeout", 3000, 0, 0, 160, 36);
	GtkWidget* timeoutWrap = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
	gtk_widget_set_halign(timeoutWrap, GTK_ALIGN_CENTER);
	gtk_box_pack_start(GTK_BOX(timeoutWrap), timeout_op, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(timeoutBox), timeoutWrap, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(mainBox), timeoutFrame, FALSE, FALSE, 0);

	// 7. A/B分区设置部分
	GtkWidget* abpartFrame = gtk_frame_new(NULL);
	GtkWidget* abpartTitle = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(abpartTitle), (std::string("<b>") + _("A/B Part read/flash manually set --- FDL2") + "</b>").c_str());
	gtk_widget_set_halign(abpartTitle, GTK_ALIGN_CENTER);
	gtk_frame_set_label_widget(GTK_FRAME(abpartFrame), abpartTitle);
	gtk_frame_set_label_align(GTK_FRAME(abpartFrame), 0.5, 0.5);
	helper.addWidget("abpart_label", abpartTitle);
	
	GtkWidget* abpartBox = makeCardBox(32, 16);
	gtk_container_add(GTK_CONTAINER(abpartFrame), abpartBox);

	GtkWidget* abpart_auto = helper.createButton(_("Not VAB --- FDL2"),"abpart_auto",nullptr,0,0,176,36);
	GtkWidget* abpart_a = helper.createButton(_("A Parts --- FDL2"),"abpart_a",nullptr,0,0,176,36);
	GtkWidget* abpart_b = helper.createButton(_("B Parts --- FDL2"),"abpart_b",nullptr,0,0,176,36);
	
	GtkWidget* abpartButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
	gtk_widget_set_halign(abpartButtonBox, GTK_ALIGN_CENTER);
	gtk_box_pack_start(GTK_BOX(abpartButtonBox), abpart_auto,FALSE,FALSE,0);
	gtk_box_pack_start(GTK_BOX(abpartButtonBox), abpart_a,FALSE,FALSE,0);
	gtk_box_pack_start(GTK_BOX(abpartButtonBox), abpart_b, FALSE,FALSE,0);

	gtk_box_pack_start(GTK_BOX(abpartBox), abpartButtonBox,FALSE,FALSE,0);
	gtk_box_pack_start(GTK_BOX(mainBox), abpartFrame, FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(advScroll), mainBox);
	helper.addToGrid(advSetPage, advScroll, 0, 0, 4, 6);

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
}
