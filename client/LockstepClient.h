/**
 * @file LockstepClient.h
 * @brief 帧同步客户端 - 负责连接服务器、发送输入、接收帧数据、执行游戏逻辑
 * @author LockstepDemo
 *
 * ============================================================================
 *                              技术概述
 * ============================================================================
 *
 * 【核心职责】
 * 帧同步客户端是玩家与游戏世界交互的桥梁：
 * 1. 连接服务器并完成房间匹配
 * 2. 采集玩家输入并发送给服务器
 * 3. 接收服务器广播的帧数据
 * 4. 执行确定性的游戏逻辑
 * 5. 渲染当前游戏状态
 *
 * 【双线程架构】
 *
 *    ┌─────────────────────────────────────────────────────────────┐
 *    │                     LockstepClient                          │
 *    │                                                             │
 *    │   ┌─────────────────────┐     ┌─────────────────────┐      │
 *    │   │    主线程 (Main)     │     │  网络线程 (Network)  │      │
 *    │   │                     │     │                     │      │
 *    │   │  ┌───────────────┐  │     │  ┌───────────────┐  │      │
 *    │   │  │ 输入采集      │  │     │  │ recvFrom()    │  │      │
 *    │   │  │ (keyboard)    │  │     │  │ 消息处理      │  │      │
 *    │   │  └───────┬───────┘  │     │  └───────┬───────┘  │      │
 *    │   │          │ atomic   │     │          │          │      │
 *    │   │          v          │     │          │ mutex    │      │
 *    │   │  ┌───────────────┐  │     │          v          │      │
 *    │   │  │ update()      │  │     │  ┌───────────────┐  │      │
 *    │   │  │ - sendInput() │  │     │  │ pendingFrames_│  │      │
 *    │   │  │ - processFrame│<─┼──── │  │   (queue)     │  │      │
 *    │   │  │ - render()    │  │     │  └───────────────┘  │      │
 *    │   │  └───────────────┘  │     │                     │      │
 *    │   └─────────────────────┘     └─────────────────────┘      │
 *    └─────────────────────────────────────────────────────────────┘
 *
 * 【客户端状态机】
 *
 *    DISCONNECTED ──connect()──> CONNECTING
 *          │                          │
 *          │                          │ JOIN_ACK
 *          │                          v
 *          │                      WAITING ──START──> PLAYING
 *          │                                            │
 *          │                                            │ 游戏结束
 *          │                                            v
 *          │<─────────disconnect()───────────────── GAME_OVER
 *
 * ============================================================================
 *                              面试要点
 * ============================================================================
 *
 * 【Q1: 为什么使用双线程架构？】
 * A: 分离网络IO和游戏逻辑：
 *    1. 网络线程：持续接收消息，不会被游戏逻辑阻塞
 *    2. 主线程：处理输入、执行逻辑、渲染，不受网络延迟影响
 *    3. 通过帧队列解耦，实现生产者-消费者模式
 *
 *    替代方案：
 *    - 单线程 + 非阻塞IO：适合简单场景
 *    - 协程：如C++20 coroutines或libco
 *    - Reactor模式：如libevent/libuv
 *
 * 【Q2: 为什么输入用atomic，帧队列用mutex？】
 * A:
 *    输入(atomic)：
 *    - 简单的读写操作
 *    - 主线程写，网络线程读
 *    - 不需要保护多个变量的一致性
 *
 *    帧队列(mutex)：
 *    - 需要保护队列的push/pop操作
 *    - 多个操作需要原子执行
 *    - mutex + lock_guard更清晰
 *
 * 【Q3: 如何检测不同步(Desync)？】
 * A: 通过校验和比对：
 *    1. 服务器可选发送当前帧的状态校验和
 *    2. 客户端本地计算校验和
 *    3. 比对不一致说明状态分歧
 *
 *    处理方式：
 *    - 轻微：记录日志继续
 *    - 严重：请求状态同步或断线重连
 *    - 调试：保存回放用于分析
 *
 * 【Q4: 帧队列积压怎么办？】
 * A: 帧队列积压意味着客户端处理不及时：
 *    1. 原因：帧处理慢、网络突然畅通带来大量数据
 *    2. 快速回放：跳过渲染，快速执行多帧逻辑
 *    3. 丢帧：对于实时性要求高的场景可以丢弃旧帧
 *    4. 限流：设置队列上限
 *
 * 【Q5: 断线重连如何实现？】
 * A: 客户端发送RECONNECT消息，附带：
 *    - playerId：之前分配的玩家ID
 *    - lastFrame：最后处理的帧号
 *    服务器返回缺失的帧数据，客户端快速回放追赶
 *
 * ============================================================================
 *                              生产实践
 * ============================================================================
 *
 * 【网络优化】
 * 1. 输入压缩：多帧输入打包发送
 * 2. 输入确认：防止重要输入丢失
 * 3. 自适应帧率：根据网络状况调整
 * 4. RTT测量：用于延迟补偿
 *
 * 【用户体验】
 * 1. 加载界面：显示"等待其他玩家"
 * 2. 网络状态指示：显示延迟和丢包率
 * 3. 重连提示：自动重连进度
 * 4. 卡顿预警：检测帧延迟过大
 *
 * 【调试工具】
 * 1. 回放系统：记录所有输入用于复现问题
 * 2. 网络模拟：人为添加延迟和丢包
 * 3. 状态快照：定期保存完整状态
 * 4. 校验日志：记录每帧校验和
 */
