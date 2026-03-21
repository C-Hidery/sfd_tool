# CMake 使用指南

> 相关文档： [README_ZH](../README_ZH.md) · [操作手册](USER_GUIDE_ZH.md) ·
> [版本发布流程](RELEASE_GUIDE_ZH.md) · [版本记录](VERSION_LOG.md)

本文件是 **sfd_tool** 的 CMake 使用说明，覆盖 macOS / Linux /
Windows，从安装、配置、调试、测试到打包。所有命令假定在项目根目录
（包含 `CMakeLists.txt` 的目录）执行，并使用 `build_*/` 作为构建目录。

> 本项目以 **CMake 为主构建系统**。根目录的 Makefile 仅为兼容和简化
> 使用而保留，不再是构建规则的“权威来源”。

---

## 1. CMake 安装

### 1.1 macOS

1. 使用 Homebrew（推荐）
   ```bash
   brew install cmake
   ```
2. 或从官网下载安装 `.dmg` 图形安装包：
   https://cmake.org/download/

安装后检查：

```bash
cmake --version
```

### 1.2 Linux（Debian/Ubuntu 为例）

- Debian/Ubuntu：
  ```bash
  sudo apt-get update
  sudo apt-get install cmake
  ```
- Fedora：
  ```bash
  sudo dnf install cmake
  ```
- CentOS/RHEL：
  ```bash
  sudo yum install cmake
  ```
- Arch：
  ```bash
  sudo pacman -S cmake
  ```

若系统源版本过旧，可从官网安装二进制或源码构建。

### 1.3 Windows

1. 使用官方安装程序（推荐）
   - 下载：https://cmake.org/download/
   - 选择 Windows x64 Installer，安装时勾选 “Add CMake to system PATH”。
2. 或使用包管理器：
   - PowerShell（Windows 11/10）：
     ```powershell
     winget install Kitware.CMake
     ```
   - Chocolatey：
     ```powershell
     choco install cmake
     ```

安装后在 “x64 Native Tools Command Prompt for VS” 或 PowerShell 中检查：

```powershell
cmake --version
```

---

## 2. 基本使用模型

CMake 的基本流程是三步：

1. **配置（Configure）**：从源码生成构建系统（Makefile、Ninja、Visual
   Studio 解决方案等）；
2. **构建（Build）**：调用底层构建工具编译；
3. **测试 / 安装 / 打包**：使用 CTest 运行测试，使用
   `cmake --install` 安装，或配合脚本/CPack 打包。

统一采用 out-of-source 构建：

```bash
cmake -S . -B build_debug [额外选项]
cmake --build build_debug [额外选项]
```

可以根据需要使用多个构建目录（如 `build_debug`、`build_release`）。

---

## 3. 生成构建目录（Configure）

### 3.1 单配置生成器（macOS/Linux 常用）

以 Ninja（推荐）或 Unix Makefiles 为例：

```bash
# Debug 构建
cmake -S . -B build_debug \
  -G "Ninja" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Release 构建
cmake -S . -B build_release \
  -G "Ninja" \
  -DCMAKE_BUILD_TYPE=Release
```

不指定 `-G` 时会使用默认生成器（通常是 Unix Makefiles）。

常用配置项：

- `CMAKE_BUILD_TYPE`：`Debug` / `Release` / `RelWithDebInfo` /
  `MinSizeRel`；
- `CMAKE_EXPORT_COMPILE_COMMANDS=ON`：生成 `compile_commands.json`，方便
  VSCode/clangd 补全和跳转。

### 3.2 多配置生成器（Windows / Visual Studio / Xcode）

多配置生成器在配置阶段不指定 `CMAKE_BUILD_TYPE`，而是在构建时通过
`--config` 选择。

Windows + Visual Studio 示例：

```powershell
cmake -S . -B build `
  -G "Visual Studio 17 2022" `
  -A x64 `
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

macOS + Xcode 示例：

```bash
cmake -S . -B build -G "Xcode"
```

---

## 4. 编译项目（Build）

### 4.1 单配置生成器（Ninja / Make）

```bash
# Debug
cmake --build build_debug -j

# Release
cmake --build build_release -j
```

`-j` 传给底层构建工具，表示并行编译。

### 4.2 多配置生成器（Visual Studio / Xcode）

```bash
# Debug
cmake --build build --config Debug -- /m

# Release
cmake --build build --config Release -- /m
```

`/m` 为 MSBuild 的并行编译选项。

也可以在 Visual Studio 中直接打开 `build/xxx.sln`，选择配置后 F5 调试。

---

## 5. 运行与调试

### 5.1 生成 Debug 版本

配置阶段确保使用 Debug 或带符号信息的构建：

- 单配置：
  ```bash
  cmake -S . -B build_debug -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
  cmake --build build_debug -j
  ```
- 多配置：
  ```bash
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64
  cmake --build build --config Debug -- /m
  ```

### 5.2 命令行调试（macOS/Linux）

假设可执行文件输出到 `build_debug/sfd_tool`（以实际 CMakeLists.txt
为准）：

- 使用 lldb（macOS 默认）：
  ```bash
  lldb build_debug/sfd_tool
  (lldb) breakpoint set --name main
  (lldb) run
  (lldb) bt
  ```
- 使用 gdb（Linux 常用）：
  ```bash
  gdb build_debug/sfd_tool
  (gdb) break main
  (gdb) run
  (gdb) bt
  ```

### 5.3 Windows 调试

1. Visual Studio：
   - 双击 `build/your_project.sln` 打开；
   - 设置启动项目，选择 `Debug` 配置；
   - 按 F5 开始调试。
