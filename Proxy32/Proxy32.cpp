// Proxy32.cpp - 32位代理进程
#include <windows.h>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <cstring>

#pragma warning(disable: 4996)

// 从 BMPlatform.h 复制的必要定义
typedef enum {
    CHANNEL_TYPE_COM = 0,
    CHANNEL_TYPE_SOCKET = 1,
    CHANNEL_TYPE_FILE = 2,
    CHANNEL_TYPE_USBMON = 3
} CHANNEL_TYPE;

typedef struct _CHANNEL_ATTRIBUTE {
    CHANNEL_TYPE ChannelType;
    union {
        struct {
            DWORD dwPortNum;
            DWORD dwBaudRate;
        } Com;
        struct {
            DWORD dwPort;
            DWORD dwIP;
            DWORD dwFlag;
        } Socket;
        struct {
            DWORD dwPackSize;
            DWORD dwPackFreq;
            WCHAR *pFilePath;
        } File;
    };
} CHANNEL_ATTRIBUTE, *PCHANNEL_ATTRIBUTE;

typedef const PCHANNEL_ATTRIBUTE PCCHANNEL_ATTRIBUTE;

class ICommChannel {
public:
    virtual ~ICommChannel() = 0;
    virtual BOOL InitLog(LPCWSTR pszLogName, UINT uiLogType,
        UINT uiLogLevel, void* pLogUtil = NULL,
        LPCWSTR pszBinLogFileExt = NULL) = 0;
    virtual BOOL SetReceiver(ULONG ulMsgId, BOOL bRcvThread, LPCVOID pReceiver) = 0;
    virtual void GetReceiver(ULONG &ulMsgId, BOOL &bRcvThread, LPVOID &pReceiver) = 0;
    virtual BOOL Open(PCCHANNEL_ATTRIBUTE pOpenArgument) = 0;
    virtual void Close() = 0;
    virtual BOOL Clear() = 0;
    virtual DWORD Read(LPVOID lpData, DWORD dwDataSize, DWORD dwTimeOut, DWORD dwReserved = 0) = 0;
    virtual DWORD Write(LPVOID lpData, DWORD dwDataSize, DWORD dwReserved = 0) = 0;
    virtual void FreeMem(LPVOID pMemBlock) = 0;
    virtual BOOL GetProperty(LONG lFlags, DWORD dwPropertyID, LPVOID pValue) = 0;
    virtual BOOL SetProperty(LONG lFlags, DWORD dwPropertyID, LPCVOID pValue) = 0;
};

ICommChannel::~ICommChannel() {}

typedef BOOL(*pfCreateChannel)(ICommChannel **, CHANNEL_TYPE);
typedef void(*pfReleaseChannel)(ICommChannel *);

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

class ChannelManager {
private:
    std::map<DWORD, ICommChannel*> m_channels;
    std::mutex m_mutex;
    DWORD m_nextId;
public:
    ChannelManager() : m_nextId(1) {}
    DWORD AddChannel(ICommChannel* channel) {
        std::lock_guard<std::mutex> lock(m_mutex);
        DWORD id = m_nextId++;
        m_channels[id] = channel;
        return id;
    }
    ICommChannel* GetChannel(DWORD id) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_channels.find(id);
        if (it != m_channels.end()) return it->second;
        return NULL;
    }
    void RemoveChannel(DWORD id) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_channels.erase(id);
    }
};

HMODULE g_hChannelLib = NULL;
pfCreateChannel g_pfCreateChannel = NULL;
pfReleaseChannel g_pfReleaseChannel = NULL;
ChannelManager g_channelManager;
HANDLE g_hPipe = INVALID_HANDLE_VALUE;
BOOL g_bRunning = TRUE;

