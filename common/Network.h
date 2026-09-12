/**
 * @file Network.h
 * @brief 跨平台网络封装 - 统一 Windows 和 Linux 的 Socket API
 *
 * 【技术概述】
 * Windows 和 Linux 的 Socket API 存在显著差异：
 * - Windows 使用 Winsock2，需要初始化/清理，socket 类型是 SOCKET (无符号)
 * - Linux 使用 POSIX socket，无需初始化，socket 类型是 int
 *
 * 本文件通过条件编译和类型别名，封装这些差异，提供统一的接口。
 *
 * 【面试要点】
 * 1. Q: Windows 和 Linux 网络编程的主要差异是什么？
 *    A: - 头文件不同：winsock2.h vs sys/socket.h
 *       - 初始化：Windows 需要 WSAStartup，Linux 不需要
 *       - 关闭函数：closesocket vs close
 *       - 错误获取：WSAGetLastError vs errno
 *       - 类型定义：SOCKET (unsigned) vs int
 *       - 非阻塞设置：ioctlsocket vs fcntl
 *
 * 2. Q: 为什么游戏选择 UDP 而不是 TCP？
 *    A: TCP 的问题：
 *       - 队头阻塞：一个包丢失会阻塞后续所有包
 *       - 重传延迟：TCP 重传增加延迟
 *       - 连接开销：三次握手增加延迟
 *       UDP 的优势：
 *       - 无连接，无队头阻塞
 *       - 可自定义可靠性（只重传关键数据）
 *       - 延迟更低
 *
 * 3. Q: 什么是非阻塞 I/O？为什么需要它？
 *    A: 阻塞 I/O 在没有数据时会挂起线程
 *       非阻塞 I/O 立即返回（成功或 WOULD_BLOCK）
 *       游戏主循环需要同时处理输入、渲染、网络，不能阻塞
 *
 * 【生产实践】
 * 1. 错误处理：生产代码应检查每个 socket 调用的返回值
 * 2. 超时设置：使用 setsockopt 设置发送/接收超时
 * 3. 缓冲区大小：调整 SO_SNDBUF/SO_RCVBUF 优化性能
 * 4. 多路复用：大量连接时使用 select/poll/epoll
 */
#pragma once

// ============ 平台相关头文件和定义 ============
/**
 * 【条件编译策略】
 * 使用 _WIN32 宏区分 Windows 和类 Unix 系统
 * _WIN32 在 32 位和 64 位 Windows 上都有定义
 */
#ifdef _WIN32
    /**
     * 【Windows 网络头文件】
     *
     * WIN32_LEAN_AND_MEAN：减少 windows.h 包含的内容
     * - 排除 Winsock 1.x 定义（避免与 Winsock 2 冲突）
     * - 排除多媒体、DDE 等不常用的 API
     * - 加快编译速度
     *
     * 【头文件顺序很重要】
     * 必须在 windows.h 之前定义 WIN32_LEAN_AND_MEAN
     * 否则会导致 Winsock 版本冲突
     */
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>   // Winsock 2 主头文件
    #include <ws2tcpip.h>   // TCP/IP 扩展（inet_pton 等）
    #pragma comment(lib, "ws2_32.lib")  // 自动链接 Winsock 库

    /**
     * 【类型别名和宏定义】
     * 提供跨平台一致的接口
     */
    using socket_t = SOCKET;    // Windows: SOCKET 是 unsigned 类型
    #define INVALID_SOCKET_VALUE INVALID_SOCKET     // (~0)
    #define SOCKET_ERROR_VALUE SOCKET_ERROR         // (-1)
    #define close_socket closesocket                // Windows 关闭函数
    #define get_socket_error() WSAGetLastError()    // Windows 错误码

    /**
     * @brief 初始化 Winsock 库
     *
     * 【Windows 特有】
     * 必须在使用任何 Winsock 函数之前调用
     * MAKEWORD(2, 2) 请求 Winsock 2.2 版本
     *
     * 【面试考点】
     * Q: WSAStartup 的参数是什么意思？
     * A: MAKEWORD(2, 2) 表示请求 Winsock 2.2 版本
     *    低字节是主版本，高字节是次版本
     */
    inline void init_network() {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    }

    /**
     * @brief 清理 Winsock 库
     *
     * 【Windows 特有】
     * 程序退出前应调用，释放 Winsock 资源
     * 每个 WSAStartup 应对应一个 WSACleanup
     */
    inline void cleanup_network() {
        WSACleanup();
    }

    /**
     * @brief 设置 socket 为非阻塞模式
     * @param sock 要设置的 socket
     *
     * 【Windows 实现】
     * 使用 ioctlsocket 函数
     * FIONBIO 是"非阻塞 I/O"的控制码
     * mode = 1 表示开启非阻塞
     */
    inline void set_nonblocking(socket_t sock) {
        u_long mode = 1;
        ioctlsocket(sock, FIONBIO, &mode);
    }