#pragma once

// ============================================================================
//                              标准库头文件
// ============================================================================
#include <iostream>
#include <queue>        // 【容器】帧数据队列
#include <mutex>        // 【同步】保护帧队列
#include <thread>       // 【并发】网络线程
#include <atomic>       // 【原子】线程安全的输入状态
#include <chrono>       // 【时间】心跳定时

// ============================================================================
//                              项目头文件
// ============================================================================
#include "../common/Network.h"   // 跨平台UDP封装
#include "../common/Protocol.h"  // 消息协议定义
#include "../common/GameWorld.h" // 游戏世界

namespace lockstep {

/**
 * @class LockstepClient
 * @brief 帧同步客户端核心类
 *
 * 【设计特点】
 * 1. 双线程：主线程处理逻辑，网络线程收发消息
 * 2. 状态机：清晰的连接状态管理
 * 3. 解耦：通过队列解耦网络和游戏逻辑
 *
 * 【使用流程】
 * 1. connect(host, port)  - 连接服务器
 * 2. 等待状态变为PLAYING
 * 3. 循环调用update()    - 处理游戏逻辑
 * 4. setMoveDirection()  - 设置玩家输入
 * 5. disconnect()        - 断开连接
 */
class LockstepClient {
public:
    // ========================================================================
    //                          客户端状态枚举
    // ========================================================================

    /**
     * @enum State
     * @brief 客户端状态
     *
     * 【状态转换】
     *
     *    ┌───────────────────────────────────────────────────┐
     *    │                  状态转换图                        │
     *    ├───────────────────────────────────────────────────┤
     *    │  DISCONNECTED ──connect()───> CONNECTING          │
     *    │       ^                            │               │
     *    │       │                            │ recv JOIN_ACK │
     *    │       │                            v               │
     *    │       │                        WAITING             │
     *    │       │                            │               │
     *    │       │                            │ recv START    │
     *    │       │                            v               │
     *    │       │                        PLAYING ───────┐    │
     *    │       │                                       │    │
     *    │       │ disconnect()                          │    │
     *    │       └────────────────────── GAME_OVER <─────┘    │
     *    └───────────────────────────────────────────────────┘
     *
     * 【面试考点】
     * Q: 为什么用enum class而不是普通enum？
     * A:
     *    1. 强类型：不能隐式转换为int
     *    2. 作用域：需要State::PLAYING访问
     *    3. 避免命名冲突
     *    4. C++11最佳实践
     */
    enum class State {
        DISCONNECTED,  // 未连接
        CONNECTING,    // 正在连接（已发送JOIN，等待ACK）
        WAITING,       // 已加入房间，等待其他玩家
        PLAYING,       // 游戏进行中
        GAME_OVER      // 游戏结束
    };

private:
    // ========================================================================
    //                          网络相关成员
    // ========================================================================

