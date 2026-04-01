#include "BMPlatform.h"
#include <iostream>

// 全局变量
int m_bOpened = 0;   // 定义

// 管道命令码（与代理一致）
enum ProxyCommand {
    CMD_CREATE_CHANNEL,
    CMD_RELEASE_CHANNEL,
    CMD_INIT_LOG,
    CMD_SET_RECEIVER,
    CMD_GET_RECEIVER,
    CMD_OPEN,
    CMD_CLOSE,
    CMD_CLEAR,
    CMD_READ,
    CMD_WRITE,
    CMD_FREE_MEM,
    CMD_GET_PROPERTY,
    CMD_SET_PROPERTY
};

// 辅助结构体（用于序列化，与代理端保持一致）
#pragma pack(push, 1)
struct CreateChannelParam {
    DWORD channelType;
};
struct ReleaseChannelParam {
    DWORD channelPtr;
};
struct InitLogParam {
    WCHAR logName[260];
    UINT logType;
    UINT logLevel;
    DWORD pLogUtil;
    WCHAR binExt[10];
};
struct SetReceiverParam {
    ULONG msgId;
    BOOL bRcvThread;
    DWORD receiver;
};
struct GetReceiverParam {
    ULONG msgId;
    BOOL bRcvThread;
    DWORD receiver;
};
struct OpenParam {
    DWORD channelType;
    DWORD comPort;
    DWORD baudRate;
};
struct ReadParam {
    DWORD channelPtr;
    DWORD maxLen;
    DWORD timeout;
    DWORD reserved;
};
struct WriteParam {
    DWORD channelPtr;
    DWORD dataLen;
    BYTE data[1];
};
struct FreeMemParam {
    DWORD pMemBlock;
};
struct GetPropertyParam {
    DWORD channelPtr;
    LONG flags;
    DWORD propId;
    DWORD bufSize;
};
struct SetPropertyParam {
    DWORD channelPtr;
    LONG flags;
    DWORD propId;
    DWORD dataSize;
    BYTE data[1];
};
#pragma pack(pop)

// ---------- CProxyChannel 实现 ----------
CProxyChannel::CProxyChannel(HANDLE hPipe) : m_hPipe(hPipe) {
    DuplicateHandle(GetCurrentProcess(), hPipe, GetCurrentProcess(), &m_hPipe, 0, FALSE, DUPLICATE_SAME_ACCESS);
}

CProxyChannel::~CProxyChannel() {
    if (m_hPipe && m_hPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hPipe);
        m_hPipe = NULL;
    }
}

// 发送命令并等待响应
BOOL CProxyChannel::SendCommand(DWORD cmd, const void* param, DWORD paramSize, void* replyBuf, DWORD replySize, DWORD* replyActual) {
    if (m_hPipe == INVALID_HANDLE_VALUE) return FALSE;
    DWORD written;
    // 发送命令码
    if (!WriteFile(m_hPipe, &cmd, sizeof(cmd), &written, NULL) || written != sizeof(cmd))
        return FALSE;
    // 发送参数（如果有）
    if (param && paramSize) {
        if (!WriteFile(m_hPipe, param, paramSize, &written, NULL) || written != paramSize)
            return FALSE;
    }
    // 读取返回的 DWORD 结果（通常第一个 DWORD 表示成功/失败或数据大小）
    DWORD result;
    DWORD read;
    if (!ReadFile(m_hPipe, &result, sizeof(result), &read, NULL) || read != sizeof(result))
        return FALSE;
    // 如果提供了 replyBuf，继续读取后续数据
    if (replyBuf && replySize) {
        DWORD toRead = replySize;
        BYTE* p = (BYTE*)replyBuf;
        while (toRead > 0) {
            if (!ReadFile(m_hPipe, p, toRead, &read, NULL)) return FALSE;
            toRead -= read;
            p += read;
        }
    }
    if (replyActual) *replyActual = result;
    return TRUE;
}

BOOL CProxyChannel::InitLog(LPCWSTR pszLogName, UINT uiLogType, UINT uiLogLevel, ISpLog *pLogUtil, LPCWSTR pszBinLogFileExt) {
    InitLogParam param = {0};
    if (pszLogName) wcscpy_s(param.logName, pszLogName);
    param.logType = uiLogType;
    param.logLevel = uiLogLevel;
    param.pLogUtil = (DWORD)pLogUtil;
    if (pszBinLogFileExt) wcscpy_s(param.binExt, pszBinLogFileExt);
    DWORD ret;
    BOOL ok = SendCommand(CMD_INIT_LOG, &param, sizeof(param), &ret, sizeof(ret), NULL);
    return ok && ret;
}

BOOL CProxyChannel::SetReceiver(ULONG ulMsgId, BOOL bRcvThread, LPCVOID pReceiver) {
    SetReceiverParam param = { ulMsgId, bRcvThread, (DWORD)pReceiver };
    DWORD ret;
    BOOL ok = SendCommand(CMD_SET_RECEIVER, &param, sizeof(param), &ret, sizeof(ret), NULL);
    return ok && ret;
}

void CProxyChannel::GetReceiver(ULONG &ulMsgId, BOOL &bRcvThread, LPVOID &pReceiver) {
    GetReceiverParam param = { ulMsgId, bRcvThread, (DWORD)pReceiver };
    GetReceiverParam reply;
    BOOL ok = SendCommand(CMD_GET_RECEIVER, &param, sizeof(param), &reply, sizeof(reply), NULL);
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
        // 其他类型未实现，可扩展
        return FALSE;
    }
    DWORD ret;
    BOOL ok = SendCommand(CMD_OPEN, &param, sizeof(param), &ret, sizeof(ret), NULL);
    return ok && ret;
}

