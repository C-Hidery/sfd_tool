/*
* SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 * MySerialChannel - SPRD VCOM Channel
 */

// MySerialChannel.cpp
#include "MySerialChannel.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>

// ------------------------------------------------------------
// 构造
CMySerialChannel::CMySerialChannel()
    : m_hCom(INVALID_HANDLE_VALUE)
    , m_hStopEvent(NULL)
    , m_hReadThread(NULL)
    , m_dwThreadId(0)
    , m_hTargetWnd(NULL)
    , m_ulMsgId(0)
    , m_bRcvThread(FALSE)
    , m_bAsyncMode(false)
    , m_syncMode(true)   // 默认同步，直到 Open 时确定
    , m_LogVisible(false) // 默认不打印日志
    , m_bRunning(false)
    , m_hReadOvEvent(NULL)
    , m_hWriteOvEvent(NULL)
    , m_bOvInitialized(false) {
    ZeroMemory(&m_readOv, sizeof(m_readOv));
    ZeroMemory(&m_writeOv, sizeof(m_writeOv));
}

// ------------------------------------------------------------
// 析构
CMySerialChannel::~CMySerialChannel() {
    Close();
}

// ------------------------------------------------------------
// 日志函数
void CMySerialChannel::Log(const char* level, const char* fmt, ...) {
    if (!m_LogVisible) return;
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
BOOL CMySerialChannel::InitLog(LPCWSTR /*pszLogName*/,
                               UINT /*uiLogType*/,
                               UINT /*uiLogLevel*/,
                               ISpLog* /*pLogUtil*/,
                               LPCWSTR /*pszBinLogFileExt*/) {
    Log("INFO", "InitLog called (no-op)");
    return TRUE;
}



// ------------------------------------------------------------
// SetReceiver：设置异步接收目标
BOOL CMySerialChannel::SetReceiver(ULONG ulMsgId, BOOL bRcvThread, LPCVOID pReceiver) {
    Log("INFO", "SetReceiver: MsgId=0x%08lX, bRcvThread=%d, pReceiver=%p",
        ulMsgId, bRcvThread, pReceiver);
    m_ulMsgId = ulMsgId;
    m_bRcvThread = bRcvThread;
    if (bRcvThread) {
        // pReceiver 是线程 ID (DWORD)
        m_hTargetWnd = (HWND)(ULONG_PTR)pReceiver;
    } else {
        m_hTargetWnd = (HWND)pReceiver;
    }
    m_bAsyncMode = (m_ulMsgId != 0 && pReceiver != NULL);
    return TRUE;
}

// ------------------------------------------------------------
void CMySerialChannel::GetReceiver(ULONG &ulMsgId, BOOL &bRcvThread, LPVOID &pReceiver) {
    ulMsgId = m_ulMsgId;
    bRcvThread = m_bRcvThread;
    pReceiver = (LPVOID)m_hTargetWnd;
}

// ------------------------------------------------------------
// 设置超时（非阻塞读取 + 长写入超时）
void CMySerialChannel::SetTimeouts() {
    if (m_hCom == INVALID_HANDLE_VALUE) return;
    COMMTIMEOUTS ct = {0};
    // 读取：立即返回（非阻塞），让内部线程快速循环
    ct.ReadIntervalTimeout = MAXDWORD;
    ct.ReadTotalTimeoutMultiplier = 0;
    ct.ReadTotalTimeoutConstant = 0;
    // 写入：5 秒超时，避免大块传输中断
    ct.WriteTotalTimeoutMultiplier = 10;
    ct.WriteTotalTimeoutConstant = 5000;
    SetCommTimeouts(m_hCom, &ct);
}

BOOL CMySerialChannel::SetLogVisible(bool bLogVisible)
{
    m_LogVisible = bLogVisible;
    return TRUE;
}


// ------------------------------------------------------------
// Open
BOOL CMySerialChannel::Open(PCCHANNEL_ATTRIBUTE pOpenArgument) {
    if (pOpenArgument->ChannelType != CHANNEL_TYPE_COM) {
        Log("ERROR", "Open: not a COM channel (type=%d)", pOpenArgument->ChannelType);
        return FALSE;
    }

    char portName[32];
    snprintf(portName, sizeof(portName), "\\\\.\\COM%lu", pOpenArgument->Com.dwPortNum);
    Log("INFO", "Opening %s at %lu baud", portName, pOpenArgument->Com.dwBaudRate);

    m_hCom = CreateFileA(portName,
                         GENERIC_READ | GENERIC_WRITE,
                         0, NULL,
                         OPEN_EXISTING,
                         FILE_FLAG_OVERLAPPED,
                         NULL);
    if (m_hCom == INVALID_HANDLE_VALUE) {
        Log("ERROR", "CreateFile failed, error %lu", GetLastError());
        return FALSE;
    }

    // 配置串口
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

    // 设置超时
    SetTimeouts();

    // 停止事件
    m_hStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!m_hStopEvent) {
        Log("ERROR", "CreateEvent for stop failed, error %lu", GetLastError());
        Close();
        return FALSE;
    }

    // 创建重叠 I/O 事件（复用，避免频繁创建销毁）
    m_hReadOvEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    m_hWriteOvEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!m_hReadOvEvent || !m_hWriteOvEvent) {
        Log("ERROR", "CreateEvent for overlap failed, error %lu", GetLastError());
        Close();
        return FALSE;
    }
    ZeroMemory(&m_readOv, sizeof(m_readOv));
    ZeroMemory(&m_writeOv, sizeof(m_writeOv));
    m_readOv.hEvent = m_hReadOvEvent;
    m_writeOv.hEvent = m_hWriteOvEvent;
    m_bOvInitialized = true;

    m_syncMode = !m_bAsyncMode;
    if (m_bAsyncMode) {
        Log("INFO", "Async mode enabled (SetReceiver called)");
        m_bRunning = true;
        m_hReadThread = CreateThread(NULL, 0, ReadThreadProc, this, 0, &m_dwThreadId);
        if (!m_hReadThread) {
            Log("ERROR", "CreateThread failed, error %lu", GetLastError());
            Close();
            return FALSE;
        }
        Log("INFO", "Async receive thread started (ID=%lu)", m_dwThreadId);
    } else {
        Log("INFO", "Sync mode (SetReceiver not called)");
        // 不启动线程，Read 将使用同步读取
    }

    Log("INFO", "Open successful");
    return TRUE;
}

