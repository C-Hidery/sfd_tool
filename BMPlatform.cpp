// BMPlatform.cpp - 修改为支持代理模式
#include "BMPlatform.h"
#include <iostream>
#include <string>
#include <vector>

int g_bOpened = 0;
int& m_bOpened = g_bOpened;

// 管道命令定义
enum ProxyCommand {
	CMD_CREATE_CHANNEL = 1,
	CMD_RELEASE_CHANNEL,
	CMD_OPEN,
	CMD_CLOSE,
	CMD_READ,
	CMD_WRITE,
	CMD_CLEAR,
	CMD_GET_PROPERTY,
	CMD_SET_PROPERTY,
	CMD_SET_RECEIVER,
	CMD_GET_RECEIVER,
	CMD_INIT_LOG,
	CMD_FREE_MEM
};

#pragma pack(push, 1)
struct CommandPacket {
	DWORD cmd;
	DWORD objectId;
	DWORD param1;
	DWORD param2;
	DWORD param3;
	DWORD param4;
	DWORD dataSize;
	BYTE data[4096];
};

struct ResponsePacket {
	BOOL success;
	DWORD result;
	DWORD dataSize;
	BYTE data[4096];
};
#pragma pack(pop)

// 代理通道实现
CProxyChannel::CProxyChannel() : m_hPipe(INVALID_HANDLE_VALUE), m_objectId(0) {
}

CProxyChannel::~CProxyChannel() {
	if (m_hPipe != INVALID_HANDLE_VALUE) {
		CloseHandle(m_hPipe);
	}
}