#else
    /**
     * 【Linux/Unix 网络头文件】
     *
     * POSIX 标准定义，所有类 Unix 系统通用
     */
    #include <sys/socket.h>     // socket(), bind(), sendto(), recvfrom()
    #include <netinet/in.h>     // sockaddr_in, INADDR_ANY
    #include <arpa/inet.h>      // inet_pton(), inet_ntop(), htons()
    #include <unistd.h>         // close()
    #include <fcntl.h>          // fcntl(), F_GETFL, F_SETFL, O_NONBLOCK
    #include <errno.h>          // errno

    /**
     * 【类型别名和宏定义】
     * 与 Windows 版本对应，提供统一接口
     */
    using socket_t = int;       // Linux: socket 描述符是 int
    #define INVALID_SOCKET_VALUE (-1)   // 无效 socket 值
    #define SOCKET_ERROR_VALUE (-1)     // 错误返回值
    #define close_socket close          // 使用标准 close
    #define get_socket_error() errno    // 使用标准 errno

    /**
     * @brief 初始化网络（Linux 空实现）
     *
     * 【Linux 不需要初始化】
     * POSIX socket 是内核功能，无需应用层初始化
     */
    inline void init_network() {}

    /**
     * @brief 清理网络（Linux 空实现）
     */
    inline void cleanup_network() {}

    /**
     * @brief 设置 socket 为非阻塞模式
     * @param sock 要设置的 socket
     *
     * 【Linux 实现】
     * 使用 fcntl (file control) 函数
     * 1. F_GETFL 获取当前标志
     * 2. 添加 O_NONBLOCK 标志
     * 3. F_SETFL 设置新标志
     *
     * 【面试考点】
     * Q: fcntl 和 ioctl 有什么区别？
     * A: fcntl 操作文件描述符标志（POSIX 标准）
     *    ioctl 操作设备特定功能（非标准，各设备不同）
     */
    inline void set_nonblocking(socket_t sock) {
        int flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    }
#endif

#include <string>
#include <cstring>

namespace lockstep {

// ============ UDP Socket 封装类 ============
/**
 * @class UdpSocket
 * @brief 跨平台 UDP Socket 封装
 *
 * 【设计理念】
 * 1. RAII：构造时创建，析构时关闭
 * 2. 封装差异：统一 Windows/Linux API
 * 3. 简洁接口：只提供必要的功能
 *
 * 【使用示例 - 服务器端】
 * UdpSocket sock;
 * sock.create();
 * sock.bind(9999);
 * sock.setNonBlocking();
 * while (running) {
 *     sockaddr_in from;
 *     int n = sock.recvFrom(buffer, sizeof(buffer), from);
 *     if (n > 0) { ... }
 * }
 *
 * 【使用示例 - 客户端】
 * UdpSocket sock;
 * sock.create();
 * sock.setNonBlocking();
 * sockaddr_in serverAddr = UdpSocket::makeAddr("127.0.0.1", 9999);
 * sock.sendTo(data, len, serverAddr);
 */
class UdpSocket {
public:
    /**
     * @brief 构造函数
     *
     * 初始化 socket 为无效值
     * socket 的实际创建在 create() 方法中
     */
    UdpSocket() : sock_(INVALID_SOCKET_VALUE) {}

