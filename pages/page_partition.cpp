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
	GtkWidget* partPage = helper.createGrid("part_page", 5, 5);
	helper.addNotebookPage(notebook, partPage, _("Partition Operation"));

	GtkWidget* part_instruction_label = helper.createLabel(_("Please check a partition"), "part_instruction", 0, 0, 300, 20);

	// ListView for partitions
	GtkWidget* scrolledWindow = gtk_scrolled_window_new(NULL, NULL);
	gtk_widget_set_size_request(scrolledWindow, 1000, 300);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledWindow),
	                               GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	GtkListStore* store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	GtkWidget* treeView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	gtk_widget_set_name(treeView, "part_list");
	helper.addWidget("part_list", treeView);
	GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeView), -1,
	        "Partition Name", renderer,
	        "text", 0, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeView), -1,
	        "Size", renderer,
	        "text", 1, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeView), -1,
	        "Type", renderer,
	        "text", 2, NULL);

	gtk_container_add(GTK_CONTAINER(scrolledWindow), treeView);

	// Operation buttons
	GtkWidget* opLabel = helper.createLabel(_("Operation"), "op_label", 0, 0, 150, 20);
	GtkWidget* writeBtn = helper.createButton(_("WRITE"), "list_write", nullptr, 0, 0, 117, 32);
	GtkWidget* writeFBtn = helper.createButton(_("FORCE WRITE"), "list_force_write", nullptr, 0, 0, 162, 32);
	GtkWidget* readBtn = helper.createButton(_("EXTRACT"), "list_read", nullptr, 0, 0, 162, 32);
	GtkWidget* eraseBtn = helper.createButton(_("ERASE"), "list_erase", nullptr, 0, 0, 170, 32);
	GtkWidget* backupAllBtn = helper.createButton(_("Backup All"), "backup_all", nullptr, 0, 0, 180, 32);
	GtkWidget* cancelBtn = helper.createButton(_("Cancel"), "list_cancel", nullptr, 0, 0, 117, 32);
	GtkWidget* xmlGetBtn = helper.createButton(_("Get partition table through scanning an Xml file"), "xml_get", nullptr, 0, 0, 250, 32);
	
	// 修改分区表
	GtkWidget* ModifyLabel = helper.createLabel(_("Modify Partition Table"), "modify_label", 0, 0, 200, 20);
	GtkWidget* SedLabel = helper.createLabel(_("[Change size] Please check a partition you want to change"), "fff_label", 0, 0, 100, 20);
	GtkWidget* SeLabel = helper.createLabel(_("Second-change partition"), "second_part_label", 0, 0, 200, 20);
	GtkWidget* secondPart = helper.createEntry("modify_second_part", "", false, 0, 0, 200, 32);
	GtkWidget* newSizeLabel = helper.createLabel(_("New size in MB"), "new_size_label", 0, 0, 100, 20);
	GtkWidget* newSizeEntry = helper.createEntry("modify_new_size", "", false, 0, 0, 150, 32);
	GtkWidget* modifyBtn = helper.createButton(_("Modify"), "modify_part", nullptr, 0, 0, 117, 32);
	GtkWidget* AddLabel = helper.createLabel(_("[Add partition] Enter partition name:"),"ff_label",0,0,100,20);
	GtkWidget* Add2Label = helper.createLabel(_("Part name:"),"f_label",0,0,50,20);
	GtkWidget* newPartEntry = helper.createEntry("new_part","",false,0,0,200,32);
	GtkWidget* new2SizeLabel = helper.createLabel(_("Size(MB):"),"f3_label",0,0,100,20);
	GtkWidget* newPartSize = helper.createEntry("modify_add_size","",false,0,0,150,32);
	GtkWidget* afterPartLabel = helper.createLabel(_("Part after this new part:"),"",0,0,100,20);
	GtkWidget* afterPart = helper.createEntry("before_new_part","",false,0,0,100,32);
	GtkWidget* addNewPartBtn = helper.createButton(_("Modify"),"modify_new_part",nullptr,0,0,117,32);
	GtkWidget* RemvLabel = helper.createLabel(_("[Remove partition] Please check a partition you want to remove"),"ffff_label",0,0,250,20);
	GtkWidget* RemvPartBtn = helper.createButton(_("Modify"),"modify_rm_part",nullptr,0,0,117,32);
	GtkWidget* RenmLabel = helper.createLabel(_("[Rename partition] Please check a partition you want to rename"),"f2_label",0,0,250,20);
	GtkWidget* RenmPartLabel = helper.createLabel(_("New name"),"f4_label",0,0,100,20);
	GtkWidget* RenmPartEntry = helper.createEntry("modify_rename_part","",false,0,0,200,32);
	GtkWidget* RenmPartBtn = helper.createButton(_("Modify"),"modify_ren_part",nullptr,0,0,117,32);

	// 设置按钮初始状态
	gtk_widget_set_sensitive(writeBtn, FALSE);
	gtk_widget_set_sensitive(readBtn, FALSE);
	gtk_widget_set_sensitive(eraseBtn, FALSE);
	gtk_widget_set_sensitive(backupAllBtn, TRUE);
	gtk_widget_set_sensitive(cancelBtn, TRUE);

	// Add to grid
	helper.addToGrid(partPage, part_instruction_label, 0, 0, 5, 1);
	helper.addToGrid(partPage, scrolledWindow, 0, 1, 5, 8);
	helper.addToGrid(partPage, opLabel, 0, 9, 5, 1);

	GtkWidget* mainButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_box_pack_start(GTK_BOX(mainButtonBox), writeBtn, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(mainButtonBox), writeFBtn, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(mainButtonBox), readBtn, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(mainButtonBox), eraseBtn, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(mainButtonBox), backupAllBtn, FALSE, FALSE, 0);
	helper.addToGrid(partPage, mainButtonBox, 0, 10, 5, 1);

	GtkWidget* cancelButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_box_pack_start(GTK_BOX(cancelButtonBox), cancelBtn, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(cancelButtonBox), xmlGetBtn, FALSE, FALSE, 0);
	GtkWidget* placeholder1 = gtk_label_new("");
	GtkWidget* placeholder2 = gtk_label_new("");
	gtk_box_pack_start(GTK_BOX(cancelButtonBox), placeholder1, FALSE, FALSE, 117);
	gtk_box_pack_start(GTK_BOX(cancelButtonBox), placeholder2, FALSE, FALSE, 0);
	helper.addToGrid(partPage, cancelButtonBox, 0, 11, 5, 1);

	helper.addToGrid(partPage, ModifyLabel, 0, 12, 5, 1);
	helper.addToGrid(partPage, SedLabel, 0, 13, 5, 1);
	GtkWidget* modifySizeBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_box_pack_start(GTK_BOX(modifySizeBox), SeLabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(modifySizeBox), secondPart, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(modifySizeBox), newSizeLabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(modifySizeBox), newSizeEntry, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(modifySizeBox), modifyBtn, FALSE, FALSE, 0);
	helper.addToGrid(partPage, modifySizeBox, 0, 14, 5, 1);
	GtkWidget* modifyAddBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	helper.addToGrid(partPage, AddLabel, 0,15,5,1);
	gtk_box_pack_start(GTK_BOX(modifyAddBox), Add2Label, FALSE,FALSE,0);
	gtk_box_pack_start(GTK_BOX(modifyAddBox), newPartEntry, FALSE,FALSE,0);
	gtk_box_pack_start(GTK_BOX(modifyAddBox), new2SizeLabel, FALSE,FALSE,0);
	gtk_box_pack_start(GTK_BOX(modifyAddBox), newPartSize, FALSE,FALSE,0);
	gtk_box_pack_start(GTK_BOX(modifyAddBox), afterPartLabel,FALSE,FALSE,0);
	gtk_box_pack_start(GTK_BOX(modifyAddBox), afterPart,FALSE,FALSE,0);
	gtk_box_pack_start(GTK_BOX(modifyAddBox), addNewPartBtn, FALSE,FALSE,0);
	helper.addToGrid(partPage, modifyAddBox, 0, 16, 5, 1);
	GtkWidget* modifyRemvBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	helper.addToGrid(partPage, RemvLabel, 0,17,5,1);
	gtk_box_pack_start(GTK_BOX(modifyRemvBox), RemvPartBtn, FALSE,FALSE,0);
	helper.addToGrid(partPage, modifyRemvBox, 0,18,5,1);
	GtkWidget* modifyRenmBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,10);
	helper.addToGrid(partPage, RenmLabel, 0,19,5,1);
	gtk_box_pack_start(GTK_BOX(modifyRenmBox), RenmPartLabel, FALSE,FALSE,0);
	gtk_box_pack_start(GTK_BOX(modifyRenmBox), RenmPartEntry, FALSE,FALSE,0);
	gtk_box_pack_start(GTK_BOX(modifyRenmBox), RenmPartBtn, FALSE,FALSE,0);
	helper.addToGrid(partPage, modifyRenmBox, 0,20,5,1);

	return partPage;
}

