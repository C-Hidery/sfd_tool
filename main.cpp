#include <iostream>
#include <cstring>
#include "main_console.cpp"
#include "GtkWidgetHelper.hpp"
#include "GtkWindowCreator.hpp"
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
    RunGtkWindow();
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