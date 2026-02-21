// GtkWidgetHelper.cpp
#include "GtkWidgetHelper.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>


// 检测窗口是否在拖动中
bool isWindowDragging(GtkWindow* window) {
    GdkWindow* gdk_window = gtk_widget_get_window(GTK_WIDGET(window));
    if (!gdk_window) return false;
    
    // 获取鼠标状态
    GdkModifierType mask;
    gdk_window_get_device_position(gdk_window, 
        gdk_device_manager_get_client_pointer(
            gdk_display_get_device_manager(gdk_window_get_display(gdk_window))),
        nullptr, nullptr, &mask);
    
    // 检查左键是否按下（拖动标志）
    return (mask & GDK_BUTTON1_MASK) != 0;
}

// 初始化拖动检测
void initDragDetection(GtkWindow* window) {
    // 每秒检测60次
    g_drag_check_timeout = g_timeout_add(16, [](gpointer data) -> gboolean {
        GtkWindow* win = GTK_WINDOW(data);
        bool was_dragging = g_window_is_dragging;
        g_window_is_dragging = isWindowDragging(win);
        
        if (was_dragging != g_window_is_dragging) {
            if (g_window_is_dragging) {
#ifdef _DEBUG
                printf("[DEBUG] Window started dragging\n");
#endif
            } else {
#ifdef _DEBUG
                printf("[DEBUG] Window stopped dragging\n");
#endif
            }
        }
        return G_SOURCE_CONTINUE;
    }, window);
}

// 选择文件
std::string showFileChooser(GtkWindow* parent, bool open) {
	GtkWidget* dialog;

	if (open) {
		dialog = gtk_file_chooser_dialog_new("Select file 选择文件",
		                                     parent,
		                                     GTK_FILE_CHOOSER_ACTION_OPEN,
		                                     "_Cancel取消", GTK_RESPONSE_CANCEL,
		                                     "_Open打开", GTK_RESPONSE_ACCEPT,
		                                     NULL);
	} else {
		dialog = gtk_file_chooser_dialog_new("Save file 保存文件",
		                                     parent,
		                                     GTK_FILE_CHOOSER_ACTION_SAVE,
		                                     "_Cancel取消", GTK_RESPONSE_CANCEL,
		                                     "_Save保存", GTK_RESPONSE_ACCEPT,
		                                     NULL);
	}

	// 设置过滤器
	GtkFileFilter* filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, "All files 所有文件 (*.*)");
	gtk_file_filter_add_pattern(filter, "*");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

	gint result = gtk_dialog_run(GTK_DIALOG(dialog));
	std::string filename;

	if (result == GTK_RESPONSE_ACCEPT) {
		char* file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
		if (file) {
			filename = file;
			g_free(file);
		}
	}

	gtk_widget_destroy(dialog);
	return filename;
}

// 选择文件夹
const char* showFolderChooser(GtkWindow* parent) {
	GtkWidget* dialog = gtk_file_chooser_dialog_new("Select folder 选择文件夹",
	                    parent,
	                    GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
	                    "_Cancel取消", GTK_RESPONSE_CANCEL,
	                    "_Select选择", GTK_RESPONSE_ACCEPT,
	                    NULL);

	gint result = gtk_dialog_run(GTK_DIALOG(dialog));
	const char* folder = nullptr;

	if (result == GTK_RESPONSE_ACCEPT) {
		char* dir = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
		if (dir) {
			folder = dir;
			g_free(dir);
		}
	}

	gtk_widget_destroy(dialog);
	return folder;
}
// 信息对话框
void showInfoDialog(GtkWindow* parent, const char* title, const char* message) {
	GtkWidget* dialog = gtk_message_dialog_new(parent,
	                    GTK_DIALOG_MODAL,
	                    GTK_MESSAGE_INFO,
	                    GTK_BUTTONS_OK,
	                    "%s", message);
	gtk_window_set_title(GTK_WINDOW(dialog), title);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}

// 警告对话框
void showWarningDialog(GtkWindow* parent, const char* title, const char* message) {
	GtkWidget* dialog = gtk_message_dialog_new(parent,
	                    GTK_DIALOG_MODAL,
	                    GTK_MESSAGE_WARNING,
	                    GTK_BUTTONS_OK,
	                    "%s", message);
	gtk_window_set_title(GTK_WINDOW(dialog), title);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}

// 错误对话框
void showErrorDialog(GtkWindow* parent, const char* title, const char* message) {
	GtkWidget* dialog = gtk_message_dialog_new(parent,
	                    GTK_DIALOG_MODAL,
	                    GTK_MESSAGE_ERROR,
	                    GTK_BUTTONS_OK,
	                    "%s", message);
	gtk_window_set_title(GTK_WINDOW(dialog), title);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}

