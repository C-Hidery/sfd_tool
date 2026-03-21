# SFD Tool — 架构文档

> 相关文档： [README_ZH](README_ZH.md) · [操作手册](docs/USER_GUIDE_ZH.md) · [CMake 使用指南](docs/cmake.md) · [版本发布流程](docs/RELEASE_GUIDE_ZH.md) · [版本记录](docs/VERSION_LOG.md)

## 项目简介

SFD Tool 是一个用于操作展讯（Spreadtrum/UNISOC）芯片的 GUI/CLI 工具，
支持分区读写、分区表导出、PAC 刷写以及 AVB 相关操作。底层通过 USB
（libusb / Windows Channel9）与设备的 BROM/FDL 引导程序通信。

本文从架构角度描述主要模块、分层设计和重构方向，帮助开发者在
阅读代码前快速建立整体认知。

---

## 目录结构（按职责分组）

```text
sfd_tool/
├── main.cpp                  # GUI 入口，应用初始化与页面注册
├── main.h                    # 与入口相关的少量全局声明
├── main_console.cpp          # 命令行模式入口（--no-gui），实现 CLI 命令解析与帮助文本
├── common.cpp / common.h     # 历史上集中逻辑的“公共模块”，部分职责已向 core/ 迁移
├── ui_common.cpp / .h        # 与 UI 相关的通用逻辑（启用/禁用控件、状态栏等）
├── GtkWidgetHelper.cpp/.hpp  # GTK 控件封装与辅助函数
├── ui_page.h                 # 页面初始化/信号绑定接口约定
├── i18n.h                    # gettext 国际化宏定义
├── GenTosNoAvb.h             # 与 AVB/TrustOS 相关的高级操作辅助
│
├── core/                     # 核心业务与底层能力模块（与 UI 解耦）
│   ├── app_state.h           # 应用状态（AppState）定义
│   ├── config_service.h/.cpp # 配置加载/保存、per-user 配置路径、ui_language 等
│   ├── device_service.h/.cpp # 设备连接、FDL 下载、基础设备操作
│   ├── flash_service.h/.cpp  # 统一的刷机与分区读写服务（含 PartitionReadService）
│   ├── file_io.h/.cpp        # 文件读写封装（跨平台路径处理等）
│   ├── logging.h/.cpp        # 日志接口与实现（DEG_LOG 等）
│   ├── pac_extract.h/.cpp    # PAC 固件解析与解包
│   ├── result.h              # Result<T> / ErrorCode 等错误返回模型
│   ├── spd_protocol.h/.cpp   # SPD/BSL 协议封装（编解码、消息收发）
│   └── usb_transport.h/.cpp  # USB 传输抽象（libusb / Windows Wrapper）
│
├── pages/                    # 各标签页（UI 层）的实现
│   ├── page_connect.cpp/.h       # 连接页（Connect）
│   ├── page_partition.cpp/.h     # 分区操作页（Partition Operation）
│   ├── page_pac_flash.cpp/.h     # PAC 刷写页（PAC Flash）
│   ├── page_manual.cpp/.h        # 手动操作页（Manual）
│   ├── page_advanced_op.cpp/.h   # 高级操作页（Advanced Operation）
│   ├── page_advanced_set.cpp/.h  # 高级设置页（Advanced Settings）
│   ├── page_debug.cpp/.h         # 调试设置页（Debug Options）
│   ├── page_log.cpp/.h           # 日志页（Log）
│   └── page_about.cpp/.h         # 关于页（About）
│
├── assets/                   # 图标、rc 资源等
├── packaging/                # 打包脚本与元数据（deb/rpm/spec/desktop/man 等）
├── locale/                   # 国际化翻译文件（.po / .mo）
├── third_party/              # 第三方依赖
│   ├── Lib/                  # 预编译库（libusb、Channel9 等，供 Windows 使用）
│   └── nlohmann/             # nlohmann/json 单头文件库
├── tests/                    # 单元测试与集成测试（通过 CTest 运行）
├── scripts/                  # 构建/开发/发布辅助脚本（dev.sh、release.sh 等）
├── docs/                     # 用户/开发/发布文档
└── .github/workflows/        # GitHub Actions CI 配置（build.yml）
```

---

## 当前架构问题概览

> 本小节从高层结构视角概括当前架构在“模块边界、职责分配、可测试性、
> 可维护性”等方面存在的主要问题，方便在阅读代码或规划重构时有一个
> 问题清单。更细致的函数级分析建议结合仓库中的 `task/` 目录下计划类
> 文档（若存在）。

1. **核心逻辑与 UI 层耦合度仍然偏高**
   - `main.cpp` 和部分页面代码中仍存在较多业务流程逻辑（设备连接、
     分区操作入口等），直接操作 GTK 控件或依赖 UI 细节；
   - 部分历史回调实现仍集中在 `main.cpp`，需要逐步挪到对应 page 模块。

