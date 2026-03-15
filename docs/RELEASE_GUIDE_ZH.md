# sfd_tool 版本发布流程

> 相关文档： [README_ZH](../README_ZH.md) · [操作手册](USER_GUIDE_ZH.md) · [CMake 使用指南](cmake.md) · [版本记录](VERSION_LOG.md)

下面假设下一个版本号是 `1.7.7.0`，Git tag 使用 `v1.7.7`，按需替换为你自己的版本号即可。

## 0. 一次性配置 GitHub 发布令牌（RELEASE_TOKEN）

> 本仓库使用 GitHub PAT（RELEASE_TOKEN）来创建 Release，避免默认 GITHUB_TOKEN 在部分场景下调用 Releases API 时返回
> `403 Resource not accessible by integration` 的问题。

1. **在 GitHub 上创建 PAT**（个人访问令牌）：
   1. 打开 <https://github.com/settings/tokens>。
   2. 选择 **Tokens (classic)** → `Generate new token`。
   3. 备注可以写成 `sfd_tool release` 之类好认的名字。
   4. 在权限（Scopes）中勾选：
      - 至少需要：`repo`（包含对 Release/contents 的读写权限）。
   5. 生成 Token 并复制保存好（只会显示一次）。

2. **配置仓库级别 Secret**：
   1. 打开项目仓库页面 → `Settings` → `Secrets and variables` → `Actions`。
   2. 点击 `New repository secret`。
   3. Name 填写：`RELEASE_TOKEN`。
   4. Secret 值粘贴刚才创建的 PAT，保存。

3. **CI 中的使用方式说明**：

   在 [`.github/workflows/build.yml`](../.github/workflows/build.yml) 中，`release` 这个 job 会调用
   `softprops/action-gh-release@v2`，并通过：

   ```yaml
   - name: Create GitHub Release
     uses: softprops/action-gh-release@v2
     with:
       token: ${{ secrets.RELEASE_TOKEN }}
       # ... 其他参数略
   ```

   来使用你刚配置的 `RELEASE_TOKEN` 调用 GitHub Releases API，从而避免默认 `GITHUB_TOKEN` 可能出现的 403 问题。

---

## 1. 更新版本号（唯一来源）

> 推荐：从 1.7.7.4 起，可以使用脚本自动完成本节和后面“更新版本记录”“同步 Windows 资源版本”的大部分工作：
>
> - 在 Linux/macOS 上：`./scripts/bump_version.sh`
> - 在 Windows PowerShell 上：`.\scripts\bump_version.ps1`
>
> 脚本会：
> - 修改根目录 `VERSION.txt`
> - 更新并追加 `docs/VERSION_LOG.md`
> - 同步 `packaging/rpm-build/sfd-tool.spec` 中的 `Version` 字段
> - 更新 `assets/app.rc` 中的版本号
> 并自动创建一条 `git commit`（提交信息形如 `Version: 1.7.7.4`）。
> 如果你更倾向于手工操作，仍然可以按照下面的步骤进行。

1. 编辑根目录 `VERSION.txt`，把第一行改成新版本号，例如：
   ```
   1.7.7.0
   ```
2. 重新运行 CMake 配置与编译（示例）：
   ```bash
   cmake -S . -B build_cmake_debug -DCMAKE_BUILD_TYPE=Debug
   cmake --build build_cmake_debug -j
   ```

## 2. 更新版本记录（VERSION_LOG）

1. 编辑 `docs/VERSION_LOG.md`：
   - 如果需要，更新文件开头的展示版本：
     ```text
     Version 1.7.6.0 LTV Edition
     ```
     改为：
     ```text
     Version 1.7.7.0 LTV Edition
     ```
   - 在文件末尾追加本次发布的变更记录，例如：
     ```markdown
     ---v 1.7.7.0---
     UI重构，优化代码逻辑
     ```
2. 运行开发脚本本地确认：
   ```bash
   ./scripts/dev.sh
   ```
   - GUI About 页应显示 `Version 1.7.7.0 ...`
   - 版本记录中应包含你刚追加的 `---v 1.7.7.0---` 条目。

## 3. 提交代码

仅需提交版本号和版本记录两个文件的改动：

```bash
git add VERSION.txt docs/VERSION_LOG.md
git commit -m "bump version to 1.7.7.0"
git push
```

## 4. 打 tag 触发 CI 编译 & 自动创建 Release

1. 在本地为当前 master 分支打 tag：

   ```bash
   git tag v1.7.7
   git push origin v1.7.7
   ```

