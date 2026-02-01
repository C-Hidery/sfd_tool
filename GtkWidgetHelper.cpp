// GtkWidgetHelper.cpp
#include "GtkWidgetHelper.hpp"
#include <iostream>

GtkWidgetHelper::GtkWidgetHelper(GtkWidget* parent) 
    : m_parent(parent) {}

void GtkWidgetHelper::setParent(GtkWidget* parent) {
    m_parent = parent;
}

// === 创建基本组件 ===

GtkWidget* GtkWidgetHelper::createLabel(const std::string& text, 
                                       const std::string& name,
                                       int x, int y, 
                                       int width, int height) {
    GtkWidget* label = gtk_label_new(text.c_str());
    
    if (!name.empty()) {
        m_widgets[name] = label;
    }
    
    if (m_parent) {
        gtk_container_add(GTK_CONTAINER(m_parent), label);
    }
    
    if (x >= 0 && y >= 0 && m_parent && GTK_IS_FIXED(m_parent)) {
        gtk_fixed_put(GTK_FIXED(m_parent), label, x, y);
    }
    
    if (width > 0 && height > 0) {
        gtk_widget_set_size_request(label, width, height);
    }
    
    return label;
}

GtkWidget* GtkWidgetHelper::createButton(const std::string& text, 
                                        const std::string& name,
                                        std::function<void()> onClick,
                                        int x, int y, 
                                        int width, int height) {
    GtkWidget* button = gtk_button_new_with_label(text.c_str());
    
    if (!name.empty()) {
        m_widgets[name] = button;
    }
    
    if (m_parent) {
        gtk_container_add(GTK_CONTAINER(m_parent), button);
    }
    
    if (x >= 0 && y >= 0 && m_parent && GTK_IS_FIXED(m_parent)) {
        gtk_fixed_put(GTK_FIXED(m_parent), button, x, y);
    }
    
    if (width > 0 && height > 0) {
        gtk_widget_set_size_request(button, width, height);
    }
    
    if (onClick) {
        g_signal_connect_data(button, "clicked", 
            G_CALLBACK(+[](GtkWidget*, gpointer data) {
                auto func = static_cast<std::function<void()>*>(data);
                (*func)();
            }), new std::function<void()>(onClick),
            [](gpointer data, GClosure*) {
                delete static_cast<std::function<void()>*>(data);
            }, G_CONNECT_DEFAULT);
    }
    
    return button;
}

GtkWidget* GtkWidgetHelper::createEntry(const std::string& name,
                                       const std::string& defaultText,
                                       int x, int y, 
                                       int width, int height) {
    GtkWidget* entry = gtk_entry_new();
    
    if (!defaultText.empty()) {
        gtk_entry_set_text(GTK_ENTRY(entry), defaultText.c_str());
    }
    
    if (!name.empty()) {
        m_widgets[name] = entry;
    }
    
    if (m_parent) {
        gtk_container_add(GTK_CONTAINER(m_parent), entry);
    }
    
    if (x >= 0 && y >= 0 && m_parent && GTK_IS_FIXED(m_parent)) {
        gtk_fixed_put(GTK_FIXED(m_parent), entry, x, y);
    }
    
    if (width > 0 && height > 0) {
        gtk_widget_set_size_request(entry, width, height);
    }
    
    return entry;
}

GtkWidget* GtkWidgetHelper::createCheckbox(const std::string& text,
                                          const std::string& name,
                                          bool defaultState,
                                          int x, int y,
                                          int width, int height) {
    GtkWidget* checkbox = gtk_check_button_new_with_label(text.c_str());
    
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox), defaultState);
    
    if (!name.empty()) {
        m_widgets[name] = checkbox;
    }
    
    if (m_parent) {
        gtk_container_add(GTK_CONTAINER(m_parent), checkbox);
    }
    
    if (x >= 0 && y >= 0 && m_parent && GTK_IS_FIXED(m_parent)) {
        gtk_fixed_put(GTK_FIXED(m_parent), checkbox, x, y);
    }
    
    if (width > 0 && height > 0) {
        gtk_widget_set_size_request(checkbox, width, height);
    }
    
    return checkbox;
}

