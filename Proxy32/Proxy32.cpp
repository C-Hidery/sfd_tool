// Proxy32.cpp : 32-bit proxy process for Channel9.dll
#include <windows.h>
#include <stdio.h>
#include <direct.h>
#include <string>
#include <time.h>

// 包含与主程序相同的接口定义（但不需要完整的类实现）
// 这里直接复制必要的定义，避免循环依赖

#define INVALID_VALUE ((DWORD)-1)
#define INFINITE_LOGFILE_SIZE ( 0 )

typedef enum {
    SPLOGLV_NONE = 0,
    SPLOGLV_ERROR = 1,
    SPLOGLV_WARN = 2,
    SPLOGLV_INFO = 3,
    SPLOGLV_DATA = 4,
    SPLOGLV_VERBOSE = 5
} SPLOG_LEVEL;

enum {
    LOG_READ = 0,
    LOG_WRITE = 1,
    LOG_ASYNC_READ = 2
};

class ISpLog {
public:
    enum OpenFlags {
        modeTimeSuffix = 0x0001,
        modeDateSuffix = 0x0002,
        modeCreate = 0x1000,
        modeNoTruncate = 0x2000,
        typeText = 0x4000,
        typeBinary = (int)0x8000,
        modeBinNone = 0x0000,
        modeBinRead = 0x0100,
        modeBinWrite = 0x0200,
        modeBinReadWrite = 0x0300,
        defaultTextFlag = modeCreate | modeDateSuffix | typeText,
        defaultBinaryFlag = modeCreate | modeDateSuffix | typeBinary | modeBinRead,
        defaultDualFlag = modeCreate | modeDateSuffix | typeText | typeBinary | modeBinRead
    };

    virtual ~ISpLog(void) {};
    virtual BOOL Open(LPCTSTR lpszLogPath,
        UINT uFlags = ISpLog::defaultTextFlag,
        UINT uLogLevel = SPLOGLV_INFO,
        UINT uMaxFileSizeInMB = INFINITE_LOGFILE_SIZE) = 0;
    virtual BOOL Close(void) = 0;
    virtual void Release(void) = 0;
    virtual BOOL LogHexData(const BYTE *pHexData, DWORD dwHexSize, UINT uFlag = LOG_READ) = 0;
    virtual BOOL LogRawStrA(UINT uLogLevel, LPCSTR lpszString) = 0;
    virtual BOOL LogRawStrW(UINT uLogLevel, LPCWSTR lpszString) = 0;
    virtual BOOL LogFmtStrA(UINT uLogLevel, LPCSTR lpszFmt, ...) = 0;
    virtual BOOL LogFmtStrW(UINT uLogLevel, LPCWSTR lpszFmt, ...) = 0;
    virtual BOOL LogBufData(UINT uLogLevel, const BYTE *pBufData, DWORD dwBufSize, UINT uFlag = LOG_WRITE, const DWORD *pUserNeedSize = NULL) = 0;
    virtual BOOL SetProperty(LONG lAttr, LONG lFlags, LPCVOID lpValue) = 0;
    virtual BOOL GetProperty(LONG lAttr, LONG lFlags, LPVOID lpValue) = 0;
};

enum CHANNEL_TYPE {
    CHANNEL_TYPE_COM = 0,
    CHANNEL_TYPE_SOCKET = 1,
    CHANNEL_TYPE_FILE = 2,
    CHANNEL_TYPE_USBMON = 3
};

typedef struct _CHANNEL_ATTRIBUTE {
    CHANNEL_TYPE ChannelType;
    union {
        struct { DWORD dwPortNum; DWORD dwBaudRate; } Com;
        struct { DWORD dwPort; DWORD dwIP; DWORD dwFlag; } Socket;
        struct { DWORD dwPackSize; DWORD dwPackFreq; WCHAR *pFilePath; } File;
    };
} CHANNEL_ATTRIBUTE, *PCHANNEL_ATTRIBUTE;
typedef const PCHANNEL_ATTRIBUTE PCCHANNEL_ATTRIBUTE;

