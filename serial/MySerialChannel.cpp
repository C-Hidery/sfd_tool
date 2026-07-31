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
// SetReceiver（仅记录设置，不启动线程，线程在 Open 中由 m_bAsyncMode 决定）
BOOL CMySerialChannel::SetReceiver(ULONG ulMsgId, BOOL bRcvThread, LPCVOID pReceiver) {
    Log("INFO", "SetReceiver: MsgId=0x%08lX, bRcvThread=%d, pReceiver=%p", ulMsgId, bRcvThread, pReceiver);
    m_hTargetWnd = (HWND)pReceiver;
    m_ulMsgId = ulMsgId;
    m_bAsyncMode = true;  // 标记异步模式
    return TRUE;
}

void CMySerialChannel::GetReceiver(ULONG &ulMsgId, BOOL &bRcvThread, LPVOID &pReceiver) {
    ulMsgId = m_ulMsgId;
    bRcvThread = FALSE;
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
// Close
void CMySerialChannel::Close() {
    Log("INFO", "Closing COM port");
    m_bRunning = false;

    // 取消挂起的 I/O
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

    Log("DEBUG", "Read: size=%lu, timeout=%lu ms", dwDataSize, dwTimeOut);

    // 等待数据池有数据
    std::unique_lock<std::mutex> lock(m_queueMutex);
    bool hasData = m_dataAvailable.wait_for(lock, std::chrono::milliseconds(dwTimeOut),
        [this]() { return !m_dataQueue.empty() || !m_bRunning; });

    if (!hasData || !m_bRunning) {
        Log("DEBUG", "Read timeout or stopped");
        return 0;
    }

    // 取出一块数据
    auto& front = m_dataQueue.front();
    DWORD bytesToCopy = (DWORD)front.size();
    if (bytesToCopy > dwDataSize) bytesToCopy = dwDataSize;
    memcpy(lpData, front.data(), bytesToCopy);

    Log("DEBUG", "Read returned %lu bytes (total available %zu)", bytesToCopy, front.size());
    m_dataQueue.pop();
    // 如果队列还有更多数据，继续通知（但本次已取出，后续读会再触发）
    // 不额外通知，因为下次 Read 会再次等待

    return bytesToCopy;
}

// ------------------------------------------------------------
// Write
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
// ReadThreadProc（异步读取线程，推入队列）
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
                        // 推入队列
                        std::lock_guard<std::mutex> lock(pThis->m_queueMutex);
                        pThis->m_dataQueue.emplace(buffer, buffer + bytesRead);
                        pThis->m_dataAvailable.notify_one();
                    }
                } else {
                    break;
                }
            } else {
                pThis->Log("ERROR", "ReadThread: WaitCommEvent failed, error %lu", GetLastError());
                break;
            }
        } else {
            // 立即成功（极少）
            BYTE buffer[4096];
            DWORD bytesRead;
            if (ReadFile(pThis->m_hCom, buffer, sizeof(buffer), &bytesRead, NULL) &&
                bytesRead > 0) {
                std::lock_guard<std::mutex> lock(pThis->m_queueMutex);
                pThis->m_dataQueue.emplace(buffer, buffer + bytesRead);
                pThis->m_dataAvailable.notify_one();
            }
        }
    }

    CloseHandle(ov.hEvent);
    pThis->Log("INFO", "ReadThread exiting");
    return 0;
}