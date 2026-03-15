# sfd_tool

[**中文文档**](README_ZH.md) · [User Guide (ZH)](docs/USER_GUIDE_ZH.md) · [Architecture](ARCHITECTURE.md) · [Release Guide (ZH)](docs/RELEASE_GUIDE_ZH.md) · [CMake Guide (ZH)](docs/cmake.md) · [CMake Guide (EN)](docs/cmake_EN.md) · [Version Log](docs/VERSION_LOG.md)

![Logo](/assets/icon.png)

![License](https://img.shields.io/github/license/C-Hidery/sfd_tool)

The modified version of tool 'spreadtrum_flash'

sfd_tool is a maintenance release for spreadtrum_flash

**(This version adds GTK3 graphical interface)**

[spreadtrum_flash](https://github.com/TomKing062/spreadtrum_flash)

---

Run this before making:

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

Then make:
``` bash
make
# Compile and load i18n translations (Optional)
make locales
# Termux
make termux
# Cmake method see docs/cmake_EN.md
```

> Note: The Makefile-based build is kept for compatibility. For cross-platform development, CMake is the recommended entry.

### Build with CMake (recommended)

On Linux/macOS:

```bash
cmake -S . -B build
cmake --build build
```

On Windows (Visual Studio 2022):

```bash
cmake -S . -B build -G "Visual Studio 17 2022"
```

Then open `build/sfd_tool.sln` in Visual Studio.
The legacy solution file at the repository root, [sfd_tool.sln](sfd_tool.sln), is kept for compatibility only and may lag behind the CMake build. Please prefer the CMake-generated solution for development.

### Helper scripts

On Linux/macOS:

```bash
./scripts/dev.sh     # Configure + build Debug with CMake and run sfd_tool
./scripts/release.sh # Configure + build Release with CMake
./scripts/bump_version.sh # Interactive version bump & changelog update
```

On Windows (PowerShell):

```powershell
.\scripts\dev.ps1       # Configure + build Debug with CMake/VS and start sfd_tool
.\scripts\release.ps1   # Configure + build Release with CMake/VS
.\scripts\bump_version.ps1 # Interactive version bump & changelog update
```

### Packaging & Release

On Debian/Ubuntu, you can build a `.deb` package using the helper script:

```bash
sudo apt-get update
sudo apt-get install -y build-essential debhelper devscripts \
  libgtk-3-dev libusb-1.0-0-dev pkg-config g++ \
  imagemagick desktop-file-utils cmake

./packaging/build-deb.sh
```

The script runs `dpkg-buildpackage` in a temporary directory under `/tmp/build-sfd-tool/` and produces `sfd-tool_*.deb` packages.

### Prebuilt binaries (GitHub Releases)

Official GitHub Releases provide:

- **sfd_tool_SPRD_Release**: Windows x86 build using the official SPRD serial driver (Channel9.dll). This variant has the best compatibility and is recommended for older machines or when stability matters most.
- **sfd_tool_LibUSB_Release**: Windows x64 build using libusb, suitable for modern 64-bit Windows systems.
- **Linux DEB / RPM packages** for mainstream Debian/Ubuntu and Fedora/RPM-based distributions.
- **macOS DMG** installer containing a standard `SFD Tool.app` application.

#### macOS notes

- The macOS release is shipped as a `SFD Tool.app` bundle that still **depends on system GTK3/libusb/gettext** at runtime; these libraries are not fully statically bundled.
- For building from source, please refer to the earlier `brew install libusb gtk+3 pkg-config` instructions. For users running the prebuilt DMG, if you encounter missing-library errors at launch, you can install the runtime dependencies via Homebrew:

  ```bash
  brew install gtk+3 libusb gettext
  ```

- Usage:
  1. Download `sfd_tool_macos.dmg` from GitHub Releases;
  2. Double-click the DMG and drag `SFD Tool.app` into `/Applications`;
  3. Launch `SFD Tool` from Launchpad or Finder.

- Gatekeeper and quarantine:
  - After downloading from a browser, macOS Gatekeeper may show a warning such as “cannot be opened because the developer cannot be verified”.
    - Recommended: in Finder, **right-click `SFD Tool.app` → Open**, then confirm in the dialog; subsequent launches will be normal.
    - If the app is still quarantined, advanced users can clear the download quarantine attribute explicitly:

      ```bash
      xattr -d com.apple.quarantine /Applications/SFD\ Tool.app
      ```

    This only removes the “downloaded from the Internet” flag and does not modify the program contents.

### Internationalization (i18n)
This tool supports multi-language adaptation:
* `make` will build the core application in English.
* `make locales` will compile the translation file `locale/zh_CN/LC_MESSAGES/sfd_tool.po`. The program will automatically display Chinese when running in a Chinese-supported locale environment.
* **Adding translations for a new language**: You can use the generated `locale/sfd_tool.pot` template file to translate and place the `.po` file into the corresponding `locale/<Language Code>/LC_MESSAGES/` directory.

Use on Termux(No GUI):

``` bash
# Search OTG device
termux-usb -l
[
  "/dev/bus/usb/xxx/xxx"
]
# Authorize OTG device
termux-usb -r /dev/bus/usb/xxx/xxx
# Run
termux-usb -e './sfd_tool --no-gui --usb-fd' /dev/bus/usb/xxx/xxx
```

**WARN : You may must run tool as root to connect to device correctly!**

---

***Modified commands:***

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
