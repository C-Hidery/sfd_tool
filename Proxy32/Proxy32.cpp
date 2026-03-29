// Proxy32.cpp - 32位代理进程
#include <windows.h>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <cstring>
#include <cstdio>

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

struct SetReceiverParams {
    ULONG ulMsgId;
    BOOL bRcvThread;
    DWORD dwReceiver;
    HANDLE hEventPipeWrite;
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

// 全局变量
HMODULE g_hChannelLib = NULL;
pfCreateChannel g_pfCreateChannel = NULL;
pfReleaseChannel g_pfReleaseChannel = NULL;
ChannelManager g_channelManager;
HANDLE g_hPipe = INVALID_HANDLE_VALUE;
BOOL g_bRunning = TRUE;
std::string g_pipeName;

// 端口占用记录
std::set<DWORD> g_openedPorts;
std::mutex g_portMutex;

// 通道窗口映射
std::map<DWORD, HWND> g_channelWindowMap;
std::mutex g_windowMapMutex;

// 窗口过程声明
LRESULT CALLBACK AsyncWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

struct ChannelWindowInfo {
    DWORD channelId;
    ULONG ulMsgId;
    HANDLE hEventPipe;
};

BOOL InitChannelDll() {
    if (g_hChannelLib) return TRUE;
    
    char cwd[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, cwd);
    std::cout << "Current directory: " << cwd << std::endl;
    
    SetDllDirectoryA(cwd);
    
    char oldPath[4096];
    GetEnvironmentVariableA("PATH", oldPath, sizeof(oldPath));
    char newPath[8192];
    snprintf(newPath, sizeof(newPath), "%s;%s;C:\\Windows\\SysWOW64", cwd, oldPath);
    SetEnvironmentVariableA("PATH", newPath);
    
    char dllPath[MAX_PATH];
    strcpy_s(dllPath, cwd);
    strcat_s(dllPath, "\\Channel9.dll");
    
    if (GetFileAttributesA(dllPath) == INVALID_FILE_ATTRIBUTES) {
        std::cerr << "Channel9.dll not found at: " << dllPath << std::endl;
        return FALSE;
    }
    std::cout << "Channel9.dll found" << std::endl;
    
    char iniPath[MAX_PATH];
    strcpy_s(iniPath, cwd);
    strcat_s(iniPath, "\\Channel.ini");
    if (GetFileAttributesA(iniPath) == INVALID_FILE_ATTRIBUTES) {
        std::cerr << "WARNING: Channel.ini not found at: " << iniPath << std::endl;
    } else {
        std::cout << "Channel.ini found" << std::endl;
    }
    
    g_hChannelLib = LoadLibraryA("Channel9.dll");
    if (!g_hChannelLib) {
        g_hChannelLib = LoadLibraryA(dllPath);
        if (!g_hChannelLib) {
            DWORD error = GetLastError();
            std::cerr << "Failed to load Channel9.dll. Error: " << error << std::endl;
            LPSTR msgBuf = NULL;
            FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
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
        
        // 清理窗口映射
        std::lock_guard<std::mutex> lock(g_windowMapMutex);
        auto it = g_channelWindowMap.find(channelId);
        if (it != g_channelWindowMap.end()) {
            ChannelWindowInfo* info = (ChannelWindowInfo*)GetWindowLongPtr(it->second, GWLP_USERDATA);
            delete info;
            DestroyWindow(it->second);
            g_channelWindowMap.erase(it);
        }
        return TRUE;
    }
    return FALSE;
}

BOOL HandleOpen(DWORD channelId, PCCHANNEL_ATTRIBUTE pAttr) {
    ICommChannel* pChannel = g_channelManager.GetChannel(channelId);
    if (!pChannel) return FALSE;
    
    DWORD portNum = pAttr->Com.dwPortNum;
    {
        std::lock_guard<std::mutex> lock(g_portMutex);
        if (g_openedPorts.find(portNum) != g_openedPorts.end()) {
            std::cout << "Port COM" << portNum << " already opened in proxy, returning success." << std::endl;
            return TRUE;
        }
    }
    
    std::cout << "HandleOpen: Opening COM" << portNum 
              << " at " << pAttr->Com.dwBaudRate << " baud" << std::endl;
    
    BOOL result = pChannel->Open(pAttr);
    if (result) {
        std::lock_guard<std::mutex> lock(g_portMutex);
        g_openedPorts.insert(portNum);
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
    if (!pChannel) return 0;
    DWORD bytesRead = pChannel->Read(lpData, dwDataSize, dwTimeOut);
    if (bytesRead > 0) std::cout << "HandleRead: read " << bytesRead << " bytes" << std::endl;
    return bytesRead;
}

DWORD HandleWrite(DWORD channelId, LPVOID lpData, DWORD dwDataSize) {
    ICommChannel* pChannel = g_channelManager.GetChannel(channelId);
    if (!pChannel) return 0;
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

BOOL HandleSetReceiver(DWORD channelId, const SetReceiverParams* pParams) {
    ICommChannel* pChannel = g_channelManager.GetChannel(channelId);
    if (!pChannel) return FALSE;
    
    // 注册窗口类（如果还没有注册）
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSA wc = {0};
        wc.lpfnWndProc = AsyncWndProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = "ProxyAsyncWnd";
        RegisterClassA(&wc);
        classRegistered = true;
    }
    
    // 创建隐藏窗口作为异步接收者
    HWND hWnd = CreateWindowA("ProxyAsyncWnd", NULL, 0, 0, 0, 0, 0, NULL, NULL, GetModuleHandle(NULL), NULL);
    if (!hWnd) return FALSE;
    
    // 保存通道信息
    ChannelWindowInfo* info = new ChannelWindowInfo;
    info->channelId = channelId;
    info->ulMsgId = pParams->ulMsgId;
    info->hEventPipe = pParams->hEventPipeWrite;
    SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)info);
    
    // 设置真实通道的接收者
    if (!pChannel->SetReceiver(pParams->ulMsgId, FALSE, (LPCVOID)hWnd)) {
        delete info;
        DestroyWindow(hWnd);
        return FALSE;
    }
    
    // 保存映射
    {
        std::lock_guard<std::mutex> lock(g_windowMapMutex);
        g_channelWindowMap[channelId] = hWnd;
    }
    
    std::cout << "SetReceiver: created hidden window for channel " << channelId << std::endl;
    return TRUE;
}

BOOL HandleGetProperty(DWORD channelId, LONG lFlags, DWORD dwPropertyID, LPVOID pValue, DWORD dwValueSize) {
    ICommChannel* pChannel = g_channelManager.GetChannel(channelId);
    if (!pChannel) return FALSE;
    return pChannel->GetProperty(lFlags, dwPropertyID, pValue);
}

BOOL HandleSetProperty(DWORD channelId, LONG lFlags, DWORD dwPropertyID, DWORD dwValue) {
    ICommChannel* pChannel = g_channelManager.GetChannel(channelId);
    if (!pChannel) return FALSE;
    return pChannel->SetProperty(lFlags, dwPropertyID, &dwValue);
}

void HandleFreeMem(DWORD channelId, ULONG_PTR ptrValue) {
    ICommChannel* pChannel = g_channelManager.GetChannel(channelId);
    if (pChannel) pChannel->FreeMem((LPVOID)ptrValue);
}

LRESULT CALLBACK AsyncWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_RCV_CHANNEL_DATA) {
        ChannelWindowInfo* info = (ChannelWindowInfo*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        if (info && info->hEventPipe != INVALID_HANDLE_VALUE) {
            BYTE* pData = (BYTE*)wParam;
            DWORD dataSize = (DWORD)lParam;
            
            AsyncEventPacket pkt;
            pkt.eventType = EVENT_ASYNC_DATA;
            pkt.objectId = info->channelId;
            pkt.ulMsgId = info->ulMsgId;
            pkt.dataSize = dataSize;
            if (dataSize > sizeof(pkt.data)) dataSize = sizeof(pkt.data);
            memcpy(pkt.data, pData, dataSize);
            
            DWORD bytesWritten;
            WriteFile(info->hEventPipe, &pkt, sizeof(pkt), &bytesWritten, NULL);
        }
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

void ProcessCommand(const CommandPacket& cmd, ResponsePacket& resp) {
    memset(&resp, 0, sizeof(resp));
    resp.success = TRUE;
    std::cout << "ProcessCommand: cmd=" << cmd.cmd << std::endl;
    
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
            if (cmd.dataSize >= sizeof(SetReceiverParams)) {
                SetReceiverParams* pParams = (SetReceiverParams*)cmd.data;
                resp.success = HandleSetReceiver(cmd.objectId, pParams);
            } else {
                resp.success = FALSE;
            }
            break;
        }
        
        case CMD_GET_PROPERTY: {
            struct { LONG lFlags; DWORD dwPropertyID; DWORD dwValueSize; }* pParams;
            pParams = (decltype(pParams))cmd.data;
            DWORD value = 0;
            resp.success = HandleGetProperty(cmd.objectId, pParams->lFlags, 
                                            pParams->dwPropertyID, &value, pParams->dwValueSize);
            if (resp.success) resp.result = value;
            break;
        }
        
        case CMD_SET_PROPERTY: {
            struct { LONG lFlags; DWORD dwPropertyID; DWORD dwValue; }* pParams;
            pParams = (decltype(pParams))cmd.data;
            resp.success = HandleSetProperty(cmd.objectId, pParams->lFlags, 
                                            pParams->dwPropertyID, pParams->dwValue);
            break;
        }
        
        case CMD_FREE_MEM:
            HandleFreeMem(cmd.objectId, (ULONG_PTR)cmd.param1);
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

int main(int argc, char* argv[]) {
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    char* lastSlash = strrchr(exePath, '\\');
    if (lastSlash) {
        *lastSlash = '\0';
    }
    SetCurrentDirectoryA(exePath);
    
    DWORD pipeId = 1;
    HANDLE hEventPipeWrite = INVALID_HANDLE_VALUE;
    if (argc >= 2) pipeId = atoi(argv[1]);
    if (argc >= 3) hEventPipeWrite = (HANDLE)atoi(argv[2]);
    
    g_pipeName = "\\\\.\\pipe\\SPRDChannelProxy_" + std::to_string(pipeId);
    
    std::cout << "========================================" << std::endl;
    std::cout << "SPRD Channel Proxy32 started (PID: " << GetCurrentProcessId() << ")" << std::endl;
    std::cout << "Pipe name: " << g_pipeName << std::endl;
    std::cout << "Event pipe handle: " << (int)hEventPipeWrite << std::endl;
    std::cout << "Proxy32.exe directory: " << exePath << std::endl;
    std::cout << "========================================" << std::endl;
    
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
    g_hPipe = CreateNamedPipeA(g_pipeName.c_str(),
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1,
        sizeof(ResponsePacket),
        sizeof(CommandPacket),
        0,
        NULL);
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