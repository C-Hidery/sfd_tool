#include "page_pac_flash.h"
#include "../main.h"
#include <string>
#include <thread>

extern int m_bOpened;
extern int selected_ab;
extern int blk_size;
extern int isCMethod;
extern int waitFDL1;

// ===== 按钮回调函数 =====

void on_button_clicked_pac_time(GtkWidgetHelper helper) {
    if (m_bOpened == -1) {
        DEG_LOG(E, "device unattached, exiting...");
        gui_idle_call_wait_drag([helper]() {
            showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error 错误", "Device unattached, exiting...\n设备已断开连接！正在退出...");
            exit(1);
        }, GTK_WINDOW(helper.getWidget("main_window")));
        return;
    }
    extern spdio_t* io;
    uint64_t pt = read_pactime(io);
    std::string msg = "PAC flash time: " + std::to_string(pt) + " s";
    gui_idle_call_wait_drag([msg, helper]() {
        showInfoDialog(GTK_WINDOW(helper.getWidget("main_window")), "PAC Time", msg.c_str());
    }, GTK_WINDOW(helper.getWidget("main_window")));
}

void on_button_clicked_abpart_auto(GtkWidgetHelper helper) {
    (void)helper;
    selected_ab = 0;
}

void on_button_clicked_abpart_a(GtkWidgetHelper helper) {
    (void)helper;
    selected_ab = 1;
}

void on_button_clicked_abpart_b(GtkWidgetHelper helper) {
    (void)helper;
    selected_ab = 2;
}

void on_button_clicked_pac_select(GtkWidgetHelper helper) {
    GtkWindow* parent = GTK_WINDOW(helper.getWidget("main_window"));
    std::string filename = showFileChooser(parent, true);
    if (!filename.empty()) {
        helper.setEntryText(helper.getWidget("pac_file_path"), filename);
    }
}

void on_button_clicked_pac_unpack(GtkWidgetHelper helper) {
    if (m_bOpened == -1) {
        DEG_LOG(E, "device unattached, exiting...");
        gui_idle_call_wait_drag([helper]() {
            showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error 错误", "Device unattached, exiting...\n设备已断开连接！正在退出...");
            exit(1);
        }, GTK_WINDOW(helper.getWidget("main_window")));
        return;
    }
    const char* pac_path = helper.getEntryText(helper.getWidget("pac_file_path"));
    FILE *fi = oxfopen(pac_path, "r");
    if (fi == nullptr) {
        DEG_LOG(E, "File does not exist.");
        gui_idle_call_wait_drag([helper]() {
            showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error 错误", "File does not exist.\n文件不存在！");
        }, GTK_WINDOW(helper.getWidget("main_window")));
        return;
    }
    fclose(fi);
    if (!pac_extract(pac_path, "pac_unpack_output")) {
        gui_idle_call_wait_drag([helper]() {
            showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error 错误", "Failed to unpack PAC file.\nPAC文件解包失败！");
        }, GTK_WINDOW(helper.getWidget("main_window")));
        DEG_LOG(E, "Failed to unpack PAC file.");
    }
}

void on_button_clicked_pac_flash_start(GtkWidgetHelper helper) {
    extern spdio_t* io;
    if (m_bOpened == -1) {
        DEG_LOG(E, "device unattached, exiting...");
        gui_idle_call_wait_drag([helper]() {
            showErrorDialog(GTK_WINDOW(helper.getWidget("main_window")), "Error 错误", "Device unattached, exiting...\n设备已断开连接！正在退出...");
            exit(1);
        }, GTK_WINDOW(helper.getWidget("main_window")));
        return;
    }
    load_partitions(
        io,
        "pac_unpack_output",
        blk_size ? blk_size : DEFAULT_BLK_SIZE,
        selected_ab,
        isCMethod
    );
}

// ===== UI 构建 =====

