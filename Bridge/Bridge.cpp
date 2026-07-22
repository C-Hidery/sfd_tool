/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 * 
 * 32-bit Bridge Server: loads original BMPlatform and handles socket requests.
 * Compile as 32-bit EXE.
 */

#include "BMPlatform.h"          // 原始硬件操作类
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <vector>
#include <iostream>
#include <cstring>
#include <fstream>
#include <string>
#include <cstdlib>

#pragma comment(lib, "ws2_32.lib")

// ---------- 协议定义（与 WrapperProxy 完全相同） ----------
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
    FID_GET_OPENED  = 100
};

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

// ---------- 每个客户端连接对应一个 CBootModeOpr 对象 ----------
struct ClientContext {
    SOCKET sock;
    CBootModeOpr* opr;
    bool initialized;
    bool isOpen;          // 缓存通道状态，用于 FID_GET_OPENED
};

// ---------- 发送响应 ----------
static void SendResponse(SOCKET s, const std::vector<uint8_t>& data) {
    uint32_t len_net = htonl(static_cast<uint32_t>(data.size()));
    if (send(s, reinterpret_cast<const char*>(&len_net), 4, 0) != 4) return;
    if (!data.empty()) {
        send(s, reinterpret_cast<const char*>(data.data()), 
             static_cast<int>(data.size()), 0);
    }
}

