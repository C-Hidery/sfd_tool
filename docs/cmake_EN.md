# CMake Usage Guide

> Related documents: [README](../README.md) · [Chinese README](../README_ZH.md)
> · [User Guide (ZH)](USER_GUIDE_ZH.md) · [Release Guide (ZH)](RELEASE_GUIDE_ZH.md)
> · [Version Log](VERSION_LOG.md)

This document is a CMake usage guide for **sfd_tool**, covering macOS / Linux /
Windows from installation, configuration, debugging, testing to packaging.
All commands are assumed to be executed in the project root directory (the
directory containing `CMakeLists.txt`), and use `build/`‑style directories as
build outputs.

> CMake is the **primary** build system for this project. The legacy Makefile
> is kept only as a thin wrapper for compatibility and should not be treated
> as the source of truth.

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

If the system repository version is too old, you can install a binary or build
from source from the official website.

### 1.3 Windows

1. Use the official installer (recommended)
   - Download: https://cmake.org/download/
   - Choose the Windows x64 Installer, and during installation, check
     "Add CMake to system PATH".
2. Or use a package manager:
   - PowerShell (Windows 11/10):
     ```powershell
     winget install Kitware.CMake
     ```
   - Chocolatey:
     ```powershell
     choco install cmake
     ```

After installation, check in an "x64 Native Tools Command Prompt for VS" or
PowerShell:

```powershell
cmake --version
```

---

## 2. Basic Usage Model (General)

The basic CMake workflow consists of three steps:

1. **Configure**: Generate the build system (Makefiles, Ninja, Visual Studio
   solutions, etc.) from the source code.
2. **Build**: Invoke the underlying build tool to compile.
3. **Test / Install / Package**: Run tests with CTest, install with
   `cmake --install`, and create packages via CPack or project‑specific
   scripts.

Always use out‑of‑source builds:

```bash
cmake -S . -B build [additional options]
cmake --build build [additional options]
```

Different build directories (e.g. `build_debug`, `build_release`) can be used to
keep configurations separate.

---

## 3. Generating the Build Directory (Configure)

### 3.1 Single‑config Generators (macOS/Linux)

Example using Ninja (recommended) or Unix Makefiles:

```bash
# Debug build
cmake -S . -B build_debug \
  -G "Ninja" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Release build
cmake -S . -B build_release \
  -G "Ninja" \
  -DCMAKE_BUILD_TYPE=Release
```

If you do not specify `-G`, the default generator will be used (usually Unix
Makefiles).

Key options:

- `CMAKE_BUILD_TYPE`: `Debug` / `Release` / `RelWithDebInfo` / `MinSizeRel`
- `CMAKE_EXPORT_COMPILE_COMMANDS=ON`: generates `compile_commands.json`, useful
  for code completion and navigation in VSCode/clangd, etc.

### 3.2 Multi‑config Generators (Windows / Visual Studio / Xcode)

Multi‑config generators do not specify `CMAKE_BUILD_TYPE` at configure time;
configuration is chosen at build time using `--config`.

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
cmake --build build_debug -j

# Release
cmake --build build_release -j
```

`-j` is passed to the underlying build tool to enable parallel compilation.

### 4.2 Multi‑config generators (Visual Studio / Xcode)

```bash
# Debug
cmake --build build --config Debug -- /m

# Release
cmake --build build --config Release -- /m
```

`/m` is MSBuild's parallel compilation option.

You can also directly open the generated `build/xxx.sln` file in Visual Studio
and select configuration/start debugging from the IDE.

---

## 5. Running and Debugging

### 5.1 Generating a Debug build

Ensure the configure stage uses `Debug` or a build with symbols:

- Single‑config:
  ```bash
  cmake -S . -B build_debug -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
  cmake --build build_debug -j
  ```
- Multi‑config:
  ```bash
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64
  cmake --build build --config Debug -- /m
  ```

### 5.2 Command‑line debugging (macOS/Linux)

Assuming the executable is output to `build_debug/sfd_tool` (actual location
depends on your `CMakeLists.txt`):

- Using **lldb** (default on macOS):
  ```bash
  lldb build_debug/sfd_tool
  # inside lldb:
  (lldb) breakpoint set --name main
  (lldb) run
  (lldb) bt
  ```
- Using **gdb** (common on Linux):
  ```bash
  gdb build_debug/sfd_tool
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
   - Choose the kit/configuration, then run `cmake --build`, and configure
     `launch.json` to use `build_debug/sfd_tool` for debugging.

---

## 6. Testing (CTest)

Prerequisite: the project has testing configuration, and
`ENABLE_TESTING`/`enable_testing()` is set in CMake.

