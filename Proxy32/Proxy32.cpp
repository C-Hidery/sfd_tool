// Proxy32.cpp : 32-bit proxy process for Channel9.dll
#include <windows.h>
#include <stdio.h>
#include <string>

// 与主程序通信的管道名称
#define PIPE_NAME L"\\\\.\\pipe\\BMProxyPipe"

// 命令码
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
    // 可扩展其他类型，这里仅实现 COM
};

struct CloseParam {
    DWORD channelPtr;
};

struct ClearParam {
    DWORD channelPtr;
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
    // 后面紧跟返回缓冲区
};

struct SetPropertyParam {
    DWORD channelPtr;
    LONG flags;
    DWORD propId;
    DWORD dataSize;
    BYTE data[1];
};
#pragma pack(pop)

// 全局函数指针
typedef BOOL(*pfCreateChannel)(ICommChannel**, CHANNEL_TYPE);
typedef void(*pfReleaseChannel)(ICommChannel*);

HMODULE g_hDll = NULL;
pfCreateChannel g_pfCreateChannel = NULL;
pfReleaseChannel g_pfReleaseChannel = NULL;

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
        } // 其他类型可扩展
        BOOL ret = pChannel->Open(&attr);
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
        WriteParam* param = (WriteParam*)malloc(sizeof(WriteParam) + 1024); // 临时分配
        DWORD headerSize = sizeof(WriteParam) - 1; // 除去data[1]
        if (!ReadFull(hPipe, param, headerSize)) {
            free(param);
            return;
        }
        DWORD dataLen = param->dataLen;
        if (dataLen > 0) {
            if (!ReadFull(hPipe, param->data, dataLen)) {
                free(param);
                return;
            }
        }
        DWORD written = pChannel->Write(param->data, dataLen, param->reserved);
        SendResponse(hPipe, written);
        free(param);
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
        SetPropertyParam* param = (SetPropertyParam*)malloc(sizeof(SetPropertyParam) + 1024);
        DWORD headerSize = sizeof(SetPropertyParam) - 1;
        if (!ReadFull(hPipe, param, headerSize)) {
            free(param);
            return;
        }
        DWORD dataSize = param->dataSize;
        if (dataSize > 0) {
            if (!ReadFull(hPipe, param->data, dataSize)) {
                free(param);
                return;
            }
        }
        BOOL ret = pChannel->SetProperty(param->flags, param->propId, param->data);
        SendResponse(hPipe, ret);
        free(param);
        break;
    }

    default:
        // 未知命令，忽略
        break;
    }
}

int wmain() {
    if (!LoadChannelDll()) {
        wprintf(L"Failed to load Channel9.dll\n");
        return 1;
    }

    HANDLE hPipe = CreateNamedPipeW(
        PIPE_NAME,
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1,          // 只支持一个客户端
        4096, 4096, 0, NULL
    );

    if (hPipe == INVALID_HANDLE_VALUE) {
        wprintf(L"CreateNamedPipe failed: %d\n", GetLastError());
        return 1;
    }

    wprintf(L"Proxy32 started, waiting for client...\n");

    while (TRUE) {
        if (ConnectNamedPipe(hPipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
            wprintf(L"Client connected.\n");

            ICommChannel* pChannel = NULL;
            while (TRUE) {
                DWORD cmd;
                DWORD read;
                if (!ReadFile(hPipe, &cmd, sizeof(cmd), &read, NULL) || read != sizeof(cmd))
                    break;
                ProcessCommand(hPipe, (ProxyCommand)cmd, pChannel);
            }

            DisconnectNamedPipe(hPipe);
            wprintf(L"Client disconnected.\n");
        }
        // 等待下一个客户端
    }

    CloseHandle(hPipe);
    if (g_hDll) FreeLibrary(g_hDll);
    return 0;
}