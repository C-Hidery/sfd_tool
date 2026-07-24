/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 * GtkWidgetHelper v0.1.1 for GTK4
 */

#include "GtkWidgetHelper.hpp"

#if defined(__has_include) && __has_include("../i18n.h")
    #include "../i18n.h"
#else
    #define _(x) x
#endif

#include <iostream>
#include <sstream>
#include <iomanip>

// 全局变量
bool g_window_is_dragging = false;
guint g_drag_check_timeout = 0;

struct DialogState {
    gint response;
    GMainLoop* loop;
};

#if !defined(GTK_ICON_SIZE_BUTTON)
    #define GTK_ICON_SIZE_BUTTON GTK_ICON_SIZE_NORMAL
#endif

static void setScrolledWindowPolicy(GtkWidget* scrolled, GtkPolicyType hpolicy, GtkPolicyType vpolicy) {
    (void)scrolled;
    (void)hpolicy;
    (void)vpolicy;
}

static GtkWidget* createScrolledWindowWidget() {
    return gtk_scrolled_window_new();
}

void destroyWidget(GtkWidget* widget) {
    if (!widget) return;
    if (GTK_IS_WINDOW(widget)) {
        gtk_window_destroy(GTK_WINDOW(widget));
    } else {
        gtk_widget_unparent(widget);
    }
}

struct DialogRunData {
    GMainLoop* loop;
    gint response;
    gboolean destroyed;
};

static void dialog_response_cb(GtkDialog* dialog, gint response_id, gpointer user_data) {
    auto* data = static_cast<DialogRunData*>(user_data);
    data->response = response_id;

    // 如果对话框尚未被销毁，立即销毁它（模拟 gtk_dialog_run 的行为）
    if (!data->destroyed) {
        data->destroyed = TRUE;
        gtk_window_destroy(GTK_WINDOW(dialog));
    }

    // 退出循环
    if (g_main_loop_is_running(data->loop))
        g_main_loop_quit(data->loop);
}

static void dialog_destroy_cb(GtkDialog* dialog, gpointer user_data) {
    auto* data = static_cast<DialogRunData*>(user_data);
    data->destroyed = TRUE;

    // 如果循环还在运行，退出
    if (g_main_loop_is_running(data->loop))
        g_main_loop_quit(data->loop);
}

// This func will destory widget automatically, DO NOT destory manually.
gint runDialog(GtkDialog* dialog) {
    DialogRunData data;
    data.loop = g_main_loop_new(nullptr, FALSE);
    data.response = GTK_RESPONSE_NONE;
    data.destroyed = FALSE;

    // 连接信号，不需要记录 handler id，因为对象销毁时会自动断开
    g_signal_connect(dialog, "response", G_CALLBACK(dialog_response_cb), &data);
    g_signal_connect(dialog, "destroy", G_CALLBACK(dialog_destroy_cb), &data);

    gtk_window_present(GTK_WINDOW(dialog));
    g_main_loop_run(data.loop);

    // 循环结束后，对话框已被销毁，不需要做任何清理
    g_main_loop_unref(data.loop);

    return data.response;
}


static std::string getFileChooserSelection(GtkFileChooser* chooser) {
    std::string path;
    GFile* file = gtk_file_chooser_get_file(chooser);
    if (file) {
        char* file_path = g_file_get_path(file);
        if (file_path) {
            path = file_path;
            g_free(file_path);
        }
        g_object_unref(file);
    }
    return path;
}

void addBoxChild(GtkWidget* box, GtkWidget* child) {
    if (!box || !child || !GTK_IS_WIDGET(child) || !GTK_IS_BOX(box)) {
        g_warning("addBoxChild: invalid parameters");
        return;
    }

    // 如果 child 已有父容器，先移除
    if (gtk_widget_get_parent(child)) {
        gtk_widget_unparent(child);
    }
    gtk_box_append(GTK_BOX(box), child);
}

void gtkBoxPackStart(GtkWidget* box, GtkWidget* child, bool expand, bool fill, int padding) {
    if (!box || !child || !GTK_IS_WIDGET(child) || !GTK_IS_BOX(box)) {
        g_warning("gtkBoxPackStart: invalid parameters");
        return;
    }

    // 如果 child 已有父容器，先移除
    if (gtk_widget_get_parent(child)) {
        gtk_widget_unparent(child);
    }

    gtk_box_append(GTK_BOX(box), child);

    // 设置布局属性
    gtk_widget_set_hexpand(child, expand);
    gtk_widget_set_vexpand(child, expand);
    if (fill) {
        gtk_widget_set_halign(child, GTK_ALIGN_FILL);
        gtk_widget_set_valign(child, GTK_ALIGN_FILL);
    } else {
        gtk_widget_set_halign(child, GTK_ALIGN_START);
        gtk_widget_set_valign(child, GTK_ALIGN_START);
    }
    if (padding > 0) {
        gtk_widget_set_margin_start(child, padding);
        gtk_widget_set_margin_end(child, padding);
        gtk_widget_set_margin_top(child, padding);
        gtk_widget_set_margin_bottom(child, padding);
    }
}


