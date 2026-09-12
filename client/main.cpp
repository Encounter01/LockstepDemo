/**
 * @file main.cpp
 * @brief 帧同步客户端入口程序 - 控制台版本，使用ASCII字符渲染
 * @author LockstepDemo
 *
 * ============================================================================
 *                              技术概述
 * ============================================================================
 *
 * 【程序职责】
 * 这是帧同步客户端的入口文件，实现了一个简单的控制台游戏：
 * 1. 连接服务器并等待匹配
 * 2. 采集键盘输入
 * 3. 驱动游戏逻辑更新
 * 4. ASCII字符渲染游戏画面
 *
 * 【使用方法】
 *
 *    # 默认连接localhost:9999
 *    ./client.exe
 *
 *    # 指定服务器地址和端口
 *    ./client.exe 192.168.1.100 9999
 *
 * 【控制方式】
 *
 *    ┌─────────────────────────┐
 *    │          W              │
 *    │         ↑              │
 *    │    A ← · → D         │
 *    │         ↓              │
 *    │          S              │
 *    │                         │
 *    │    Space = 攻击         │
 *    │    Q     = 退出         │
 *    └─────────────────────────┘
 *
 * 【渲染示意】
 *
 *    |--------------------------------------------------|
 *    |                                                   |
 *    |          [@]                  *                   |
 *    |                    #                              |
 *    |      *                                            |
 *    |                          $                        |
 *    |--------------------------------------------------|
 *    Frame: 150  |  Bullets: 3
 *    > P0 [@]: HP=100  Kills=2  Deaths=0  ALIVE
 *      P1 [#]: HP= 75  Kills=1  Deaths=1  ALIVE
 *      P2 [$]: HP=  0  Kills=0  Deaths=2  DEAD
 *
 * ============================================================================
 *                              面试要点
 * ============================================================================
 *
 * 【Q1: 为什么使用双缓冲渲染？】
 * A: 本Demo使用screenBuffer数组作为后台缓冲：
 *    1. 先在内存中构建完整画面
 *    2. 一次性输出到控制台
 *    3. 减少屏幕闪烁
 *
 *    原理类似于图形程序的双缓冲技术，
 *    避免用户看到"画到一半"的画面。
 *
 * 【Q2: 跨平台键盘输入如何处理？】
 * A: 使用条件编译：
 *    - Windows: _kbhit() + _getch()
 *    - Linux/Mac: termios设置非规范模式
 *
 *    两者都实现了非阻塞键盘检测，
 *    使游戏循环不会被输入阻塞。
 *
 * 【Q3: 渲染帧率和逻辑帧率为什么不同？】
 * A:
 *    - 渲染帧率：60 FPS（16ms间隔）
 *    - 逻辑帧率：15 FPS（服务器下发帧数据的频率）
 *
 *    渲染可以比逻辑快：
 *    1. 相同状态多次渲染，画面更流畅
 *    2. 可以添加插值动画（本Demo未实现）
 *    3. 及时响应用户输入
 *
 * 【Q4: 坐标系转换是如何工作的？】
 * A: 游戏坐标 -> 屏幕坐标：
 *    - SCALE_X = SCREEN_WIDTH / MAP_WIDTH
 *    - SCALE_Y = SCREEN_HEIGHT / MAP_HEIGHT
 *
 *    实际计算：
 *    screenX = gameX * SCALE_X
 *    screenY = gameY * SCALE_Y + 1 (偏移边框)
 *
 * ============================================================================
 *                              生产实践
 * ============================================================================
 *
 * 【真正的游戏渲染】
 * 生产环境通常使用：
 * 1. OpenGL/DirectX/Vulkan 图形API
 * 2. SDL/SFML 游戏开发库
 * 3. Unity/Unreal 游戏引擎
 * 4. Cocos2d-x 2D游戏引擎
 *
 * 【输入处理改进】
 * 1. 支持同时按多个键
 * 2. 按键缓冲处理
 * 3. 输入平滑/去抖动
 * 4. 手柄支持
 *
 * 【渲染优化】
 * 1. 增量渲染（只更新变化的部分）
 * 2. 视口裁剪（只渲染可见区域）
 * 3. 图形批处理（减少绘制调用）
 * 4. 异步渲染（渲染和逻辑并行）
 */
#include <iostream>
#include <thread>       // sleep_for
#include <chrono>       // 时间控制
#include <cstring>      // memset
#include "LockstepClient.h"

// ============================================================================
//                          跨平台控制台操作
// ============================================================================

