// GtkWidgetHelper.hpp
#ifndef GTK_WIDGET_HELPER_HPP
#define GTK_WIDGET_HELPER_HPP

#include <gtk/gtk.h>
#include <string>
#include <vector>
#include <functional>
#include <map>
#include <memory>
#include <algorithm>

// 回调函数包装器
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
    FIXED,      // 绝对定位
    BOX,        // 盒子布局
    GRID,       // 网格布局
    NONE        // 无布局
};

class GtkWidgetHelper {
private:
    GtkWidget* m_parent;
    std::map<std::string, std::shared_ptr<WidgetInfo>> m_widgets;
    std::map<std::string, std::shared_ptr<CallbackData>> m_callbacks;
    LayoutType m_layoutType;
    
    // 内部辅助方法
    void setupWidget(const std::string& name, GtkWidget* widget, 
                    const std::string& type, int x, int y, 
                    int width, int height);
    GtkWidget* createAndPlace(GtkWidget* widget, int x, int y, 
                             int width, int height);
    
public:
    // 构造函数，传入父窗口或容器
    explicit GtkWidgetHelper(GtkWidget* parent = nullptr);
    ~GtkWidgetHelper();
    
    // 设置父容器
    void setParent(GtkWidget* parent, LayoutType layoutType = LayoutType::FIXED);
    
    // === 创建基本组件 ===
    
    // 创建标签
    GtkWidget* createLabel(const std::string& text, 
                          const std::string& name = "",
                          int x = -1, int y = -1, 
                          int width = -1, int height = -1,
                          bool markup = false);
    
    // 创建按钮
    GtkWidget* createButton(const std::string& text, 
                           const std::string& name = "",
                           std::function<void()> onClick = nullptr,
                           int x = -1, int y = -1, 
                           int width = -1, int height = -1,
                           const std::string& icon = "");
    
    // 创建带图标的按钮
    GtkWidget* createButtonWithIcon(const std::string& iconName,
                                   const std::string& text,
                                   const std::string& name = "",
                                   std::function<void()> onClick = nullptr,
                                   int x = -1, int y = -1,
                                   int width = -1, int height = -1);
    
    // 创建文本框
    GtkWidget* createEntry(const std::string& name = "",
                          const std::string& defaultText = "",
                          bool password = false,
                          int x = -1, int y = -1, 
                          int width = -1, int height = -1,
                          int maxLength = 0);
    
    // 创建多行文本框
    GtkWidget* createTextArea(const std::string& name = "",
                             const std::string& defaultText = "",
                             int x = -1, int y = -1,
                             int width = -1, int height = -1,
                             bool editable = true,
                             bool wrap = true);
    
    // 创建复选框
    GtkWidget* createCheckbox(const std::string& text,
                             const std::string& name = "",
                             bool defaultState = false,
                             int x = -1, int y = -1,
                             int width = -1, int height = -1);
    
    // 创建单选框组
    GtkWidget* createRadioButtonGroup(const std::vector<std::string>& options,
                                     const std::string& groupName = "",
                                     int selectedIndex = 0,
                                     int x = -1, int y = -1,
                                     int spacing = 10);
    
    // 创建组合框
    GtkWidget* createComboBox(const std::vector<std::string>& items,
                             const std::string& name = "",
                             int selectedIndex = 0,
                             int x = -1, int y = -1,
                             int width = -1, int height = -1);
    
    // 创建列表视图
    GtkWidget* createListView(const std::vector<std::string>& columns,
                             const std::string& name = "",
                             int x = -1, int y = -1,
                             int width = -1, int height = -1);
    
    // 创建进度条
    GtkWidget* createProgressBar(const std::string& name = "",
                                double fraction = 0.0,
                                bool showText = false,
                                int x = -1, int y = -1,
                                int width = -1, int height = -1);
    
    // 创建水平滑块
    GtkWidget* createHScale(double min = 0.0, double max = 100.0,
                           double step = 1.0, double value = 0.0,
                           const std::string& name = "",
                           int x = -1, int y = -1,
                           int width = -1, int height = -1);
    
    // 创建垂直滑块
    GtkWidget* createVScale(double min = 0.0, double max = 100.0,
                           double step = 1.0, double value = 0.0,
                           const std::string& name = "",
                           int x = -1, int y = -1,
                           int width = -1, int height = -1);
    
    // 创建滚动窗口
    GtkWidget* createScrolledWindow(GtkWidget* child,
                                   const std::string& name = "",
                                   int x = -1, int y = -1,
                                   int width = -1, int height = -1);
    
    // 创建框架
    GtkWidget* createFrame(const std::string& label = "",
                          GtkWidget* child = nullptr,
                          const std::string& name = "",
                          int x = -1, int y = -1,
                          int width = -1, int height = -1);
    
