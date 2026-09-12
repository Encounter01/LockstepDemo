/**
 * @file LockstepServer.h
 * @brief 帧同步服务器 - 负责收集输入、广播帧数据、管理玩家会话
 * @author LockstepDemo
 *
 * ============================================================================
 *                              技术概述
 * ============================================================================
 *
 * 【核心职责】
 * 帧同步服务器是整个帧同步架构的"中央调度器"，主要职责包括：
 * 1. 接收玩家的加入请求，管理玩家会话
 * 2. 收集每帧所有玩家的输入
 * 3. 将收集到的输入广播给所有玩家
 * 4. 维护帧历史，支持断线重连
 *
 * 【架构设计】
 *
 *                    ┌─────────────────────┐
 *                    │   LockstepServer    │
 *                    │                     │
 *   Client 1 ──UDP──>│  ┌───────────────┐  │<──UDP── Client 3
 *                    │  │ PlayerSession │  │
 *   Client 2 ──UDP──>│  │   HashMap     │  │<──UDP── Client 4
 *                    │  └───────────────┘  │
 *                    │         │           │
 *                    │         v           │
 *                    │  ┌───────────────┐  │
 *                    │  │ Frame History │  │
 *                    │  │   (1000帧)    │  │
 *                    │  └───────────────┘  │
 *                    └─────────────────────┘
 *                            │
 *                            │ broadcast
 *                            v
 *                    ┌───────────────────┐
 *                    │ All Clients       │
 *                    │ 收到相同的帧数据  │
 *                    └───────────────────┘
 *
 * 【帧同步时序】
 *
 *   时间 ────────────────────────────────────────────────>
 *
 *   Client1:  [Input1]────────────────>[收到Frame]──>[执行]
 *   Client2:  [Input2]────────────────>[收到Frame]──>[执行]
 *   Server:   ────[收集]──[等待]──[广播Frame]───────────────>
 *                  │                    │
 *                  └── 66ms (15FPS) ────┘
 *
 * ============================================================================
 *                              面试要点
 * ============================================================================
 *
 * 【Q1: 为什么使用UDP而不是TCP？】
 * A: 帧同步游戏对延迟极其敏感：
 *    1. TCP的重传机制会导致头部阻塞(Head-of-Line Blocking)
 *    2. 游戏输入有时效性，过期的输入不如直接丢弃
 *    3. UDP允许自定义可靠性策略，可以只对关键数据保序
 *    4. TCP的拥塞控制可能导致发送延迟
 *
 *    生产环境常用方案：
 *    - UDP + 自定义可靠层(如ENet、KCP)
 *    - QUIC协议（基于UDP的可靠传输）
 *
 * 【Q2: 如何处理玩家输入丢失？】
 * A: 本实现采用"空输入替代"策略：
 *    - 如果某玩家的输入未到达，使用静止(moveDir=8)替代
 *    - 这保证了帧能正常推进，但体验可能不佳
 *
 *    更好的方案：
 *    - 等待所有输入到达（可能导致卡顿）
 *    - 输入预测 + 回滚（增加复杂度）
 *    - 乐观帧同步（客户端预执行，错误时回滚）
 *
 * 【Q3: 帧历史的作用是什么？】
 * A: 主要用于断线重连：
 *    1. 玩家断线后重连，发送上次收到的帧号
 *    2. 服务器从历史中找到缺失的帧，一次性发送
 *    3. 客户端快速回放追赶到当前帧
 *
 *    为什么限制1000帧？
 *    - 15FPS下1000帧 ≈ 67秒
 *    - 超过这个时间重连通常需要完整同步
 *    - 内存考量：每帧数据约100字节，1000帧 ≈ 100KB
 *
 * 【Q4: 服务器的线程模型是什么？】
 * A: 当前实现是单线程事件循环：
 *    - 主循环：处理网络 -> 检查帧时间 -> tick -> sleep
 *    - 优点：简单，无锁竞争（mutex仅保护数据结构）
 *    - 缺点：单线程处理能力有限
 *
 *    生产环境改进：
 *    - 网络IO线程 + 逻辑线程分离
 *    - 使用I/O多路复用(epoll/IOCP)
 *    - 多房间多线程
 *
 * 【Q5: 如何保证帧数据的确定性？】
 * A: 服务器端的关键措施：
 *    1. 对输入按playerId排序后再广播
 *    2. 所有客户端收到相同顺序的输入
 *    3. 使用相同的随机种子初始化
 *    4. 帧号单调递增，不会乱序
 *
 * ============================================================================
 *                              生产实践
 * ============================================================================
 *
 * 【性能优化】
 * 1. 对象池：PlayerSession可以预分配，避免动态内存分配
 * 2. 无锁队列：网络收包用无锁队列，减少mutex竞争
 * 3. 批量发送：将多个小包合并发送，减少系统调用
 * 4. 内存池：帧历史使用环形缓冲区，避免vector扩容
 *
 * 【可靠性增强】
 * 1. 心跳超时检测：超过N秒无响应标记为断线
 * 2. 输入确认机制：客户端确认收到帧后服务器再清理
 * 3. 关键帧重发：对于START、SYNC等重要消息多次发送
 * 4. 序列号校验：防止重放攻击和乱序处理
 *
 * 【安全考量】
 * 1. 输入校验：检查playerId是否合法，输入值是否在范围内
 * 2. 频率限制：防止客户端发送过多请求
 * 3. 加密通信：敏感游戏可使用DTLS加密
 * 4. 反作弊：服务器可以运行游戏逻辑做校验
 *
 * 【监控指标】
 * 1. 帧处理延迟：tick()执行时间
 * 2. 网络往返时间：记录每个玩家的RTT
 * 3. 丢包率：统计未收到输入的帧占比
 * 4. 重连次数：监控网络稳定性
 */
