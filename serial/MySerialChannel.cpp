// MySerialChannel.cpp
#include "MySerialChannel.h"
#include "../core/app_state.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>

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
    , m_bRcvThread(FALSE)
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
    m_ulMsgId = ulMsgId;
    m_bRcvThread = bRcvThread;
    if (bRcvThread) {
        // pReceiver 是线程 ID (DWORD)，强制转换为 DWORD 存储为句柄
        m_hTargetWnd = (HWND)(ULONG_PTR)pReceiver; // 为了存储方便，实际使用时强制转换
    } else {
        m_hTargetWnd = (HWND)pReceiver;
    }
    m_bAsyncMode = (m_ulMsgId != 0 && pReceiver != NULL);
    return TRUE;
}

void CMySerialChannel::GetReceiver(ULONG &ulMsgId, BOOL &bRcvThread, LPVOID &pReceiver) {
    ulMsgId = m_ulMsgId;
    bRcvThread = m_bRcvThread;
    pReceiver = (LPVOID)m_hTargetWnd;
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

    // 清空队列
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
// Read（同步读取，从数据池取数据）
DWORD CMySerialChannel::Read(LPVOID lpData, DWORD dwDataSize, DWORD dwTimeOut, DWORD /*dwReserved*/) {
    if (m_hCom == INVALID_HANDLE_VALUE || !lpData || dwDataSize == 0) {
        Log("WARN", "Read invalid parameters");
        return 0;
    }

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
// Write
DWORD CMySerialChannel::Write(LPVOID lpData, DWORD dwDataSize, DWORD /*dwReserved*/) {
    if (m_hCom == INVALID_HANDLE_VALUE || !lpData || dwDataSize == 0) {
        Log("WARN", "Write invalid parameters");
        return 0;
    }

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
    ct.ReadTotalTimeoutMultiplier = 0;
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
        pThis->Log("DEBUG", "ReadThread: Loop: Waiting for communication event");
        if (!WaitCommEvent(pThis->m_hCom, NULL, &ov)) {
            if (GetLastError() == ERROR_IO_PENDING) {
                HANDLE waitHandles[2] = { ov.hEvent, pThis->m_hStopEvent };
                DWORD ret = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
                if (ret == WAIT_OBJECT_0) {
                    if (!pThis->m_bRunning) break;
                    pThis->Log("DEBUG", "ReadThread: WaitForMultipleObjects completed");
                    // 读取数据
                    BYTE buffer[4096];
                    DWORD bytesRead = 0;
                    if (ReadFile(pThis->m_hCom, buffer, sizeof(buffer), &bytesRead, NULL) &&
                        bytesRead > 0) {
                        // 1. 推入队列（供同步 Read 使用）
                        {
                            pThis->Log("DEBUG", "ReadThread: loop: bytesRead %lu", bytesRead);
                            std::lock_guard<std::mutex> lock(pThis->m_queueMutex);
                            pThis->m_dataQueue.emplace(buffer, buffer + bytesRead);
                            pThis->m_dataAvailable.notify_one();
                        }

                        // 2. 如果设置了接收目标，发送原始数据给外部
                        if (pThis->m_bAsyncMode) {
                            // 分配内存供外部释放（模拟 Channel9 行为）
                            BYTE* copy = (BYTE*)malloc(bytesRead);
                            if (copy) {
                                memcpy(copy, buffer, bytesRead);
                                // 根据 bRcvThread 选择发送方式
                                if (pThis->m_bRcvThread) {
                                    // 发送到线程
                                    PostThreadMessage((DWORD)(ULONG_PTR)pThis->m_hTargetWnd,
                                                      pThis->m_ulMsgId,
                                                      (WPARAM)copy, (LPARAM)bytesRead);
                                    pThis->Log("DEBUG", "ReadThread: PostThreadMessage completed");
                                } else {
                                    // 发送到窗口
                                    PostMessage(pThis->m_hTargetWnd,
                                                pThis->m_ulMsgId,
                                                (WPARAM)copy, (LPARAM)bytesRead);
                                    pThis->Log("DEBUG", "ReadThread: PostMessage completed");
                                }
                            }
                        }
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
            pThis->Log("ERROR", "ReadThread: WaitCommEvent completed immediately");
            // WaitCommEvent 立即成功（罕见）
            BYTE buffer[4096];
            DWORD bytesRead;
            if (ReadFile(pThis->m_hCom, buffer, sizeof(buffer), &bytesRead, NULL) &&
                bytesRead > 0) {
                {
                    pThis->Log("DEBUG", "ReadThread: loop: immediately: bytesRead %lu", bytesRead);
                    std::lock_guard<std::mutex> lock(pThis->m_queueMutex);
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
                            pThis->Log("DEBUG", "ReadThread: immediately: PostThreadMessage completed");
                        } else {
                            PostMessage(pThis->m_hTargetWnd,
                                        pThis->m_ulMsgId,
                                        (WPARAM)copy, (LPARAM)bytesRead);
                            pThis->Log("DEBUG", "ReadThread: immediately: PostMessage completed");
                        }
                    }
                }
            }
        }
    }

    CloseHandle(ov.hEvent);
    pThis->Log("INFO", "ReadThread exiting");
    return 0;
}