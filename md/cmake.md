# CMake 使用指南

下面是一份面向当前项目的 CMake 使用指南，覆盖 macOS / Linux / Windows，从安装、配置、调试、测试到打包。所有命令假定在项目根目录（有 CMakeLists.txt 的目录）执行，并使用 `build/` 作为构建目录。

---

## 1. CMake 安装

### 1.1 macOS

1. 使用 Homebrew（推荐）
   ```bash
   brew install cmake
   ```
2. 或官网下载 `.dmg` 图形安装包：
   https://cmake.org/download/

安装后检查：
```bash
cmake --version
```

### 1.2 Linux（Debian/Ubuntu 为例）

常见发行版：

- Debian/Ubuntu：
  ```bash
  sudo apt-get update
  sudo apt-get install cmake
  ```
- Fedora:
  ```bash
  sudo dnf install cmake
  ```
- CentOS/RHEL:
  ```bash
  sudo yum install cmake
  ```
- Arch:
  ```bash
  sudo pacman -S cmake
  ```

若系统源版本过旧，可从官网安装二进制或源码构建。

### 1.3 Windows

1. 使用官方安装程序（推荐）
   - 下载：https://cmake.org/download/
   - 选择 Windows x64 Installer，双击安装时勾选 “Add CMake to system PATH”。

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

## 2. 基本使用模型（通用）

CMake 的基本流程是三步：

1. **配置（Configure）**：从源码生成构建系统（Makefile、Ninja、Visual Studio 解决方案等）
2. **构建（Build）**：调用底层构建工具编译
3. **测试 / 安装 / 打包（CTest / install / CPack）

统一使用 out-of-source 构建：

```bash
cmake -S . -B build [额外选项]
cmake --build build [额外选项]
```

---

## 3. 生成构建目录（Configure）

### 3.1 单配置生成器（macOS/Linux 常用）

例如使用 Ninja（推荐）或 Unix Makefiles：

```bash
# Debug 构建
cmake -S . -B build \
  -G "Ninja" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Release 构建
cmake -S . -B build-release \
  -G "Ninja" \
  -DCMAKE_BUILD_TYPE=Release
```

不指定 `-G` 时会用默认生成器（通常是 Unix Makefiles）。

说明：

- `CMAKE_BUILD_TYPE`：`Debug` / `Release` / `RelWithDebInfo` / `MinSizeRel`
- `CMAKE_EXPORT_COMPILE_COMMANDS=ON`：生成 `compile_commands.json`，方便 VSCode/clangd 等做补全和跳转。

### 3.2 多配置生成器（Windows / Visual Studio / Xcode）

多配置生成器的配置阶段不指定 `CMAKE_BUILD_TYPE`，而是在构建时通过 `--config` 选择。

Windows + Visual Studio 例如：

```powershell
cmake -S . -B build `
  -G "Visual Studio 17 2022" `
  -A x64 `
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

macOS + Xcode 例如：

```bash
cmake -S . -B build -G "Xcode"
```

---

## 4. 编译项目（Build）

### 4.1 单配置生成器（Ninja / Make）

```bash
# Debug
cmake --build build -j

# Release
cmake --build build-release -j
```

说明：

- `-j`：传给底层构建工具的参数，表示并行编译（Ninja/Make 都支持）。

### 4.2 多配置生成器（Visual Studio / Xcode）

```bash
# Debug
cmake --build build --config Debug -- /m

# Release
cmake --build build --config Release -- /m
```

- `/m`：VS/MSBuild 的并行编译选项。

你也可以直接在 Visual Studio 打开 `build/xxx.sln`，在 IDE 中选择配置和启动调试。

---

## 5. 运行与调试

### 5.1 生成 Debug 版本

配置阶段保证使用 Debug 或带符号信息的构建：

- 单配置：
  ```bash
  cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
  cmake --build build -j
  ```
- 多配置：
  ```bash
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64
  cmake --build build --config Debug -- /m
  ```

### 5.2 命令行调试（macOS/Linux）

假设可执行文件输出到 `build/bin/your_app`（以实际 CMakeLists.txt 为准）：

- 使用 `lldb`（macOS 默认）：
  ```bash
  lldb build/bin/your_app
  # 在 lldb 中：
  (lldb) breakpoint set --name main
  (lldb) run
  (lldb) bt
  ```
- 使用 `gdb`（Linux 常用）：
  ```bash
  gdb build/bin/your_app
  # gdb 内：
  (gdb) break main
  (gdb) run
  (gdb) bt
  ```

### 5.3 Windows 调试

两种常见方式：

1. Visual Studio：
   - 双击打开 `build/your_project.sln`
   - 右键设置启动项目
   - 选择 `Debug` 配置，按 F5 调试。

2. VSCode + CMake：
   - 安装 CMake Tools + C/C++ 插件
   - 让 CMake Tools 识别 CMakeLists.txt
   - 选择 kit / 配置，然后 `cmake --build`，配置 `launch.json` 使用 `build/.../your_app.exe` 调试。