// 确认对话框（是/否）
bool showConfirmDialog(GtkWindow* parent, const char* title, const char* message) {
	GtkWidget* dialog = gtk_message_dialog_new(parent,
	                    GTK_DIALOG_MODAL,
	                    GTK_MESSAGE_QUESTION,
	                    GTK_BUTTONS_YES_NO,
	                    "%s", message);
	gtk_window_set_title(GTK_WINDOW(dialog), title);

	gint result = gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);

	return (result == GTK_RESPONSE_YES);
}
// 文件选择对话框函数
std::string showSaveFileDialog(GtkWindow* parent,
                               const std::string& default_filename,
                               const std::vector<std::pair<std::string, std::string>>& filters) {
	GtkWidget* dialog = gtk_file_chooser_dialog_new("Saving files 保存文件",
	                    parent,
	                    GTK_FILE_CHOOSER_ACTION_SAVE,
	                    "_Cancel取消", GTK_RESPONSE_CANCEL,
	                    "_Save保存", GTK_RESPONSE_ACCEPT,
	                    NULL);

	// 设置默认文件名
	if (!default_filename.empty()) {
		gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), default_filename.c_str());
	}

	// 添加文件过滤器
	for (const auto& filter_pair : filters) {
		GtkFileFilter* filter = gtk_file_filter_new();
		gtk_file_filter_set_name(filter, filter_pair.first.c_str());
		gtk_file_filter_add_pattern(filter, filter_pair.second.c_str());
		gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
	}

	// 默认添加"所有文件"过滤器
	GtkFileFilter* all_filter = gtk_file_filter_new();
	gtk_file_filter_set_name(all_filter, "All files 所有文件 (*.*)");
	gtk_file_filter_add_pattern(all_filter, "*");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), all_filter);

	std::string filename;
	gint result = gtk_dialog_run(GTK_DIALOG(dialog));

	if (result == GTK_RESPONSE_ACCEPT) {
		char* file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
		if (file) {
			filename = file;
			g_free(file);
		}
	}

	gtk_widget_destroy(dialog);
	return filename;
}

GtkWidgetHelper::GtkWidgetHelper(GtkWidget* parent) 
    : m_parent(parent), m_layoutType(LayoutType::NONE) {}

GtkWidgetHelper::~GtkWidgetHelper() {
    clearWidgets(false);
}

// 内部辅助方法
void GtkWidgetHelper::setupWidget(const std::string& name, GtkWidget* widget, 
                                 const std::string& type, int x, int y, 
                                 int width, int height) {
    if (!widget) return;
    
    auto info = std::make_shared<WidgetInfo>();
    info->widget = widget;
    info->type = type;
    info->x = x;
    info->y = y;
    info->width = width;
    info->height = height;
    
    if (!name.empty()) {
        m_widgets[name] = info;
    }
    
    if (width > 0 && height > 0) {
        gtk_widget_set_size_request(widget, width, height);
    }
}

GtkWidget* GtkWidgetHelper::createAndPlace(GtkWidget* widget, int x, int y, 
                                          int width, int height) {
    if (!widget || !m_parent) return widget;
    
    if (width > 0 && height > 0) {
        gtk_widget_set_size_request(widget, width, height);
    }
    
    switch (m_layoutType) {
        case LayoutType::FIXED:
            if (GTK_IS_FIXED(m_parent)) {
                gtk_fixed_put(GTK_FIXED(m_parent), widget, x, y);
            }
            break;
        case LayoutType::BOX:
            if (GTK_IS_BOX(m_parent)) {
                gtk_box_pack_start(GTK_BOX(m_parent), widget, FALSE, FALSE, 0);
            }
            break;
        case LayoutType::GRID:
            if (GTK_IS_GRID(m_parent)) {
                gtk_grid_attach(GTK_GRID(m_parent), widget, 0, 0, 1, 1);
            }
            break;
        case LayoutType::NONE:
            if (GTK_IS_CONTAINER(m_parent)) {
                gtk_container_add(GTK_CONTAINER(m_parent), widget);
            }
            break;
    }
    
    return widget;
}

void GtkWidgetHelper::setParent(GtkWidget* parent, LayoutType layoutType) {
    m_parent = parent;
    m_layoutType = layoutType;
}

// === 创建基本组件 ===

GtkWidget* GtkWidgetHelper::createLabel(const std::string& text, 
                                       const std::string& name,
                                       int x, int y, 
                                       int width, int height,
                                       bool markup) {
    GtkWidget* label = gtk_label_new(nullptr);
    
    if (markup) {
        gtk_label_set_markup(GTK_LABEL(label), text.c_str());
    } else {
        gtk_label_set_text(GTK_LABEL(label), text.c_str());
    }
    
    setupWidget(name, label, "label", x, y, width, height);
    return createAndPlace(label, x, y, width, height);
}

GtkWidget* GtkWidgetHelper::createButton(const std::string& text, 
                                        const std::string& name,
                                        std::function<void()> onClick,
                                        int x, int y, 
                                        int width, int height,
                                        const std::string& icon) {
    GtkWidget* button;
    
    if (!icon.empty()) {
        button = gtk_button_new();
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        GtkWidget* image = gtk_image_new_from_icon_name(icon.c_str(), GTK_ICON_SIZE_BUTTON);
        GtkWidget* label = gtk_label_new(text.c_str());
        gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(button), box);
        gtk_widget_show_all(box);
    } else {
        button = gtk_button_new_with_label(text.c_str());
    }
    
    setupWidget(name, button, "button", x, y, width, height);
    
    if (onClick) {
        bindClick(button, onClick);
    }
    
    return createAndPlace(button, x, y, width, height);
}