    /**
     * UDP套接字
     *
     * 【客户端特点】
     * - 不需要bind，系统自动分配端口
     * - 非阻塞模式，配合轮询使用
     */
    UdpSocket socket_;

    /**
     * 服务器地址
     *
     * 【初始化】connect()时通过UdpSocket::makeAddr()创建
     */
    sockaddr_in serverAddr_{};

    /**
     * 服务器主机名和端口
     *
     * 【用途】保存用于断线重连
     */
    std::string serverHost_;
    uint16_t serverPort_;

    // ========================================================================
    //                          游戏状态
    // ========================================================================

    /**
     * 游戏世界
     *
     * 【初始化时机】收到START消息后调用init()
     *
     * 【确定性】使用与服务器相同的种子初始化
     */
    GameWorld world_;

    /**
     * 本客户端的玩家ID
     *
     * 【分配】服务器在JOIN_ACK中返回
     *
     * 【用途】
     * - 发送输入时标识自己
     * - 渲染时高亮自己的角色
     */
    uint32_t playerId_ = 0;

    /**
     * 当前状态
     *
     * 【读写】
     * - 主线程和网络线程都会读写
     * - 使用普通枚举是因为状态变化是顺序的，竞争风险低
     * - 生产环境建议用atomic<State>
     */
    State state_ = State::DISCONNECTED;

    // ========================================================================
    //                          帧数据队列
    // ========================================================================

    /**
     * 待处理帧队列
     *
     * 【生产者-消费者模式】
     *
     *    网络线程 ──push()──> [queue] ──pop()──> 主线程
     *
     * 【线程安全】
     * - 使用mutex保护
     * - 网络线程push，主线程pop
     *
     * 【面试Q】为什么用queue而不是vector？
     * A: FIFO语义：
     *    1. 帧必须按顺序处理
     *    2. queue天然支持先进先出
     *    3. 队列操作O(1)
     */
    std::queue<FrameData> pendingFrames_;

    /**
     * 帧队列互斥锁
     *
     * 【保护范围】pendingFrames_的所有操作
     */
    std::mutex frameMutex_;

    // ========================================================================
    //                          输入状态（原子变量）
    // ========================================================================

    /**
     * 移动方向
     *
     * 【编码】0-7表示8个方向，8表示静止
     *
     *       7  0  1
     *        \ | /
     *      6 ──*── 2
     *        / | \
     *       5  4  3
     *
     * 【线程安全】atomic保证读写原子性
     *
     * 【面试Q】为什么输入用atomic而不是mutex？
     * A:
     *    1. 简单的单值读写
     *    2. atomic开销更小（无锁）
     *    3. 不需要保护多个变量的一致性
     */
    std::atomic<uint8_t> moveDir_{8};  // 默认静止

    /**
     * 动作位域
     *
     * 【位定义】（示例）
     * - bit 0: 射击
     * - bit 1: 跳跃
     * - bit 2: 技能1
     * - ...
     */
    std::atomic<uint8_t> actions_{0};

    /**
     * 目标坐标（用于点击移动或瞄准）
     */
    std::atomic<int32_t> targetX_{0};
    std::atomic<int32_t> targetY_{0};

    // ========================================================================
    //                          网络线程
    // ========================================================================

    /**
     * 网络线程
     *
     * 【职责】
     * 1. 持续接收UDP消息
     * 2. 解析并分发消息
     * 3. 发送心跳包
     */
    std::thread networkThread_;