#pragma once

// ============================================================================
//                              标准库头文件
// ============================================================================
#include <iostream>
#include <unordered_map>    // 【选型】O(1)查找玩家会话，比map更快
#include <vector>
#include <chrono>           // 【时间】高精度时钟，用于帧间隔控制
#include <thread>           // 【并发】sleep_for用于CPU降压
#include <mutex>            // 【同步】保护共享数据
#include <atomic>           // 【原子】无锁的running_标志
#include <functional>

// ============================================================================
//                              项目头文件
// ============================================================================
#include "../common/Network.h"   // 跨平台UDP封装
#include "../common/Protocol.h"  // 消息协议定义
#include "../common/GameWorld.h" // 游戏世界（服务器可选用于校验）

namespace lockstep {

/**
 * @class LockstepServer
 * @brief 帧同步服务器核心类
 *
 * 【设计模式】
 * - 单例模式的候选者（一个进程通常只有一个服务器实例）
 * - 观察者模式：可以添加事件回调通知外部
 *
 * 【生命周期】
 * 1. 构造 -> 2. start() -> 3. run() -> 4. stop() -> 5. 析构
 *
 * 【面试考点】为什么不把start()放在构造函数中？
 * A: 遵循RAII但允许二阶段初始化：
 *    - 构造函数不应该失败（或使用异常）
 *    - start()返回bool表示成功/失败
 *    - 允许构造后配置、然后再启动
 */
class LockstepServer {
public:
    // ========================================================================
    //                          配置常量
    // ========================================================================

    /**
     * 【面试要点】为什么选择15FPS作为逻辑帧率？
     *
     * 帧同步游戏的帧率选择是性能与体验的权衡：
     *
     * | 帧率   | 帧间隔  | 网络开销 | 手感    | 典型游戏   |
     * |--------|---------|----------|---------|------------|
     * | 10 FPS | 100ms   | 低       | 迟钝    | 棋牌       |
     * | 15 FPS | 66ms    | 中等     | 可接受  | MOBA       |
     * | 20 FPS | 50ms    | 较高     | 流畅    | RTS        |
     * | 30 FPS | 33ms    | 高       | 很流畅  | 格斗       |
     *
     * 15FPS的优势：
     * 1. 网络开销适中（每秒15个包 vs 30个包）
     * 2. 对延迟容忍度较高（66ms内到达即可）
     * 3. 大多数MOBA游戏采用类似帧率
     *
     * 【生产实践】可以做成可配置参数，根据游戏类型调整
     */
    static constexpr int LOGIC_FPS = 15;

    /**
     * 每帧的毫秒数
     *
     * 【计算】1000ms / 15 = 66.67ms，取整为66ms
     *
     * 【注意】整数除法会有误差累积！
     * 15FPS理论上每帧66.67ms，但我们用66ms，会导致：
     * - 每秒实际执行 1000/66 ≈ 15.15 帧
     * - 长时间运行会有轻微偏差
     *
     * 【生产改进】使用浮点时间累积，消除漂移
     */
    static constexpr int FRAME_MS = 1000 / LOGIC_FPS;

    /**
     * 最大玩家数量
     *
     * 【设计考量】
     * 1. 4人是很多MOBA游戏的常见配置（如王者荣耀5v5）
     * 2. 玩家数影响帧数据大小：4人 × 18字节输入 ≈ 72字节/帧
     * 3. 太多玩家会增加输入收集的复杂度
     *
     * 【面试Q】如何支持100人大房间？
     * A: 需要AOI(Area of Interest)优化：
     *    - 只同步视野内玩家的输入
     *    - 分区域广播
     *    - 状态同步替代帧同步
     */
    static constexpr uint32_t MAX_PLAYERS = 4;

    /**
     * 最少开始人数
     *
     * 【游戏设计】满足最少人数后自动开始游戏
     * 生产环境通常需要：
     * - 准备确认机制
     * - 倒计时开始
     * - 可配置的等待超时
     */
    static constexpr uint32_t MIN_PLAYERS = 2;

    /**
     * 历史帧缓存数量
     *
     * 【内存估算】
     * - 每帧数据：4字节帧号 + N个玩家输入
     * - 4玩家：4 + 4×18 = 76字节/帧
     * - 1000帧：76KB
     *
     * 【时间跨度】
     * - 15FPS下：1000帧 = 1000/15 ≈ 66.7秒
     * - 足够应对大多数短暂断线场景
     *
     * 【面试Q】为什么不无限存储？
     * A: 1. 内存限制
     *    2. 断线太久需要完整状态同步
     *    3. 可以定期做快照(Snapshot)压缩历史
     */
    static constexpr size_t MAX_HISTORY = 1000;

private:
    // ========================================================================
    //                          玩家会话结构
    // ========================================================================