GtkWidget* GtkWidgetHelper::createButtonWithIcon(const std::string& iconName,
                                                const std::string& text,
                                                const std::string& name,
                                                std::function<void()> onClick,
                                                int x, int y,
                                                int width, int height) {
    GtkWidget* button = gtk_button_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    
    GtkWidget* image = gtk_image_new_from_icon_name(iconName.c_str(), GTK_ICON_SIZE_BUTTON);
    gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);
    
    if (!text.empty()) {
        GtkWidget* label = gtk_label_new(text.c_str());
        gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    }
    
    gtk_container_add(GTK_CONTAINER(button), box);
    gtk_widget_show_all(box);
    
    setupWidget(name, button, "button", x, y, width, height);
    
    if (onClick) {
        bindClick(button, onClick);
    }
    
    return createAndPlace(button, x, y, width, height);
}

GtkWidget* GtkWidgetHelper::createEntry(const std::string& name,
                                       const std::string& defaultText,
                                       bool password,
                                       int x, int y, 
                                       int width, int height,
                                       int maxLength) {
    GtkWidget* entry = gtk_entry_new();
    
    if (!defaultText.empty()) {
        gtk_entry_set_text(GTK_ENTRY(entry), defaultText.c_str());
    }
    
    if (password) {
        gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
        gtk_entry_set_invisible_char(GTK_ENTRY(entry), '*');
    }
    
    if (maxLength > 0) {
        gtk_entry_set_max_length(GTK_ENTRY(entry), maxLength);
    }
    
    setupWidget(name, entry, "entry", x, y, width, height);
    return createAndPlace(entry, x, y, width, height);
}

GtkWidget* GtkWidgetHelper::createTextArea(const std::string& name,
                                          const std::string& defaultText,
                                          int x, int y,
                                          int width, int height,
                                          bool editable,
                                          bool wrap) {
    GtkWidget* textview = gtk_text_view_new();
    
    if (!defaultText.empty()) {
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
        gtk_text_buffer_set_text(buffer, defaultText.c_str(), -1);
    }
    
    gtk_text_view_set_editable(GTK_TEXT_VIEW(textview), editable);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(textview), 
                               wrap ? GTK_WRAP_WORD : GTK_WRAP_NONE);
    
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scrolled), textview);
    
    setupWidget(name, scrolled, "textarea", x, y, width, height);
    if (width > 0 && height > 0) {
        gtk_widget_set_size_request(scrolled, width, height);
    }
    
    return createAndPlace(scrolled, x, y, width, height);
}

GtkWidget* GtkWidgetHelper::createCheckbox(const std::string& text,
                                          const std::string& name,
                                          bool defaultState,
                                          int x, int y,
                                          int width, int height) {
    GtkWidget* checkbox = gtk_check_button_new_with_label(text.c_str());
    
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox), defaultState);
    
    setupWidget(name, checkbox, "checkbox", x, y, width, height);
    return createAndPlace(checkbox, x, y, width, height);
}

GtkWidget* GtkWidgetHelper::createRadioButtonGroup(const std::vector<std::string>& options,
                                                  const std::string& groupName,
                                                  int selectedIndex,
                                                  int x, int y,
                                                  int spacing) {
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, spacing);
    
    GSList* group = nullptr;
    for (size_t i = 0; i < options.size(); ++i) {
        GtkWidget* radio;
        if (i == 0) {
            radio = gtk_radio_button_new_with_label(nullptr, options[i].c_str());
            group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(radio));
        } else {
            radio = gtk_radio_button_new_with_label(group, options[i].c_str());
        }
        
        if (static_cast<int>(i) == selectedIndex) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio), TRUE);
        }
        
        gtk_box_pack_start(GTK_BOX(vbox), radio, FALSE, FALSE, 0);
        
        std::string btnName = groupName + "_radio_" + std::to_string(i);
        addWidget(btnName, radio, "radio");
    }
    
    setupWidget(groupName, vbox, "radiogroup", x, y, -1, -1);
    return createAndPlace(vbox, x, y, -1, -1);
}

GtkWidget* GtkWidgetHelper::createComboBox(const std::vector<std::string>& items,
                                          const std::string& name,
                                          int selectedIndex,
                                          int x, int y,
                                          int width, int height) {
    GtkWidget* combo = gtk_combo_box_text_new();
    
    for (const auto& item : items) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), item.c_str());
    }
    
    if (!items.empty() && selectedIndex >= 0 && selectedIndex < static_cast<int>(items.size())) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(combo), selectedIndex);
    }
    
    setupWidget(name, combo, "combobox", x, y, width, height);
    return createAndPlace(combo, x, y, width, height);
}

GtkWidget* GtkWidgetHelper::createListView(const std::vector<std::string>& columns,
                                          const std::string& name,
                                          int x, int y,
                                          int width, int height) {
    GType* types = new GType[columns.size()];
    for (size_t i = 0; i < columns.size(); ++i) {
        types[i] = G_TYPE_STRING;
    }
    
    GtkListStore* store = gtk_list_store_newv(columns.size(), types);
    delete[] types;
    
    GtkWidget* treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    
    for (size_t i = 0; i < columns.size(); ++i) {
        GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
        GtkTreeViewColumn* column = gtk_tree_view_column_new_with_attributes(
            columns[i].c_str(), renderer, "text", i, nullptr);
        gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);
    }
    
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scrolled), treeview);
    
    setupWidget(name, scrolled, "listview", x, y, width, height);
    if (width > 0 && height > 0) {
        gtk_widget_set_size_request(scrolled, width, height);
    }
    
    return createAndPlace(scrolled, x, y, width, height);
}