    /**
     * 运行标志
     *
     * 【生命周期】
     * - connect()时设为true
     * - disconnect()时设为false
     * - 网络线程检测到false后退出
     */
    std::atomic<bool> running_{false};

    // ========================================================================
    //                          接收缓冲区
    // ========================================================================

    /**
     * 接收缓冲区大小
     *
     * 【计算依据】
     * - 需要能容纳SYNC消息（多个帧数据）
     * - 1000帧 × 76字节 ≈ 76KB
     * - 64KB是安全的选择
     *
     * 【注意】UDP单包理论最大65535字节
     */
    static constexpr size_t RECV_BUFFER_SIZE = 65536;

    /**
     * 接收缓冲区
     *
     * 【线程归属】只在网络线程使用，无需同步
     */
    uint8_t recvBuffer_[RECV_BUFFER_SIZE];

    // ========================================================================
    //                          心跳机制
    // ========================================================================

    /**
     * 上次心跳时间
     *
     * 【更新时机】每次发送心跳后更新
     */
    std::chrono::steady_clock::time_point lastHeartbeat_;

    /**
     * 心跳间隔（毫秒）
     *
     * 【设计考量】
     * - 1秒：足够保持NAT映射
     * - 不能太频繁：增加网络开销
     * - 不能太慢：可能被NAT丢弃
     *
     * 【NAT穿透】
     * NAT设备会维护地址映射表，超时会被清理
     * 心跳包保持映射活跃，使服务器消息能送达
     */
    static constexpr int HEARTBEAT_INTERVAL_MS = 1000;

public:
    // ========================================================================
    //                          构造与析构
    // ========================================================================

    /**
     * @brief 默认构造函数
     *
     * 【初始化列表】使用成员默认值
     */
    LockstepClient() = default;

    /**
     * @brief 析构函数
     *
     * 【RAII】确保网络线程正确退出，资源正确释放
     */
    ~LockstepClient() {
        disconnect();
    }

    // ========================================================================
    //                          连接管理
    // ========================================================================

    /**
     * @brief 连接到服务器
     * @param host 服务器地址（IP或域名）
     * @param port 服务器端口
     * @return true=连接请求已发送
     *
     * 【执行步骤】
     * 1. 初始化网络库
     * 2. 创建UDP套接字
     * 3. 设置非阻塞模式
     * 4. 解析服务器地址
     * 5. 启动网络线程
     * 6. 发送JOIN请求
     *
     * 【注意】
     * - 返回true不代表连接成功
     * - 需要等待状态变为WAITING或PLAYING
     *
     * 【面试Q】UDP没有连接概念，为什么叫connect？
     * A: 这里的"connect"是逻辑概念：
     *    1. 准备好通信的socket
     *    2. 记住服务器地址
     *    3. 发送加入请求
     *    实际通信仍然是无连接的
     */
    bool connect(const std::string& host, uint16_t port) {
        // 保存服务器信息（用于重连）
        serverHost_ = host;
        serverPort_ = port;

        // 初始化网络库（Windows需要）
        init_network();

        // 创建UDP套接字
        if (!socket_.create()) {
            std::cerr << "[Client] Failed to create socket" << std::endl;
            return false;
        }

        // 设置非阻塞模式
        socket_.setNonBlocking();

        // 解析服务器地址
        serverAddr_ = UdpSocket::makeAddr(host, port);

        // 设置状态
        state_ = State::CONNECTING;
        running_ = true;

        // 启动网络线程
        // 【C++11】使用成员函数作为线程入口
        networkThread_ = std::thread(&LockstepClient::networkLoop, this);

        // 发送加入请求
        std::vector<uint8_t> msg;
        writeU8(msg, static_cast<uint8_t>(MsgType::JOIN));
        socket_.sendTo(msg.data(), msg.size(), serverAddr_);

        std::cout << "[Client] Connecting to " << host << ":" << port << std::endl;

        return true;
    }

