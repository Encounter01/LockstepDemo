/**
 * 帧同步客户端入口
 *
 * 控制台版本，使用ASCII字符渲染
 * 用法: client.exe [server_ip] [port]
 * 默认: localhost:9999
 *
 * 控制:
 *   W/A/S/D - 移动
 *   Space   - 攻击
 *   Q       - 退出
 */
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include "LockstepClient.h"

#ifdef _WIN32
    #include <conio.h>
    #include <windows.h>

    void clearScreen() {
        system("cls");
    }

    void setCursorPos(int x, int y) {
        COORD pos = {static_cast<SHORT>(x), static_cast<SHORT>(y)};
        SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), pos);
    }

    void hideCursor() {
        CONSOLE_CURSOR_INFO info;
        info.dwSize = 1;
        info.bVisible = FALSE;
        SetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info);
    }

    int getKeyPressed() {
        if (_kbhit()) {
            return _getch();
        }
        return 0;
    }
#else
    #include <termios.h>
    #include <unistd.h>
    #include <fcntl.h>

    void clearScreen() {
        std::cout << "\033[2J\033[H";
    }

    void setCursorPos(int x, int y) {
        std::cout << "\033[" << (y + 1) << ";" << (x + 1) << "H";
    }

    void hideCursor() {
        std::cout << "\033[?25l";
    }

    int getKeyPressed() {
        struct termios oldt, newt;
        int ch;
        int oldf;

        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

        ch = getchar();

        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        fcntl(STDIN_FILENO, F_SETFL, oldf);

        return (ch == EOF) ? 0 : ch;
    }
#endif

using namespace lockstep;

// 渲染配置
constexpr int SCREEN_WIDTH = 80;
constexpr int SCREEN_HEIGHT = 30;
constexpr float SCALE_X = static_cast<float>(SCREEN_WIDTH) / GameWorld::MAP_WIDTH;
constexpr float SCALE_Y = static_cast<float>(SCREEN_HEIGHT - 5) / GameWorld::MAP_HEIGHT;

// 屏幕缓冲
char screenBuffer[SCREEN_HEIGHT][SCREEN_WIDTH + 1];

// 玩家颜色符号
const char PLAYER_CHARS[] = {'@', '#', '$', '%'};
const char BULLET_CHAR = '*';
const char DEAD_CHAR = 'X';

void initScreen() {
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        memset(screenBuffer[y], ' ', SCREEN_WIDTH);
        screenBuffer[y][SCREEN_WIDTH] = '\0';
    }
}

void drawBorder() {
    // 顶部边框
    for (int x = 0; x < SCREEN_WIDTH; ++x) {
        screenBuffer[0][x] = '-';
    }
    // 底部边框（游戏区域）
    for (int x = 0; x < SCREEN_WIDTH; ++x) {
        screenBuffer[SCREEN_HEIGHT - 6][x] = '-';
    }
    // 左右边框
    for (int y = 0; y < SCREEN_HEIGHT - 5; ++y) {
        screenBuffer[y][0] = '|';
        screenBuffer[y][SCREEN_WIDTH - 1] = '|';
    }
}

void renderWorld(const GameWorld& world, uint32_t myPlayerId) {
    initScreen();
    drawBorder();

    // 渲染子弹
    for (const auto& bullet : world.bullets) {
        if (!bullet.alive) continue;

        int sx = static_cast<int>(bullet.pos.x.toFloat() * SCALE_X);
        int sy = static_cast<int>(bullet.pos.y.toFloat() * SCALE_Y) + 1;

        if (sx >= 1 && sx < SCREEN_WIDTH - 1 && sy >= 1 && sy < SCREEN_HEIGHT - 6) {
            screenBuffer[sy][sx] = BULLET_CHAR;
        }
    }

    // 渲染玩家
    for (const auto& player : world.players) {
        int sx = static_cast<int>(player.pos.x.toFloat() * SCALE_X);
        int sy = static_cast<int>(player.pos.y.toFloat() * SCALE_Y) + 1;

        if (sx >= 1 && sx < SCREEN_WIDTH - 1 && sy >= 1 && sy < SCREEN_HEIGHT - 6) {
            if (player.alive) {
                char ch = PLAYER_CHARS[player.ownerId % 4];
                screenBuffer[sy][sx] = ch;

                // 标记自己
                if (player.ownerId == myPlayerId) {
                    if (sx > 1) screenBuffer[sy][sx - 1] = '[';
                    if (sx < SCREEN_WIDTH - 2) screenBuffer[sy][sx + 1] = ']';
                }
            } else {
                screenBuffer[sy][sx] = DEAD_CHAR;
            }
        }
    }

    // 渲染状态栏
    int statusY = SCREEN_HEIGHT - 5;
    snprintf(screenBuffer[statusY], SCREEN_WIDTH,
             "Frame: %u  |  Bullets: %zu",
             world.currentFrame, world.bullets.size());

    // 渲染玩家状态
    statusY++;
    for (size_t i = 0; i < world.players.size(); ++i) {
        const auto& p = world.players[i];
        char marker = (p.ownerId == myPlayerId) ? '>' : ' ';
        snprintf(screenBuffer[statusY + i], SCREEN_WIDTH,
                 "%c P%u [%c]: HP=%3d  Kills=%u  Deaths=%u  %s",
                 marker, p.ownerId, PLAYER_CHARS[p.ownerId % 4],
                 p.hp, p.kills, p.deaths,
                 p.alive ? "ALIVE" : "DEAD");
    }

    // 输出到屏幕
    setCursorPos(0, 0);
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        std::cout << screenBuffer[y] << "\n";
    }
    std::cout.flush();
}

