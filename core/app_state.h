#pragma once
#include <cstdint>

struct spdio_t; // 前向声明，避免头文件循环依赖

// 集中管理应用运行时状态，替代分散的 extern 全局变量
struct AppState {
    // 设备与连接状态
    int m_bOpened = 0;          // 当前设备连接状态（0 未连接，1 已连接，-1 已拔出等待退出等）
    int bListenLibusb = -1;     // 是否正在监听 libusb 事件
    int device_stage = -1;      // 设备当前阶段（参考 core/spd_protocol.h::Stages）
    int device_mode = -1;       // 设备当前模式

    // 引导 / 分区 / 刷机相关状态
    int gpt_failed = 1;         // GPT 读取是否失败
    int isCMethod = 0;          // 兼容模式标志
    int selected_ab = -1;       // 当前使用的 slot（0=无，1=a，2=b）

    // 连接与 I/O
    spdio_t* io = nullptr;      // 设备 I/O 句柄
    int ret = 0;                // 最近一次操作返回值

    // 其他状态按需继续补充
};

extern AppState g_app_state;
