/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#ifndef WRAPPER_H
#define WRAPPER_H
#pragma once

#include <windows.h>
#include <winsock2.h>
#include <mutex>

// ---------- 结构体定义（包含 Socket 代理所需成员） ----------
typedef struct {
    SOCKET sock;           // Socket 连接句柄
    std::mutex mtx;        // 线程安全锁
} ClassHandle;

#ifdef __cplusplus
extern "C" {
#endif

// ---------- Wrapper API ----------
ClassHandle *createClass();
void destroyClass(ClassHandle *handle);

BOOL call_Initialize(ClassHandle *handle);
void call_Uninitialize(ClassHandle *handle);

int call_Read(ClassHandle *handle, UCHAR *m_RecvData, int max_len, int dwTimeout);
int call_Write(ClassHandle *handle, UCHAR *lpData, int iDataSize);

BOOL call_ConnectChannel(ClassHandle *handle, DWORD dwPort, ULONG ulMsgId, DWORD Receiver);
BOOL call_DisconnectChannel(ClassHandle *handle);

BOOL call_GetProperty(ClassHandle *handle, LONG lFlags, DWORD dwPropertyID, LPVOID pValue);
BOOL call_SetProperty(ClassHandle *handle, LONG lFlags, DWORD dwPropertyID, LPCVOID pValue);

void call_Clear(ClassHandle *handle);
void call_FreeMem(ClassHandle *handle, LPVOID pMemBlock);

#ifdef __cplusplus
}
#endif

#endif // WRAPPER_H