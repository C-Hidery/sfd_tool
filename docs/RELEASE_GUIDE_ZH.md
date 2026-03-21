# sfd_tool 版本发布流程

> 相关文档： [README_ZH](../README_ZH.md) · [操作手册](USER_GUIDE_ZH.md) ·
> [CMake 使用指南](cmake.md) · [版本记录](VERSION_LOG.md)

本文件说明如何在当前 CI/打包体系下发布一个新的 **sfd_tool** 版本，
包括一次性配置 GitHub PAT、更新版本号和版本记录、打 tag 触发 CI 与
自动创建 Release 等步骤。

> 以下示例中以版本号 `1.8.2.2`、Git tag `v1.8.2.2` 为例，请根据实际
> 发布版本替换为你自己的版本号/标签。

---

## 0. （一次性）配置 GitHub 发布令牌（RELEASE_TOKEN）

> 本仓库使用 GitHub PAT（RELEASE_TOKEN）在 CI 中创建 Release，避免
> 默认 `GITHUB_TOKEN` 在部分场景下面对 Releases API 返回
> `403 Resource not accessible by integration` 的问题。

1. 在 GitHub 上创建 PAT（Personal Access Token）：
   1. 打开 <https://github.com/settings/tokens>；
   2. 选择 **Tokens (classic)** → `Generate new token`；
   3. 备注可写为 `sfd_tool release` 等易识别名称；
   4. 在权限（Scopes）中至少勾选：`repo`；
   5. 生成 Token，并妥善保存（**只会显示一次**）。

2. 配置仓库级 Secret：
   1. 打开项目仓库 → `Settings` → `Secrets and variables` → `Actions`；
   2. 点击 `New repository secret`；
   3. Name 填写：`RELEASE_TOKEN`；
   4. Secret 值粘贴刚才生成的 PAT。

3. CI 使用方式：

   在 [`.github/workflows/build.yml`](../.github/workflows/build.yml) 中，
   `release` job 会调用：

   ```yaml
   - name: Create GitHub Release
     uses: softprops/action-gh-release@v2
     with:
       token: ${{ secrets.RELEASE_TOKEN }}
       # ... 其他参数略
   ```

   从而在 tag 推送后自动创建 GitHub Release 并上传构建产物。

---

## 1. 更新版本号（VERSION.txt）

> 推荐做法：使用仓库提供的脚本自动完成版本号与相关文件更新。

### 1.1 使用脚本自动更新（推荐）

在 Linux/macOS 上：

```bash
./scripts/bump_version.sh
```

在 Windows PowerShell 上：

```powershell
.\scriptsump_version.ps1
```

脚本会引导你输入新版本号，并完成：

- 修改根目录 `VERSION.txt`；
- 在 `docs/VERSION_LOG.md` 末尾追加新版本条目；
- 同步 `packaging/rpm-build/sfd-tool.spec` 中的 `Version` 字段；
- 更新 `assets/app.rc` 中的 Windows 资源版本；
- 创建一条形如 `Version: 1.8.2.2` 的 git commit。

如不使用脚本，可按下一小节手动操作。

### 1.2 手动更新

1. 编辑根目录 `VERSION.txt`，将第一行改为新版本号，例如：

   ```text
   1.8.2.2
   ```

2. 重新运行 CMake 配置与编译，确保版本号正确写入生成文件：

   ```bash
   cmake -S . -B build_cmake_debug -DCMAKE_BUILD_TYPE=Debug
   cmake --build build_cmake_debug -j
   ```

---

## 2. 更新版本记录（docs/VERSION_LOG.md）

1. 编辑 [docs/VERSION_LOG.md](VERSION_LOG.md)：
   - 如需更新文件开头展示的版本（About 页展示），可把：
     ```text
     Version 1.8.2.1 LTV Edition
     ```
     修改为：
     ```text
     Version 1.8.2.2 LTV Edition
     ```
     （如该版本属于同一 LTV 系列，也可以保留大版本不变）。
   - 在文件末尾追加一个新的版本块，例如：
     ```markdown
     ---v 1.8.2.2---
     修复：XXX
     优化：YYY
     调整：ZZZ
     ```