void showWaitingScreen(uint32_t playerId) {
    clearScreen();
    std::cout << "========================================\n";
    std::cout << "     Lockstep Demo Client v1.0\n";
    std::cout << "========================================\n\n";
    std::cout << "  You are Player " << playerId << "\n\n";
    std::cout << "  Waiting for other players to join...\n\n";
    std::cout << "  Controls:\n";
    std::cout << "    W/A/S/D - Move\n";
    std::cout << "    Space   - Attack\n";
    std::cout << "    Q       - Quit\n\n";
    std::cout << "========================================\n";
}

int main(int argc, char* argv[]) {
    // 解析参数
    std::string host = "127.0.0.1";
    uint16_t port = 9999;

    if (argc > 1) {
        host = argv[1];
    }
    if (argc > 2) {
        port = static_cast<uint16_t>(std::atoi(argv[2]));
    }

    std::cout << "========================================\n";
    std::cout << "     Lockstep Demo Client v1.0\n";
    std::cout << "========================================\n\n";
    std::cout << "Connecting to " << host << ":" << port << "...\n";

    LockstepClient client;

    if (!client.connect(host, port)) {
        std::cerr << "Failed to connect!\n";
        return 1;
    }

    // 等待连接确认
    auto startTime = std::chrono::steady_clock::now();
    while (client.getState() == LockstepClient::State::CONNECTING) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();

        if (elapsed > 5) {
            std::cerr << "Connection timeout!\n";
            return 1;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (client.getState() == LockstepClient::State::DISCONNECTED) {
        std::cerr << "Connection failed!\n";
        return 1;
    }

    hideCursor();
    bool lastWaiting = false;
    bool running = true;

    // 主循环
    while (running && client.isConnected()) {
        // 处理输入
        int key = getKeyPressed();
        if (key) {
            switch (key) {
                case 'q':
                case 'Q':
                    running = false;
                    break;

                case 'w':
                case 'W':
                    client.setMoveDirection(0);  // 上
                    break;
                case 'd':
                case 'D':
                    client.setMoveDirection(2);  // 右
                    break;
                case 's':
                case 'S':
                    client.setMoveDirection(4);  // 下
                    break;
                case 'a':
                case 'A':
                    client.setMoveDirection(6);  // 左
                    break;

                case ' ':
                    client.setAction(0x01, true);  // 攻击
                    break;

                default:
                    client.setMoveDirection(8);  // 停止
                    client.setAction(0x01, false);
                    break;
            }
        } else {
            // 没有按键时停止移动
            client.setMoveDirection(8);
            client.setAction(0x01, false);
        }

        // 更新游戏
        if (client.isPlaying()) {
            client.update();
            renderWorld(client.getWorld(), client.getPlayerId());
        } else if (client.getState() == LockstepClient::State::WAITING) {
            if (!lastWaiting) {
                showWaitingScreen(client.getPlayerId());
                lastWaiting = true;
            }
        } else if (client.getState() == LockstepClient::State::GAME_OVER) {
            // 显示最终状态
            renderWorld(client.getWorld(), client.getPlayerId());
            setCursorPos(SCREEN_WIDTH / 2 - 10, SCREEN_HEIGHT / 2);
            std::cout << "=== GAME OVER ===" << std::endl;

            int winner = client.getWorld().getWinnerId();
            setCursorPos(SCREEN_WIDTH / 2 - 10, SCREEN_HEIGHT / 2 + 1);
            if (winner == static_cast<int>(client.getPlayerId())) {
                std::cout << "  YOU WIN!  " << std::endl;
            } else if (winner >= 0) {
                std::cout << "Winner: Player " << winner << std::endl;
            } else {
                std::cout << "   DRAW   " << std::endl;
            }

            setCursorPos(SCREEN_WIDTH / 2 - 10, SCREEN_HEIGHT / 2 + 3);
            std::cout << "Press Q to quit" << std::endl;
        }

        // 控制帧率
        std::this_thread::sleep_for(std::chrono::milliseconds(16));  // ~60 FPS
    }

    client.disconnect();

    clearScreen();
    std::cout << "Thanks for playing!\n";

    return 0;
}
