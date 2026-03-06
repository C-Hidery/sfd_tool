#include "page_partition.h"
#include "../common.h"
#include "../main.h"
#include "../i18n.h"
#include <thread>
#include <iostream>

extern spdio_t* io;
extern int ret;
extern int m_bOpened;
extern int blk_size;
extern int isCMethod;
extern int gpt_failed;
extern int selected_ab;

void populatePartitionList(GtkWidgetHelper& helper, const std::vector<partition_t>& partitions) {
	GtkWidget* part_list = helper.getWidget("part_list");
	if (!part_list || !GTK_IS_TREE_VIEW(part_list)) {
		std::cerr << "part_list not found or not a TreeView" << std::endl;
		return;
	}

	GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(part_list));
	if (!model) {
		std::cerr << "TreeView model not found" << std::endl;
		return;
	}

	GtkListStore* store = GTK_LIST_STORE(model);
	gtk_list_store_clear(store);

	int index = 1;
	GtkTreeIter iter_spl;
	gtk_list_store_append(store, &iter_spl);
	long long spl_size = g_spl_size > 0 ? g_spl_size : 0;
	std::string display_name = std::to_string(index) + ". splloader";
	std::string size_str;
	if (spl_size < 1024) {
		size_str = std::to_string(spl_size) + " B";
	} else {
		size_str = std::to_string(spl_size / 1024) + " KB";
	}
	gtk_list_store_set(store, &iter_spl,
	                   0, display_name.c_str(),
	                   1, size_str.c_str(),
	                   2, "splloader",
	                   -1);

	index++;
	for (const auto& partition : partitions) {
		GtkTreeIter iter;
		gtk_list_store_append(store, &iter);

		std::string display_name = std::to_string(index) + ". " + partition.name;

		std::string size_str;
		if (partition.size < 1024) {
			size_str = std::to_string(partition.size) + " B";
		} else if (partition.size < 1024 * 1024) {
			size_str = std::to_string(partition.size / 1024) + " KB";
		} else if (partition.size < 1024 * 1024 * 1024) {
			size_str = std::to_string(partition.size / (1024 * 1024)) + " MB";
		} else {
			size_str = std::to_string(partition.size / (1024 * 1024 * 1024.0)) + " GB";
		}

		gtk_list_store_set(store, &iter,
		                   0, display_name.c_str(),
		                   1, size_str.c_str(),
		                   2, partition.name,
		                   -1);

		index++;
	}

	gtk_widget_queue_draw(part_list);
}

std::string getSelectedPartitionName(GtkWidgetHelper& helper) {
	GtkWidget* part_list = helper.getWidget("part_list");
	if (!part_list || !GTK_IS_TREE_VIEW(part_list)) {
		return "";
	}

	GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(part_list));
	GtkTreeModel* model;
	GtkTreeIter iter;

	if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
		gchar* original_name = nullptr;
		gtk_tree_model_get(model, &iter, 2, &original_name, -1);

		if (original_name) {
			std::string name = original_name;
			g_free(original_name);
			return name;
		}
	}

	return "";
}

// 分区操作回调函数 — 保留在 main.cpp 中引用
// 由于 on_button_clicked_list_write, list_read, list_erase, list_force_write,
// modify_part, modify_new_part, modify_rm_part, modify_ren_part, xml_get,
// backup_all, list_cancel 等函数与全局 io 状态高度耦合且数量众多，
// 暂时保留外部声明方式
extern void on_button_clicked_list_write(GtkWidgetHelper helper);
extern void on_button_clicked_list_force_write(GtkWidgetHelper helper);
extern void on_button_clicked_list_read(GtkWidgetHelper helper);
extern void on_button_clicked_list_erase(GtkWidgetHelper helper);
extern void on_button_clicked_backup_all(GtkWidgetHelper helper);
extern void on_button_clicked_list_cancel(GtkWidgetHelper helper);
extern void on_button_clicked_modify_part(GtkWidgetHelper helper);
extern void on_button_clicked_modify_new_part(GtkWidgetHelper helper);
extern void on_button_clicked_modify_rm_part(GtkWidgetHelper helper);
extern void on_button_clicked_modify_ren_part(GtkWidgetHelper helper);
extern void on_button_clicked_xml_get(GtkWidgetHelper helper);