GtkWidget* GtkWidgetHelper::createProgressBar(const std::string& name,
                                             double fraction,
                                             bool showText,
                                             int x, int y,
                                             int width, int height) {
    GtkWidget* progress = gtk_progress_bar_new();
    
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress), fraction);
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(progress), showText);
    
    setupWidget(name, progress, "progressbar", x, y, width, height);
    return createAndPlace(progress, x, y, width, height);
}

GtkWidget* GtkWidgetHelper::createHScale(double min, double max,
                                        double step, double value,
                                        const std::string& name,
                                        int x, int y,
                                        int width, int height) {
    GtkWidget* scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, min, max, step);
    gtk_range_set_value(GTK_RANGE(scale), value);
    
    setupWidget(name, scale, "hscale", x, y, width, height);
    return createAndPlace(scale, x, y, width, height);
}

GtkWidget* GtkWidgetHelper::createVScale(double min, double max,
                                        double step, double value,
                                        const std::string& name,
                                        int x, int y,
                                        int width, int height) {
    GtkWidget* scale = gtk_scale_new_with_range(GTK_ORIENTATION_VERTICAL, min, max, step);
    gtk_range_set_value(GTK_RANGE(scale), value);
    
    setupWidget(name, scale, "vscale", x, y, width, height);
    return createAndPlace(scale, x, y, width, height);
}

GtkWidget* GtkWidgetHelper::createScrolledWindow(GtkWidget* child,
                                                const std::string& name,
                                                int x, int y,
                                                int width, int height) {
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);
    
    if (child) {
        gtk_container_add(GTK_CONTAINER(scrolled), child);
    }
    
    setupWidget(name, scrolled, "scrolledwindow", x, y, width, height);
    return createAndPlace(scrolled, x, y, width, height);
}

GtkWidget* GtkWidgetHelper::createFrame(const std::string& label,
                                       GtkWidget* child,
                                       const std::string& name,
                                       int x, int y,
                                       int width, int height) {
    GtkWidget* frame = gtk_frame_new(label.empty() ? nullptr : label.c_str());
    
    if (child) {
        gtk_container_add(GTK_CONTAINER(frame), child);
    }
    
    setupWidget(name, frame, "frame", x, y, width, height);
    return createAndPlace(frame, x, y, width, height);
}

GtkWidget* GtkWidgetHelper::createPaned(GtkOrientation orientation,
                                       GtkWidget* child1,
                                       GtkWidget* child2,
                                       const std::string& name,
                                       int x, int y,
                                       int width, int height) {
    GtkWidget* paned = gtk_paned_new(orientation);
    
    if (child1) {
        gtk_paned_add1(GTK_PANED(paned), child1);
    }
    
    if (child2) {
        gtk_paned_add2(GTK_PANED(paned), child2);
    }
    
    setupWidget(name, paned, "paned", x, y, width, height);
    return createAndPlace(paned, x, y, width, height);
}

GtkWidget* GtkWidgetHelper::createNotebook(const std::string& name,
                                          int x, int y,
                                          int width, int height) {
    GtkWidget* notebook = gtk_notebook_new();
    
    setupWidget(name, notebook, "notebook", x, y, width, height);
    return createAndPlace(notebook, x, y, width, height);
}

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
        
        gtk_box_pack_start(GTK_BOX(hbox), pageLabel, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), closeButton, FALSE, FALSE, 0);
        gtk_widget_show_all(hbox);
        
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), child, hbox);
        
        g_signal_connect_swapped(closeButton, "clicked",
                                G_CALLBACK(gtk_widget_destroy), child);
    }
}

GtkWidget* GtkWidgetHelper::createToolbar(const std::string& name,
                                         int x, int y,
                                         int width, int height) {
    GtkWidget* toolbar = gtk_toolbar_new();
    gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_ICONS);
    
    setupWidget(name, toolbar, "toolbar", x, y, width, height);
    return createAndPlace(toolbar, x, y, width, height);
}

void GtkWidgetHelper::addToolbarButton(GtkWidget* toolbar, const std::string& iconName,
                                      const std::string& tooltip,
                                      std::function<void()> onClick) {
    if (!GTK_IS_TOOLBAR(toolbar)) return;
    
    GtkToolItem* item = gtk_tool_button_new(nullptr, nullptr);
    if (!iconName.empty()) {
        GtkWidget* image = gtk_image_new_from_icon_name(iconName.c_str(), GTK_ICON_SIZE_LARGE_TOOLBAR);
        gtk_tool_button_set_icon_widget(GTK_TOOL_BUTTON(item), image);
    }
    
    if (!tooltip.empty()) {
        gtk_tool_item_set_tooltip_text(item, tooltip.c_str());
    }
    
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), item, -1);
    
    if (onClick) {
        auto data = std::make_shared<CallbackData>();
        data->func = onClick;
        std::string key = "toolbtn_" + std::to_string(reinterpret_cast<uintptr_t>(item));
        m_callbacks[key] = data;
        
        g_signal_connect_data(item, "clicked",
            G_CALLBACK(+[](GtkToolItem*, gpointer data) {
                auto cb = static_cast<CallbackData*>(data);
                if (cb && cb->func) cb->func();
            }), data.get(), nullptr, G_CONNECT_DEFAULT);
    }
}