    /**
     * @struct PlayerSession
     * @brief 玩家会话信息
     *
     * 【设计意图】
     * 存储每个玩家的网络地址、输入状态、连接状态
     *
     * 【面试考点】为什么用结构体而不是类？
     * A: 这是一个纯数据容器(Plain Old Data)，没有复杂行为：
     *    - 所有成员都是公开的
     *    - 不需要封装getter/setter
     *    - 便于序列化和内存操作
     *
     * 【生产改进】
     * 1. 添加玩家昵称、头像等元数据
     * 2. 添加延迟统计（RTT、丢包率）
     * 3. 添加anti-cheat相关数据
     */
    struct PlayerSession {
        uint32_t playerId;      // 玩家唯一标识（0开始的连续ID）
        sockaddr_in endpoint;   // 玩家的网络地址（IP:Port）
        PlayerInput lastInput;  // 最近收到的输入
        bool inputReceived = false;  // 本帧是否收到输入

        /**
         * 最后心跳时间
         *
         * 【用途】检测玩家是否断线
         * 【实现】每次收到任何消息都更新
         *
         * 【生产改进】
         * - 超过N秒无响应，标记为断线
         * - 可以发送DISCONNECT通知其他玩家
         */
        std::chrono::steady_clock::time_point lastHeartbeat;

        /**
         * 连接状态
         *
         * 【状态机】
         * connected = true  : 正常连接
         * connected = false : 已断线，保留会话等待重连
         *
         * 【面试Q】为什么不直接删除断线玩家？
         * A: 保留会话用于重连，避免重新分配playerId导致混乱
         */
        bool connected = true;
    };

    // ========================================================================
    //                          网络相关成员
    // ========================================================================

    /**
     * UDP套接字
     *
     * 【选型理由】见文件头部面试要点Q1
     */
    UdpSocket socket_;

    /**
     * 监听端口
     */
    uint16_t port_;

    // ========================================================================
    //                          玩家管理
    // ========================================================================

    /**
     * 玩家会话表
     *
     * 【数据结构选择】unordered_map
     * - key: playerId (uint32_t)
     * - value: PlayerSession
     *
     * 【复杂度】
     * - 查找: O(1) 平均
     * - 插入: O(1) 平均
     * - 遍历: O(n)
     *
     * 【面试Q】为什么不用vector<PlayerSession>？
     * A:
     *    1. map支持任意playerId，vector需要连续
     *    2. map允许O(1)查找特定玩家
     *    3. 虽然4个玩家差别不大，但设计应该scalable
     */
    std::unordered_map<uint32_t, PlayerSession> players_;

    /**
     * 玩家表互斥锁
     *
     * 【保护对象】players_的读写操作
     *
     * 【面试Q】当前是单线程为什么还要mutex？
     * A:
     *    1. 为未来多线程扩展预留
     *    2. 可能有外部线程访问（如统计、管理接口）
     *    3. 防御性编程
     *
     * 【性能考量】
     * - 单线程：mutex开销极小（无竞争时约20ns）
     * - 多线程：考虑使用读写锁(shared_mutex)
     */
    std::mutex playersMutex_;

    // ========================================================================
    //                          游戏状态
    // ========================================================================

    /**
     * 当前帧号
     *
     * 【初始值】0
     * 【范围】uint32_t，约40亿帧
     *
     * 【计算】15FPS下：
     * - 1小时 = 15×3600 = 54,000帧
     * - uint32_t最大值可以运行约8.8万小时 ≈ 10年
     * - 溢出风险极低，但生产环境应该处理
     */
    uint32_t currentFrame_ = 0;

    /**
     * 游戏是否已开始
     *
     * 【状态机】
     * false -> true : 满足MIN_PLAYERS后触发
     * true  -> false: 当前实现不支持（生产需要添加游戏结束逻辑）
     */
    bool gameStarted_ = false;

    /**
     * 随机种子
     *
     * 【用途】所有客户端使用相同种子，保证随机数一致
     *
     * 【初始化】使用系统时间生成
     *
     * 【面试Q】为什么不用固定种子？
     * A: 固定种子会导致每局游戏完全一样，缺乏变化
     *    但如果需要重放(Replay)，可以记录种子
     */
    uint32_t randomSeed_ = 0;

    // ========================================================================
    //                          帧历史
    // ========================================================================

    /**
     * 帧历史缓存
     *
     * 【数据结构】vector实现的环形缓冲区概念
     *
     * 【操作】
     * - push_back：添加新帧
     * - erase(begin)：超过MAX_HISTORY时删除最老的帧
     *
     * 【面试Q】为什么不用deque？
     * A:
     *    1. deque两端操作O(1)，更适合这个场景
     *    2. 但vector的连续内存对缓存更友好
     *    3. 1000个元素时差异不大
     *
     * 【生产改进】使用boost::circular_buffer或自定义环形缓冲区
     */
    std::vector<FrameData> frameHistory_;

    /**
     * 历史帧互斥锁
     *
     * 【保护对象】frameHistory_的读写
     * 【并发场景】重连处理可能与tick()同时访问
     */
    std::mutex historyMutex_;

    // ========================================================================
    //                          运行状态
    // ========================================================================

