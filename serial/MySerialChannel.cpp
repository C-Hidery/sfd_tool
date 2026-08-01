// MySerialChannel.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// 完整替代 Channel9.dll 的串口通道实现，兼容异步/同步模式

#include "MySerialChannel.h"
#include "../common.h"
#include "../core/app_state.h"
#include "../core/spd_protocol.h"   // recv_transcode, recv_check_crc 等
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <queue>
#include <mutex>
#include <condition_variable>

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
// 日志
void CMySerialChannel::Log(const char* level, const char* fmt, ...) {
    if (!g_app_state.transport.channelLog) return;
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[Channel] [%s] ", level);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    fflush(stderr);
    va_end(args);
}

// ------------------------------------------------------------
// InitLog 空实现
BOOL CMySerialChannel::InitLog(LPCWSTR, UINT, UINT, ISpLog*, LPCWSTR) {
    Log("INFO", "InitLog called (no-op)");
    return TRUE;
}

// ------------------------------------------------------------
// SetReceiver：设置异步接收目标
BOOL CMySerialChannel::SetReceiver(ULONG ulMsgId, BOOL bRcvThread, LPCVOID pReceiver) {
    Log("INFO", "SetReceiver: MsgId=0x%08lX, bRcvThread=%d, pReceiver=%p",
        ulMsgId, bRcvThread, pReceiver);
    // 我们统一使用窗口消息（忽略 bRcvThread，只支持窗口）
    m_hTargetWnd = (HWND)pReceiver;
    m_ulMsgId = ulMsgId;
    m_bAsyncMode = (m_hTargetWnd != NULL && m_ulMsgId != 0);
    return TRUE;
}

void CMySerialChannel::GetReceiver(ULONG &ulMsgId, BOOL &bRcvThread, LPVOID &pReceiver) {
    ulMsgId = m_ulMsgId;
    bRcvThread = FALSE;
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
    snprintf(portName, sizeof(portName), "\\\\.\\COM%lu", pOpenArgument->Com.dwPortNum);
    Log("INFO", "Opening %s at %lu baud", portName, pOpenArgument->Com.dwBaudRate);

    m_hCom = CreateFileA(portName, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                         OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (m_hCom == INVALID_HANDLE_VALUE) {
        Log("ERROR", "CreateFile failed, error %lu", GetLastError());
        return FALSE;
    }

    DCB dcb = {0};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(m_hCom, &dcb)) {
        Log("ERROR", "GetCommState failed, error %lu", GetLastError());
        Close();
        return FALSE;
    }
    dcb.BaudRate = pOpenArgument->Com.dwBaudRate;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    if (!SetCommState(m_hCom, &dcb)) {
        Log("ERROR", "SetCommState failed, error %lu", GetLastError());
        Close();
        return FALSE;
    }

    SetTimeouts(50, 100);

    m_hStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!m_hStopEvent) {
        Log("ERROR", "CreateEvent failed, error %lu", GetLastError());
        Close();
        return FALSE;
    }

    // 如果启用了异步模式，启动内部读取线程
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

    if (m_hCom != INVALID_HANDLE_VALUE) {
        CancelIo(m_hCom);
    }

    if (m_hStopEvent) {
        SetEvent(m_hStopEvent);
        if (m_hReadThread) {
            Log("DEBUG", "Waiting for receive thread to exit...");
            WaitForSingleObject(m_hReadThread, INFINITE);
            CloseHandle(m_hReadThread);
            m_hReadThread = NULL;
            Log("DEBUG", "Receive thread exited");
        }
        CloseHandle(m_hStopEvent);
        m_hStopEvent = NULL;
    }

    // 清空数据池
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        while (!m_dataQueue.empty()) m_dataQueue.pop();
    }
    m_dataAvailable.notify_all();

    if (m_hCom != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hCom);
        m_hCom = INVALID_HANDLE_VALUE;
        Log("INFO", "COM port closed");
    }
}

// ------------------------------------------------------------
// Clear
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
// 同步读取（从数据池取数据）
DWORD CMySerialChannel::Read(LPVOID lpData, DWORD dwDataSize, DWORD dwTimeOut, DWORD /*dwReserved*/) {
    if (m_hCom == INVALID_HANDLE_VALUE || !lpData || dwDataSize == 0) {
        Log("WARN", "Read invalid parameters");
        return 0;
    }

    Log("DEBUG", "Read: size=%lu, timeout=%lu ms", dwDataSize, dwTimeOut);

    std::unique_lock<std::mutex> lock(m_queueMutex);
    bool hasData = m_dataAvailable.wait_for(lock, std::chrono::milliseconds(dwTimeOut),
        [this]() { return !m_dataQueue.empty() || !m_bRunning; });

    if (!hasData || !m_bRunning || m_dataQueue.empty()) {
        Log("DEBUG", "Read timeout or stopped");
        return 0;
    }

    auto& front = m_dataQueue.front();
    DWORD bytesToCopy = (DWORD)front.size();
    if (bytesToCopy > dwDataSize) bytesToCopy = dwDataSize;
    memcpy(lpData, front.data(), bytesToCopy);
    m_dataQueue.pop();

    Log("DEBUG", "Read returned %lu bytes", bytesToCopy);
    return bytesToCopy;
}

