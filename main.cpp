#include <iostream>
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
        return 0;
    }
}