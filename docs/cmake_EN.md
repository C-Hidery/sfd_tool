# CMake Usage Guide

This document is a CMake usage guide for **sfd_tool**, covering macOS / Linux / Windows from installation, configuration, debugging, testing to packaging. All commands are assumed to be executed in the project root directory (the directory containing `CMakeLists.txt`), and use `build/` (or dedicated sub‑directories) as the build directory.

---

## 1. Installing CMake

### 1.1 macOS

1. Using Homebrew (recommended)
   ```bash
   brew install cmake
   ```
2. Or download the `.dmg` graphical installer from the official website:
   https://cmake.org/download/

After installation, verify:

```bash
cmake --version
```

### 1.2 Linux (Debian/Ubuntu as example)

Common distributions:

- Debian/Ubuntu:
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

If the system repository version is too old, you can install a binary or build from source from the official website.

### 1.3 Windows

1. Use the official installer (recommended)
   - Download: https://cmake.org/download/
   - Choose the Windows x64 Installer, and during installation, check "Add CMake to system PATH".

2. Or use a package manager:
   - PowerShell (Windows 11/10):
     ```powershell
     winget install Kitware.CMake
     ```
   - Chocolatey:
     ```powershell
     choco install cmake
     ```

After installation, check in "x64 Native Tools Command Prompt for VS" or PowerShell:

```powershell
cmake --version
```

---

## 2. Basic Usage Model (General)

The basic CMake workflow consists of three steps:

1. **Configure**: Generate the build system (Makefiles, Ninja, Visual Studio solutions, etc.) from the source code.
2. **Build**: Invoke the underlying build tool to compile.
3. **Test / Install / Package**: Run tests with CTest, install with `cmake --install`, and create packages via CPack.

Always use out‑of‑source builds:

```bash
cmake -S . -B build [additional options]
cmake --build build [additional options]
```

---

## 3. Generating the Build Directory (Configure)

### 3.1 Single‑config Generators (common on macOS/Linux)

For example, using Ninja (recommended) or Unix Makefiles:

```bash
# Debug build
cmake -S . -B build \
  -G "Ninja" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Release build
cmake -S . -B build-release \
  -G "Ninja" \
  -DCMAKE_BUILD_TYPE=Release
```

If you do not specify `-G`, the default generator will be used (usually Unix Makefiles).

Explanation:

- `CMAKE_BUILD_TYPE`: `Debug` / `Release` / `RelWithDebInfo` / `MinSizeRel`
- `CMAKE_EXPORT_COMPILE_COMMANDS=ON`: generates `compile_commands.json`, useful for code completion and navigation in VSCode/clangd, etc.

### 3.2 Multi‑config Generators (Windows / Visual Studio / Xcode)

Multi‑config generators do not specify `CMAKE_BUILD_TYPE` at configure time; the configuration is chosen at build time using `--config`.

Example for Windows + Visual Studio:

```powershell
cmake -S . -B build `
  -G "Visual Studio 17 2022" `
  -A x64 `
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

Example for macOS + Xcode:

```bash
cmake -S . -B build -G "Xcode"
```

---

## 4. Building the Project (Build)

### 4.1 Single‑config generators (Ninja / Make)

```bash
# Debug
cmake --build build -j

# Release
cmake --build build-release -j
```

Explanation:

- `-j`: passed to the underlying build tool to enable parallel compilation (supported by both Ninja and Make).

### 4.2 Multi‑config generators (Visual Studio / Xcode)

```bash
# Debug
cmake --build build --config Debug -- /m

# Release
cmake --build build --config Release -- /m
```

- `/m`: MSBuild's parallel compilation option.

You can also directly open the generated `build/xxx.sln` file in Visual Studio and select configuration and start debugging from the IDE.

---

## 5. Running and Debugging

### 5.1 Generating a Debug build

Ensure the configure stage uses `Debug` or a build with symbols:

- Single‑config:
  ```bash
  cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
  cmake --build build -j
  ```
- Multi‑config:
  ```bash
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64
  cmake --build build --config Debug -- /m
  ```

### 5.2 Command‑line debugging (macOS/Linux)

Assuming the executable is output to `build/bin/your_app` (actual location depends on your `CMakeLists.txt`):

- Using **lldb** (default on macOS):
  ```bash
  lldb build/bin/your_app
  # inside lldb:
  (lldb) breakpoint set --name main
  (lldb) run
  (lldb) bt
  ```