/**
 * 【条件编译】
 * Windows和Linux/Mac的控制台API完全不同，需要分别实现：
 * - 清屏
 * - 光标定位
 * - 隐藏光标
 * - 非阻塞键盘输入
 *
 * 【面试考点】
 * Q: 为什么不用第三方库（如ncurses）？
 * A:
 *    1. 减少依赖，Demo更简洁
 *    2. 展示底层API使用方法
 *    3. 生产环境确实推荐用库
 */

#ifdef _WIN32
    // ========================================================================
    //                      Windows实现
    // ========================================================================
    #include <conio.h>      // _kbhit, _getch
    #include <windows.h>    // Console API

    /**
     * @brief 清除屏幕
     *
     * 【实现】调用system("cls")
     *
     * 【面试Q】为什么不用ANSI转义序列？
     * A: Windows传统命令提示符不完全支持ANSI
     *    Windows 10后支持，但需要手动启用
     */
    void clearScreen() {
        system("cls");
    }

    /**
     * @brief 设置光标位置
     * @param x 列（0-based）
     * @param y 行（0-based）
     *
     * 【Windows API】
     * SetConsoleCursorPosition()设置光标位置
     * COORD结构体存储坐标
     */
    void setCursorPos(int x, int y) {
        COORD pos = {static_cast<SHORT>(x), static_cast<SHORT>(y)};
        SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), pos);
    }

    /**
     * @brief 隐藏光标
     *
     * 【用途】游戏渲染时不显示闪烁的光标
     *
     * 【Windows API】
     * SetConsoleCursorInfo()设置光标信息
     * bVisible = FALSE 隐藏光标
     */
    void hideCursor() {
        CONSOLE_CURSOR_INFO info;
        info.dwSize = 1;
        info.bVisible = FALSE;
        SetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info);
    }

    /**
     * @brief 非阻塞检测按键
     * @return 按下的键码，0表示无按键
     *
     * 【实现】
     * - _kbhit(): 检查是否有按键
     * - _getch(): 获取按键（不回显）
     *
     * 【面试考点】
     * Q: _getch()和getchar()的区别？
     * A:
     *    _getch(): 不等待回车，不回显
     *    getchar(): 等待回车，回显到屏幕
     */
    int getKeyPressed() {
        if (_kbhit()) {
            return _getch();
        }
        return 0;
    }

#else
    // ========================================================================
    //                      Linux/Mac实现
    // ========================================================================
    #include <termios.h>    // 终端控制
    #include <unistd.h>     // STDIN_FILENO
    #include <fcntl.h>      // fcntl

    /**
     * @brief 清除屏幕
     *
     * 【ANSI转义序列】
     * \033[2J : 清除整个屏幕
     * \033[H  : 光标移到左上角(1,1)
     */
    void clearScreen() {
        std::cout << "\033[2J\033[H";
    }

    /**
     * @brief 设置光标位置
     * @param x 列（0-based）
     * @param y 行（0-based）
     *
     * 【ANSI转义序列】
     * \033[row;colH : 移动光标到指定位置
     * 注意ANSI是1-based，所以要+1
     */
    void setCursorPos(int x, int y) {
        std::cout << "\033[" << (y + 1) << ";" << (x + 1) << "H";
    }

    /**
     * @brief 隐藏光标
     *
     * 【ANSI转义序列】
     * \033[?25l : 隐藏光标
     * \033[?25h : 显示光标（程序退出前应恢复）
     */
    void hideCursor() {
        std::cout << "\033[?25l";
    }

    /**
     * @brief 非阻塞检测按键
     * @return 按下的键码，0表示无按键
     *
     * 【实现原理】
     * 1. 保存原始终端设置
     * 2. 设置非规范模式（无需回车确认）
     * 3. 关闭回显
     * 4. 设置非阻塞读取
     * 5. 尝试读取一个字符
     * 6. 恢复终端设置
     *
     * 【面试考点：termios】
     * - c_lflag: 本地模式标志
     * - ICANON: 规范模式（等待回车）
     * - ECHO: 回显输入
     *
     * 【面试考点：fcntl】
     * - O_NONBLOCK: 非阻塞标志
     * - 设置后read()/getchar()不会阻塞
     */
    int getKeyPressed() {
        struct termios oldt, newt;
        int ch;
        int oldf;

        // 获取当前终端设置
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;

        // 关闭规范模式和回显
        newt.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);

        // 设置非阻塞
        oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

        // 尝试读取
        ch = getchar();

        // 恢复终端设置
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        fcntl(STDIN_FILENO, F_SETFL, oldf);

        return (ch == EOF) ? 0 : ch;
    }
