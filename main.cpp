#include <iostream>
#include <cstring>
#include "main_console.cpp"
#include "GtkWidgetHelper.hpp"
#include <thread>
#include <chrono>
#include <gtk/gtk.h>
char* open_file_dialog (GtkWidget *widget, gpointer data) {
    GtkWidget *dialog;
    GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;
    gint res;

    // 创建文件选择对话框
    dialog = gtk_file_chooser_dialog_new ("Open a file 打开文件",
                                          GTK_WINDOW (data),
                                          action,
                                          "_Cancel取消",
                                          GTK_RESPONSE_CANCEL,
                                          "_Open打开",
                                          GTK_RESPONSE_ACCEPT,
                                          NULL);

    // 运行对话框
	char *filename;
    res = gtk_dialog_run (GTK_DIALOG (dialog));
    if (res == GTK_RESPONSE_ACCEPT) {
        
        GtkFileChooser *chooser = GTK_FILE_CHOOSER (dialog);
        filename = gtk_file_chooser_get_filename (chooser);
        g_print ("选中的文件: %s\n", filename);
       
    }

    // 销毁对话框
    gtk_widget_destroy (dialog);
	return filename;
}

class WidgetHandler {
public:
    WidgetHandler() {}
    
    void Button_connect_clicked() {
        std::thread([](){
            std::cout << "Connect button clicked!" << std::endl;
            // Simulate a long operation
            std::this_thread::sleep_for(std::chrono::seconds(2));
            std::cout << "Device connected." << std::endl;
        }).detach();
    }
};
int gtk_kmain(int argc, char** argv) {
    DEG_LOG(I, "Starting GUI mode...");
    gtk_init(&argc, &argv);
    WidgetHandler handler = WidgetHandler();
    // Window Setup
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "SFD Tool GUI By Ryan Crepa");
    gtk_window_set_default_size(GTK_WINDOW(window), 1174, 765);
    
    // 设置关闭信号
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    
    // 创建主网格布局
    GtkWidget* mainGrid = gtk_grid_new();
    
    // 创建 GtkWidgetHelper
    GtkWidgetHelper helper(window);
    helper.setParent(window, LayoutType::GRID);
    
    // 创建Notebook（标签页控件）
    GtkWidget* notebook = helper.createNotebook("main_notebook", 0, 0, 1174, 672);
    
    // ========== Connect Page ==========
{
    GtkWidget* connectPage = helper.createGrid("connect_page", 5, 5);
    helper.addNotebookPage(notebook, connectPage, "Connect  连接");
    
    // Welcome labels
    GtkWidget* welcomeLabel1 = helper.createLabel("Welcome to SFD Tool GUI!", "welcome_en", 0, 0, 467, 28);
    GtkWidget* welcomeLabel2 = helper.createLabel("欢迎使用SFD Tool GUI!", "welcome_cn", 0, 30, 400, 28);
    GtkWidget* ti_c = helper.createLabel("请将你的设备连接到BROM模式", "ti_c", 0, 60, 400, 28);
    GtkWidget* ti_e = helper.createLabel("Please connect your device with BROM mode", "ti_e", 0, 90, 500, 28);
    
    // 设置字体大小
    PangoAttrList* attr_list = pango_attr_list_new();
    PangoAttribute* attr = pango_attr_size_new(20 * PANGO_SCALE);
    pango_attr_list_insert(attr_list, attr);
    gtk_label_set_attributes(GTK_LABEL(welcomeLabel1), attr_list);
    gtk_label_set_attributes(GTK_LABEL(welcomeLabel2), attr_list);
    gtk_label_set_attributes(GTK_LABEL(ti_c), attr_list);
    gtk_label_set_attributes(GTK_LABEL(ti_e), attr_list);
    
    // 连接说明
    GtkWidget* instruction1 = helper.createLabel("Press and hold the volume up or down keys and the power key to connect", 
                                                "instruction1", 0, 120, 600, 20);
    GtkWidget* instruction2 = helper.createLabel("按住音量增大或减小键和电源键进行连接", 
                                                "instruction2", 0, 140, 400, 20);
    
    // FDL Settings section
    GtkWidget* fdlSettings = helper.createLabel("FDL Send Settings", "fdl_settings", 0, 170, 150, 20);
    GtkWidget* fdlSettingsCn = helper.createLabel("FDL发送设置", "fdl_settings_cn", 0, 190, 150, 20);
    
    // FDL File Path
    GtkWidget* fdlLabel = helper.createLabel("FDL File Path  FDL文件路径 :", "fdl_label", 0, 220, 200, 20);
    GtkWidget* fdlFilePath = helper.createEntry("fdl_file_path", "", false, 200, 215, 275, 32);
    GtkWidget* selectFdlBtn = helper.createButton("...", "select_fdl", nullptr, 485, 215, 40, 32);
    
    // FDL Address
    GtkWidget* fdlAddrLabel = helper.createLabel("FDL Send Address  FDL发送地址 ：", "fdl_addr_label", 0, 260, 220, 20);
    GtkWidget* fdlAddr = helper.createEntry("fdl_addr", "", false, 220, 255, 185, 32);
    
    // Execute button
    GtkWidget* fdlExecBtn = helper.createButton("Execute   执行", "fdl_exec", nullptr, 0, 300, 157, 32);
    
    // Advanced Options - 放在左边
    GtkWidget* advLabel = helper.createLabel("Advanced   高级选项", "adv_label", 0, 350, 150, 20);
    
    // CVE Toggle Switch - 放在左边
    GtkWidget* cveSwitchBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* cveSwitch = gtk_switch_new();
    gtk_widget_set_name(cveSwitch, "exec_addr");
    gtk_box_pack_start(GTK_BOX(cveSwitchBox), cveSwitch, FALSE, FALSE, 0);
    GtkWidget* cveSwitchLabel = helper.createLabel("Try to use CVE to skip FDL verification(brom stage only)   利用漏洞绕过FDL签名验证(仅BROM模式)", 
                                                   "exec_addr_label", 0, 0, 500, 20);
    gtk_box_pack_start(GTK_BOX(cveSwitchBox), cveSwitchLabel, FALSE, FALSE, 0);
    
    // CVE Binary File - 放在左边，标签在右边，输入框在左边
    GtkWidget* cveAddrBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* cveAddr = helper.createEntry("cve_addr", "", false, 0, 0, 295, 32);
    GtkWidget* cveLabel = helper.createLabel("CVE Binary File Address CVE可执行镜像", "cve_label", 0, 0, 270, 20);
    gtk_box_pack_start(GTK_BOX(cveAddrBox), cveAddr, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(cveAddrBox), cveLabel, FALSE, FALSE, 0);
    GtkWidget* selectCveBtn = helper.createButton("...", "select_cve", nullptr, 0, 0, 40, 32);
    gtk_box_pack_start(GTK_BOX(cveAddrBox), selectCveBtn, FALSE, FALSE, 0);
    
    // SPRD4 Toggle Switch - 放在右边
    GtkWidget* sprd4SwitchBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* sprd4Switch = gtk_switch_new();
    gtk_widget_set_name(sprd4Switch, "sprd4");
    gtk_box_pack_start(GTK_BOX(sprd4SwitchBox), sprd4Switch, FALSE, FALSE, 0);
    GtkWidget* sprd4Label = helper.createLabel("Kick device to SPRD4  使用SPRD4模式", 
                                               "sprd4_label", 0, 0, 250, 20);
    gtk_box_pack_start(GTK_BOX(sprd4SwitchBox), sprd4Label, FALSE, FALSE, 0);
    
    // Addr 地址 - 放在右边，在SPRD4开关下面
    GtkWidget* addrBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* cveAddrLabel2 = helper.createLabel("Addr  地址", "cve_addr_label2", 0, 0, 70, 20);
    GtkWidget* cveAddrC = helper.createEntry("cve_addr_c", "", false, 0, 0, 120, 32);
    gtk_box_pack_start(GTK_BOX(addrBox), cveAddrLabel2, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(addrBox), cveAddrC, FALSE, FALSE, 0);
    
    // Connect Button - 放在右边
    GtkWidget* connectBtn = helper.createButton("CONNECT  连接", "connect_1", nullptr, 0, 0, 143, 52);
    
    // Wait connection time - 放在右边
    GtkWidget* waitBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* waitLabel = helper.createLabel("Wait connection time  连接等待时间 (s):", "wait_label", 0, 0, 250, 20);
    GtkWidget* waitCon = helper.createSpinButton(1, 120, 1, "wait_con", 30, 0, 0, 120, 32);
    gtk_box_pack_start(GTK_BOX(waitBox), waitLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(waitBox), waitCon, FALSE, FALSE, 0);
    
    // Status labels - 放在底部
    GtkWidget* statusLabel = helper.createLabel("Status : ", "status_label", 0, 0, 70, 24);
    GtkWidget* conStatus = helper.createLabel("Not connected", "con", 0, 0, 150, 23);
    GtkWidget* modeLabel = helper.createLabel("  Mode : ", "mode_label", 0, 0, 50, 19);
    GtkWidget* modeStatus = helper.createLabel("BROM Not connected!!!", "mode", 0, 0, 200, 19);
    
    // Add all widgets to connect page grid
    // 使用4列网格：0-3列
    // 左边区域：0-2列，右边区域：3列
    
    // 欢迎信息（横跨所有列）
    helper.addToGrid(connectPage, welcomeLabel1, 0, 0, 4, 1);
    helper.addToGrid(connectPage, welcomeLabel2, 0, 1, 4, 1);
    helper.addToGrid(connectPage, ti_c, 0, 2, 4, 1);
    helper.addToGrid(connectPage, ti_e, 0, 3, 4, 1);
    
    // 连接说明
    helper.addToGrid(connectPage, instruction1, 0, 4, 4, 1);
    helper.addToGrid(connectPage, instruction2, 0, 5, 4, 1);
    
    // FDL设置部分
    helper.addToGrid(connectPage, fdlSettings, 0, 6, 2, 1);
    helper.addToGrid(connectPage, fdlSettingsCn, 0, 7, 2, 1);
    
    // FDL文件路径
    helper.addToGrid(connectPage, fdlLabel, 0, 8, 1, 1);
    helper.addToGrid(connectPage, fdlFilePath, 1, 8, 1, 1);
    helper.addToGrid(connectPage, selectFdlBtn, 2, 8, 1, 1);
    
    // FDL地址
    helper.addToGrid(connectPage, fdlAddrLabel, 0, 9, 1, 1);
    helper.addToGrid(connectPage, fdlAddr, 1, 9, 2, 1);
    
    // 执行按钮
    helper.addToGrid(connectPage, fdlExecBtn, 0, 10, 2, 1);
    
    // 高级选项标题
    helper.addToGrid(connectPage, advLabel, 0, 11, 2, 1);
    
    // CVE开关
    helper.addToGrid(connectPage, cveSwitchBox, 0, 12, 3, 1);
    
    // CVE文件地址 - 注意：输入框在标签左边
    helper.addToGrid(connectPage, cveAddrBox, 0, 13, 3, 1);
    
    // ========== 右边区域 ==========
    // SPRD4开关
    helper.addToGrid(connectPage, sprd4SwitchBox, 3, 12, 1, 1);
    
    // Addr 地址标签和输入框
    helper.addToGrid(connectPage, addrBox, 3, 13, 1, 1);
    
    // 连接按钮
    helper.addToGrid(connectPage, connectBtn, 3, 14, 1, 1);
    
    // 等待时间
    helper.addToGrid(connectPage, waitBox, 3, 15, 1, 1);
    
    // ========== 底部状态信息 ==========
    // 状态行
    GtkWidget* statusBox1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(statusBox1), statusLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(statusBox1), conStatus, FALSE, FALSE, 0);
    helper.addToGrid(connectPage, statusBox1, 0, 16, 2, 1);
    
    // 模式行
    GtkWidget* statusBox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(statusBox2), modeLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(statusBox2), modeStatus, FALSE, FALSE, 0);
    helper.addToGrid(connectPage, statusBox2, 0, 17, 2, 1);
}
    
    // ========== Partition Operation Page ==========
    {
        GtkWidget* partPage = helper.createGrid("part_page", 5, 5);
        helper.addNotebookPage(notebook, partPage, "Partition Operation  分区操作");
        
        GtkWidget* instruction = helper.createLabel("Please check a partition        请选择一个分区", "part_instruction", 0, 0, 300, 20);
        
        // ListView for partitions
        GtkWidget* scrolledWindow = gtk_scrolled_window_new(NULL, NULL);
        gtk_widget_set_size_request(scrolledWindow, 1000, 450);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledWindow),
                                      GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        
        GtkListStore* store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
        GtkWidget* treeView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
        gtk_widget_set_name(treeView, "part_list");
        
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
        GtkWidget* opLabel = helper.createLabel("Operation    操作：", "op_label", 0, 0, 150, 20);
        GtkWidget* writeBtn = helper.createButton("WRITE  刷写", "list_write", nullptr, 0, 0, 117, 32);
        GtkWidget* readBtn = helper.createButton("EXTRACT  读取分区", "list_read", nullptr, 0, 0, 162, 32);
        GtkWidget* eraseBtn = helper.createButton("ERASE  擦除分区", "list_erase", nullptr, 0, 0, 170, 32);
        
        // Add to grid
        helper.addToGrid(partPage, instruction, 0, 0, 4, 1);
        helper.addToGrid(partPage, scrolledWindow, 0, 1, 4, 8);
        helper.addToGrid(partPage, opLabel, 0, 9, 4, 1);
        
        // Button row
        GtkWidget* buttonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_box_pack_start(GTK_BOX(buttonBox), writeBtn, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(buttonBox), readBtn, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(buttonBox), eraseBtn, FALSE, FALSE, 0);
        helper.addToGrid(partPage, buttonBox, 0, 10, 4, 1);
    }
    
    // ========== Manually Operate Page ==========
    {
        GtkWidget* manualPage = helper.createGrid("manual_page", 5, 5);
        helper.addNotebookPage(notebook, manualPage, "Manually Operate  手动操作");
        
        // Write partition section
        GtkWidget* writeLabel = helper.createLabel("Write partition   刷写分区", "write_label", 0, 0, 200, 20);
        GtkWidget* writePartLabel = helper.createLabel("Partition name  分区名：", "write_part_label", 0, 0, 150, 20);
        GtkWidget* mPartFlash = helper.createEntry("m_part_flash", "", false, 0, 0, 155, 32);
        
        GtkWidget* filePathLabel = helper.createLabel("Image file path  镜像文件地址：", "file_path_label", 0, 0, 200, 20);
        GtkWidget* mFilePath = helper.createEntry("m_file_path", "", false, 0, 0, 245, 32);
        GtkWidget* mSelectBtn = helper.createButton("...", "m_select", nullptr, 0, 0, 40, 32);
        
        GtkWidget* mWriteBtn = helper.createButton("WRITE   刷写", "m_write", nullptr, 0, 0, 120, 32);
        
        // Separator
        GtkWidget* sep1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        
        // Extract partition section
        GtkWidget* extractLabel = helper.createLabel("Extract partition  读取分区", "extract_label", 0, 0, 200, 20);
        GtkWidget* extractPartLabel = helper.createLabel("Partition name  分区名：", "extract_part_label", 0, 0, 150, 20);
        GtkWidget* mPartRead = helper.createEntry("m_part_read", "", false, 0, 0, 145, 32);
        
        GtkWidget* mReadBtn = helper.createButton("EXTRACT  读取", "m_read", nullptr, 0, 0, 120, 32);
        
        // Separator
        GtkWidget* sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        
        // Erase partition section
        GtkWidget* eraseLabel = helper.createLabel("Erase partition  擦除分区", "erase_label", 0, 0, 200, 20);
        GtkWidget* erasePartLabel = helper.createLabel("Partition name  分区名：", "erase_part_label", 0, 0, 150, 20);
        GtkWidget* mPartErase = helper.createEntry("m_part_erase", "", false, 0, 0, 150, 32);
        
        GtkWidget* mEraseBtn = helper.createButton("ERASE  擦除", "m_erase", nullptr, 0, 0, 120, 32);
        
        // Add to grid
        int row = 0;
        helper.addToGrid(manualPage, writeLabel, 0, row++, 3, 1);
        helper.addToGrid(manualPage, writePartLabel, 0, row, 1, 1);
        helper.addToGrid(manualPage, mPartFlash, 1, row++, 2, 1);
        
        helper.addToGrid(manualPage, filePathLabel, 0, row, 1, 1);
        helper.addToGrid(manualPage, mFilePath, 1, row, 2, 1);
        helper.addToGrid(manualPage, mSelectBtn, 3, row++, 1, 1);
        
        helper.addToGrid(manualPage, mWriteBtn, 0, row++, 3, 1);
        
        // Add separator
        row++;
        helper.addToGrid(manualPage, sep1, 0, row++, 4, 1);
        row++;
        
        helper.addToGrid(manualPage, extractLabel, 0, row++, 3, 1);
        helper.addToGrid(manualPage, extractPartLabel, 0, row, 1, 1);
        helper.addToGrid(manualPage, mPartRead, 1, row++, 2, 1);
        
        helper.addToGrid(manualPage, mReadBtn, 0, row++, 3, 1);
        
        // Add separator
        row++;
        helper.addToGrid(manualPage, sep2, 0, row++, 4, 1);
        row++;
        
        helper.addToGrid(manualPage, eraseLabel, 0, row++, 3, 1);
        helper.addToGrid(manualPage, erasePartLabel, 0, row, 1, 1);
        helper.addToGrid(manualPage, mPartErase, 1, row++, 2, 1);
        
        helper.addToGrid(manualPage, mEraseBtn, 0, row++, 3, 1);
    }
    
    // ========== Advanced Operation Page ==========
    {
        GtkWidget* advOpPage = helper.createGrid("adv_op_page", 5, 5);
        helper.addNotebookPage(notebook, advOpPage, "Advanced Operation  高级操作");
        
        // A/B partition
        GtkWidget* abLabel = helper.createLabel("Toggle the A/B partition boot settings  切换A/B分区启动设置", "ab_label", 0, 0, 400, 20);
        GtkWidget* setActiveA = helper.createButton("Boot A partitons  启动A分区", "set_active_a", nullptr, 0, 0, 200, 32);
        GtkWidget* setActiveB = helper.createButton("Boot B partitions  启动B分区", "set_active_b", nullptr, 0, 0, 200, 32);
        
        // Repartition
        GtkWidget* repartLabel = helper.createLabel("Repartition  重新分区", "repart_label", 0, 0, 200, 20);
        GtkWidget* xmlLabel = helper.createLabel("XML part info file path  XML分区表文件路径", "xml_label", 0, 0, 300, 20);
        GtkWidget* xmlPath = helper.createEntry("xml_path", "", false, 0, 0, 374, 32);
        GtkWidget* selectXmlBtn = helper.createButton("...", "select_xml", nullptr, 0, 0, 40, 32);
        GtkWidget* startRepartBtn = helper.createButton("START  开始", "start_repart", nullptr, 0, 0, 120, 32);
        
        GtkWidget* readXmlBtn = helper.createButton("Extract part info to a XML file (if support)  备份分区表到XML文件（如果支持）", 
                                                   "read_xml", nullptr, 0, 0, 500, 32);
        
        // DM-verify
        GtkWidget* dmvLabel = helper.createLabel("DM-verify Settings (if support)  DM-verify设置（如果支持）", "dmv_label", 0, 0, 400, 20);
        GtkWidget* dmvDisable = helper.createButton("Disable DM-verify  禁用DM-verify", "dmv_disable", nullptr, 0, 0, 200, 32);
        GtkWidget* dmvEnable = helper.createButton("Enable DM-verify  启用DM-verify", "dmv_enable", nullptr, 0, 0, 200, 32);
        
        // Add to grid
        int row = 0;
        helper.addToGrid(advOpPage, abLabel, 0, row++, 3, 1);
        
        GtkWidget* abButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
        gtk_box_pack_start(GTK_BOX(abButtonBox), setActiveA, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(abButtonBox), setActiveB, FALSE, FALSE, 0);
        helper.addToGrid(advOpPage, abButtonBox, 0, row++, 3, 1);
        
        row += 2; // Add some spacing
        
        helper.addToGrid(advOpPage, repartLabel, 0, row++, 3, 1);
        helper.addToGrid(advOpPage, xmlLabel, 0, row++, 3, 1);
        
        GtkWidget* xmlBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_box_pack_start(GTK_BOX(xmlBox), xmlPath, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(xmlBox), selectXmlBtn, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(xmlBox), startRepartBtn, FALSE, FALSE, 0);
        helper.addToGrid(advOpPage, xmlBox, 0, row++, 3, 1);
        
        helper.addToGrid(advOpPage, readXmlBtn, 0, row++, 3, 1);
        
        row += 2; // Add some spacing
        
        helper.addToGrid(advOpPage, dmvLabel, 0, row++, 3, 1);
        
        GtkWidget* dmvButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
        gtk_box_pack_start(GTK_BOX(dmvButtonBox), dmvDisable, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(dmvButtonBox), dmvEnable, FALSE, FALSE, 0);
        helper.addToGrid(advOpPage, dmvButtonBox, 0, row++, 3, 1);
    }
    
    // ========== Advanced Settings Page ==========
    {
        GtkWidget* advSetPage = helper.createGrid("adv_set_page", 5, 5);
        helper.addNotebookPage(notebook, advSetPage, "Advanced Settings  高级设置");
        
        GtkWidget* blkLabel = helper.createLabel("Data block size  数据块大小", "blk_label", 0, 0, 200, 20);
        
        GtkWidget* blkSlider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 10000, 60000, 10000);
        gtk_range_set_value(GTK_RANGE(blkSlider), 60000);
        gtk_scale_set_draw_value(GTK_SCALE(blkSlider), TRUE);
        gtk_scale_set_value_pos(GTK_SCALE(blkSlider), GTK_POS_RIGHT);
        gtk_widget_set_name(blkSlider, "blk_size");
        gtk_widget_set_size_request(blkSlider, 1036, 30);
        
        GtkWidget* sizeCon = helper.createLabel("60000", "size_con", 0, 0, 60, 20);
        
        // Add to grid
        helper.addToGrid(advSetPage, blkLabel, 0, 0, 2, 1);
        
        GtkWidget* sliderBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_box_pack_start(GTK_BOX(sliderBox), blkSlider, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(sliderBox), sizeCon, FALSE, FALSE, 0);
        helper.addToGrid(advSetPage, sliderBox, 0, 1, 2, 1);
    }
    
    // ========== About Page ==========
    {
        GtkWidget* aboutPage = helper.createGrid("about_page", 5, 5);
        helper.addNotebookPage(notebook, aboutPage, "About  关于");
        
        GtkWidget* scrolledAbout = gtk_scrolled_window_new(NULL, NULL);
        gtk_widget_set_size_request(scrolledAbout, 1084, 557);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledAbout),
                                      GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        
        GtkWidget* aboutTextView = gtk_text_view_new();
        gtk_text_view_set_editable(GTK_TEXT_VIEW(aboutTextView), FALSE);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(aboutTextView), GTK_WRAP_WORD);
        gtk_widget_set_name(aboutTextView, "about_text");
        
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(aboutTextView));
        gtk_text_buffer_set_text(buffer, 
            "SFD Tool GUI\n\nVersion 1.7.1.0\n\nBy Ryan Crepa    QQ:3285087232    @Bilibili RyanCrepa\n\nVersion logs:\n\n---v 1.7.1.0---\nFirst GUI Version", -1);
        
        gtk_container_add(GTK_CONTAINER(scrolledAbout), aboutTextView);
        helper.addToGrid(aboutPage, scrolledAbout, 0, 0, 1, 1);
    }
    
    // ========== Log Page ==========
    {
        GtkWidget* logPage = helper.createGrid("log_page", 5, 5);
        helper.addNotebookPage(notebook, logPage, "Log  日志");
        
        GtkWidget* scrolledLog = gtk_scrolled_window_new(NULL, NULL);
        gtk_widget_set_size_request(scrolledLog, 1124, 500);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledLog),
                                      GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        
        GtkWidget* logTextView = gtk_text_view_new();
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(logTextView), GTK_WRAP_WORD);
        gtk_widget_set_name(logTextView, "txtOutput");
        
        gtk_container_add(GTK_CONTAINER(scrolledLog), logTextView);
        
        GtkWidget* expLogBtn = helper.createButton("Export  导出", "exp_log", nullptr, 0, 0, 120, 32);
        GtkWidget* logClearBtn = helper.createButton("Clear  清空", "log_clear", nullptr, 0, 0, 120, 32);
        
        // Add to grid
        helper.addToGrid(logPage, scrolledLog, 0, 0, 4, 8);
        
        GtkWidget* logButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 30);
        gtk_box_pack_start(GTK_BOX(logButtonBox), expLogBtn, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(logButtonBox), logClearBtn, FALSE, FALSE, 0);
        helper.addToGrid(logPage, logButtonBox, 0, 9, 4, 1);
    }
    
    // ========== Bottom Controls ==========
    {
        // Progress section
        GtkWidget* progressLabel = helper.createLabel("Progress  进度:", "progress_label", 0, 0, 100, 20);
        GtkWidget* progressBar = gtk_progress_bar_new();
        gtk_widget_set_name(progressBar, "progressBar_1");
        gtk_widget_set_size_request(progressBar, 345, 9);
        
        GtkWidget* percentLabel = helper.createLabel("0%", "percent", 0, 0, 30, 20);
        GtkWidget* speedLabel = helper.createLabel("0 MB/s", "speedtext", 0, 0, 80, 20);
        GtkWidget* timeLabel = helper.createLabel("Need 剩余: 0.00s", "timetext", 0, 0, 120, 20);
        
        // Control buttons
        GtkWidget* poweroffBtn = helper.createButton("POWEROFF  关机", "poweroff", nullptr, 0, 0, 130, 32);
        GtkWidget* rebootBtn = helper.createButton("REBOOT  重启", "reboot", nullptr, 0, 0, 110, 32);
        GtkWidget* recoveryBtn = helper.createButton("BOOT TO RECOVERY  重启到恢复模式", "recovery", nullptr, 0, 0, 260, 32);
        GtkWidget* fastbootBtn = helper.createButton("BOOT TO FASTBOOT  重启到线刷模式", "fastboot", nullptr, 0, 0, 260, 32);
        
        // Create bottom grid
        GtkWidget* bottomGrid = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(bottomGrid), 5);
        gtk_grid_set_column_spacing(GTK_GRID(bottomGrid), 10);
        
        // Add to bottom grid
        gtk_grid_attach(GTK_GRID(bottomGrid), progressLabel, 0, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(bottomGrid), progressBar, 0, 1, 1, 1);
        
        gtk_grid_attach(GTK_GRID(bottomGrid), percentLabel, 1, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(bottomGrid), speedLabel, 2, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(bottomGrid), timeLabel, 3, 0, 1, 1);
        
        gtk_grid_attach(GTK_GRID(bottomGrid), poweroffBtn, 4, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(bottomGrid), rebootBtn, 5, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(bottomGrid), recoveryBtn, 6, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(bottomGrid), fastbootBtn, 7, 0, 1, 1);
        
        // Add notebook and bottom grid to main grid
        gtk_grid_attach(GTK_GRID(mainGrid), notebook, 0, 0, 10, 1);
        gtk_grid_attach(GTK_GRID(mainGrid), bottomGrid, 0, 1, 10, 1);
    }
    
    // 创建CSS样式
    GtkCssProvider* provider = gtk_css_provider_new();
    const gchar* css = 
        "label.big-label { font-size: 20px; }"
        "progressbar { min-height: 9px; }";
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                             GTK_STYLE_PROVIDER(provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    
    gtk_container_add(GTK_CONTAINER(window), mainGrid);
    
    // 显示所有组件
    gtk_widget_show_all(window);
    // Bind signals
	{

	}
    // 启动GTK主循环
    gtk_main();
    return 0;
}
int main(int argc, char** argv) {
    if (argc > 1 && !strcmp(argv[1], "--no-gui")) {
        // Call the console version of main
        return main_console(argc-1, argv+1); // Skip the first argument
    }
    else {
        return gtk_kmain(argc, argv);
    }
}