void CProxyChannel::Close() {
    SendCommand(CMD_CLOSE, NULL, 0, NULL, 0, NULL);
}

BOOL CProxyChannel::Clear() {
    DWORD ret;
    BOOL ok = SendCommand(CMD_CLEAR, NULL, 0, &ret, sizeof(ret), NULL);
    return ok && ret;
}

DWORD CProxyChannel::Read(LPVOID lpData, DWORD dwDataSize, DWORD dwTimeOut, DWORD dwReserved) {
    ReadParam param = { 0, dwDataSize, dwTimeOut, dwReserved };  // channelPtr 在代理端不需要，此处占位
    DWORD readSize;
    BOOL ok = SendCommand(CMD_READ, &param, sizeof(param), lpData, dwDataSize, &readSize);
    if (ok) return readSize;
    return 0;
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
    BOOL ok = SendCommand(CMD_WRITE, buf, totalSize, &written, sizeof(written), NULL);
    delete[] buf;
    return ok ? written : 0;
}

void CProxyChannel::FreeMem(LPVOID pMemBlock) {
    FreeMemParam param = { (DWORD)pMemBlock };
    SendCommand(CMD_FREE_MEM, &param, sizeof(param), NULL, 0, NULL);
}

BOOL CProxyChannel::GetProperty(LONG lFlags, DWORD dwPropertyID, LPVOID pValue) {
    GetPropertyParam param = { 0, lFlags, dwPropertyID, 0 }; // channelPtr 忽略
    // 假设 pValue 指向一个 DWORD 大小的值，实际可根据属性调整
    DWORD value = 0;
    BOOL ok = SendCommand(CMD_GET_PROPERTY, &param, sizeof(param), &value, sizeof(value), NULL);
    if (ok && pValue) *(DWORD*)pValue = value;
    return ok;
}

BOOL CProxyChannel::SetProperty(LONG lFlags, DWORD dwPropertyID, LPCVOID pValue) {
    DWORD dataSize = sizeof(DWORD); // 假设属性值大小为 DWORD，可根据实际调整
    DWORD totalSize = sizeof(SetPropertyParam) - 1 + dataSize;
    BYTE* buf = new BYTE[totalSize];
    SetPropertyParam* p = (SetPropertyParam*)buf;
    p->channelPtr = 0;
    p->flags = lFlags;
    p->propId = dwPropertyID;
    p->dataSize = dataSize;
    if (pValue) memcpy(p->data, pValue, dataSize);
    DWORD ret;
    BOOL ok = SendCommand(CMD_SET_PROPERTY, buf, totalSize, &ret, sizeof(ret), NULL);
    delete[] buf;
    return ok && ret;
}

// ---------- CBMPlatformApp 实现 ----------
CBMPlatformApp::CBMPlatformApp() : m_hPipe(INVALID_HANDLE_VALUE), m_bConnected(FALSE) {
}

CBMPlatformApp::~CBMPlatformApp() {
    if (m_hPipe != INVALID_HANDLE_VALUE)
        CloseHandle(m_hPipe);
}

BOOL CBMPlatformApp::InitInstance() {
    // 连接到代理进程的命名管道
    m_hPipe = CreateFileW(
        L"\\\\.\\pipe\\BMProxyPipe",
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED, NULL
    );
    if (m_hPipe == INVALID_HANDLE_VALUE) {
        std::cout << "Failed to connect to proxy pipe. Error: " << GetLastError() << std::endl;
        return FALSE;
    }
    // 设置为消息模式
    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(m_hPipe, &mode, NULL, NULL);
    m_bConnected = TRUE;
    return TRUE;
}

int CBMPlatformApp::ExitInstance() {
    if (m_hPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hPipe);
        m_hPipe = INVALID_HANDLE_VALUE;
        m_bConnected = FALSE;
    }
    return TRUE;
}

ICommChannel* CBMPlatformApp::CreateChannel(CHANNEL_TYPE type) {
    if (!m_bConnected) return NULL;
    CreateChannelParam param = { (DWORD)type };
    DWORD channelPtr = 0;
    BOOL ok = FALSE;
    // 发送命令并期望返回一个指针值（在代理进程中）—— 但我们实际上不需要这个值，因为代理内部自己维护
    // 这里我们只是返回一个 CProxyChannel 对象
    if (SendCommand(CMD_CREATE_CHANNEL, &param, sizeof(param), &channelPtr, sizeof(channelPtr), NULL)) {
        // 创建代理通道对象，传递管道句柄的副本
        HANDLE hDup;
        DuplicateHandle(GetCurrentProcess(), m_hPipe, GetCurrentProcess(), &hDup, 0, FALSE, DUPLICATE_SAME_ACCESS);
        return new CProxyChannel(hDup);
    }
    return NULL;
}

void CBMPlatformApp::ReleaseChannel(ICommChannel* pChannel) {
    if (pChannel) {
        // 向代理发送释放命令
        ReleaseChannelParam param = { 0 };
        SendCommand(CMD_RELEASE_CHANNEL, &param, sizeof(param), NULL, 0, NULL);
        delete pChannel;
    }
}

CBMPlatformApp g_theApp;

// ---------- CBootModeOpr 实现 ----------
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
        if (dwRead) {
            return dwRead;
        }
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