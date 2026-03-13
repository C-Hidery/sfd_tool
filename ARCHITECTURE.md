# SFD Tool — 架构文档

## 项目简介

SFD Tool 是一个用于操作展讯（Spreadtrum/UNISOC）芯片的 GUI 工具，支持分区读写、分区表修改、AVB 操作等。底层通过 USB（libusb）与设备的 BROM/FDL 引导程序通信。

## 技术栈

| 层次 | 技术 |
|------|------|
| UI 框架 | GTK+ 3.0 |
| 语言 | C++17 |
| USB 通信 | libusb-1.0 |
| 构建系统 | CMake（主） + Makefile（兼容） |
| 国际化 | gettext（libintl） |

---

## 目录结构

```
sfd_tool/
├── main.cpp                    # 入口 + 连接逻辑（on_button_clicked_connect/fdl_exec）
├── main.h                      # 全局变量声明
├── main_console.cpp            # 命令行模式入口
├── common.cpp / common.h       # 上层逻辑、结构体定义、跨模块公共接口
├── core/                       # 拆分后的底层核心模块
│   ├── logging.h/.cpp          # 日志与错误处理（DEG_LOG/ERR_EXIT、打印内存等）
│   ├── file_io.h/.cpp          # 文件读写封装（xfopen/my_fopen，Windows UTF-8 路径处理）
│   ├── pac_extract.h/.cpp      # PAC 固件解析与解包（sprd_head_t/sprd_file_t、Unpac 类）
│   ├── usb_transport.h/.cpp    # USB 通信与端点发现（libusb / Windows Wrapper、spdio_t 缓冲区管理）
│   └── spd_protocol.h/.cpp     # SPD/BSL 协议封装（HDLC 转码、CRC/Checksum、encode_msg/send_msg/recv_msg）
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
├── assets/                     # 图标、rc 资源等
├── packaging/                  # 打包脚本（deb/rpm、desktop 文件、man 手册）
├── locale/                     # 国际化翻译文件（.po / .mo）
├── third_party/                # 第三方依赖
│   ├── Lib/                    # 预编译库（libusb 等，供 Windows 使用）
│   └── nlohmann/               # nlohmann/json 单头文件库
└── scripts/                    # 其他辅助脚本
```

---

## 当前架构问题概览

> 本小节从高层结构视角概括当前架构在“模块边界、职责分配、可测试性、可维护性”等方面存在的主要问题。更细致的函数级分析见 [`task/工程结构分析与整理计划.md`](task/工程结构分析与整理计划.md)。

1. **核心逻辑与 UI 层耦合度高**
   - `main.cpp` 中包含大量业务流程（设备连接、FDL 执行、分区读写等），这些流程直接操作 GTK 控件或依赖 UI 细节。
   - 部分页面（如 `page_connect`, `page_partition`）的回调函数仍然实现在 `main.cpp`，导致页面模块与主入口强耦合。

2. **协议/传输与平台细节混杂**
   - 历史上大量逻辑集中在 `common.cpp` 中，包括：SPD 协议编解码、USB 传输、文件 I/O、PAC 解包、日志输出等。
   - 协议层直接依赖 libusb / Windows Channel9 等平台细节，缺乏清晰的抽象层，使得后续想引入测试或替换底层实现的成本很高。

3. **全局状态分散且以 `extern` 形式暴露**
   - 设备连接状态、块大小、A/B 分区选择等关键状态通过多个 `extern` 变量散落在不同源文件中。
   - 这增加了理解成本，也使得重构过程中很难保证所有状态在一个一致的生命周期内被正确管理。

4. **配置与 JSON 操作直接暴露在 UI 中**
   - 当前 UI 代码直接操作 `nlohmann::json`，不同页面自行解析/拼装配置结构，缺乏统一的配置域模型。
   - 随着功能增加，配置项的演化难以集中治理，容易产生隐性依赖和行为差异。

5. **日志与长任务处理模式不统一**
   - 各处自行使用日志输出宏/函数，缺乏统一接口和等级管理；
   - 刷机、备份等长时间运行任务在不同页面中以不同方式禁用按钮、更新进度条、处理取消操作，不利于用户体验一致性与后续维护。

6. **测试与构建体系对重构不友好**
   - 协议/解析等核心逻辑缺少单元测试，任何非局部性改动都需要依赖人工回归；
   - 构建系统以 Makefile 为主，VS 工程与 CMake 的角色界限尚不清晰，不利于在多平台、多 IDE 环境下协同开发。

上述问题在《工程重构执行计划》中被进一步拆解为可操作的任务（例如 T1-01～T1-05、T2-01～T2-03 等），这里以“问题清单”的方式帮助读者从整体上把握架构痛点。

---

## 目标架构轮廓

> 本小节描述期望达到的目标架构形态，用于指导具体重构任务的取舍与优先级。对应的实施步骤与里程碑请参考 [`task/工程重构执行计划.md`](task/工程重构执行计划.md)。

### 1. 分层结构与边界

目标是形成一个**UI ← Service ← Core ← 平台** 的分层结构，每一层的职责大致如下：