    // 创建分割面板
    GtkWidget* createPaned(GtkOrientation orientation = GTK_ORIENTATION_HORIZONTAL,
                          GtkWidget* child1 = nullptr,
                          GtkWidget* child2 = nullptr,
                          const std::string& name = "",
                          int x = -1, int y = -1,
                          int width = -1, int height = -1);
    
    // 创建笔记本（标签页）
    GtkWidget* createNotebook(const std::string& name = "",
                             int x = -1, int y = -1,
                             int width = -1, int height = -1);
    
    // 在笔记本中添加页面
    void addNotebookPage(GtkWidget* notebook, GtkWidget* child,
                        const std::string& label,
                        bool closeable = false);
    
    // 创建工具条
    GtkWidget* createToolbar(const std::string& name = "",
                            int x = -1, int y = -1,
                            int width = -1, int height = -1);
    
    // 在工具条中添加按钮
    void addToolbarButton(GtkWidget* toolbar, const std::string& iconName,
                         const std::string& tooltip,
                         std::function<void()> onClick = nullptr);
    
    // 创建菜单栏
    GtkWidget* createMenuBar(const std::string& name = "",
                            int x = -1, int y = -1,
                            int width = -1, int height = -1);
    
    // 创建状态栏
    GtkWidget* createStatusBar(const std::string& name = "",
                              int x = -1, int y = -1,
                              int width = -1, int height = -1);
    
    // 在状态栏显示消息
    void showStatusMessage(GtkWidget* statusbar, 
                          const std::string& message,
                          int contextId = 0);
    
    // 创建盒子容器
    GtkWidget* createBox(GtkOrientation orientation = GTK_ORIENTATION_VERTICAL,
                        int spacing = 10,
                        bool homogeneous = false,
                        const std::string& name = "",
                        int x = -1, int y = -1,
                        int width = -1, int height = -1);
    
    // 在盒子中添加组件
    void addToBox(GtkWidget* box, GtkWidget* child,
                 bool expand = false, bool fill = false,
                 int padding = 0);
    
    // 创建网格容器
    GtkWidget* createGrid(const std::string& name = "",
                         int rowSpacing = 5,
                         int columnSpacing = 5,
                         int x = -1, int y = -1,
                         int width = -1, int height = -1);
    
    // 在网格中添加组件
    void addToGrid(GtkWidget* grid, GtkWidget* child,
                  int left, int top,
                  int width = 1, int height = 1);
    
    // 创建日历
    GtkWidget* createCalendar(const std::string& name = "",
                             int x = -1, int y = -1,
                             int width = -1, int height = -1);
    
    // 创建颜色选择按钮
    GtkWidget* createColorButton(const std::string& name = "",
                                const GdkRGBA* initialColor = nullptr,
                                int x = -1, int y = -1,
                                int width = -1, int height = -1);
    
    // 创建文件选择按钮
    GtkWidget* createFileChooserButton(const std::string& title,
                                      GtkFileChooserAction action,
                                      const std::string& name = "",
                                      int x = -1, int y = -1,
                                      int width = -1, int height = -1);
    
    // 创建数字输入框
    GtkWidget* createSpinButton(double min, double max, double step,
                               const std::string& name = "",
                               double value = 0,
                               int x = -1, int y = -1,
                               int width = -1, int height = -1);
    
    // 创建开关按钮
    GtkWidget* createSwitch(const std::string& name = "",
                           bool active = false,
                           int x = -1, int y = -1,
                           int width = -1, int height = -1);
    
    // 创建链接按钮
    GtkWidget* createLinkButton(const std::string& url,
                               const std::string& label,
                               const std::string& name = "",
                               int x = -1, int y = -1,
                               int width = -1, int height = -1);
    
    // 创建搜索框
    GtkWidget* createSearchEntry(const std::string& name = "",
                                const std::string& placeholder = "",
                                int x = -1, int y = -1,
                                int width = -1, int height = -1);
    
    // 创建标签页栏
    GtkWidget* createTabBar(const std::vector<std::string>& tabs,
                           const std::string& name = "",
                           int x = -1, int y = -1,
                           int width = -1, int height = -1);
    
    // === 布局控制 ===
    
    // 设置组件位置（仅对FIXED布局有效）
    void setWidgetPosition(GtkWidget* widget, int x, int y);
    
    // 设置组件大小
    void setWidgetSize(GtkWidget* widget, int width, int height);
    
    // 设置组件位置和大小
    void setWidgetGeometry(GtkWidget* widget, int x, int y, 
                          int width, int height);
    
    // 设置组件扩展属性
    void setWidgetExpand(GtkWidget* widget, 
                        bool hexpand = true, bool vexpand = true);
    
    // 设置组件对齐方式
    void setWidgetAlignment(GtkWidget* widget, 
                           float xalign = 0.0, float yalign = 0.0);
    
