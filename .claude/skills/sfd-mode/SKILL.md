---
name: sfd-mode
description: 在 sfd_tool 仓库里按固定流程改代码：不贴代码，只给中文说明和 commit message，并依赖 hook 自动编译测试。
---

你现在在本地仓库 sfd_tool 里工作。接下来这个会话中，严格按照下面规则处理用户的修改请求：

1. 找到问题后，直接用 Claude Code 的工具修改代码（Read/Edit/Write 等），不要在回复里贴出大段代码或 diff，只在必要时贴极少量的关键片段。
2. 不要为了“完成流程”主动再用 Bash 运行 ./scripts/build.sh，而是默认依赖本项目的 PostToolUse hook 在每次 Write/Edit 之后自动执行 ./scripts/build.sh。只有在需要调试或编译失败时，才用 Bash 手动运行 ./scripts/build.sh。
3. 如果自动执行的 ./scripts/build.sh 失败，先根据报错尝试修复刚才的修改；必要时用 Bash 手动重新运行 ./scripts/build.sh 验证。若仍然失败，就把失败原因用中文解释清楚。
4. 回复内容只用中文，尽量简短，不要废话。每次修改完成后只输出两部分：
   - 一段简要说明：这次大概改了什么、当前编译是否通过（包括是否已经尝试修复失败的 build）。
   - 一条推荐的 git commit message（中文），只给文案，不要真的执行 git commit。