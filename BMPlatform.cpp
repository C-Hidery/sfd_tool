#include "BMPlatform.h"
#include <iostream>

int m_bOpened = 0;
const int MAX_WAIT_MS = 5000;   // 最多等5秒
const int SLEEP_MS = 100;       // 每次等待间隔
DWORD startTime = GetTickCount();
BOOL pipeReady = FALSE;
// ---------- CBMPlatformApp 实现 ----------
CBMPlatformApp::CBMPlatformApp() : m_hPipe(INVALID_HANDLE_VALUE), m_bConnected(FALSE) {
    InitializeCriticalSection(&m_cs);
}

CBMPlatformApp::~CBMPlatformApp() {
    if (m_hPipe != INVALID_HANDLE_VALUE)
        CloseHandle(m_hPipe);
    DeleteCriticalSection(&m_cs);
}

// 在 CBMPlatformApp::InitInstance 中，只启动代理，不等待管道
BOOL CBMPlatformApp::InitInstance() {
    WCHAR exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    WCHAR* p = wcsrchr(exePath, L'\\');
    if (p) *p = L'\0';

    WCHAR proxyPath[MAX_PATH];
    wcscpy_s(proxyPath, exePath);
    wcscat_s(proxyPath, L"\\Proxy32.exe");

    // 快速检查管道是否存在，如果存在则说明代理已在运行
    HANDLE hTest = CreateFileW(
        L"\\\\.\\pipe\\BMProxyPipe",
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED, NULL
    );
    if (hTest != INVALID_HANDLE_VALUE) {
        CloseHandle(hTest);
        return TRUE;  // 代理已运行
    }

    // 启动代理进程
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(proxyPath, NULL, NULL, NULL, FALSE, 0, NULL, exePath, &si, &pi)) {
        std::cout << "Failed to start proxy process. Error: " << GetLastError() << std::endl;
        return FALSE;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // 立即返回，不等待管道
    return TRUE;
}

// ExitInstance 只在连接存在时发送关闭命令
int CBMPlatformApp::ExitInstance() {
    if (m_bConnected && m_hPipe != INVALID_HANDLE_VALUE) {
        SendCommand(CMD_SHUTDOWN, NULL, 0, NULL, 0, NULL);
    }
    if (m_hPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hPipe);
        m_hPipe = INVALID_HANDLE_VALUE;
    }
    m_bConnected = FALSE;
    return TRUE;
}

// SendCommand 负责延迟连接管道
BOOL CBMPlatformApp::SendCommand(DWORD cmd, const void* param, DWORD paramSize,
                                 void* replyBuf, DWORD replySize, DWORD* replyActual) {
    EnterCriticalSection(&m_cs);

    // 如果尚未连接，尝试连接（带重试）
    if (!m_bConnected || m_hPipe == INVALID_HANDLE_VALUE) {
        const int MAX_WAIT_MS = 5000;
        const int SLEEP_MS = 100;
        DWORD start = GetTickCount();
        BOOL connected = FALSE;
        while (GetTickCount() - start < MAX_WAIT_MS) {
            HANDLE hPipe = CreateFileW(
                L"\\\\.\\pipe\\BMProxyPipe",
                GENERIC_READ | GENERIC_WRITE,
                0, NULL, OPEN_EXISTING,
                0, NULL  // 同步方式
            );
            if (hPipe != INVALID_HANDLE_VALUE) {
                DWORD mode = PIPE_READMODE_MESSAGE;
                SetNamedPipeHandleState(hPipe, &mode, NULL, NULL);
                m_hPipe = hPipe;
                m_bConnected = TRUE;
                connected = TRUE;
                break;
            }
            Sleep(SLEEP_MS);
        }
        if (!connected) {
            LeaveCriticalSection(&m_cs);
            return FALSE;
        }
    }

    // 发送命令码
    DWORD written;
    BOOL ok = WriteFile(m_hPipe, &cmd, sizeof(cmd), &written, NULL);
    if (ok && written == sizeof(cmd)) {
        if (param && paramSize) {
            ok = WriteFile(m_hPipe, param, paramSize, &written, NULL);
            if (!ok || written != paramSize) {
                LeaveCriticalSection(&m_cs);
                return FALSE;
            }
        }

        DWORD result;
        DWORD read;
        ok = ReadFile(m_hPipe, &result, sizeof(result), &read, NULL);
        if (ok && read == sizeof(result)) {
            if (replyActual) *replyActual = result;
            if (replyBuf && replySize) {
                DWORD toRead = replySize;
                BYTE* p = (BYTE*)replyBuf;
                while (toRead > 0) {
                    if (!ReadFile(m_hPipe, p, toRead, &read, NULL)) {
                        ok = FALSE;
                        break;
                    }
                    toRead -= read;
                    p += read;
                }
            }
        } else {
            ok = FALSE;
        }
    } else {
        ok = FALSE;
    }

    LeaveCriticalSection(&m_cs);
    return ok;
}