### 6.1 Running all tests

- macOS/Linux:
  ```bash
  cmake -S . -B build_debug -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
  cmake --build build_debug -j

  cd build_debug
  ctest --output-on-failure
  ```

- Windows:
  ```powershell
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64
  cmake --build build --config Debug -- /m

  cd build
  ctest -C Debug --output-on-failure
  ```

Useful options:

- `--output-on-failure`: print test output for failed tests.
- `-C Debug`: for multi‑config generators to specify the configuration.

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
  cmake -S . -B build_release -G "Ninja" -DCMAKE_BUILD_TYPE=Release
  cmake --build build_release -j

  cmake --install build_release --prefix /opt/sfd_tool
  ```

- Multi‑config (Windows):
  ```powershell
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64
  cmake --build build --config Release -- /m

  cmake --install build --config Release --prefix "C:/Program Files/sfd_tool"
  ```

After installation, the executable will typically be located at:

- macOS/Linux: `/opt/sfd_tool/bin/sfd_tool`
- Windows: `C:\\Program Files\\sfd_tool\\bin\\sfd_tool.exe` (depending on
  your install rules)

---

## 8. Packaging

This project uses a combination of **CMake/CPack** and
**distribution‑specific scripts** in the `packaging/` directory for
production‑ready packages.

For local experiments you can use CPack directly:

```bash
cd build_release
cpack
```

For official Debian/RPM packages and other artifacts, refer to:

- `packaging/build-deb.sh`
- `packaging/build-rpm.sh`
- `packaging/rpm-build/sfd-tool.spec`

These scripts are orchestrated by the CI workflow
[.github/workflows/build.yml](../.github/workflows/build.yml) and described in
more detail in [docs/RELEASE_GUIDE_ZH.md](RELEASE_GUIDE_ZH.md).

---

## 9. Recommended day‑to‑day commands (dev / release)

To avoid remembering long CMake command lines, this project provides helper
scripts in the [scripts/](../scripts/) directory.

### 9.1 Development (Debug) — `scripts/dev.sh`

```bash
./scripts/dev.sh
```

Roughly equivalent to:

```bash
cmake -S . -B build_cmake_debug -G "Ninja" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build_cmake_debug -j
./build_cmake_debug/sfd_tool
```

`dev.sh` will:

- Prefer Ninja if available, otherwise fall back to Unix Makefiles;
- Use `build_cmake_debug/` as the Debug build directory;
- Run the Debug GUI at the end. The GUI reads the per‑user config file to
  determine `ui_language` and other settings (see below).

### 9.2 Release builds — `scripts/release.sh` (or platform‑specific variants)

For producing release‑like builds (without full packaging):

```bash
./scripts/release.sh
```

The script typically configures a Release build directory (e.g.
`build_cmake_release/`), builds it and may run a minimal smoke test. For
full packaging, prefer the `packaging/` scripts and CI pipelines.

---

## 10. Per‑user configuration and UI language

SFD Tool stores per‑user configuration (including UI language and recent paths)
under a platform‑specific directory, typically:

- Linux: `$XDG_CONFIG_HOME/sfd_tool/` or `~/.config/sfd_tool/`
- macOS: `$HOME/Library/Application Support/sfd_tool/`
- Windows: `%APPDATA%\\sfd_tool\\`

The exact path is determined by `ConfigService` in `core/config_service`.

Key points:

- On first run, if only an old `sfd_tool_config.json` exists in the project
  root / executable directory, the program may migrate it to the per‑user
  location (keeping the old file as a backup);
- The config file is JSON; the `ui_language` field controls the GUI language:
  - `"ui_language": "auto"` — follow system locale;
  - `"ui_language": "zh_CN"` — Simplified Chinese;
  - `"ui_language": "en_US"` — English.

You can also change UI language from the GUI’s **Advanced Settings** page;
changes are persisted back to the per‑user config file.

---

## 11. Legacy Makefile (for compatibility only)

The repository root still contains a `Makefile`, primarily for historical
reasons. It now acts as a thin wrapper around CMake:

- `make` / `make all`:
  - Equivalent to configuring and building a Release‑like build directory
    via CMake.
- `make debug`:
  - Equivalent to a Debug‑like CMake build.

Important notes:

- In older versions the Makefile directly called the compiler and did not
  generate some CMake‑managed intermediate files (such as `version.h`);
  this workflow is **deprecated**;
- For any non‑trivial configuration (turning tests on/off, experimenting
  with options, etc.), **always prefer explicit `cmake -S . -B ...` commands
  or the helper scripts**, not editing the Makefile.

In short, treat the Makefile as a convenience shim, not as the canonical
build description.