BOOL CProxyChannel::ConnectToProxy() {
	if (m_hPipe != INVALID_HANDLE_VALUE) {
		return TRUE;
	}
	
	// 尝试连接已运行的代理进程
	for (int i = 0; i < 10; i++) {
		m_hPipe = CreateFileA(
			"\\\\.\\pipe\\SPRDChannelProxy",
			GENERIC_READ | GENERIC_WRITE,
			0, NULL, OPEN_EXISTING, 0, NULL);
		
		if (m_hPipe != INVALID_HANDLE_VALUE) {
			DWORD pipeMode = PIPE_READMODE_MESSAGE;
			SetNamedPipeHandleState(m_hPipe, &pipeMode, NULL, NULL);
			
			// 创建通道对象
			CommandPacket cmd = {0};
			cmd.cmd = CMD_CREATE_CHANNEL;
			
			ResponsePacket resp;
			DWORD bytesWritten, bytesRead;
			
			if (WriteFile(m_hPipe, &cmd, sizeof(cmd), &bytesWritten, NULL) &&
				ReadFile(m_hPipe, &resp, sizeof(resp), &bytesRead, NULL) &&
				resp.success) {
				m_objectId = resp.result;
				return TRUE;
			}
			
			CloseHandle(m_hPipe);
			m_hPipe = INVALID_HANDLE_VALUE;
		}
		
		Sleep(100);
	}
	
	// 启动代理进程
	char proxyPath[MAX_PATH];
	GetCurrentDirectoryA(MAX_PATH, proxyPath);
	strcat_s(proxyPath, "\\Proxy32.exe");
	
	STARTUPINFOA si = { sizeof(si) };
	PROCESS_INFORMATION pi;
	
	if (CreateProcessA(proxyPath, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		
		// 等待代理启动
		Sleep(1000);
		
		// 再次尝试连接
		for (int i = 0; i < 10; i++) {
			m_hPipe = CreateFileA(
				"\\\\.\\pipe\\SPRDChannelProxy",
				GENERIC_READ | GENERIC_WRITE,
				0, NULL, OPEN_EXISTING, 0, NULL);
			
			if (m_hPipe != INVALID_HANDLE_VALUE) {
				DWORD pipeMode = PIPE_READMODE_MESSAGE;
				SetNamedPipeHandleState(m_hPipe, &pipeMode, NULL, NULL);
				
				CommandPacket cmd = {0};
				cmd.cmd = CMD_CREATE_CHANNEL;
				
				ResponsePacket resp;
				DWORD bytesWritten, bytesRead;
				
				if (WriteFile(m_hPipe, &cmd, sizeof(cmd), &bytesWritten, NULL) &&
					ReadFile(m_hPipe, &resp, sizeof(resp), &bytesRead, NULL) &&
					resp.success) {
					m_objectId = resp.result;
					return TRUE;
				}
				
				CloseHandle(m_hPipe);
				m_hPipe = INVALID_HANDLE_VALUE;
			}
			Sleep(100);
		}
	}
	
	return FALSE;
}

BOOL CProxyChannel::SendCommand(DWORD cmd, void* params, DWORD paramSize, void* resp, DWORD respSize) {
	if (!ConnectToProxy()) {
		return FALSE;
	}
	
	CommandPacket packet = {0};
	packet.cmd = cmd;
	packet.objectId = m_objectId;
	
	if (params && paramSize > 0 && paramSize <= sizeof(packet.data)) {
		packet.dataSize = paramSize;
		memcpy(packet.data, params, paramSize);
	}
	
	DWORD bytesWritten;
	if (!WriteFile(m_hPipe, &packet, sizeof(packet), &bytesWritten, NULL)) {
		return FALSE;
	}
	
	ResponsePacket response;
	DWORD bytesRead;
	if (!ReadFile(m_hPipe, &response, sizeof(response), &bytesRead, NULL)) {
		return FALSE;
	}
	
	if (resp && respSize > 0 && response.dataSize > 0) {
		DWORD copySize = min(respSize, response.dataSize);
		memcpy(resp, response.data, copySize);
	}
	
	return response.success;
}

BOOL CProxyChannel::InitLog(LPCWSTR pszLogName, UINT uiLogType, UINT uiLogLevel, ISpLog *pLogUtil, LPCWSTR pszBinLogFileExt) {
	// 简化处理，返回成功
	return TRUE;
}

BOOL CProxyChannel::SetReceiver(ULONG ulMsgId, BOOL bRcvThread, LPCVOID pReceiver) {
	struct {
		ULONG ulMsgId;
		BOOL bRcvThread;
		LPCVOID pReceiver;
	} params = { ulMsgId, bRcvThread, pReceiver };
	
	return SendCommand(CMD_SET_RECEIVER, &params, sizeof(params), NULL, 0);
}

void CProxyChannel::GetReceiver(ULONG &ulMsgId, BOOL &bRcvThread, LPVOID &pReceiver) {
	// 简化处理
	ulMsgId = 0;
	bRcvThread = FALSE;
	pReceiver = NULL;
}

BOOL CProxyChannel::Open(PCCHANNEL_ATTRIBUTE pOpenArgument) {
	return SendCommand(CMD_OPEN, (void*)pOpenArgument, sizeof(CHANNEL_ATTRIBUTE), NULL, 0);
}

void CProxyChannel::Close() {
	SendCommand(CMD_CLOSE, NULL, 0, NULL, 0);
}

BOOL CProxyChannel::Clear() {
	BOOL result;
	if (SendCommand(CMD_CLEAR, NULL, 0, &result, sizeof(result))) {
		return result;
	}
	return FALSE;
}

DWORD CProxyChannel::Read(LPVOID lpData, DWORD dwDataSize, DWORD dwTimeOut, DWORD dwReserved) {
	struct {
		DWORD dwDataSize;
		DWORD dwTimeOut;
		DWORD dwReserved;
	} params = { dwDataSize, dwTimeOut, dwReserved };
	
	DWORD bytesRead = 0;
	if (SendCommand(CMD_READ, &params, sizeof(params), &bytesRead, sizeof(bytesRead))) {
		if (bytesRead > 0 && lpData) {
			// 实际数据需要通过额外的管道读取
			ResponsePacket resp;
			DWORD bytesRead2;
			ReadFile(m_hPipe, &resp, sizeof(resp), &bytesRead2, NULL);
			if (resp.dataSize > 0) {
				memcpy(lpData, resp.data, min(dwDataSize, resp.dataSize));
				return resp.dataSize;
			}
		}
		return bytesRead;
	}
	return 0;
}

DWORD CProxyChannel::Write(LPVOID lpData, DWORD dwDataSize, DWORD dwReserved) {
	struct {
		DWORD dwDataSize;
		DWORD dwReserved;
	} params = { dwDataSize, dwReserved };
	
	DWORD bytesWritten = 0;
	if (SendCommand(CMD_WRITE, &params, sizeof(params), &bytesWritten, sizeof(bytesWritten))) {
		// 发送实际数据
		if (dwDataSize > 0 && lpData) {
			ResponsePacket resp;
			DWORD bytesWritten2;
			WriteFile(m_hPipe, lpData, dwDataSize, &bytesWritten2, NULL);
			ReadFile(m_hPipe, &resp, sizeof(resp), &bytesWritten2, NULL);
		}
		return bytesWritten;
	}
	return 0;
}

void CProxyChannel::FreeMem(LPVOID pMemBlock) {
	SendCommand(CMD_FREE_MEM, &pMemBlock, sizeof(pMemBlock), NULL, 0);
}

BOOL CProxyChannel::GetProperty(LONG lFlags, DWORD dwPropertyID, LPVOID pValue) {
	struct {
		LONG lFlags;
		DWORD dwPropertyID;
	} params = { lFlags, dwPropertyID };
	
	return SendCommand(CMD_GET_PROPERTY, &params, sizeof(params), pValue, sizeof(DWORD));
}

BOOL CProxyChannel::SetProperty(LONG lFlags, DWORD dwPropertyID, LPCVOID pValue) {
	struct {
		LONG lFlags;
		DWORD dwPropertyID;
		DWORD dwValue;
	} params = { lFlags, dwPropertyID, pValue ? *(DWORD*)pValue : 0 };
	
	return SendCommand(CMD_SET_PROPERTY, &params, sizeof(params), NULL, 0);
}

// 创建通道的包装函数 - 决定使用真实DLL还是代理
static BOOL CreateChannelWithProxy(ICommChannel **ppChannel, CHANNEL_TYPE type) {
	*ppChannel = new CProxyChannel();
	return TRUE;
}

static void ReleaseChannelWithProxy(ICommChannel *pChannel) {
	if (pChannel) {
		delete pChannel;
	}
}

// CBMPlatformApp 实现
CBMPlatformApp::CBMPlatformApp() {
	m_pfCreateChannel = NULL;
	m_pfReleaseChannel = NULL;
	m_hChannelLib = NULL;
	m_bUseProxy = TRUE;  // 默认使用代理
}

BOOL CBMPlatformApp::InitInstance() {
	// 检查是否需要使用代理（64位系统自动启用）
#ifdef _WIN64
	m_bUseProxy = TRUE;
#else
	// 32位系统可以直接加载DLL
	m_hChannelLib = LoadLibrary(_T("Channel9.dll"));
	if (m_hChannelLib) {
		m_pfCreateChannel = (pfCreateChannel)GetProcAddress(m_hChannelLib, "CreateChannel");
		m_pfReleaseChannel = (pfReleaseChannel)GetProcAddress(m_hChannelLib, "ReleaseChannel");
		
		if (m_pfCreateChannel && m_pfReleaseChannel) {
			m_bUseProxy = FALSE;
			return TRUE;
		}
	}
	m_bUseProxy = TRUE;
#endif
	
	// 使用代理模式
	if (m_bUseProxy) {
		m_pfCreateChannel = CreateChannelWithProxy;
		m_pfReleaseChannel = ReleaseChannelWithProxy;
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
	
	return TRUE;
}

CBMPlatformApp g_theApp;

// CBootModeOpr 实现（完全保持不变）
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
	if (g_theApp.m_pfReleaseChannel && m_pChannel) {
		g_theApp.m_pfReleaseChannel(m_pChannel);
	}
	m_pChannel = NULL;
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
	
	m_bOpened = m_pChannel->Open(&ca);
	if (m_bOpened) std::cout << "Successfully connected to port: " << dwPort << std::endl;
	return m_bOpened;
}

BOOL CBootModeOpr::DisconnectChannel() {
	m_pChannel->Close();
	m_bOpened = 0;
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