    /**
     * @brief 析构函数
     *
     * 【RAII 原则】
     * 自动关闭 socket，防止资源泄漏
     */
    ~UdpSocket() {
        close();
    }

    /**
     * @brief 创建 UDP socket
     * @return 成功返回 true
     *
     * 【参数说明】
     * - AF_INET: IPv4 地址族
     * - SOCK_DGRAM: 数据报类型（UDP）
     * - IPPROTO_UDP: UDP 协议
     *
     * 【面试考点】
     * Q: SOCK_DGRAM 和 SOCK_STREAM 的区别？
     * A: SOCK_DGRAM: 数据报，无连接，保留消息边界（UDP）
     *    SOCK_STREAM: 字节流，面向连接，无消息边界（TCP）
     */
    bool create() {
        sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        return sock_ != INVALID_SOCKET_VALUE;
    }

    /**
     * @brief 绑定到指定端口
     * @param port 端口号
     * @return 成功返回 true
     *
     * 【使用场景】
     * - 服务器必须 bind 到固定端口，客户端才能连接
     * - 客户端通常不需要 bind，系统自动分配临时端口
     *
     * 【地址说明】
     * - INADDR_ANY: 绑定所有网卡（0.0.0.0）
     * - htons: Host TO Network Short，转换端口的字节序
     *
     * 【面试考点】
     * Q: 为什么需要 htons？
     * A: 网络字节序是大端序，端口号需要转换
     *    x86 是小端序，直接用会导致端口号错误
     */
    bool bind(uint16_t port) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        return ::bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != SOCKET_ERROR_VALUE;
    }

    /**
     * @brief 设置为非阻塞模式
     *
     * 【非阻塞模式的行为】
     * - sendto: 如果发送缓冲区满，立即返回错误
     * - recvfrom: 如果没有数据，立即返回错误（EAGAIN/EWOULDBLOCK）
     *
     * 【为什么游戏需要非阻塞】
     * 游戏主循环每帧都要检查网络，但不能等待
     * 非阻塞允许"尝试接收，没有就继续"
     */
    void setNonBlocking() {
        set_nonblocking(sock_);
    }

    /**
     * @brief 发送数据到指定地址
     * @param data 数据指针
     * @param len 数据长度
     * @param addr 目标地址
     * @return 发送的字节数，失败返回 -1
     *
     * 【UDP 发送特点】
     * - 无连接：每次发送都要指定目标地址
     * - 不可靠：数据可能丢失、乱序
     * - 有边界：一次 sendto 对应一次 recvfrom
     *
     * 【注意】
     * 返回值是发送到内核缓冲区的字节数
     * 不代表对方一定收到了
     */
    int sendTo(const uint8_t* data, size_t len, const sockaddr_in& addr) {
        return sendto(sock_, reinterpret_cast<const char*>(data),
                     static_cast<int>(len), 0,
                     reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    }

    /**
     * @brief 接收数据并获取发送方地址
     * @param buffer 接收缓冲区
     * @param maxLen 缓冲区大小
     * @param fromAddr [out] 发送方地址
     * @return 接收的字节数，无数据返回 -1
     *
     * 【返回值说明】
     * - > 0: 成功接收的字节数
     * - 0: 对方发送了空数据包（有效的 UDP 包）
     * - -1: 错误或无数据（非阻塞模式下常见）
     *
     * 【跨平台差异】
     * 地址长度参数类型不同：
     * - Windows: int
     * - Linux: socklen_t
     *
     * 【使用模式】
     * int n = sock.recvFrom(buf, sizeof(buf), from);
     * if (n > 0) {
     *     // 处理收到的数据
     *     processPacket(buf, n, from);
     * }
     */
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

    /**
     * @brief 关闭 socket
     *
     * 【幂等性】
     * 多次调用是安全的，会检查是否已关闭
     */
    void close() {
        if (sock_ != INVALID_SOCKET_VALUE) {
            close_socket(sock_);
            sock_ = INVALID_SOCKET_VALUE;
        }
    }

    /**
     * @brief 获取底层 socket 句柄
     * @return socket 描述符
     *
     * 【使用场景】
     * 需要使用原生 API 时（如 select、setsockopt）
     */
    socket_t handle() const { return sock_; }

    // ============ 静态辅助方法 ============

    /**
     * @brief 创建 sockaddr_in 结构
     * @param ip IP 地址字符串（如 "127.0.0.1"）
     * @param port 端口号
     * @return 填充好的地址结构
     *
     * 【inet_pton 说明】
     * "presentation to numeric"
     * 将可读的 IP 字符串转换为二进制格式
     *
     * 【使用示例】
     * sockaddr_in serverAddr = UdpSocket::makeAddr("192.168.1.100", 9999);
     */
    static sockaddr_in makeAddr(const std::string& ip, uint16_t port) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
        return addr;
    }