---

## 6. 测试（CTest）

前提：项目中已有类似配置（仅示例）：

```cmake
enable_testing()

add_executable(my_test tests/my_test.cpp)
add_test(NAME MyTest COMMAND my_test)
```

### 6.1 运行全部测试

- macOS/Linux：
  ```bash
  cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
  cmake --build build -j

  # 在 build 目录中运行测试
  cd build
  ctest --output-on-failure
  ```

- Windows：
  ```powershell
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64
  cmake --build build --config Debug -- /m

  cd build
  ctest -C Debug --output-on-failure
  ```

说明：

- `--output-on-failure`：失败时打印测试程序输出，便于定位问题。
- `-C Debug`：多配置生成器需要指定测试使用的配置。

### 6.2 按名称筛选 / 提高日志

```bash
# 只跑名称匹配 "MyTest" 的测试
ctest -R MyTest --output-on-failure

# 输出更详细日志
ctest -VV
```

---

## 7. 安装（install）

前提：CMake 中定义了安装规则（示例）：

```cmake
install(TARGETS sfd_tool RUNTIME DESTINATION bin)
install(FILES config/default.conf DESTINATION share/sfd_tool)
```

### 7.1 安装到自定义前缀

- 单配置（macOS/Linux）：
  ```bash
  cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
  cmake --build build -j

  cmake --install build --prefix /opt/sfd_tool
  ```

- 多配置（Windows）：
  ```powershell
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64
  cmake --build build --config Release -- /m

  cmake --install build --config Release --prefix "C:/Program Files/sfd_tool"
  ```

安装之后，可执行文件通常会出现在：

- macOS/Linux：`/opt/sfd_tool/bin/sfd_tool`
- Windows：`C:\\Program Files\\sfd_tool\\bin\\sfd_tool.exe`（取决于你的安装规则）

---

## 8. 打包（CPack）

如果在 CMakeLists.txt 中启用了 CPack，通常会有类似配置：

```cmake
set(CPACK_PACKAGE_NAME "sfd_tool")
set(CPACK_PACKAGE_VERSION "1.0.0")
# 其他 CPACK_* 设置...
include(CPack)
```

### 8.1 基本使用流程

1. 先完成构建与安装规则配置（见上一节）。
2. 在构建目录中调用 CPack。

- macOS/Linux：
  ```bash
  cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
  cmake --build build -j

  cd build
  # 按默认配置打包
  cpack
  ```

- Windows：
  ```powershell
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64
  cmake --build build --config Release -- /m

  cd build
  cpack -C Release
  ```

### 8.2 指定打包格式

常见生成器：

- 通用：`TGZ`、`ZIP`
- macOS：`DragNDrop`（DMG）、`Bundle`
- Linux：`DEB`、`RPM`
- Windows：`NSIS`、`WIX`（MSI）

示例：

```bash
# 生成 .tar.gz 包
cpack -G TGZ

# 生成 .zip 包
cpack -G ZIP

# Windows 下生成 NSIS 安装器（前提是安装了 NSIS）
cpack -G NSIS -C Release
```

生成的安装包会放在 `build/` 下（例如 `sfd_tool-1.0.0-Linux.tar.gz`、`sfd_tool-1.0.0-win64.exe`）。


## 10. 语言与中文界面

本项目使用 gettext 做多语言支持，相关初始化代码在 `main.cpp` 中：

```cpp
setlocale(LC_ALL, "");
bindtextdomain("sfd_tool", "./locale");
textdomain("sfd_tool");
bind_textdomain_codeset("sfd_tool", "UTF-8");
```

中文翻译文件为 `locale/zh_CN/LC_MESSAGES/sfd_tool.mo`，由 CMake 在构建时自动生成到 `build_cmake/locale/zh_CN/LC_MESSAGES/`，运行时通过 `bindtextdomain` 的 `./locale` 路径加载。

在 macOS 上实测：

- 直接运行：界面为英文
  ```bash
  ./build_cmake/sfd_tool
  ```
- 仅设置 `LANG`：仍为英文
  ```bash
  LANG=zh_CN ./build_cmake/sfd_tool
  LANG=zh_CN.UTF-8 ./build_cmake/sfd_tool
  ```
- 设置 `LC_ALL` 为中文：界面切换为中文（推荐）
  ```bash
  LC_ALL=zh_CN.UTF-8 ./build_cmake/sfd_tool
  ```

因此，在 macOS 上如果希望**临时**以中文界面启动，推荐命令为：

```bash
LC_ALL=zh_CN.UTF-8 ./build_cmake/sfd_tool
```

如果你希望在当前 shell 中默认使用中文，也可以在 `~/.zshrc` 中加入：

```bash
export LC_ALL=zh_CN.UTF-8
```

然后重新打开终端再运行程序即可始终以中文界面启动。