void gtkContainerAdd(GtkWidget* container, GtkWidget* child) {
    if (!container || !child || !GTK_IS_WIDGET(child)) {
        g_warning("gtkContainerAdd: invalid parameters");
        return;
    }

    // 如果 child 已有父容器，先移除
    if (gtk_widget_get_parent(child)) {
        gtk_widget_unparent(child);
    }

    // 根据容器类型选择正确的添加方法
    if (GTK_IS_WINDOW(container)) {
        gtk_window_set_child(GTK_WINDOW(container), child);
    } else if (GTK_IS_BOX(container)) {
        gtk_box_append(GTK_BOX(container), child);
    } else if (GTK_IS_GRID(container)) {
        gtk_grid_attach(GTK_GRID(container), child, 0, 0, 1, 1);
    } else if (GTK_IS_FRAME(container)) {
        gtk_frame_set_child(GTK_FRAME(container), child);
    } else if (GTK_IS_SCROLLED_WINDOW(container)) {
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(container), child);
    } else if (GTK_IS_NOTEBOOK(container)) {
        // Notebook 需要特殊处理
        gtk_notebook_append_page(GTK_NOTEBOOK(container), child, gtk_label_new("Page"));
    } else {
        gtk_widget_set_parent(child, container);
    }
}

void gtkFrameSetLabelAlign(GtkWidget* frame, float xalign, float yalign) {
    (void)yalign;
    gtk_frame_set_label_align(GTK_FRAME(frame), xalign);
}

static void setButtonChild(GtkWidget* button, GtkWidget* child) {
    gtk_button_set_child(GTK_BUTTON(button), child);
}

static void setWindowChild(GtkWidget* window, GtkWidget* child) {
    gtk_window_set_child(GTK_WINDOW(window), child);
}

static void setScrolledChild(GtkWidget* scrolled, GtkWidget* child) {
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), child);
}

static void setFrameChild(GtkWidget* frame, GtkWidget* child) {
    gtk_frame_set_child(GTK_FRAME(frame), child);
}

static void setPanedChild(GtkWidget* paned, GtkWidget* child, bool start) {
    if (start) {
        gtk_paned_set_start_child(GTK_PANED(paned), child);
    } else {
        gtk_paned_set_end_child(GTK_PANED(paned), child);
    }
}

static GtkWidget* createIconImage(const std::string& iconName) {
    return gtk_image_new_from_icon_name(iconName.c_str());
}

static const gchar* getEntryTextCompat(GtkEntry* entry) {
    return gtk_editable_get_text(GTK_EDITABLE(entry));
}

static void setEntryTextCompat(GtkEntry* entry, const gchar* text) {
    gtk_editable_set_text(GTK_EDITABLE(entry), text);
}

// ---------- 修正：GTK4 无 gtk_widget_get_surface ----------
bool isWindowDragging(GtkWindow* window) {
    if (!window || !GTK_IS_WINDOW(window)) {
        return false;
    }
    GtkNative* native = gtk_widget_get_native(GTK_WIDGET(window));
    if (!native) return false;
    GdkSurface* surface = gtk_native_get_surface(native);
    if (!surface) return false;

    GdkModifierType mask;
    GdkDisplay* display = gdk_surface_get_display(surface);
    GdkSeat* seat = gdk_display_get_default_seat(display);
    GdkDevice* pointer = gdk_seat_get_pointer(seat);
    gdk_surface_get_device_position(surface, pointer, nullptr, nullptr, &mask);
    return (mask & GDK_BUTTON1_MASK) != 0;
}

void initDragDetection(GtkWindow* window) {
    g_drag_check_timeout = g_timeout_add(16, [](gpointer data) -> gboolean {
        GtkWindow* win = GTK_WINDOW(data);
        bool was = g_window_is_dragging;
        g_window_is_dragging = isWindowDragging(win);
        if (was != g_window_is_dragging) {
#ifdef _DEBUG
            printf("[DEBUG] Window %s dragging\n", g_window_is_dragging ? "started" : "stopped");
#endif
        }
        return G_SOURCE_CONTINUE;
    }, window);
}

void waitForDragEnd(GtkWindow* window) {
    while (window && isWindowDragging(window)) {
        g_main_context_iteration(g_main_context_default(), FALSE);
        g_usleep(10000);
    }
}

static GMainLoop* g_gui_main_loop = nullptr;

void gui_run_main_loop() {
    if (!g_gui_main_loop) {
        g_gui_main_loop = g_main_loop_new(nullptr, FALSE);
    }
    g_main_loop_run(g_gui_main_loop);
}

void gui_quit_main_loop() {
    if (g_gui_main_loop && g_main_loop_is_running(g_gui_main_loop)) {
        g_main_loop_quit(g_gui_main_loop);
    }
}

// ---------- 对话框函数（GTK4 所有 API 均有效） ----------
std::string showFileChooser(GtkWindow* parent, bool open) {
    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        open ? _("Select file") : _("Save file"),
        parent,
        open ? GTK_FILE_CHOOSER_ACTION_OPEN : GTK_FILE_CHOOSER_ACTION_SAVE,
        _("_Cancel"), GTK_RESPONSE_CANCEL,
        open ? _("_Open") : _("_Save"), GTK_RESPONSE_ACCEPT,
        NULL);
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, _("All files (*.*)"));
    gtk_file_filter_add_pattern(filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    gint result = runDialog(GTK_DIALOG(dialog));
    std::string filename;
    if (result == GTK_RESPONSE_ACCEPT) {
        filename = getFileChooserSelection(GTK_FILE_CHOOSER(dialog));
    }
    // destroyWidget(dialog);
    return filename;
}

std::string showFolderChooser(GtkWindow* parent) {
    GtkWidget* dialog = gtk_file_chooser_dialog_new(_("Select folder"),
        parent, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        _("_Cancel"), GTK_RESPONSE_CANCEL,
        _("_Select"), GTK_RESPONSE_ACCEPT, NULL);
    gint result = runDialog(GTK_DIALOG(dialog));
    std::string folder;
    if (result == GTK_RESPONSE_ACCEPT) {
        folder = getFileChooserSelection(GTK_FILE_CHOOSER(dialog));
    }
    // destroyWidget(dialog);
    return folder;
}

