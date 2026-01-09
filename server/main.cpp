/**
 * 帧同步服务器入口
 *
 * 用法: server.exe [port]
 * 默认端口: 9999
 */
#include <iostream>
#include <csignal>
#include "LockstepServer.h"

lockstep::LockstepServer* g_server = nullptr;

void signalHandler(int signum) {
    std::cout << "\n[Server] Shutting down..." << std::endl;
    if (g_server) {
        g_server->stop();
    }
}

int main(int argc, char* argv[]) {
    // 解析端口
    uint16_t port = 9999;
    if (argc > 1) {
        port = static_cast<uint16_t>(std::atoi(argv[1]));
    }

    // 设置信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    std::cout << "========================================" << std::endl;
    std::cout << "     Lockstep Demo Server v1.0" << std::endl;
    std::cout << "========================================" << std::endl;

    lockstep::LockstepServer server(port);
    g_server = &server;

    if (!server.start()) {
        std::cerr << "[Server] Failed to start" << std::endl;
        return 1;
    }

    std::cout << "[Server] Logic FPS: " << lockstep::LockstepServer::LOGIC_FPS << std::endl;
    std::cout << "[Server] Max players: " << lockstep::LockstepServer::MAX_PLAYERS << std::endl;
    std::cout << "[Server] Press Ctrl+C to stop" << std::endl;
    std::cout << std::endl;

    server.run();

    std::cout << "[Server] Stopped" << std::endl;
    return 0;
}