ICommChannel* CBMPlatformApp::CreateChannel(CHANNEL_TYPE type) {
    if (!m_bConnected) return NULL;
    CreateChannelParam param = { (DWORD)type };
    ULONG_PTR channelPtr = 0;
    if (SendCommand(CMD_CREATE_CHANNEL, &param, sizeof(param), &channelPtr, sizeof(channelPtr), NULL)) {
        return new CProxyChannel(this);
    }
    return NULL;
}

void CBMPlatformApp::ReleaseChannel(ICommChannel* pChannel) {
    if (pChannel) {
        ReleaseChannelParam param = { 0 };
        SendCommand(CMD_RELEASE_CHANNEL, &param, sizeof(param), NULL, 0, NULL);
        delete pChannel;
    }
}

CBMPlatformApp g_theApp;

// ---------- CProxyChannel 实现 ----------
CProxyChannel::CProxyChannel(CBMPlatformApp* pApp) : m_pApp(pApp) {}
CProxyChannel::~CProxyChannel() {}

BOOL CProxyChannel::InitLog(LPCWSTR pszLogName, UINT uiLogType, UINT uiLogLevel, ISpLog *pLogUtil, LPCWSTR pszBinLogFileExt) {
    InitLogParam param = {0};
    if (pszLogName) wcscpy_s(param.logName, pszLogName);
    param.logType = uiLogType;
    param.logLevel = uiLogLevel;
    param.pLogUtil = (ULONG_PTR)pLogUtil;
    if (pszBinLogFileExt) wcscpy_s(param.binExt, pszBinLogFileExt);
    DWORD ret;
    BOOL ok = m_pApp->SendCommand(CMD_INIT_LOG, &param, sizeof(param), &ret, sizeof(ret), NULL);
    return ok && ret;
}

BOOL CProxyChannel::SetReceiver(ULONG ulMsgId, BOOL bRcvThread, LPCVOID pReceiver) {
    SetReceiverParam param = { ulMsgId, bRcvThread, (ULONG_PTR)pReceiver };
    DWORD ret;
    BOOL ok = m_pApp->SendCommand(CMD_SET_RECEIVER, &param, sizeof(param), &ret, sizeof(ret), NULL);
    return ok && ret;
}

void CProxyChannel::GetReceiver(ULONG &ulMsgId, BOOL &bRcvThread, LPVOID &pReceiver) {
    GetReceiverParam param = { ulMsgId, bRcvThread, (ULONG_PTR)pReceiver };
    GetReceiverParam reply;
    BOOL ok = m_pApp->SendCommand(CMD_GET_RECEIVER, &param, sizeof(param), &reply, sizeof(reply), NULL);
    if (ok) {
        ulMsgId = reply.msgId;
        bRcvThread = reply.bRcvThread;
        pReceiver = (LPVOID)reply.receiver;
    }
}

BOOL CProxyChannel::Open(PCCHANNEL_ATTRIBUTE pOpenArgument) {
    OpenParam param;
    param.channelType = pOpenArgument->ChannelType;
    if (param.channelType == CHANNEL_TYPE_COM) {
        param.comPort = pOpenArgument->Com.dwPortNum;
        param.baudRate = pOpenArgument->Com.dwBaudRate;
    } else {
        return FALSE;
    }
    DWORD ret;
    BOOL ok = m_pApp->SendCommand(CMD_OPEN, &param, sizeof(param), &ret, sizeof(ret), NULL);
    return ok && ret;
}

void CProxyChannel::Close() {
    m_pApp->SendCommand(CMD_CLOSE, NULL, 0, NULL, 0, NULL);
}

BOOL CProxyChannel::Clear() {
    DWORD ret;
    BOOL ok = m_pApp->SendCommand(CMD_CLEAR, NULL, 0, &ret, sizeof(ret), NULL);
    return ok && ret;
}

DWORD CProxyChannel::Read(LPVOID lpData, DWORD dwDataSize, DWORD dwTimeOut, DWORD dwReserved) {
    ReadParam param = { 0, dwDataSize, dwTimeOut, dwReserved };
    DWORD readSize;
    BOOL ok = m_pApp->SendCommand(CMD_READ, &param, sizeof(param), lpData, dwDataSize, &readSize);
    return ok ? readSize : 0;
}

DWORD CProxyChannel::Write(LPVOID lpData, DWORD dwDataSize, DWORD dwReserved) {
    DWORD totalSize = sizeof(WriteParam) - 1 + dwDataSize;
    BYTE* buf = new BYTE[totalSize];
    WriteParam* p = (WriteParam*)buf;
    p->channelPtr = 0;
    p->dataLen = dwDataSize;
    if (dwDataSize > 0)
        memcpy(p->data, lpData, dwDataSize);
    DWORD written;
    BOOL ok = m_pApp->SendCommand(CMD_WRITE, buf, totalSize, &written, sizeof(written), NULL);
    delete[] buf;
    return ok ? written : 0;
}