- Using **gdb** (common on Linux):
  ```bash
  gdb build/bin/your_app
  # inside gdb:
  (gdb) break main
  (gdb) run
  (gdb) bt
  ```

### 5.3 Windows debugging

Two common methods:

1. **Visual Studio**:
   - Double‑click `build/your_project.sln` to open it.
   - Right‑click to set the startup project.
   - Select the `Debug` configuration and press F5 to start debugging.

2. **VSCode + CMake**:
   - Install the "CMake Tools" and "C/C++" extensions.
   - Let CMake Tools discover `CMakeLists.txt`.
   - Choose the kit / configuration, then run `cmake --build`, and configure `launch.json` to use `build/.../your_app.exe` for debugging.

---

## 6. Testing (CTest)

Prerequisite: the project has testing configuration similar to:

```cmake
enable_testing()

add_executable(my_test tests/my_test.cpp)
add_test(NAME MyTest COMMAND my_test)
```

### 6.1 Running all tests

- macOS/Linux:
  ```bash
  cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
  cmake --build build -j

  # Run tests in the build directory
  cd build
  ctest --output-on-failure
  ```

- Windows:
  ```powershell
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64
  cmake --build build --config Debug -- /m

  cd build
  ctest -C Debug --output-on-failure
  ```

Explanation:

- `--output-on-failure`: print test program output when a test fails, making it easier to diagnose issues.
- `-C Debug`: required for multi‑config generators to specify the configuration for testing.

### 6.2 Filtering by name / increasing verbosity

```bash
# Run only tests whose names match "MyTest"
ctest -R MyTest --output-on-failure

# Output more detailed logs
ctest -VV
```

---

## 7. Installing (`cmake --install`)

Prerequisite: installation rules are defined in `CMakeLists.txt`, for example:

```cmake
install(TARGETS sfd_tool RUNTIME DESTINATION bin)
install(FILES config/default.conf DESTINATION share/sfd_tool)
```

### 7.1 Installing to a custom prefix

- Single‑config (macOS/Linux):
  ```bash
  cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
  cmake --build build -j

  cmake --install build --prefix /opt/sfd_tool
  ```

- Multi‑config (Windows):
  ```powershell
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64
  cmake --build build --config Release -- /m

  cmake --install build --config Release --prefix "C:/Program Files/sfd_tool"
  ```

After installation, the executable will typically be located at:

- macOS/Linux: `/opt/sfd_tool/bin/sfd_tool`
- Windows: `C:\\Program Files\\sfd_tool\\bin\\sfd_tool.exe` (depending on your install rules)

---

## 8. Packaging (CPack)

If CPack is enabled in your `CMakeLists.txt`, you will usually see configuration similar to:

```cmake
set(CPACK_PACKAGE_NAME "sfd_tool")
set(CPACK_PACKAGE_VERSION "1.0.0")
# Other CPACK_* settings...
include(CPack)
```

### 8.1 Basic workflow

1. First, complete the build and ensure installation rules are configured (see previous section).
2. Invoke CPack from the build directory.

- macOS/Linux:
  ```bash
  cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
  cmake --build build -j

  cd build
  # Package using default configuration
  cpack
  ```

- Windows:
  ```powershell
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64
  cmake --build build --config Release -- /m

  cd build
  cpack -C Release
  ```

### 8.2 Choosing package formats

Common generators:

- General: `TGZ`, `ZIP`
- macOS: `DragNDrop` (DMG), `Bundle`
- Linux: `DEB`, `RPM`
- Windows: `NSIS`, `WIX` (MSI)

Examples:

```bash
# Generate a .tar.gz package
cpack -G TGZ

# Generate a .zip package
cpack -G ZIP

# Generate an NSIS installer on Windows (requires NSIS installed)
cpack -G NSIS -C Release
```

Generated packages will be placed in the `build/` directory (for example `sfd_tool-1.0.0-Linux.tar.gz`, `sfd_tool-1.0.0-win64.exe`).

---

## 9. Recommended day‑to‑day commands for this project (dev / release)

To avoid remembering different CMake command lines, this project provides helper scripts in the [scripts/](../scripts/) directory, similar to `pnpm dev` / `pnpm build` in JS projects.

### 9.1 Development (Debug)

```bash
# Run in the project root
./scripts/dev.sh
```

This is equivalent to:

```bash
# 1. Generate/update the Debug build directory (prefer Ninja)
cmake -S . -B build_cmake_debug -G "Ninja" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# 2. Build the Debug configuration
cmake --build build_cmake_debug -j

# 3. Run the Debug GUI (UI language is determined by the per‑user config file field `ui_language`)
./build_cmake_debug/sfd_tool
```

`dev.sh` will automatically:

- Detect whether `ninja` is installed; use `Ninja` if available, otherwise fall back to `Unix Makefiles`.
- Use `build_cmake_debug/` as the Debug build directory.
- Always build in Debug mode.
- Run `./build_cmake_debug/sfd_tool` directly. The program reads `ui_language` from the per‑user config file and by default uses Chinese (on first run it writes a default config).

### 9.2 Release build

```bash
# Run in the project root
./scripts/release.sh
```

Equivalent to:

```bash
# 1. Generate/update the Release build directory (prefer Ninja)
cmake -S . -B build_cmake -G "Ninja" -DCMAKE_BUILD_TYPE=Release

# 2. Build the Release configuration
cmake --build build_cmake -j

# 3. To run (UI language determined by config file):
./build_cmake/sfd_tool
```

### 9.3 Windows scripts (PowerShell)

On Windows, the recommended workflow is **Visual Studio 2022 + CMake**, wrapped by PowerShell scripts:

- Development (Debug):
  ```powershell
  # Run in the project root
  .\scripts\dev.ps1
  ```

- Release build:
  ```powershell
  # Run in the project root
  .\scripts\release.ps1
  ```

These scripts do the following:

- Use generator `Visual Studio 17 2022` and platform `x64` by default.
- Use `build_vs/` as the VS build directory.
- `dev.ps1`:
  - Runs:
    ```powershell
    cmake -S . -B build_vs -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    cmake --build build_vs --config Debug -- /m
    ```
  - If `build_vs/Debug/sfd_tool.exe` is generated, it will automatically start the GUI.
- `release.ps1`:
  - Runs:
    ```powershell
    cmake -S . -B build_vs -G "Visual Studio 17 2022" -A x64
    cmake --build build_vs --config Release -- /m
    ```
  - Does not automatically run the program, only prints the path to `build_vs/Release/sfd_tool.exe`.

Dependency checks and hints:

- If `cmake` is not found in PowerShell:
  - Suggest installing Visual Studio 2022 and enabling the "Desktop development with C++" workload; or
  - Install CMake via `winget install Kitware.CMake`.
- Recommended environments to run the scripts so that the VS toolchain is available:
  - PowerShell launched from `x64 Native Tools Command Prompt for VS 2022`.
  - Visual Studio's built‑in "Developer PowerShell".

> In short:
> - macOS / Linux: use `./scripts/dev.sh`, `./scripts/release.sh`.
> - Windows (VS 2022 + PowerShell): use `./scripts/dev.ps1`, `./scripts/release.ps1`.

---

## 10. UI language and per‑user configuration

`sfd_tool` uses **gettext** for multi‑language support. At startup, the program reads the `ui_language` field from a per‑user configuration file to decide the UI language:

- `"zh_CN"`: force Simplified Chinese UI (default).
- `"en_US"`: force English UI.
- `"auto"` or empty: follow the system / terminal locale.

On first run, if the config file does not exist, the program writes a default config where `ui_language` is `"zh_CN"`. This means that simply running:

```bash
./build_cmake/sfd_tool
```

will normally show the Chinese UI.

There are two ways to switch UI language:

1. In the GUI, open the **"Advanced Settings"** page, find the **"UI language"** dropdown, select the desired language and click the **"Apply"** button next to it. Restart the program to take effect.
2. Or manually edit the per‑user config file (paths may differ:
   - Linux: `$XDG_CONFIG_HOME/sfd_tool/sfd_tool_config.json` or `~/.config/sfd_tool/sfd_tool_config.json`
   - macOS: `$HOME/Library/Application Support/sfd_tool/sfd_tool_config.json`
   - Windows: `%APPDATA%\sfd_tool\sfd_tool_config.json`

   Set `ui_language` to one of the values above and restart `sfd_tool`.

Previous versions of the documentation suggested using environment variables such as `LC_ALL=zh_CN.UTF-8` to force a Chinese interface. This is **no longer recommended**. In normal cases you can just run the binary directly and control the UI language via the per‑user configuration file.