    /**
     * 运行标志
     *
     * 【类型选择】std::atomic<bool>
     *
     * 【面试Q】为什么用atomic而不是普通bool？
     * A:
     *    1. 跨线程可见性保证
     *    2. 避免编译器优化导致的问题
     *    3. 即使当前单线程，也是最佳实践
     *
     * 【内存序】
     * - 默认memory_order_seq_cst
     * - 可以优化为memory_order_relaxed（仅读写）
     */
    std::atomic<bool> running_{false};

    // ========================================================================
    //                          接收缓冲区
    // ========================================================================

    /**
     * 接收缓冲区大小
     *
     * 【计算依据】
     * - 最大消息：SYNC消息（包含多个帧数据）
     * - 100帧数据 ≈ 100×76 = 7600字节
     * - 4096足够处理单个玩家输入，SYNC需要更大
     *
     * 【生产改进】
     * - 动态分配大缓冲区处理SYNC
     * - 或者SYNC分片发送
     */
    static constexpr size_t RECV_BUFFER_SIZE = 4096;

    /**
     * 接收缓冲区
     *
     * 【设计意图】预分配避免每次recvFrom都分配内存
     *
     * 【线程安全】单线程访问，无需加锁
     */
    uint8_t recvBuffer_[RECV_BUFFER_SIZE];

public:
    // ========================================================================
    //                          构造与析构
    // ========================================================================

    /**
     * @brief 构造函数
     * @param port 监听端口号
     *
     * 【设计原则】
     * 1. 只做简单初始化，不执行可能失败的操作
     * 2. socket创建放在start()中，允许失败处理
     *
     * 【随机种子生成】
     * 使用系统时钟的纳秒数作为种子
     * - 优点：简单，每次不同
     * - 缺点：可预测（知道大概启动时间可以猜测）
     * - 生产改进：使用crypto-secure RNG
     */
    LockstepServer(uint16_t port) : port_(port) {
        // 使用当前时间作为随机种子
        randomSeed_ = static_cast<uint32_t>(
            std::chrono::system_clock::now().time_since_epoch().count()
        );
    }

    /**
     * @brief 析构函数
     *
     * 【RAII原则】确保资源正确释放
     *
     * 【调用stop()】
     * - 即使用户忘记调用stop()，析构时也会清理
     * - stop()内部检查running_状态，重复调用安全
     */
    ~LockstepServer() {
        stop();
    }

    // ========================================================================
    //                          生命周期管理
    // ========================================================================

    /**
     * @brief 启动服务器
     * @return true=成功, false=失败
     *
     * 【执行步骤】
     * 1. 初始化网络库（Windows需要WSAStartup）
     * 2. 创建UDP套接字
     * 3. 绑定到指定端口
     * 4. 设置非阻塞模式
     * 5. 标记running_=true
     *
     * 【错误处理】
     * - 任何步骤失败都返回false
     * - 打印错误信息帮助调试
     *
     * 【面试Q】为什么设置非阻塞模式？
     * A:
     *    1. 非阻塞recvFrom立即返回，不会卡住主循环
     *    2. 允许在同一线程处理多个操作
     *    3. 配合循环轮询实现事件处理
     */
    bool start() {
        // 步骤1: 初始化网络库（Windows平台需要）
        init_network();

        // 步骤2: 创建UDP套接字
        if (!socket_.create()) {
            std::cerr << "[Server] Failed to create socket" << std::endl;
            return false;
        }

        // 步骤3: 绑定端口
        // 【面试Q】bind失败的常见原因？
        // A: 1. 端口已被占用
        //    2. 权限不足（<1024端口需要root）
        //    3. 地址已在使用中（TIME_WAIT状态）
        if (!socket_.bind(port_)) {
            std::cerr << "[Server] Failed to bind to port " << port_ << std::endl;
            return false;
        }

        // 步骤4: 设置非阻塞模式
        socket_.setNonBlocking();

        // 步骤5: 标记运行状态
        running_ = true;

        std::cout << "[Server] Started on port " << port_ << std::endl;
        std::cout << "[Server] Waiting for " << MIN_PLAYERS << " players..." << std::endl;

        return true;
    }

    /**
     * @brief 停止服务器
     *
     * 【操作顺序】
     * 1. 设置running_=false（通知主循环退出）
     * 2. 关闭套接字
     * 3. 清理网络库
     *
     * 【线程安全】
     * - running_是atomic，可以从其他线程调用stop()
     * - 主循环会在下一次迭代检测到并退出
     */
    void stop() {
        running_ = false;
        socket_.close();
        cleanup_network();
    }

