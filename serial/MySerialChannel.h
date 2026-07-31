// MySerialChannel.h
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <windows.h>
#include <atomic>
#include <mutex>
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
    HANDLE m_hCom;
    HANDLE m_hStopEvent;
    HANDLE m_hReadThread;
    DWORD  m_dwThreadId;

    HWND   m_hTargetWnd;
    ULONG  m_ulMsgId;
    bool   m_bAsyncMode;

    std::atomic<bool> m_bRunning;
    std::mutex        m_comMutex;   // 保护 m_hCom 的互斥锁

    static DWORD WINAPI ReadThreadProc(LPVOID lpParam);
    void ProcessReceivedData(const BYTE* data, DWORD len);
    void SetTimeouts(DWORD readInterval, DWORD readTotalConst);
    void Log(const char* level, const char* fmt, ...);
};