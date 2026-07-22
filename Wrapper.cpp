/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 * 
 * 64-bit proxy: forwards all Wrapper API calls via socket to 32-bit Bridge.exe.
 * Include this file instead of the original Wrapper.cpp in your 64-bit project.
 */

#include "Wrapper.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mutex>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstring>
#include <fstream>
#include <string>
#include <cstdlib>
#include <tlhelp32.h>

#pragma comment(lib, "ws2_32.lib")

extern int& m_bOpened;
static std::mutex g_stateMutex;

// ---------- 线程安全读写 m_bOpened ----------
static void SetOpened(int value) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    m_bOpened = value;
}

static int GetOpened() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    return m_bOpened;
}

// ---------- 协议定义（与 Bridge 保持一致） ----------
enum FuncID : uint32_t {
    FID_CREATE      = 0,
    FID_DESTROY     = 1,
    FID_INITIALIZE  = 2,
    FID_UNINITIALIZE= 3,
    FID_READ        = 4,
    FID_WRITE       = 5,
    FID_CONNECT     = 6,
    FID_DISCONNECT  = 7,
    FID_GETPROP     = 8,
    FID_SETPROP     = 9,
    FID_CLEAR       = 10,
    FID_FREEMEM     = 11,
    FID_GET_OPENED  = 100     // 查询当前连接状态
};

// ---------- 全局变量 ----------
static std::atomic<bool> g_monitorRunning{false};
static std::thread g_monitorThread;
static SOCKET g_monitorSock = INVALID_SOCKET;
static std::mutex g_monitorMutex;

// ---------- 读取端口号配置 ----------
static int GetBridgePort() {
    int defaultPort = 52001;
    std::ifstream file("bridge.ini");
    if (!file.is_open()) return defaultPort;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '[' || line[0] == ';') continue;
        size_t pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        while (!key.empty() && key.back() == ' ') key.pop_back();
        while (!value.empty() && value.front() == ' ') value.erase(0, 1);
        if (key == "Port") {
            return std::stoi(value);
        }
    }
    return defaultPort;
}

// ---------- Bridge 进程管理 ----------
static bool IsBridgeRunning() {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32 pe = { sizeof(PROCESSENTRY32) };
    bool found = false;
    if (Process32First(hSnapshot, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, "Bridge.exe") == 0) {
                found = true;
                break;
            }
        } while (Process32Next(hSnapshot, &pe));
    }
    CloseHandle(hSnapshot);
    return found;
}