BOOL InitChannelDll() {
    if (g_hChannelLib) return TRUE;
    
    // 获取当前目录（已经切换到程序目录）
    char cwd[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, cwd);
    std::cout << "Current directory: " << cwd << std::endl;
    
    // 设置 DLL 搜索路径
    SetDllDirectoryA(cwd);
    
    // 将当前目录添加到 PATH
    char oldPath[4096];
    GetEnvironmentVariableA("PATH", oldPath, sizeof(oldPath));
    char newPath[8192];
    snprintf(newPath, sizeof(newPath), "%s;%s;C:\\Windows\\SysWOW64", cwd, oldPath);
    SetEnvironmentVariableA("PATH", newPath);
    
    std::cout << "PATH set to: " << newPath << std::endl;
    
    // 检查 Channel9.dll 是否存在
    char dllPath[MAX_PATH];
    strcpy_s(dllPath, cwd);
    strcat_s(dllPath, "\\Channel9.dll");
    
    if (GetFileAttributesA(dllPath) == INVALID_FILE_ATTRIBUTES) {
        std::cerr << "Channel9.dll not found at: " << dllPath << std::endl;
        return FALSE;
    }
    std::cout << "Channel9.dll found" << std::endl;
    
    // 检查 Channel.ini 是否存在
    char iniPath[MAX_PATH];
    strcpy_s(iniPath, cwd);
    strcat_s(iniPath, "\\Channel.ini");
    
    if (GetFileAttributesA(iniPath) == INVALID_FILE_ATTRIBUTES) {
        std::cerr << "WARNING: Channel.ini not found at: " << iniPath << std::endl;
    } else {
        std::cout << "Channel.ini found" << std::endl;
    }
    
    // 尝试加载 DLL
    g_hChannelLib = LoadLibraryA("Channel9.dll");
    
    if (!g_hChannelLib) {
        // 尝试用完整路径
        g_hChannelLib = LoadLibraryA(dllPath);
        
        if (!g_hChannelLib) {
            DWORD error = GetLastError();
            std::cerr << "Failed to load Channel9.dll. Error: " << error << std::endl;
            
            // 获取详细错误信息
            LPSTR msgBuf = NULL;
            FormatMessageA(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                NULL, error, 0, (LPSTR)&msgBuf, 0, NULL);
            
            if (msgBuf) {
                std::cerr << "Details: " << msgBuf << std::endl;
                LocalFree(msgBuf);
            }
            
            return FALSE;
        }
    }
    
    std::cout << "Channel9.dll loaded successfully!" << std::endl;
    
    g_pfCreateChannel = (pfCreateChannel)GetProcAddress(g_hChannelLib, "CreateChannel");
    g_pfReleaseChannel = (pfReleaseChannel)GetProcAddress(g_hChannelLib, "ReleaseChannel");
    
    if (!g_pfCreateChannel || !g_pfReleaseChannel) {
        std::cerr << "Failed to get function addresses" << std::endl;
        FreeLibrary(g_hChannelLib);
        g_hChannelLib = NULL;
        return FALSE;
    }
    
    return TRUE;
}

DWORD HandleCreateChannel() {
    if (!InitChannelDll()) return 0;
    ICommChannel* pChannel = NULL;
    if (!g_pfCreateChannel(&pChannel, CHANNEL_TYPE_COM)) return 0;
    return g_channelManager.AddChannel(pChannel);
}

BOOL HandleReleaseChannel(DWORD channelId) {
    ICommChannel* pChannel = g_channelManager.GetChannel(channelId);
    if (pChannel && g_pfReleaseChannel) {
        g_pfReleaseChannel(pChannel);
        g_channelManager.RemoveChannel(channelId);
        return TRUE;
    }
    return FALSE;
}

BOOL HandleOpen(DWORD channelId, PCCHANNEL_ATTRIBUTE pAttr) {
    ICommChannel* pChannel = g_channelManager.GetChannel(channelId);
    if (!pChannel) {
        std::cerr << "HandleOpen: channel not found, ID=" << channelId << std::endl;
        return FALSE;
    }
    
    std::cout << "HandleOpen: Opening COM" << pAttr->Com.dwPortNum 
              << " at " << pAttr->Com.dwBaudRate << " baud" << std::endl;
    
    BOOL result = pChannel->Open(pAttr);
    
    if (result) {
        std::cout << "HandleOpen: SUCCESS" << std::endl;
    } else {
        std::cerr << "HandleOpen: FAILED" << std::endl;
    }
    
    return result;
}

void HandleClose(DWORD channelId) {
    ICommChannel* pChannel = g_channelManager.GetChannel(channelId);
    if (pChannel) pChannel->Close();
}

DWORD HandleRead(DWORD channelId, LPVOID lpData, DWORD dwDataSize, DWORD dwTimeOut) {
    ICommChannel* pChannel = g_channelManager.GetChannel(channelId);
    if (!pChannel) {
        std::cerr << "HandleRead: channel not found" << std::endl;
        return 0;
    }
    
    DWORD bytesRead = pChannel->Read(lpData, dwDataSize, dwTimeOut);
    if (bytesRead > 0) {
        std::cout << "HandleRead: read " << bytesRead << " bytes" << std::endl;
    }
    return bytesRead;
}