2. 运行开发脚本进行本地确认：

   ```bash
   ./scripts/dev.sh
   ```

   - GUI About 页应显示最新版本号；
   - About 页中的版本记录中应能看到刚追加的 `---v 1.8.2.2---` 条目。

---

## 3. 提交更改

若使用脚本自动更新，脚本通常已经完成 commit。若你手动更新了相关文件，
可以按如下方式提交：

```bash
git add VERSION.txt docs/VERSION_LOG.md \
        packaging/rpm-build/sfd-tool.spec assets/app.rc

git commit -m "Version: 1.8.2.2"

git push
```

> 可根据实际修改的文件调整 `git add` 列表。

---

## 4. 打 tag 触发 CI 构建与自动 Release

1. 在本地为当前主分支打上版本 tag：

   ```bash
   git tag v1.8.2.2
   git push origin v1.8.2.2
   ```

2. 推送 tag 后，GitHub Actions 将执行完整 CI：

   - `build-windows`：
     - Windows x64 LibUSB 构建（`artifact_name: LibUSB`）；
     - Windows x64 SPRD 构建（`artifact_name: SPRD`）；
   - `build-macos`：构建 macOS 可执行文件及 DMG；
   - `build-linux-deb`：构建 `.deb` 包；
   - `build-linux-rpm`：构建 `.rpm` 包；
   - 所有构建成功后，触发 `release` job：
     - 下载上述 job 上传的构建产物；
     - 重命名 Windows 目录为：
       - `sfd_tool_windows_x64_libusb`（原 `sfd_tool_LibUSB_Release`）
       - `sfd_tool_windows_x64_sprd`（原 `sfd_tool_SPRD_Release`）
     - 拷贝 `VERSION_LOG.md`、`Channel9.dll` 等必要文件；
     - 压缩为 `${TAG}.zip`（例如 `v1.8.2.2.zip`）；
     - 使用 `RELEASE_TOKEN` 调用 GitHub Releases API 创建 Release 并上传：
       - Windows x64（SPRD/LibUSB）目录；
       - macOS `.dmg`；
       - Linux `.deb` / `.rpm`；
       - 汇总压缩包 `${TAG}.zip`。

如在 `Create GitHub Release` 步骤看到 `403 Resource not accessible by integration` 等错误，请检查：

- 仓库 `Settings → Secrets and variables → Actions` 中是否正确配置了
  `RELEASE_TOKEN`；
- PAT 是否仍然有效、未过期，并保留了 `repo` 权限；
- tag 是否推送到主仓库而不是 fork。

---

## 5. 同步 Windows 可执行文件属性版本（可选）

若希望 Windows 可执行文件的“属性→详细信息”中显示与版本号一致的
信息，需要更新资源文件 [assets/app.rc](../assets/app.rc)：

1. 找到类似以下字段：

   ```rc
   FILEVERSION 1,8,2,1
   PRODUCTVERSION 1,8,2,1
   VALUE "FileVersion", "1.8.2.1"
   VALUE "ProductVersion", "1.8.2.1"
   ```

2. 将其更新为新版本号（例如 1.8.2.2）：

   ```rc
   FILEVERSION 1,8,2,2
   PRODUCTVERSION 1,8,2,2
   VALUE "FileVersion", "1.8.2.2"
   VALUE "ProductVersion", "1.8.2.2"
   ```

3. 重新在 Windows 上使用 CMake/Visual Studio 构建可执行文件。

此步骤只影响资源属性显示，不影响 CI 构建和其它平台版本号。

---

## 6. GitHub Actions CI 构建矩阵概览

当前 CI 工作流文件为
[.github/workflows/build.yml](../.github/workflows/build.yml)，主要包括：

### 6.1 触发条件

- 推送到 `master` 分支的 commit；
- 推送以 `v` 开头的 tag（如 `v1.8.2.2`）：用于正式发布；
- 指向 `master` 的 Pull Request。