    /**
     * @brief 主运行循环
     *
     * 【事件循环模式】
     *
     *    ┌──────────────────────────────────────────┐
     *    │              while(running_)              │
     *    │                                          │
     *    │   ┌─────────────────┐                    │
     *    │   │ processNetwork()│ ◄─ 处理所有待处理消息│
     *    │   └────────┬────────┘                    │
     *    │            │                             │
     *    │            v                             │
     *    │   ┌─────────────────┐                    │
     *    │   │ gameStarted_?   │                    │
     *    │   └────────┬────────┘                    │
     *    │            │ yes                         │
     *    │            v                             │
     *    │   ┌─────────────────┐                    │
     *    │   │ elapsed >= 66ms?│                    │
     *    │   └────────┬────────┘                    │
     *    │            │ yes                         │
     *    │            v                             │
     *    │   ┌─────────────────┐                    │
     *    │   │     tick()      │ ◄─ 收集输入,广播帧  │
     *    │   └────────┬────────┘                    │
     *    │            │                             │
     *    │            v                             │
     *    │   ┌─────────────────┐                    │
     *    │   │ sleep(1ms)      │ ◄─ 降低CPU占用     │
     *    │   └─────────────────┘                    │
     *    └──────────────────────────────────────────┘
     *
     * 【面试Q】为什么用sleep而不是忙等待？
     * A:
     *    1. 忙等待会占用100% CPU
     *    2. 1ms的sleep几乎不影响响应延迟
     *    3. 降低功耗，对服务器和笔记本都友好
     *
     * 【面试Q】如何改进这个循环？
     * A: 生产环境改进：
     *    1. 使用select/epoll/IOCP等I/O多路复用
     *    2. 事件驱动而非轮询
     *    3. 精确的定时器（如timerfd）
     */
    void run() {
        // 上一帧的时间点
        auto lastFrameTime = std::chrono::steady_clock::now();

        while (running_) {
            // 步骤1: 处理网络消息
            // 非阻塞接收，有多少处理多少
            processNetwork();

            // 步骤2: 如果游戏已开始，执行帧逻辑
            if (gameStarted_) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - lastFrameTime
                ).count();

                // 达到帧间隔，执行tick
                if (elapsed >= FRAME_MS) {
                    tick();
                    lastFrameTime = now;

                    // 【优化点】如果处理太慢(elapsed > 2*FRAME_MS)
                    // 可能需要跳帧或警告
                }
            }

            // 步骤3: 短暂休眠避免CPU满载
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

private:
    // ========================================================================
    //                          网络处理
    // ========================================================================

    /**
     * @brief 处理所有待处理的网络消息
     *
     * 【非阻塞轮询】
     * - recvFrom在非阻塞模式下立即返回
     * - 返回-1表示没有数据（WOULD_BLOCK）
     * - 循环处理直到没有更多数据
     *
     * 【面试Q】为什么要循环处理直到没有数据？
     * A:
     *    1. 一次可能收到多个包
     *    2. 确保及时处理所有消息
     *    3. 避免消息堆积
     */
    void processNetwork() {
        sockaddr_in fromAddr{};
        int len = socket_.recvFrom(recvBuffer_, RECV_BUFFER_SIZE, fromAddr);

        // 循环处理所有待处理消息
        while (len > 0) {
            handleMessage(recvBuffer_, len, fromAddr);
            len = socket_.recvFrom(recvBuffer_, RECV_BUFFER_SIZE, fromAddr);
        }
    }

    /**
     * @brief 分发处理消息
     * @param data 消息数据
     * @param len 数据长度
     * @param sender 发送者地址
     *
     * 【协议解析】
     * 消息第一个字节是消息类型(MsgType)
     * 后续字节是消息体
     *
     *    ┌──────┬───────────────────────┐
     *    │ Type │       Payload         │
     *    │ 1B   │      变长              │
     *    └──────┴───────────────────────┘
     *
     * 【面试Q】这种消息分发方式的优缺点？
     * 优点：
     *    - 简单直观
     *    - 易于理解和调试
     * 缺点：
     *    - switch-case不够灵活
     *    - 新增消息类型需要修改代码
     * 改进：
     *    - 使用消息处理器注册表（map<MsgType, Handler>）
     *    - 反射或代码生成
     */
    void handleMessage(const uint8_t* data, size_t len, const sockaddr_in& sender) {
        if (len < 1) return;  // 消息太短，至少需要类型字节

        MsgType type = static_cast<MsgType>(data[0]);

        switch (type) {
            case MsgType::JOIN:
                handleJoin(sender);
                break;
            case MsgType::INPUT:
                handleInput(data + 1, len - 1);  // 跳过类型字节
                break;
            case MsgType::RECONNECT:
                handleReconnect(data + 1, len - 1, sender);
                break;
            case MsgType::HEARTBEAT:
                handleHeartbeat(data + 1, len - 1, sender);
                break;
            default:
                // 未知消息类型，忽略
                // 【安全】生产环境应该记录日志
                break;
        }
    }

    // ========================================================================
    //                          加入处理
    // ========================================================================