2. VSCode + CMake：
   - 安装 CMake Tools + C/C++ 插件；
   - 让 CMake Tools 识别 CMakeLists.txt；
   - 选择 kit / 配置，执行 `cmake --build` 后，在 `launch.json`
     中使用 `build_debug/sfd_tool` 作为调试目标。

---

## 6. 测试（CTest）

前提：项目已在 CMake 中启用测试（`enable_testing()`）并添加测试用例。

### 6.1 运行全部测试

- macOS/Linux：
  ```bash
  cmake -S . -B build_debug -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
  cmake --build build_debug -j

  cd build_debug
  ctest --output-on-failure
  ```

- Windows：
  ```powershell
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64
  cmake --build build --config Debug -- /m

  cd build
  ctest -C Debug --output-on-failure
  ```

常用选项：

- `--output-on-failure`：测试失败时打印输出；
- `-C Debug`：多配置生成器指定测试使用的配置。

### 6.2 按名称筛选 / 提高日志

```bash
# 只跑名称包含 "MyTest" 的测试
ctest -R MyTest --output-on-failure

# 输出更详细日志
ctest -VV
```

---

## 7. 安装（`cmake --install`）

前提：在 `CMakeLists.txt` 中定义了安装规则，例如：

```cmake
install(TARGETS sfd_tool RUNTIME DESTINATION bin)
install(FILES config/default.conf DESTINATION share/sfd_tool)
```

### 7.1 安装到自定义前缀

- 单配置（macOS/Linux）：
  ```bash
  cmake -S . -B build_release -G "Ninja" -DCMAKE_BUILD_TYPE=Release
  cmake --build build_release -j

  cmake --install build_release --prefix /opt/sfd_tool
  ```

- 多配置（Windows）：
  ```powershell
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64
  cmake --build build --config Release -- /m

  cmake --install build --config Release --prefix "C:/Program Files/sfd_tool"
  ```

安装后，可执行文件通常位于：

- macOS/Linux：`/opt/sfd_tool/bin/sfd_tool`；
- Windows：`C:\\Program Files\\sfd_tool\\bin\\sfd_tool.exe`（视安装规则而定）。

---

## 8. 打包

项目的正式打包流程主要依赖 `packaging/` 目录下的脚本（如
`build-deb.sh` / `build-rpm.sh`）以及 GitHub Actions CI，详见：

- [packaging/build-deb.sh](../packaging/build-deb.sh)
- [packaging/build-rpm.sh](../packaging/build-rpm.sh)
- [packaging/rpm-build/sfd-tool.spec](../packaging/rpm-build/sfd-tool.spec)
- CI 工作流：[.github/workflows/build.yml](../.github/workflows/build.yml)
- 文档：[docs/RELEASE_GUIDE_ZH.md](RELEASE_GUIDE_ZH.md)

本文件只给出本地实验性打包的示例，实际发行版包以脚本和 CI 的输出为准。

---

## 9. 推荐日常命令（开发 / 发布）

为避免记忆复杂的 CMake 命令，项目提供了若干脚本（位于
[scripts/](../scripts/) 目录），类似 JS 项目中的 `npm run dev`、
`npm run build`：

### 9.1 开发调试（Debug）—— `scripts/dev.sh`

```bash
./scripts/dev.sh
```

大致等价于：

```bash
cmake -S . -B build_cmake_debug -G "Ninja" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build_cmake_debug -j
./build_cmake_debug/sfd_tool
```

`dev.sh` 会：

- 优先使用 Ninja，无则退回 Unix Makefiles；
- 使用 `build_cmake_debug/` 作为 Debug 构建目录；
- 构建完成后直接运行 Debug 版 GUI，GUI 会从 per-user 配置中读取
  `ui_language` 等字段。

### 9.2 本地 Release 构建—— `scripts/release.sh`（或平台专用脚本）

```bash
./scripts/release.sh
```

脚本通常会配置一个 Release 构建目录（如 `build_cmake_release/`），完成
编译并运行简单的检查。正式打包仍应依赖 `packaging/` 脚本和 CI 流程。

---

## 10. per-user 配置与界面语言（ui_language）

SFD Tool 在每个用户下维护独立的 JSON 配置文件，保存界面语言、最近
路径以及部分高级设置。典型路径为：

- Linux：`$XDG_CONFIG_HOME/sfd_tool/` 或 `~/.config/sfd_tool/`；
- macOS：`$HOME/Library/Application Support/sfd_tool/`；
- Windows：`%APPDATA%\\sfd_tool\\`。

路径由 `core/config_service` 中的逻辑统一决定。

重要字段：

- `"ui_language": "auto"`：跟随系统语言；
- `"ui_language": "zh_CN"`：中文界面；
- `"ui_language": "en_US"`：英文界面。

旧版本曾在程序所在目录写入 `sfd_tool_config.json`；新版首次启动时，如
检测到 per-user 目录无配置而当前目录存在旧配置，会尝试迁移，并保留
旧文件作为备份。

你也可以在 GUI 的「高级设置」页中修改语言，修改会写回配置文件。

---

## 11. Makefile 兼容说明

根目录的 `Makefile` 目前仅作为 CMake 的简易封装：

- `make` / `make all`：
  - 触发一个 Release 风格的 CMake 构建；
- `make debug`：
  - 触发一个 Debug 风格的 CMake 构建。

需要注意：

- 早期版本中 Makefile 直接调用编译器，不会生成某些 CMake 中间文件
  （如 `version.h`），这条路径已废弃；
- 如需启用/禁用测试、调节编译选项等，请优先使用显式的
  `cmake -S . -B ...` 命令或上述脚本，而不是修改 Makefile。

因此，应将 Makefile 视为“兼容层/便捷入口”，而非构建配置的权威来源。