GtkWidget* GtkWidgetHelper::createMenuBar(const std::string& name,
                                         int x, int y,
                                         int width, int height) {
    GtkWidget* menubar = gtk_menu_bar_new();
    
    setupWidget(name, menubar, "menubar", x, y, width, height);
    return createAndPlace(menubar, x, y, width, height);
}

GtkWidget* GtkWidgetHelper::createStatusBar(const std::string& name,
                                           int x, int y,
                                           int width, int height) {
    GtkWidget* statusbar = gtk_statusbar_new();
    
    setupWidget(name, statusbar, "statusbar", x, y, width, height);
    return createAndPlace(statusbar, x, y, width, height);
}

void GtkWidgetHelper::showStatusMessage(GtkWidget* statusbar, 
                                       const std::string& message,
                                       int contextId) {
    if (GTK_IS_STATUSBAR(statusbar)) {
        gtk_statusbar_push(GTK_STATUSBAR(statusbar), contextId, message.c_str());
    }
}

GtkWidget* GtkWidgetHelper::createBox(GtkOrientation orientation,
                                     int spacing,
                                     bool homogeneous,
                                     const std::string& name,
                                     int x, int y,
                                     int width, int height) {
    GtkWidget* box = gtk_box_new(orientation, spacing);
    gtk_box_set_homogeneous(GTK_BOX(box), homogeneous);
    
    setupWidget(name, box, "box", x, y, width, height);
    return createAndPlace(box, x, y, width, height);
}

void GtkWidgetHelper::addToBox(GtkWidget* box, GtkWidget* child,
                              bool expand, bool fill,
                              int padding) {
    if (GTK_IS_BOX(box) && child) {
        gtk_box_pack_start(GTK_BOX(box), child, expand, fill, padding);
    }
}

GtkWidget* GtkWidgetHelper::createGrid(const std::string& name,
                                      int rowSpacing,
                                      int columnSpacing,
                                      int x, int y,
                                      int width, int height) {
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), rowSpacing);
    gtk_grid_set_column_spacing(GTK_GRID(grid), columnSpacing);
    
    setupWidget(name, grid, "grid", x, y, width, height);
    return createAndPlace(grid, x, y, width, height);
}

void GtkWidgetHelper::addToGrid(GtkWidget* grid, GtkWidget* child,
                               int left, int top,
                               int width, int height) {
    if (GTK_IS_GRID(grid) && child) {
        gtk_grid_attach(GTK_GRID(grid), child, left, top, width, height);
    }
}

GtkWidget* GtkWidgetHelper::createCalendar(const std::string& name,
                                          int x, int y,
                                          int width, int height) {
    GtkWidget* calendar = gtk_calendar_new();
    
    setupWidget(name, calendar, "calendar", x, y, width, height);
    return createAndPlace(calendar, x, y, width, height);
}

GtkWidget* GtkWidgetHelper::createColorButton(const std::string& name,
                                             const GdkRGBA* initialColor,
                                             int x, int y,
                                             int width, int height) {
    GtkWidget* colorBtn = gtk_color_button_new();
    
    if (initialColor) {
        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(colorBtn), initialColor);
    }
    
    setupWidget(name, colorBtn, "colorbutton", x, y, width, height);
    return createAndPlace(colorBtn, x, y, width, height);
}


GtkWidget* GtkWidgetHelper::createFileChooserButton(const std::string& title,
                                                   GtkFileChooserAction action,
                                                   const std::string& name,
                                                   int x, int y,
                                                   int width, int height) {
    GtkWidget* fileBtn = gtk_file_chooser_button_new(title.c_str(), action);
    
    setupWidget(name, fileBtn, "filechooser", x, y, width, height);
    return createAndPlace(fileBtn, x, y, width, height);
}

GtkWidget* GtkWidgetHelper::createSpinButton(double min, double max, double step,
                                            const std::string& name,
                                            double value,
                                            int x, int y,
                                            int width, int height) {
    GtkWidget* spin = gtk_spin_button_new_with_range(min, max, step);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), value);
    
    setupWidget(name, spin, "spinbutton", x, y, width, height);
    return createAndPlace(spin, x, y, width, height);
}

GtkWidget* GtkWidgetHelper::createSwitch(const std::string& name,
                                        bool active,
                                        int x, int y,
                                        int width, int height) {
    GtkWidget* switchBtn = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(switchBtn), active);
    
    setupWidget(name, switchBtn, "switch", x, y, width, height);
    return createAndPlace(switchBtn, x, y, width, height);
}

GtkWidget* GtkWidgetHelper::createLinkButton(const std::string& url,
                                            const std::string& label,
                                            const std::string& name,
                                            int x, int y,
                                            int width, int height) {
    GtkWidget* linkBtn = gtk_link_button_new_with_label(url.c_str(), label.c_str());
    
    setupWidget(name, linkBtn, "linkbutton", x, y, width, height);
    return createAndPlace(linkBtn, x, y, width, height);
}