    // 设置组件边距
    void setWidgetMargin(GtkWidget* widget, 
                        int top = 0, int bottom = 0,
                        int left = 0, int right = 0);
    
    // 设置组件填充
    void setWidgetPadding(GtkWidget* widget, int padding = 0);
    
    
    // === 属性和内容控制 ===
    
    // 标签操作
    std::string getLabelText(GtkWidget* label) const;
    void setLabelText(GtkWidget* label, const std::string& text);
    void setLabelMarkup(GtkWidget* label, const std::string& markup);
    
    // 文本框操作
    const char* getEntryText(GtkWidget* entry) const;
    void setEntryText(GtkWidget* entry, const std::string& text);
    void clearEntry(GtkWidget* entry);
    
    // 多行文本框操作
    const char* getTextAreaText(GtkWidget* textview) const;
    void setTextAreaText(GtkWidget* textview, const std::string& text);
    void appendTextAreaText(GtkWidget* textview, const std::string& text);
    
    // 复选框操作
    bool getCheckboxState(GtkWidget* checkbox) const;
    void setCheckboxState(GtkWidget* checkbox, bool state);
    
    // 单选框组操作
    int getSelectedRadioIndex(const std::string& groupName) const;
    void setSelectedRadioIndex(const std::string& groupName, int index);
    
    // 组合框操作
    int getComboSelectedIndex(GtkWidget* combo) const;
    void setComboSelectedIndex(GtkWidget* combo, int index);
    std::string getComboSelectedText(GtkWidget* combo) const;
    void addComboItem(GtkWidget* combo, const std::string& item);
    void removeComboItem(GtkWidget* combo, int index);
    
    // 进度条操作
    double getProgressValue(GtkWidget* progressBar) const;
    void setProgressValue(GtkWidget* progressBar, double fraction);
    void pulseProgressBar(GtkWidget* progressBar);
    
    // 滑块操作
    double getScaleValue(GtkWidget* scale) const;
    void setScaleValue(GtkWidget* scale, double value);
    
    // 开关操作
    bool getSwitchState(GtkWidget* switchBtn) const;
    void setSwitchState(GtkWidget* switchBtn, bool state);
    
    // 数字输入框操作
    double getSpinValue(GtkWidget* spin) const;
    void setSpinValue(GtkWidget* spin, double value);
    
    // 日历操作
    void getCalendarDate(GtkWidget* calendar, 
                        guint& year, guint& month, guint& day) const;
    void setCalendarDate(GtkWidget* calendar, 
                        guint year, guint month, guint day);
    
    // === 信号绑定 ===
    
    // 绑定点击事件
    void bindClick(GtkWidget* button, std::function<void()> callback);
    
    // 绑定切换事件
    void bindToggled(GtkWidget* toggleButton, std::function<void()> callback);
    
    // 绑定值改变事件
    void bindValueChanged(GtkWidget* widget, std::function<void()> callback);
    
    // 绑定文本改变事件
    void bindTextChanged(GtkWidget* entry, std::function<void()> callback);
    
    // 绑定选择改变事件
    void bindSelectionChanged(GtkWidget* widget, std::function<void()> callback);
    
    // 绑定行激活事件（用于列表视图）
    void bindRowActivated(GtkWidget* treeview, 
                         std::function<void(int)> callback);
    
    // === 组件管理 ===
    
    // 通过名称获取组件
    GtkWidget* getWidget(const std::string& name) const;
    
    // 获取组件信息
    std::shared_ptr<WidgetInfo> getWidgetInfo(const std::string& name) const;
    
    // 检查组件是否存在
    bool hasWidget(const std::string& name) const;
    
    // 添加组件到管理器
    void addWidget(const std::string& name, GtkWidget* widget,
                  const std::string& type = "unknown");
    
    // 移除组件
    void removeWidget(const std::string& name, bool destroy = true);
    
    // 显示/隐藏组件
    void showWidget(const std::string& name);
    void hideWidget(const std::string& name);
    void setWidgetVisible(const std::string& name, bool visible);
    
    // 启用/禁用组件
    void enableWidget(const std::string& name);
    void disableWidget(const std::string& name);
    void setWidgetSensitive(const std::string& name, bool sensitive);
    
    // 获取所有组件名称
    std::vector<std::string> getWidgetNames() const;
    
    // 根据类型获取组件名称
    std::vector<std::string> getWidgetNamesByType(const std::string& type) const;
    
    // 清空所有组件
    void clearWidgets(bool destroy = true);
    
    // 获取父容器
    GtkWidget* getParent() const { return m_parent; }
    
    // 获取布局类型
    LayoutType getLayoutType() const { return m_layoutType; }
};

#endif // GTK_WIDGET_HELPER_HPP