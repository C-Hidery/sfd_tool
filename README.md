# SFD Tool

SFD Tool is a cross‑platform GUI and CLI tool for working with Spreadtrum/UNISOC devices.
It focuses on **safe partition backup/restore, PAC flashing, and advanced maintenance
operations**, with first‑class support for modern 64‑bit platforms.

> NOTE: This README is a high‑level English overview. The **Chinese user guide**
> in [docs/USER_GUIDE_ZH.md](docs/USER_GUIDE_ZH.md) is the canonical, most
> detailed end‑user documentation.

![Logo](icon.png)

![License](https://img.shields.io/github/license/C-Hidery/sfd_tool)

**License: GPL-3.0-or-later**

[中文文档](README_ZH.md)

## Introduction


This is the modified version of tool 'spreadtrum_flash'

sfd_tool is a maintenance release for spreadtrum_flash

**(This version adds GTK3 graphical interface)**

[spreadtrum_flash](https://github.com/TomKing062/spreadtrum_flash)

---

Run this before making:

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

Then make:
``` bash
make
# Termux
make termux
# Locales i18n
make locales
```

Use on Termux(No GUI):

``` bash
# Search OTG device
termux-usb -l
# example
#[
#  "/dev/bus/usb/xxx/xxx"
#]
# Authorize OTG device
termux-usb -r /dev/bus/usb/xxx/xxx
# Run(example)
termux-usb -e './sfd_tool --no-gui --usb-fd' /dev/bus/usb/xxx/xxx
```

**WARN : You may must run tool as root to connect to device correctly!**

---

***Modified CLI commands:***

    part_table [FILE PATH]

**This command is equivalent to the `partition_list` command.**

    exec_addr [BINARY FILE] [ADDR]
    
**Modified, you need to provide file path and address**

    exec <ADDR>

**Modified, you need to provide FDL1 address when you execute FDL1**

    read_spec [PART NAME] [OFFSET] [SIZE] [FILE]

**Modified, equivalent to the `read_part` command, then `read_part` is equivalent to the `r`**

    --no-fdl

**New option, execute it if you want to connect to device without FDL1/2(Only Sprd4 Mode).**

    cptable
    
**New command, use it to get partition table through compatibility method(FDL2 only)**

    --no-gui

**New parameter, open sfd_tool without GUI**

    dis_avb_tos

**New Command, use with caution.**

    --tool-mode

**New parameter, you can use some commands that DO NOT need device connection, use `exit` to exit tool**

## Reconnecting mode in GUI mode

You can run `sfd_tool` with this parameter:

    --reconnect

This option will let tool try to reconnect to device, equivalent to `-r` in CLI.

## WARNING - DISABLE VERITY & AVB

**Command:** `dis_avb_tos` / `verity 0`

This command will **DISABLE** Android's DM-verity and AVB security verification mechanisms on your device.

## CONSEQUENCES

- Your device will have **NO system integrity protection**
- The following apps will **STOP WORKING**:
  - Banking apps and Google Pay
  - Apps using fingerprint/password for payments (WeChat Pay, Alipay, etc.)
  - Netflix HD streaming and some DRM-protected content
  - Games with anti-cheat protection (PUBG, Genshin Impact, etc.)
- Malware can easily modify your system without any warning
- Your device becomes significantly more vulnerable to data theft

## BEFORE CONTINUING

- For  `dis_avb_tos` , SFD Tool has automatically backed up your `trustos` partition (see `trustos-orig.bin`)
- **Keep this backup safe** - you will need it for recovery

## TO RESTORE

For `verity 0`, Use command: `verity 1`

For  `dis_avb_tos`, flash back `trustos-orig.bin`

[Useful link for more details](https://github.com/TomKing062/unisoc_chipram_signcheck_exploit/)

## Features

- **Cross‑platform GUI** (GTK3)
  - Tabs for Connect, Partition Operation, PAC Flash, Manual Operation,
    Advanced Operation, Advanced Settings, Debug Options, Log, and About.
- **Partition backup and restore**
  - Read/write individual partitions.
  - Block‑size‑aware backup/restore pipeline to avoid alignment issues.
  - **Restore from folder**: batch restore multiple partitions from a folder
    of images (see user guide for details).
- **PAC firmware flashing**
  - Inspect PAC contents and flash selected partitions.
- **Manual low‑level operations**
  - Read/write by address, execute code at arbitrary addresses, etc.
- **Advanced maintenance tools**
  - Other expert‑only functions for debugging and recovery.
- **CLI mode**
  - Run without GUI using `--no-gui`.
  - Rich command set for scripting and headless environments.
- **Internationalisation (i18n)**
  - Translated UI (e.g. `zh_CN`, `en_US`), using gettext `.po`/`.mo` files.

## Supported platforms & binaries

SFD Tool is primarily tested and released for:

- **Windows 10/11 x64/x86**
- **Linux x86_64** (e.g. Debian/Ubuntu, RPM‑based distros)
- **macOS** (recent versions)
- **Termux / Android** (CLI only, via `--no-gui`)

Prebuilt binaries and packages are provided on GitHub Releases, typically including:

- Windows x64/x86 executables (LibUSB(x64) and/or SPRD(x86) driver builds).
- `.deb` packages for popular Debian/Ubuntu versions.
- `.rpm` packages for RPM‑based distributions.
- macOS `.dmg` application bundles.

And Arch Linux users can use this pre-built package in aur:

[sfd_tool_arch_aur_package](https://aur.archlinux.org/packages/sfd-tool)

For exact platforms and artifact names, refer to the latest release page and
[.github/workflows/build.yml](.github/workflows/build.yml).

## Building from source (CMake)

SFD Tool uses **CMake** as the primary build system. The legacy Makefile is
kept only for reference.

Basic out‑of‑source build (example):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

On Windows you can use a Visual Studio generator, on macOS and Linux Ninja or
Makefiles. See:

- [docs/cmake_EN.md](docs/cmake_EN.md) – English CMake/build guide.
- [docs/cmake.md](docs/cmake.md) – Chinese CMake/build guide with more
  details, including per‑user config and `ui_language`.

## Running

### GUI

After building, run the GUI executable from your build directory, e.g.:

```bash
./build/sfd_tool
```

On installed systems you can start SFD Tool from your desktop environment
(menu entry created by the package) or by running `sfd_tool` from a terminal.

### CLI mode

For headless and scripted environments you can run SFD Tool in CLI mode:

```bash
sfd_tool --no-gui --help
```

This prints the full list of commands and options. Some examples:

- List partition table.
- Read a specific region to a file.
- Execute code at a given address.
- Use `--no-fdl` or similar flags for special boot modes.

For the authoritative CLI reference, see:

- Inline help: `sfd_tool --help`
- Manpage (on Linux): `man sfd-tool`

## Configuration & language

SFD Tool stores per‑user configuration (UI language, recent paths, etc.) in a
JSON config file in the user’s home directory (location varies by platform).

Key points:

- `ui_language` can be set to `auto`, `zh_CN`, `en_US`, etc.
- On first run the tool may migrate legacy config files from the project root
  into the per‑user location.

See [docs/cmake.md](docs/cmake.md) for details.

## Documentation map

- **Chinese user guide (canonical for end users)**
  - [docs/USER_GUIDE_ZH.md](docs/USER_GUIDE_ZH.md)
- **Project overview (English)**
  - This README
  - [README_ZH.md](README_ZH.md) – Chinese overview
- **Architecture & development**
  - [ARCHITECTURE.md](ARCHITECTURE.md)
- **Build & configuration**
  - [docs/cmake_EN.md](docs/cmake_EN.md)
  - [docs/cmake.md](docs/cmake.md)
- **Release & CI (maintainers)**
  - [docs/RELEASE_GUIDE_ZH.md](docs/RELEASE_GUIDE_ZH.md)
  - CI workflow: [.github/workflows/build.yml](.github/workflows/build.yml)
- **Changelog**
  - [docs/VERSION_LOG.md](docs/VERSION_LOG.md) – shown in the About tab.

## Safety and disclaimer

Working with device partitions and bootloaders is inherently risky. Incorrect
operations can brick a device and may lead to data loss.

- Always back up critical partitions before flashing or using advanced tools.
- Do not use functions you do not fully understand.
- Read the warnings in the user guide and on the Advanced Operation tab.

SFD Tool is distributed under the terms of the GPL. See the repository for
license details.

## Want to Run SFD Tool without root privileges?

Actually, SFD Tool can be run without root privileges, but you have to create a `udev` rule.

Create `/etc/udev/rules.d/80-spd.rules` with this line:

```text
SUBSYSTEMS=="usb", ATTRS{idVendor}=="1782", ATTRS{idProduct}=="4d00", MODE="0666", TAG+="uaccess"
```

By the way, `80-spd.rules` is also in repo.
