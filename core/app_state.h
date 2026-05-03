#pragma once
#include <cstdint>

struct spdio_t; // 前向声明，避免头文件循环依赖

// 设备连接与协议阶段等“业务状态”
struct DeviceState {
    int m_bOpened = 0;      // 当前设备连接状态（0 未连接，1 已连接，-1 已拔出等待退出等）
    int device_stage = -1;  // 设备当前阶段（参考 core/spd_protocol.h::Stages）
    int device_mode = -1;   // 设备当前模式（SPRD3 / SPRD4 等）
};

// 引导 / 分区 / 刷机相关的业务状态
struct FlashState {
    int gpt_failed = 1;     // GPT 读取是否失败：1 初始/未读，0 成功，其它为错误
    int isCMethod = 0;      // 兼容模式标志（PartList 等）
    int selected_ab = -1;   // 当前使用的 slot（0=无，1=a，2=b）
    int g_w_force = 1;     // 是否自动启用强制写入（针对部分特殊分区）
    bool isPacFlashing = false; // 是否正在进行 PAC 刷机（影响分区选择和写入策略）
    std::string pac_xmlPath; // PAC 刷机时解析出的 XML 路径，供后续分区信息查询使用
};

// 与具体传输实现相关的状态（平台/IO 细节），暂时仍放在这里，后续可进一步下沉到传输层
struct TransportState {
    int bListenLibusb = -1; // 是否正在监听 libusb 事件 / 设备热插拔
    spdio_t* io = nullptr;  // 设备 I/O 句柄（底层传输对象）
};

// 错误/返回值相关的临时状态（待后续用统一 Result/ErrorCode 替代）
struct ErrorState {
    int last_ret = 0;       // 最近一次核心操作返回值
};

// 集中管理应用运行时状态，替代分散的 extern 全局变量
// 注意：AppState 仅承载“业务相关状态”及少量过渡期传输/错误状态，
// 不包含任何 UI 控件、窗口句柄或直接的平台 API 资源。
struct AppState {
    DeviceState    device;    // 设备连接 & 协议阶段
    FlashState     flash;     // 分区/GPT/A/B slot 等刷机相关状态
    TransportState transport; // 传输相关（libusb 监听标志、IO 句柄），后续可进一步收缩
    ErrorState     error;     // 临时错误状态，占位以便后续 T2-02 引入统一错误模型
};

extern AppState g_app_state;