DWORD HandleWrite(DWORD channelId, LPVOID lpData, DWORD dwDataSize) {
    ICommChannel* pChannel = g_channelManager.GetChannel(channelId);
    if (!pChannel) {
        std::cerr << "HandleWrite: channel not found" << std::endl;
        return 0;
    }
    
    std::cout << "HandleWrite: writing " << dwDataSize << " bytes" << std::endl;
    DWORD bytesWritten = pChannel->Write(lpData, dwDataSize);
    std::cout << "HandleWrite: wrote " << bytesWritten << " bytes" << std::endl;
    
    return bytesWritten;
}

BOOL HandleClear(DWORD channelId) {
    ICommChannel* pChannel = g_channelManager.GetChannel(channelId);
    if (!pChannel) return FALSE;
    return pChannel->Clear();
}

BOOL HandleSetReceiver(DWORD channelId, ULONG ulMsgId, BOOL bRcvThread, LPCVOID pReceiver) {
    ICommChannel* pChannel = g_channelManager.GetChannel(channelId);
    if (!pChannel) return FALSE;
    return pChannel->SetReceiver(ulMsgId, bRcvThread, pReceiver);
}

BOOL HandleGetProperty(DWORD channelId, LONG lFlags, DWORD dwPropertyID, LPVOID pValue) {
    ICommChannel* pChannel = g_channelManager.GetChannel(channelId);
    if (!pChannel) return FALSE;
    return pChannel->GetProperty(lFlags, dwPropertyID, pValue);
}

BOOL HandleSetProperty(DWORD channelId, LONG lFlags, DWORD dwPropertyID, LPCVOID pValue) {
    ICommChannel* pChannel = g_channelManager.GetChannel(channelId);
    if (!pChannel) return FALSE;
    return pChannel->SetProperty(lFlags, dwPropertyID, pValue);
}

void HandleFreeMem(DWORD channelId, LPVOID pMemBlock) {
    ICommChannel* pChannel = g_channelManager.GetChannel(channelId);
    if (pChannel) pChannel->FreeMem(pMemBlock);
}

void ProcessCommand(const CommandPacket& cmd, ResponsePacket& resp) {
    memset(&resp, 0, sizeof(resp));
    resp.success = TRUE;
    
    switch (cmd.cmd) {
        case CMD_CREATE_CHANNEL:
            resp.result = HandleCreateChannel();
            resp.success = (resp.result != 0);
            break;
        case CMD_RELEASE_CHANNEL:
            resp.success = HandleReleaseChannel(cmd.objectId);
            break;
        case CMD_OPEN: {
            CHANNEL_ATTRIBUTE attr;
            if (cmd.dataSize >= sizeof(CHANNEL_ATTRIBUTE)) {
                memcpy(&attr, cmd.data, sizeof(CHANNEL_ATTRIBUTE));
                resp.success = HandleOpen(cmd.objectId, &attr);
            } else {
                std::cerr << "CMD_OPEN: data size too small: " << cmd.dataSize << std::endl;
                resp.success = FALSE;
            }
            break;
        }
        case CMD_CLOSE:
            HandleClose(cmd.objectId);
            resp.success = TRUE;
            break;
        case CMD_READ: {
            DWORD bytesRead = HandleRead(cmd.objectId, resp.data, cmd.param1, cmd.param2);
            resp.result = bytesRead;
            resp.dataSize = bytesRead;
            resp.success = TRUE;
            break;
        }
        case CMD_WRITE: {
            DWORD bytesWritten = HandleWrite(cmd.objectId, (LPVOID)cmd.data, cmd.dataSize);
            resp.result = bytesWritten;
            resp.success = (bytesWritten == cmd.dataSize);
            break;
        }
        case CMD_CLEAR:
            resp.success = HandleClear(cmd.objectId);
            break;
        case CMD_SET_RECEIVER: {
            struct { ULONG ulMsgId; BOOL bRcvThread; LPCVOID pReceiver; }* pParams;
            pParams = (decltype(pParams))cmd.data;
            resp.success = HandleSetReceiver(cmd.objectId, pParams->ulMsgId, pParams->bRcvThread, pParams->pReceiver);
            break;
        }
        case CMD_GET_PROPERTY: {
            struct { LONG lFlags; DWORD dwPropertyID; }* pParams;
            pParams = (decltype(pParams))cmd.data;
            DWORD value = 0;
            resp.success = HandleGetProperty(cmd.objectId, pParams->lFlags, pParams->dwPropertyID, &value);
            if (resp.success) resp.result = value;
            break;
        }
        case CMD_SET_PROPERTY: {
            struct { LONG lFlags; DWORD dwPropertyID; DWORD dwValue; }* pParams;
            pParams = (decltype(pParams))cmd.data;
            resp.success = HandleSetProperty(cmd.objectId, pParams->lFlags, pParams->dwPropertyID, &pParams->dwValue);
            break;
        }
        case CMD_FREE_MEM:
            HandleFreeMem(cmd.objectId, (LPVOID)(ULONG_PTR)cmd.param1);
            resp.success = TRUE;
            break;
        case CMD_SHUTDOWN:
            g_bRunning = FALSE;
            resp.success = TRUE;
            break;
        default:
            resp.success = FALSE;
            break;
    }
}