void CProxyChannel::FreeMem(LPVOID pMemBlock) {
    FreeMemParam param = { (ULONG_PTR)pMemBlock };
    m_pApp->SendCommand(CMD_FREE_MEM, &param, sizeof(param), NULL, 0, NULL);
}

BOOL CProxyChannel::GetProperty(LONG lFlags, DWORD dwPropertyID, LPVOID pValue) {
    GetPropertyParam param = { 0, lFlags, dwPropertyID, sizeof(DWORD) }; // 假设属性值为 DWORD
    DWORD value = 0;
    BOOL ok = m_pApp->SendCommand(CMD_GET_PROPERTY, &param, sizeof(param), &value, sizeof(value), NULL);
    if (ok && pValue) *(DWORD*)pValue = value;
    return ok;
}

BOOL CProxyChannel::SetProperty(LONG lFlags, DWORD dwPropertyID, LPCVOID pValue) {
    DWORD dataSize = sizeof(DWORD); // 假设属性值为 DWORD
    DWORD totalSize = sizeof(SetPropertyParam) - 1 + dataSize;
    BYTE* buf = new BYTE[totalSize];
    SetPropertyParam* p = (SetPropertyParam*)buf;
    p->channelPtr = 0;
    p->flags = lFlags;
    p->propId = dwPropertyID;
    p->dataSize = dataSize;
    if (pValue) memcpy(p->data, pValue, dataSize);
    DWORD ret;
    BOOL ok = m_pApp->SendCommand(CMD_SET_PROPERTY, buf, totalSize, &ret, sizeof(ret), NULL);
    delete[] buf;
    return ok && ret;
}

// ---------- CBootModeOpr 实现（与原来相同，只是调用已修改的全局对象）----------
CBootModeOpr::CBootModeOpr() : m_pChannel(NULL) {
    g_theApp.InitInstance();
}

CBootModeOpr::~CBootModeOpr() {
    g_theApp.ExitInstance();
}

BOOL CBootModeOpr::Initialize() {
    m_pChannel = g_theApp.CreateChannel(CHANNEL_TYPE_COM);
    return (m_pChannel != NULL);
}

void CBootModeOpr::Uninitialize() {
    if (m_pChannel) {
        g_theApp.ReleaseChannel(m_pChannel);
        m_pChannel = NULL;
    }
}

int CBootModeOpr::Read(UCHAR *m_RecvData, int max_len, int dwTimeout) {
    if (!m_pChannel) return 0;
    ULONGLONG tBegin = GetTickCount64();
    ULONGLONG tCur;
    do {
        tCur = GetTickCount64();
        DWORD dwRead = m_pChannel->Read(m_RecvData, max_len, dwTimeout);
        if (dwRead) return dwRead;
    } while ((tCur - tBegin) < (ULONGLONG)dwTimeout);
    return 0;
}

int CBootModeOpr::Write(UCHAR *lpData, int iDataSize) {
    if (!m_pChannel) return 0;
    return m_pChannel->Write(lpData, iDataSize);
}

BOOL CBootModeOpr::GetProperty(LONG lFlags, DWORD dwPropertyID, LPVOID pValue) {
    if (!m_pChannel) return FALSE;
    return m_pChannel->GetProperty(lFlags, dwPropertyID, pValue);
}

BOOL CBootModeOpr::SetProperty(LONG lFlags, DWORD dwPropertyID, LPCVOID pValue) {
    if (!m_pChannel) return FALSE;
    return m_pChannel->SetProperty(lFlags, dwPropertyID, pValue);
}

BOOL CBootModeOpr::ConnectChannel(DWORD dwPort, ULONG ulMsgId, DWORD Receiver) {
    if (!dwPort) return FALSE;
    if (!m_pChannel) return FALSE;

    if (Receiver) m_pChannel->SetReceiver(ulMsgId, TRUE, (LPVOID)Receiver);
    CHANNEL_ATTRIBUTE ca;
    ca.ChannelType = CHANNEL_TYPE_COM;
    ca.Com.dwPortNum = dwPort;
    ca.Com.dwBaudRate = 115200;

    m_bOpened = m_pChannel->Open(&ca);
    if (m_bOpened) std::cout << "Successfully connected to port: " << dwPort << std::endl;
    return m_bOpened;
}

BOOL CBootModeOpr::DisconnectChannel() {
    if (m_pChannel) {
        m_pChannel->Close();
        m_bOpened = 0;
    }
    return TRUE;
}

void CBootModeOpr::Clear() {
    if (m_pChannel) m_pChannel->Clear();
}

void CBootModeOpr::FreeMem(LPVOID pMemBlock) {
    if (m_pChannel) m_pChannel->FreeMem(pMemBlock);
}