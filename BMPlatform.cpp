#include "BMPlatform.h"
#include <iostream>

// 全局变量定义（保持原有）
int m_bOpened = 0;

CBMPlatformApp::CBMPlatformApp() {
	m_pfCreateChannel = NULL;
	m_pfReleaseChannel = NULL;
	m_hChannelLib = NULL;
	m_bUseProxy = TRUE;
	m_bInitialized = FALSE;  // 初始化标志
}

// 创建代理通道的包装函数
static BOOL CreateChannelWithProxy(ICommChannel **ppChannel, CHANNEL_TYPE type) {
	*ppChannel = new CProxyChannel();
	return TRUE;
}

static void ReleaseChannelWithProxy(ICommChannel *pChannel) {
	if (pChannel) {
		delete pChannel;
	}
}

BOOL CBMPlatformApp::InitInstance() {
	// 防止重复初始化
	if (m_bInitialized) {
		return TRUE;
	}
	
#ifdef _WIN64
	m_bUseProxy = TRUE;
#else
	m_hChannelLib = LoadLibrary(_T("Channel9.dll"));
	if (m_hChannelLib) {
		m_pfCreateChannel = (pfCreateChannel)GetProcAddress(m_hChannelLib, "CreateChannel");
		m_pfReleaseChannel = (pfReleaseChannel)GetProcAddress(m_hChannelLib, "ReleaseChannel");
		if (m_pfCreateChannel && m_pfReleaseChannel) {
			m_bUseProxy = FALSE;
			m_bInitialized = TRUE;
			return TRUE;
		}
	}
	m_bUseProxy = TRUE;
#endif

	if (m_bUseProxy) {
		m_pfCreateChannel = CreateChannelWithProxy;
		m_pfReleaseChannel = ReleaseChannelWithProxy;
		m_bInitialized = TRUE;
		return TRUE;
	}
	return FALSE;
}

int CBMPlatformApp::ExitInstance() {
	if (!m_bUseProxy && m_hChannelLib) {
		FreeLibrary(m_hChannelLib);
		m_hChannelLib = NULL;
	}
	m_pfCreateChannel = NULL;
	m_pfReleaseChannel = NULL;
	m_bInitialized = FALSE;
	return TRUE;
}

CBMPlatformApp g_theApp;

CBootModeOpr::CBootModeOpr() {
	m_pChannel = NULL;
	c_m_bOpened = FALSE;  // 初始化成员变量
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
	if (g_theApp.m_pfReleaseChannel && m_pChannel) {
		g_theApp.m_pfReleaseChannel(m_pChannel);
	}
	m_pChannel = NULL;
}

int CBootModeOpr::Read(UCHAR *m_RecvData, int max_len, int dwTimeout) {
	ULONGLONG tBegin, tCur;
	tBegin = GetTickCount64();
	do {
		tCur = GetTickCount64();
		DWORD dwRead = m_pChannel->Read(m_RecvData, max_len, dwTimeout);
		if (dwRead) {
			return dwRead;
		}
	} while ((tCur - tBegin) < (ULONGLONG)dwTimeout);
	return 0;
}

int CBootModeOpr::Write(UCHAR *lpData, int iDataSize) {
	return m_pChannel->Write(lpData, iDataSize);
}

BOOL CBootModeOpr::ConnectChannel(DWORD dwPort, ULONG ulMsgId, DWORD Receiver) {
	if (!dwPort) return FALSE;
	if (Receiver) m_pChannel->SetReceiver(ulMsgId, TRUE, (LPVOID)Receiver);
	CHANNEL_ATTRIBUTE ca;
	ca.ChannelType = CHANNEL_TYPE_COM;
	ca.Com.dwPortNum = dwPort;
	ca.Com.dwBaudRate = 115200;
	c_m_bOpened = m_pChannel->Open(&ca);  // 使用成员变量
	m_bOpened = c_m_bOpened;  // 同步全局变量，供外部使用
	if (c_m_bOpened) std::cout << "Successfully connected to port: " << dwPort << std::endl;
	return c_m_bOpened;
}

BOOL CBootModeOpr::DisconnectChannel() {
	m_pChannel->Close();
	c_m_bOpened = FALSE;  // 重置成员变量
	m_bOpened = FALSE;    // 同步全局变量
	return TRUE;
}

BOOL CBootModeOpr::GetProperty(LONG lFlags, DWORD dwPropertyID, LPVOID pValue) {
	return m_pChannel->GetProperty(lFlags, dwPropertyID, pValue);
}

BOOL CBootModeOpr::SetProperty(LONG lFlags, DWORD dwPropertyID, LPCVOID pValue) {
	return m_pChannel->SetProperty(lFlags, dwPropertyID, pValue);
}

void CBootModeOpr::Clear() {
	m_pChannel->Clear();
}

void CBootModeOpr::FreeMem(LPVOID pMemBlock) {
	m_pChannel->FreeMem(pMemBlock);
}