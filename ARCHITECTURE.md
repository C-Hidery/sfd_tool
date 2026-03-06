# SFD Tool — 架构文档

## 项目简介

SFD Tool 是一个用于操作展讯（Spreadtrum/UNISOC）芯片的 GUI 工具，支持分区读写、分区表修改、AVB 操作等。底层通过 USB（libusb）与设备的 BROM/FDL 引导程序通信。

## 技术栈

| 层次 | 技术 |
|------|------|
| UI 框架 | GTK+ 3.0 |
| 语言 | C++17 |
| USB 通信 | libusb-1.0 |
| 构建系统 | Makefile |
| 国际化 | gettext（libintl） |

---

## 目录结构

```
sfd_tool/
├── main.cpp                    # 入口 + 连接逻辑（on_button_clicked_connect/fdl_exec）
├── main.h                      # 全局变量声明
├── main_console.cpp            # 命令行模式入口
├── common.cpp / common.h       # 设备通信协议、数据结构、底层函数
├── GtkWidgetHelper.cpp/.hpp    # GTK Widget 抽象封装层
├── ui_common.cpp / ui_common.h # 公共 UI 函数（EnableWidgets、底部控制栏）
├── i18n.h                      # 国际化宏定义（gettext）
├── GenTosNoAvb.h               # TrustOS AVB 补丁工具
│
├── pages/                      # 各标签页独立模块
│   ├── page_connect.cpp/h          # Connect 标签页
│   ├── page_partition.cpp/h        # Partition Operation 标签页
│   ├── page_manual.cpp/h           # Manually Operate 标签页
│   ├── page_advanced_op.cpp/h      # Advanced Operation 标签页
│   ├── page_advanced_set.cpp/h     # Advanced Settings 标签页
│   ├── page_debug.cpp/h            # Debug Options 标签页
│   ├── page_about.cpp/h            # About 标签页
│   └── page_log.cpp/h              # Log 标签页
│
├── locale/                     # 国际化翻译文件（.po / .mo）
└── Lib/                        # 第三方库（libusb 等）
```

---

## 模块化设计

### 页面模块接口规范

每个 `pages/page_xxx.cpp` 遵循统一接口：

```cpp
// 创建 UI，将标签页添加到 notebook
GtkWidget* create_xxx_page(GtkWidgetHelper& helper, GtkWidget* notebook);

// 绑定该页面的所有按钮信号
void bind_xxx_signals(GtkWidgetHelper& helper);
```

`gtk_kmain`（在 `main.cpp`）只负责依次调用这些函数：

```cpp
create_connect_page(helper, notebook);
create_partition_page(helper, notebook);
// ...
bind_connect_signals(helper, argc, argv);
bind_partition_signals(helper);
// ...
```

### GtkWidgetHelper

`GtkWidgetHelper` 是对 GTK 的统一封装，核心能力：

- `addWidget(name, widget)` / `getWidget(name)` — 用字符串 name 管理所有 widget（避免散乱的全局指针）
- `createButton / createEntry / createLabel / createGrid` — 快捷创建 widget
- `bindClick(widget, lambda)` — 绑定点击回调
- `setLabelText / setEntryText / getEntryText` — 读写控件值

### 全局状态

以下全局变量在 `main.h` 中声明，各模块通过 `extern` 引用：

| 变量 | 说明 |
|------|------|
| `io` | spdio_t*，设备通信上下文 |
| `helper` | GtkWidgetHelper，全局 widget 管理器 |
| `m_bOpened` | 设备连接状态（-1 = 未连接） |
| `isCMethod` | 是否使用兼容模式读取分区表 |
| `blk_size` | 数据块大小 |
| `selected_ab` | A/B 分区选择（0=auto, 1=A, 2=B） |

---

## 新增标签页指南

1. 在 `pages/` 新建 `page_myfeat.cpp` 和 `page_myfeat.h`
2. 在 `.h` 中声明 `create_myfeat_page` 和 `bind_myfeat_signals`
3. 在 `.cpp` 中实现 UI 构建和回调函数
4. 在 `main.cpp` 的 `gtk_kmain` 中添加两行调用
5. 在 `Makefile` 的 `SOURCES` 中添加 `pages/page_myfeat.cpp`

---

## 编译

```bash
# 完整编译
make

# Debug 版本
make debug

# 清理
make clean
```

**依赖：** GTK+ 3.0、libusb-1.0、gettext（libintl）
