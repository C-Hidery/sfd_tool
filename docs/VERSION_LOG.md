# sfd_tool 版本记录

> 说明：本文件用于在 GUI 的 About 页面展示历史版本变更。每次发布新版本时，在文末追加新条目即可。
> 相关文档： [README_ZH](../README_ZH.md) · [版本发布流程](RELEASE_GUIDE_ZH.md) · [CMake 使用指南](cmake.md) · [操作手册](USER_GUIDE_ZH.md)

SFD Tool GUI

Version 1.8.5.0 LTV Edition

Copyright 2026 Ryan Crepa    QQ:3285087232    @Bilibili RyanCrepa

Version logs:

---v 1.7.1.0---
First GUI Version
--v 1.7.1.1---
Fix check_confirm issue
---v 1.7.1.2---
Add Force write function when partition list is available
---v 1.7.2.0---
Add debug options
---v 1.7.2.1---
Add root permission check for Linux
---v 1.7.2.2---
Add dis_avb function
---v 1.7.2.3---
Fix some bugs
---v 1.7.3.0---
Add some advanced settings
---v 1.7.3.1---
Add SPRD4 one-time kick mode
---v 1.7.3.2---
Fix some bugs
---v 1.7.3.3---
Fix dis_avb func
---v 1.7.3.4---
Fix some bugs, improved UI
---v 1.7.3.5---
Fix some bugs
---v 1.7.4.0---
Add window dragging detection for Windows dialog-showing issue
---v 1.7.4.1---
Add CVE v2 function, fix some bugs
---v 1.7.4.2---
Fix some bugs, add crash info displaying
---v 1.7.4.3---
Fix some bugs
---v 1.7.5.0---
Fix some bugs, improved console
---v 1.7.5.1---
Fix some bugs, add partition table modify function, add DHTB Signature read for ums9117
---v 1.7.5.2---
Add slot flash/read manually set, add storage/slot showing
---v 1.7.6.0---
Add PAC flash func, auto FDL send
---v 1.7.7.0---
UI Refactor, optimization code logic.
---v 1.7.7.3---
- Fix CI build issues for x86 (GitHub Actions)
- Fix missing .mo file when packaging
- Fix build.yml parsing error


---v 1.7.7.4---
新增更新版本脚本

---v 1.7.7.5---
完善各平台打包脚本，支持快捷键退出

---v 1.7.7.6---
修复：编译脚本-mscos最低支持版本11.0

---v 1.7.7.7---
优化版本命名

---v 1.7.7.8---
优化：打包文件全部都放在 output 下

---v 1.7.7.9---
移除fdl路径占位配置

---v 1.7.7.10---
修复：打包规则

---v 1.7.7.11---
修复：windows和linux找不到mo文件；windows找不到intl-8.dll或libintl-8.dll

---v 1.7.7.12---
修复：linux下关于无法展示版本日志；windows缺失vcpkg gettext运行库

---v 1.7.7.15---
修复：windows无法展示中文

---v 1.7.7.16---
修复：widnows下中英文切换

---v 1.8.0.0---
修复：自动探测硬件信息导致fdl执行失败；helper空引用导致读取分区后程序崩溃；兼容分区表模式下清理 GPT 计数，避免读 prodnv 仍错误访问失效 ptable 崩溃

---v 1.8.0.1---
修复：数据块大小问题，macOS下.app应用写入文件权限导致崩溃的问题

---v 1.8.0.2---
修复
    修复 CI 在 macOS 平台编译报错的问题
    修复 GUI 全分区备份路径以及取消提示的本地化问题
    修复部分界面文案未翻译的问题
    修复全盘备份打包路径错误的问题
    修复 AUTO_DEFAULT 模式下执行全盘备份导致程序崩溃的问题
    修复“恢复默认块大小”时未正确回退到握手默认步长并恢复 AUTO 读速率的问题
    修复并区分 macOS .app 与 CLI 的分区读取路径行为，确保“恢复默认块大小”后回到旧链路的默认读速率
优化
    优化分区表读取：仅在启用兼容模式时通过兼容方法读取分区表，避免每次执行 FDL2 时重复遍历分区表导致的额外延迟
    优化 macOS .app 备份取消路径的选择逻辑，并在取消时提示实际保存位置
    优化 macOS .app 备份保存路径：统一使用固定文稿目录，并为全盘备份自动创建按时间命名的子目录
调整 / 重构相关
    在 ui_common 中引入块大小模式配置结构，为后续块大小相关重构做准备
    引入块大小模式配置，并根据该配置在 GUI 中切换分区备份新旧链路
    单分区读取根据块大小模式在旧链路与 FlashService 之间自动切换
    统一 GUI 中 block_size 的使用方式，全面接入新的块大小模式配置
    为分区读取和全盘备份接入取消逻辑，支持在操作中途安全取消
    为块大小模式与分区读写路径增加详细日志，便于排查“恢复默认块大小”后读速异常等问题
    更新 sfd_tool.po 多语言翻译文件
    合并 master 分支最新改动到 develop 分支

