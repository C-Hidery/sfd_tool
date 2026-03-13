# sfd_tool 版本发布流程（简要）

## 1. 更新版本号

1. 修改根目录 `VERSION.txt` 第一行，例如：
   ```
   1.7.7.0
   ```
2. 重新运行 CMake 配置与编译（示例）：
   ```bash
   cmake -S . -B build_cmake_debug -DCMAKE_BUILD_TYPE=Debug
   cmake --build build_cmake_debug -j
   ```

## 2. 更新版本记录

1. 编辑 `docs/VERSION_LOG.md`，在末尾按现有格式追加一段：
   ```markdown
   ---v 1.7.7.0---
   Fix XXX bug, improve YYY
   ```
2. 重新运行程序即可在 About 页面看到更新后的版本记录。

## 3. 发布新版本（建议）

1. 在 master 上准备好代码与上述版本信息。
2. 打 tag：
   ```bash
   git tag v1.7.7
   git push origin v1.7.7
   ```
3. GitHub Actions 会基于 tag 构建并发布 Release。
