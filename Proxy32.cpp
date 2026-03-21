// Proxy32.cpp - 32位代理进程
#include <windows.h>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <cstring>

#pragma warning(disable: 4996)

// 从 BMPlatform.h 复制的必要定义
typedef enum {
	CHANNEL_TYPE_COM = 0,
	CHANNEL_TYPE_SOCKET = 1,
	CHANNEL_TYPE_FILE = 2,
	CHANNEL_TYPE_USBMON = 3
} CHANNEL_TYPE;

typedef struct _CHANNEL_ATTRIBUTE {
	CHANNEL_TYPE ChannelType;
	union {
		struct {
			DWORD dwPortNum;
			DWORD dwBaudRate;
		} Com;
		struct {
			DWORD dwPort;
			DWORD dwIP;
			DWORD dwFlag;
		} Socket;
		struct {
			DWORD dwPackSize;
			DWORD dwPackFreq;
			WCHAR *pFilePath;
		} File;
	};
} CHANNEL_ATTRIBUTE, *PCHANNEL_ATTRIBUTE;

typedef const PCHANNEL_ATTRIBUTE PCCHANNEL_ATTRIBUTE;

class ICommChannel {
public:
	virtual ~ICommChannel() = 0;
	virtual BOOL InitLog(LPCWSTR pszLogName, UINT uiLogType,
		UINT uiLogLevel, void* pLogUtil = NULL,
		LPCWSTR pszBinLogFileExt = NULL) = 0;
	virtual BOOL SetReceiver(ULONG ulMsgId, BOOL bRcvThread, LPCVOID pReceiver) = 0;
	virtual void GetReceiver(ULONG &ulMsgId, BOOL &bRcvThread, LPVOID &pReceiver) = 0;
	virtual BOOL Open(PCCHANNEL_ATTRIBUTE pOpenArgument) = 0;
	virtual void Close() = 0;
	virtual BOOL Clear() = 0;
	virtual DWORD Read(LPVOID lpData, DWORD dwDataSize, DWORD dwTimeOut, DWORD dwReserved = 0) = 0;
	virtual DWORD Write(LPVOID lpData, DWORD dwDataSize, DWORD dwReserved = 0) = 0;
	virtual void FreeMem(LPVOID pMemBlock) = 0;
	virtual BOOL GetProperty(LONG lFlags, DWORD dwPropertyID, LPVOID pValue) = 0;
	virtual BOOL SetProperty(LONG lFlags, DWORD dwPropertyID, LPCVOID pValue) = 0;
};

ICommChannel::~ICommChannel() {}

typedef BOOL(*pfCreateChannel)(ICommChannel **, CHANNEL_TYPE);
typedef void(*pfReleaseChannel)(ICommChannel *);

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
	CMD_FREE_MEM,
	CMD_SHUTDOWN
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

class ChannelManager {
private:
	std::map<DWORD, ICommChannel*> m_channels;
	std::mutex m_mutex;
	DWORD m_nextId;
public:
	ChannelManager() : m_nextId(1) {}
	DWORD AddChannel(ICommChannel* channel) {
		std::lock_guard<std::mutex> lock(m_mutex);
		DWORD id = m_nextId++;
		m_channels[id] = channel;
		return id;
	}
	ICommChannel* GetChannel(DWORD id) {
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_channels.find(id);
		if (it != m_channels.end()) return it->second;
		return NULL;
	}
	void RemoveChannel(DWORD id) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_channels.erase(id);
	}
};

HMODULE g_hChannelLib = NULL;
pfCreateChannel g_pfCreateChannel = NULL;
pfReleaseChannel g_pfReleaseChannel = NULL;
ChannelManager g_channelManager;
HANDLE g_hPipe = INVALID_HANDLE_VALUE;
BOOL g_bRunning = TRUE;

BOOL InitChannelDll() {
	if (g_hChannelLib) return TRUE;
	
	const char* dllPaths[] = {
		"Channel9.dll",
		".\\Channel9.dll",
		"C:\\Program Files (x86)\\SPRD\\Channel9.dll",
		"C:\\SPRD\\Channel9.dll"
	};
	
	for (int i = 0; i < sizeof(dllPaths) / sizeof(dllPaths[0]); i++) {
		g_hChannelLib = LoadLibraryA(dllPaths[i]);
		if (g_hChannelLib) {
			std::cout << "Loaded Channel9.dll from: " << dllPaths[i] << std::endl;
			break;
		}
	}
	
	if (!g_hChannelLib) {
		std::cerr << "Failed to load Channel9.dll. Error: " << GetLastError() << std::endl;
		return FALSE;
	}
	
	g_pfCreateChannel = (pfCreateChannel)GetProcAddress(g_hChannelLib, "CreateChannel");
	g_pfReleaseChannel = (pfReleaseChannel)GetProcAddress(g_hChannelLib, "ReleaseChannel");
	
	if (!g_pfCreateChannel || !g_pfReleaseChannel) {
		std::cerr << "Failed to get function addresses" << std::endl;
		FreeLibrary(g_hChannelLib);
		g_hChannelLib = NULL;
		return FALSE;
	}
	
	return TRUE;
}