void showInfoDialog(GtkWindow* parent, const char* title, const char* message) {
    GtkWidget* dialog = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", message);
    gtk_window_set_title(GTK_WINDOW(dialog), title);
    runDialog(GTK_DIALOG(dialog));
    // destroyWidget(dialog);
}

void showWarningDialog(GtkWindow* parent, const char* title, const char* message) {
    GtkWidget* dialog = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL,
        GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, "%s", message);
    gtk_window_set_title(GTK_WINDOW(dialog), title);
    runDialog(GTK_DIALOG(dialog));
    // destroyWidget(dialog);
}

void showErrorDialog(GtkWindow* parent, const char* title, const char* message) {
    GtkWidget* dialog = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL,
        GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", message);
    gtk_window_set_title(GTK_WINDOW(dialog), title);
    runDialog(GTK_DIALOG(dialog));
    // destroyWidget(dialog);
}

bool showConfirmDialog(GtkWindow* parent, const char* title, const char* message) {
    GtkWidget* dialog = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "%s", message);
    gtk_window_set_title(GTK_WINDOW(dialog), title);
    gint result = runDialog(GTK_DIALOG(dialog));
    // destroyWidget(dialog);
    return result == GTK_RESPONSE_YES;
}

std::string showInputDialog(GtkWindow* parent, const char* title, const char* message) {
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        title, parent, GTK_DIALOG_MODAL,
        "_OK", GTK_RESPONSE_OK,
        "_Cancel", GTK_RESPONSE_CANCEL, NULL);
    GtkWidget* content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    // 替代 gtk_container_set_border_width 和 gtk_widget_set_margin_all
    gtk_widget_set_margin_start(vbox, 10);
    gtk_widget_set_margin_end(vbox, 10);
    gtk_widget_set_margin_top(vbox, 10);
    gtk_widget_set_margin_bottom(vbox, 10);

    GtkWidget* label = gtk_label_new(message);
    gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
    addBoxChild(vbox, label);

    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    addBoxChild(vbox, entry);

    addBoxChild(content_area, vbox);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

    std::string result;
    gint response = runDialog(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_OK) {
        const gchar* text = gtk_editable_get_text(GTK_EDITABLE(entry));
        if (text) result = text;
    }
    // destroyWidget(dialog);
    return result;
}

std::string showSaveFileDialog(GtkWindow* parent,
                               const std::string& default_filename,
                               const std::vector<std::pair<std::string, std::string>>& filters) {
    GtkWidget* dialog = gtk_file_chooser_dialog_new(_("Saving files"),
        parent, GTK_FILE_CHOOSER_ACTION_SAVE,
        _("_Cancel"), GTK_RESPONSE_CANCEL,
        _("_Save"), GTK_RESPONSE_ACCEPT, NULL);
    if (!default_filename.empty())
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), default_filename.c_str());
    for (const auto& f : filters) {
        GtkFileFilter* filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, f.first.c_str());
        gtk_file_filter_add_pattern(filter, f.second.c_str());
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    }
    GtkFileFilter* all = gtk_file_filter_new();
    gtk_file_filter_set_name(all, _("All files (*.*)"));
    gtk_file_filter_add_pattern(all, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), all);

    std::string filename;
    gint result = runDialog(GTK_DIALOG(dialog));
    if (result == GTK_RESPONSE_ACCEPT) {
        filename = getFileChooserSelection(GTK_FILE_CHOOSER(dialog));
    }
    // destroyWidget(dialog);
    return filename;
}

// ==================== 线程安全对话框（Thread-Safe Dialogs） ====================

bool showConfirmDialogSyncInThread(GtkWindow* parent, const char* title, const char* message) {
    if (g_main_context_is_owner(g_main_context_default())) {
        waitForDragEnd(parent);                // 若有拖拽则等待结束
        return showConfirmDialog(parent, title, message);
    }
    GMainContext* worker_ctx = g_main_context_new();
    g_main_context_push_thread_default(worker_ctx);
    GMainLoop* loop = g_main_loop_new(worker_ctx, FALSE);
    bool result = false;

    std::string title_str(title);
    std::string msg_str(message);

    g_main_context_invoke(g_main_context_default(), [](gpointer user_data) -> gboolean {
        auto* data = static_cast<std::tuple<GMainLoop*, bool*, GtkWindow*, std::string, std::string>*>(user_data);
        GMainLoop* loop = std::get<0>(*data);
        bool* result = std::get<1>(*data);
        GtkWindow* parent = std::get<2>(*data);
        std::string& title = std::get<3>(*data);
        std::string& message = std::get<4>(*data);

        waitForDragEnd(parent);
        *result = showConfirmDialog(parent, title.c_str(), message.c_str());
        g_main_loop_quit(loop);
        delete data;
        return G_SOURCE_REMOVE;
    }, new std::tuple<GMainLoop*, bool*, GtkWindow*, std::string, std::string>(loop, &result, parent, title_str, msg_str));

    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    g_main_context_pop_thread_default(worker_ctx);
    g_main_context_unref(worker_ctx);
    return result;
}