// ---------- 处理单个客户端（线程函数） ----------
static void HandleClient(SOCKET clientSock) {
    ClientContext ctx{};
    ctx.sock = clientSock;
    ctx.opr = new CBootModeOpr();
    ctx.initialized = false;
    ctx.isOpen = false;

    while (true) {
        uint32_t fid_net = 0;
        int ret = recv(clientSock, reinterpret_cast<char*>(&fid_net), 4, 0);
        if (ret <= 0) break;
        FuncID fid = static_cast<FuncID>(ntohl(fid_net));

        std::vector<uint8_t> respData;

        switch (fid) {
        case FID_CREATE: {
            respData.push_back(1);
            break;
        }
        case FID_DESTROY: {
            delete ctx.opr;
            ctx.opr = nullptr;
            respData.push_back(1);
            SendResponse(clientSock, respData);
            closesocket(clientSock);
            return;
        }
        case FID_INITIALIZE: {
            BOOL ok = ctx.opr->Initialize();
            ctx.initialized = (ok == TRUE);
            respData.push_back(ok ? 1 : 0);
            break;
        }
        case FID_UNINITIALIZE: {
            ctx.opr->Uninitialize();
            ctx.initialized = false;
            respData.push_back(1);
            break;
        }
        case FID_READ: {
            if (!ctx.isOpen) {
                respData.resize(4);
                *reinterpret_cast<int*>(respData.data()) = 0;
                break;
            }
            uint32_t max_len = 0, dwTimeout = 0;
            if (recv(clientSock, reinterpret_cast<char*>(&max_len), 4, 0) != 4) goto disconnect;
            if (recv(clientSock, reinterpret_cast<char*>(&dwTimeout), 4, 0) != 4) goto disconnect;
            max_len = ntohl(max_len);
            dwTimeout = ntohl(dwTimeout);

            std::vector<UCHAR> buffer(max_len);
            int len = ctx.opr->Read(buffer.data(), max_len, dwTimeout);
            if (len < 0) {
                ctx.isOpen = false;
                len = 0;
            }
            respData.resize(4 + len);
            *reinterpret_cast<int*>(respData.data()) = len;
            if (len > 0) {
                memcpy(respData.data() + 4, buffer.data(), len);
            }
            break;
        }
        case FID_WRITE: {
            if (!ctx.isOpen) {
                respData.resize(4);
                *reinterpret_cast<int*>(respData.data()) = 0;
                break;
            }
            uint32_t dataSize = 0;
            if (recv(clientSock, reinterpret_cast<char*>(&dataSize), 4, 0) != 4) goto disconnect;
            dataSize = ntohl(dataSize);
            std::vector<UCHAR> buffer(dataSize);
            size_t total = 0;
            while (total < dataSize) {
                int r = recv(clientSock, reinterpret_cast<char*>(buffer.data() + total),
                             static_cast<int>(dataSize - total), 0);
                if (r <= 0) goto disconnect;
                total += r;
            }
            int written = ctx.opr->Write(buffer.data(), dataSize);
            if (written < 0) {
                ctx.isOpen = false;
                written = 0;
            }
            respData.resize(4);
            *reinterpret_cast<int*>(respData.data()) = written;
            break;
        }
        case FID_CONNECT: {
            uint32_t dwPort, ulMsgId, Receiver;
            if (recv(clientSock, reinterpret_cast<char*>(&dwPort), 4, 0) != 4) goto disconnect;
            if (recv(clientSock, reinterpret_cast<char*>(&ulMsgId), 4, 0) != 4) goto disconnect;
            if (recv(clientSock, reinterpret_cast<char*>(&Receiver), 4, 0) != 4) goto disconnect;
            dwPort = ntohl(dwPort);
            ulMsgId = ntohl(ulMsgId);
            Receiver = ntohl(Receiver);

            BOOL ok = ctx.opr->ConnectChannel(dwPort, ulMsgId, Receiver);
            ctx.isOpen = (ok == TRUE);
            // 同步更新全局 m_bOpened（app_state.h）
            m_bOpened = ctx.isOpen ? 1 : 0;
            respData.push_back(ok ? 1 : 0);
            break;
        }
        case FID_DISCONNECT: {
            ctx.opr->DisconnectChannel();
            ctx.isOpen = false;
            m_bOpened = 0;
            respData.push_back(1);
            break;
        }
        case FID_GETPROP: {
            uint32_t lFlags, dwPropertyID, bufSize;
            if (recv(clientSock, reinterpret_cast<char*>(&lFlags), 4, 0) != 4) goto disconnect;
            if (recv(clientSock, reinterpret_cast<char*>(&dwPropertyID), 4, 0) != 4) goto disconnect;
            if (recv(clientSock, reinterpret_cast<char*>(&bufSize), 4, 0) != 4) goto disconnect;
            lFlags = ntohl(lFlags);
            dwPropertyID = ntohl(dwPropertyID);
            bufSize = ntohl(bufSize);

            std::vector<UCHAR> buffer(bufSize);
            BOOL ok = ctx.opr->GetProperty(lFlags, dwPropertyID, buffer.data());
            respData.resize(5 + (ok ? bufSize : 0));
            respData[0] = ok ? 1 : 0;
            *reinterpret_cast<DWORD*>(&respData[1]) = ok ? bufSize : 0;
            if (ok && bufSize > 0) {
                memcpy(respData.data() + 5, buffer.data(), bufSize);
            }
            break;
        }
        case FID_SETPROP: {
            uint32_t lFlags, dwPropertyID, dataSize;
            if (recv(clientSock, reinterpret_cast<char*>(&lFlags), 4, 0) != 4) goto disconnect;
            if (recv(clientSock, reinterpret_cast<char*>(&dwPropertyID), 4, 0) != 4) goto disconnect;
            if (recv(clientSock, reinterpret_cast<char*>(&dataSize), 4, 0) != 4) goto disconnect;
            lFlags = ntohl(lFlags);
            dwPropertyID = ntohl(dwPropertyID);
            dataSize = ntohl(dataSize);

            std::vector<UCHAR> buffer(dataSize);
            size_t total = 0;
            while (total < dataSize) {
                int r = recv(clientSock, reinterpret_cast<char*>(buffer.data() + total),
                             static_cast<int>(dataSize - total), 0);
                if (r <= 0) goto disconnect;
                total += r;
            }
            BOOL ok = ctx.opr->SetProperty(lFlags, dwPropertyID, buffer.data());
            respData.push_back(ok ? 1 : 0);
            break;
        }
        case FID_CLEAR: {
            ctx.opr->Clear();
            respData.push_back(1);
            break;
        }
        case FID_FREEMEM: {
            respData.push_back(1);
            break;
        }
        case FID_GET_OPENED: {
            respData.push_back(ctx.isOpen ? 1 : 0);
            break;
        }
        default:
            respData.push_back(0);
            break;
        }

        SendResponse(clientSock, respData);
        continue;

    disconnect:
        break;
    }

    if (ctx.opr) {
        ctx.opr->DisconnectChannel();
        ctx.opr->Uninitialize();
        delete ctx.opr;
    }
    closesocket(clientSock);
}

// ---------- 主函数 ----------
int main(int argc, char* argv[]) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return 1;
    }

    int port = GetBridgePort();
    if (argc >= 2) {
        port = std::stoi(argv[1]);  // 命令行参数优先
    }

    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) {
        std::cerr << "socket failed" << std::endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(static_cast<u_short>(port));

    if (bind(listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "bind failed on port " << port << std::endl;
        closesocket(listenSock);
        WSACleanup();
        return 1;
    }

    if (listen(listenSock, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "listen failed" << std::endl;
        closesocket(listenSock);
        WSACleanup();
        return 1;
    }

    std::cout << "Bridge listening on 127.0.0.1:" << port << " (32-bit)" << std::endl;

    while (true) {
        SOCKET clientSock = accept(listenSock, nullptr, nullptr);
        if (clientSock == INVALID_SOCKET) {
            std::cerr << "accept failed" << std::endl;
            break;
        }
        std::thread(HandleClient, clientSock).detach();
    }

    closesocket(listenSock);
    WSACleanup();
    return 0;
}