// BMPlatform_Proxy.cpp - 代理通道实现
#include "BMPlatform.h"
#include <windows.h>
#include <cstring>
#include <iostream>
#include <sstream>
#include <atomic>
#include <map>

// 管道命令定义
enum ProxyCommand {
    CMD_CREATE_CHANNEL = 1,
    CMD_RELEASE_CHANNEL,
    CMD_OPEN,
    CMD_CLOSE,
    CMD_READ,
    CMD_WRITE,
    CMD_CLEAR,
    CMD_GET_PROPERTY,
    CMD_SET_PROPERTY,
    CMD_SET_RECEIVER,
    CMD_GET_RECEIVER,
    CMD_INIT_LOG,
    CMD_FREE_MEM,
    CMD_SHUTDOWN
};

// 异步事件类型
enum AsyncEventType {
    EVENT_ASYNC_DATA = 1,
};

#pragma pack(push, 1)
struct CommandPacket {
    DWORD cmd;
    DWORD objectId;
    DWORD param1;
    DWORD param2;
    DWORD param3;
    DWORD param4;
    DWORD dataSize;
    BYTE data[4096];
};

struct ResponsePacket {
    BOOL success;
    DWORD result;
    DWORD dataSize;
    BYTE data[4096];
};

struct AsyncEventPacket {
    DWORD eventType;
    DWORD objectId;
    DWORD ulMsgId;
    DWORD dataSize;
    BYTE data[4096];
};
#pragma pack(pop)

// SetReceiver 参数结构
struct SetReceiverParams {
    ULONG ulMsgId;
    BOOL bRcvThread;
    DWORD dwReceiver;
    HANDLE hEventPipeWrite;
};

// 全局进程ID计数器
static std::atomic<DWORD> g_pipeIdCounter(1);

CProxyChannel::CProxyChannel() 
    : m_hPipe(INVALID_HANDLE_VALUE), 
      m_objectId(0), 
      m_hProxyProcess(NULL), 
      m_bConnected(FALSE),
      m_currentPort(0),
      m_hEventPipe(INVALID_HANDLE_VALUE),
      m_hEventPipeWrite(INVALID_HANDLE_VALUE),
      m_hEventThread(NULL),
      m_bStopEventThread(FALSE),
      m_ulMsgId(0),
      m_bRcvThread(FALSE),
      m_dwReceiver(0) {
}

CProxyChannel::~CProxyChannel() {
    m_bStopEventThread = TRUE;
    
    if (m_hEventPipe) {
        DWORD dummy = 0;
        WriteFile(m_hEventPipeWrite, &dummy, 1, &dummy, NULL);
        if (m_hEventThread) {
            WaitForSingleObject(m_hEventThread, 5000);
            CloseHandle(m_hEventThread);
        }
        CloseHandle(m_hEventPipe);
        CloseHandle(m_hEventPipeWrite);
    }
    
    if (m_hPipe != INVALID_HANDLE_VALUE) {
        CommandPacket cmd = {0};
        cmd.cmd = CMD_SHUTDOWN;
        cmd.objectId = m_objectId;
        DWORD bytesWritten;
        WriteFile(m_hPipe, &cmd, sizeof(cmd), &bytesWritten, NULL);
        CloseHandle(m_hPipe);
    }
    
    if (m_hProxyProcess) {
        WaitForSingleObject(m_hProxyProcess, 5000);
        CloseHandle(m_hProxyProcess);
    }
}

DWORD WINAPI CProxyChannel::AsyncEventThreadProc(LPVOID lpParam) {
    CProxyChannel* pThis = (CProxyChannel*)lpParam;
    HANDLE hPipe = pThis->m_hEventPipe;
    
    while (!pThis->m_bStopEventThread) {
        AsyncEventPacket pkt;
        DWORD bytesRead;
        
        if (!ReadFile(hPipe, &pkt, sizeof(pkt), &bytesRead, NULL) || bytesRead == 0) {
            if (pThis->m_bStopEventThread) break;
            continue;
        }
        
        if (pkt.eventType == EVENT_ASYNC_DATA) {
            // 将数据通过 Windows 消息发送给原始接收者
            if (pThis->m_bRcvThread) {
                // 向线程发送消息
                PostThreadMessage(pThis->m_dwReceiver, pThis->m_ulMsgId,
                                  (WPARAM)pkt.data, (LPARAM)pkt.dataSize);
            } else {
                // 向窗口发送消息
                PostMessage((HWND)(UINT_PTR)pThis->m_dwReceiver, pThis->m_ulMsgId,
                            (WPARAM)pkt.data, (LPARAM)pkt.dataSize);
            }
        }
    }
    return 0;
}