// ------------------------------------------------------------
// Close
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

    // 清理重叠事件
    if (m_hReadOvEvent) {
        CloseHandle(m_hReadOvEvent);
        m_hReadOvEvent = NULL;
    }
    if (m_hWriteOvEvent) {
        CloseHandle(m_hWriteOvEvent);
        m_hWriteOvEvent = NULL;
    }
    m_bOvInitialized = false;

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
// 同步读取（直接调用 ReadFile，用于同步模式）
DWORD CMySerialChannel::SyncRead(LPVOID lpData, DWORD dwDataSize, DWORD dwTimeOut) {
    std::lock_guard<std::mutex> lock(m_serialMutex);
    if (m_hCom == INVALID_HANDLE_VALUE || !lpData || dwDataSize == 0) {
        Log("WARN", "SyncRead invalid parameters");
        return 0;
    }

    DWORD bytesRead = 0;
    OVERLAPPED ov = {0};
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent) {
        Log("ERROR", "SyncRead CreateEvent failed");
        return 0;
    }

    if (!ReadFile(m_hCom, lpData, dwDataSize, &bytesRead, &ov)) {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            DWORD wait = WaitForSingleObject(ov.hEvent, dwTimeOut);
            if (wait == WAIT_OBJECT_0) {
                if (!GetOverlappedResult(m_hCom, &ov, &bytesRead, FALSE)) {
                    Log("ERROR", "SyncRead GetOverlappedResult failed, error=%lu", GetLastError());
                    bytesRead = 0;
                }
            } else {
                CancelIo(m_hCom);
                WaitForSingleObject(ov.hEvent, INFINITE);
                GetOverlappedResult(m_hCom, &ov, &bytesRead, FALSE);
                bytesRead = 0;
                Log("DEBUG", "SyncRead timeout after %lu ms", dwTimeOut);
            }
        } else {
            Log("ERROR", "SyncRead ReadFile failed, error=%lu", err);
            bytesRead = 0;
        }
    } else {
        Log("DEBUG", "SyncRead completed immediately, bytesRead=%lu", bytesRead);
    }

    CloseHandle(ov.hEvent);
    Log("DEBUG", "SyncRead returned %lu bytes", bytesRead);
    return bytesRead;
}