    /**
     * @brief 断开连接
     *
     * 【执行步骤】
     * 1. 设置running_=false，通知网络线程退出
     * 2. 等待网络线程结束（join）
     * 3. 关闭套接字
     * 4. 清理网络库
     * 5. 重置状态
     *
     * 【线程安全】
     * - running_是atomic，设置后网络线程会看到
     * - join()阻塞直到线程真正退出
     *
     * 【面试Q】为什么要join而不是detach？
     * A:
     *    1. join确保线程完全退出后再继续
     *    2. 防止悬空引用（线程可能访问已析构的对象）
     *    3. 资源管理更清晰
     *    detach适合"发射后不管"的场景
     */
    void disconnect() {
        running_ = false;

        // 等待网络线程退出
        if (networkThread_.joinable()) {
            networkThread_.join();
        }

        socket_.close();
        cleanup_network();
        state_ = State::DISCONNECTED;
    }

    // ========================================================================
    //                          输入设置
    // ========================================================================

    /**
     * @brief 设置移动方向
     * @param dir 方向编码（0-7为8个方向，8为静止）
     *
     * 【线程安全】atomic store保证原子写入
     *
     * 【使用示例】
     *
     *    // 根据键盘状态计算方向
     *    int dx = (right ? 1 : 0) - (left ? 1 : 0);
     *    int dy = (down ? 1 : 0) - (up ? 1 : 0);
     *
     *    static const uint8_t DIR_MAP[3][3] = {
     *        {7, 0, 1},  // dx=-1
     *        {6, 8, 2},  // dx=0
     *        {5, 4, 3}   // dx=1
     *    };
     *    client.setMoveDirection(DIR_MAP[dx+1][dy+1]);
     */
    void setMoveDirection(uint8_t dir) {
        moveDir_ = dir;
    }

    /**
     * @brief 设置动作状态
     * @param actionBit 动作位掩码
     * @param pressed true=按下，false=释放
     *
     * 【位操作】
     * - 按下：OR操作设置位
     * - 释放：AND NOT操作清除位
     *
     * 【使用示例】
     *
     *    const uint8_t ACTION_SHOOT = 0x01;
     *    const uint8_t ACTION_JUMP  = 0x02;
     *
     *    // 按下射击键
     *    client.setAction(ACTION_SHOOT, true);
     *
     *    // 释放射击键
     *    client.setAction(ACTION_SHOOT, false);
     */
    void setAction(uint8_t actionBit, bool pressed) {
        if (pressed) {
            actions_ = actions_ | actionBit;   // 设置位
        } else {
            actions_ = actions_ & ~actionBit;  // 清除位
        }
    }

    /**
     * @brief 设置目标坐标
     * @param x 目标X坐标
     * @param y 目标Y坐标
     *
     * 【用途】
     * - 点击移动：鼠标点击位置
     * - 技能瞄准：技能释放目标
     */
    void setTarget(int32_t x, int32_t y) {
        targetX_ = x;
        targetY_ = y;
    }

    // ========================================================================
    //                          游戏更新
    // ========================================================================