2. 推送 tag 后，GitHub Actions 会：
   - 使用该 tag 触发完整的 CI：
     - `build-windows`（Windows x64 可执行文件 & 依赖打包）
     - `build-macos`（macOS 可执行文件 & DMG）
     - `build-linux-deb`（Deb 包）
     - `build-linux-rpm`（RPM 包）
   - 四个构建 job 成功后，触发 `release` job：
     - 下载各平台构建产物，整理到 `output/` 目录下；
     - 生成 `${TAG}.zip`（例如 `v1.7.7.zip`），其中包含：
       - Windows x64 目录（带 DLL 和运行时数据文件）；
       - Linux deb/rpm 包；
       - macOS DMG；
       - README 等文档；
     - 使用 `RELEASE_TOKEN` 调用 GitHub Releases API，自动创建对应的 Release：
       - Release 标题类似：`sfd_tool v1.7.7`；
       - Release 文案由 [`.github/workflows/build.yml`](../.github/workflows/build.yml) 中的 `body` 字段定义；
       - `${TAG}.zip`、`.deb`、`.rpm`、`.dmg` 会自动作为附件上传到该 Release。

> 说明：如果未来看到 `Create GitHub Release` 步骤里出现
> `Resource not accessible by integration` 403 错误，优先检查：
> - 仓库 `Settings → Secrets and variables → Actions` 中是否存在 `RELEASE_TOKEN`；
> - 该 PAT 是否仍然有效、未过期，并保留了 `repo` 权限；
> - 打 tag 的仓库是否为主仓库而不是 fork。

## 5. （可选）同步 Windows 可执行文件属性版本

如果需要在 Windows 资源（右键属性→详细信息）中显示一致的版本号：

1. 编辑 `assets/app.rc`：
   - 把以下字段中的版本更新为新版本，例如从：
     ```rc
     FILEVERSION 1,7,6,0
     PRODUCTVERSION 1,7,6,0
     VALUE "FileVersion", "1.7.6.0"
     VALUE "ProductVersion", "1.7.6.0"
     ```
     改为：
     ```rc
     FILEVERSION 1,7,7,0
     PRODUCTVERSION 1,7,7,0
     VALUE "FileVersion", "1.7.7.0"
     VALUE "ProductVersion", "1.7.7.0"
     ```
2. 重新用 Visual Studio 或 CMake 构建 Windows 版本。

此步骤只影响 Windows 文件属性展示，不影响 CI 构建和 Linux/macOS 版本号。

## 6. GitHub Actions CI 构建矩阵概览

本仓库使用单个工作流文件 [build.yml](../.github/workflows/build.yml) 完成多平台构建和自动发布。下面仅做概要说明，具体命令以工作流文件为准。

### 6.1 触发条件

- 推送到 `master` 分支的 commit
- 推送以 `v` 开头的 tag（例如 `v1.7.7`）：在完成各平台构建后，会触发 `release` job 自动创建 GitHub Release
- 针对 `master` 的 Pull Request

### 6.2 各 job 简介

- `build-windows`
  - 运行环境：`windows-latest`
  - 使用 vcpkg (`C:\vcpkg`) 安装 `gettext`（用于生成 .mo 语言文件，提供 `msgfmt`）
  - 使用预装 MSYS2 (`C:\msys64`) 安装 GTK3、glib、pango、cairo 等依赖
  - 通过 CMake 生成 Visual Studio 2022 工程（`build_cmake`），开启 `SFD_ENABLE_TESTS=ON`
  - 矩阵构建：
    - x64：`USE_LIBUSB=ON`，对应最终的 `sfd_tool_LibUSB_Release`
    - x86：`USE_LIBUSB=OFF`，使用官方 SPRD 驱动，对应 `sfd_tool_SPRD_Release`
  - 构建并运行单元测试 (`ctest`)，随后将可执行文件与 GTK 运行时 DLL、图标、schema 等一起拷贝到 `./build/<arch>/<config>/` 目录，并作为构建产物上传

- `build-macos`
  - 运行环境：`macos-latest`
  - 使用 Homebrew 安装 `gtk+3`, `libusb`, `gettext`, `cmake`, `ninja`
  - 使用 CMake+Ninja 构建 Release 版本并运行单元测试
  - 将可执行文件、README、LICENSE 以及 `.mo` 语言文件整理到 `dist/macos/`，再打包成 `sfd_tool_macos.dmg` 作为构建产物上传