GtkWidget* create_pac_flash_page(GtkWidgetHelper& helper, GtkWidget* notebook) {
    // 创建 PAC Flash 页面网格
    GtkWidget* pacFlashPage = helper.createGrid("pac_flash_page", 5, 5);
    helper.addNotebookPage(notebook, pacFlashPage, "PAC Flash  PAC烧录");

    // 标题
    GtkWidget* pacFlashLabel = helper.createLabel("PAC Flash Settings  PAC烧录设置", "pac_flash_label", 0, 0, 400, 20);
    // 文件路径行
    GtkWidget* pacFileLabel = helper.createLabel("PAC file path  PAC文件路径", "pac_file_label", 0, 0, 200, 20);
    GtkWidget* pacFilePath  = helper.createEntry("pac_file_path", "", false, 0, 0, 240, 32);
    GtkWidget* pacSelectBtn = helper.createButton("...", "pac_select", nullptr, 0, 0, 40, 32);
    GtkWidget* pacUnpackBtn = helper.createButton("Unpack PAC file 解包PAC文件", "pac_unpack", nullptr, 0, 0, 180, 32);

    // 分区列表（带滚动窗口）
    GtkWidget* p_scrolledWindow = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(p_scrolledWindow, 1000, 500);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(p_scrolledWindow),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    GtkListStore* p_store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget* p_treeView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(p_store));
    gtk_widget_set_name(p_treeView, "pac_list");
    helper.addWidget("pac_list", p_treeView);
    GtkCellRenderer* p_renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(p_treeView), -1,
        "Partition Name", p_renderer, "text", 0, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(p_treeView), -1,
        "Size", p_renderer, "text", 1, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(p_treeView), -1,
        "Type", p_renderer, "text", 2, NULL);
    gtk_container_add(GTK_CONTAINER(p_scrolledWindow), p_treeView);

    // 烧录按钮
    GtkWidget* pacFlashBtn = helper.createButton("START PAC Flash  开始PAC烧录", "pac_flash_start", nullptr, 0, 0, 180, 32);

    // AB分区选择按钮
    GtkWidget* abpart_auto = helper.createButton("Not VAB 不是A/B分区 --- FDL2", "abpart_auto", nullptr, 0, 0, 120, 32);
    GtkWidget* abpart_a    = helper.createButton("A Parts A分区 --- FDL2",         "abpart_a",    nullptr, 0, 0, 130, 32);
    GtkWidget* abpart_b    = helper.createButton("B Parts B分区 --- FDL2",         "abpart_b",    nullptr, 0, 0, 130, 32);
    GtkWidget* abpartButtonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(abpartButtonBox), abpart_auto, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(abpartButtonBox), abpart_a,    FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(abpartButtonBox), abpart_b,    FALSE, FALSE, 0);

    // 布局
    int row = 0;
    helper.addToGrid(pacFlashPage, pacFlashLabel, 0, row++, 4, 1);

    GtkWidget* pacFileBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(pacFileBox), pacFileLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(pacFileBox), pacFilePath,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(pacFileBox), pacSelectBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(pacFileBox), pacUnpackBtn, FALSE, FALSE, 0);
    helper.addToGrid(pacFlashPage, pacFileBox,       0, row++, 4, 1);
    helper.addToGrid(pacFlashPage, p_scrolledWindow, 0, row++, 4, 1);
    helper.addToGrid(pacFlashPage, abpartButtonBox,  0, row++, 4, 1);
    helper.addToGrid(pacFlashPage, pacFlashBtn,      0, row++, 4, 1);

    return pacFlashPage;
}

// ===== 信号绑定 =====

void bind_pac_flash_signals(GtkWidgetHelper& helper) {
    GtkWidget* pacSelectBtn = helper.getWidget("pac_select");
    if (pacSelectBtn) {
        helper.bindClick(pacSelectBtn, [&helper]() {
            on_button_clicked_pac_select(helper);
        });
    }
    GtkWidget* pacUnpackBtn = helper.getWidget("pac_unpack");
    if (pacUnpackBtn) {
        helper.bindClick(pacUnpackBtn, [&helper]() {
            on_button_clicked_pac_unpack(helper);
        });
    }
    GtkWidget* pacFlashBtn = helper.getWidget("pac_flash_start");
    if (pacFlashBtn) {
        helper.bindClick(pacFlashBtn, [&helper]() {
            on_button_clicked_pac_flash_start(helper);
        });
    }
    GtkWidget* abpart_auto = helper.getWidget("abpart_auto");
    if (abpart_auto) {
        helper.bindClick(abpart_auto, [&helper]() {
            on_button_clicked_abpart_auto(helper);
        });
    }
    GtkWidget* abpart_a = helper.getWidget("abpart_a");
    if (abpart_a) {
        helper.bindClick(abpart_a, [&helper]() {
            on_button_clicked_abpart_a(helper);
        });
    }
    GtkWidget* abpart_b = helper.getWidget("abpart_b");
    if (abpart_b) {
        helper.bindClick(abpart_b, [&helper]() {
            on_button_clicked_abpart_b(helper);
        });
    }
}