GtkWidget* GtkWidgetHelper::createRadioButton(const std::string& text,
                                             const std::string& name,
                                             GtkWidget* group,
                                             int x, int y,
                                             int width, int height) {
    GtkWidget* radio;
    if (group) {
        radio = gtk_radio_button_new_with_label_from_widget(
            GTK_RADIO_BUTTON(group), text.c_str());
    } else {
        radio = gtk_radio_button_new_with_label(nullptr, text.c_str());
    }
    
    if (!name.empty()) {
        m_widgets[name] = radio;
    }
    
    if (m_parent) {
        gtk_container_add(GTK_CONTAINER(m_parent), radio);
    }
    
    if (x >= 0 && y >= 0 && m_parent && GTK_IS_FIXED(m_parent)) {
        gtk_fixed_put(GTK_FIXED(m_parent), radio, x, y);
    }
    
    if (width > 0 && height > 0) {
        gtk_widget_set_size_request(radio, width, height);
    }
    
    return radio;
}

GtkWidget* GtkWidgetHelper::createComboBox(const std::vector<std::string>& items,
                                          const std::string& name,
                                          int x, int y,
                                          int width, int height) {
    GtkWidget* combo = gtk_combo_box_text_new();
    
    for (const auto& item : items) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), item.c_str());
    }
    
    if (!items.empty()) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    }
    
    if (!name.empty()) {
        m_widgets[name] = combo;
    }
    
    if (m_parent) {
        gtk_container_add(GTK_CONTAINER(m_parent), combo);
    }
    
    if (x >= 0 && y >= 0 && m_parent && GTK_IS_FIXED(m_parent)) {
        gtk_fixed_put(GTK_FIXED(m_parent), combo, x, y);
    }
    
    if (width > 0 && height > 0) {
        gtk_widget_set_size_request(combo, width, height);
    }
    
    return combo;
}

// ... 其他创建方法的实现类似 ...

// === 布局控制 ===

void GtkWidgetHelper::setFixedPosition(GtkWidget* widget, int x, int y) {
    if (m_parent && GTK_IS_FIXED(m_parent)) {
        gtk_fixed_put(GTK_FIXED(m_parent), widget, x, y);
    }
}

void GtkWidgetHelper::setSize(GtkWidget* widget, int width, int height) {
    if (width > 0 && height > 0) {
        gtk_widget_set_size_request(widget, width, height);
    }
}

void GtkWidgetHelper::setGeometry(GtkWidget* widget, int x, int y, int width, int height) {
    setFixedPosition(widget, x, y);
    setSize(widget, width, height);
}

void GtkWidgetHelper::setExpand(GtkWidget* widget, bool hexpand, bool vexpand) {
    gtk_widget_set_hexpand(widget, hexpand);
    gtk_widget_set_vexpand(widget, vexpand);
}

void GtkWidgetHelper::setAlignment(GtkWidget* widget, float xalign, float yalign) {
    if (GTK_IS_MISC(widget)) {
        gtk_misc_set_alignment(GTK_MISC(widget), xalign, yalign);
    }
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

std::string GtkWidgetHelper::getEntryText(GtkWidget* entry) const {
    if (GTK_IS_ENTRY(entry)) {
        const gchar* text = gtk_entry_get_text(GTK_ENTRY(entry));
        return text ? text : "";
    }
    return "";
}

void GtkWidgetHelper::setEntryText(GtkWidget* entry, const std::string& text) {
    if (GTK_IS_ENTRY(entry)) {
        gtk_entry_set_text(GTK_ENTRY(entry), text.c_str());
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

// ... 其他属性控制方法的实现 ...

// === 信号绑定 ===

void GtkWidgetHelper::bindClick(GtkWidget* button, std::function<void()> callback) {
    if (GTK_IS_BUTTON(button) && callback) {
        g_signal_connect_data(button, "clicked", 
            G_CALLBACK(+[](GtkWidget*, gpointer data) {
                auto func = static_cast<std::function<void()>*>(data);
                (*func)();
            }), new std::function<void()>(callback),
            [](gpointer data, GClosure*) {
                delete static_cast<std::function<void()>*>(data);
            }, G_CONNECT_DEFAULT);
    }
}

// === 组件管理 ===

GtkWidget* GtkWidgetHelper::getWidget(const std::string& name) const {
    auto it = m_widgets.find(name);
    return it != m_widgets.end() ? it->second : nullptr;
}

bool GtkWidgetHelper::hasWidget(const std::string& name) const {
    return m_widgets.find(name) != m_widgets.end();
}

void GtkWidgetHelper::addWidget(const std::string& name, GtkWidget* widget) {
    m_widgets[name] = widget;
}

void GtkWidgetHelper::removeWidget(const std::string& name) {
    m_widgets.erase(name);
}

void GtkWidgetHelper::clearWidgets() {
    m_widgets.clear();
}

std::vector<std::string> GtkWidgetHelper::getWidgetNames() const {
    std::vector<std::string> names;
    for (const auto& pair : m_widgets) {
        names.push_back(pair.first);
    }
    return names;
}