- `build-linux-deb`
  - 运行环境：`ubuntu-latest`
  - 安装 Debian 打包依赖（`build-essential`, `debhelper`, `devscripts`, `libgtk-3-dev`, `libusb-1.0-0-dev` 等）
  - 调用 [packaging/build-deb.sh](../packaging/build-deb.sh) 在 `/tmp/build-sfd-tool/` 下构建 `.deb` 包及相关元数据，并上传为构建产物

- `build-linux-rpm`
  - 运行环境：`ubuntu-latest`，在 `fedora:latest` 容器内执行
  - 安装 `rpm-build`, `gtk3-devel`, `libusb1-devel`, `cmake`, `ninja-build` 等依赖
  - 调用 [packaging/build-rpm.sh](../packaging/build-rpm.sh) 在 `~/rpmbuild/RPMS/` 下生成 `.rpm` 包并上传

- `release`
  - 仅当当前 ref 为 `refs/tags/v*` 时执行
  - 依赖前面四个构建 job 成功（Windows/macOS/Linux deb & rpm）
  - 下载各平台构建产物到 `output/` 目录，拷贝 README、第三方 DLL 等
  - 将 `sfd_tool_x86_Release` 重命名为 `sfd_tool_SPRD_Release`，`sfd_tool_x64_Release` 重命名为 `sfd_tool_LibUSB_Release`
  - 打出一个包含全部内容的压缩包 `${TAG}.zip`，并把 `.deb`、`.rpm`、`.dmg` 一并作为附件上传
  - 使用 `softprops/action-gh-release@v2` + 仓库 Secret `RELEASE_TOKEN` 创建对应 tag 的 GitHub Release

### 6.3 修改 CI 时的注意事项

1. **保持 artifact 名称和目录结构稳定**
   - `build-windows`/`build-macos`/`build-linux-*` 中上传的 artifact 名称（如 `sfd_tool_*_Release`, `sfd_tool_macos_dmg`, `sfd_tool_linux_deb`, `sfd_tool_rpm_package`）以及内部目录结构被 `release` job 依赖。
   - 如需调整 artifact 名称或路径，必须同步更新 `release` job 中的 `download-artifact` 以及后续 `cp`/`mv`/`zip` 命令，否则自动发布会失败或缺文件。

2. **依赖安装方式与本地脚本保持一致**
   - Windows 端：工作流假定存在 `C:\vcpkg` 与 `C:\msys64`，并在其中安装 GTK3、gettext 等。如果你改动 vcpkg/MSYS2 路径或依赖安装方式，需要一并更新：
     - `.github/workflows/build.yml` 中对应的命令
     - 本地脚本如 [scripts/dev.ps1](../scripts/dev.ps1)、[scripts/release.ps1](../scripts/release.ps1)
   - macOS/Linux 端：如果 CMake 选项或依赖（如 `USE_GTK`, `USE_LIBUSB`, `SFD_ENABLE_TESTS`）有变动，请同步更新：
     - 本地脚本 [scripts/dev.sh](../scripts/dev.sh)、[scripts/release.sh](../scripts/release.sh)
     - GitHub Actions 工作流中的 CMake 命令与依赖安装步骤（`brew`/`apt`/`dnf`）

3. **Release Token 与权限**
   - `release` job 使用仓库 Secret `RELEASE_TOKEN` 调用 GitHub Releases API，其配置方式见前文“0. 一次性配置 GitHub 发布令牌（RELEASE_TOKEN）”。
   - 如果在 `Create GitHub Release` 步骤看到 `Resource not accessible by integration` 等 403 错误，优先检查：
     - 是否配置了 `RELEASE_TOKEN` Secret 且未过期；
     - PAT 是否至少包含 `repo` 权限；
     - 当前 tag 是否推送到主仓库而不是 fork。

4. **测试与本地复现**
   - CI 中所有构建都启用了 `SFD_ENABLE_TESTS=ON` 并运行 `ctest`。如需在本地复现 CI 环境，可尽量使用与 CI 一致的命令：
     - Windows：参考 `build-windows` job 中的 CMake 生成与配置选项
     - macOS：使用 `cmake -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DUSE_GTK=ON -DUSE_LIBUSB=ON -DSFD_ENABLE_TESTS=ON`
     - Linux：优先使用 [packaging/build-deb.sh](../packaging/build-deb.sh) 与 [packaging/build-rpm.sh](../packaging/build-rpm.sh) 进行打包测试

这样，当你调整 CMake/依赖或新增平台支持时，可以快速定位需要同步修改的工作流片段，避免破坏自动发布流程。