    /**
     * @brief 处理玩家加入请求
     * @param sender 发送者地址
     *
     * 【加入流程】
     *
     *    Client                     Server
     *      │                           │
     *      │──── JOIN ────────────────>│
     *      │                           │ 验证:
     *      │                           │ - 游戏未开始?
     *      │                           │ - 房间未满?
     *      │                           │ - 未重复加入?
     *      │                           │
     *      │<─── JOIN_ACK ────────────│ 分配playerId
     *      │    (playerId, count)      │
     *      │                           │
     *      │    [等待其他玩家...]       │
     *      │                           │
     *      │<─── START ───────────────│ 达到MIN_PLAYERS
     *      │    (count, seed)          │
     *
     * 【面试Q】如何防止重复加入？
     * A: 检查发送者IP:Port是否已存在
     *    - 相同地址视为重复请求
     *    - 直接忽略或返回已有的playerId
     */
    void handleJoin(const sockaddr_in& sender) {
        std::lock_guard<std::mutex> lock(playersMutex_);

        // 检查1: 游戏是否已开始
        if (gameStarted_) {
            std::cout << "[Server] Game already started, rejecting join" << std::endl;
            // 【改进】可以发送拒绝消息告知原因
            return;
        }

        // 检查2: 房间是否已满
        if (players_.size() >= MAX_PLAYERS) {
            std::cout << "[Server] Room full, rejecting join" << std::endl;
            // 【改进】发送ROOM_FULL消息
            return;
        }

        // 检查3: 是否重复加入
        for (const auto& [id, session] : players_) {
            if (UdpSocket::addrEquals(session.endpoint, sender)) {
                std::cout << "[Server] Player already joined" << std::endl;
                // 【改进】重发JOIN_ACK，客户端可能没收到
                return;
            }
        }

        // 分配playerId（使用当前玩家数量作为ID）
        // 【注意】这种方式产生连续ID：0, 1, 2, 3
        uint32_t playerId = static_cast<uint32_t>(players_.size());

        // 创建会话
        PlayerSession session;
        session.playerId = playerId;
        session.endpoint = sender;
        session.lastInput = {};
        session.lastInput.playerId = playerId;
        session.lastInput.moveDir = 8;  // 初始静止
        session.inputReceived = false;
        session.lastHeartbeat = std::chrono::steady_clock::now();
        session.connected = true;

        players_[playerId] = session;

        std::cout << "[Server] Player " << playerId << " joined from "
                  << UdpSocket::addrToString(sender) << std::endl;

        // 发送加入确认
        sendJoinAck(playerId, sender);

        // 检查是否可以开始游戏
        if (players_.size() >= MIN_PLAYERS) {
            startGame();
        }
    }

    /**
     * @brief 发送加入确认消息
     * @param playerId 分配的玩家ID
     * @param addr 目标地址
     *
     * 【消息内容】
     * - playerId: 分配给该玩家的ID
     * - playerCount: 当前房间人数
     *
     * 【面试Q】UDP消息可能丢失，如何确保客户端收到？
     * A: 当前实现未处理。改进方案：
     *    1. 客户端未收到ACK会重发JOIN
     *    2. 服务器检测到重复JOIN可以重发ACK
     *    3. 或使用可靠UDP层
     */
    void sendJoinAck(uint32_t playerId, const sockaddr_in& addr) {
        std::vector<uint8_t> msg;
        JoinAckMsg ack;
        ack.playerId = playerId;
        ack.playerCount = static_cast<uint32_t>(players_.size());
        ack.serialize(msg);

        socket_.sendTo(msg.data(), msg.size(), addr);
    }

    // ========================================================================
    //                          游戏开始
    // ========================================================================

    /**
     * @brief 开始游戏
     *
     * 【触发条件】玩家数量达到MIN_PLAYERS
     *
     * 【广播内容】
     * - playerCount: 参与游戏的玩家数量
     * - randomSeed: 统一的随机种子
     *
     * 【确定性保证】
     * 所有客户端收到相同的：
     * 1. 玩家数量（决定实体数量）
     * 2. 随机种子（决定随机序列）
     * 3. 帧号从0开始
     */
    void startGame() {
        gameStarted_ = true;
        currentFrame_ = 0;

        std::cout << "[Server] Game starting with " << players_.size()
                  << " players, seed=" << randomSeed_ << std::endl;

        // 构建开始消息
        std::vector<uint8_t> msg;
        StartMsg start;
        start.playerCount = static_cast<uint32_t>(players_.size());
        start.randomSeed = randomSeed_;
        start.serialize(msg);

        // 广播给所有玩家
        broadcast(msg);
    }

    // ========================================================================
    //                          输入处理
    // ========================================================================

    /**
     * @brief 处理玩家输入
     * @param data 输入数据（不含消息类型）
     * @param len 数据长度
     *
     * 【面试Q】为什么不验证输入来源？
     * A: 当前实现信任输入中的playerId
     *    改进方案：
     *    1. 记录每个地址对应的playerId
     *    2. 验证发送者地址与playerId匹配
     *    3. 防止伪造其他玩家输入
     *
     * 【面试Q】如何处理输入到达过快？
     * A: 当前实现只保留最新输入
     *    每帧开始时inputReceived重置为false
     *    帧结束前收到的最后一个输入被使用
     */
    void handleInput(const uint8_t* data, size_t len) {
        if (len < PlayerInput::SIZE) return;  // 数据不完整

        // 反序列化输入
        PlayerInput input;
        size_t offset = 0;
        input.deserialize(data, offset);

        std::lock_guard<std::mutex> lock(playersMutex_);

        // 更新对应玩家的输入
        if (players_.count(input.playerId)) {
            players_[input.playerId].lastInput = input;
            players_[input.playerId].inputReceived = true;
            // 收到输入也更新心跳时间
            players_[input.playerId].lastHeartbeat = std::chrono::steady_clock::now();
        }
        // 【安全】未知playerId的输入被忽略
    }

    // ========================================================================
    //                          帧更新（核心逻辑）
    // ========================================================================