BOOL CProxyChannel::ConnectToProxy() {
    if (m_bConnected && m_hPipe != INVALID_HANDLE_VALUE) {
        return TRUE;
    }
    
    // 创建事件管道（可继承）
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    if (!CreatePipe(&m_hEventPipe, &m_hEventPipeWrite, &sa, 0)) {
        std::cerr << "Failed to create event pipe" << std::endl;
        return FALSE;
    }
    
    DWORD pipeId = g_pipeIdCounter++;
    std::string pipeName = "\\\\.\\pipe\\SPRDChannelProxy_" + std::to_string(pipeId);
    
    char exePath[MAX_PATH];
    char workingDir[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    char* lastSlash = strrchr(exePath, '\\');
    if (lastSlash) {
        *(lastSlash + 1) = '\0';
    }
    strcpy_s(workingDir, exePath);
    
    char proxyPath[MAX_PATH];
    strcpy_s(proxyPath, workingDir);
    strcat_s(proxyPath, "Proxy32.exe");
    
    char cmdLine[MAX_PATH * 2];
    snprintf(cmdLine, sizeof(cmdLine), "\"%s\" %d %d", proxyPath, pipeId, (int)m_hEventPipeWrite);
    
    std::cout << "Starting Proxy32.exe with pipe ID: " << pipeId << std::endl;
    
    if (GetFileAttributesA(proxyPath) == INVALID_FILE_ATTRIBUTES) {
        std::cerr << "Proxy32.exe not found at: " << proxyPath << std::endl;
        return FALSE;
    }
    
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    if (!CreateProcessA(proxyPath, cmdLine, NULL, NULL, TRUE, 0, NULL, workingDir, &si, &pi)) {
        DWORD error = GetLastError();
        std::cerr << "Failed to start Proxy32.exe. Error: " << error << std::endl;
        return FALSE;
    }
    
    m_hProxyProcess = pi.hProcess;
    CloseHandle(pi.hThread);
    
    Sleep(500);
    
    for (int i = 0; i < 15; i++) {
        m_hPipe = CreateFileA(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE,
                              0, NULL, OPEN_EXISTING, 0, NULL);
        if (m_hPipe != INVALID_HANDLE_VALUE) {
            DWORD pipeMode = PIPE_READMODE_MESSAGE;
            SetNamedPipeHandleState(m_hPipe, &pipeMode, NULL, NULL);
            
            CommandPacket cmd = {0};
            cmd.cmd = CMD_CREATE_CHANNEL;
            ResponsePacket resp;
            DWORD bytesWritten, bytesRead;
            if (WriteFile(m_hPipe, &cmd, sizeof(cmd), &bytesWritten, NULL) &&
                ReadFile(m_hPipe, &resp, sizeof(resp), &bytesRead, NULL) &&
                resp.success) {
                m_objectId = resp.result;
                m_bConnected = TRUE;
                std::cout << "Connected to Proxy32, object ID: " << m_objectId << std::endl;
                
                // 启动事件处理线程
                m_hEventThread = CreateThread(NULL, 0, AsyncEventThreadProc, this, 0, NULL);
                return TRUE;
            }
            CloseHandle(m_hPipe);
            m_hPipe = INVALID_HANDLE_VALUE;
        }
        Sleep(200);
    }
    
    std::cerr << "Failed to connect to Proxy32 after starting it" << std::endl;
    return FALSE;
}

BOOL CProxyChannel::SendCommand(DWORD cmd, void* params, DWORD paramSize, void* resp, DWORD respSize) {
    if (!ConnectToProxy()) {
        return FALSE;
    }
    
    std::lock_guard<std::mutex> lock(m_pipeMutex);
    
    CommandPacket packet = {0};
    packet.cmd = cmd;
    packet.objectId = m_objectId;
    if (params && paramSize > 0 && paramSize <= sizeof(packet.data)) {
        packet.dataSize = paramSize;
        memcpy(packet.data, params, paramSize);
    }
    
    DWORD bytesWritten;
    if (!WriteFile(m_hPipe, &packet, sizeof(packet), &bytesWritten, NULL)) {
        return FALSE;
    }
    
    ResponsePacket response;
    DWORD bytesRead;
    if (!ReadFile(m_hPipe, &response, sizeof(response), &bytesRead, NULL)) {
        return FALSE;
    }
    
    if (resp && respSize > 0 && response.dataSize > 0) {
        DWORD copySize = min(respSize, response.dataSize);
        memcpy(resp, response.data, copySize);
    }
    
    return response.success;
}

BOOL CProxyChannel::InitLog(LPCWSTR pszLogName, UINT uiLogType, UINT uiLogLevel, 
                            ISpLog *pLogUtil, LPCWSTR pszBinLogFileExt) {
    return TRUE;
}

BOOL CProxyChannel::SetReceiver(ULONG ulMsgId, BOOL bRcvThread, LPCVOID pReceiver) {
    // 记录接收者信息
    m_ulMsgId = ulMsgId;
    m_bRcvThread = bRcvThread;
    m_dwReceiver = (DWORD)(ULONG_PTR)pReceiver;
    
    SetReceiverParams params;
    params.ulMsgId = ulMsgId;
    params.bRcvThread = bRcvThread;
    params.dwReceiver = m_dwReceiver;
    params.hEventPipeWrite = m_hEventPipeWrite;
    
    return SendCommand(CMD_SET_RECEIVER, &params, sizeof(params), NULL, 0);
}

void CProxyChannel::GetReceiver(ULONG &ulMsgId, BOOL &bRcvThread, LPVOID &pReceiver) {
    ulMsgId = m_ulMsgId;
    bRcvThread = m_bRcvThread;
    pReceiver = (LPVOID)(ULONG_PTR)m_dwReceiver;
}

BOOL CProxyChannel::Open(PCCHANNEL_ATTRIBUTE pOpenArgument) {
    if (!pOpenArgument) return FALSE;
    
    DWORD portNum = pOpenArgument->Com.dwPortNum;
    
    // 如果已经打开了同一个端口，直接返回成功
    if (m_bConnected && m_currentPort == portNum) {
        std::cout << "Port COM" << portNum << " already opened in this channel, reusing." << std::endl;
        return TRUE;
    }
    
    BOOL result = SendCommand(CMD_OPEN, (void*)pOpenArgument, sizeof(CHANNEL_ATTRIBUTE), NULL, 0);
    if (result) {
        m_bConnected = TRUE;
        m_currentPort = portNum;
    }
    return result;
}

void CProxyChannel::Close() {
    SendCommand(CMD_CLOSE, NULL, 0, NULL, 0);
    m_bConnected = FALSE;
    m_currentPort = 0;
}

BOOL CProxyChannel::Clear() {
    BOOL result;
    if (SendCommand(CMD_CLEAR, NULL, 0, &result, sizeof(result))) {
        return result;
    }
    return FALSE;
}

DWORD CProxyChannel::Write(LPVOID lpData, DWORD dwDataSize, DWORD dwReserved) {
    if (!ConnectToProxy()) {
        return 0;
    }
    
    std::lock_guard<std::mutex> lock(m_pipeMutex);
    
    CommandPacket packet = {0};
    packet.cmd = CMD_WRITE;
    packet.objectId = m_objectId;
    packet.dataSize = dwDataSize;
    if (dwDataSize > 0 && lpData && dwDataSize <= sizeof(packet.data)) {
        memcpy(packet.data, lpData, dwDataSize);
    }
    
    DWORD bytesWritten;
    if (!WriteFile(m_hPipe, &packet, sizeof(packet), &bytesWritten, NULL)) {
        return 0;
    }
    
    ResponsePacket response;
    DWORD bytesRead;
    if (!ReadFile(m_hPipe, &response, sizeof(response), &bytesRead, NULL)) {
        return 0;
    }
    
    if (response.success) {
        return response.result;
    }
    return 0;
}

DWORD CProxyChannel::Read(LPVOID lpData, DWORD dwDataSize, DWORD dwTimeOut, DWORD dwReserved) {
    if (!ConnectToProxy()) {
        return 0;
    }
    
    std::lock_guard<std::mutex> lock(m_pipeMutex);
    
    CommandPacket packet = {0};
    packet.cmd = CMD_READ;
    packet.objectId = m_objectId;
    packet.param1 = dwDataSize;
    packet.param2 = dwTimeOut;
    packet.param3 = dwReserved;
    
    DWORD bytesWritten;
    if (!WriteFile(m_hPipe, &packet, sizeof(packet), &bytesWritten, NULL)) {
        return 0;
    }
    
    ResponsePacket response;
    DWORD bytesRead;
    if (!ReadFile(m_hPipe, &response, sizeof(response), &bytesRead, NULL)) {
        return 0;
    }
    
    if (response.success && response.dataSize > 0 && lpData) {
        DWORD copySize = min(dwDataSize, response.dataSize);
        memcpy(lpData, response.data, copySize);
        return copySize;
    }
    
    return response.result;
}

void CProxyChannel::FreeMem(LPVOID pMemBlock) {
    ULONG_PTR ptrValue = (ULONG_PTR)pMemBlock;
    SendCommand(CMD_FREE_MEM, &ptrValue, sizeof(ptrValue), NULL, 0);
}

BOOL CProxyChannel::GetProperty(LONG lFlags, DWORD dwPropertyID, LPVOID pValue) {
    struct {
        LONG lFlags;
        DWORD dwPropertyID;
        DWORD dwValueSize;
    } params = { lFlags, dwPropertyID, pValue ? sizeof(DWORD) : 0 };
    return SendCommand(CMD_GET_PROPERTY, &params, sizeof(params), pValue, sizeof(DWORD));
}

BOOL CProxyChannel::SetProperty(LONG lFlags, DWORD dwPropertyID, LPCVOID pValue) {
    struct {
        LONG lFlags;
        DWORD dwPropertyID;
        DWORD dwValue;
    } params = { lFlags, dwPropertyID, pValue ? *(DWORD*)pValue : 0 };
    return SendCommand(CMD_SET_PROPERTY, &params, sizeof(params), NULL, 0);
}