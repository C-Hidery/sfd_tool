// MySerialChannel.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// 实现文件

#include "MySerialChannel.h"
#include "../core/app_state.h"   // 需要包含 AppState 定义（包含 transport.channelLog）
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>

// 外部全局 AppState（在 main.cpp 中定义）
extern AppState g_app_state;

// ------------------------------------------------------------
// 构造 / 析构
CMySerialChannel::CMySerialChannel()
    : m_hCom(INVALID_HANDLE_VALUE)
    , m_hStopEvent(NULL)
    , m_hReadThread(NULL)
    , m_dwThreadId(0)
    , m_hTargetWnd(NULL)
    , m_ulMsgId(0)
    , m_bAsyncMode(false)
    , m_bRunning(false) {
}

CMySerialChannel::~CMySerialChannel() {
    Close();
}

// ------------------------------------------------------------
// 日志函数（条件输出）
void CMySerialChannel::Log(const char* level, const char* fmt, ...) {
    if (!g_app_state.transport.channelLog)
        return;

    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[Channel] [%s] ", level);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    fflush(stderr);
    va_end(args);
}

// ------------------------------------------------------------
// InitLog 空实现（可扩展）
BOOL CMySerialChannel::InitLog(LPCWSTR /*pszLogName*/,
                               UINT /*uiLogType*/,
                               UINT /*uiLogLevel*/,
                               ISpLog* /*pLogUtil*/,
                               LPCWSTR /*pszBinLogFileExt*/) {
    Log("INFO", "InitLog called (no-op)");
    return TRUE;
}

// ------------------------------------------------------------
// 设置异步接收目标
BOOL CMySerialChannel::SetReceiver(ULONG ulMsgId,
                                   BOOL bRcvThread,
                                   LPCVOID pReceiver) {
    Log("INFO", "SetReceiver: MsgId=0x%08lX, bRcvThread=%d, pReceiver=%p",
        ulMsgId, bRcvThread, pReceiver);

    // 我们统一使用窗口消息（PostMessage），忽略 bRcvThread
    m_hTargetWnd = (HWND)pReceiver;
    m_ulMsgId = ulMsgId;
    m_bAsyncMode = (m_hTargetWnd != NULL && m_ulMsgId != 0);
    return TRUE;
}

void CMySerialChannel::GetReceiver(ULONG &ulMsgId,
                                   BOOL &bRcvThread,
                                   LPVOID &pReceiver) {
    ulMsgId = m_ulMsgId;
    bRcvThread = FALSE;   // 我们只支持窗口消息
    pReceiver = (LPVOID)m_hTargetWnd;
}

// ------------------------------------------------------------
// 打开串口
BOOL CMySerialChannel::Open(PCCHANNEL_ATTRIBUTE pOpenArgument) {
    if (pOpenArgument->ChannelType != CHANNEL_TYPE_COM) {
        Log("ERROR", "Open: not a COM channel (type=%d)", pOpenArgument->ChannelType);
        return FALSE;
    }

    char portName[32];
    snprintf(portName, sizeof(portName), "\\\\.\\COM%lu",
             pOpenArgument->Com.dwPortNum);

    Log("INFO", "Opening %s at %lu baud", portName, pOpenArgument->Com.dwBaudRate);

    // 使用重叠 I/O 以支持超时和异步
    m_hCom = CreateFileA(portName,
                         GENERIC_READ | GENERIC_WRITE,
                         0,
                         NULL,
                         OPEN_EXISTING,
                         FILE_FLAG_OVERLAPPED,
                         NULL);
    if (m_hCom == INVALID_HANDLE_VALUE) {
        Log("ERROR", "CreateFile failed, error %lu", GetLastError());
        return FALSE;
    }

    DCB dcb = {0};
    dcb.DCBlength = sizeof(DCB);

    // 1. 先获取当前配置
    if (!GetCommState(m_hCom, &dcb)) {
        // 记录错误并返回
        Log("ERROR", "GetCommState failed, error %lu", GetLastError());
        Close();
        return FALSE;
    }

    // 2. 修改你需要更改的字段
    dcb.BaudRate = pOpenArgument->Com.dwBaudRate;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;

    // 3. 应用新配置
    if (!SetCommState(m_hCom, &dcb)) {
        // 记录错误并返回
        Log("ERROR", "SetCommState failed, error %lu", GetLastError());
        Close();
        return FALSE;
    }

    // 设置超时
    SetTimeouts(50, 100);  // 读间隔50ms，总超时100ms

    // 创建停止事件
    m_hStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!m_hStopEvent) {
        Log("ERROR", "CreateEvent failed, error %lu", GetLastError());
        Close();
        return FALSE;
    }

    // 如果启用异步模式，启动接收线程
    if (m_bAsyncMode) {
        m_bRunning = true;
        m_hReadThread = CreateThread(NULL, 0, ReadThreadProc, this, 0, &m_dwThreadId);
        if (!m_hReadThread) {
            Log("ERROR", "CreateThread failed, error %lu", GetLastError());
            Close();
            return FALSE;
        }
        Log("INFO", "Async receive thread started (ID=%lu)", m_dwThreadId);
    } else {
        Log("WARN", "Async mode not enabled, no receiver thread");
    }

    Log("INFO", "Open successful");
    return TRUE;
}

