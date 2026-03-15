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