#endif

using namespace lockstep;

// ============================================================================
//                          渲染配置
// ============================================================================

/**
 * 屏幕尺寸（字符数）
 *
 * 【设计考量】
 * - 80字符宽度：传统终端标准宽度
 * - 30行高度：留有空间显示状态栏
 */
constexpr int SCREEN_WIDTH = 80;
constexpr int SCREEN_HEIGHT = 30;

/**
 * 坐标缩放比例
 *
 * 【坐标转换】
 * 游戏世界坐标 -> 屏幕字符坐标
 *
 * 例如：MAP_WIDTH=1000, SCREEN_WIDTH=80
 * SCALE_X = 80/1000 = 0.08
 * 游戏坐标500 -> 屏幕坐标 500*0.08 = 40
 */
constexpr float SCALE_X = static_cast<float>(SCREEN_WIDTH) / GameWorld::MAP_WIDTH;
constexpr float SCALE_Y = static_cast<float>(SCREEN_HEIGHT - 5) / GameWorld::MAP_HEIGHT;
// -5 是为状态栏预留空间

// ============================================================================
//                          渲染缓冲区
// ============================================================================

/**
 * 屏幕缓冲区
 *
 * 【双缓冲思想】
 * 1. 先在内存中绘制完整画面
 * 2. 一次性输出到屏幕
 * 3. 减少闪烁
 *
 * 【结构】
 * - 二维字符数组
 * - +1是为了存储'\0'结束符
 */
char screenBuffer[SCREEN_HEIGHT][SCREEN_WIDTH + 1];

// ============================================================================
//                          渲染字符定义
// ============================================================================

/**
 * 玩家字符
 *
 * 【设计】不同玩家用不同符号，便于区分
 * - @: Player 0
 * - #: Player 1
 * - $: Player 2
 * - %: Player 3
 */
const char PLAYER_CHARS[] = {'@', '#', '$', '%'};

/**
 * 子弹字符
 */
const char BULLET_CHAR = '*';

/**
 * 死亡玩家字符
 */
const char DEAD_CHAR = 'X';

// ============================================================================
//                          渲染函数
// ============================================================================

/**
 * @brief 初始化屏幕缓冲区
 *
 * 【操作】将所有字符设为空格，末尾添加'\0'
 */
void initScreen() {
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        memset(screenBuffer[y], ' ', SCREEN_WIDTH);
        screenBuffer[y][SCREEN_WIDTH] = '\0';
    }
}

/**
 * @brief 绘制边框
 *
 * 【边框样式】
 *    |-------------------------------------|
 *    |                                     |
 *    |           游戏区域                   |
 *    |                                     |
 *    |-------------------------------------|
 *    状态栏（边框外）
 */
void drawBorder() {
    // 顶部边框
    for (int x = 0; x < SCREEN_WIDTH; ++x) {
        screenBuffer[0][x] = '-';
    }
    // 底部边框（游戏区域下方）
    for (int x = 0; x < SCREEN_WIDTH; ++x) {
        screenBuffer[SCREEN_HEIGHT - 6][x] = '-';
    }
    // 左右边框
    for (int y = 0; y < SCREEN_HEIGHT - 5; ++y) {
        screenBuffer[y][0] = '|';
        screenBuffer[y][SCREEN_WIDTH - 1] = '|';
    }
}

/**
 * @brief 渲染游戏世界
 * @param world 游戏世界状态
 * @param myPlayerId 本客户端的玩家ID
 *
 * 【渲染顺序】
 * 1. 清空缓冲区
 * 2. 绘制边框
 * 3. 绘制子弹
 * 4. 绘制玩家
 * 5. 绘制状态栏
 * 6. 输出到屏幕
 *
 * 【面试Q】为什么先画子弹后画玩家？
 * A: 后绘制的会覆盖先绘制的
 *    玩家比子弹重要，应该显示在上层
 */