GtkWidget* GtkWidgetHelper::createSearchEntry(const std::string& name,
                                             const std::string& placeholder,
                                             int x, int y,
                                             int width, int height) {
    GtkWidget* searchEntry = gtk_search_entry_new();
    
    if (!placeholder.empty()) {
        gtk_entry_set_placeholder_text(GTK_ENTRY(searchEntry), placeholder.c_str());
    }
    
    setupWidget(name, searchEntry, "searchentry", x, y, width, height);
    return createAndPlace(searchEntry, x, y, width, height);
}

GtkWidget* GtkWidgetHelper::createTabBar(const std::vector<std::string>& tabs,
                                        const std::string& name,
                                        int x, int y,
                                        int width, int height) {
    GtkWidget* notebook = gtk_notebook_new();
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_TOP);
    
    for (const auto& tab : tabs) {
        GtkWidget* page = gtk_label_new(tab.c_str());
        GtkWidget* tabLabel = gtk_label_new(tab.c_str());
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page, tabLabel);
    }
    
    setupWidget(name, notebook, "tabbar", x, y, width, height);
    return createAndPlace(notebook, x, y, width, height);
}

// === 布局控制 ===

void GtkWidgetHelper::setWidgetPosition(GtkWidget* widget, int x, int y) {
    if (!widget || m_layoutType != LayoutType::FIXED || !m_parent) return;
    
    if (GTK_IS_FIXED(m_parent)) {
        GtkWidget* parent = gtk_widget_get_parent(widget);
        if (parent) {
            gtk_container_remove(GTK_CONTAINER(parent), widget);
        }
        gtk_fixed_put(GTK_FIXED(m_parent), widget, x, y);
    }
}

void GtkWidgetHelper::setWidgetSize(GtkWidget* widget, int width, int height) {
    if (!widget) return;
    
    if (width > 0 && height > 0) {
        gtk_widget_set_size_request(widget, width, height);
    }
}

void GtkWidgetHelper::setWidgetGeometry(GtkWidget* widget, int x, int y, 
                                       int width, int height) {
    setWidgetPosition(widget, x, y);
    setWidgetSize(widget, width, height);
}

void GtkWidgetHelper::setWidgetExpand(GtkWidget* widget, 
                                     bool hexpand, bool vexpand) {
    if (!widget) return;
    
    gtk_widget_set_hexpand(widget, hexpand);
    gtk_widget_set_vexpand(widget, vexpand);
}

void GtkWidgetHelper::setWidgetAlignment(GtkWidget* widget, 
                                        float xalign, float yalign) {
    if (!widget) return;
    
    // 修复：GTK3 移除了 Misc 小部件，需要改用其他方式
    if (GTK_IS_LABEL(widget)) {
        // 对于标签，使用新的对齐方法
        gtk_label_set_xalign(GTK_LABEL(widget), xalign);
        gtk_label_set_yalign(GTK_LABEL(widget), yalign);
    } else if (GTK_IS_CONTAINER(widget)) {
        // 对于容器小部件，可以设置其子部件的对齐方式
        // 使用 CSS 或布局容器来控制对齐
        GtkStyleContext* context = gtk_widget_get_style_context(widget);
        GtkCssProvider* provider = gtk_css_provider_new();
        
        std::stringstream css;
        css << "* { xalign: " << xalign << "; yalign: " << yalign << "; }";
        
        gtk_css_provider_load_from_data(provider, css.str().c_str(), -1, nullptr);
        gtk_style_context_add_provider(context, 
                                      GTK_STYLE_PROVIDER(provider),
                                      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(provider);
    }
    // 注意：不是所有小部件都支持直接设置对齐，有些需要通过布局容器来实现
}

void GtkWidgetHelper::setWidgetMargin(GtkWidget* widget, 
                                     int top, int bottom,
                                     int left, int right) {
    if (!widget) return;
    
    GtkStyleContext* context = gtk_widget_get_style_context(widget);
    GtkCssProvider* provider = gtk_css_provider_new();
    
    std::stringstream css;
    css << "* { margin: " << top << "px " << right << "px " 
        << bottom << "px " << left << "px; }";
    
    gtk_css_provider_load_from_data(provider, css.str().c_str(), -1, nullptr);
    gtk_style_context_add_provider(context, 
                                  GTK_STYLE_PROVIDER(provider),
                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

void GtkWidgetHelper::setWidgetPadding(GtkWidget* widget, int padding) {
    setWidgetMargin(widget, padding, padding, padding, padding);
}


// === 属性和内容控制 ===

std::string GtkWidgetHelper::getLabelText(GtkWidget* label) const {
    if (GTK_IS_LABEL(label)) {
        const gchar* text = gtk_label_get_text(GTK_LABEL(label));
        return text ? text : "";
    }
    return "";
}

void GtkWidgetHelper::setLabelText(GtkWidget* label, const std::string& text) {
    if (GTK_IS_LABEL(label)) {
        gtk_label_set_text(GTK_LABEL(label), text.c_str());
    }
}

void GtkWidgetHelper::setLabelMarkup(GtkWidget* label, const std::string& markup) {
    if (GTK_IS_LABEL(label)) {
        gtk_label_set_markup(GTK_LABEL(label), markup.c_str());
    }
}

const char* GtkWidgetHelper::getEntryText(GtkWidget* entry) const {
    if (GTK_IS_ENTRY(entry)) {
        const char* text = gtk_entry_get_text(GTK_ENTRY(entry));
        return text ? text : "";
    }
    return "";
}

void GtkWidgetHelper::setEntryText(GtkWidget* entry, const std::string& text) {
    if (GTK_IS_ENTRY(entry)) {
        gtk_entry_set_text(GTK_ENTRY(entry), text.c_str());
    }
}

void GtkWidgetHelper::clearEntry(GtkWidget* entry) {
    setEntryText(entry, "");
}

const char* GtkWidgetHelper::getTextAreaText(GtkWidget* textview) const {
    if (GTK_IS_TEXT_VIEW(textview)) {
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
        GtkTextIter start, end;
        gtk_text_buffer_get_start_iter(buffer, &start);
        gtk_text_buffer_get_end_iter(buffer, &end);
        gchar* text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
        return text;
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
    if (GTK_IS_TOGGLE_BUTTON(checkbox)) {
        return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox));
    }
    return false;
}

void GtkWidgetHelper::setCheckboxState(GtkWidget* checkbox, bool state) {
    if (GTK_IS_TOGGLE_BUTTON(checkbox)) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox), state);
    }
}