### 6.2 各 job 概览

- `build-windows`
  - 运行环境：`windows-latest`；
  - 使用 vcpkg (`C:\vcpkg`) 安装 `gettext` 以及可选的 `libusb`；
  - 使用 MSYS2 (`C:\msys64`) 安装 GTK3 及依赖；
  - 使用 CMake 生成 Visual Studio 2022 工程（`build_cmake`），开启
    `SFD_ENABLE_TESTS=ON`；
  - 构建并运行 `ctest`；
  - 复制 `sfd_tool.exe`、GTK 运行时 DLL、vcpkg/Libusb DLL、
    `.mo` 语言文件等到：
    - `build/LibUSB/Release/`
    - `build/SPRD/Release/`
  - 上传 artifact：
    - `sfd_tool_LibUSB_Release`
    - `sfd_tool_SPRD_Release`

- `build-macos`
  - 运行环境：`macos-latest`；
  - 使用 Homebrew 安装 `gtk+3`, `libusb`, `gettext`, `cmake`, `ninja`；
  - CMake+Ninja 构建 Release 并运行 `ctest`；
  - 通过 `scripts/release.sh` / `scripts/build_macos_app.sh` 构建自包含
    macOS 应用与 `.dmg`；
  - 上传 artifact：`sfd_tool_macos_dmg`。

- `build-linux-deb`
  - 运行环境：`ubuntu-latest`；
  - 安装打包依赖（`debhelper`, `devscripts`, `libgtk-3-dev`,
    `libusb-1.0-0-dev` 等）；
  - 调用 [packaging/build-deb.sh](../packaging/build-deb.sh) 构建 `.deb`
    包（以及必要的 `.rpm` 以便在部分环境测试）；
  - 上传 artifact：`sfd_tool_linux_deb`（内含 `.deb`/`.rpm`）。

- `build-linux-rpm`
  - 运行环境：`ubuntu-latest`，在 `fedora:latest` 容器中执行；
  - 安装 `rpm-build`, `gtk3-devel`, `libusb1-devel`, `cmake`,
    `ninja-build` 等；
  - 调用 [packaging/build-rpm.sh](../packaging/build-rpm.sh) 构建
    `.rpm` 包；
  - 上传 artifact：`sfd_tool_rpm_package`。

- `release`
  - 仅在 `github.ref` 以 `refs/tags/v` 开头时执行；
  - 依赖上述 4 个构建 job；
  - 下载所有 artifact 到 `output/`；
  - 重命名 Windows 目录并补充文档/第三方 DLL；
  - 打包 `${TAG}.zip`；
  - 使用 `RELEASE_TOKEN` 调用 `softprops/action-gh-release@v2`
    创建 GitHub Release 并上传 `.zip`/`.deb`/`.rpm`/`.dmg`。

### 6.3 修改 CI 时的注意事项

1. **保持 artifact 名称与目录结构稳定**：
   - `release` job 依赖 `build-windows`/`build-macos`/
     `build-linux-deb`/`build-linux-rpm` 上传的 artifact 名称与路径；
   - 如需调整名称或路径，请同步更新 `release` job 中对应的
     `download-artifact` 和文件操作命令。

2. **依赖安装方式需与本地脚本保持一致**：
   - Windows 端需保持 vcpkg/MSYS2 路径、安装包名称与本地脚本相符；
   - macOS/Linux 端若修改 CMake 选项或依赖，请同步更新
     `scripts/dev.sh` / `scripts/release.sh` 以及 CI 中对应命令。

3. **Release Token 与权限**：
   - 确认 `RELEASE_TOKEN` Secret 存在且 PAT 未过期，并包含 `repo` 权限。

4. **测试与本地复现**：
   - 尽量使用与 CI 一致的命令在本地复现问题，减小环境差异。

通过以上流程，可以在保留现有 CI/打包体系的前提下，稳妥地发布新的
sfd_tool 版本，并确保 GUI About 页、版本记录和 GitHub Releases 三者
在版本和内容上保持一致。