    /**
     * @brief 游戏主循环更新
     * @return true=处理了一帧，false=没有帧可处理
     *
     * 【调用时机】游戏主循环中持续调用
     *
     * 【执行步骤】
     * 1. 检查状态是否为PLAYING
     * 2. 发送当前输入
     * 3. 从队列取出帧数据
     * 4. 执行游戏逻辑(tick)
     * 5. 验证校验和（可选）
     * 6. 检查游戏结束
     *
     * 【帧处理流程】
     *
     *    update()
     *       │
     *       ├── sendInput() ───────────> 服务器
     *       │
     *       ├── 从队列取帧 <───────────── 网络线程push
     *       │
     *       ├── world_.tick(frame)
     *       │
     *       ├── 校验checksum
     *       │
     *       └── 检查游戏结束
     *
     * 【面试Q】为什么先发送输入再处理帧？
     * A:
     *    1. 输入应该尽早发送，减少延迟
     *    2. 当前帧的输入用于下一帧
     *    3. 发送和处理可以并行（网络线程发送）
     */
    bool update() {
        // 只在游戏进行中处理
        if (state_ != State::PLAYING) {
            return false;
        }

        // 发送当前输入到服务器
        sendInput();

        // 尝试从队列获取帧数据
        FrameData frame;
        bool hasFrame = false;

        {
            std::lock_guard<std::mutex> lock(frameMutex_);
            if (!pendingFrames_.empty()) {
                frame = pendingFrames_.front();
                pendingFrames_.pop();
                hasFrame = true;
            }
        }

        if (hasFrame) {
            // 执行游戏逻辑
            world_.tick(frame);

            // 【可选】校验和验证
            // 服务器如果发送了checksum（非0），进行本地验证
            if (frame.checksum != 0) {
                uint32_t localChecksum = world_.calcChecksum();
                if (localChecksum != frame.checksum) {
                    // 【不同步检测】
                    // 本地状态与服务器预期不一致
                    std::cerr << "[Client] DESYNC at frame " << frame.frameId
                              << "! Local=" << localChecksum
                              << " Server=" << frame.checksum << std::endl;

                    // 【生产处理】
                    // - 记录详细日志用于调试
                    // - 尝试重连获取正确状态
                    // - 严重时断开连接
                }
            }

            // 检查游戏结束
            if (world_.isGameOver()) {
                state_ = State::GAME_OVER;
                int winnerId = world_.getWinnerId();
                if (winnerId == static_cast<int>(playerId_)) {
                    std::cout << "[Client] YOU WIN!" << std::endl;
                } else {
                    std::cout << "[Client] GAME OVER - Winner: Player " << winnerId << std::endl;
                }
            }

            return true;  // 处理了一帧
        }

        return false;  // 没有帧可处理
    }

    // ========================================================================
    //                          状态查询
    // ========================================================================

    /**
     * 获取当前状态
     */
    State getState() const { return state_; }

    /**
     * 获取玩家ID
     */
    uint32_t getPlayerId() const { return playerId_; }

    /**
     * 获取游戏世界（只读）
     *
     * 【用途】渲染时读取实体位置等信息
     */
    const GameWorld& getWorld() const { return world_; }

    /**
     * 获取当前帧号
     */
    uint32_t getCurrentFrame() const { return world_.currentFrame; }

    /**
     * 是否正在游戏
     */
    bool isPlaying() const { return state_ == State::PLAYING; }

    /**
     * 是否已连接（包括等待中和游戏中）
     */
    bool isConnected() const { return state_ != State::DISCONNECTED; }

private:
    // ========================================================================
    //                          网络线程
    // ========================================================================