void renderWorld(const GameWorld& world, uint32_t myPlayerId) {
    // 清空并画边框
    initScreen();
    drawBorder();

    // ========== 渲染子弹 ==========
    for (const auto& bullet : world.bullets) {
        if (!bullet.alive) continue;

        // 坐标转换
        int sx = static_cast<int>(bullet.pos.x.toFloat() * SCALE_X);
        int sy = static_cast<int>(bullet.pos.y.toFloat() * SCALE_Y) + 1;  // +1跳过顶部边框

        // 边界检查
        if (sx >= 1 && sx < SCREEN_WIDTH - 1 && sy >= 1 && sy < SCREEN_HEIGHT - 6) {
            screenBuffer[sy][sx] = BULLET_CHAR;
        }
    }

    // ========== 渲染玩家 ==========
    for (const auto& player : world.players) {
        // 坐标转换
        int sx = static_cast<int>(player.pos.x.toFloat() * SCALE_X);
        int sy = static_cast<int>(player.pos.y.toFloat() * SCALE_Y) + 1;

        // 边界检查
        if (sx >= 1 && sx < SCREEN_WIDTH - 1 && sy >= 1 && sy < SCREEN_HEIGHT - 6) {
            if (player.alive) {
                // 根据玩家ID选择字符
                char ch = PLAYER_CHARS[player.ownerId % 4];
                screenBuffer[sy][sx] = ch;

                // 用方括号标记自己 [@]
                if (player.ownerId == myPlayerId) {
                    if (sx > 1) screenBuffer[sy][sx - 1] = '[';
                    if (sx < SCREEN_WIDTH - 2) screenBuffer[sy][sx + 1] = ']';
                }
            } else {
                // 死亡玩家显示X
                screenBuffer[sy][sx] = DEAD_CHAR;
            }
        }
    }

    // ========== 渲染状态栏 ==========
    int statusY = SCREEN_HEIGHT - 5;

    // 帧信息
    snprintf(screenBuffer[statusY], SCREEN_WIDTH,
             "Frame: %u  |  Bullets: %zu",
             world.currentFrame, world.bullets.size());

    // 玩家状态
    statusY++;
    for (size_t i = 0; i < world.players.size(); ++i) {
        const auto& p = world.players[i];
        char marker = (p.ownerId == myPlayerId) ? '>' : ' ';  // '>'标记自己
        snprintf(screenBuffer[statusY + i], SCREEN_WIDTH,
                 "%c P%u [%c]: HP=%3d  Kills=%u  Deaths=%u  %s",
                 marker, p.ownerId, PLAYER_CHARS[p.ownerId % 4],
                 p.hp, p.kills, p.deaths,
                 p.alive ? "ALIVE" : "DEAD");
    }

    // ========== 输出到屏幕 ==========
    setCursorPos(0, 0);  // 光标回到左上角（覆盖式渲染）
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        std::cout << screenBuffer[y] << "\n";
    }
    std::cout.flush();  // 立即刷新输出
}

/**
 * @brief 显示等待画面
 * @param playerId 本客户端的玩家ID
 *
 * 【显示时机】加入房间后，等待其他玩家
 */
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

// ============================================================================
//                          主函数
// ============================================================================

/**
 * @brief 程序入口
 * @param argc 参数个数
 * @param argv 参数数组
 * @return 0=正常退出, 1=连接失败
 *
 * 【命令行参数】
 * - argv[0]: 程序名
 * - argv[1]: 服务器地址（可选，默认127.0.0.1）
 * - argv[2]: 服务器端口（可选，默认9999）
 *
 * 【程序流程】
 *
 *    main()
 *      │
 *      ├── 解析命令行参数
 *      │
 *      ├── 创建客户端并连接
 *      │         │
 *      │         v
 *      ├── 等待连接确认 ──超时──> 退出
 *      │         │
 *      │         v (成功)
 *      ├── 隐藏光标
 *      │
 *      └── 主循环 ──────────────────────────┐
 *            │                               │
 *            ├── getKeyPressed() ──> 处理输入 │
 *            │                               │
 *            ├── 状态判断:                    │
 *            │   ├─ PLAYING: update + render │
 *            │   ├─ WAITING: 等待画面         │
 *            │   └─ GAME_OVER: 结束画面      │
 *            │                               │
 *            └── sleep(16ms) ────────────────┘
 *                     │
 *                     v (Q键或断线)
 *              disconnect()
 *              清屏退出
 */
