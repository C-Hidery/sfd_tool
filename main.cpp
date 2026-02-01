#include <iostream>
#include <cstring>
#include "main_console.cpp"
#include "GtkWidgetHelper.hpp"
#include <thread>
#include <chrono>
#include <gtk/gtk.h>
class WidgetHanduler{
public:
	WidgetHanduler(){}
	void Button_connect_clicked(){
		std::thread([](){
			std::cout << "Connect button clicked!" << std::endl;
			// Simulate a long operation
			std::this_thread::sleep_for(std::chrono::seconds(2));
			std::cout << "Device connected." << std::endl;
		}).detach();
	}
};
int gtk_kmain(int argc, char** argv){
    DEG_LOG(I, "Starting GUI mode...");
    gtk_init(&argc, &argv);
	WidgetHanduler handuler = WidgetHanduler();
	// Window Setup
    // 创建主窗口
	GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), "SFD Tool GUI By Ryan Crepa");
	gtk_window_set_default_size(GTK_WINDOW(window), 1174, 765);
	//设置关闭信号
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	// 创建 GtkWidgetHelper
	GtkWidgetHelper helper(window);
	helper.setParent(window, LayoutType::GRID);

	// 创建主网格布局
	GtkWidget* mainGrid = gtk_grid_new();
	GtkWidget *notebook = helper.createNotebook("main_notebook", 0, 0, 1174, 765);
	std::vector<std::string> options = {
		"Connect 连接",
		"FDL Settings FDL设置",
		"Partition List 分区列表",
		"Manual Operations 手动操作",
		"Advanced Settings 高级设置",
		"About 关于",
		"Logcat 日志"
	};
	// Connect Page
	{

	}
	// FDL Settings Page
	{

	}
	// Partition List Page
	{

	}
	// Manual Operations Page
	{

	}
	// Advanced Settings Page
	{

	}
	// About Page
	{

	}
	// Logcat Page
	{
		
	}
	// ========== 设置Notebook属性 ==========
	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_TOP);
	gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), TRUE);
	gtk_container_add(GTK_CONTAINER(window), mainGrid);
	// 显示所有组件
	gtk_widget_show_all(window);
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