int main() {
    // 获取 Proxy32.exe 所在目录
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    char* lastSlash = strrchr(exePath, '\\');
    if (lastSlash) {
        *lastSlash = '\0';
    }
    
    // 切换到程序所在目录
    SetCurrentDirectoryA(exePath);
    
    std::cout << "========================================" << std::endl;
    std::cout << "SPRD Channel Proxy32 started (PID: " << GetCurrentProcessId() << ")" << std::endl;
    std::cout << "Proxy32.exe directory: " << exePath << std::endl;
    std::cout << "Current working directory: " << exePath << std::endl;
    std::cout << "========================================" << std::endl;
    
    // 验证关键文件
    char dllPath[MAX_PATH];
    strcpy_s(dllPath, exePath);
    strcat_s(dllPath, "\\Channel9.dll");
    
    if (GetFileAttributesA(dllPath) != INVALID_FILE_ATTRIBUTES) {
        std::cout << "[OK] Channel9.dll found" << std::endl;
    } else {
        std::cerr << "[ERROR] Channel9.dll NOT found!" << std::endl;
    }
    
    char iniPath[MAX_PATH];
    strcpy_s(iniPath, exePath);
    strcat_s(iniPath, "\\Channel.ini");
    
    if (GetFileAttributesA(iniPath) != INVALID_FILE_ATTRIBUTES) {
        std::cout << "[OK] Channel.ini found" << std::endl;
    } else {
        std::cerr << "[WARNING] Channel.ini NOT found!" << std::endl;
    }
    
    std::cout << "========================================" << std::endl;
    
    // 创建命名管道
    g_hPipe = CreateNamedPipeA(
        "\\\\.\\pipe\\SPRDChannelProxy",
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1,
        sizeof(ResponsePacket),
        sizeof(CommandPacket),
        0,
        NULL
    );
    
    if (g_hPipe == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to create named pipe. Error: " << GetLastError() << std::endl;
        return 1;
    }
    
    std::cout << "Waiting for client connection..." << std::endl;
    
    if (!ConnectNamedPipe(g_hPipe, NULL)) {
        if (GetLastError() != ERROR_PIPE_CONNECTED) {
            std::cerr << "Failed to connect pipe. Error: " << GetLastError() << std::endl;
            CloseHandle(g_hPipe);
            return 1;
        }
    }
    
    std::cout << "Client connected" << std::endl;
    std::cout << "========================================" << std::endl;
    
    CommandPacket cmd;
    DWORD bytesRead;
    
    while (g_bRunning) {
        memset(&cmd, 0, sizeof(cmd));
        if (!ReadFile(g_hPipe, &cmd, sizeof(cmd), &bytesRead, NULL)) {
            if (GetLastError() == ERROR_BROKEN_PIPE) break;
            continue;
        }
        if (bytesRead == 0) continue;
        
        ResponsePacket resp;
        ProcessCommand(cmd, resp);
        
        DWORD bytesWritten;
        WriteFile(g_hPipe, &resp, sizeof(resp), &bytesWritten, NULL);
        
        if (cmd.cmd == CMD_SHUTDOWN) break;
    }
    
    if (g_hPipe != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(g_hPipe);
        DisconnectNamedPipe(g_hPipe);
        CloseHandle(g_hPipe);
    }
    
    if (g_hChannelLib) {
        FreeLibrary(g_hChannelLib);
    }
    
    std::cout << "Proxy32 shutting down" << std::endl;
    return 0;
}