// ------------------------------------------------------------
// Read（根据同步/异步模式选择读取方式）
DWORD CMySerialChannel::Read(LPVOID lpData, DWORD dwDataSize, DWORD dwTimeOut, DWORD /*dwReserved*/) {
    if (m_hCom == INVALID_HANDLE_VALUE || !lpData || dwDataSize == 0) {
        Log("WARN", "Read invalid parameters");
        return 0;
    }

    // 同步模式：直接读取
    if (m_syncMode) {
        Log("DEBUG", "Read (sync) size=%lu, timeout=%lu", dwDataSize, dwTimeOut);
        return SyncRead(lpData, dwDataSize, dwTimeOut);
    }

    // 异步模式：从数据池取数据
    Log("DEBUG", "Read (async) size=%lu, timeout=%lu", dwDataSize, dwTimeOut);
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
// Write（复用重叠事件，支持大块写入）
DWORD CMySerialChannel::Write(LPVOID lpData, DWORD dwDataSize, DWORD /*dwReserved*/) {
    std::lock_guard<std::mutex> lock(m_serialMutex);
    if (m_hCom == INVALID_HANDLE_VALUE || !lpData || dwDataSize == 0) {
        Log("WARN", "Write invalid parameters");
        return 0;
    }

    if (!m_bOvInitialized) {
        Log("ERROR", "Write: overlap not initialized");
        return 0;
    }

    DWORD bytesWritten = 0;
    ResetEvent(m_hWriteOvEvent);

    if (!WriteFile(m_hCom, lpData, dwDataSize, &bytesWritten, &m_writeOv)) {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            WaitForSingleObject(m_hWriteOvEvent, INFINITE);
            if (!GetOverlappedResult(m_hCom, &m_writeOv, &bytesWritten, FALSE)) {
                Log("ERROR", "Write GetOverlappedResult failed, error=%lu", GetLastError());
                bytesWritten = 0;
            }
        } else {
            Log("ERROR", "WriteFile failed, error=%lu", err);
            bytesWritten = 0;
        }
    }

    Log("DEBUG", "Write returned %lu bytes", bytesWritten);
    return bytesWritten;
}

// ------------------------------------------------------------
// FreeMem
void CMySerialChannel::FreeMem(LPVOID pMemBlock) {
    if (pMemBlock) {
        Log("DEBUG", "FreeMem: free %p", pMemBlock);
        free(pMemBlock);
    }
}

// ------------------------------------------------------------
// SetProperty：设置通道属性（目前支持波特率修改）
BOOL CMySerialChannel::SetProperty(LONG lFlags, DWORD dwPropertyID, LPCVOID pValue) {
    // 目前只支持设置波特率 (dwPropertyID == 100)
    std::lock_guard<std::mutex> lock(m_serialMutex);
    if (dwPropertyID != 100) {
        Log("WARN", "SetProperty: unknown property ID %lu", dwPropertyID);
        return FALSE;
    }
    if (!pValue) {
        Log("ERROR", "SetProperty: pValue is NULL");
        return FALSE;
    }
    if (m_hCom == INVALID_HANDLE_VALUE) {
        Log("ERROR", "SetProperty: invalid handle");
        return FALSE;
    }

    DWORD newBaudRate = *(DWORD*)pValue;
    DCB dcb = {0};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(m_hCom, &dcb)) {
        Log("ERROR", "SetProperty: GetCommState failed, error=%lu", GetLastError());
        return FALSE;
    }
    dcb.BaudRate = newBaudRate;
    if (!SetCommState(m_hCom, &dcb)) {
        Log("ERROR", "SetProperty: SetCommState failed, error=%lu", GetLastError());
        return FALSE;
    }
    Log("INFO", "SetProperty: baudrate changed to %lu", newBaudRate);
    return TRUE;
}