std::string showInputDialogSyncInThread(GtkWindow* parent, const char* title, const char* message) {
    if (g_main_context_is_owner(g_main_context_default())) {
        waitForDragEnd(parent);                // 若有拖拽则等待结束
        return showInputDialog(parent, title, message);
    }
    GMainContext* worker_ctx = g_main_context_new();
    g_main_context_push_thread_default(worker_ctx);
    GMainLoop* loop = g_main_loop_new(worker_ctx, FALSE);
    std::string result;

    std::string title_str(title);
    std::string msg_str(message);

    g_main_context_invoke(g_main_context_default(), [](gpointer user_data) -> gboolean {
        auto* data = static_cast<std::tuple<GMainLoop*, std::string*, GtkWindow*, std::string, std::string>*>(user_data);
        GMainLoop* loop = std::get<0>(*data);
        std::string* result = std::get<1>(*data);
        GtkWindow* parent = std::get<2>(*data);
        std::string& title = std::get<3>(*data);
        std::string& message = std::get<4>(*data);

        waitForDragEnd(parent);
        *result = showInputDialog(parent, title.c_str(), message.c_str());
        g_main_loop_quit(loop);
        delete data;
        return G_SOURCE_REMOVE;
    }, new std::tuple<GMainLoop*, std::string*, GtkWindow*, std::string, std::string>(loop, &result, parent, title_str, msg_str));

    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    g_main_context_pop_thread_default(worker_ctx);
    g_main_context_unref(worker_ctx);
    return result;
}

void showErrorDialogSyncInThread(GtkWindow* parent, const char* title, const char* message) {
    if (g_main_context_is_owner(g_main_context_default())) {
        waitForDragEnd(parent);                // 若有拖拽则等待结束
        showErrorDialog(parent, title, message);
        return;
    }
    GMainContext* worker_ctx = g_main_context_new();
    g_main_context_push_thread_default(worker_ctx);
    GMainLoop* loop = g_main_loop_new(worker_ctx, FALSE);

    std::string title_str(title);
    std::string msg_str(message);

    g_main_context_invoke(g_main_context_default(), [](gpointer user_data) -> gboolean {
        auto* data = static_cast<std::tuple<GMainLoop*, GtkWindow*, std::string, std::string>*>(user_data);
        GMainLoop* loop = std::get<0>(*data);
        GtkWindow* parent = std::get<1>(*data);
        std::string& title = std::get<2>(*data);
        std::string& message = std::get<3>(*data);

        waitForDragEnd(parent);
        showErrorDialog(parent, title.c_str(), message.c_str());
        g_main_loop_quit(loop);
        delete data;
        return G_SOURCE_REMOVE;
    }, new std::tuple<GMainLoop*, GtkWindow*, std::string, std::string>(loop, parent, title_str, msg_str));

    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    g_main_context_pop_thread_default(worker_ctx);
    g_main_context_unref(worker_ctx);
}

void showInfoDialogSyncInThread(GtkWindow* parent, const char* title, const char* message) {
    if (g_main_context_is_owner(g_main_context_default())) {
        waitForDragEnd(parent);                // 若有拖拽则等待结束
        showInfoDialog(parent, title, message);
        return;
    }
    GMainContext* worker_ctx = g_main_context_new();
    g_main_context_push_thread_default(worker_ctx);
    GMainLoop* loop = g_main_loop_new(worker_ctx, FALSE);

    std::string title_str(title);
    std::string msg_str(message);

    g_main_context_invoke(g_main_context_default(), [](gpointer user_data) -> gboolean {
        auto* data = static_cast<std::tuple<GMainLoop*, GtkWindow*, std::string, std::string>*>(user_data);
        GMainLoop* loop = std::get<0>(*data);
        GtkWindow* parent = std::get<1>(*data);
        std::string& title = std::get<2>(*data);
        std::string& message = std::get<3>(*data);

        waitForDragEnd(parent);
        showInfoDialog(parent, title.c_str(), message.c_str());
        g_main_loop_quit(loop);
        delete data;
        return G_SOURCE_REMOVE;
    }, new std::tuple<GMainLoop*, GtkWindow*, std::string, std::string>(loop, parent, title_str, msg_str));

    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    g_main_context_pop_thread_default(worker_ctx);
    g_main_context_unref(worker_ctx);
}

void showWarningDialogSyncInThread(GtkWindow* parent, const char* title, const char* message) {
    if (g_main_context_is_owner(g_main_context_default())) {
        waitForDragEnd(parent);                // 若有拖拽则等待结束
        showWarningDialog(parent, title, message);
        return;
    }
    GMainContext* worker_ctx = g_main_context_new();
    g_main_context_push_thread_default(worker_ctx);
    GMainLoop* loop = g_main_loop_new(worker_ctx, FALSE);

    std::string title_str(title);
    std::string msg_str(message);

    g_main_context_invoke(g_main_context_default(), [](gpointer user_data) -> gboolean {
        auto* data = static_cast<std::tuple<GMainLoop*, GtkWindow*, std::string, std::string>*>(user_data);
        GMainLoop* loop = std::get<0>(*data);
        GtkWindow* parent = std::get<1>(*data);
        std::string& title = std::get<2>(*data);
        std::string& message = std::get<3>(*data);

        waitForDragEnd(parent);
        showWarningDialog(parent, title.c_str(), message.c_str());
        g_main_loop_quit(loop);
        delete data;
        return G_SOURCE_REMOVE;
    }, new std::tuple<GMainLoop*, GtkWindow*, std::string, std::string>(loop, parent, title_str, msg_str));

    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    g_main_context_pop_thread_default(worker_ctx);
    g_main_context_unref(worker_ctx);
}