int GtkWidgetHelper::getSelectedRadioIndex(const std::string& groupName) const {
    // 查找该组的所有单选按钮
    for (int i = 0; ; ++i) {
        std::string btnName = groupName + "_radio_" + std::to_string(i);
        auto it = m_widgets.find(btnName);
        if (it == m_widgets.end()) break;
        
        GtkWidget* radio = it->second->widget;
        if (GTK_IS_RADIO_BUTTON(radio) && 
            gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radio))) {
            return i;
        }
    }
    return -1;
}

void GtkWidgetHelper::setSelectedRadioIndex(const std::string& groupName, int index) {
    std::string btnName = groupName + "_radio_" + std::to_string(index);
    auto it = m_widgets.find(btnName);
    if (it != m_widgets.end() && GTK_IS_RADIO_BUTTON(it->second->widget)) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(it->second->widget), TRUE);
    }
}

int GtkWidgetHelper::getComboSelectedIndex(GtkWidget* combo) const {
    if (GTK_IS_COMBO_BOX(combo)) {
        return gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
    }
    return -1;
}

void GtkWidgetHelper::setComboSelectedIndex(GtkWidget* combo, int index) {
    if (GTK_IS_COMBO_BOX(combo)) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(combo), index);
    }
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
    if (GTK_IS_COMBO_BOX_TEXT(combo)) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), item.c_str());
    }
}

void GtkWidgetHelper::removeComboItem(GtkWidget* combo, int index) {
    if (GTK_IS_COMBO_BOX_TEXT(combo)) {
        gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(combo), index);
    }
}

double GtkWidgetHelper::getProgressValue(GtkWidget* progressBar) const {
    if (GTK_IS_PROGRESS_BAR(progressBar)) {
        return gtk_progress_bar_get_fraction(GTK_PROGRESS_BAR(progressBar));
    }
    return 0.0;
}

void GtkWidgetHelper::setProgressValue(GtkWidget* progressBar, double fraction) {
    if (GTK_IS_PROGRESS_BAR(progressBar)) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progressBar), fraction);
    }
}

void GtkWidgetHelper::pulseProgressBar(GtkWidget* progressBar) {
    if (GTK_IS_PROGRESS_BAR(progressBar)) {
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(progressBar));
    }
}

double GtkWidgetHelper::getScaleValue(GtkWidget* scale) const {
    if (GTK_IS_RANGE(scale)) {
        return gtk_range_get_value(GTK_RANGE(scale));
    }
    return 0.0;
}

void GtkWidgetHelper::setScaleValue(GtkWidget* scale, double value) {
    if (GTK_IS_RANGE(scale)) {
        gtk_range_set_value(GTK_RANGE(scale), value);
    }
}

bool GtkWidgetHelper::getSwitchState(GtkWidget* switchBtn) const {
    if (GTK_IS_SWITCH(switchBtn)) {
        return gtk_switch_get_active(GTK_SWITCH(switchBtn));
    }
    return false;
}

void GtkWidgetHelper::setSwitchState(GtkWidget* switchBtn, bool state) {
    if (GTK_IS_SWITCH(switchBtn)) {
        gtk_switch_set_active(GTK_SWITCH(switchBtn), state);
    }
}

double GtkWidgetHelper::getSpinValue(GtkWidget* spin) const {
    if (GTK_IS_SPIN_BUTTON(spin)) {
        return gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin));
    }
    return 0.0;
}

void GtkWidgetHelper::setSpinValue(GtkWidget* spin, double value) {
    if (GTK_IS_SPIN_BUTTON(spin)) {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), value);
    }
}

void GtkWidgetHelper::getCalendarDate(GtkWidget* calendar, 
                                     guint& year, guint& month, guint& day) const {
    if (GTK_IS_CALENDAR(calendar)) {
        gtk_calendar_get_date(GTK_CALENDAR(calendar), &year, &month, &day);
    }
}

void GtkWidgetHelper::setCalendarDate(GtkWidget* calendar, 
                                     guint year, guint month, guint day) {
    if (GTK_IS_CALENDAR(calendar)) {
        gtk_calendar_select_day(GTK_CALENDAR(calendar), day);
        gtk_calendar_select_month(GTK_CALENDAR(calendar), month, year);
    }
}

