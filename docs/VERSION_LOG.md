# sfd_tool 版本记录

> 说明：本文件用于在 GUI 的 About 页面展示历史版本变更。每次发布新版本时，在文末追加新条目即可。
> 相关文档： [README_ZH](../README_ZH.md) · [版本发布流程](RELEASE_GUIDE_ZH.md) · [CMake 使用指南](cmake.md) · [操作手册](USER_GUIDE_ZH.md)

SFD Tool GUI

Version 1.8.0.1 LTV Edition

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

---v 1.7.7.16---
修复：widnows下中英文切换

---v 1.8.0.0---
修复：自动探测硬件信息导致fdl执行失败；helper空引用导致读取分区后程序崩溃；兼容分区表模式下清理 GPT 计数，避免读 prodnv 仍错误访问失效 ptable 崩溃

---v 1.8.0.1---
修复：数据块大小问题，macOS下.app应用写入文件权限导致崩溃的问题

Under GPL v3 License
Github: C-Hidery/sfd_tool
LTV means Long-time-version
