#include "framework4cpp/StreamingSessions.h"

#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace framework4cpp {

namespace {

#ifndef _WIN32
using socket_t = int;
constexpr socket_t invalid_socket = -1;
#else
using socket_t = SOCKET;
constexpr socket_t invalid_socket = INVALID_SOCKET;
#endif

// ソケットハンドルを安全にクローズする
void closeSocket(socket_t sock) {
#ifdef _WIN32
    if (sock != INVALID_SOCKET) {
        closesocket(sock);
    }
#else
    if (sock >= 0) {
        ::close(sock);
    }
#endif
}

// ソケットをノンブロッキングモードに設定する
bool setSocketNonBlocking(socket_t sock) {
#ifdef _WIN32
    u_long nonBlocking = 1;
    return ioctlsocket(sock, FIONBIO, &nonBlocking) == 0;
#else
    int flags = ::fcntl(sock, F_GETFL, 0);
    if (flags == -1) {
        return false;
    }
    return ::fcntl(sock, F_SETFL, flags | O_NONBLOCK) != -1;
#endif
}

} // namespace

IpSession::IpSession(const IpInputSettings &settings, GlobalBuffer &buffer)
    : StreamingSession(buffer), settings_(settings) {}

void IpSession::run() {
    if (!settings_.enabled) {
        // 無効化されている場合は処理せず終了
        return;
    }

#ifdef _WIN32
    // WinSock を初期化する
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        throw std::runtime_error("Failed to initialize WinSock");
    }
    wsaInitialized_ = true;
#endif

    struct addrinfo hints {};
    struct addrinfo *result = nullptr;
    // IPv4 を前提にソケット種別とプロトコルを設定
    hints.ai_family = AF_INET;
    hints.ai_socktype = settings_.udp ? SOCK_DGRAM : SOCK_STREAM;
    hints.ai_protocol = settings_.udp ? IPPROTO_UDP : IPPROTO_TCP;

    // ホストが未指定の場合は 0.0.0.0 を利用
    std::string host = settings_.host.empty() ? "0.0.0.0" : settings_.host;
    std::string port = std::to_string(settings_.port);

    // 指定されたエンドポイントの解決を行う
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &result) != 0 || result == nullptr) {
#ifdef _WIN32
        if (wsaInitialized_) {
            WSACleanup();
            wsaInitialized_ = false;
        }
#endif
        throw std::runtime_error("Failed to resolve address: " + host + ":" + port);
    }

    socket_t sock = invalid_socket;
    for (addrinfo *rp = result; rp != nullptr; rp = rp->ai_next) {
        // 各候補でソケットを試しに開く
        sock = static_cast<socket_t>(::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol));
        if (sock == invalid_socket) {
            continue;
        }

        if (settings_.udp) {
            // UDP の場合は bind して受信に備える
            if (::bind(sock, rp->ai_addr, static_cast<int>(rp->ai_addrlen)) == 0) {
                break;
            }
        } else {
            // TCP の場合は対象へ接続する
            if (::connect(sock, rp->ai_addr, static_cast<int>(rp->ai_addrlen)) == 0) {
                break;
            }
        }

        // 失敗したソケットは閉じて次の候補へ
        closeSocket(sock);
        sock = invalid_socket;
    }

    freeaddrinfo(result);

    if (sock == invalid_socket) {
#ifdef _WIN32
        if (wsaInitialized_) {
            WSACleanup();
            wsaInitialized_ = false;
        }
#endif
        throw std::runtime_error("Failed to open network session");
    }

    // ノンブロッキングモードへ切り替えて停止指示で速やかに抜けられるようにする
    if (!setSocketNonBlocking(sock)) {
        closeSocket(sock);
#ifdef _WIN32
        if (wsaInitialized_) {
            WSACleanup();
            wsaInitialized_ = false;
        }
#endif
        throw std::runtime_error("Failed to configure non-blocking socket");
    }

    // 有効なソケットハンドルを保持する
    socketHandle_ = static_cast<std::intptr_t>(sock);

    // 受信バッファを確保して読み取りループを開始
    std::vector<std::uint8_t> buffer(settings_.readChunkSize);
    while (isRunning()) {
        int received = 0;
        if (settings_.udp) {
            // UDP は recvfrom で受信
            received = static_cast<int>(::recvfrom(sock, reinterpret_cast<char *>(buffer.data()), static_cast<int>(buffer.size()), 0, nullptr, nullptr));
        } else {
            // TCP は recv を利用
            received = static_cast<int>(::recv(sock, reinterpret_cast<char *>(buffer.data()), static_cast<int>(buffer.size()), 0));
        }

        if (received > 0) {
            // 受信したデータを共有バッファへ格納
            BufferItem item;
            item.source = settings_.host + ":" + std::to_string(settings_.port);
            item.timestamp = std::chrono::system_clock::now();
            item.payload.assign(buffer.begin(), buffer.begin() + received);
            this->buffer_.push(std::move(item));
        } else {
#ifdef _WIN32
            if ((received == SOCKET_ERROR) && (WSAGetLastError() == WSAEWOULDBLOCK)) {
                // ノンブロッキング待ち時は少しスリープして再試行
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
#else
            if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
#endif
            // 0 バイトまたは致命的なエラーであれば終了
            break;
        }
    }
}

void IpSession::cleanup() {
    if (socketHandle_ != -1) {
        // 保持しているソケットをクローズする
        closeSocket(static_cast<socket_t>(socketHandle_));
        socketHandle_ = -1;
    }
#ifdef _WIN32
    if (wsaInitialized_) {
        // 初期化済みの WinSock を解放する
        WSACleanup();
        wsaInitialized_ = false;
    }
#endif
}

} // namespace framework4cpp