class ICommChannel {
public:
    virtual ~ICommChannel() = 0;
    virtual BOOL InitLog(LPCWSTR pszLogName, UINT uiLogType,
        UINT uiLogLevel = INVALID_VALUE, ISpLog *pLogUtil = NULL,
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

inline ICommChannel::~ICommChannel() {}

// 函数指针类型定义
typedef BOOL(*pfCreateChannel)(ICommChannel**, CHANNEL_TYPE);
typedef void(*pfReleaseChannel)(ICommChannel*);

// 与主程序通信的管道名称
#define PIPE_NAME L"\\\\.\\pipe\\BMProxyPipe"

// 命令码
enum ProxyCommand {
    CMD_CREATE_CHANNEL = 0,
    CMD_RELEASE_CHANNEL = 1,
    CMD_INIT_LOG = 2,
    CMD_SET_RECEIVER = 3,
    CMD_GET_RECEIVER = 4,
    CMD_OPEN = 5,
    CMD_CLOSE = 6,
    CMD_CLEAR = 7,
    CMD_READ = 8,
    CMD_WRITE = 9,
    CMD_FREE_MEM = 10,
    CMD_GET_PROPERTY = 11,
    CMD_SET_PROPERTY = 12,
    CMD_SHUTDOWN = 13
};

// 参数结构体（用于序列化/反序列化）
#pragma pack(push, 1)
struct CreateChannelParam {
    DWORD channelType;  // CHANNEL_TYPE
};

struct ReleaseChannelParam {
    DWORD channelPtr;   // 实际是 ICommChannel*，但进程间传递无意义，这里只用作标识
};

struct InitLogParam {
    WCHAR logName[260];
    UINT logType;
    UINT logLevel;
    DWORD pLogUtil;     // 实际是 ISpLog*，进程间无意义，置0
    WCHAR binExt[10];
};

struct SetReceiverParam {
    ULONG msgId;
    BOOL bRcvThread;
    DWORD receiver;     // 实际指针，进程间无效
};

struct GetReceiverParam {
    ULONG msgId;
    BOOL bRcvThread;
    DWORD receiver;
};

struct OpenParam {
    DWORD channelType;          // CHANNEL_TYPE
    DWORD comPort;              // 对于 COM 类型
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
    BYTE data[1];   // 实际长度动态
};

struct FreeMemParam {
    DWORD pMemBlock;    // 指针值，仅用于标识
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

// 全局变量
HMODULE g_hDll = NULL;
pfCreateChannel g_pfCreateChannel = NULL;
pfReleaseChannel g_pfReleaseChannel = NULL;
volatile BOOL g_bShutdown = FALSE;
// 加载 DLL
BOOL LoadChannelDll() {
    // 获取当前进程目录
    WCHAR path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    WCHAR* p = wcsrchr(path, L'\\');
    if (p) *p = L'\0';
    wcscat_s(path, L"\\Channel9.dll");

    g_hDll = LoadLibraryW(path);
    if (!g_hDll) {
        wprintf(L"LoadLibrary failed: %d\n", GetLastError());
        return FALSE;
    }

    g_pfCreateChannel = (pfCreateChannel)GetProcAddress(g_hDll, "CreateChannel");
    g_pfReleaseChannel = (pfReleaseChannel)GetProcAddress(g_hDll, "ReleaseChannel");
    if (!g_pfCreateChannel || !g_pfReleaseChannel) {
        wprintf(L"GetProcAddress failed\n");
        return FALSE;
    }
    return TRUE;
}

// 发送响应（简单的先写一个DWORD表示成功/失败，后面跟实际数据）
void SendResponse(HANDLE hPipe, DWORD result, const void* data = NULL, DWORD dataSize = 0) {
    DWORD written;
    WriteFile(hPipe, &result, sizeof(result), &written, NULL);
    if (data && dataSize)
        WriteFile(hPipe, data, dataSize, &written, NULL);
}

// 从管道读取完整数据
BOOL ReadFull(HANDLE hPipe, void* buf, DWORD size) {
    DWORD read;
    return ReadFile(hPipe, buf, size, &read, NULL) && read == size;
}

// 处理命令
void ProcessCommand(HANDLE hPipe, ProxyCommand cmd, ICommChannel*& pChannel) {
    switch (cmd) {
    case CMD_CREATE_CHANNEL: {
        CreateChannelParam param;
        if (!ReadFull(hPipe, &param, sizeof(param))) return;
        ICommChannel* newChan = NULL;
        BOOL ok = g_pfCreateChannel(&newChan, (CHANNEL_TYPE)param.channelType);
        if (ok && newChan) {
            pChannel = newChan;
            SendResponse(hPipe, TRUE, &newChan, sizeof(newChan));
        } else {
            SendResponse(hPipe, FALSE);
        }
        break;
    }

    case CMD_RELEASE_CHANNEL: {
        ReleaseChannelParam param;
        if (!ReadFull(hPipe, &param, sizeof(param))) return;
        if (pChannel) {
            g_pfReleaseChannel(pChannel);
            pChannel = NULL;
            SendResponse(hPipe, TRUE);
        } else {
            SendResponse(hPipe, FALSE);
        }
        break;
    }

    case CMD_INIT_LOG: {
        InitLogParam param;
        if (!ReadFull(hPipe, &param, sizeof(param))) return;
        BOOL ret = pChannel->InitLog(param.logName, param.logType, param.logLevel,
                                     (ISpLog*)param.pLogUtil, param.binExt);
        SendResponse(hPipe, ret);
        break;
    }

    case CMD_SET_RECEIVER: {
        SetReceiverParam param;
        if (!ReadFull(hPipe, &param, sizeof(param))) return;
        BOOL ret = pChannel->SetReceiver(param.msgId, param.bRcvThread, (LPCVOID)param.receiver);
        SendResponse(hPipe, ret);
        break;
    }

    case CMD_GET_RECEIVER: {
        GetReceiverParam param;
        if (!ReadFull(hPipe, &param, sizeof(param))) return;
        ULONG msgId = param.msgId;
        BOOL bThread = param.bRcvThread;
        LPVOID receiver = (LPVOID)param.receiver;
        pChannel->GetReceiver(msgId, bThread, receiver);
        // 返回更新后的值
        GetReceiverParam result = { msgId, bThread, (DWORD)receiver };
        SendResponse(hPipe, TRUE, &result, sizeof(result));
        break;
    }

    case CMD_OPEN: {
        OpenParam param;
        if (!ReadFull(hPipe, &param, sizeof(param))) return;
        CHANNEL_ATTRIBUTE attr;
        attr.ChannelType = (CHANNEL_TYPE)param.channelType;
        if (param.channelType == CHANNEL_TYPE_COM) {
            attr.Com.dwPortNum = param.comPort;
            attr.Com.dwBaudRate = param.baudRate;
            wprintf(L"Opening COM%d, baud=%d\n", param.comPort, param.baudRate);
        }
        BOOL ret = pChannel->Open(&attr);
        if (!ret) {
            DWORD err = GetLastError();
            wprintf(L"Open failed with error: %d\n", err);
        } else {
            wprintf(L"Open succeeded\n");
        }
        SendResponse(hPipe, ret);
        break;
    }

    case CMD_CLOSE: {
        pChannel->Close();
        SendResponse(hPipe, TRUE);
        break;
    }

    case CMD_CLEAR: {
        BOOL ret = pChannel->Clear();
        SendResponse(hPipe, ret);
        break;
    }

    case CMD_READ: {
        ReadParam param;
        if (!ReadFull(hPipe, &param, sizeof(param))) return;
        BYTE* buf = new BYTE[param.maxLen];
        DWORD read = pChannel->Read(buf, param.maxLen, param.timeout, param.reserved);
        SendResponse(hPipe, read, buf, read);
        delete[] buf;
        break;
    }

    case CMD_WRITE: {
        // 先读取固定头部
        WriteParam header;
        DWORD read;
        if (!ReadFile(hPipe, &header, sizeof(WriteParam) - 1, &read, NULL) || 
            read != sizeof(WriteParam) - 1) return;
        
        DWORD dataLen = header.dataLen;
        BYTE* dataBuf = new BYTE[dataLen];
        if (dataLen > 0) {
            if (!ReadFile(hPipe, dataBuf, dataLen, &read, NULL) || read != dataLen) {
                delete[] dataBuf;
                return;
            }
        }
        
        DWORD written = pChannel->Write(dataBuf, dataLen);
        SendResponse(hPipe, written);
        delete[] dataBuf;
        break;
    }

    case CMD_FREE_MEM: {
        FreeMemParam param;
        if (!ReadFull(hPipe, &param, sizeof(param))) return;
        pChannel->FreeMem((LPVOID)param.pMemBlock);
        SendResponse(hPipe, TRUE);
        break;
    }

    case CMD_GET_PROPERTY: {
        GetPropertyParam param;
        if (!ReadFull(hPipe, &param, sizeof(param))) return;
        BYTE* buf = new BYTE[param.bufSize];
        BOOL ret = pChannel->GetProperty(param.flags, param.propId, buf);
        SendResponse(hPipe, ret, buf, param.bufSize);
        delete[] buf;
        break;
    }

    case CMD_SET_PROPERTY: {
        SetPropertyParam header;
        DWORD read;
        if (!ReadFile(hPipe, &header, sizeof(SetPropertyParam) - 1, &read, NULL) ||
            read != sizeof(SetPropertyParam) - 1) return;
        
        DWORD dataSize = header.dataSize;
        BYTE* dataBuf = new BYTE[dataSize];
        if (dataSize > 0) {
            if (!ReadFile(hPipe, dataBuf, dataSize, &read, NULL) || read != dataSize) {
                delete[] dataBuf;
                return;
            }
        }
        
        BOOL ret = pChannel->SetProperty(header.flags, header.propId, dataBuf);
        SendResponse(hPipe, ret);
        delete[] dataBuf;
        break;
    }
    case CMD_SHUTDOWN: {
        SendResponse(hPipe, TRUE);
        g_bShutdown = TRUE;
        break;
    }
    default:
        break;
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    HANDLE hPipe = CreateNamedPipeW(
        L"\\\\.\\pipe\\BMProxyPipe",
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES,
        4096, 4096, 0, NULL
    );

    if (hPipe == INVALID_HANDLE_VALUE) {
        printf("CreateNamedPipe failed: %d\n", GetLastError());
        return 1;
    }
    char fn_log[260];
    snprintf(fn_log, sizeof(fn_log), "Proxy_Log_%lld", (long long)time(nullptr));
    _mkdir(fn_log);
    char log_path_out[300];
    snprintf(log_path_out, sizeof(log_path_out), "%s\\proxy_out.log", fn_log);
    FILE *log_file_out = freopen(log_path_out, "w", stdout);
    char log_path_err[300];
    snprintf(log_path_err, sizeof(log_path_err), "%s\\proxy_err.log", fn_log);
    FILE *log_file_err = freopen(log_path_err, "w", stderr);
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
    if (!log_file_out || !log_file_err) {
        MessageBoxW(NULL, L"Failed to create log files", L"Proxy32 Error", MB_OK);
        return 1;
    }

    if (!LoadChannelDll()) {
        // 写入日志或消息框
        MessageBoxW(NULL, L"Failed to load Channel9.dll", L"Proxy32 Error", MB_OK);
        return 1;
    }


    // 主循环
    while (!g_bShutdown) {
        if (ConnectNamedPipe(hPipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
            ICommChannel* pChannel = NULL;
            while (TRUE) {
                DWORD cmd;
                DWORD read;
                if (!ReadFile(hPipe, &cmd, sizeof(cmd), &read, NULL) || read != sizeof(cmd))
                    break;
                ProcessCommand(hPipe, (ProxyCommand)cmd, pChannel);
            }
            DisconnectNamedPipe(hPipe);
        }
    }

    CloseHandle(hPipe);
    if (g_hDll) FreeLibrary(g_hDll);
    fclose(log_file_out);
    fclose(log_file_err);
    return 0;
}