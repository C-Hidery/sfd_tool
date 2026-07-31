// MySerialChannel.h
// SPDX-License-Identifier: GPL-3.0-or-later
// 完整替代 Channel9.dll 的串口通道实现，纯 MinGW/Windows API，无 MSVC 依赖

#pragma once

#include <windows.h>
#include <atomic>
#include <mutex>
#include "BMPlatform.h"   // ICommChannel 接口定义

class CMySerialChannel : public ICommChannel {
public:
    CMySerialChannel();
    virtual ~CMySerialChannel();

    // ---------- ICommChannel 接口 ----------
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
    // ---------- 状态 ----------
    HANDLE m_hCom;                 // 串口句柄
    HANDLE m_hStopEvent;           // 用于通知线程退出
    HANDLE m_hReadThread;          // 异步接收线程句柄
    DWORD  m_dwThreadId;           // 线程 ID

    HWND   m_hTargetWnd;           // 接收消息的窗口句柄
    ULONG  m_ulMsgId;              // 消息 ID (如 WM_RCV_CHANNEL_DATA)
    bool   m_bAsyncMode;           // 是否启用异步接收

    std::atomic<bool> m_bRunning;  // 线程运行标志
    std::mutex        m_mutex;     // 保护共享资源（如需）

    // ---------- 内部辅助 ----------
    static DWORD WINAPI ReadThreadProc(LPVOID lpParam);
    void ProcessReceivedData(const BYTE* data, DWORD len);
    void SetTimeouts(DWORD readInterval, DWORD readTotalConst);
    void Log(const char* level, const char* fmt, ...);
};