2. **协议/传输与平台细节混杂**
   - 虽然已经引入 `usb_transport` 和 `spd_protocol` 等模块，但部分逻辑
     仍然耦合具体平台行为（例如 Windows Wrapper / Channel9 与 Linux
     libusb 的差异）；
   - 对未来新增平台或替换底层实现的灵活性仍有限。

3. **状态管理集中化进展中**
   - 已有 `AppState` 用于集中存放设备信息、分区列表、配置等共享状态；
   - 一些遗留 `extern` 全局变量仍然存在，需要逐步收敛到 AppState 或更
     合理的所有者中，以降低全局可变状态带来的复杂度。

4. **配置访问逐步统一，但历史代码仍存在**
   - 当前推荐通过 `ConfigService` 以及明确的配置结构访问 per-user 配置，
     JSON 仅作为持久化格式；
   - 历史上直接操作 JSON 的 UI 代码尚未完全收敛，需要在后续重构中统一
     到服务层接口之上。

5. **日志与长任务处理模式正在统一**
   - `core/logging` 提供统一日志接口，但部分旧代码仍使用自定义输出；
   - 长时间任务（PAC 刷写、分区备份/还原）在不同页面的按钮禁用、取消
     流程、进度更新方式仍不完全一致，需要在 UI 层进一步抽象统一模式。

6. **测试覆盖仍有提升空间**
   - `tests/` 中已经加入了 `spd_protocol`、`pac_extract`、块大小模式与
     备份路径等核心逻辑的测试用例；
   - 但部分业务流程仍依赖人工回归测试，未来可在 Service 层增加更多测试
     针对典型用例链路（例如“PAC 刷机”与“分区批量还原”）。

---

## 目标分层架构

目标是形成一个清晰的 **UI ← Service ← Core ← 平台** 分层结构：

### 1. UI 层（pages/*, GtkWidgetHelper, ui_common）

- 负责界面展示、用户交互、基本输入校验和简单状态管理；
- 通过 Service 接口调用业务能力，不直接操作 USB、协议、文件或 JSON；
- 每个标签页（Connect / Partition / PAC Flash / Manual / Advanced / Settings /
  Debug / Log / About）与用户视角的功能一一对应，其说明可参见
  [docs/USER_GUIDE_ZH.md](docs/USER_GUIDE_ZH.md)；
- 公共 UI 行为（如长任务期间统一禁用按钮、刷新状态栏）通过
  `GtkWidgetHelper` 与 `ui_common` 中的辅助函数实现。

### 2. Service 层（core/flash_service, device_service, config_service 等）

- 向上暴露 **用例级接口**，例如：
  - `FlashService::flashPac(...)`：刷写一个 PAC 包；
  - `FlashService::backupPartitions(...)`：备份若干分区；
  - `FlashService::partitionReader()`：获取统一的分区读取服务
    `PartitionReadService`；
  - `DeviceService`：处理设备探测、握手与 FDL 下载；
  - `ConfigService`：加载/保存 per-user 配置、迁移旧配置等；
- 向下组合使用 Core 层模块（协议、传输、PAC 解析、文件 IO 等），并统一
  把错误包装成 `Result<T>` / `FlashStatus` 等形式；
- 是未来单元测试与集成测试的主要入口层。

### 3. Core 层（协议/IO/日志等通用能力）

- 提供与业务场景无关的基础能力：
  - `spd_protocol`：SPD/BSL 协议编解码与消息收发；
  - `usb_transport`：USB 端点发现与收发；
  - `pac_extract`：PAC 文件解析与解包；
  - `file_io`：文件读写抽象；
  - `logging`：统一日志接口；
  - `result`：错误返回模型；
- 不依赖 UI，不直接持有 GTK 控件；
- 通过接口或轻量适配层与平台层解耦，便于在测试中替换底层实现。

### 4. 平台层（BMPlatform, Wrapper, 平台适配）

- 抽象与操作系统/驱动相关的行为：
  - Windows: Channel9 DLL、句柄管理等；
  - Linux/macOS: libusb 设备枚举与权限处理；
- 对上提供统一接口，让 `usb_transport` 等模块在不关心平台细节的前提下
  完成收发操作。

---

## 分区读取与备份新架构（PartitionReadService）

> 该部分对应在 [docs/VERSION_LOG.md](docs/VERSION_LOG.md) 中提到的
> “统一分区读取服务与块大小模式/模型，引入 PartitionReadService，将
> 分区读取与备份路径整合到统一链路中”。

相关代码主要位于 [core/flash_service.h](core/flash_service.h) / `.cpp`：