---v 1.8.1.0---
修复
    - 修复默认握手块大小未正确生效的问题，确保 AUTO 模式下读写速率回到预期步长。（a70dcfb）
    - 修复分区页结构问题并增强分区读取进度提示，避免进度显示异常。（9790b78）
    - 修复分区备份路径与块大小重置相关的细节问题，使 GUI 行为与后台 FlashService 一致。（5d89da4, f341029, 79bcb17）
    - 修复进度状态栏文案与部分提示文本，提升状态信息可读性。（67b2d84, a62a8ab）

优化
    - 优化分区备份进度回调与路径逻辑，让备份过程中的状态更新更加清晰、稳定。（5d89da4）
    - 优化进度状态栏的布局与分区读取完成后的弹窗行为，使进度与结果提示更加直观。（67b2d84）

调整 / 重构相关
    - 统一分区读取服务与块大小模式/模型，引入 PartitionReadService，将分区读取与备份路径整合到统一链路中，减少旧逻辑分叉。（88f253c, 3d3c6f9, 3f41648）
    - 将 GUI 备份路径逻辑与新的分区读取服务及块大小模型对齐，统一处理 GUI 端备份路径与底层服务之间的交互。（705ce80）
    - 为分区备份路径和块大小重置补充专门的测试用例，覆盖回归场景。（f341029, 79bcb17）

测试 / 构建 / CI 与平台适配
    - 添加并完善本地构建脚本，修复 dev 构建缺失 core/pages 子目录等问题，保证 `dev.sh` 能从零成功构建并运行。（fb08a6a, c92d28b, 8509744）
    - 本地构建默认打开单元测试，并对齐本地与 CI 的 CMake 配置与测试行为，修复 macOS CI 编译和测试路径问题，避免 blocksize 测试在 mac 下崩溃。（87a3602, 86d01f4, e9ff621, 6932efc）
    - 为测试目标提供 GUI/日志桩实现，并在 CI 中禁用对 GTK/GUI 的硬依赖，避免因缺少 GTK 头文件或运行环境导致的构建失败。（5aecb32, 8fcdd20）
    - 修复 Fedora / RPM 构建环境下 GTK3、pango 链接错误（“DSO missing from command line”等），保证 RPM 构建在主流发行版上可用。（da7485e, b0f4ba4）
    - 调整 Windows 平台支持：移除不再维护的 Windows x86 构建，仅保留 Windows x64 版本，简化维护面。（fca954b）
    - 修复 Windows x64 单元测试在 CI 中的链接与运行问题：
        * 补全 GTK3 / libusb / libintl / savepath 等符号的链接配置，解决 LNK2019 与未解析符号错误。（ad79a86, b225ccc, e6544e4）
        * 解决 blocksize 相关单元测试在运行时缺少 DLL 导致的 0xc0000135 异常，并在 CI 中临时禁用该用例以保证流水线稳定通过。（12e9b25, 90be8ff）

---v 1.8.2.0---
新增：从文件夹恢复分区功能

---v 1.8.2.1---
新增：从文件夹恢复功能（批量刷入分区），完善：x64打包（移除x86，支持LibUSB和SPRD驱动）

---v 1.8.2.2---
优化：打包脚本；更新：全面更新用户指南、架构与发布流程说明

---v 1.8.2.3---
修复：分区备份校验兼容 NV 分区 512 字节偏移行为，完善：国际化自动化脚本

---v 1.8.2.4---
修复 Proxy32 相关问题，提升设备兼容性
修复 PAC 相关逻辑异常，避免刷机流程出错
修复 Linux 下备份文件和文件夹被写成 root 权限的问题
修复 macOS 默认窗口尺寸计算逻辑，改用非废弃 GTK API
优化全盘备份行为：默认排除 userdata 分区，并在完成后保留提示信息
优化进度条刷新逻辑：按已读字节量实时刷新 GUI 显示，更平滑直观
优化窗口默认尺寸与分辨率适配，避免小屏幕分辨率下遮挡底部状态栏
更新 locale/sfd_tool.pot 翻译模板，同步最新文案
合并上游 develop 分支最新改动

---v 1.8.2.5---
优化：进度条实时更新，自适应UI窗口可调整

---v 1.8.2.6---
优化：底部状态栏适配和数据展示

---v 1.8.3.0---
重构：UI相关的目录结构

---v 1.8.3.1---
优化：统一分区读取与全盘备份的数据块配置与显示

---v 1.8.3.2---
优化批量恢复刷机体验；修复连接时间误触

---v 1.8.3.3---
修复：super 分区备份状态栏显示问题；优化：分区操作页按钮布局；新增：分区操作页新增“抹除所有分区”功能

---v 1.8.3.4---
完善：文件夹分区恢复并新增备份完整性检查；优化：修改分区导致线程阻塞

---v 1.8.3.5---
完善Trustos修补功能

---v 1.8.4.0---
修复已知bug, 工具进入长期状态

---v 1.8.4.5---
更新PAC刷写机制

---v 1.8.5.0---
修复PAC刷写逻辑，lambda获取错误

Under GPL v3 License
Github: C-Hidery/sfd_tool
LTV means Long-time-version
