#include <iostream>
#include <cstring>
#include "main_console.cpp"
#include <gtk/gtk.h>
int main(int argc, char** argv) {
    if (argc > 1 && !strcmp(argv[1], "--no-gui")) {
        // Call the console version of main
        return main_console(argc-1, argv+1); // Skip the first argument
    }
    else {
        DEG_LOG(I, "Starting GUI mode...");
        gtk_init(&argc, &argv);
        GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(window), "SFD Tool GUI");
        gtk_window_set_default_size(GTK_WINDOW(window), 1000, 800);
        
        // 设置关闭信号
        g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
        
        GtkWidget *label = gtk_label_new("SFD Tool GUI Mode");
        gtk_container_add(GTK_CONTAINER(window), label);
        
        // 显示所有组件
        gtk_widget_show_all(window);
        
        // 启动GTK主循环
        gtk_main();
        return 0;
    }
}