// MySerialChannel.h
#pragma once
#include <windows.h>
#include <atomic>
#include <mutex>
#include <queue>
#include <condition_variable>
#include "BMPlatform.h"

class CMySerialChannel : public ICommChannel {
public:
    CMySerialChannel();
    virtual ~CMySerialChannel();

    // ICommChannel 接口
    virtual BOOL InitLog(LPCWSTR pszLogName, UINT uiLogType, UINT uiLogLevel,
                         ISpLog *pLogUtil, LPCWSTR pszBinLogFileExt) override;
    virtual BOOL SetReceiver(ULONG ulMsgId, BOOL bRcvThread, LPCVOID pReceiver) override;
    virtual void GetReceiver(ULONG &ulMsgId, BOOL &bRcvThread, LPVOID &pReceiver) override;
    virtual BOOL Open(PCCHANNEL_ATTRIBUTE pOpenArgument) override;
    virtual void Close() override;
    virtual BOOL Clear() override;
    virtual DWORD Read(LPVOID lpData, DWORD dwDataSize, DWORD dwTimeOut, DWORD dwReserved = 0) override;
    virtual DWORD Write(LPVOID lpData, DWORD dwDataSize, DWORD dwReserved = 0) override;
    virtual void FreeMem(LPVOID pMemBlock) override;
    virtual BOOL GetProperty(LONG lFlags, DWORD dwPropertyID, LPVOID pValue) override;
    virtual BOOL SetProperty(LONG lFlags, DWORD dwPropertyID, LPCVOID pValue) override;

private:
    // 串口句柄
    HANDLE m_hCom;
    // 停止事件（用于通知读取线程退出）
    HANDLE m_hStopEvent;
    // 读取线程句柄和 ID
    HANDLE m_hReadThread;
    DWORD  m_dwThreadId;
    // 接收目标（窗口或线程）
    HWND   m_hTargetWnd;
    ULONG  m_ulMsgId;
    BOOL   m_bRcvThread;         // TRUE 表示线程，FALSE 表示窗口
    bool   m_bAsyncMode;         // 是否启用异步推送
    std::atomic<bool> m_bRunning;

    // 数据池（用于同步 Read）
    std::queue<std::vector<BYTE>> m_dataQueue;
    std::mutex                    m_queueMutex;
    std::condition_variable       m_dataAvailable;

    // 辅助函数
    static DWORD WINAPI ReadThreadProc(LPVOID lpParam);
    void SetTimeouts(DWORD readInterval, DWORD readTotalConst);
    void Log(const char* level, const char* fmt, ...);
};