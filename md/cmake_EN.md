CMake Usage Guide

The following is a CMake usage guide for the current project, covering macOS / Linux / Windows, from installation, configuration, debugging, testing to packaging. All commands assume execution in the project root directory (the directory containing CMakeLists.txt), using build/ as the build directory.

---

1. Installing CMake

1.1 macOS

1. Using Homebrew (recommended)
   ```bash
   brew install cmake
   ```
2. Or download the .dmg graphical installer from the official website:
   https://cmake.org/download/

Verify the installation:

```bash
cmake --version
```

1.2 Linux (using Debian/Ubuntu as example)

Common distributions:

· Debian/Ubuntu:
  ```bash
  sudo apt-get update
  sudo apt-get install cmake
  ```
· Fedora:
  ```bash
  sudo dnf install cmake
  ```
· CentOS/RHEL:
  ```bash
  sudo yum install cmake
  ```
· Arch:
  ```bash
  sudo pacman -S cmake
  ```

If the system repository version is too old, you can install a binary or build from source from the official website.

1.3 Windows

1. Use the official installer (recommended)
   · Download: https://cmake.org/download/
   · Choose the Windows x64 Installer, and during installation, check "Add CMake to system PATH".
2. Or use a package manager:
   · PowerShell (Windows 11/10):
     ```powershell
     winget install Kitware.CMake
     ```
   · Chocolatey:
     ```powershell
     choco install cmake
     ```

After installation, verify in the "x64 Native Tools Command Prompt for VS" or PowerShell:

```powershell
cmake --version
```

---

2. Basic Usage Model (General)

The basic CMake workflow consists of three steps:

1. Configure: Generate the build system (Makefiles, Ninja, Visual Studio solutions, etc.) from the source code.
2. Build: Invoke the underlying build tool to compile.
3. Test / Install / Package (CTest / install / CPack)

Always use out-of-source builds:

```bash
cmake -S . -B build [additional options]
cmake --build build [additional options]
```

---

3. Generating the Build Directory (Configure)

3.1 Single-Config Generators (Common on macOS/Linux)

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

If you don't specify -G, the default generator will be used (usually Unix Makefiles).

Explanation:

· CMAKE_BUILD_TYPE: Debug / Release / RelWithDebInfo / MinSizeRel
· CMAKE_EXPORT_COMPILE_COMMANDS=ON: Generates compile_commands.json, useful for code completion and navigation in VSCode/clangd, etc.

3.2 Multi-Config Generators (Windows / Visual Studio / Xcode)

The configuration stage for multi-config generators does not specify CMAKE_BUILD_TYPE; instead, you choose the configuration at build time using --config.

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

4. Building the Project (Build)

4.1 Single-Config Generators (Ninja / Make)

```bash
# Debug
cmake --build build -j

# Release
cmake --build build-release -j
```

Explanation:

· -j: Passed to the underlying build tool to enable parallel compilation (supported by both Ninja and Make).

4.2 Multi-Config Generators (Visual Studio / Xcode)

```bash
# Debug
cmake --build build --config Debug -- /m

# Release
cmake --build build --config Release -- /m
```

· /m: MSBuild's parallel compilation option.

You can also directly open the build/xxx.sln file in Visual Studio and select the configuration and start debugging from the IDE.

---

5. Running and Debugging

5.1 Generating a Debug Version

Ensure the configuration stage uses Debug or a build with symbolic information:

· Single-config:
  ```bash
  cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
  cmake --build build -j
  ```
· Multi-config:
  ```bash
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64
  cmake --build build --config Debug -- /m
  ```

5.2 Command Line Debugging (macOS/Linux)

Assuming the executable is output to build/bin/your_app (actual location depends on your CMakeLists.txt):

· Using lldb (default on macOS):
  ```bash
  lldb build/bin/your_app
  # Inside lldb:
  (lldb) breakpoint set --name main
  (lldb) run
  (lldb) bt
  ```
· Using gdb (common on Linux):
  ```bash
  gdb build/bin/your_app
  # Inside gdb:
  (gdb) break main
  (gdb) run
  (gdb) bt
  ```

5.3 Windows Debugging

Two common methods:

1. Visual Studio:
   · Double-click to open build/your_project.sln
   · Right-click to set the startup project
   · Select the Debug configuration and press F5 to debug.
2. VSCode + CMake:
   · Install the CMake Tools + C/C++ extensions
   · Let CMake Tools recognize CMakeLists.txt
   · Choose the kit / configuration, then cmake --build, configure launch.json to use build/.../your_app.exe for debugging.

---

6. Testing (CTest)