    /**
     * @brief 将地址结构转换为可读字符串
     * @param addr 地址结构
     * @return "IP:Port" 格式的字符串
     *
     * 【inet_ntop 说明】
     * "numeric to presentation"
     * 将二进制 IP 转换为可读字符串
     *
     * 【使用场景】
     * 日志输出、调试打印
     *
     * 【输出格式】
     * "192.168.1.100:9999"
     */
    static std::string addrToString(const sockaddr_in& addr) {
        char buf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf));
        return std::string(buf) + ":" + std::to_string(ntohs(addr.sin_port));
    }

    /**
     * @brief 比较两个地址是否相同
     * @param a 地址 1
     * @param b 地址 2
     * @return 相同返回 true
     *
     * 【比较内容】
     * 1. IP 地址（sin_addr.s_addr）
     * 2. 端口号（sin_port）
     *
     * 【使用场景】
     * 识别已连接的玩家：
     * for (auto& player : players) {
     *     if (UdpSocket::addrEquals(player.addr, fromAddr)) { ... }
     * }
     */
    static bool addrEquals(const sockaddr_in& a, const sockaddr_in& b) {
        return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
    }

private:
    socket_t sock_;  // 底层 socket 描述符
};

} // namespace lockstep

/**
 * 【扩展阅读】
 *
 * 1. UDP vs TCP 详细对比：
 *    | 特性 | UDP | TCP |
 *    |------|-----|-----|
 *    | 连接 | 无连接 | 面向连接 |
 *    | 可靠性 | 不可靠 | 可靠（重传） |
 *    | 顺序 | 不保证 | 保证顺序 |
 *    | 边界 | 保留消息边界 | 字节流 |
 *    | 开销 | 小（8字节头） | 大（20字节头） |
 *    | 延迟 | 低 | 可能高（队头阻塞） |
 *    | 适用 | 游戏、语音 | 网页、文件 |
 *
 * 2. 游戏常用的 UDP 可靠性方案：
 *    - ACK 确认：收到包后回复确认
 *    - 序号检测：检测丢包和乱序
 *    - 选择重传：只重传丢失的包
 *    - 冗余发送：重要数据多发几次
 *
 * 3. 生产环境网络优化：
 *    - SO_REUSEADDR：允许地址复用
 *    - SO_RCVBUF/SO_SNDBUF：调整缓冲区
 *    - IP_TOS：设置服务类型（低延迟）
 *    - 多线程：专门的网络收发线程
 *
 * 4. 常见网络问题及解决：
 *    | 问题 | 原因 | 解决方案 |
 *    |------|------|----------|
 *    | 地址已在使用 | 未正确关闭 | SO_REUSEADDR |
 *    | 发送失败 | 缓冲区满 | 非阻塞 + 重试 |
 *    | 接收不到 | 防火墙 | 检查端口开放 |
 *    | NAT 穿透 | 内网地址 | STUN/TURN |
 *
 * 5. 跨平台网络库推荐：
 *    - Boost.Asio：C++ 标准候选
 *    - libuv：Node.js 底层，跨平台
 *    - ENet：专为游戏设计的可靠 UDP
 *    - RakNet：成熟的游戏网络库
 */
