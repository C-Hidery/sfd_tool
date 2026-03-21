#include "BMPlatform.h"
#include <windows.h>
#include <cstring>
#include <iostream>

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

// CProxyChannel 实现
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
	
	char proxyPath[MAX_PATH];
	GetCurrentDirectoryA(MAX_PATH, proxyPath);
	strcat_s(proxyPath, "\\Proxy32.exe");
	
	STARTUPINFOA si = { sizeof(si) };
	PROCESS_INFORMATION pi;
	
	if (CreateProcessA(proxyPath, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		Sleep(1000);
		
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

BOOL CProxyChannel::InitLog(LPCWSTR pszLogName, UINT uiLogType, UINT uiLogLevel, 
							ISpLog *pLogUtil, LPCWSTR pszBinLogFileExt) {
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
	ULONG_PTR ptrValue = (ULONG_PTR)pMemBlock;
	SendCommand(CMD_FREE_MEM, &ptrValue, sizeof(ptrValue), NULL, 0);
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