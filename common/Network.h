/**
 * Network.h - 跨平台网络封装
 *
 * 封装Windows和Linux的socket API差异
 */
#pragma once

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")

    using socket_t = SOCKET;
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
    #define SOCKET_ERROR_VALUE SOCKET_ERROR
    #define close_socket closesocket
    #define get_socket_error() WSAGetLastError()

    inline void init_network() {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    }

    inline void cleanup_network() {
        WSACleanup();
    }

    inline void set_nonblocking(socket_t sock) {
        u_long mode = 1;
        ioctlsocket(sock, FIONBIO, &mode);
    }

#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>

    using socket_t = int;
    #define INVALID_SOCKET_VALUE (-1)
    #define SOCKET_ERROR_VALUE (-1)
    #define close_socket close
    #define get_socket_error() errno

    inline void init_network() {}
    inline void cleanup_network() {}

    inline void set_nonblocking(socket_t sock) {
        int flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    }
#endif

#include <string>
#include <cstring>

namespace lockstep {

// UDP Socket封装
class UdpSocket {
public:
    UdpSocket() : sock_(INVALID_SOCKET_VALUE) {}

    ~UdpSocket() {
        close();
    }

    bool create() {
        sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        return sock_ != INVALID_SOCKET_VALUE;
    }

    bool bind(uint16_t port) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        return ::bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != SOCKET_ERROR_VALUE;
    }

    void setNonBlocking() {
        set_nonblocking(sock_);
    }

    int sendTo(const uint8_t* data, size_t len, const sockaddr_in& addr) {
        return sendto(sock_, reinterpret_cast<const char*>(data),
                     static_cast<int>(len), 0,
                     reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    }

    int recvFrom(uint8_t* buffer, size_t maxLen, sockaddr_in& fromAddr) {
        #ifdef _WIN32
            int addrLen = sizeof(fromAddr);
        #else
            socklen_t addrLen = sizeof(fromAddr);
        #endif

        return recvfrom(sock_, reinterpret_cast<char*>(buffer),
                       static_cast<int>(maxLen), 0,
                       reinterpret_cast<sockaddr*>(&fromAddr), &addrLen);
    }

    void close() {
        if (sock_ != INVALID_SOCKET_VALUE) {
            close_socket(sock_);
            sock_ = INVALID_SOCKET_VALUE;
        }
    }

    socket_t handle() const { return sock_; }

    static sockaddr_in makeAddr(const std::string& ip, uint16_t port) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
        return addr;
    }

    static std::string addrToString(const sockaddr_in& addr) {
        char buf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf));
        return std::string(buf) + ":" + std::to_string(ntohs(addr.sin_port));
    }

    static bool addrEquals(const sockaddr_in& a, const sockaddr_in& b) {
        return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
    }

private:
    socket_t sock_;
};

} // namespace lockstep
