# SFD Tool 使用说明

SFD Tool 是一个面向 Spreadtrum / UNISOC 设备的跨平台刷机与维护工具，
同时提供 **图形界面（GUI）** 和 **命令行（CLI）** 两种使用方式。

它主要用于：

- 备份 / 还原设备分区；
- 刷写 PAC 固件包；
- 进行手动低级操作（按地址读写、执行代码等）；
- 执行一些高级维护功能（如 AVB 相关操作、调试辅助等）。

> 更完整的图形界面使用说明请阅读
> **[docs/USER_GUIDE_ZH.md](docs/USER_GUIDE_ZH.md)**。

---

## 支持平台与预编译包

当前主要支持并测试的运行平台：

- **Windows 10/11 x64/x86**
- **Linux x86_64**（如 Debian/Ubuntu、部分 RPM 系发行版）
- **macOS**（近期版本）
- **Termux / Android**（仅命令行模式，通过 `--no-gui`）

GitHub Releases 中通常会提供：

- Windows x64 可执行文件（LibUSB / SPRD 驱动版本）；
- Linux `.deb` / `.rpm` 安装包；
- macOS `.dmg` 应用包。

Arch Linux 用户可以使用此AUR包：

[sfd_tool_arch_aur_package](https://aur.archlinux.org/packages/sfd-tool)

具体支持的系统版本和产物名称，请以最新 Release 页面和
[.github/workflows/build.yml](.github/workflows/build.yml) 为准。

![Logo](icon.png)

![License](https://img.shields.io/github/license/C-Hidery/sfd_tool)

**License: GPL-3.0-or-later**

## 介绍


这是工具'spreadtrum_flash'的维护


**(这个版本添加了GTK3图形化)**

[spreadtrum_flash](https://github.com/TomKing062/spreadtrum_flash)

---

编译之前:

``` bash
sudo apt update
# Ubuntu/Debian
sudo apt install libusb-1.0-0-dev gcc g++ libgtk-3-dev pkg-config make cmake gettext
# Fedora/RHEL
sudo dnf install gcc-c++ gtk3-devel libusb1-devel libusb1 pkgconf-pkg-config make cmake gettext
# macOS
brew install libusb gtk+3 pkg-config make cmake gettext
# Android(Termux)
pkg install x11-repo
pkg install termux-api libusb clang git pkg-config gtk3 glib pango libcairo gdk-pixbuf at-spi2-core xorgproto xorg-util-macros make cmake attr gettext
```

然后make:
``` bash
make
# Termux
make termux
# Locales 国际化
make locales
```

在Termux上使用（无GUI）:

``` bash
# 搜索OTG设备
termux-usb -l
#输出示例
#[
#  "/dev/bus/usb/xxx/xxx"
#]
# 授权OTG设备(示例)
termux-usb -r /dev/bus/usb/xxx/xxx
# 运行
termux-usb -e './sfd_tool --no-gui --usb-fd' /dev/bus/usb/xxx/xxx
```

**警告：你可能需要使用ROOT权限运行工具！**

---

***对比spd_dump修改的一些CLI命令:***

    part_table [FILE PATH]

**等效于`partition_list`命令.**

    exec_addr [BINARY FILE] [ADDR]
    
**已修改, 你需要提供文件路径和发送地址**

    exec <ADDR>

**已修改, 在执行FDL1是需要提供FDL1地址**

    read_spec [PART NAME] [OFFSET] [SIZE] [FILE]

**已修改, 等效于`read_part`命令，`read_part` 之后就等同于 `r`**

    --no-fdl

**新参数, 如果你想在SPRD4下无FDL刷机，那么执行它.**

    cptable
    
**新命令, 如果分区表不可用，可在FDL2下使用兼容性模式读取分区表**

    --no-gui

**新参数，无GUI启动工具（下文也会提到）**

    dis_avb_tos
    
**新命令，谨慎使用**

    --tool-mode

**新参数，你可以使用一些不需要设备连接的命令，使用`exit`退出**

## GUI下的重连模式

您可以用下面的参数启动`sfd_tool`:

    --reconnect

这个选项将使工具尝试重连设备，等效于CLI模式中的`-r`参数

## 警告 - 禁用 VERITY 与 AVB

**命令：** `dis_avb_tos` / `verity 0`

此命令将**禁用**您设备上的 Android DM-verity 和 AVB 安全校验机制。

## 后果说明

- 您的设备将**失去系统完整性保护**
- 以下应用可能**停止工作**：
  - 银行 App 和 Google Pay
  - 使用指纹/密码支付的应用（微信支付、支付宝等）
  - Netflix 高清播放及部分 DRM 保护内容
  - 带有反作弊保护的游戏
- 恶意软件可在无任何警告的情况下轻松修改您的系统
- 您的设备将变得极易遭受数据窃取攻击

## 操作前须知

- 对于  `dis_avb_tos`，SFD Tool 已自动备份您的 `trustos` 分区（请查看`trustos-orig.bin`）
- **请妥善保管此备份** - 恢复时需要用到

## 如何恢复

对于`verity 0`, 使用命令：`verity 1`

对于 `dis_avb_tos`，刷回`trustos-orig.bin`

[更多技术细节](https://github.com/TomKing062/unisoc_chipram_signcheck_exploit/)

---

## 主要功能概览

- **图形界面（GTK3）**
  - 「连接」页：设备连接与基本信息。
  - 「分区操作」页：分区列表、备份、还原、从文件夹批量还原等。
  - 「PAC 刷写」页：加载 PAC、查看分区、选择刷写内容并执行刷机。
  - 「手动操作」页：按地址读写、执行指定地址代码等高级操作。
  - 「高级操作」页：其他高风险维护功能。
  - 「高级设置」页：块大小模式、调试选项等全局设置。
  - 「调试设置」页：日志等级、实验性开关等。
  - 「日志」页：运行时日志查看。
  - 「关于」页：版本信息和 [docs/VERSION_LOG.md](docs/VERSION_LOG.md) 中的更新记录。

- **分区备份 / 还原**
  - 支持单分区备份/还原；
  - 使用统一的 `PartitionReadService` 与块大小模式，避免对齐问题；
  - 支持 **从文件夹批量还原**：从一个目录中自动匹配多个分区镜像并一次性刷写。

- **PAC 刷机**
  - 解析 PAC 文件，展示其中的分区列表；
  - 按选项执行 PAC 刷机流程；
  - 可以只解析 PAC，不立即刷写。

- **命令行模式（CLI）**
  - 使用 `--no-gui` 参数在终端中运行；
  - 提供读取分区表、按地址读取、执行代码等命令；
  - 完整命令列表请使用 `sfd_tool --help` 或 Linux 下 `man sfd-tool` 查看。

---

## 构建方式（CMake）

项目使用 **CMake** 作为主要构建系统，根目录下的旧版 Makefile 仅作为历史兼容参考。

最简单的 out-of-source 构建示例：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

不同平台可选择不同生成器：

- Windows：Visual Studio 生成器；
- macOS / Linux：Ninja 或 Unix Makefiles。

详细说明请参考：

- [docs/cmake.md](docs/cmake.md) – CMake 使用说明（中文，包含 per-user 配置与 `ui_language`）；
- [docs/cmake_EN.md](docs/cmake_EN.md) – CMake 使用说明（英文简版）。


---

## 运行与使用

### 图形界面

构建完成后，从构建目录启动 GUI，可例如：

```bash
./build/sfd_tool
```

安装后可以通过桌面菜单或执行 `sfd_tool` 启动。

图形界面的详细使用步骤（包括「从文件夹还原」、「高级操作」等）
请阅读 [docs/USER_GUIDE_ZH.md](docs/USER_GUIDE_ZH.md)。

### 命令行模式

在终端中以命令行方式运行：

```bash
sfd_tool --no-gui --help
```

常见用法示例：

- 查看分区表；
- 按地址读取数据到文件；
- 执行指定地址的代码；
- 使用 `--no-fdl` 等参数控制启动方式。

**注意：** CLI 命令列表随版本演进，权威说明以 `--help` 输
出和 `man sfd-tool` 为准，本项目文档只给出典型示例。

---

## 配置文件与多语言

SFD Tool 在不同平台上使用 per-user 配置文件保存用户偏好，
包括 UI 语言、最近使用路径等。配置文件路径由 `config_service`
统一管理，一般位于当前用户目录下。

关键字段：

- `ui_language`：`auto` / `zh_CN` / `en_US` 等；
- 旧版本可能在项目根目录存放配置文件，首次运行新版本时会自动迁移。

更多细节请查看 [docs/cmake.md](docs/cmake.md) 中关于配置与语言的章节。

---

## 文档索引

- **中文用户手册（权威 GUI & CLI 使用说明）**
  - [docs/USER_GUIDE_ZH.md](docs/USER_GUIDE_ZH.md)
- **英文总览**
  - [README.md](README.md)
- **架构与开发说明**
  - [ARCHITECTURE.md](ARCHITECTURE.md)
- **构建与配置**
  - [docs/cmake.md](docs/cmake.md)
  - [docs/cmake_EN.md](docs/cmake_EN.md)
- **发布与 CI（维护者）**
  - [docs/RELEASE_GUIDE_ZH.md](docs/RELEASE_GUIDE_ZH.md)
  - CI 工作流：[.github/workflows/build.yml](.github/workflows/build.yml)
- **更新日志**
  - [docs/VERSION_LOG.md](docs/VERSION_LOG.md) – 在「关于」页展示。

---

## 风险提示与免责声明

对设备分区和引导链进行写入操作具有较高风险，
错误操作可能导致设备变砖或数据永久丢失。

- 强烈建议在任何刷写和高级操作前先完整备份关键分区；
- 对不理解的选项与功能不要轻易尝试；
- 仔细阅读用户手册以及界面上的警告提示，特别是
  「高级操作」相关内容。

本项目遵循 GPL 等开源许可证发布，详细信息请参见仓库中的 LICENSE 及相关说明。

## 无root权限运行SFD Tool?

实际上，SFD Tool可以在没有root权限的情况下正常运行，但首先需要添加一条`udev`规则。

创建`/etc/udev/rules.d/80-spd.rules`，写入这一条：

```text
SUBSYSTEMS=="usb", ATTRS{idVendor}=="1782", ATTRS{idProduct}=="4d00", MODE="0666", TAG+="uaccess"
```