static bool LaunchBridge() {
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    char* lastSlash = strrchr(exePath, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    strcat_s(exePath, "Bridge.exe");

    int port = GetBridgePort();
    char cmdLine[512];
    sprintf_s(cmdLine, "\"%sBridge.exe\" %d", exePath, port);

    STARTUPINFOA si = { sizeof(STARTUPINFOA) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi;
    if (!CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, exePath, &si, &pi)) {
        return false;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

// ---------- Socket 连接 ----------
static SOCKET ConnectToBridge() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2,2), &wsaData);
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return INVALID_SOCKET;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(static_cast<u_short>(GetBridgePort()));

    if (connect(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

// ---------- 发送请求并接收响应 ----------
static bool SendRequest(SOCKET s, FuncID fid, const std::vector<uint8_t>& reqData, std::vector<uint8_t>& respData) {
    uint32_t fid_net = htonl(static_cast<uint32_t>(fid));
    if (send(s, reinterpret_cast<const char*>(&fid_net), 4, 0) != 4)
        return false;

    if (!reqData.empty()) {
        if (send(s, reinterpret_cast<const char*>(reqData.data()), 
                 static_cast<int>(reqData.size()), 0) != static_cast<int>(reqData.size()))
            return false;
    }

    uint32_t len_net = 0;
    if (recv(s, reinterpret_cast<char*>(&len_net), 4, 0) != 4)
        return false;
    uint32_t len = ntohl(len_net);

    respData.resize(len);
    size_t total = 0;
    while (total < len) {
        int ret = recv(s, reinterpret_cast<char*>(respData.data()) + total, 
                       static_cast<int>(len - total), 0);
        if (ret <= 0) return false;
        total += ret;
    }
    return true;
}

// ---------- 查询 Bridge 中的 m_bOpened 状态 ----------
static int QueryOpenedStatus(SOCKET s) {
    std::vector<uint8_t> req, resp;
    if (!SendRequest(s, FID_GET_OPENED, req, resp))
        return 0;
    if (resp.size() < 4) return 0;
    return *reinterpret_cast<int*>(resp.data());
}

// ---------- 后台监控线程 ----------
static void MonitorThread(SOCKET s) {
    g_monitorRunning = true;
    while (g_monitorRunning) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        std::lock_guard<std::mutex> lock(g_monitorMutex);
        if (s == INVALID_SOCKET) break;

        int state = QueryOpenedStatus(s);
        SetOpened(state);   // 直接同步 Bridge 的状态（0, 1, -1）
    }
}

// ---------- Wrapper API 实现 ----------

ClassHandle* createClass() {
    SOCKET s = ConnectToBridge();
    if (s == INVALID_SOCKET) {
        if (!LaunchBridge()) {
            SetOpened(-1);
            return nullptr;
        }
        // 等待 Bridge 启动（最多3秒）
        for (int i = 0; i < 30; i++) {
            Sleep(100);
            s = ConnectToBridge();
            if (s != INVALID_SOCKET) break;
        }
        if (s == INVALID_SOCKET) {
            SetOpened(-1);
            return nullptr;
        }
    }

    std::vector<uint8_t> req, resp;
    if (!SendRequest(s, FID_CREATE, req, resp)) {
        closesocket(s);
        SetOpened(-1);
        return nullptr;
    }
    if (resp.size() < 1 || resp[0] == 0) {
        closesocket(s);
        SetOpened(-1);
        return nullptr;
    }

    ClassHandle* h = new ClassHandle;
    h->sock = s;

    // 初始状态：未连接
    SetOpened(0);

    {
        std::lock_guard<std::mutex> lock(g_monitorMutex);
        if (!g_monitorRunning || !g_monitorThread.joinable()) {
            g_monitorSock = s;
            if (g_monitorThread.joinable()) g_monitorThread.join();
            g_monitorThread = std::thread(MonitorThread, s);
        }
    }
    return h;
}

void destroyClass(ClassHandle* handle) {
    if (!handle) return;

    g_monitorRunning = false;
    if (g_monitorThread.joinable()) {
        g_monitorThread.join();
    }

    {
        std::lock_guard<std::mutex> lock(handle->mtx);
        std::vector<uint8_t> req, resp;
        SendRequest(handle->sock, FID_DESTROY, req, resp);
        closesocket(handle->sock);
    }
    delete handle;

    SetOpened(0);
}

BOOL call_Initialize(ClassHandle* handle) {
    if (!handle) return FALSE;
    std::lock_guard<std::mutex> lock(handle->mtx);
    std::vector<uint8_t> req, resp;
    if (!SendRequest(handle->sock, FID_INITIALIZE, req, resp)) return FALSE;
    return (!resp.empty() && resp[0] != 0);
}

void call_Uninitialize(ClassHandle* handle) {
    if (!handle) return;
    std::lock_guard<std::mutex> lock(handle->mtx);
    std::vector<uint8_t> req, resp;
    SendRequest(handle->sock, FID_UNINITIALIZE, req, resp);
    // Bridge 端会自己更新 m_bOpened，监控线程会同步
}

int call_Read(ClassHandle* handle, UCHAR* m_RecvData, int max_len, int dwTimeout) {
    if (!handle || !m_RecvData || max_len <= 0) return 0;

    // 快速检查：如果 m_bOpened != 1，直接返回 0
    if (GetOpened() != 1) return 0;

    std::lock_guard<std::mutex> lock(handle->mtx);
    std::vector<uint8_t> req(8);
    *reinterpret_cast<int*>(&req[0]) = max_len;
    *reinterpret_cast<int*>(&req[4]) = dwTimeout;

    std::vector<uint8_t> resp;
    if (!SendRequest(handle->sock, FID_READ, req, resp)) {
        SetOpened(-1);
        return 0;
    }
    if (resp.size() < 4) return 0;

    int readLen = *reinterpret_cast<int*>(&resp[0]);
    if (readLen > max_len) readLen = max_len;
    if (readLen > 0 && resp.size() > 4) {
        int copyLen = (readLen < (int)resp.size() - 4) ? readLen : (int)resp.size() - 4;
        memcpy(m_RecvData, resp.data() + 4, copyLen);
    }
    return readLen;
}

int call_Write(ClassHandle* handle, UCHAR* lpData, int iDataSize) {
    if (!handle || !lpData || iDataSize <= 0) return 0;

    // 快速检查：如果 m_bOpened != 1，直接返回 0
    if (GetOpened() != 1) return 0;

    std::lock_guard<std::mutex> lock(handle->mtx);
    std::vector<uint8_t> req(4 + iDataSize);
    *reinterpret_cast<int*>(&req[0]) = iDataSize;
    memcpy(req.data() + 4, lpData, iDataSize);

    std::vector<uint8_t> resp;
    if (!SendRequest(handle->sock, FID_WRITE, req, resp)) {
        SetOpened(-1);
        return 0;
    }
    if (resp.size() < 4) return 0;
    return *reinterpret_cast<int*>(&resp[0]);
}

BOOL call_ConnectChannel(ClassHandle* handle, DWORD dwPort, ULONG ulMsgId, DWORD Receiver) {
    if (!handle) return FALSE;

    std::lock_guard<std::mutex> lock(handle->mtx);
    std::vector<uint8_t> req(12);
    *reinterpret_cast<DWORD*>(&req[0]) = dwPort;
    *reinterpret_cast<ULONG*>(&req[4]) = ulMsgId;
    *reinterpret_cast<DWORD*>(&req[8]) = Receiver;

    std::vector<uint8_t> resp;
    if (!SendRequest(handle->sock, FID_CONNECT, req, resp)) {
        SetOpened(-1);
        return FALSE;
    }
    BOOL ok = (!resp.empty() && resp[0] != 0);
    // 注意：Bridge 端已经更新了 m_bOpened，监控线程会在 500ms 内同步
    // 但为了更实时，可以主动查询一次
    if (ok) {
        // 立即查询最新状态
        int state = QueryOpenedStatus(handle->sock);
        SetOpened(state);
    } else {
        SetOpened(-1);
    }
    return ok;
}

BOOL call_DisconnectChannel(ClassHandle* handle) {
    if (!handle) return FALSE;

    std::lock_guard<std::mutex> lock(handle->mtx);
    std::vector<uint8_t> req, resp;
    if (!SendRequest(handle->sock, FID_DISCONNECT, req, resp)) {
        SetOpened(-1);
        return FALSE;
    }
    // Bridge 端会更新 m_bOpened，主动查询一次同步
    int state = QueryOpenedStatus(handle->sock);
    SetOpened(state);
    return (!resp.empty() && resp[0] != 0);
}

BOOL call_GetProperty(ClassHandle* handle, LONG lFlags, DWORD dwPropertyID, LPVOID pValue) {
    if (!handle || !pValue) return FALSE;

    std::lock_guard<std::mutex> lock(handle->mtx);
    const DWORD MAX_PROP_SIZE = 1024;
    std::vector<uint8_t> req(12);
    *reinterpret_cast<LONG*>(&req[0]) = lFlags;
    *reinterpret_cast<DWORD*>(&req[4]) = dwPropertyID;
    *reinterpret_cast<DWORD*>(&req[8]) = MAX_PROP_SIZE;

    std::vector<uint8_t> resp;
    if (!SendRequest(handle->sock, FID_GETPROP, req, resp)) return FALSE;
    if (resp.size() < 5) return FALSE;

    BOOL ok = (resp[0] != 0);
    if (ok) {
        DWORD dataLen = *reinterpret_cast<DWORD*>(&resp[1]);
        if (dataLen > MAX_PROP_SIZE) dataLen = MAX_PROP_SIZE;
        if (dataLen > 0 && resp.size() > 5) {
            memcpy(pValue, resp.data() + 5, dataLen);
        }
    }
    return ok;
}

BOOL call_SetProperty(ClassHandle* handle, LONG lFlags, DWORD dwPropertyID, LPCVOID pValue) {
    if (!handle || !pValue) return FALSE;

    std::lock_guard<std::mutex> lock(handle->mtx);
    const DWORD MAX_PROP_SIZE = 1024;
    std::vector<uint8_t> req(12 + MAX_PROP_SIZE);
    *reinterpret_cast<LONG*>(&req[0]) = lFlags;
    *reinterpret_cast<DWORD*>(&req[4]) = dwPropertyID;
    *reinterpret_cast<DWORD*>(&req[8]) = MAX_PROP_SIZE;
    memcpy(req.data() + 12, pValue, MAX_PROP_SIZE);

    std::vector<uint8_t> resp;
    if (!SendRequest(handle->sock, FID_SETPROP, req, resp)) return FALSE;
    return (!resp.empty() && resp[0] != 0);
}

void call_Clear(ClassHandle* handle) {
    if (!handle) return;
    std::lock_guard<std::mutex> lock(handle->mtx);
    std::vector<uint8_t> req, resp;
    SendRequest(handle->sock, FID_CLEAR, req, resp);
}

void call_FreeMem(ClassHandle* handle, LPVOID pMemBlock) {
    if (!handle) return;
    (void)pMemBlock;
    std::lock_guard<std::mutex> lock(handle->mtx);
    std::vector<uint8_t> req, resp;
    SendRequest(handle->sock, FID_FREEMEM, req, resp);
}