GtkWidget* create_partition_page(GtkWidgetHelper& helper, GtkWidget* notebook) {
	// ── 页面根容器（垂直 Box，整体可滚动） ──
	GtkWidget* outerScroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(outerScroll),
	                               GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_hexpand(outerScroll, TRUE);
	gtk_widget_set_vexpand(outerScroll, TRUE);

	GtkWidget* mainBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_margin_start(mainBox, 16);
	gtk_widget_set_margin_end(mainBox, 16);
	gtk_widget_set_margin_top(mainBox, 10);
	gtk_widget_set_margin_bottom(mainBox, 10);
	gtk_container_add(GTK_CONTAINER(outerScroll), mainBox);

	// 把 outerScroll 作为 notebook 的 page
	helper.addWidget("part_page", outerScroll);
	helper.addNotebookPage(notebook, outerScroll, _("Partition Operation"));

	// ── 区块辅助 lambda：创建带内边距的垂直内容 box ──
	auto makeCardBox = [](int pad_h, int pad_v) -> GtkWidget* {
		GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
		gtk_widget_set_margin_start(box, pad_h);
		gtk_widget_set_margin_end(box, pad_h);
		gtk_widget_set_margin_top(box, pad_v);
		gtk_widget_set_margin_bottom(box, pad_v);
		return box;
	};

	// ══════════════════════════════════════════════
	//  第一部分：请选择一个分区
	// ══════════════════════════════════════════════
	GtkWidget* selectTitle = gtk_label_new(_("Please check a partition"));
	helper.addWidget("part_instruction", selectTitle);
	gtk_widget_set_halign(selectTitle, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_bottom(selectTitle, 6);
	gtk_box_pack_start(GTK_BOX(mainBox), selectTitle, FALSE, FALSE, 0);

	// 分区列表
	GtkWidget* listScroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_widget_set_size_request(listScroll, -1, 220);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(listScroll),
	                               GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_hexpand(listScroll, TRUE);

	GtkListStore* store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	GtkWidget* treeView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	gtk_widget_set_name(treeView, "part_list");
	helper.addWidget("part_list", treeView);
	GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeView), -1,
	        _("Partition Name"), renderer, "text", 0, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeView), -1,
	        _("Size"), renderer, "text", 1, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeView), -1,
	        _("Type"), renderer, "text", 2, NULL);
	gtk_container_add(GTK_CONTAINER(listScroll), treeView);
	gtk_box_pack_start(GTK_BOX(mainBox), listScroll, FALSE, FALSE, 0);

	// ── 操作标题 ──
	GtkWidget* opTitle = gtk_label_new(_("Operation"));
	helper.addWidget("op_label", opTitle);
	gtk_widget_set_halign(opTitle, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_top(opTitle, 10);
	gtk_widget_set_margin_bottom(opTitle, 6);
	gtk_box_pack_start(GTK_BOX(mainBox), opTitle, FALSE, FALSE, 0);

	// ── 按钮行 1：五个主操作按钮 ──
	GtkWidget* writeBtn    = helper.createButton(_("WRITE"),       "list_write",       nullptr, 0, 0, -1, 32);
	GtkWidget* writeFBtn   = helper.createButton(_("FORCE WRITE"), "list_force_write", nullptr, 0, 0, -1, 32);
	GtkWidget* readBtn     = helper.createButton(_("EXTRACT"),     "list_read",        nullptr, 0, 0, -1, 32);
	GtkWidget* eraseBtn    = helper.createButton(_("ERASE"),       "list_erase",       nullptr, 0, 0, -1, 32);
	GtkWidget* backupAllBtn = helper.createButton(_("Backup All"), "backup_all",       nullptr, 0, 0, -1, 32);

	GtkWidget* btnRow1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_halign(btnRow1, GTK_ALIGN_FILL);
	gtk_box_pack_start(GTK_BOX(btnRow1), writeBtn,     TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(btnRow1), writeFBtn,    TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(btnRow1), readBtn,      TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(btnRow1), eraseBtn,     TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(btnRow1), backupAllBtn, TRUE, TRUE, 0);
	gtk_widget_set_margin_bottom(btnRow1, 8);
	gtk_box_pack_start(GTK_BOX(mainBox), btnRow1, FALSE, FALSE, 0);

	// ── 按钮行 2：取消 + XML获取 ──
	GtkWidget* cancelBtn = helper.createButton(_("Cancel"), "list_cancel", nullptr, 0, 0, 100, 32);
	GtkWidget* xmlGetBtn = helper.createButton(_("Get partition table through scanning an Xml file"), "xml_get", nullptr, 0, 0, -1, 32);

	GtkWidget* btnRow2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_pack_start(GTK_BOX(btnRow2), cancelBtn, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(btnRow2), xmlGetBtn, TRUE,  TRUE,  0);
	gtk_widget_set_margin_bottom(btnRow2, 16);
	gtk_box_pack_start(GTK_BOX(mainBox), btnRow2, FALSE, FALSE, 0);

	// 设置按钮初始状态
	gtk_widget_set_sensitive(writeBtn,    FALSE);
	gtk_widget_set_sensitive(readBtn,     FALSE);
	gtk_widget_set_sensitive(eraseBtn,    FALSE);
	gtk_widget_set_sensitive(backupAllBtn, TRUE);
	gtk_widget_set_sensitive(cancelBtn,   TRUE);

	// ══════════════════════════════════════════════
	//  第二部分：修改分区表 大标题
	// ══════════════════════════════════════════════
	GtkWidget* modifyPageTitle = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(modifyPageTitle),
	    (std::string("<b>") + _("Modify Partition Table") + "</b>").c_str());
	helper.addWidget("modify_label", modifyPageTitle);
	gtk_widget_set_halign(modifyPageTitle, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_bottom(modifyPageTitle, 10);
	gtk_box_pack_start(GTK_BOX(mainBox), modifyPageTitle, FALSE, FALSE, 0);

	// ══════════════════════════════════════════════
	//  卡片1：修改分区大小
	// ══════════════════════════════════════════════
	GtkWidget* sizeFrame = gtk_frame_new(NULL);
	gtk_widget_set_margin_bottom(sizeFrame, 10);
	GtkWidget* sizeCardBox = makeCardBox(12, 10);
	gtk_container_add(GTK_CONTAINER(sizeFrame), sizeCardBox);

	// 卡片标题
	GtkWidget* sizeTitleLabel = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(sizeTitleLabel),
	    (std::string("<b>") + _("Change size") + "</b>").c_str());
	helper.addWidget("fff_label", sizeTitleLabel);
	gtk_widget_set_halign(sizeTitleLabel, GTK_ALIGN_START);
	gtk_box_pack_start(GTK_BOX(sizeCardBox), sizeTitleLabel, FALSE, FALSE, 0);

	// 卡片说明
	GtkWidget* sizeDescLabel = gtk_label_new(_("Please check a partition you want to change"));
	gtk_widget_set_halign(sizeDescLabel, GTK_ALIGN_START);
	gtk_widget_set_margin_bottom(sizeDescLabel, 6);
	gtk_box_pack_start(GTK_BOX(sizeCardBox), sizeDescLabel, FALSE, FALSE, 0);

	// 控件行
	GtkWidget* sizeCtrlRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget* SeLabel     = gtk_label_new(_("Second-change partition"));
	helper.addWidget("second_part_label", SeLabel);
	GtkWidget* secondPart  = helper.createEntry("modify_second_part", "", false, 0, 0, 180, 32);
	GtkWidget* newSizeLabel = gtk_label_new(_("New size (MB)"));
	helper.addWidget("new_size_label", newSizeLabel);
	GtkWidget* newSizeEntry = helper.createEntry("modify_new_size", "", false, 0, 0, 120, 32);
	GtkWidget* modifyBtn    = helper.createButton(_("Modify"), "modify_part", nullptr, 0, 0, 80, 32);
	gtk_box_pack_start(GTK_BOX(sizeCtrlRow), SeLabel,     FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(sizeCtrlRow), secondPart,  FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(sizeCtrlRow), newSizeLabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(sizeCtrlRow), newSizeEntry, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(sizeCtrlRow), modifyBtn,   FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(sizeCardBox), sizeCtrlRow, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(mainBox), sizeFrame, FALSE, FALSE, 0);

	// ══════════════════════════════════════════════
	//  卡片2：添加分区
	// ══════════════════════════════════════════════
	GtkWidget* addFrame = gtk_frame_new(NULL);
	gtk_widget_set_margin_bottom(addFrame, 10);
	GtkWidget* addCardBox = makeCardBox(12, 10);
	gtk_container_add(GTK_CONTAINER(addFrame), addCardBox);

	GtkWidget* addTitleLabel = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(addTitleLabel),
	    (std::string("<b>") + _("Add partition") + "</b>").c_str());
	helper.addWidget("ff_label", addTitleLabel);
	gtk_widget_set_halign(addTitleLabel, GTK_ALIGN_START);
	gtk_box_pack_start(GTK_BOX(addCardBox), addTitleLabel, FALSE, FALSE, 0);

	GtkWidget* addDescLabel = gtk_label_new(_("Enter partition name:"));
	gtk_widget_set_halign(addDescLabel, GTK_ALIGN_START);
	gtk_widget_set_margin_bottom(addDescLabel, 6);
	gtk_box_pack_start(GTK_BOX(addCardBox), addDescLabel, FALSE, FALSE, 0);

	GtkWidget* addCtrlRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget* Add2Label   = gtk_label_new(_("Part name:"));
	helper.addWidget("f_label", Add2Label);
	GtkWidget* newPartEntry = helper.createEntry("new_part", "", false, 0, 0, 160, 32);
	GtkWidget* new2SizeLabel = gtk_label_new(_("Size (MB):"));
	helper.addWidget("f3_label", new2SizeLabel);
	GtkWidget* newPartSize = helper.createEntry("modify_add_size", "", false, 0, 0, 100, 32);
	GtkWidget* afterPartLabel = gtk_label_new(_("Part after this new part:"));
	helper.addWidget("after_part_label", afterPartLabel);
	GtkWidget* afterPart      = helper.createEntry("before_new_part", "", false, 0, 0, 120, 32);
	GtkWidget* addNewPartBtn  = helper.createButton(_("Modify"), "modify_new_part", nullptr, 0, 0, 80, 32);
	gtk_box_pack_start(GTK_BOX(addCtrlRow), Add2Label,     FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(addCtrlRow), newPartEntry,  FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(addCtrlRow), new2SizeLabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(addCtrlRow), newPartSize,   FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(addCtrlRow), afterPartLabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(addCtrlRow), afterPart,     FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(addCtrlRow), addNewPartBtn, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(addCardBox), addCtrlRow, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(mainBox), addFrame, FALSE, FALSE, 0);

	// ══════════════════════════════════════════════
	//  卡片3：移除分区
	// ══════════════════════════════════════════════
	GtkWidget* rmFrame = gtk_frame_new(NULL);
	gtk_widget_set_margin_bottom(rmFrame, 10);
	GtkWidget* rmCardBox = makeCardBox(12, 10);
	gtk_container_add(GTK_CONTAINER(rmFrame), rmCardBox);

	GtkWidget* rmTitleLabel = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(rmTitleLabel),
	    (std::string("<b>") + _("Remove partition") + "</b>").c_str());
	helper.addWidget("ffff_label", rmTitleLabel);
	gtk_widget_set_halign(rmTitleLabel, GTK_ALIGN_START);
	gtk_box_pack_start(GTK_BOX(rmCardBox), rmTitleLabel, FALSE, FALSE, 0);

	GtkWidget* rmDescLabel = gtk_label_new(_("Please check a partition you want to remove"));
	gtk_widget_set_halign(rmDescLabel, GTK_ALIGN_START);
	gtk_widget_set_margin_bottom(rmDescLabel, 6);
	gtk_box_pack_start(GTK_BOX(rmCardBox), rmDescLabel, FALSE, FALSE, 0);

	// 删除按钮（红色样式）
	GtkWidget* RemvPartBtn = helper.createButton(_("Delete"), "modify_rm_part", nullptr, 0, 0, 80, 32);
	gtk_widget_set_name(RemvPartBtn, "danger_button");
	gtk_style_context_add_class(gtk_widget_get_style_context(RemvPartBtn), "destructive-action");
	gtk_widget_set_halign(RemvPartBtn, GTK_ALIGN_START);
	gtk_box_pack_start(GTK_BOX(rmCardBox), RemvPartBtn, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(mainBox), rmFrame, FALSE, FALSE, 0);

	// ══════════════════════════════════════════════
	//  卡片4：重命名分区
	// ══════════════════════════════════════════════
	GtkWidget* renFrame = gtk_frame_new(NULL);
	gtk_widget_set_margin_bottom(renFrame, 10);
	GtkWidget* renCardBox = makeCardBox(12, 10);
	gtk_container_add(GTK_CONTAINER(renFrame), renCardBox);

	GtkWidget* renTitleLabel = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(renTitleLabel),
	    (std::string("<b>") + _("Rename partition") + "</b>").c_str());
	helper.addWidget("f2_label", renTitleLabel);
	gtk_widget_set_halign(renTitleLabel, GTK_ALIGN_START);
	gtk_box_pack_start(GTK_BOX(renCardBox), renTitleLabel, FALSE, FALSE, 0);

	GtkWidget* renDescLabel = gtk_label_new(_("Please check a partition you want to rename"));
	gtk_widget_set_halign(renDescLabel, GTK_ALIGN_START);
	gtk_widget_set_margin_bottom(renDescLabel, 6);
	gtk_box_pack_start(GTK_BOX(renCardBox), renDescLabel, FALSE, FALSE, 0);

	GtkWidget* renCtrlRow   = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget* RenmPartLabel = gtk_label_new(_("New name"));
	helper.addWidget("f4_label", RenmPartLabel);
	GtkWidget* RenmPartEntry = helper.createEntry("modify_rename_part", "", false, 0, 0, 200, 32);
	GtkWidget* RenmPartBtn   = helper.createButton(_("Modify"), "modify_ren_part", nullptr, 0, 0, 80, 32);
	gtk_box_pack_start(GTK_BOX(renCtrlRow), RenmPartLabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(renCtrlRow), RenmPartEntry, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(renCtrlRow), RenmPartBtn,   FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(renCardBox), renCtrlRow, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(mainBox), renFrame, FALSE, FALSE, 0);

	gtk_widget_show_all(outerScroll);
	return outerScroll;
}