std::string showFileChooserSyncInThread(GtkWindow* parent, bool open) {
    if (g_main_context_is_owner(g_main_context_default())) {
        waitForDragEnd(parent);                // 若有拖拽则等待结束
        return showFileChooser(parent, open);
    }
    GMainContext* worker_ctx = g_main_context_new();
    g_main_context_push_thread_default(worker_ctx);
    GMainLoop* loop = g_main_loop_new(worker_ctx, FALSE);
    std::string result;

    g_main_context_invoke(g_main_context_default(), [](gpointer user_data) -> gboolean {
        auto* data = static_cast<std::tuple<GMainLoop*, std::string*, GtkWindow*, bool>*>(user_data);
        GMainLoop* loop = std::get<0>(*data);
        std::string* result = std::get<1>(*data);
        GtkWindow* parent = std::get<2>(*data);
        bool open = std::get<3>(*data);

        waitForDragEnd(parent);
        *result = showFileChooser(parent, open);
        g_main_loop_quit(loop);
        delete data;
        return G_SOURCE_REMOVE;
    }, new std::tuple<GMainLoop*, std::string*, GtkWindow*, bool>(loop, &result, parent, open));

    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    g_main_context_pop_thread_default(worker_ctx);
    g_main_context_unref(worker_ctx);
    return result;
}

std::string showFolderChooserSyncInThread(GtkWindow* parent) {
    if (g_main_context_is_owner(g_main_context_default())) {
        waitForDragEnd(parent);                // 若有拖拽则等待结束
        return showFolderChooser(parent);
    }
    GMainContext* worker_ctx = g_main_context_new();
    g_main_context_push_thread_default(worker_ctx);
    GMainLoop* loop = g_main_loop_new(worker_ctx, FALSE);
    std::string result;

    g_main_context_invoke(g_main_context_default(), [](gpointer user_data) -> gboolean {
        auto* data = static_cast<std::tuple<GMainLoop*, std::string*, GtkWindow*>*>(user_data);
        GMainLoop* loop = std::get<0>(*data);
        std::string* result = std::get<1>(*data);
        GtkWindow* parent = std::get<2>(*data);

        waitForDragEnd(parent);
        *result = showFolderChooser(parent);
        g_main_loop_quit(loop);
        delete data;
        return G_SOURCE_REMOVE;
    }, new std::tuple<GMainLoop*, std::string*, GtkWindow*>(loop, &result, parent));

    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    g_main_context_pop_thread_default(worker_ctx);
    g_main_context_unref(worker_ctx);
    return result;
}

std::string showSaveFileDialogSyncInThread(GtkWindow* parent,
                                       const std::string& default_filename,
                                       const std::vector<std::pair<std::string, std::string>>& filters) {
    if (g_main_context_is_owner(g_main_context_default())) {
        waitForDragEnd(parent);                // 若有拖拽则等待结束
        return showSaveFileDialog(parent, default_filename, filters);
    }
    GMainContext* worker_ctx = g_main_context_new();
    g_main_context_push_thread_default(worker_ctx);
    GMainLoop* loop = g_main_loop_new(worker_ctx, FALSE);
    std::string result;

    g_main_context_invoke(g_main_context_default(), [](gpointer user_data) -> gboolean {
        auto* data = static_cast<std::tuple<GMainLoop*, std::string*, GtkWindow*, std::string, std::vector<std::pair<std::string, std::string>>>*>(user_data);
        GMainLoop* loop = std::get<0>(*data);
        std::string* result = std::get<1>(*data);
        GtkWindow* parent = std::get<2>(*data);
        std::string& default_filename = std::get<3>(*data);
        auto& filters = std::get<4>(*data);

        waitForDragEnd(parent);
        *result = showSaveFileDialog(parent, default_filename, filters);
        g_main_loop_quit(loop);
        delete data;
        return G_SOURCE_REMOVE;
    }, new std::tuple<GMainLoop*, std::string*, GtkWindow*, std::string, std::vector<std::pair<std::string, std::string>>>(loop, &result, parent, default_filename, filters));

    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    g_main_context_pop_thread_default(worker_ctx);
    g_main_context_unref(worker_ctx);
    return result;
}

// ==================== GtkWidgetHelper 实现 ====================

GtkWidgetHelper::GtkWidgetHelper(GtkWidget* parent)
    : m_parent(parent), m_layoutType(LayoutType::NONE) {}

GtkWidgetHelper::~GtkWidgetHelper() { clearWidgets(false); }

void GtkWidgetHelper::addNotebookPage(GtkWidget* notebook, GtkWidget* child,
                                      const std::string& label,
                                      bool closeable) {
    if (!GTK_IS_NOTEBOOK(notebook) || !child) return;
    if (!closeable) {
        GtkWidget* pageLabel = gtk_label_new(label.c_str());
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), child, pageLabel);
    } else {
        GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        GtkWidget* pageLabel = gtk_label_new(label.c_str());
        GtkWidget* closeButton = gtk_button_new_with_label("×");
        gtk_widget_set_size_request(closeButton, 20, 20);
        addBoxChild(hbox, pageLabel);
        addBoxChild(hbox, closeButton);
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), child, hbox);
        g_signal_connect_swapped(closeButton, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer data) {
            destroyWidget(GTK_WIDGET(data));
        }), child);
    }
}

void GtkWidgetHelper::addToGrid(GtkWidget* grid, GtkWidget* child,
                                int left, int top, int width, int height) {
    if (GTK_IS_GRID(grid) && child) {
        if (gtk_widget_get_parent(child)) {
            gtk_widget_unparent(child);
        }
        gtk_grid_attach(GTK_GRID(grid), child, left, top, width, height);
    }
}