    /**
     * @brief 网络线程入口
     *
     * 【职责】
     * 1. 持续接收UDP消息
     * 2. 分发消息到对应处理函数
     * 3. 定时发送心跳
     *
     * 【线程生命周期】
     *
     *    networkThread_ = std::thread(&networkLoop, this);
     *           │
     *           v
     *    ┌─────────────────────────────────────────┐
     *    │            networkLoop()                │
     *    │                                         │
     *    │  while (running_) {                     │
     *    │      recvFrom() ──> handleMessage()     │
     *    │      checkHeartbeat() ──> sendHeartbeat │
     *    │      sleep(1ms)                         │
     *    │  }                                      │
     *    └─────────────────────────────────────────┘
     *           │
     *           v  [running_ = false]
     *      线程退出
     *
     * 【面试Q】为什么网络线程不直接执行游戏逻辑？
     * A:
     *    1. 游戏逻辑需要与渲染同步
     *    2. 分离关注点，网络只负责收发
     *    3. 方便控制帧率和游戏节奏
     *    4. 避免网络波动影响游戏体验
     */
    void networkLoop() {
        lastHeartbeat_ = std::chrono::steady_clock::now();

        while (running_) {
            // 接收消息（非阻塞）
            sockaddr_in fromAddr{};
            int len = socket_.recvFrom(recvBuffer_, RECV_BUFFER_SIZE, fromAddr);

            // 处理所有待处理消息
            while (len > 0) {
                handleMessage(recvBuffer_, len);
                len = socket_.recvFrom(recvBuffer_, RECV_BUFFER_SIZE, fromAddr);
            }

            // 心跳检查
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lastHeartbeat_
            ).count();

            if (elapsed >= HEARTBEAT_INTERVAL_MS) {
                sendHeartbeat();
                lastHeartbeat_ = now;
            }

            // 避免CPU满载
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    /**
     * @brief 消息分发
     * @param data 消息数据
     * @param len 数据长度
     *
     * 【消息类型】
     * - JOIN_ACK：服务器确认加入
     * - START：游戏开始
     * - FRAME：帧数据
     * - SYNC：重连同步数据
     * - GAME_OVER：游戏结束
     */
    void handleMessage(const uint8_t* data, size_t len) {
        if (len < 1) return;

        MsgType type = static_cast<MsgType>(data[0]);

        switch (type) {
            case MsgType::JOIN_ACK:
                handleJoinAck(data + 1, len - 1);
                break;
            case MsgType::START:
                handleStart(data + 1, len - 1);
                break;
            case MsgType::FRAME:
                handleFrame(data + 1, len - 1);
                break;
            case MsgType::SYNC:
                handleSync(data + 1, len - 1);
                break;
            case MsgType::GAME_OVER:
                handleGameOver(data + 1, len - 1);
                break;
            default:
                // 忽略未知消息
                break;
        }
    }

    // ========================================================================
    //                          消息处理
    // ========================================================================

    /**
     * @brief 处理加入确认
     *
     * 【消息内容】
     * - playerId：分配的玩家ID
     * - playerCount：房间当前人数
     *
     * 【状态转换】CONNECTING -> WAITING
     */
    void handleJoinAck(const uint8_t* data, size_t len) {
        if (len < 8) return;

        JoinAckMsg ack;
        size_t offset = 0;
        ack.deserialize(data, offset);

        playerId_ = ack.playerId;
        state_ = State::WAITING;

        std::cout << "[Client] Joined as Player " << playerId_
                  << " (" << ack.playerCount << " players in room)" << std::endl;
    }

    /**
     * @brief 处理游戏开始
     *
     * 【消息内容】
     * - playerCount：参与人数
     * - randomSeed：随机种子
     *
     * 【执行操作】
     * 1. 初始化游戏世界
     * 2. 状态转换：WAITING -> PLAYING
     *
     * 【确定性保证】
     * 使用服务器下发的种子初始化随机数生成器
     */
    void handleStart(const uint8_t* data, size_t len) {
        if (len < 8) return;

        StartMsg start;
        size_t offset = 0;
        start.deserialize(data, offset);

        // 使用服务器种子初始化游戏世界
        world_.init(start.playerCount, start.randomSeed);

        state_ = State::PLAYING;

        std::cout << "[Client] Game started! Players: " << start.playerCount
                  << ", Seed: " << start.randomSeed << std::endl;
        std::cout << "[Client] You are Player " << playerId_ << std::endl;
    }

    /**
     * @brief 处理帧数据
     *
     * 【处理方式】
     * 反序列化后放入帧队列
     * 主线程的update()会取出并处理
     *
     * 【线程安全】使用mutex保护队列操作
     */
    void handleFrame(const uint8_t* data, size_t len) {
        FrameData frame;
        size_t offset = 0;
        frame.deserialize(data, offset, len);

        std::lock_guard<std::mutex> lock(frameMutex_);
        pendingFrames_.push(frame);
    }

    /**
     * @brief 处理同步数据（重连时使用）
     *
     * 【消息格式】
     * - frameCount (2字节)
     * - FrameData[] (变长)
     *
     * 【处理方式】
     * 将所有帧按顺序放入队列
     * 主线程会快速执行追赶
     */
    void handleSync(const uint8_t* data, size_t len) {
        if (len < 2) return;

        size_t offset = 0;
        uint16_t frameCount = readU16(data, offset);

        std::cout << "[Client] Receiving " << frameCount << " sync frames" << std::endl;

        std::lock_guard<std::mutex> lock(frameMutex_);

        for (uint16_t i = 0; i < frameCount && offset < len; ++i) {
            FrameData frame;
            frame.deserialize(data, offset, len);
            pendingFrames_.push(frame);
        }
    }

    /**
     * @brief 处理游戏结束
     *
     * 【状态转换】-> GAME_OVER
     */
    void handleGameOver(const uint8_t* data, size_t len) {
        state_ = State::GAME_OVER;
        std::cout << "[Client] Game Over!" << std::endl;
    }

    // ========================================================================
    //                          消息发送
    // ========================================================================

    /**
     * @brief 发送玩家输入
     *
     * 【发送时机】每次update()调用时
     *
     * 【输入内容】
     * - playerId：自己的ID
     * - frameId：当前帧号
     * - moveDir：移动方向
     * - actions：动作位域
     * - targetX/Y：目标坐标
     *
     * 【面试Q】输入丢失怎么办？
     * A: UDP不保证送达，但：
     *    1. 15FPS，每帧都发送，丢一个影响不大
     *    2. 服务器会用空输入替代
     *    3. 可以添加输入确认机制（增加复杂度）
     */
    void sendInput() {
        PlayerInput input;
        input.playerId = playerId_;
        input.frameId = world_.currentFrame;
        input.moveDir = moveDir_.load();
        input.actions = actions_.load();
        input.targetX = targetX_.load();
        input.targetY = targetY_.load();

        std::vector<uint8_t> msg;
        writeU8(msg, static_cast<uint8_t>(MsgType::INPUT));
        input.serialize(msg);

        socket_.sendTo(msg.data(), msg.size(), serverAddr_);
    }

    /**
     * @brief 发送心跳包
     *
     * 【用途】
     * 1. 保持NAT映射活跃
     * 2. 告知服务器客户端仍然在线
     *
     * 【消息内容】
     * - 消息类型
     * - playerId
     *
     * 【面试Q】为什么心跳要带playerId？
     * A:
     *    1. 服务器可以通过playerId快速定位会话
     *    2. 避免遍历所有会话查找地址
     *    3. 客户端地址可能因NAT变化
     */
    void sendHeartbeat() {
        std::vector<uint8_t> msg;
        writeU8(msg, static_cast<uint8_t>(MsgType::HEARTBEAT));
        writeU32(msg, playerId_);
        socket_.sendTo(msg.data(), msg.size(), serverAddr_);
    }
};

} // namespace lockstep

// ============================================================================
//                              扩展阅读
// ============================================================================
/**
 * 【客户端网络模型演进】
 *
 * 1. 阻塞模型：
 *    recv()阻塞等待 -> 无法同时处理输入
 *    不适合游戏
 *
 * 2. 非阻塞轮询（本Demo）：
 *    循环调用非阻塞recv() + sleep
 *    简单但浪费CPU
 *
 * 3. I/O多路复用：
 *    select/poll/epoll监听多个fd
 *    事件驱动，高效
 *
 * 4. 异步I/O：
 *    IOCP(Windows)/io_uring(Linux)
 *    最高性能
 *
 * 【移动平台注意事项】
 *
 * 1. 后台限制：
 *    iOS/Android后台时网络会被限制
 *    需要处理暂停/恢复
 *
 * 2. 电量优化：
 *    减少心跳频率
 *    批量发送数据
 *
 * 3. 网络切换：
 *    WiFi <-> 移动数据切换会断开连接
 *    需要自动重连
 */