DWORD HandleCreateChannel() {
	if (!InitChannelDll()) return 0;
	ICommChannel* pChannel = NULL;
	if (!g_pfCreateChannel(&pChannel, CHANNEL_TYPE_COM)) return 0;
	return g_channelManager.AddChannel(pChannel);
}

BOOL HandleReleaseChannel(DWORD channelId) {
	ICommChannel* pChannel = g_channelManager.GetChannel(channelId);
	if (pChannel && g_pfReleaseChannel) {
		g_pfReleaseChannel(pChannel);
		g_channelManager.RemoveChannel(channelId);
		return TRUE;
	}
	return FALSE;
}

BOOL HandleOpen(DWORD channelId, PCCHANNEL_ATTRIBUTE pAttr) {
	ICommChannel* pChannel = g_channelManager.GetChannel(channelId);
	if (!pChannel) return FALSE;
	return pChannel->Open(pAttr);
}

void HandleClose(DWORD channelId) {
	ICommChannel* pChannel = g_channelManager.GetChannel(channelId);
	if (pChannel) pChannel->Close();
}

DWORD HandleRead(DWORD channelId, LPVOID lpData, DWORD dwDataSize, DWORD dwTimeOut) {
	ICommChannel* pChannel = g_channelManager.GetChannel(channelId);
	if (!pChannel) return 0;
	return pChannel->Read(lpData, dwDataSize, dwTimeOut);
}

DWORD HandleWrite(DWORD channelId, LPVOID lpData, DWORD dwDataSize) {
	ICommChannel* pChannel = g_channelManager.GetChannel(channelId);
	if (!pChannel) return 0;
	return pChannel->Write(lpData, dwDataSize);
}

BOOL HandleClear(DWORD channelId) {
	ICommChannel* pChannel = g_channelManager.GetChannel(channelId);
	if (!pChannel) return FALSE;
	return pChannel->Clear();
}

BOOL HandleSetReceiver(DWORD channelId, ULONG ulMsgId, BOOL bRcvThread, LPCVOID pReceiver) {
	ICommChannel* pChannel = g_channelManager.GetChannel(channelId);
	if (!pChannel) return FALSE;
	return pChannel->SetReceiver(ulMsgId, bRcvThread, pReceiver);
}

BOOL HandleGetProperty(DWORD channelId, LONG lFlags, DWORD dwPropertyID, LPVOID pValue) {
	ICommChannel* pChannel = g_channelManager.GetChannel(channelId);
	if (!pChannel) return FALSE;
	return pChannel->GetProperty(lFlags, dwPropertyID, pValue);
}

BOOL HandleSetProperty(DWORD channelId, LONG lFlags, DWORD dwPropertyID, LPCVOID pValue) {
	ICommChannel* pChannel = g_channelManager.GetChannel(channelId);
	if (!pChannel) return FALSE;
	return pChannel->SetProperty(lFlags, dwPropertyID, pValue);
}

void HandleFreeMem(DWORD channelId, LPVOID pMemBlock) {
	ICommChannel* pChannel = g_channelManager.GetChannel(channelId);
	if (pChannel) pChannel->FreeMem(pMemBlock);
}