// ---------- 布局控制 ----------
void GtkWidgetHelper::setWidgetPosition(GtkWidget* widget, int x, int y) {
    if (!widget || m_layoutType != LayoutType::FIXED || !m_parent) return;
    if (GTK_IS_FIXED(m_parent)) {
        if (gtk_widget_get_parent(widget)) gtk_widget_unparent(widget);
        gtk_fixed_put(GTK_FIXED(m_parent), widget, x, y);
    }
}

void GtkWidgetHelper::setWidgetSize(GtkWidget* widget, int width, int height) {
    if (widget && width > 0 && height > 0) gtk_widget_set_size_request(widget, width, height);
}

void GtkWidgetHelper::setWidgetGeometry(GtkWidget* widget, int x, int y, int width, int height) {
    setWidgetPosition(widget, x, y);
    setWidgetSize(widget, width, height);
}

void GtkWidgetHelper::setWidgetExpand(GtkWidget* widget, bool hexpand, bool vexpand) {
    if (widget) { gtk_widget_set_hexpand(widget, hexpand); gtk_widget_set_vexpand(widget, vexpand); }
}

void GtkWidgetHelper::setWidgetAlignment(GtkWidget* widget, float xalign, float yalign) {
    if (!widget) return;

    auto alignValue = [](float value) {
        if (value <= 0.0f) return GTK_ALIGN_START;
        if (value >= 1.0f) return GTK_ALIGN_END;
        return GTK_ALIGN_CENTER;
    };

    gtk_widget_set_halign(widget, alignValue(xalign));
    gtk_widget_set_valign(widget, alignValue(yalign));
}

void GtkWidgetHelper::setWidgetMargin(GtkWidget* widget, int top, int bottom, int left, int right) {
    if (!widget) return;
    // 使用四个独立的 margin 函数
    gtk_widget_set_margin_top(widget, top);
    gtk_widget_set_margin_bottom(widget, bottom);
    gtk_widget_set_margin_start(widget, left);
    gtk_widget_set_margin_end(widget, right);
}

void GtkWidgetHelper::setWidgetPadding(GtkWidget* widget, int padding) {
    setWidgetMargin(widget, padding, padding, padding, padding);
}

// ---------- 属性和内容控制 ----------
const char* GtkWidgetHelper::getLabelText(GtkWidget* label) const {
    if (GTK_IS_LABEL(label)) {
        const gchar* text = gtk_label_get_text(GTK_LABEL(label));
        return text ? text : "";
    }
    return "";
}

void GtkWidgetHelper::setLabelText(GtkWidget* label, const std::string& text) {
    if (GTK_IS_LABEL(label)) gtk_label_set_text(GTK_LABEL(label), text.c_str());
}

void GtkWidgetHelper::setLabelMarkup(GtkWidget* label, const std::string& markup) {
    if (GTK_IS_LABEL(label)) gtk_label_set_markup(GTK_LABEL(label), markup.c_str());
}

const char* GtkWidgetHelper::getEntryText(GtkWidget* entry) const {
    if (GTK_IS_ENTRY(entry)) {
        const gchar* text = getEntryTextCompat(GTK_ENTRY(entry));
        return text ? text : "";
    }
    return "";
}

void GtkWidgetHelper::setEntryText(GtkWidget* entry, const std::string& text) {
    if (GTK_IS_ENTRY(entry)) gtk_editable_set_text(GTK_EDITABLE(entry), text.c_str());
}

void GtkWidgetHelper::clearEntry(GtkWidget* entry) {
    setEntryText(entry, "");
}

std::string GtkWidgetHelper::getTextAreaText(GtkWidget* textview) const {
    if (GTK_IS_TEXT_VIEW(textview)) {
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
        GtkTextIter start, end;
        gtk_text_buffer_get_start_iter(buffer, &start);
        gtk_text_buffer_get_end_iter(buffer, &end);
        gchar* text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
        std::string result(text ? text : "");
        g_free(text);
        return result;
    }
    return "";
}

void GtkWidgetHelper::setTextAreaText(GtkWidget* textview, const std::string& text) {
    if (GTK_IS_TEXT_VIEW(textview)) {
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
        gtk_text_buffer_set_text(buffer, text.c_str(), -1);
    }
}

void GtkWidgetHelper::appendTextAreaText(GtkWidget* textview, const std::string& text) {
    if (GTK_IS_TEXT_VIEW(textview)) {
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(buffer, &end);
        gtk_text_buffer_insert(buffer, &end, text.c_str(), -1);
    }
}

bool GtkWidgetHelper::getCheckboxState(GtkWidget* checkbox) const {
    if (GTK_IS_TOGGLE_BUTTON(checkbox))
        return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox));
    return false;
}

void GtkWidgetHelper::setCheckboxState(GtkWidget* checkbox, bool state) {
    if (GTK_IS_TOGGLE_BUTTON(checkbox))
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox), state);
}

int GtkWidgetHelper::getSelectedRadioIndex(const std::string& groupName) const {
    for (int i = 0; ; ++i) {
        std::string btnName = groupName + "_radio_" + std::to_string(i);
        auto it = m_widgets.find(btnName);
        if (it == m_widgets.end()) break;
        GtkWidget* radio = it->second->widget;
        if (GTK_IS_CHECK_BUTTON(radio) && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radio)))
            return i;
    }
    return -1;
}

void GtkWidgetHelper::setSelectedRadioIndex(const std::string& groupName, int index) {
    std::string btnName = groupName + "_radio_" + std::to_string(index);
    auto it = m_widgets.find(btnName);
    if (it != m_widgets.end() && GTK_IS_CHECK_BUTTON(it->second->widget))
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(it->second->widget), TRUE);
}