    /**
     * @brief 执行一帧的服务器逻辑
     *
     * 【核心职责】
     * 1. 收集所有玩家的输入
     * 2. 对输入排序保证确定性
     * 3. 广播帧数据给所有玩家
     * 4. 保存到历史用于重连
     *
     * 【帧数据流程】
     *
     *    Player 0 ──[Input]──┐
     *                        │
     *    Player 1 ──[Input]──┼──> [收集] -> [排序] -> [广播]
     *                        │                           │
     *    Player 2 ──[Input]──┤                           v
     *                        │                    ┌─────────────┐
     *    Player 3 ──[Input]──┘                    │ FrameData   │
     *                                             │ - frameId   │
     *                                             │ - inputs[]  │
     *                                             └──────┬──────┘
     *                                                    │
     *                    ┌────────────────────────────────┼────────────────────────────────┐
     *                    │                                │                                │
     *                    v                                v                                v
     *               Player 0                         Player 1                         Player N
     *               (执行帧)                          (执行帧)                          (执行帧)
     *
     * 【面试要点：确定性排序】
     * - 必须对inputs按playerId排序
     * - 否则不同客户端可能以不同顺序处理输入
     * - 导致状态分歧
     */
    void tick() {
        FrameData frame;
        frame.frameId = currentFrame_;

        {
            std::lock_guard<std::mutex> lock(playersMutex_);

            // 收集所有玩家输入
            for (auto& [id, session] : players_) {
                if (session.inputReceived) {
                    // 使用收到的输入
                    frame.inputs.push_back(session.lastInput);
                } else {
                    // 【关键】没收到输入，使用空输入
                    // 这保证帧可以继续，但玩家可能感觉"卡住了"
                    PlayerInput empty{};
                    empty.playerId = id;
                    empty.frameId = currentFrame_;
                    empty.moveDir = 8;  // 静止
                    frame.inputs.push_back(empty);
                }
                // 重置输入标记，准备收集下一帧
                session.inputReceived = false;
            }
        }

        // 【确定性关键】排序确保所有客户端处理顺序一致
        // unordered_map遍历顺序不确定，必须排序
        std::sort(frame.inputs.begin(), frame.inputs.end(),
            [](const PlayerInput& a, const PlayerInput& b) {
                return a.playerId < b.playerId;
            });

        // 构建并广播帧消息
        std::vector<uint8_t> msg;
        writeU8(msg, static_cast<uint8_t>(MsgType::FRAME));
        frame.serialize(msg);

        broadcast(msg);

        // 保存到历史（用于重连）
        {
            std::lock_guard<std::mutex> lock(historyMutex_);
            frameHistory_.push_back(frame);

            // 维护历史大小限制
            // 【优化】erase(begin)是O(n)操作
            // 改进：使用环形缓冲区
            if (frameHistory_.size() > MAX_HISTORY) {
                frameHistory_.erase(frameHistory_.begin());
            }
        }

        currentFrame_++;

        // 每100帧输出状态（约6.6秒）
        if (currentFrame_ % 100 == 0) {
            std::cout << "[Server] Frame " << currentFrame_ << std::endl;
        }
    }

    // ========================================================================
    //                          重连处理
    // ========================================================================

    /**
     * @brief 处理重连请求
     * @param data 请求数据
     * @param len 数据长度
     * @param sender 发送者地址
     *
     * 【重连流程】
     *
     *    Client                           Server
     *      │                                 │
     *      │  [断线...]                      │
     *      │                                 │
     *      │── RECONNECT ───────────────────>│
     *      │   (playerId, lastFrame)         │
     *      │                                 │
     *      │                     验证playerId │
     *      │                     更新endpoint │
     *      │                                 │
     *      │<────── SYNC ────────────────────│
     *      │   (遗漏的帧数据)                 │
     *      │                                 │
     *      │   [快速回放追赶]                 │
     *      │                                 │
     *      │<────── FRAME ───────────────────│
     *      │   [正常游戏]                     │
     *
     * 【面试Q】为什么需要更新endpoint？
     * A: 重连后客户端的IP:Port可能变化：
     *    - NAT重新分配端口
     *    - 切换网络（WiFi<->移动数据）
     *    - 不同网络接口
     */
    void handleReconnect(const uint8_t* data, size_t len, const sockaddr_in& sender) {
        if (len < 8) return;  // 至少需要playerId + lastFrame

        // 解析重连请求
        size_t offset = 0;
        uint32_t playerId = readU32(data, offset);
        uint32_t lastFrame = readU32(data, offset);

        std::lock_guard<std::mutex> lock(playersMutex_);

        // 验证playerId存在
        if (!players_.count(playerId)) return;

        // 更新会话信息
        players_[playerId].endpoint = sender;     // 新地址
        players_[playerId].connected = true;      // 恢复连接
        players_[playerId].lastHeartbeat = std::chrono::steady_clock::now();

        std::cout << "[Server] Player " << playerId << " reconnecting from frame "
                  << lastFrame << std::endl;

        // 发送遗漏的帧数据
        sendSyncFrames(playerId, lastFrame, sender);
    }

