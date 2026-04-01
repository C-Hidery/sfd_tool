#pragma once

#include <Windows.h>
#include <tchar.h>

// 原有定义保持不变
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

// 代理通道类，将 ICommChannel 调用转换为管道通信
class CProxyChannel : public ICommChannel {
public:
    CProxyChannel(HANDLE hPipe);
    virtual ~CProxyChannel();

    // ICommChannel methods
    virtual BOOL InitLog(LPCWSTR pszLogName, UINT uiLogType,
        UINT uiLogLevel, ISpLog *pLogUtil, LPCWSTR pszBinLogFileExt) override;
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
    HANDLE m_hPipe;
    // 内部辅助函数：发送命令并接收响应
    BOOL SendCommand(DWORD cmd, const void* param, DWORD paramSize, void* replyBuf, DWORD replySize, DWORD* replyActual = NULL);
};

// 应用程序类：负责与代理进程建立连接
class CBMPlatformApp {
public:
    CBMPlatformApp();
    ~CBMPlatformApp();

    BOOL InitInstance();
    int ExitInstance();

    ICommChannel* CreateChannel(CHANNEL_TYPE type);
    void ReleaseChannel(ICommChannel* pChannel);

private:
    HANDLE m_hPipe;      // 与代理进程的管道句柄
    BOOL   m_bConnected;
};

// 外部全局变量（原代码中有 extern int m_bOpened）
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