int GtkWidgetHelper::getComboSelectedIndex(GtkWidget* combo) const {
    if (GTK_IS_COMBO_BOX(combo)) return gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
    return -1;
}

void GtkWidgetHelper::setComboSelectedIndex(GtkWidget* combo, int index) {
    if (GTK_IS_COMBO_BOX(combo)) gtk_combo_box_set_active(GTK_COMBO_BOX(combo), index);
}

std::string GtkWidgetHelper::getComboSelectedText(GtkWidget* combo) const {
    if (GTK_IS_COMBO_BOX_TEXT(combo)) {
        gchar* text = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));
        std::string result(text ? text : "");
        g_free(text);
        return result;
    }
    return "";
}

void GtkWidgetHelper::addComboItem(GtkWidget* combo, const std::string& item) {
    if (GTK_IS_COMBO_BOX_TEXT(combo))
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), item.c_str());
}

void GtkWidgetHelper::removeComboItem(GtkWidget* combo, int index) {
    if (GTK_IS_COMBO_BOX_TEXT(combo))
        gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(combo), index);
}

double GtkWidgetHelper::getProgressValue(GtkWidget* progressBar) const {
    if (GTK_IS_PROGRESS_BAR(progressBar))
        return gtk_progress_bar_get_fraction(GTK_PROGRESS_BAR(progressBar));
    return 0.0;
}

void GtkWidgetHelper::setProgressValue(GtkWidget* progressBar, double fraction) {
    if (GTK_IS_PROGRESS_BAR(progressBar))
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progressBar), fraction);
}

void GtkWidgetHelper::pulseProgressBar(GtkWidget* progressBar) {
    if (GTK_IS_PROGRESS_BAR(progressBar)) gtk_progress_bar_pulse(GTK_PROGRESS_BAR(progressBar));
}

double GtkWidgetHelper::getScaleValue(GtkWidget* scale) const {
    if (GTK_IS_RANGE(scale)) return gtk_range_get_value(GTK_RANGE(scale));
    return 0.0;
}

void GtkWidgetHelper::setScaleValue(GtkWidget* scale, double value) {
    if (GTK_IS_RANGE(scale)) gtk_range_set_value(GTK_RANGE(scale), value);
}

bool GtkWidgetHelper::getSwitchState(GtkWidget* switchBtn) const {
    if (GTK_IS_SWITCH(switchBtn)) return gtk_switch_get_active(GTK_SWITCH(switchBtn));
    return false;
}

void GtkWidgetHelper::setSwitchState(GtkWidget* switchBtn, bool state) {
    if (GTK_IS_SWITCH(switchBtn)) gtk_switch_set_active(GTK_SWITCH(switchBtn), state);
}

double GtkWidgetHelper::getSpinValue(GtkWidget* spin) const {
    if (GTK_IS_SPIN_BUTTON(spin)) return gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin));
    return 0.0;
}

void GtkWidgetHelper::setSpinValue(GtkWidget* spin, double value) {
    if (GTK_IS_SPIN_BUTTON(spin)) gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), value);
}

void GtkWidgetHelper::getCalendarDate(GtkWidget* calendar, guint& year, guint& month, guint& day) const {
    if (GTK_IS_CALENDAR(calendar)) {
        GDateTime* date = gtk_calendar_get_date(GTK_CALENDAR(calendar));
        if (date) {
            year = g_date_time_get_year(date);
            month = g_date_time_get_month(date);
            day = g_date_time_get_day_of_month(date);
            g_date_time_unref(date);
        }
    }
}

void GtkWidgetHelper::setCalendarDate(GtkWidget* calendar, guint year, guint month, guint day) {
    if (GTK_IS_CALENDAR(calendar)) {
        GDateTime* date = g_date_time_new_local(year, month, day, 0, 0, 0);
        gtk_calendar_select_day(GTK_CALENDAR(calendar), date);
        g_date_time_unref(date);
    }
}

// ---------- 信号绑定 ----------
void GtkWidgetHelper::bindClick(GtkWidget* button, std::function<void()> callback) {
    if (GTK_IS_BUTTON(button) && callback) {
        auto data = std::make_shared<CallbackData>();
        data->func = callback;
        std::string key = "click_" + std::to_string(reinterpret_cast<uintptr_t>(button));
        m_callbacks[key] = data;
        g_signal_connect_data(button, "clicked",
            G_CALLBACK(+[](GtkWidget*, gpointer d) {
                auto cb = static_cast<CallbackData*>(d);
                if (cb && cb->func) cb->func();
            }), data.get(), nullptr, G_CONNECT_DEFAULT);
    }
}

void GtkWidgetHelper::bindToggled(GtkWidget* toggleButton, std::function<void()> callback) {
    if (GTK_IS_TOGGLE_BUTTON(toggleButton) && callback) {
        auto data = std::make_shared<CallbackData>();
        data->func = callback;
        std::string key = "toggled_" + std::to_string(reinterpret_cast<uintptr_t>(toggleButton));
        m_callbacks[key] = data;
        g_signal_connect_data(toggleButton, "toggled",
            G_CALLBACK(+[](GtkWidget*, gpointer d) {
                auto cb = static_cast<CallbackData*>(d);
                if (cb && cb->func) cb->func();
            }), data.get(), nullptr, G_CONNECT_DEFAULT);
    }
}