// === 信号绑定 ===

void GtkWidgetHelper::bindClick(GtkWidget* button, std::function<void()> callback) {
    if (GTK_IS_BUTTON(button) && callback) {
        auto data = std::make_shared<CallbackData>();
        data->func = callback;
        std::string key = "click_" + std::to_string(reinterpret_cast<uintptr_t>(button));
        m_callbacks[key] = data;
        
        g_signal_connect_data(button, "clicked", 
            G_CALLBACK(+[](GtkWidget*, gpointer data) {
                auto cb = static_cast<CallbackData*>(data);
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
            G_CALLBACK(+[](GtkWidget*, gpointer data) {
                auto cb = static_cast<CallbackData*>(data);
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
    
    if (GTK_IS_RANGE(widget)) {
        g_signal_connect_data(widget, "value-changed",
            G_CALLBACK(+[](GtkWidget*, gpointer data) {
                auto cb = static_cast<CallbackData*>(data);
                if (cb && cb->func) cb->func();
            }), data.get(), nullptr, G_CONNECT_DEFAULT);
    } else if (GTK_IS_SPIN_BUTTON(widget)) {
        g_signal_connect_data(widget, "value-changed",
            G_CALLBACK(+[](GtkWidget*, gpointer data) {
                auto cb = static_cast<CallbackData*>(data);
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
            G_CALLBACK(+[](GtkWidget*, gpointer data) {
                auto cb = static_cast<CallbackData*>(data);
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
            G_CALLBACK(+[](GtkWidget*, gpointer data) {
                auto cb = static_cast<CallbackData*>(data);
                if (cb && cb->func) cb->func();
            }), data.get(), nullptr, G_CONNECT_DEFAULT);
    }
}

void GtkWidgetHelper::bindRowActivated(GtkWidget* treeview, 
                                      std::function<void(int)> callback) {
    if (GTK_IS_TREE_VIEW(treeview) && callback) {
        auto data = std::make_shared<CallbackData>();
        // 这里需要特殊处理，因为需要传递行索引
        // 简化实现，实际使用时可能需要调整
        g_signal_connect(treeview, "row-activated",
            G_CALLBACK(+[](GtkTreeView* view, GtkTreePath* path, 
                          GtkTreeViewColumn* col, gpointer data) {
                auto func = static_cast<std::function<void(int)>*>(data);
                if (func) {
                    gint* indices = gtk_tree_path_get_indices(path);
                    if (indices) {
                        (*func)(indices[0]);
                    }
                }
            }), new std::function<void(int)>(callback));
    }
}

// === 组件管理 ===

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

void GtkWidgetHelper::addWidget(const std::string& name, GtkWidget* widget,
                               const std::string& type) {
    if (!widget || name.empty()) return;
    
    auto info = std::make_shared<WidgetInfo>();
    info->widget = widget;
    info->type = type;
    info->x = -1;
    info->y = -1;
    info->width = -1;
    info->height = -1;
    
    m_widgets[name] = info;
}

void GtkWidgetHelper::removeWidget(const std::string& name, bool destroy) {
    auto it = m_widgets.find(name);
    if (it != m_widgets.end()) {
        if (destroy && it->second->widget) {
            gtk_widget_destroy(it->second->widget);
        }
        m_widgets.erase(it);
    }
}

void GtkWidgetHelper::showWidget(const std::string& name) {
    auto widget = getWidget(name);
    if (widget) gtk_widget_show(widget);
}

void GtkWidgetHelper::hideWidget(const std::string& name) {
    auto widget = getWidget(name);
    if (widget) gtk_widget_hide(widget);
}

void GtkWidgetHelper::setWidgetVisible(const std::string& name, bool visible) {
    auto widget = getWidget(name);
    if (widget) {
        if (visible) {
            gtk_widget_show(widget);
        } else {
            gtk_widget_hide(widget);
        }
    }
}

void GtkWidgetHelper::enableWidget(const std::string& name) {
    auto widget = getWidget(name);
    if (widget) gtk_widget_set_sensitive(widget, TRUE);
}

void GtkWidgetHelper::disableWidget(const std::string& name) {
    auto widget = getWidget(name);
    if (widget) gtk_widget_set_sensitive(widget, FALSE);
}

void GtkWidgetHelper::setWidgetSensitive(const std::string& name, bool sensitive) {
    auto widget = getWidget(name);
    if (widget) gtk_widget_set_sensitive(widget, sensitive);
}

std::vector<std::string> GtkWidgetHelper::getWidgetNames() const {
    std::vector<std::string> names;
    for (const auto& pair : m_widgets) {
        names.push_back(pair.first);
    }
    return names;
}

std::vector<std::string> GtkWidgetHelper::getWidgetNamesByType(const std::string& type) const {
    std::vector<std::string> names;
    for (const auto& pair : m_widgets) {
        if (pair.second->type == type) {
            names.push_back(pair.first);
        }
    }
    return names;
}

void GtkWidgetHelper::clearWidgets(bool destroy) {
    if (destroy) {
        for (const auto& pair : m_widgets) {
            if (pair.second->widget) {
                gtk_widget_destroy(pair.second->widget);
            }
        }
    }
    m_widgets.clear();
    m_callbacks.clear();
}