- **UI 层（pages/*, GtkWidgetHelper, ui_common）**
  - 负责界面展示、用户交互、输入校验和简单的状态管理；
  - 通过 Service 接口调用业务能力，不直接操作 USB、协议、文件或 JSON；
  - 页面之间通过清晰的接口（例如初始化、应用配置、执行操作）进行协作。

- **Service 层（core/flash_service, device_service, config_service 等）**
  - 对上提供面向用例的高层接口（例如“刷一个 PAC 包”、“探测设备信息”、“加载/保存配置”）；
  - 对下组合使用 core 模块（协议/传输、PAC 解析、文件 I/O 等），并负责将底层错误转换为统一的 Result/ErrorCode；
  - 是未来测试与监控的重点入口层。

- **Core 层（core/spd_protocol, usb_transport, pac_extract, file_io, logging 等）**
  - 提供与业务场景无关的“通用能力”：协议编解码、字节流读写、文件访问、日志输出等；
  - 不依赖 UI，不直接持有 GtkWidget 等 UI 对象；
  - 尽量通过抽象接口（如 IUsbTransport）隔离具体平台实现，方便替换与单元测试。

- **平台层（BMPlatform, Wrapper, 平台相关宏/适配）**
  - 封装与操作系统或特定驱动相关的细节（如 Windows Channel9、libusb 设备枚举方式差异等）；
  - 提供统一的接口给 core/usb_transport 等模块使用。

### 2. 状态与配置管理

- 引入集中管理的应用状态（AppState 或等价机制），替代当前散落的 `extern` 全局变量；
- app_state 仅承载“业务状态”（设备信息、当前 PAC 信息、分区表等），不再持有 UI 控件指针；
- 配置相关逻辑通过 ConfigService 和明确定义的配置结构体（AppConfig、ConnectionConfig、FlashConfig 等）统一管理，JSON 仅作为持久化格式存在。

### 3. 错误处理与日志

- 在 core 层引入统一的 Result<T> + ErrorCode 体系，避免“0/非 0”“true/false”等隐式约定；
- 在全局统一日志接口与等级划分（LOG_DEBUG/INFO/WARN/ERROR），并记录模块名，方便定位问题；
- UI 层通过统一的日志/提示接口与 core 层交互，不直接依赖底层日志实现细节。

### 4. 可测试性与可维护性

- 协议编解码、PAC 解析、关键 Service 接口具备基础单元测试，重构后可通过 CI 快速验证回归；
- 文件和模块边界清晰，一个模块的改动尽量局限在有限的几个文件范围内；
- 对外暴露的接口（尤其是 Service 层）保持简洁稳定，为后续功能扩展留有余地。

### 5. 构建与发布流程

- 明确 CMake、VS 工程与 Makefile 的角色：
  - CMake 作为跨平台构建配置的“单一事实来源”；
  - VS 工程可以由 CMake 生成或标记为 legacy；
  - Makefile 可在过渡期保留或简化，最终以 CMake 为主。
- CI 能够覆盖核心平台（至少 Linux + Windows）的基础构建与测试，打包脚本（Debian、可能的 RPM 等）与当前目录结构保持同步。
- GitHub Actions 工作流（见 `.github/workflows/build.yml`）：
  - Windows job 使用 CMake + Visual Studio 构建 x64 变体并运行 `ctest`；
  - macOS job 使用 CMake + Ninja 构建可执行文件并打包为 DMG；
  - Linux job 使用 `packaging/build-deb.sh` 构建 `.deb` 包；
  - 另有基于 Fedora 容器的 RPM 打包 job；
  - 在 `master` 分支上，CI 会聚合上述产物并创建 GitHub Release。

通过上述目标架构的约束，可以将《工程重构执行计划》中每一条任务（T1-01, T2-01, T3-01 等）映射到具体的“架构收益”，从而在实施过程中持续校验：

- 这次改动是否让某个边界更清晰了？
- 是否降低了 UI 与 core、协议与平台之间的耦合？
- 是否提升了未来新增功能或排查问题的可行性？

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

---

## 运行

### 国际化说明

程序已集成 gettext 国际化。直接运行（或 `sudo ./sfd_tool`）时，语言跟随系统默认 locale，部分环境下 `sudo` 会重置环境变量导致显示英文界面。

**推荐运行方式：**

```bash
# 不需要 root 权限的情况（macOS 或已配置 udev 规则的 Linux）
./sfd_tool

# Linux — 需要 USB 访问权限时（推荐）
sudo -E ./sfd_tool
```

> 首次运行会在当前目录生成 `sfd_tool_config.json`，默认 `ui_language` 为 `zh_CN`，因此界面为中文。之后可以通过修改该文件或在 GUI 高级设置中切换界面语言。

### Linux USB 权限

Linux 默认对 USB 设备的访问需要 root 权限，因此推荐使用 `sudo -E`。
或者，可以配置 udev 规则避免每次都需要 sudo：

```bash
# 示例：允许所有用户访问展讯 USB 设备
echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="1782", MODE="0666"' \
  | sudo tee /etc/udev/rules.d/99-sprd.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

你也可以通过修改 `sfd_tool_config.json` 中的 `ui_language` 字段切换界面语言，例如：

- `"ui_language": "zh_CN"`：中文界面；
- `"ui_language": "en_US"`：英文界面；
- `"ui_language": "auto"` 或空字符串：跟随系统/终端环境。

配置保存后，下次启动即按照该配置加载 i18n。
