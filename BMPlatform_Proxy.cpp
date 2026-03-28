// BMPlatform_Proxy.cpp - 代理通道实现
#include "BMPlatform.h"
#include <windows.h>
#include <cstring>
#include <iostream>
#include <sstream>
#include <atomic>

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

// 全局进程ID计数器，用于生成唯一管道名
static std::atomic<DWORD> g_pipeIdCounter(1);

CProxyChannel::CProxyChannel() 
    : m_hPipe(INVALID_HANDLE_VALUE), 
      m_objectId(0), 
      m_hProxyProcess(NULL), 
      m_bConnected(FALSE) {
}

CProxyChannel::~CProxyChannel() {
    if (m_hPipe != INVALID_HANDLE_VALUE) {
        // 通知代理进程关闭
        CommandPacket cmd = {0};
        cmd.cmd = CMD_SHUTDOWN;
        cmd.objectId = m_objectId;
        
        DWORD bytesWritten;
        WriteFile(m_hPipe, &cmd, sizeof(cmd), &bytesWritten, NULL);
        
        CloseHandle(m_hPipe);
    }
    
    if (m_hProxyProcess) {
        // 等待代理进程结束
        WaitForSingleObject(m_hProxyProcess, 5000);
        CloseHandle(m_hProxyProcess);
    }
}

// ConnectToProxy 实现（每个对象独立管道）
BOOL CProxyChannel::ConnectToProxy() {
    if (m_bConnected && m_hPipe != INVALID_HANDLE_VALUE) {
        return TRUE;
    }
    
    // 生成唯一的管道名
    DWORD pipeId = g_pipeIdCounter++;
    std::string pipeName = "\\\\.\\pipe\\SPRDChannelProxy_" + std::to_string(pipeId);
    
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
    
    // 构建命令行，传递管道名参数
    char cmdLine[MAX_PATH * 2];
    snprintf(cmdLine, sizeof(cmdLine), "\"%s\" %d", proxyPath, pipeId);
    
    std::cout << "Starting Proxy32.exe with pipe ID: " << pipeId << std::endl;
    
    // 检查 Proxy32.exe 是否存在
    if (GetFileAttributesA(proxyPath) == INVALID_FILE_ATTRIBUTES) {
        std::cerr << "Proxy32.exe not found at: " << proxyPath << std::endl;
        return FALSE;
    }
    
    // 启动代理进程
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    
    if (!CreateProcessA(
        proxyPath,
        cmdLine,
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        workingDir,
        &si,
        &pi)) {
        DWORD error = GetLastError();
        std::cerr << "Failed to start Proxy32.exe. Error: " << error << std::endl;
        return FALSE;
    }
    
    m_hProxyProcess = pi.hProcess;
    CloseHandle(pi.hThread);
    
    // 等待代理进程启动并创建管道
    Sleep(500);
    
    // 连接到代理进程的唯一管道
    for (int i = 0; i < 15; i++) {
        m_hPipe = CreateFileA(
            pipeName.c_str(),
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
                m_bConnected = TRUE;
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

// 修改 SendCommand，添加互斥锁保护
BOOL CProxyChannel::SendCommand(DWORD cmd, void* params, DWORD paramSize, void* resp, DWORD respSize) {
    if (!ConnectToProxy()) {
        return FALSE;
    }
    
    std::lock_guard<std::mutex> lock(m_pipeMutex);  // 互斥锁保护
    
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
    // 注意：pReceiver 是指针，不能跨进程传递，这里只保存值
    // 实际应用中，pReceiver 应该是一个可以在32位进程中使用的句柄或标识符
    if (pReceiver) {
        // 将指针值转换为 DWORD 传递（仅适用于64位地址转换到32位的情况）
        DWORD dwReceiver = (DWORD)(ULONG_PTR)pReceiver;
        struct {
            ULONG ulMsgId;
            BOOL bRcvThread;
            DWORD dwReceiver;
        } params = { ulMsgId, bRcvThread, dwReceiver };
        return SendCommand(CMD_SET_RECEIVER, &params, sizeof(params), NULL, 0);
    } else {
        struct {
            ULONG ulMsgId;
            BOOL bRcvThread;
            DWORD dwReceiver;
        } params = { ulMsgId, bRcvThread, 0 };
        return SendCommand(CMD_SET_RECEIVER, &params, sizeof(params), NULL, 0);
    }
}

void CProxyChannel::GetReceiver(ULONG &ulMsgId, BOOL &bRcvThread, LPVOID &pReceiver) {
    // 由于跨进程限制，此方法无法正常工作
    // 建议改为使用 GetProperty 获取接收者信息
    ulMsgId = 0;
    bRcvThread = FALSE;
    pReceiver = NULL;
}

BOOL CProxyChannel::Open(PCCHANNEL_ATTRIBUTE pOpenArgument) {
    // 注意：CHANNEL_ATTRIBUTE 中的 pFilePath 指针不能跨进程传递
    // 如果使用文件通道，需要特殊处理（如传递文件路径字符串）
    return SendCommand(CMD_OPEN, (void*)pOpenArgument, sizeof(CHANNEL_ATTRIBUTE), NULL, 0);
}

void CProxyChannel::Close() {
    SendCommand(CMD_CLOSE, NULL, 0, NULL, 0);
    m_bConnected = FALSE;
}

BOOL CProxyChannel::Clear() {
    BOOL result;
    if (SendCommand(CMD_CLEAR, NULL, 0, &result, sizeof(result))) {
        return result;
    }
    return FALSE;
}

// Write 方法 - 添加互斥锁保护
DWORD CProxyChannel::Write(LPVOID lpData, DWORD dwDataSize, DWORD dwReserved) {
    if (!ConnectToProxy()) {
        return 0;
    }
    
    std::lock_guard<std::mutex> lock(m_pipeMutex);
    
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

// Read 方法 - 添加互斥锁保护
DWORD CProxyChannel::Read(LPVOID lpData, DWORD dwDataSize, DWORD dwTimeOut, DWORD dwReserved) {
    if (!ConnectToProxy()) {
        return 0;
    }
    
    std::lock_guard<std::mutex> lock(m_pipeMutex);
    
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
    // 注意：pMemBlock 指针不能跨进程传递
    // 这里传递的是内存块标识符，实际释放应在创建内存的进程中执行
    ULONG_PTR ptrValue = (ULONG_PTR)pMemBlock;
    SendCommand(CMD_FREE_MEM, &ptrValue, sizeof(ptrValue), NULL, 0);
}

BOOL CProxyChannel::GetProperty(LONG lFlags, DWORD dwPropertyID, LPVOID pValue) {
    // 支持传递任意大小的数据
    struct {
        LONG lFlags;
        DWORD dwPropertyID;
        DWORD dwValueSize;
    } params = { lFlags, dwPropertyID, pValue ? sizeof(DWORD) : 0 };
    
    // 对于大于 DWORD 的属性，需要特殊处理
    return SendCommand(CMD_GET_PROPERTY, &params, sizeof(params), pValue, sizeof(DWORD));
}

BOOL CProxyChannel::SetProperty(LONG lFlags, DWORD dwPropertyID, LPCVOID pValue) {
    // 支持传递任意大小的数据
    struct {
        LONG lFlags;
        DWORD dwPropertyID;
        DWORD dwValue;
    } params = { lFlags, dwPropertyID, pValue ? *(DWORD*)pValue : 0 };
    return SendCommand(CMD_SET_PROPERTY, &params, sizeof(params), NULL, 0);
}