// ------------------------------------------------------------
// 同步写入
DWORD CMySerialChannel::Write(LPVOID lpData, DWORD dwDataSize, DWORD /*dwReserved*/) {
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
// FreeMem
void CMySerialChannel::FreeMem(LPVOID pMemBlock) {
    if (pMemBlock) {
        void* ptr = pMemBlock;
        free(pMemBlock);
        Log("DEBUG", "FreeMem: freed %p", ptr);
    }
}

// ------------------------------------------------------------
// GetProperty / SetProperty
BOOL CMySerialChannel::GetProperty(LONG, DWORD, LPVOID) { return FALSE; }
BOOL CMySerialChannel::SetProperty(LONG, DWORD, LPCVOID) { return FALSE; }

// ------------------------------------------------------------
// SetTimeouts
void CMySerialChannel::SetTimeouts(DWORD readInterval, DWORD readTotalConst) {
    if (m_hCom == INVALID_HANDLE_VALUE) return;
    COMMTIMEOUTS ct = {0};
    ct.ReadIntervalTimeout = readInterval;
    ct.ReadTotalTimeoutMultiplier = 10;
    ct.ReadTotalTimeoutConstant = readTotalConst;
    ct.WriteTotalTimeoutMultiplier = 10;
    ct.WriteTotalTimeoutConstant = readTotalConst;
    SetCommTimeouts(m_hCom, &ct);
}

// ------------------------------------------------------------
// 内部读取线程过程
DWORD WINAPI CMySerialChannel::ReadThreadProc(LPVOID lpParam) {
    CMySerialChannel* pThis = static_cast<CMySerialChannel*>(lpParam);
    pThis->Log("INFO", "ReadThread started");

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
        if (!WaitCommEvent(pThis->m_hCom, NULL, &ov)) {
            if (GetLastError() == ERROR_IO_PENDING) {
                HANDLE waitHandles[2] = { ov.hEvent, pThis->m_hStopEvent };
                DWORD ret = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
                if (ret == WAIT_OBJECT_0) {
                    if (!pThis->m_bRunning) break;
                    // 读取数据
                    BYTE buffer[4096];
                    DWORD bytesRead = 0;
                    if (ReadFile(pThis->m_hCom, buffer, sizeof(buffer), &bytesRead, NULL) &&
                        bytesRead > 0) {
                        // 1. 推入队列（供同步 Read 使用）
                        {
                            std::lock_guard<std::mutex> lock(pThis->m_queueMutex);
                            pThis->m_dataQueue.emplace(buffer, buffer + bytesRead);
                            pThis->m_dataAvailable.notify_one();
                        }
#if !USE_LIBUSB
                        // 2. 如果外部使用异步路径（io->m_dwRecvThreadID != 0），
                        //    则解码并触发事件，模拟 RcvDataThreadProc
                        spdio_t* io = g_app_state.transport.io;
                        if (io && io->m_dwRecvThreadID != 0) {
                            int plen = 6;
                            io->raw_len = 0;
                            memcpy(io->recv_buf, buffer, bytesRead);
                            io->recv_len = bytesRead;
                            if (recv_transcode(io, io->recv_buf, io->recv_len, &plen) &&
                                io->raw_len == plen) {
                                // 可选：校验 CRC，但 recv_msg_async 不校验，所以可以跳过
                                // 设置事件，唤醒 recv_msg_async
                                SetEvent(io->m_hOprEvent);
                            } else {
                                pThis->Log("WARN", "Transcode failed, data discarded");
                            }
                        }
#endif
                        // 3. 如果设置了窗口目标，发送消息（模拟 Channel9 行为）
                        if (pThis->m_hTargetWnd && pThis->m_ulMsgId) {
                            BYTE* copy = (BYTE*)malloc(bytesRead);
                            if (copy) {
                                memcpy(copy, buffer, bytesRead);
                                PostMessage(pThis->m_hTargetWnd, pThis->m_ulMsgId,
                                            (WPARAM)copy, (LPARAM)bytesRead);
                            }
                        }
                    }
                } else {
                    break;
                }
            } else {
                pThis->Log("ERROR", "ReadThread: WaitCommEvent failed, error %lu", GetLastError());
                break;
            }
        } else {
            // 立即成功（罕见）
            BYTE buffer[4096];
            DWORD bytesRead;
            if (ReadFile(pThis->m_hCom, buffer, sizeof(buffer), &bytesRead, NULL) &&
                bytesRead > 0) {
                // 同上处理
                {
                    std::lock_guard<std::mutex> lock(pThis->m_queueMutex);
                    pThis->m_dataQueue.emplace(buffer, buffer + bytesRead);
                    pThis->m_dataAvailable.notify_one();
                }
#if !USE_LIBUSB
                spdio_t* io = g_app_state.transport.io;
                if (io && io->m_dwRecvThreadID != 0) {
                    int plen = 6;
                    io->raw_len = 0;
                    memcpy(io->recv_buf, buffer, bytesRead);
                    io->recv_len = bytesRead;
                    if (recv_transcode(io, io->recv_buf, io->recv_len, &plen) &&
                        io->raw_len == plen) {
                        SetEvent(io->m_hOprEvent);
                    }
                }
#endif
                if (pThis->m_hTargetWnd && pThis->m_ulMsgId) {
                    BYTE* copy = (BYTE*)malloc(bytesRead);
                    if (copy) {
                        memcpy(copy, buffer, bytesRead);
                        PostMessage(pThis->m_hTargetWnd, pThis->m_ulMsgId,
                                    (WPARAM)copy, (LPARAM)bytesRead);
                    }
                }
            }
        }
    }

    CloseHandle(ov.hEvent);
    pThis->Log("INFO", "ReadThread exiting");
    return 0;
}