void bind_partition_signals(GtkWidgetHelper& helper) {
	helper.bindClick(helper.getWidget("list_write"), []() {
		on_button_clicked_list_write(helper);
	});
	helper.bindClick(helper.getWidget("list_read"), []() {
		on_button_clicked_list_read(helper);
	});
	helper.bindClick(helper.getWidget("list_erase"), []() {
		on_button_clicked_list_erase(helper);
	});
	helper.bindClick(helper.getWidget("backup_all"),[](){
		on_button_clicked_backup_all(helper);
	});
	helper.bindClick(helper.getWidget("list_cancel"), []() {
		on_button_clicked_list_cancel(helper);
	});
	helper.bindClick(helper.getWidget("list_force_write"), []() {
		on_button_clicked_list_force_write(helper);
	});
	helper.bindClick(helper.getWidget("modify_part"),[]() {
		on_button_clicked_modify_part(helper);
	});
	helper.bindClick(helper.getWidget("modify_new_part"),[](){
		on_button_clicked_modify_new_part(helper);
	});
	helper.bindClick(helper.getWidget("modify_rm_part"),[](){
		on_button_clicked_modify_rm_part(helper);
	});
	helper.bindClick(helper.getWidget("modify_ren_part"),[](){
		on_button_clicked_modify_ren_part(helper);
	});
	helper.bindClick(helper.getWidget("xml_get"),[](){
		on_button_clicked_xml_get(helper);
	});
}
