// BMPlatform_Proxy.cpp - 代理通道实现
#include "BMPlatform.h"
#include <windows.h>
#include <cstring>
#include <iostream>

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
    CMD_FREE_MEM
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
#pragma pack(pop)

// CProxyChannel 构造函数
CProxyChannel::CProxyChannel() : m_hPipe(INVALID_HANDLE_VALUE), m_objectId(0) {
}

CProxyChannel::~CProxyChannel() {
    if (m_hPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hPipe);
    }
}

// ConnectToProxy 实现
BOOL CProxyChannel::ConnectToProxy() {
    if (m_hPipe != INVALID_HANDLE_VALUE) {
        return TRUE;
    }
    
    // 尝试连接已存在的代理进程
    for (int i = 0; i < 10; i++) {
        m_hPipe = CreateFileA(
            "\\\\.\\pipe\\SPRDChannelProxy",
            GENERIC_READ | GENERIC_WRITE,
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
                return TRUE;
            }
            
            CloseHandle(m_hPipe);
            m_hPipe = INVALID_HANDLE_VALUE;
        }
        Sleep(100);
    }
    
    // 获取当前程序（sfd_tool.exe）所在目录
    char exePath[MAX_PATH];
    char workingDir[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    
    char* lastSlash = strrchr(exePath, '\\');
    if (lastSlash) {
        *(lastSlash + 1) = '\0';
    }
    strcpy_s(workingDir, exePath);
    
    // 构建 Proxy32.exe 完整路径
    char proxyPath[MAX_PATH];
    strcpy_s(proxyPath, workingDir);
    strcat_s(proxyPath, "Proxy32.exe");
    
    std::cout << "Starting Proxy32.exe from: " << proxyPath << std::endl;
    std::cout << "Working directory: " << workingDir << std::endl;
    
    // 检查 Proxy32.exe 是否存在
    if (GetFileAttributesA(proxyPath) == INVALID_FILE_ATTRIBUTES) {
        std::cerr << "Proxy32.exe not found at: " << proxyPath << std::endl;
        return FALSE;
    }
    
    // 简单方法：不传递自定义环境，让子进程继承父进程的环境
    // 父进程的环境已经包含正确的 PATH
    
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    
    // 不使用自定义环境，让子进程继承
    if (!CreateProcessA(
        proxyPath,              // 可执行文件路径
        NULL,                   // 命令行
        NULL,                   // 进程安全属性
        NULL,                   // 线程安全属性
        FALSE,                  // 继承句柄
        0,                      // 创建标志（不使用自定义环境）
        NULL,                   // 环境变量（继承父进程）
        workingDir,             // 工作目录
        &si,                    // 启动信息
        &pi)) {                 // 进程信息
        
        DWORD error = GetLastError();
        std::cerr << "Failed to start Proxy32.exe. Error: " << error << std::endl;
        return FALSE;
    }
    
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    
    // 等待代理进程启动
    Sleep(500);
    
    // 连接到代理进程
    for (int i = 0; i < 15; i++) {
        m_hPipe = CreateFileA(
            "\\\\.\\pipe\\SPRDChannelProxy",
            GENERIC_READ | GENERIC_WRITE,
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
                std::cout << "Connected to Proxy32, object ID: " << m_objectId << std::endl;
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

// 修改 SendCommand，返回结果值
BOOL CProxyChannel::SendCommand(DWORD cmd, void* params, DWORD paramSize, void* resp, DWORD respSize) {
    if (!ConnectToProxy()) {
        return FALSE;
    }
    
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

// 其他方法的实现
BOOL CProxyChannel::InitLog(LPCWSTR pszLogName, UINT uiLogType, UINT uiLogLevel, 
                            ISpLog *pLogUtil, LPCWSTR pszBinLogFileExt) {
    return TRUE;
}

BOOL CProxyChannel::SetReceiver(ULONG ulMsgId, BOOL bRcvThread, LPCVOID pReceiver) {
    struct {
        ULONG ulMsgId;
        BOOL bRcvThread;
        LPCVOID pReceiver;
    } params = { ulMsgId, bRcvThread, pReceiver };
    return SendCommand(CMD_SET_RECEIVER, &params, sizeof(params), NULL, 0);
}

void CProxyChannel::GetReceiver(ULONG &ulMsgId, BOOL &bRcvThread, LPVOID &pReceiver) {
    ulMsgId = 0;
    bRcvThread = FALSE;
    pReceiver = NULL;
}

BOOL CProxyChannel::Open(PCCHANNEL_ATTRIBUTE pOpenArgument) {
    return SendCommand(CMD_OPEN, (void*)pOpenArgument, sizeof(CHANNEL_ATTRIBUTE), NULL, 0);
}

void CProxyChannel::Close() {
    SendCommand(CMD_CLOSE, NULL, 0, NULL, 0);
}

BOOL CProxyChannel::Clear() {
    BOOL result;
    if (SendCommand(CMD_CLEAR, NULL, 0, &result, sizeof(result))) {
        return result;
    }
    return FALSE;
}

// Write 方法 - 直接处理，不使用 SendCommand
DWORD CProxyChannel::Write(LPVOID lpData, DWORD dwDataSize, DWORD dwReserved) {
    if (!ConnectToProxy()) {
        return 0;
    }
    
    // 构建命令包，包含要写入的数据
    CommandPacket packet = {0};
    packet.cmd = CMD_WRITE;
    packet.objectId = m_objectId;
    packet.dataSize = dwDataSize;
    if (dwDataSize > 0 && lpData && dwDataSize <= sizeof(packet.data)) {
        memcpy(packet.data, lpData, dwDataSize);
    }
    
    // 发送命令和数据
    DWORD bytesWritten;
    if (!WriteFile(m_hPipe, &packet, sizeof(packet), &bytesWritten, NULL)) {
        return 0;
    }
    
    // 读取响应
    ResponsePacket response;
    DWORD bytesRead;
    if (!ReadFile(m_hPipe, &response, sizeof(response), &bytesRead, NULL)) {
        return 0;
    }
    
    // 返回实际写入的字节数
    if (response.success) {
        return response.result;
    }
    return 0;
}

// Read 方法 - 直接处理，不使用 SendCommand
DWORD CProxyChannel::Read(LPVOID lpData, DWORD dwDataSize, DWORD dwTimeOut, DWORD dwReserved) {
    if (!ConnectToProxy()) {
        return 0;
    }
    
    // 构建命令包
    CommandPacket packet = {0};
    packet.cmd = CMD_READ;
    packet.objectId = m_objectId;
    packet.param1 = dwDataSize;
    packet.param2 = dwTimeOut;
    packet.param3 = dwReserved;
    
    // 发送命令
    DWORD bytesWritten;
    if (!WriteFile(m_hPipe, &packet, sizeof(packet), &bytesWritten, NULL)) {
        return 0;
    }
    
    // 读取响应（包含读取的数据）
    ResponsePacket response;
    DWORD bytesRead;
    if (!ReadFile(m_hPipe, &response, sizeof(response), &bytesRead, NULL)) {
        return 0;
    }
    
    // 复制读取的数据
    if (response.success && response.dataSize > 0 && lpData) {
        DWORD copySize = min(dwDataSize, response.dataSize);
        memcpy(lpData, response.data, copySize);
        return copySize;
    }
    
    // 返回 result（可能是读取的字节数或 0）
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
    } params = { lFlags, dwPropertyID };
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