/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 * GtkWidgetHelper v0.1.1 for GTK4
 */

#ifndef GTK_WIDGET_HELPER_HPP
#define GTK_WIDGET_HELPER_HPP

#ifndef __cplusplus
    #error "This header requires C++. Please compile with a C++ compiler."
#endif

#include <gtk/gtk.h>
#include <glib.h>
#include <string>
#include <vector>
#include <functional>
#include <map>
#include <memory>
#include <algorithm>

// 全局变量声明
extern bool g_window_is_dragging;
extern guint g_drag_check_timeout;

// 函数声明
bool isWindowDragging(GtkWindow* window);
void initDragDetection(GtkWindow* window);
void gui_run_main_loop();
void gui_quit_main_loop();
void gtkBoxPackStart(GtkWidget* box, GtkWidget* child, bool expand, bool fill, int padding);
void gtkContainerAdd(GtkWidget* container, GtkWidget* child);
void gtkFrameSetLabelAlign(GtkWidget* frame, float xalign, float yalign = -1);
gint runDialog(GtkDialog* dialog);
void destroyWidget(GtkWidget* widget);
void addBoxChild(GtkWidget* box, GtkWidget* child);

template<typename Func>
void gui_idle_call(Func&& func) {
    using FuncType = typename std::decay<Func>::type;
    auto* func_ptr = new FuncType(std::forward<Func>(func));
    g_main_context_invoke(nullptr, [](gpointer data) -> gboolean {
        auto* f = static_cast<FuncType*>(data);
        (*f)();
        delete f;
        return G_SOURCE_REMOVE;
    }, func_ptr);
}

template<typename Func>
void gui_idle_call_wait_drag(Func&& func, GtkWindow* window) {
    using FuncType = typename std::decay<Func>::type;
    if (g_window_is_dragging) {
        struct WaitData {
            FuncType func;
            GtkWindow* window;
            int retry_count;
        };
        auto* wait_data = new WaitData{
            std::forward<Func>(func),
            window,
            0
        };
        g_timeout_add(50, [](gpointer data) -> gboolean {
            auto* wd = static_cast<WaitData*>(data);
            g_window_is_dragging = isWindowDragging(wd->window);
            if (!g_window_is_dragging) {
                wd->func();
                delete wd;
                return G_SOURCE_REMOVE;
            }
            if (++wd->retry_count > 200) {
                printf("[WARN] Drag wait timeout, forcing dialog\n");
                wd->func();
                delete wd;
                return G_SOURCE_REMOVE;
            }
            return G_SOURCE_CONTINUE;
        }, wait_data);
        return;
    }
    gui_idle_call(std::forward<Func>(func));
}

template<typename Func>
void wait_drag_sync(Func&& func, GtkWindow* window) {
    using FuncType = typename std::decay<Func>::type;
    while (g_window_is_dragging) {
        g_window_is_dragging = isWindowDragging(window);
        g_usleep(50000);
    }
    FuncType func_copy(std::forward<Func>(func));
    func_copy();
}

template<typename Func, typename Callback>
void wait_drag_sync_with_callback(Func&& func, Callback&& callback, GtkWindow* window) {
    using FuncType = typename std::decay<Func>::type;
    using CallbackType = typename std::decay<Callback>::type;
    while (g_window_is_dragging) {
        g_window_is_dragging = isWindowDragging(window);
        g_usleep(50000);
    }
    FuncType func_copy(std::forward<Func>(func));
    CallbackType callback_copy(std::forward<Callback>(callback));
    bool result = func_copy();
    callback_copy(result);
}

