# sfd_tool 版本发布流程

下面假设下一个版本号是 `1.7.7.0`，Git tag 使用 `v1.7.7`，按需替换为你自己的版本号即可。

## 1. 更新版本号（唯一来源）

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
     Fix XXX bug, improve YYY behavior
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

## 4. 打 tag 触发 CI 发布

在远端分支上打版本 tag：

```bash
git tag v1.7.7
git push origin v1.7.7
```

- GitHub Actions 会使用该 tag 触发编译与发布流程。
- Release 标题类似 `sfd_tool v1.7.7`，产物中的版本号统一来自 `VERSION.txt`。

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