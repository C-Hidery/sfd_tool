/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#include "BMPlatform.h"
#include "serial/MySerialChannel.h"
#include <iostream>


CBMPlatformApp::CBMPlatformApp() {
	// TODO: add construction code here,
	// Place all significant initialization in InitInstance
	m_pfCreateChannel = NULL;
	m_pfReleaseChannel = NULL;
	m_hChannelLib = NULL;
}

BOOL CBMPlatformApp::InitInstance() {
	// 不再加载 Channel9.dll，直接使用我们的类
	m_pfCreateChannel = [](ICommChannel** ppChannel, CHANNEL_TYPE type) -> BOOL {
		if (type != CHANNEL_TYPE_COM) {
			return FALSE;
		}
		*ppChannel = new CMySerialChannel();
		return (*ppChannel != NULL);
	};
	m_pfReleaseChannel = [](ICommChannel* pChannel) {
		delete pChannel;
	};
	return TRUE;
}

int CBMPlatformApp::ExitInstance() {
	// 无需释放 DLL
	// 如果 m_hChannelLib 还在，可清掉
	m_hChannelLib = NULL;
	m_pfCreateChannel = NULL;
	m_pfReleaseChannel = NULL;
	return TRUE;
}

CBMPlatformApp g_theApp;

CBootModeOpr::CBootModeOpr() {
	m_pChannel = NULL;
	g_theApp.InitInstance();
}

CBootModeOpr::~CBootModeOpr() {
	g_theApp.ExitInstance();
}

BOOL CBootModeOpr::Initialize() {
	if (!g_theApp.m_pfCreateChannel) {
		return FALSE;
	}
	if (!g_theApp.m_pfCreateChannel((ICommChannel **)&m_pChannel, CHANNEL_TYPE_COM)) {
		return FALSE;
	}
	return TRUE;
}

void CBootModeOpr::Uninitialize() {
	if (g_theApp.m_pfReleaseChannel) g_theApp.m_pfReleaseChannel(m_pChannel);
	m_pChannel = NULL;
}

BOOL CBootModeOpr::SetLogVisible(bool bLogVisible)
{
	return m_pChannel->SetLogVisible(bLogVisible);
}

int CBootModeOpr::Read(UCHAR *m_RecvData, int max_len, int dwTimeout) {
	ULONGLONG tBegin;
	ULONGLONG tCur;
	tBegin = GetTickCount64();
	do {
		tCur = GetTickCount64();
		DWORD dwRead = m_pChannel->Read(m_RecvData, max_len, dwTimeout);
		if (dwRead) {
			return dwRead;
		}
	} while ((tCur - tBegin) < dwTimeout);
	return 0;
}

int CBootModeOpr::Write(UCHAR *lpData, int iDataSize) {
	return m_pChannel->Write(lpData, iDataSize);
}

BOOL CBootModeOpr::GetProperty(LONG lFlags, DWORD dwPropertyID, LPVOID pValue) {
	return m_pChannel->GetProperty(lFlags, dwPropertyID, pValue);
}

BOOL CBootModeOpr::SetProperty(LONG lFlags, DWORD dwPropertyID, LPCVOID pValue) {
	return m_pChannel->SetProperty(lFlags, dwPropertyID, pValue);
}

BOOL CBootModeOpr::ConnectChannel(DWORD dwPort, ULONG ulMsgId, DWORD Receiver) {
	if (!dwPort) return FALSE;

	if (Receiver) m_pChannel->SetReceiver(ulMsgId, TRUE, (LPVOID)Receiver);
	CHANNEL_ATTRIBUTE ca;
	ca.ChannelType = CHANNEL_TYPE_COM;
	ca.Com.dwPortNum = dwPort;
	ca.Com.dwBaudRate = 115200;

	m_bOpened = m_pChannel->Open(&ca);
	if (m_bOpened) std::cout << "Successfully connected to port: " << dwPort << std::endl;
	return m_bOpened;
}

BOOL CBootModeOpr::DisconnectChannel() {
	m_pChannel->Close();
	m_bOpened = 0;
	return TRUE;
}

void CBootModeOpr::Clear() {
	m_pChannel->Clear();
}

void CBootModeOpr::FreeMem(LPVOID pMemBlock) {
	m_pChannel->FreeMem(pMemBlock);
}

// 提供 ICommChannel 纯虚析构函数的实现
ICommChannel::~ICommChannel() {
	// 基类析构，空实现即可
}