// ------------------------------------------------------------
// 关闭串口
void CMySerialChannel::Close() {
    Log("INFO", "Closing COM port");

    m_bRunning = false;

    if (m_hStopEvent) {
        SetEvent(m_hStopEvent);
        if (m_hReadThread) {
            Log("DEBUG", "Waiting for receive thread to exit...");
            WaitForSingleObject(m_hReadThread, 2000);
            CloseHandle(m_hReadThread);
            m_hReadThread = NULL;
            Log("DEBUG", "Receive thread exited");
        }
        CloseHandle(m_hStopEvent);
        m_hStopEvent = NULL;
    }

    if (m_hCom != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hCom);
        m_hCom = INVALID_HANDLE_VALUE;
        Log("INFO", "COM port closed");
    }
}

// ------------------------------------------------------------
// 清除缓冲区
BOOL CMySerialChannel::Clear() {
    if (m_hCom == INVALID_HANDLE_VALUE) {
        Log("WARN", "Clear called on closed port");
        return FALSE;
    }
    BOOL ok = PurgeComm(m_hCom, PURGE_RXCLEAR | PURGE_TXCLEAR);
    Log("DEBUG", "Clear: %s", ok ? "success" : "failed");
    return ok;
}

// ------------------------------------------------------------
// 同步读取（带超时）
DWORD CMySerialChannel::Read(LPVOID lpData,
                             DWORD dwDataSize,
                             DWORD dwTimeOut,
                             DWORD /*dwReserved*/) {
    if (m_hCom == INVALID_HANDLE_VALUE || !lpData || dwDataSize == 0) {
        Log("WARN", "Read invalid parameters");
        return 0;
    }

    Log("DEBUG", "Read: size=%lu, timeout=%lu ms", dwDataSize, dwTimeOut);

    DWORD bytesRead = 0;
    OVERLAPPED ov = {0};
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent) {
        Log("ERROR", "Read CreateEvent failed");
        return 0;
    }

    if (!ReadFile(m_hCom, lpData, dwDataSize, &bytesRead, &ov)) {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            DWORD wait = WaitForSingleObject(ov.hEvent, dwTimeOut);
            if (wait == WAIT_OBJECT_0) {
                GetOverlappedResult(m_hCom, &ov, &bytesRead, FALSE);
            } else {
                // 超时或取消
                CancelIo(m_hCom);
                WaitForSingleObject(ov.hEvent, INFINITE);
                GetOverlappedResult(m_hCom, &ov, &bytesRead, FALSE);
                bytesRead = 0;
                Log("DEBUG", "Read timeout after %lu ms", dwTimeOut);
            }
        } else {
            Log("ERROR", "ReadFile failed, error %lu", err);
            bytesRead = 0;
        }
    }
    CloseHandle(ov.hEvent);
    Log("DEBUG", "Read returned %lu bytes", bytesRead);
    return bytesRead;
}

// ------------------------------------------------------------
// 同步写入
DWORD CMySerialChannel::Write(LPVOID lpData,
                              DWORD dwDataSize,
                              DWORD /*dwReserved*/) {
    if (m_hCom == INVALID_HANDLE_VALUE || !lpData || dwDataSize == 0) {
        Log("WARN", "Write invalid parameters");
        return 0;
    }

    Log("DEBUG", "Write: size=%lu", dwDataSize);

    DWORD bytesWritten = 0;
    OVERLAPPED ov = {0};
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent) {
        Log("ERROR", "Write CreateEvent failed");
        return 0;
    }

    if (!WriteFile(m_hCom, lpData, dwDataSize, &bytesWritten, &ov)) {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            WaitForSingleObject(ov.hEvent, INFINITE);
            GetOverlappedResult(m_hCom, &ov, &bytesWritten, FALSE);
        } else {
            Log("ERROR", "WriteFile failed, error %lu", err);
            bytesWritten = 0;
        }
    }
    CloseHandle(ov.hEvent);
    Log("DEBUG", "Write returned %lu bytes", bytesWritten);
    return bytesWritten;
}