void ProcessCommand(const CommandPacket& cmd, ResponsePacket& resp) {
	memset(&resp, 0, sizeof(resp));
	resp.success = TRUE;
	
	switch (cmd.cmd) {
		case CMD_CREATE_CHANNEL:
			resp.result = HandleCreateChannel();
			resp.success = (resp.result != 0);
			break;
		case CMD_RELEASE_CHANNEL:
			resp.success = HandleReleaseChannel(cmd.objectId);
			break;
		case CMD_OPEN: {
			CHANNEL_ATTRIBUTE attr;
			memcpy(&attr, cmd.data, sizeof(CHANNEL_ATTRIBUTE));
			resp.success = HandleOpen(cmd.objectId, &attr);
			break;
		}
		case CMD_CLOSE:
			HandleClose(cmd.objectId);
			resp.success = TRUE;
			break;
		case CMD_READ: {
			DWORD bytesRead = HandleRead(cmd.objectId, resp.data, cmd.param1, cmd.param2);
			resp.result = bytesRead;
			resp.dataSize = bytesRead;
			resp.success = TRUE;
			break;
		}
		case CMD_WRITE: {
			DWORD bytesWritten = HandleWrite(cmd.objectId, (LPVOID)cmd.data, cmd.dataSize);
			resp.result = bytesWritten;
			resp.success = (bytesWritten == cmd.dataSize);
			break;
		}
		case CMD_CLEAR:
			resp.success = HandleClear(cmd.objectId);
			break;
		case CMD_SET_RECEIVER: {
			struct { ULONG ulMsgId; BOOL bRcvThread; LPCVOID pReceiver; }* pParams;
			pParams = (decltype(pParams))cmd.data;
			resp.success = HandleSetReceiver(cmd.objectId, pParams->ulMsgId, pParams->bRcvThread, pParams->pReceiver);
			break;
		}
		case CMD_GET_PROPERTY: {
			struct { LONG lFlags; DWORD dwPropertyID; }* pParams;
			pParams = (decltype(pParams))cmd.data;
			DWORD value = 0;
			resp.success = HandleGetProperty(cmd.objectId, pParams->lFlags, pParams->dwPropertyID, &value);
			if (resp.success) resp.result = value;
			break;
		}
		case CMD_SET_PROPERTY: {
			struct { LONG lFlags; DWORD dwPropertyID; DWORD dwValue; }* pParams;
			pParams = (decltype(pParams))cmd.data;
			resp.success = HandleSetProperty(cmd.objectId, pParams->lFlags, pParams->dwPropertyID, &pParams->dwValue);
			break;
		}
		case CMD_FREE_MEM:
			HandleFreeMem(cmd.objectId, (LPVOID)(ULONG_PTR)cmd.param1);
			resp.success = TRUE;
			break;
		case CMD_SHUTDOWN:
			g_bRunning = FALSE;
			resp.success = TRUE;
			break;
		default:
			resp.success = FALSE;
			break;
	}
}

int main() {
	std::cout << "SPRD Channel Proxy32 started (PID: " << GetCurrentProcessId() << ")" << std::endl;
	
	g_hPipe = CreateNamedPipeA(
		"\\\\.\\pipe\\SPRDChannelProxy",
		PIPE_ACCESS_DUPLEX,
		PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
		1,
		sizeof(ResponsePacket),
		sizeof(CommandPacket),
		0,
		NULL
	);
	
	if (g_hPipe == INVALID_HANDLE_VALUE) {
		std::cerr << "Failed to create named pipe. Error: " << GetLastError() << std::endl;
		return 1;
	}
	
	std::cout << "Waiting for client connection..." << std::endl;
	
	if (!ConnectNamedPipe(g_hPipe, NULL)) {
		if (GetLastError() != ERROR_PIPE_CONNECTED) {
			std::cerr << "Failed to connect pipe. Error: " << GetLastError() << std::endl;
			CloseHandle(g_hPipe);
			return 1;
		}
	}
	
	std::cout << "Client connected" << std::endl;
	
	CommandPacket cmd;
	DWORD bytesRead;
	
	while (g_bRunning) {
		memset(&cmd, 0, sizeof(cmd));
		if (!ReadFile(g_hPipe, &cmd, sizeof(cmd), &bytesRead, NULL)) {
			if (GetLastError() == ERROR_BROKEN_PIPE) break;
			continue;
		}
		if (bytesRead == 0) continue;
		
		ResponsePacket resp;
		ProcessCommand(cmd, resp);
		
		DWORD bytesWritten;
		WriteFile(g_hPipe, &resp, sizeof(resp), &bytesWritten, NULL);
		
		if (cmd.cmd == CMD_SHUTDOWN) break;
	}
	
	if (g_hPipe != INVALID_HANDLE_VALUE) {
		FlushFileBuffers(g_hPipe);
		DisconnectNamedPipe(g_hPipe);
		CloseHandle(g_hPipe);
	}
	
	if (g_hChannelLib) {
		FreeLibrary(g_hChannelLib);
	}
	
	std::cout << "Proxy32 shutting down" << std::endl;
	return 0;
}