- `struct PartitionReadInfo`：描述要读取的分区（名称、起始位置、大小等）；
- `struct PartitionReadOptions`：输出路径、块大小配置 `BlockSizeConfig`；
- `struct PartitionReadCallbacks`：开始、进度、完成三个回调；
- `class PartitionReadService`：
  - 核心方法：
    - `FlashStatus readOne(const PartitionReadInfo&, const PartitionReadOptions&, ...)`；
  - 统一处理：
    - 根据分区表查找分区；
    - 根据块大小模式选择实际步长；
    - 负责在读取消除旧链路中的分支和重复代码。
- `class FlashService`：对外暴露：
  - `partitionReader()`：返回 `PartitionReadService&`；
  - `backupPartitions(...)`：按名称列表/全部备份到目录；

UI 层（如 `page_partition`）在执行备份/还原操作时，统一走这条服务链路，
并在“从文件夹批量还原”等新功能中利用同一套块大小配置和备份路径策略，
避免历史逻辑中的分叉与不一致行为。

更具体的行为可参考测试用例：

- `tests/test_blocksize_and_backup_service.cpp`
- `tests/test_partition_backup_paths.cpp`

---

## 新增标签页指南（基于 CMake）

新增标签页的推荐步骤如下：

1. 在 `pages/` 新建文件：
   - `page_myfeat.cpp`
   - `page_myfeat.h`
2. 在 `page_myfeat.h` 中声明：
   - `GtkWidget* create_myfeat_page(GtkWidgetHelper& helper);`
   - `void bind_myfeat_signals(GtkWidgetHelper& helper);`
3. 在 `page_myfeat.cpp` 中：
   - 在 `create_myfeat_page` 中构建本页 UI 布局；
   - 在 `bind_myfeat_signals` 中连接按钮/控件与对应回调（回调内部调用
     Service 层接口，而非直接访问底层协议）。
4. 在 `main.cpp` 中：
   - 在初始化阶段调用新页面的 `create_myfeat_page` 与
     `bind_myfeat_signals`，将其插入主窗口的 Notebook。
5. 在 CMake 中注册新源文件：
   - 打开 `pages/CMakeLists.txt`（或顶层 `CMakeLists.txt` 中包含 pages 段落）；
   - 在对应的 `target_sources` / `add_library` / `add_executable` 段中
     增加 `pages/page_myfeat.cpp`；
   - 重新运行 CMake 配置并构建。

> 历史上 `Makefile` 有一个 `SOURCES` 列表需要手动维护，但在 CMake
> 成为主构建系统后，该列表仅保留作兼容参考。**新增标签页时请以 CMake
> 为准，若确实需要 Makefile 支持，可在完成 CMake 配置后再按需补充。**

---

## 构建与运行（简要总结）

构建与运行的详细说明请参考：

- [docs/cmake.md](docs/cmake.md)
- [docs/cmake_EN.md](docs/cmake_EN.md)

这里仅给出简要示例：

```bash
# Debug 构建（Ninja）
cmake -S . -B build_cmake_debug -G "Ninja" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build_cmake_debug -j

# 运行 Debug GUI（界面语言由 per-user 配置文件中的 ui_language 决定）
./build_cmake_debug/sfd_tool
```

关于 per-user 配置和 `ui_language` 字段，详见：

- [docs/cmake.md](docs/cmake.md) 中相关章节；
- `core/config_service` 的实现。

---

## 与 CI / 发布流程的关系

GitHub Actions 的主要工作流定义在
[.github/workflows/build.yml](.github/workflows/build.yml) 中，当前大致包含：

- Windows x64 构建与测试 Job；
- macOS 构建与打包（DMG） Job；
- Linux DEB/RPM 打包 Job；
- 基于 tag 的 Release Job（聚合各平台产物并上传到 Releases）。

发布流程、版本号更新、版本记录维护的详情，请参考：

- [docs/RELEASE_GUIDE_ZH.md](docs/RELEASE_GUIDE_ZH.md)
- [docs/VERSION_LOG.md](docs/VERSION_LOG.md)

架构文档的目标是与上述流程保持同步，使得：

- 任何一个模块（core/page/service）发生结构性调整时，都能在此处找到
  对应位置更新说明；
- 新贡献者可以通过本文件快速找到自己关心的层次和文件入口；
- 与 CI/发布有关的目录结构（packaging/、tests/ 等）在此有清晰标注。

---

## 总结

- **UI 层** 面向用户视角，负责交互与展示，避免直接操作底层协议；
- **Service 层** 以用例为中心，统一对接 UI 与 Core；
- **Core 层** 提供稳定可靠的底层能力模块；
- **平台层** 隔离操作系统和驱动细节。

随着 `PartitionReadService`、统一块大小模式、per-user 配置等新结构的
引入，项目正在逐步向“高内聚、低耦合、易测试”的方向演进。未来在引入
新特性或进行大型重构时，建议始终参考本文件所描述的分层思路，确保新
代码落在合适的层次上，并尽量复用现有的服务与基础设施。