int main(int argc, char* argv[]) {
    // ========================================================================
    // 步骤1: 解析命令行参数
    // ========================================================================

    std::string host = "127.0.0.1";  // 默认本地
    uint16_t port = 9999;            // 默认端口

    if (argc > 1) {
        host = argv[1];
    }
    if (argc > 2) {
        port = static_cast<uint16_t>(std::atoi(argv[2]));
    }

    // 显示启动信息
    std::cout << "========================================\n";
    std::cout << "     Lockstep Demo Client v1.0\n";
    std::cout << "========================================\n\n";
    std::cout << "Connecting to " << host << ":" << port << "...\n";

    // ========================================================================
    // 步骤2: 创建客户端并连接
    // ========================================================================

    LockstepClient client;

    if (!client.connect(host, port)) {
        std::cerr << "Failed to connect!\n";
        return 1;
    }

    // ========================================================================
    // 步骤3: 等待连接确认（超时5秒）
    // ========================================================================

    /**
     * 【等待逻辑】
     * 1. 循环检查状态
     * 2. 超过5秒仍在CONNECTING则超时
     * 3. 变为WAITING或PLAYING表示成功
     *
     * 【面试Q】为什么要有超时？
     * A:
     *    1. 服务器可能不存在或不可达
     *    2. 网络问题导致响应丢失
     *    3. 避免无限等待影响用户体验
     */
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

    // 检查连接结果
    if (client.getState() == LockstepClient::State::DISCONNECTED) {
        std::cerr << "Connection failed!\n";
        return 1;
    }

    // ========================================================================
    // 步骤4: 准备游戏循环
    // ========================================================================

    hideCursor();  // 隐藏光标，避免闪烁

    bool lastWaiting = false;  // 用于只显示一次等待画面
    bool running = true;        // 主循环标志

    // ========================================================================
    // 步骤5: 主循环
    // ========================================================================

    /**
     * 【循环结构】
     *
     * while (running && 已连接) {
     *     1. 处理输入
     *     2. 根据状态更新/渲染
     *     3. 控制帧率（16ms ≈ 60 FPS）
     * }
     *
     * 【帧率控制】
     * - 渲染60FPS：画面流畅
     * - 逻辑15FPS：由服务器帧数据驱动
     * - 输入60FPS：响应及时
     */
    while (running && client.isConnected()) {

        // ============ 处理输入 ============
        int key = getKeyPressed();
        if (key) {
            switch (key) {
                // 退出
                case 'q':
                case 'Q':
                    running = false;
                    break;

                // 移动（8方向编码）
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

                // 攻击
                case ' ':
                    client.setAction(0x01, true);
                    break;

                // 其他键：停止移动
                default:
                    client.setMoveDirection(8);  // 静止
                    client.setAction(0x01, false);
                    break;
            }
        } else {
            // 没有按键时停止移动和攻击
            // 【设计】按住才移动/攻击，松开就停止
            client.setMoveDirection(8);
            client.setAction(0x01, false);
        }

        // ============ 状态处理 ============

        if (client.isPlaying()) {
            // ----- 游戏进行中 -----
            client.update();  // 处理帧数据
            renderWorld(client.getWorld(), client.getPlayerId());

        } else if (client.getState() == LockstepClient::State::WAITING) {
            // ----- 等待其他玩家 -----
            if (!lastWaiting) {
                showWaitingScreen(client.getPlayerId());
                lastWaiting = true;  // 只显示一次
            }

        } else if (client.getState() == LockstepClient::State::GAME_OVER) {
            // ----- 游戏结束 -----
            // 最后渲染一次当前状态
            renderWorld(client.getWorld(), client.getPlayerId());

            // 显示游戏结束提示
            setCursorPos(SCREEN_WIDTH / 2 - 10, SCREEN_HEIGHT / 2);
            std::cout << "=== GAME OVER ===" << std::endl;

            // 显示获胜者
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

        // ============ 帧率控制 ============
        // 16ms ≈ 60 FPS 渲染帧率
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // ========================================================================
    // 步骤6: 清理退出
    // ========================================================================

    client.disconnect();

    clearScreen();
    std::cout << "Thanks for playing!\n";

    return 0;
}

// ============================================================================
//                              扩展阅读
// ============================================================================
/**
 * 【控制台游戏渲染技术】
 *
 * 1. 直接输出（本Demo）：
 *    优点：简单
 *    缺点：闪烁
 *
 * 2. 双缓冲（本Demo使用）：
 *    内存中构建 -> 一次输出
 *    减少闪烁
 *
 * 3. 增量更新：
 *    只更新变化的字符
 *    效率更高
 *
 * 4. 使用ncurses库：
 *    专业的终端UI库
 *    支持颜色、窗口、鼠标等
 *
 * 【从控制台到图形界面】
 *
 * 学习路径建议：
 * 1. 控制台 + ASCII（本Demo）
 * 2. SDL2 + 2D精灵
 * 3. OpenGL + 3D渲染
 * 4. 游戏引擎（Unity/Unreal）
 *
 * 每一步都是在相同的游戏逻辑基础上
 * 更换渲染层，帧同步核心代码无需改变。
 */
