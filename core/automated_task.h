/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sfd {

struct AutomatedTaskPartition {
    std::string name;
    std::uint64_t size_bytes = 0;
};

struct AutomatedTaskRequest {
    // ZIP 解压根目录；write_part / write_parts 的镜像根目录为 <root>/images。
    std::string package_root;
    // package.json 中的分区表，供 repartition 使用。
    std::vector<AutomatedTaskPartition> partitions;
    // 按顺序执行的命令，每个元素是一行命令。
    std::vector<std::string> actions;
};

struct AutomatedTaskResult {
    bool success = false;
    std::string message;
    // 执行了 reset / poweroff / reboot-*，设备已断开。
    bool device_detached = false;
};

using AutomatedTaskLogFn = std::function<void(const std::string&)>;

// 依次解析并执行 actions。日志通过 log 回调输出，失败时立即停止。
AutomatedTaskResult run_automated_task(const AutomatedTaskRequest& request,
                                       const AutomatedTaskLogFn& log);

} // namespace sfd