Prerequisite: The project contains configuration similar to this example:

```cmake
enable_testing()

add_executable(my_test tests/my_test.cpp)
add_test(NAME MyTest COMMAND my_test)
```

6.1 Running All Tests

· macOS/Linux:
  ```bash
  cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
  cmake --build build -j
  
  # Run tests in the build directory
  cd build
  ctest --output-on-failure
  ```
· Windows:
  ```powershell
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64
  cmake --build build --config Debug -- /m
  
  cd build
  ctest -C Debug --output-on-failure
  ```

Explanation:

· --output-on-failure: Prints test program output on failure, making it easier to diagnose issues.
· -C Debug: Required for multi-config generators to specify the configuration for testing.

6.2 Filtering by Name / Increasing Verbosity

```bash
# Run only tests matching the name "MyTest"
ctest -R MyTest --output-on-failure

# Output more detailed logs
ctest -VV
```

---

7. Installing (install)

Prerequisite: Installation rules are defined in the CMakeLists.txt (example):

```cmake
install(TARGETS my_tool RUNTIME DESTINATION bin)
install(FILES config/default.conf DESTINATION share/my_tool)
```

7.1 Installing to a Custom Prefix

· Single-config (macOS/Linux):
  ```bash
  cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
  cmake --build build -j
  
  cmake --install build --prefix /opt/my_tool
  ```
· Multi-config (Windows):
  ```powershell
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64
  cmake --build build --config Release -- /m
  
  cmake --install build --config Release --prefix "C:/Program Files/my_tool"
  ```

After installation, the executable will typically be located at:

· macOS/Linux: /opt/my_tool/bin/my_tool
· Windows: C:\\Program Files\\my_tool\\bin\\my_tool.exe (depending on your install rules)

---

8. Packaging (CPack)

If CPack is enabled in your CMakeLists.txt, it will typically have similar configuration:

```cmake
set(CPACK_PACKAGE_NAME "my_tool")
set(CPACK_PACKAGE_VERSION "1.0.0")
# Other CPACK_* settings...
include(CPack)
```

8.1 Basic Workflow

1. First, complete the build and configure the installation rules (see previous section).
2. Invoke CPack from the build directory.

· macOS/Linux:
  ```bash
  cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
  cmake --build build -j
  
  cd build
  # Package using default configuration
  cpack
  ```
· Windows:
  ```powershell
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64
  cmake --build build --config Release -- /m
  
  cd build
  cpack -C Release
  ```

8.2 Specifying Package Formats

Common generators:

· General: TGZ, ZIP
· macOS: DragNDrop (DMG), Bundle
· Linux: DEB, RPM
· Windows: NSIS, WIX (MSI)

Examples:

```bash
# Generate a .tar.gz package
cpack -G TGZ

# Generate a .zip package
cpack -G ZIP

# Generate an NSIS installer on Windows (requires NSIS installed)
cpack -G NSIS -C Release
```

The generated packages will be placed in the build/ directory (e.g., my_tool-1.0.0-Linux.tar.gz, my_tool-1.0.0-win64.exe).

---

10. Locale and Chinese Interface

This project uses gettext for multi-language support. The relevant initialization code is in main.cpp:

```cpp
setlocale(LC_ALL, "");
bindtextdomain("my_tool", "./locale");
textdomain("my_tool");
bind_textdomain_codeset("my_tool", "UTF-8");
```

The Chinese translation file is locale/zh_CN/LC_MESSAGES/my_tool.mo, which is automatically generated by CMake during the build process into build_cmake/locale/zh_CN/LC_MESSAGES/. At runtime, it is loaded via the ./locale path specified in bindtextdomain.

Tested on macOS:

· Running directly: Interface is in English
  ```bash
  ./build_cmake/my_tool
  ```
· Setting only LANG: Still in English
  ```bash
  LANG=zh_CN ./build_cmake/my_tool
  LANG=zh_CN.UTF-8 ./build_cmake/my_tool
  ```
· Setting LC_ALL to Chinese: Interface switches to Chinese (recommended)
  ```bash
  LC_ALL=zh_CN.UTF-8 ./build_cmake/my_tool
  ```

Therefore, on macOS, if you want to temporarily start the program with the Chinese interface, the recommended command is:

```bash
LC_ALL=zh_CN.UTF-8 ./build_cmake/my_tool
```

If you prefer to use Chinese by default in your current shell, you can also add the following to your ~/.zshrc:

```bash
export LC_ALL=zh_CN.UTF-8
```

Then, reopen your terminal and run the program; it will always start with the Chinese interface.