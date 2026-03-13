# sfd_tool

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
# Cmake编译见md/cmake.md
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

脚本会在临时目录 `/tmp/build-sfd-tool/` 下执行 `dpkg-buildpackage`，并生成 `sfd-tool_*.deb` 安装包。

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
