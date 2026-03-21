# sfd_tool

[English README](README.md) · [架构文档](ARCHITECTURE.md) · [操作手册](docs/USER_GUIDE_ZH.md) · [发布流程](docs/RELEASE_GUIDE_ZH.md) · [CMake 使用指南](docs/cmake.md) · [版本记录](docs/VERSION_LOG.md)

## 文档索引

### 面向普通用户

- **使用手册（GUI + 命令行）**
  详细操作说明（图形界面各标签页、常见刷机流程、命令行模式）：
  [docs/USER_GUIDE_ZH.md](docs/USER_GUIDE_ZH.md)

- **配置文件与界面语言**
  per-user 配置路径、旧版配置迁移、`ui_language` 字段说明：
  [docs/cmake.md](docs/cmake.md) 中的「界面语言与 per-user 配置」章节（英文版参见 [docs/cmake_EN.md](docs/cmake_EN.md)）

- **平台特定用法**
  - Termux / Android（无图形界面）：示例命令见本 README 末尾以及 [docs/USER_GUIDE_ZH.md](docs/USER_GUIDE_ZH.md) 中的「只使用命令行模式」一节；
  - macOS / Windows / Linux 预编译版本说明：见下文「预编译二进制（GitHub Releases）」小节。

### 面向开发者

- **架构与内部设计**
  模块分层、core/service/ui 责任划分、重构方向：
  [ARCHITECTURE.md](ARCHITECTURE.md)

- **构建与开发工作流**
  - CMake 使用指南（中文）：[docs/cmake.md](docs/cmake.md)
  - CMake Usage Guide（English）：[docs/cmake_EN.md](docs/cmake_EN.md)
  - 本地开发/发布脚本（`dev.sh` / `release.sh` / `bump_version.sh` 等）：见下文「开发/发布辅助脚本」一节。

- **测试与 CI**
  - CTest 测试说明：见 CMake 指南第 6 章；
  - GitHub Actions 构建矩阵与自动发布流程：
    [docs/RELEASE_GUIDE_ZH.md](docs/RELEASE_GUIDE_ZH.md)

### 版本与发布

- **版本记录（用户可见变化）**
  [docs/VERSION_LOG.md](docs/VERSION_LOG.md) —— 同步展示在 GUI「About」标签页中。

- **版本号策略与发布流程**
  如何更新 `VERSION.txt` / `docs/VERSION_LOG.md`，以及通过 CI 自动生成 Release：
  [docs/RELEASE_GUIDE_ZH.md](docs/RELEASE_GUIDE_ZH.md)

![Logo](/assets/icon.png)