template<typename Func, typename Callback>
void gui_idle_call_with_callback(Func&& func, Callback&& callback, GtkWindow* window) {
    using FuncType = typename std::decay<Func>::type;
    using CallbackType = typename std::decay<Callback>::type;
    if (g_window_is_dragging) {
        struct WaitData {
            FuncType func;
            CallbackType callback;
            GtkWindow* window;
            int retry_count;
        };
        auto* wait_data = new WaitData{
            std::forward<Func>(func),
            std::forward<Callback>(callback),
            window,
            0
        };
        g_timeout_add(50, [](gpointer data) -> gboolean {
            auto* wd = static_cast<WaitData*>(data);
            g_window_is_dragging = isWindowDragging(wd->window);
            if (!g_window_is_dragging) {
                auto* func_ptr = new FuncType(std::move(wd->func));
                auto* callback_ptr = new CallbackType(std::move(wd->callback));
                g_main_context_invoke(nullptr, [](gpointer data) -> gboolean {
                    auto* pair = static_cast<std::pair<FuncType*, CallbackType*>*>(data);
                    bool result = (*pair->first)();
                    (*pair->second)(result);
                    delete pair->first;
                    delete pair->second;
                    delete pair;
                    return G_SOURCE_REMOVE;
                }, new std::pair<FuncType*, CallbackType*>(func_ptr, callback_ptr));
                delete wd;
                return G_SOURCE_REMOVE;
            }
            if (++wd->retry_count > 200) {
                printf("[WARN] Drag wait timeout, forcing dialog\n");
                auto* func_ptr = new FuncType(std::move(wd->func));
                auto* callback_ptr = new CallbackType(std::move(wd->callback));
                g_main_context_invoke(nullptr, [](gpointer data) -> gboolean {
                    auto* pair = static_cast<std::pair<FuncType*, CallbackType*>*>(data);
                    bool result = (*pair->first)();
                    (*pair->second)(result);
                    delete pair->first;
                    delete pair->second;
                    delete pair;
                    return G_SOURCE_REMOVE;
                }, new std::pair<FuncType*, CallbackType*>(func_ptr, callback_ptr));
                delete wd;
                return G_SOURCE_REMOVE;
            }
            return G_SOURCE_CONTINUE;
        }, wait_data);
        return;
    }
    auto* func_ptr = new FuncType(std::forward<Func>(func));
    auto* callback_ptr = new CallbackType(std::forward<Callback>(callback));
    g_main_context_invoke(nullptr, [](gpointer data) -> gboolean {
        auto* pair = static_cast<std::pair<FuncType*, CallbackType*>*>(data);
        bool result = (*pair->first)();
        (*pair->second)(result);
        delete pair->first;
        delete pair->second;
        delete pair;
        return G_SOURCE_REMOVE;
    }, new std::pair<FuncType*, CallbackType*>(func_ptr, callback_ptr));
}

// 对话框函数
std::string showFileChooser(GtkWindow* parent, bool open = true);
std::string showFolderChooser(GtkWindow* parent);
void showInfoDialog(GtkWindow* parent, const char* title, const char* message);
void showWarningDialog(GtkWindow* parent, const char* title, const char* message);
void showErrorDialog(GtkWindow* parent, const char* title, const char* message);
bool showConfirmDialog(GtkWindow* parent, const char* title, const char* message);
std::string showInputDialog(GtkWindow* parent, const char* title, const char* message);
std::string showSaveFileDialog(GtkWindow* parent,
                               const std::string& default_filename = "",
                               const std::vector<std::pair<std::string, std::string>>& filters = {});

// 线程安全对话框
void waitForDragEnd(GtkWindow* window);
std::string showInputDialogSyncInThread(GtkWindow* parent, const char* title, const char* message);
bool showConfirmDialogSyncInThread(GtkWindow* parent, const char* title, const char* message);
void showErrorDialogSyncInThread(GtkWindow* parent, const char* title, const char* message);
void showInfoDialogSyncInThread(GtkWindow* parent, const char* title, const char* message);
void showWarningDialogSyncInThread(GtkWindow* parent, const char* title, const char* message);
std::string showFileChooserSyncInThread(GtkWindow* parent, bool open = true);
std::string showFolderChooserSyncInThread(GtkWindow* parent);
std::string showSaveFileDialogSyncInThread(GtkWindow* parent,
                                       const std::string& default_filename = "",
                                       const std::vector<std::pair<std::string, std::string>>& filters = {});

#ifndef G_CONNECT_DEFAULT
#define G_CONNECT_DEFAULT ((GConnectFlags)0)
#endif

struct CallbackData {
    std::function<void()> func;
    virtual ~CallbackData() = default;
};

struct WidgetInfo {
    GtkWidget* widget;
    std::string type;
    int x;
    int y;
    int width;
    int height;
};

enum class LayoutType {
    FIXED,
    BOX,
    GRID,
    NONE
};

class GtkWidgetHelper {
private:
    GtkWidget* m_parent;
    std::map<std::string, std::shared_ptr<WidgetInfo>> m_widgets;
    std::map<std::string, std::shared_ptr<CallbackData>> m_callbacks;
    LayoutType m_layoutType;
    
public:
    explicit GtkWidgetHelper(GtkWidget* parent = nullptr);

