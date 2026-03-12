#pragma once

struct spdio_t;

// 非 GUI 场景下统一检查设备是否未连接，并在未连接时输出 Error 级别日志。
// 仅负责判定和日志，不负责退出或抛异常。
// 返回值：true 表示“设备未连接（并已打日志）”，false 表示“设备已连接”。
bool is_device_unattached_and_log(spdio_t *io);