void bind_partition_signals(GtkWidgetHelper& helper) {
	helper.bindClick(helper.getWidget("list_write"), [&]() {
		on_button_clicked_list_write(helper);
	});
	helper.bindClick(helper.getWidget("list_read"), [&]() {
		on_button_clicked_list_read(helper);
	});
	helper.bindClick(helper.getWidget("list_erase"), [&]() {
		on_button_clicked_list_erase(helper);
	});
	helper.bindClick(helper.getWidget("backup_all"),[&](){
		on_button_clicked_backup_all(helper);
	});
	helper.bindClick(helper.getWidget("list_cancel"), [&]() {
		on_button_clicked_list_cancel(helper);
	});
	helper.bindClick(helper.getWidget("list_force_write"), [&]() {
		on_button_clicked_list_force_write(helper);
	});
	helper.bindClick(helper.getWidget("modify_part"),[&]() {
		on_button_clicked_modify_part(helper);
	});
	helper.bindClick(helper.getWidget("modify_new_part"),[&](){
		on_button_clicked_modify_new_part(helper);
	});
	helper.bindClick(helper.getWidget("modify_rm_part"),[&](){
		on_button_clicked_modify_rm_part(helper);
	});
	helper.bindClick(helper.getWidget("modify_ren_part"),[&](){
		on_button_clicked_modify_ren_part(helper);
	});
	helper.bindClick(helper.getWidget("xml_get"),[&](){
		on_button_clicked_xml_get(helper);
	});
}