    ~GtkWidgetHelper();

    void addNotebookPage(GtkWidget* notebook, GtkWidget* child,
                        const std::string& label,
                        bool closeable = false);

    void addToGrid(GtkWidget* grid, GtkWidget* child,
                  int left, int top,
                  int width = 1, int height = 1);

    // 布局控制
    void setWidgetPosition(GtkWidget* widget, int x, int y);
    void setWidgetSize(GtkWidget* widget, int width, int height);
    void setWidgetGeometry(GtkWidget* widget, int x, int y, int width, int height);
    void setWidgetExpand(GtkWidget* widget, bool hexpand = true, bool vexpand = true);
    void setWidgetAlignment(GtkWidget* widget, float xalign = 0.0, float yalign = 0.0);
    void setWidgetMargin(GtkWidget* widget, int top = 0, int bottom = 0, int left = 0, int right = 0);
    void setWidgetPadding(GtkWidget* widget, int padding = 0);

    // 属性和内容控制
    const char* getLabelText(GtkWidget* label) const;
    void setLabelText(GtkWidget* label, const std::string& text);
    void setLabelMarkup(GtkWidget* label, const std::string& markup);

    const char* getEntryText(GtkWidget* entry) const;
    void setEntryText(GtkWidget* entry, const std::string& text);
    void clearEntry(GtkWidget* entry);

    std::string getTextAreaText(GtkWidget* textview) const;
    void setTextAreaText(GtkWidget* textview, const std::string& text);
    void appendTextAreaText(GtkWidget* textview, const std::string& text);

    bool getCheckboxState(GtkWidget* checkbox) const;
    void setCheckboxState(GtkWidget* checkbox, bool state);

    int getSelectedRadioIndex(const std::string& groupName) const;
    void setSelectedRadioIndex(const std::string& groupName, int index);

    int getComboSelectedIndex(GtkWidget* combo) const;
    void setComboSelectedIndex(GtkWidget* combo, int index);
    std::string getComboSelectedText(GtkWidget* combo) const;
    void addComboItem(GtkWidget* combo, const std::string& item);
    void removeComboItem(GtkWidget* combo, int index);

    double getProgressValue(GtkWidget* progressBar) const;
    void setProgressValue(GtkWidget* progressBar, double fraction);
    void pulseProgressBar(GtkWidget* progressBar);

    double getScaleValue(GtkWidget* scale) const;
    void setScaleValue(GtkWidget* scale, double value);

    bool getSwitchState(GtkWidget* switchBtn) const;
    void setSwitchState(GtkWidget* switchBtn, bool state);

    double getSpinValue(GtkWidget* spin) const;
    void setSpinValue(GtkWidget* spin, double value);

    void getCalendarDate(GtkWidget* calendar, guint& year, guint& month, guint& day) const;
    void setCalendarDate(GtkWidget* calendar, guint year, guint month, guint day);

    // 信号绑定
    void bindClick(GtkWidget* button, std::function<void()> callback);
    void bindToggled(GtkWidget* toggleButton, std::function<void()> callback);
    void bindValueChanged(GtkWidget* widget, std::function<void()> callback);
    void bindTextChanged(GtkWidget* entry, std::function<void()> callback);
    void bindSelectionChanged(GtkWidget* widget, std::function<void()> callback);
    void bindRowActivated(GtkWidget* treeview, std::function<void(int)> callback);

    // 组件管理
    GtkWidget* getWidget(const std::string& name) const;
    std::shared_ptr<WidgetInfo> getWidgetInfo(const std::string& name) const;
    bool hasWidget(const std::string& name) const;
    void addWidget(const std::string& name, GtkWidget* widget, const std::string& type = "unknown");
    void removeWidget(const std::string& name, bool destroy = true);
    void showWidget(const std::string& name);
    void hideWidget(const std::string& name);
    void setWidgetVisible(const std::string& name, bool visible);
    void enableWidget(const std::string& name);
    void disableWidget(const std::string& name);
    void setWidgetSensitive(const std::string& name, bool sensitive);
    std::vector<std::string> getWidgetNames() const;
    std::vector<std::string> getWidgetNamesByType(const std::string& type) const;
    void clearWidgets(bool destroy = true);

    GtkWidget* getParent() const { return m_parent; }
    LayoutType getLayoutType() const { return m_layoutType; }
};

#endif // GTK_WIDGET_HELPER_HPP