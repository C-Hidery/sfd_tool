// GtkWidgetHelper.hpp
#ifndef GTK_WIDGET_HELPER_HPP
#define GTK_WIDGET_HELPER_HPP

#include <gtk/gtk.h>
#include <string>
#include <vector>
#include <functional>
#include <map>

class GtkWidgetHelper {
private:
    GtkWidget* m_parent;
    std::map<std::string, GtkWidget*> m_widgets;
    
public:
    // 构造函数，传入父窗口或容器
    explicit GtkWidgetHelper(GtkWidget* parent = nullptr);
    
    // 设置父容器
    void setParent(GtkWidget* parent);
    
    // === 创建基本组件 ===
    
    // 创建标签
    GtkWidget* createLabel(const std::string& text, 
                          const std::string& name = "",
                          int x = -1, int y = -1, 
                          int width = -1, int height = -1);
    
    // 创建按钮
    GtkWidget* createButton(const std::string& text, 
                           const std::string& name = "",
                           std::function<void()> onClick = nullptr,
                           int x = -1, int y = -1, 
                           int width = -1, int height = -1);
    
    // 创建文本框
    GtkWidget* createEntry(const std::string& name = "",
                          const std::string& defaultText = "",
                          int x = -1, int y = -1, 
                          int width = -1, int height = -1);
    
    // 创建复选框
    GtkWidget* createCheckbox(const std::string& text,
                             const std::string& name = "",
                             bool defaultState = false,
                             int x = -1, int y = -1,
                             int width = -1, int height = -1);
    
    // 创建单选框
    GtkWidget* createRadioButton(const std::string& text,
                                const std::string& name = "",
                                GtkWidget* group = nullptr,
                                int x = -1, int y = -1,
                                int width = -1, int height = -1);
    
    // 创建组合框
    GtkWidget* createComboBox(const std::vector<std::string>& items,
                             const std::string& name = "",
                             int x = -1, int y = -1,
                             int width = -1, int height = -1);
    
    // 创建进度条
    GtkWidget* createProgressBar(const std::string& name = "",
                                double fraction = 0.0,
                                int x = -1, int y = -1,
                                int width = -1, int height = -1);
    
    // 创建滑块
    GtkWidget* createScale(const std::string& name = "",
                          double min = 0.0, double max = 100.0,
                          double step = 1.0, double value = 0.0,
                          int x = -1, int y = -1,
                          int width = -1, int height = -1);
    
    // 创建文本视图
    GtkWidget* createTextView(const std::string& name = "",
                             const std::string& defaultText = "",
                             int x = -1, int y = -1,
                             int width = -1, int height = -1);
    
    // 创建框架
    GtkWidget* createFrame(const std::string& label = "",
                          const std::string& name = "",
                          int x = -1, int y = -1,
                          int width = -1, int height = -1);
    
    // 创建盒子容器
    GtkWidget* createBox(GtkOrientation orientation = GTK_ORIENTATION_VERTICAL,
                        int spacing = 10,
                        const std::string& name = "",
                        int x = -1, int y = -1,
                        int width = -1, int height = -1);
    
    // 创建网格容器
    GtkWidget* createGrid(const std::string& name = "",
                         int x = -1, int y = -1,
                         int width = -1, int height = -1);
    
    // === 布局控制 ===
    
    // 固定位置布局
    void setFixedPosition(GtkWidget* widget, int x, int y);
    
    // 设置大小
    void setSize(GtkWidget* widget, int width, int height);
    
    // 设置位置和大小
    void setGeometry(GtkWidget* widget, int x, int y, int width, int height);
    
    // 设置组件扩展属性
    void setExpand(GtkWidget* widget, bool hexpand = true, bool vexpand = true);
    
    // 设置组件对齐方式
    void setAlignment(GtkWidget* widget, 
                     float xalign = 0.0, float yalign = 0.0);
    
    // === 属性和内容控制 ===
    
    // 获取/设置标签文本
    std::string getLabelText(GtkWidget* label) const;
    void setLabelText(GtkWidget* label, const std::string& text);
    
    // 获取/设置文本框内容
    std::string getEntryText(GtkWidget* entry) const;
    void setEntryText(GtkWidget* entry, const std::string& text);
    
    // 获取/设置复选框状态
    bool getCheckboxState(GtkWidget* checkbox) const;
    void setCheckboxState(GtkWidget* checkbox, bool state);
    
    // 获取/设置进度条进度
    double getProgressValue(GtkWidget* progressBar) const;
    void setProgressValue(GtkWidget* progressBar, double fraction);
    
    // 获取/设置滑块值
    double getScaleValue(GtkWidget* scale) const;
    void setScaleValue(GtkWidget* scale, double value);
    
    // === 信号绑定 ===
    
    // 绑定点击事件
    void bindClick(GtkWidget* button, std::function<void()> callback);
    
    // 绑定值改变事件
    void bindValueChanged(GtkWidget* widget, std::function<void()> callback);
    
    // 绑定文本改变事件
    void bindTextChanged(GtkWidget* entry, std::function<void()> callback);
    
    // === 组件管理 ===
    
    // 通过名称获取组件
    GtkWidget* getWidget(const std::string& name) const;
    
    // 检查组件是否存在
    bool hasWidget(const std::string& name) const;
    
    // 添加组件到管理器
    void addWidget(const std::string& name, GtkWidget* widget);
    
    // 从管理器移除组件
    void removeWidget(const std::string& name);
    
    // 清空所有组件
    void clearWidgets();
    
    // 获取所有组件名称
    std::vector<std::string> getWidgetNames() const;
};

#endif // GTK_WIDGET_HELPER_HPP