void GtkWidgetHelper::bindValueChanged(GtkWidget* widget, std::function<void()> callback) {
    if (!widget || !callback) return;
    auto data = std::make_shared<CallbackData>();
    data->func = callback;
    std::string key = "valuechanged_" + std::to_string(reinterpret_cast<uintptr_t>(widget));
    m_callbacks[key] = data;
    if (GTK_IS_RANGE(widget) || GTK_IS_SPIN_BUTTON(widget)) {
        g_signal_connect_data(widget, "value-changed",
            G_CALLBACK(+[](GtkWidget*, gpointer d) {
                auto cb = static_cast<CallbackData*>(d);
                if (cb && cb->func) cb->func();
            }), data.get(), nullptr, G_CONNECT_DEFAULT);
    }
}

void GtkWidgetHelper::bindTextChanged(GtkWidget* entry, std::function<void()> callback) {
    if (GTK_IS_ENTRY(entry) && callback) {
        auto data = std::make_shared<CallbackData>();
        data->func = callback;
        std::string key = "textchanged_" + std::to_string(reinterpret_cast<uintptr_t>(entry));
        m_callbacks[key] = data;
        g_signal_connect_data(entry, "changed",
            G_CALLBACK(+[](GtkWidget*, gpointer d) {
                auto cb = static_cast<CallbackData*>(d);
                if (cb && cb->func) cb->func();
            }), data.get(), nullptr, G_CONNECT_DEFAULT);
    }
}

void GtkWidgetHelper::bindSelectionChanged(GtkWidget* widget, std::function<void()> callback) {
    if (!widget || !callback) return;
    auto data = std::make_shared<CallbackData>();
    data->func = callback;
    std::string key = "selectionchanged_" + std::to_string(reinterpret_cast<uintptr_t>(widget));
    m_callbacks[key] = data;
    if (GTK_IS_COMBO_BOX(widget)) {
        g_signal_connect_data(widget, "changed",
            G_CALLBACK(+[](GtkWidget*, gpointer d) {
                auto cb = static_cast<CallbackData*>(d);
                if (cb && cb->func) cb->func();
            }), data.get(), nullptr, G_CONNECT_DEFAULT);
    }
}

void GtkWidgetHelper::bindRowActivated(GtkWidget* treeview, std::function<void(int)> callback) {
    if (GTK_IS_TREE_VIEW(treeview) && callback) {
        auto* func = new std::function<void(int)>(callback);
        g_signal_connect_data(treeview, "row-activated",
            G_CALLBACK(+[](GtkTreeView* view, GtkTreePath* path, GtkTreeViewColumn* col, gpointer d) {
                (void)view; (void)col;
                auto f = static_cast<std::function<void(int)>*>(d);
                gint* indices = gtk_tree_path_get_indices(path);
                if (indices) (*f)(indices[0]);
            }), func, [](gpointer d, GClosure*) { delete static_cast<std::function<void(int)>*>(d); }, G_CONNECT_DEFAULT);
    }
}

// ---------- 组件管理 ----------
GtkWidget* GtkWidgetHelper::getWidget(const std::string& name) const {
    auto it = m_widgets.find(name);
    return it != m_widgets.end() ? it->second->widget : nullptr;
}

std::shared_ptr<WidgetInfo> GtkWidgetHelper::getWidgetInfo(const std::string& name) const {
    auto it = m_widgets.find(name);
    return it != m_widgets.end() ? it->second : nullptr;
}

bool GtkWidgetHelper::hasWidget(const std::string& name) const {
    return m_widgets.find(name) != m_widgets.end();
}

void GtkWidgetHelper::addWidget(const std::string& name, GtkWidget* widget, const std::string& type) {
    if (!widget || name.empty()) return;
    auto info = std::make_shared<WidgetInfo>();
    info->widget = widget;
    info->type = type;
    info->x = info->y = info->width = info->height = -1;
    m_widgets[name] = info;
}

void GtkWidgetHelper::removeWidget(const std::string& name, bool destroy) {
    auto it = m_widgets.find(name);
    if (it != m_widgets.end()) {
        if (destroy && it->second->widget) destroyWidget(it->second->widget);
        m_widgets.erase(it);
    }
}

void GtkWidgetHelper::showWidget(const std::string& name) {
    auto w = getWidget(name);
    if (w) gtk_widget_set_visible(w, TRUE);
}

void GtkWidgetHelper::hideWidget(const std::string& name) {
    auto w = getWidget(name);
    if (w) gtk_widget_set_visible(w, FALSE);
}

void GtkWidgetHelper::setWidgetVisible(const std::string& name, bool visible) {
    auto w = getWidget(name);
    if (w) gtk_widget_set_visible(w, visible);
}

void GtkWidgetHelper::enableWidget(const std::string& name) {
    auto w = getWidget(name);
    if (w) gtk_widget_set_sensitive(w, TRUE);
}

void GtkWidgetHelper::disableWidget(const std::string& name) {
    auto w = getWidget(name);
    if (w) gtk_widget_set_sensitive(w, FALSE);
}

void GtkWidgetHelper::setWidgetSensitive(const std::string& name, bool sensitive) {
    auto w = getWidget(name);
    if (w) gtk_widget_set_sensitive(w, sensitive);
}

std::vector<std::string> GtkWidgetHelper::getWidgetNames() const {
    std::vector<std::string> names;
    for (const auto& p : m_widgets) names.push_back(p.first);
    return names;
}

std::vector<std::string> GtkWidgetHelper::getWidgetNamesByType(const std::string& type) const {
    std::vector<std::string> names;
    for (const auto& p : m_widgets)
        if (p.second->type == type) names.push_back(p.first);
    return names;
}

void GtkWidgetHelper::clearWidgets(bool destroy) {
    if (destroy) {
        for (const auto& p : m_widgets)
            if (p.second->widget) destroyWidget(p.second->widget);
    }
    m_widgets.clear();
    m_callbacks.clear();
}