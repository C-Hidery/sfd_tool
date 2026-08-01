/*
* SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 * MySerialChannel - SPRD VCOM Channel
 */
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
    HANDLE m_hCom;
    HANDLE m_hStopEvent;
    HANDLE m_hReadThread;
    DWORD  m_dwThreadId;
    HWND   m_hTargetWnd;
    ULONG  m_ulMsgId;
    BOOL   m_bRcvThread;
    bool   m_bAsyncMode;
    std::atomic<bool> m_bRunning;

    // 数据池
    std::queue<std::vector<BYTE>> m_dataQueue;
    std::mutex                    m_queueMutex;
    std::condition_variable       m_dataAvailable;

    // 重叠 I/O 复用
    OVERLAPPED m_readOv;
    OVERLAPPED m_writeOv;
    HANDLE     m_hReadOvEvent;
    HANDLE     m_hWriteOvEvent;
    bool       m_bOvInitialized;

    static DWORD WINAPI ReadThreadProc(LPVOID lpParam);
    void SetTimeouts();
    void Log(const char* level, const char* fmt, ...);
};