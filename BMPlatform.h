#pragma once

#include <Windows.h>
#include <tchar.h>

// 原有定义保持不变
#define INVALID_VALUE ((DWORD)-1)
#define INFINITE_LOGFILE_SIZE ( 0 )

typedef enum { SPLOGLV_NONE = 0, SPLOGLV_ERROR = 1, SPLOGLV_WARN = 2, SPLOGLV_INFO = 3, SPLOGLV_DATA = 4, SPLOGLV_VERBOSE = 5 } SPLOG_LEVEL;
enum { LOG_READ = 0, LOG_WRITE = 1, LOG_ASYNC_READ = 2 };

class ISpLog { /* 与原来相同，此处省略 */ };
enum CHANNEL_TYPE { CHANNEL_TYPE_COM = 0, CHANNEL_TYPE_SOCKET = 1, CHANNEL_TYPE_FILE = 2, CHANNEL_TYPE_USBMON = 3 };
typedef struct _CHANNEL_ATTRIBUTE { CHANNEL_TYPE ChannelType; union { struct { DWORD dwPortNum; DWORD dwBaudRate; } Com; struct { DWORD dwPort; DWORD dwIP; DWORD dwFlag; } Socket; struct { DWORD dwPackSize; DWORD dwPackFreq; WCHAR *pFilePath; } File; }; } CHANNEL_ATTRIBUTE, *PCHANNEL_ATTRIBUTE;
typedef const PCHANNEL_ATTRIBUTE PCCHANNEL_ATTRIBUTE;

class ICommChannel {
public:
    virtual ~ICommChannel() = 0;
    virtual BOOL InitLog(LPCWSTR pszLogName, UINT uiLogType, UINT uiLogLevel = INVALID_VALUE, ISpLog *pLogUtil = NULL, LPCWSTR pszBinLogFileExt = NULL) = 0;
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

typedef BOOL(*pfCreateChannel)(ICommChannel**, CHANNEL_TYPE);
typedef void(*pfReleaseChannel)(ICommChannel*);

// 与代理进程通信的命令码
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

// 参数结构体（使用 ULONG_PTR 保存指针，避免 64 位截断）
#pragma pack(push, 1)
struct CreateChannelParam { DWORD channelType; };
struct ReleaseChannelParam { ULONG_PTR channelPtr; };
struct InitLogParam { WCHAR logName[260]; UINT logType; UINT logLevel; ULONG_PTR pLogUtil; WCHAR binExt[10]; };
struct SetReceiverParam { ULONG msgId; BOOL bRcvThread; ULONG_PTR receiver; };
struct GetReceiverParam { ULONG msgId; BOOL bRcvThread; ULONG_PTR receiver; };
struct OpenParam { DWORD channelType; DWORD comPort; DWORD baudRate; };
struct ReadParam { ULONG_PTR channelPtr; DWORD maxLen; DWORD timeout; DWORD reserved; };
struct WriteParam { ULONG_PTR channelPtr; DWORD dataLen; BYTE data[1]; };
struct FreeMemParam { ULONG_PTR pMemBlock; };
struct GetPropertyParam { ULONG_PTR channelPtr; LONG flags; DWORD propId; DWORD bufSize; };
struct SetPropertyParam { ULONG_PTR channelPtr; LONG flags; DWORD propId; DWORD dataSize; BYTE data[1]; };
#pragma pack(pop)

class CBMPlatformApp {
public:
    CBMPlatformApp();
    ~CBMPlatformApp();

    BOOL InitInstance();
    int ExitInstance();

    ICommChannel* CreateChannel(CHANNEL_TYPE type);
    void ReleaseChannel(ICommChannel* pChannel);

    // 发送命令到代理进程（线程安全）
    BOOL SendCommand(DWORD cmd, const void* param, DWORD paramSize, void* replyBuf, DWORD replySize, DWORD* replyActual = NULL);

private:
    HANDLE m_hPipe;
    BOOL   m_bConnected;
    CRITICAL_SECTION m_cs;
};

// 代理通道类，将 ICommChannel 调用转发到代理进程
class CProxyChannel : public ICommChannel {
public:
    CProxyChannel(CBMPlatformApp* pApp);
    virtual ~CProxyChannel();

    virtual BOOL InitLog(LPCWSTR pszLogName, UINT uiLogType, UINT uiLogLevel, ISpLog *pLogUtil, LPCWSTR pszBinLogFileExt) override;
    virtual BOOL SetReceiver(ULONG ulMsgId, BOOL bRcvThread, LPCVOID pReceiver) override;
    virtual void GetReceiver(ULONG &ulMsgId, BOOL &bRcvThread, LPVOID &pReceiver) override;
    virtual BOOL Open(PCCHANNEL_ATTRIBUTE pOpenArgument) override;
    virtual void Close() override;
    virtual BOOL Clear() override;
    virtual DWORD Read(LPVOID lpData, DWORD dwDataSize, DWORD dwTimeOut, DWORD dwReserved) override;
    virtual DWORD Write(LPVOID lpData, DWORD dwDataSize, DWORD dwReserved) override;
    virtual void FreeMem(LPVOID pMemBlock) override;
    virtual BOOL GetProperty(LONG lFlags, DWORD dwPropertyID, LPVOID pValue) override;
    virtual BOOL SetProperty(LONG lFlags, DWORD dwPropertyID, LPCVOID pValue) override;

private:
    CBMPlatformApp* m_pApp;
};

extern CBMPlatformApp g_theApp;
extern int m_bOpened;

class CBootModeOpr {
public:
    CBootModeOpr();
    ~CBootModeOpr();
    BOOL Initialize();
    void Uninitialize();
    int Read(UCHAR *m_RecvData, int max_len, int dwTimeout);
    int Write(UCHAR *lpData, int iDataSize);
    BOOL ConnectChannel(DWORD dwPort, ULONG ulMsgId, DWORD Receiver);
    BOOL DisconnectChannel();
    BOOL GetProperty(LONG lFlags, DWORD dwPropertyID, LPVOID pValue);
    BOOL SetProperty(LONG lFlags, DWORD dwPropertyID, LPCVOID pValue);
    void Clear();
    void FreeMem(LPVOID pMemBlock);
private:
    ICommChannel *m_pChannel;
};