// MySerialChannel.h
// SPDX-License-Identifier: GPL-3.0-or-later
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
    virtual BOOL InitLog(LPCWSTR pszLogName,
                         UINT uiLogType,
                         UINT uiLogLevel,
                         ISpLog *pLogUtil,
                         LPCWSTR pszBinLogFileExt) override;

    virtual BOOL SetReceiver(ULONG ulMsgId,
                             BOOL bRcvThread,
                             LPCVOID pReceiver) override;

    virtual void GetReceiver(ULONG &ulMsgId,
                             BOOL &bRcvThread,
                             LPVOID &pReceiver) override;

    virtual BOOL Open(PCCHANNEL_ATTRIBUTE pOpenArgument) override;
    virtual void Close() override;
    virtual BOOL Clear() override;

    virtual DWORD Read(LPVOID lpData,
                       DWORD dwDataSize,
                       DWORD dwTimeOut,
                       DWORD dwReserved = 0) override;

    virtual DWORD Write(LPVOID lpData,
                        DWORD dwDataSize,
                        DWORD dwReserved = 0) override;

    virtual void FreeMem(LPVOID pMemBlock) override;

    virtual BOOL GetProperty(LONG lFlags,
                             DWORD dwPropertyID,
                             LPVOID pValue) override;

    virtual BOOL SetProperty(LONG lFlags,
                             DWORD dwPropertyID,
                             LPCVOID pValue) override;

private:
    // 原有成员
    HANDLE m_hCom;
    HANDLE m_hStopEvent;
    HANDLE m_hReadThread;
    DWORD  m_dwThreadId;
    HWND   m_hTargetWnd;
    ULONG  m_ulMsgId;
    bool   m_bAsyncMode;
    std::atomic<bool> m_bRunning;

    // 新增数据池
    std::queue<std::vector<BYTE>> m_dataQueue;  // 存储数据块
    std::mutex                    m_queueMutex;
    std::condition_variable       m_dataAvailable; // 用于通知有数据

    // 辅助函数
    static DWORD WINAPI ReadThreadProc(LPVOID lpParam);
    void SetTimeouts(DWORD readInterval, DWORD readTotalConst);
    void Log(const char* level, const char* fmt, ...);
};