![License](https://img.shields.io/github/license/C-Hidery/sfd_tool)

工具'spreadtrum_flash'的修改版

sfd_tool是它的一个维护版本（原仓库已存档）

**（此版本添加了GTK3图形化）**

[spreadtrum_flash](https://github.com/TomKing062/spreadtrum_flash)

---

执行make时请先运行这个：

``` bash
sudo apt update
# Ubuntu/Debian
sudo apt install libusb-1.0-0-dev gcc g++ libgtk-3-dev pkg-config
# Fedora/RHEL
sudo dnf install gcc-c++ gtk3-devel libusb1-devel libusb1 pkgconf-pkg-config make
# macOS
brew install libusb gtk+3 pkg-config
# Android(Termux)
pkg install x11-repo
pkg install termux-api libusb clang git pkg-config gtk3 glib pango libcairo gdk-pixbuf at-spi2-core xorgproto xorg-util-macros
```

然后make:
``` bash
make
# 编译及加载国际化多语言支持（可选）
make locales
# Termux
make termux
# CMake 详细用法见 docs/cmake.md（中文）/ docs/cmake_EN.md（English）
```

> 说明：基于 Makefile 的构建方式主要用于兼容历史环境。对于日常开发，推荐使用 CMake 作为统一的构建入口。

### 使用 CMake 构建（推荐）

在 Linux/macOS 上：

```bash
cmake -S . -B build
cmake --build build
```

在 Windows（Visual Studio 2022）上：

```bash
cmake -S . -B build -G "Visual Studio 17 2022"
```

然后在 Visual Studio 中打开 `build/sfd_tool.sln` 进行开发调试。

根目录下的旧版解决方案文件 [sfd_tool.sln](sfd_tool.sln) 仅作为兼容/参考，可能与当前 CMake 配置存在一定滞后，请以 CMake 生成的解决方案为准。

### 开发/发布辅助脚本

在 Linux/macOS 上：

```bash
./scripts/dev.sh       # 使用 CMake 生成 Debug 构建并直接运行
./scripts/release.sh   # 使用 CMake 生成 Release 构建
./scripts/bump_version.sh # 交互式更新版本号和 docs/VERSION_LOG.md
```

在 Windows（PowerShell）上：

```powershell
.\scripts\dev.ps1         # 使用 CMake/VS 生成 Debug 构建并启动程序
.\scripts\release.ps1     # 使用 CMake/VS 生成 Release 构建
.\scripts\bump_version.ps1 # 交互式更新版本号和 docs/VERSION_LOG.md
```

### 打包与发布

> 详细发布与版本维护流程见：[RELEASE_GUIDE_ZH](docs/RELEASE_GUIDE_ZH.md)

在 Debian/Ubuntu 上，本地构建 .deb 包可以使用项目提供的脚本：

```bash
sudo apt-get update
sudo apt-get install -y build-essential debhelper devscripts \
  libgtk-3-dev libusb-1.0-0-dev pkg-config g++ \
  imagemagick desktop-file-utils cmake

./packaging/build-deb.sh
```

### 预编译二进制（GitHub Releases）

GitHub Releases 页面会提供以下预编译包：

- **`sfd_tool_windows_x64_sprd_vX.Y.Z.zip`**：Windows x64 版本，使用官方 SPRD 串口驱动（Channel9.dll），兼容性最佳，推荐在老机器或对稳定性要求较高的环境中使用；
- **`sfd_tool_windows_x64_libusb_vX.Y.Z.zip`**：Windows x64 版本，使用 libusb 驱动（需通过 Zadig 等工具安装），适合现代的 64 位 Windows 系统；
- **Linux `.deb` 包**（例如 `sfd-tool_X.Y.Z-1_amd64.deb`），适用于 Debian/Ubuntu 及其衍生发行版；
- **Linux `.rpm` 包**（例如 `sfd-tool-X.Y.Z-1.x86_64.rpm`），适用于 Fedora/RPM 系发行版；
- **macOS DMG**，文件名形如 `sfd_tool_macos_vX.Y.Z.dmg`，内含标准的 `SFD Tool.app` 应用；
- **`sfd_tool_all_vX.Y.Z.zip`**：整合包，包含上述各平台安装包以及顶层说明文档（`README*`、`ARCHITECTURE.md` 等）。

#### macOS 使用注意事项

- 使用方式：
  1. 从 GitHub Releases 下载形如 `sfd_tool_macos_vX.Y.Z.dmg` 的 DMG 文件；
  2. 双击打开 DMG，将 `SFD Tool.app` 拖动到 `/Applications`；
  3. 在 Launchpad 或 Finder 中打开 `SFD Tool` 即可。

- Gatekeeper 相关说明：
  - 从浏览器下载 DMG / 应用后，macOS 可能会因为 Gatekeeper 显示“无法验证开发者”等提示：
    - 推荐做法：在 Finder 中 **右键 SFD Tool.app / 打开**，按照系统提示再次确认即可；
    - 如仍被系统标记为隔离文件，可在终端中手动清除下载隔离标记（高级用法）：

      ```bash
      xattr -d com.apple.quarantine /Applications/SFD\ Tool.app
      ```

    以上命令只会移除系统的“从互联网下载”标签，不会修改程序内容。

### 多语言国际化 (i18n)
本工具支持中英文等多种语言适配：
* `make` 会优先构建出纯英文核心程序。
* `make locales` 将会对 `locale/zh_CN/LC_MESSAGES/sfd_tool.po` 翻译文件进行编译。在支持中文的环境下运行程序会自动显示中文。
* **为新语言增加翻译**：可使用项目中提取生成的 `locale/sfd_tool.pot` 词库文件，翻译后放入对应的 `locale/<语言代码>/LC_MESSAGES/` 目录下即可。

在Termux上使用(No GUI):

``` bash
# 搜索OTG设备
termux-usb -l
[
  "/dev/bus/usb/xxx/xxx"
]
# 授权OTG设备
termux-usb -r /dev/bus/usb/xxx/xxx
# 运行
termux-usb -e './sfd_tool --no-gui --usb-fd' /dev/bus/usb/xxx/xxx
```

> 说明：`--usb-fd` 为配合 `termux-usb -e` 使用的内部参数，Termux 会将 USB 设备的文件描述符传递给 `sfd_tool`；一般情况下无需手工填写 fd 数字。

**警告：您可能必须以 root 用户运行该工具才能正确连接设备!**

---

***修改的命令:***

    part_table [文件地址]

**等同于命令`partition_list`**

    exec_addr [镜像文件] [地址]

**你需要同时提供文件路径和地址**

    exec <地址>

**你需要在执行FDL1时提供FDL1的地址**

    read_spec [分区名] [偏移] [大小] [文件名]

**等同于`read_part`命令，之后`read_part`等同于`r`**

    --no-fdl

**新增参数，如果你想免FDL刷机，那么执行它（仅Sprd4模式）**

    cptable

**新增命令，使用此命令以兼容性方法获取分区表（仅FDL2）**

    --no-gui

**新增参数，以命令行方式打开工具**

完整命令与参数列表请以 `./sfd_tool --help` 或 Linux 下的 `man sfd-tool` 为准。