// ------------------------------------------------------------
// GetProperty：获取通道属性（目前支持获取当前波特率）
BOOL CMySerialChannel::GetProperty(LONG lFlags, DWORD dwPropertyID, LPVOID pValue) {
    if (dwPropertyID != 100) {
        Log("WARN", "GetProperty: unknown property ID %lu", dwPropertyID);
        return FALSE;
    }
    if (!pValue) {
        Log("ERROR", "GetProperty: pValue is NULL");
        return FALSE;
    }
    if (m_hCom == INVALID_HANDLE_VALUE) {
        Log("ERROR", "GetProperty: invalid handle");
        return FALSE;
    }

    DCB dcb = {0};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(m_hCom, &dcb)) {
        Log("ERROR", "GetProperty: GetCommState failed, error=%lu", GetLastError());
        return FALSE;
    }
    *(DWORD*)pValue = dcb.BaudRate;
    Log("DEBUG", "GetProperty: current baudrate = %lu", dcb.BaudRate);
    return TRUE;
}

// ------------------------------------------------------------
// ReadThreadProc（异步读取线程，复用读取重叠事件）
DWORD WINAPI CMySerialChannel::ReadThreadProc(LPVOID lpParam) {
    CMySerialChannel* pThis = static_cast<CMySerialChannel*>(lpParam);
    pThis->Log("INFO", "ReadThread started");

    if (!pThis->m_bOvInitialized) {
        pThis->Log("ERROR", "ReadThread: overlap not initialized");
        return 1;
    }

    OVERLAPPED waitOv = {0};
    waitOv.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!waitOv.hEvent) {
        pThis->Log("ERROR", "ReadThread: CreateEvent for WaitCommEvent failed");
        return 1;
    }

    if (!SetCommMask(pThis->m_hCom, EV_RXCHAR)) {
        pThis->Log("ERROR", "ReadThread: SetCommMask failed");
        CloseHandle(waitOv.hEvent);
        return 1;
    }

    while (pThis->m_bRunning) {
        if (!WaitCommEvent(pThis->m_hCom, NULL, &waitOv)) {
            if (GetLastError() == ERROR_IO_PENDING) {
                HANDLE waitHandles[2] = { waitOv.hEvent, pThis->m_hStopEvent };
                DWORD ret = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
                if (ret == WAIT_OBJECT_0) {
                    if (!pThis->m_bRunning) break;

                    // 循环读取直到缓冲区为空
                    while (pThis->m_bRunning) {
                        BYTE buffer[4096];
                        DWORD bytesRead = 0;
                        ResetEvent(pThis->m_hReadOvEvent);

                        if (!ReadFile(pThis->m_hCom, buffer, sizeof(buffer), &bytesRead, &pThis->m_readOv)) {
                            DWORD err = GetLastError();
                            if (err == ERROR_IO_PENDING) {
                                HANDLE readHandles[2] = { pThis->m_hReadOvEvent, pThis->m_hStopEvent };
                                DWORD readRet = WaitForMultipleObjects(2, readHandles, FALSE, INFINITE);
                                if (readRet == WAIT_OBJECT_0) {
                                    if (!GetOverlappedResult(pThis->m_hCom, &pThis->m_readOv, &bytesRead, FALSE)) {
                                        pThis->Log("ERROR", "ReadThread: GetOverlappedResult failed, error=%lu", GetLastError());
                                        break;
                                    }
                                } else {
                                    CancelIo(pThis->m_hCom);
                                    pThis->Log("DEBUG", "ReadThread: ReadFile cancelled by stop event");
                                    break;
                                }
                            } else {
                                pThis->Log("ERROR", "ReadThread: ReadFile failed, error=%lu", err);
                                break;
                            }
                        }

                        if (bytesRead == 0) break;

                        // 推入数据池
                        {
                            std::lock_guard<std::mutex> lock(pThis->m_queueMutex);
                            if (pThis->m_dataQueue.size() >= MAX_QUEUE_PACKETS) {
                                pThis->Log("WARN", "Queue overflow, dropping oldest packet (size=%zu)",
                                           pThis->m_dataQueue.size());
                                pThis->m_dataQueue.pop();
                            }
                            pThis->m_dataQueue.emplace(buffer, buffer + bytesRead);
                            pThis->m_dataAvailable.notify_one();
                        }

                        // 发送给外部（如果设置了异步模式）
                        if (pThis->m_bAsyncMode) {
                            BYTE* copy = (BYTE*)malloc(bytesRead);
                            if (copy) {
                                memcpy(copy, buffer, bytesRead);
                                if (pThis->m_bRcvThread) {
                                    PostThreadMessage((DWORD)(ULONG_PTR)pThis->m_hTargetWnd,
                                                      pThis->m_ulMsgId,
                                                      (WPARAM)copy, (LPARAM)bytesRead);
                                } else {
                                    PostMessage(pThis->m_hTargetWnd,
                                                pThis->m_ulMsgId,
                                                (WPARAM)copy, (LPARAM)bytesRead);
                                }
                            }
                        }
                    }
                } else {
                    // 停止事件触发
                    break;
                }
            } else {
                pThis->Log("ERROR", "ReadThread: WaitCommEvent failed, error=%lu", GetLastError());
                break;
            }
        } else {
            // WaitCommEvent 立即成功（罕见）
            while (pThis->m_bRunning) {
                BYTE buffer[4096];
                DWORD bytesRead = 0;
                ResetEvent(pThis->m_hReadOvEvent);

                if (ReadFile(pThis->m_hCom, buffer, sizeof(buffer), &bytesRead, &pThis->m_readOv) ||
                    (GetLastError() == ERROR_IO_PENDING &&
                     WaitForSingleObject(pThis->m_hReadOvEvent, INFINITE) == WAIT_OBJECT_0 &&
                     GetOverlappedResult(pThis->m_hCom, &pThis->m_readOv, &bytesRead, FALSE))) {
                    if (bytesRead == 0) break;
                    {
                        std::lock_guard<std::mutex> lock(pThis->m_queueMutex);
                        if (pThis->m_dataQueue.size() >= MAX_QUEUE_PACKETS) {
                        pThis->Log("WARN", "Queue overflow, dropping oldest packet (size=%zu)",
                                           pThis->m_dataQueue.size());
                            pThis->m_dataQueue.pop();
                        }
                        pThis->m_dataQueue.emplace(buffer, buffer + bytesRead);
                        pThis->m_dataAvailable.notify_one();
                    }
                    if (pThis->m_bAsyncMode) {
                        BYTE* copy = (BYTE*)malloc(bytesRead);
                        if (copy) {
                            memcpy(copy, buffer, bytesRead);
                            if (pThis->m_bRcvThread) {
                                PostThreadMessage((DWORD)(ULONG_PTR)pThis->m_hTargetWnd,
                                                  pThis->m_ulMsgId,
                                                  (WPARAM)copy, (LPARAM)bytesRead);
                            } else {
                                PostMessage(pThis->m_hTargetWnd,
                                            pThis->m_ulMsgId,
                                            (WPARAM)copy, (LPARAM)bytesRead);
                            }
                        }
                    }
                } else {
                    pThis->Log("ERROR", "ReadThread: immediate ReadFile failed, error=%lu", GetLastError());
                    break;
                }
            }
        }
    }

    CloseHandle(waitOv.hEvent);
    pThis->Log("INFO", "ReadThread exiting");
    return 0;
}