// ------------------------------------------------------------
// 释放内存（由异步接收分配）
void CMySerialChannel::FreeMem(LPVOID pMemBlock) {
    if (pMemBlock) {
        void* ptr = pMemBlock;
        free(pMemBlock);
        Log("DEBUG", "FreeMem: freed %p", ptr);
    }
}

// ------------------------------------------------------------
// 属性（暂不实现）
BOOL CMySerialChannel::GetProperty(LONG /*lFlags*/,
                                   DWORD /*dwPropertyID*/,
                                   LPVOID /*pValue*/) {
    return FALSE;
}

BOOL CMySerialChannel::SetProperty(LONG /*lFlags*/,
                                   DWORD /*dwPropertyID*/,
                                   LPCVOID /*pValue*/) {
    return FALSE;
}

// ------------------------------------------------------------
// 辅助：设置超时
void CMySerialChannel::SetTimeouts(DWORD readInterval, DWORD readTotalConst) {
    if (m_hCom == INVALID_HANDLE_VALUE)
        return;
    COMMTIMEOUTS ct = {0};
    ct.ReadIntervalTimeout         = readInterval;
    ct.ReadTotalTimeoutMultiplier  = 10;
    ct.ReadTotalTimeoutConstant    = readTotalConst;
    ct.WriteTotalTimeoutMultiplier = 10;
    ct.WriteTotalTimeoutConstant   = readTotalConst;
    SetCommTimeouts(m_hCom, &ct);
}

// ------------------------------------------------------------
// 异步接收线程过程
DWORD WINAPI CMySerialChannel::ReadThreadProc(LPVOID lpParam) {
    CMySerialChannel* pThis = static_cast<CMySerialChannel*>(lpParam);
    pThis->Log("INFO", "ReadThread started");

    // 使用 WaitCommEvent + 重叠 I/O
    OVERLAPPED ov = {0};
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent) {
        pThis->Log("ERROR", "ReadThread: CreateEvent failed");
        return 1;
    }

    if (!SetCommMask(pThis->m_hCom, EV_RXCHAR)) {
        pThis->Log("ERROR", "ReadThread: SetCommMask failed");
        CloseHandle(ov.hEvent);
        return 1;
    }

    while (pThis->m_bRunning) {
        // 等待串口事件
        if (!WaitCommEvent(pThis->m_hCom, NULL, &ov)) {
            if (GetLastError() == ERROR_IO_PENDING) {
                HANDLE waitHandles[2] = { ov.hEvent, pThis->m_hStopEvent };
                DWORD ret = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
                if (ret == WAIT_OBJECT_0) {
                    // 串口事件发生
                    GetOverlappedResult(pThis->m_hCom, &ov, NULL, FALSE);
                    // 读取所有可用数据
                    BYTE buffer[4096];
                    DWORD bytesRead = 0;
                    if (ReadFile(pThis->m_hCom, buffer, sizeof(buffer), &bytesRead, NULL) &&
                        bytesRead > 0) {
                        pThis->ProcessReceivedData(buffer, bytesRead);
                    }
                } else {
                    // 停止事件触发
                    break;
                }
            } else {
                pThis->Log("ERROR", "ReadThread: WaitCommEvent failed, error %lu", GetLastError());
                break;
            }
        } else {
            // WaitCommEvent 立即成功（罕见）
            BYTE buffer[4096];
            DWORD bytesRead;
            if (ReadFile(pThis->m_hCom, buffer, sizeof(buffer), &bytesRead, NULL) &&
                bytesRead > 0) {
                pThis->ProcessReceivedData(buffer, bytesRead);
            }
        }
    }

    CloseHandle(ov.hEvent);
    pThis->Log("INFO", "ReadThread exiting");
    return 0;
}

// ------------------------------------------------------------
// 处理收到的数据：复制并投递消息
void CMySerialChannel::ProcessReceivedData(const BYTE* data, DWORD len) {
    BYTE* copy = (BYTE*)malloc(len);
    if (!copy) {
        Log("ERROR", "ProcessReceivedData: malloc failed");
        return;
    }
    memcpy(copy, data, len);
    Log("DEBUG", "ProcessReceivedData: %lu bytes, posting to window", len);

    if (m_hTargetWnd && m_ulMsgId) {
        PostMessage(m_hTargetWnd, m_ulMsgId, (WPARAM)copy, (LPARAM)len);
    } else {
        Log("WARN", "No target window or MsgId, discarding data");
        free(copy);
    }
}