    /**
     * @brief 发送同步帧数据
     * @param playerId 玩家ID
     * @param lastFrame 客户端最后收到的帧号
     * @param addr 目标地址
     *
     * 【同步策略】
     * 发送从 lastFrame+1 到当前帧的所有数据
     *
     * 【消息格式】
     *
     *    ┌──────┬───────┬──────────┬──────────┬─────┐
     *    │ Type │ Count │ Frame 1  │ Frame 2  │ ... │
     *    │ 1B   │ 2B    │ 变长     │ 变长     │     │
     *    └──────┴───────┴──────────┴──────────┴─────┘
     *
     * 【面试Q】如果需要同步的帧太多怎么办？
     * A: 当前实现一次性发送所有帧
     *    问题：
     *    - 消息可能过大（超过MTU导致分片）
     *    - UDP不保证大包可靠送达
     *
     *    改进：
     *    - 分批发送（每次100帧）
     *    - 使用可靠传输层
     *    - 超过阈值发送全量快照
     */
    void sendSyncFrames(uint32_t playerId, uint32_t lastFrame, const sockaddr_in& addr) {
        std::lock_guard<std::mutex> lock(historyMutex_);

        if (frameHistory_.empty()) return;

        // 计算起始索引
        // 【算法】lastFrame之后的帧在历史中的位置
        size_t startIdx = 0;
        if (!frameHistory_.empty() && lastFrame >= frameHistory_.front().frameId) {
            startIdx = lastFrame - frameHistory_.front().frameId + 1;
        }

        // 没有需要同步的帧
        if (startIdx >= frameHistory_.size()) return;

        // 构建同步消息
        std::vector<uint8_t> msg;
        writeU8(msg, static_cast<uint8_t>(MsgType::SYNC));

        uint16_t count = static_cast<uint16_t>(frameHistory_.size() - startIdx);
        writeU16(msg, count);

        // 序列化所有需要同步的帧
        for (size_t i = startIdx; i < frameHistory_.size(); ++i) {
            frameHistory_[i].serialize(msg);
        }

        socket_.sendTo(msg.data(), msg.size(), addr);

        std::cout << "[Server] Sent " << count << " sync frames to player "
                  << playerId << std::endl;
    }

    // ========================================================================
    //                          心跳处理
    // ========================================================================

    /**
     * @brief 处理心跳消息
     * @param data 心跳数据
     * @param len 数据长度
     * @param sender 发送者地址
     *
     * 【心跳机制】
     * 客户端定期发送心跳，服务器更新lastHeartbeat
     *
     * 【用途】
     * 1. 检测玩家是否在线
     * 2. NAT保活（某些NAT会超时断开）
     *
     * 【面试Q】当前实现有什么问题？
     * A:
     *    1. 没有超时检测逻辑（应该有定时器检查）
     *    2. 通过地址查找效率低（O(n)遍历）
     *    3. 可以让心跳消息携带playerId提高效率
     */
    void handleHeartbeat(const uint8_t* data, size_t len, const sockaddr_in& sender) {
        std::lock_guard<std::mutex> lock(playersMutex_);

        // 遍历查找发送者（低效，应该用地址->ID映射）
        for (auto& [id, session] : players_) {
            if (UdpSocket::addrEquals(session.endpoint, sender)) {
                session.lastHeartbeat = std::chrono::steady_clock::now();
                break;
            }
        }
    }

    // ========================================================================
    //                          广播
    // ========================================================================

    /**
     * @brief 广播消息给所有已连接的玩家
     * @param msg 要发送的消息
     *
     * 【实现】遍历所有会话，发送给connected=true的玩家
     *
     * 【面试Q】UDP广播的效率问题？
     * A:
     *    1. 每个玩家单独发送，系统调用次数多
     *    2. 改进：使用sendmmsg批量发送（Linux特有）
     *    3. 或使用多播(Multicast)减少发送次数
     *
     * 【面试Q】如果某个玩家发送失败怎么办？
     * A:
     *    1. UDP发送通常不会失败（除非缓冲区满）
     *    2. 即使失败，帧同步不重传单个帧
     *    3. 客户端通过重连机制补齐缺失帧
     */
    void broadcast(const std::vector<uint8_t>& msg) {
        std::lock_guard<std::mutex> lock(playersMutex_);

        for (const auto& [id, session] : players_) {
            if (session.connected) {
                socket_.sendTo(msg.data(), msg.size(), session.endpoint);
            }
        }
    }
};

} // namespace lockstep

// ============================================================================
//                              扩展阅读
// ============================================================================
/**
 * 【帧同步服务器架构演进】
 *
 * Level 1: 单线程轮询（本Demo）
 * - 简单，适合学习和原型
 * - 处理能力有限
 *
 * Level 2: I/O多路复用
 * - 使用select/poll/epoll
 * - 事件驱动，不浪费CPU
 * - 仍然是单线程处理
 *
 * Level 3: 多线程
 * - 网络IO线程 + 逻辑线程
 * - 无锁队列传递消息
 * - 需要仔细处理线程安全
 *
 * Level 4: 多进程/分布式
 * - 多房间多进程
 * - 负载均衡
 * - 服务发现
 *
 * 【推荐学习资源】
 * - 《游戏编程模式》- 事件循环、组件模式
 * - 《Linux高性能服务器编程》- epoll、多线程
 * - GDC演讲：Overwatch Gameplay Architecture
 */
