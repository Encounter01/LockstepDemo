/**
 * @file GameWorld.h
 * @brief 游戏世界 - 帧同步的核心逻辑容器
 *
 * 【技术概述】
 * GameWorld 是帧同步游戏的核心类，负责：
 * 1. 管理所有游戏实体（玩家、子弹）
 * 2. 处理每帧的输入和更新
 * 3. 执行物理模拟和碰撞检测
 * 4. 计算状态校验和
 *
 * 【确定性保证 - 最重要的设计目标】
 * 相同的初始状态 + 相同的输入序列 = 完全相同的最终状态
 * 这是通过以下方式实现的：
 * 1. 所有计算使用定点数（Fixed）
 * 2. 输入按 playerId 排序后处理
 * 3. 使用确定性随机数生成器
 * 4. 实体遍历顺序固定
 *
 * 【面试要点】
 * 1. Q: 帧同步的"帧"指的是什么？
 *    A: 逻辑帧（Logic Frame），不是渲染帧
 *       - 逻辑帧率通常较低（15-30 FPS）
 *       - 渲染帧率可以更高（60 FPS）
 *       - 两者通过插值平滑衔接
 *
 * 2. Q: 为什么输入要排序？
 *    A: 确保所有客户端以相同顺序处理输入
 *       如果不排序，网络包到达顺序不同会导致处理顺序不同
 *       处理顺序不同可能导致结果不同（如同时攻击的判定）
 *
 * 3. Q: 校验和（checksum）怎么用？
 *    A: 每帧计算整个游戏状态的哈希值
 *       服务器广播校验和，客户端对比
 *       不一致说明状态分歧，需要处理（重连/报警）
 *
 * 【生产实践】
 * 1. 内存管理：使用对象池管理子弹等频繁创建的对象
 * 2. 碰撞优化：使用空间分区数据结构
 * 3. 状态保存：支持快照保存和恢复，用于回滚
 * 4. 调试工具：添加录像回放功能，便于排查不同步
 */
#pragma once
#include <vector>
#include <algorithm>
#include <iostream>
#include "Entity.h"
#include "Random.h"
#include "Protocol.h"

namespace lockstep {

/**
 * @class GameWorld
 * @brief 游戏世界管理器
 *
 * 【设计理念】
 * 游戏世界是一个"纯函数"：
 * new_state = tick(old_state, inputs)
 *
 * 给定相同的 old_state 和 inputs，必定得到相同的 new_state
 * 这是帧同步能够工作的数学基础
 *
 * 【使用方式】
 * 1. 初始化：world.init(playerCount, seed)
 * 2. 每帧更新：world.tick(frameData)
 * 3. 获取状态：world.players, world.bullets
 * 4. 校验同步：world.calcChecksum()
 */
class GameWorld {
public:
    // ============ 常量定义 ============
    /**
     * 【地图大小】
     * 使用编译期常量，避免魔数
     * 800x600 是常见的游戏分辨率
     */
    static constexpr int MAP_WIDTH = 800;
    static constexpr int MAP_HEIGHT = 600;

    // ============ 状态变量 ============
    /**
     * 【当前帧号】
     * 从 0 开始，每次 tick 后递增
     * 用于：
     * - 同步校验（服务器和客户端帧号应一致）
     * - 调试日志
     * - 录像标记
     */
    uint32_t currentFrame = 0;

    /**
     * 【玩家列表】
     * 使用 vector 存储，按 ownerId 对应索引
     * players[0] 是玩家 0，players[1] 是玩家 1
     *
     * 【确定性要求】
     * 遍历顺序固定（vector 保证顺序）
     */
    std::vector<Player> players;

    /**
     * 【子弹列表】
     * 动态增减的实体列表
     * 新子弹追加到末尾，死亡子弹会被清理
     *
     * 【内存考虑】
     * 生产环境应使用对象池避免频繁分配
     */
    std::vector<Bullet> bullets;

    /**
     * 【确定性随机数生成器】
     * 所有客户端使用相同种子初始化
     * 调用顺序必须一致
     */
    DeterministicRandom random;

    /**
     * 【下一个实体 ID】
     * 用于分配唯一 ID
     * 从 1000 开始，避免与玩家 ID 冲突
     *
     * 【确定性要求】
     * ID 分配顺序必须一致
     */
    uint32_t nextEntityId = 1000;

    // ============ 初始化 ============
    /**
     * @brief 初始化游戏世界
     * @param playerCount 玩家数量
     * @param seed 随机数种子
     *
     * 【初始化流程】
     * 1. 设置随机数种子
     * 2. 重置帧号和实体 ID
     * 3. 清空所有实体
     * 4. 创建玩家并随机放置
     *
     * 【确定性保证】
     * 相同的 playerCount 和 seed 会产生相同的初始状态
     * 因为随机位置由确定性随机数决定
     */
    void init(uint32_t playerCount, uint32_t seed) {
        random.setSeed(seed);
        currentFrame = 0;
        nextEntityId = 1000;

        players.clear();
        bullets.clear();

        // 创建玩家
        Fixed mapW = Fixed::fromInt(MAP_WIDTH);
        Fixed mapH = Fixed::fromInt(MAP_HEIGHT);

        for (uint32_t i = 0; i < playerCount; ++i) {
            Player player;
            player.id = i;          // 实体 ID = 玩家索引
            player.ownerId = i;     // 所有者 ID = 玩家索引

            // 随机出生位置（避免边缘 100 像素）
            // 注意：这里使用确定性随机数
            player.pos = {
                Fixed::fromInt(100 + random.range(0, MAP_WIDTH - 200)),
                Fixed::fromInt(100 + random.range(0, MAP_HEIGHT - 200))
            };
            player.vel = FixedVec2::zero();

            players.push_back(player);
        }

        std::cout << "[GameWorld] Initialized with " << playerCount
                  << " players, seed=" << seed << std::endl;
    }

    // ============ 帧更新 ============
    /**
     * @brief 执行一帧的游戏逻辑
     * @param frame 帧数据（包含所有玩家的输入）
     *
     * 【核心方法】帧同步的心脏
     *
     * 【更新顺序】（顺序很重要！）
     * 1. 输入排序：按 playerId 排序，确保处理顺序
     * 2. 应用输入：处理玩家移动和攻击
     * 3. 更新玩家：位置更新、冷却更新
     * 4. 更新子弹：位置更新、生命周期
     * 5. 碰撞检测：玩家-玩家、子弹-玩家
     * 6. 清理死亡：移除已死亡的实体
     * 7. 帧号递增
     *
     * 【确定性检查点】
     * - 输入排序保证处理顺序
     * - 所有计算使用定点数
     * - 遍历顺序固定
     * - 碰撞检测顺序固定
     */
    void tick(const FrameData& frame) {
        // 1. 按 playerId 排序输入（确保确定性）
        std::vector<PlayerInput> sortedInputs = frame.inputs;
        std::sort(sortedInputs.begin(), sortedInputs.end(),
            [](const PlayerInput& a, const PlayerInput& b) {
                return a.playerId < b.playerId;
            });

        // 2. 应用输入
        for (const auto& input : sortedInputs) {
            if (input.playerId < players.size()) {
                applyPlayerInput(players[input.playerId], input);
            }
        }

        // 3. 更新所有玩家
        for (auto& player : players) {
            player.update();
            player.clampToMap(Fixed::fromInt(MAP_WIDTH), Fixed::fromInt(MAP_HEIGHT));
        }

        // 4. 更新所有子弹
        for (auto& bullet : bullets) {
            bullet.update();
            bullet.clampToMap(Fixed::fromInt(MAP_WIDTH), Fixed::fromInt(MAP_HEIGHT));
        }

        // 5. 碰撞检测
        checkCollisions();

        // 6. 清理死亡实体
        cleanupDeadEntities();

        // 7. 递增帧号
        currentFrame++;
    }

    // ============ 应用玩家输入 ============
    /**
     * @brief 处理单个玩家的输入
     * @param player 玩家实体
     * @param input 玩家输入
     *
     * 【处理内容】
     * 1. 移动：调用 Entity::applyInput
     * 2. 攻击：检查动作位域的 bit0
     */
    void applyPlayerInput(Player& player, const PlayerInput& input) {
        if (!player.alive) return;

        // 处理移动
        player.applyInput(input);

        // 处理攻击（bit0 = 攻击键）
        if ((input.actions & 0x01) && player.canAttack()) {
            spawnBullet(player, input);
            player.startAttack();
        }
    }

    // ============ 生成子弹 ============
    /**
     * @brief 创建子弹实体
     * @param player 发射者
     * @param input 玩家输入（用于确定方向）
     *
     * 【子弹方向优先级】
     * 1. 如果有目标位置（targetX/Y），朝向目标
     * 2. 否则如果在移动，朝移动方向
     * 3. 否则默认向右
     *
     * 【确定性保证】
     * - 子弹 ID 使用递增分配
     * - 方向计算使用定点数
     */
    void spawnBullet(const Player& player, const PlayerInput& input) {
        Bullet bullet;
        bullet.id = nextEntityId++;     // 分配唯一 ID
        bullet.ownerId = player.ownerId;  // 记录发射者
        bullet.pos = player.pos;        // 从玩家位置发射

        // 计算子弹方向
        if (input.targetX != 0 || input.targetY != 0) {
            // 朝向目标位置
            FixedVec2 target = {
                Fixed::raw(input.targetX),
                Fixed::raw(input.targetY)
            };
            FixedVec2 dir = (target - player.pos).normalize();
            bullet.vel = dir * bullet.speed;
        } else if (input.moveDir < 8) {
            // 朝移动方向
            bullet.vel = DIR_TABLE[input.moveDir] * bullet.speed;
        } else {
            // 默认向右
            bullet.vel = DIR_TABLE[2] * bullet.speed;
        }

        bullets.push_back(bullet);
    }

    // ============ 碰撞检测 ============
    /**
     * @brief 检测并处理所有碰撞
     *
     * 【碰撞类型】
     * 1. 玩家-玩家：弹开（不造成伤害）
     * 2. 子弹-玩家：命中判定（造成伤害）
     *
     * 【复杂度分析】
     * - 玩家-玩家：O(N²)，N 是玩家数（通常很小）
     * - 子弹-玩家：O(B×N)，B 是子弹数
     *
     * 【确定性保证】
     * - 嵌套循环顺序固定
     * - 先处理玩家碰撞，再处理子弹碰撞
     */
    void checkCollisions() {
        // 玩家-玩家碰撞（弹开）
        for (size_t i = 0; i < players.size(); ++i) {
            for (size_t j = i + 1; j < players.size(); ++j) {
                if (players[i].collidesWith(players[j])) {
                    resolvePlayerCollision(players[i], players[j]);
                }
            }
        }

        // 子弹-玩家碰撞
        for (auto& bullet : bullets) {
            if (!bullet.alive) continue;

            for (auto& player : players) {
                // 不能打自己（友军伤害可以改为可选）
                if (bullet.ownerId == player.ownerId) continue;
                if (!player.alive) continue;

                if (bullet.collidesWith(player)) {
                    // 命中！造成伤害
                    player.takeDamage(bullet.damage);
                    bullet.alive = false;

                    // 如果击杀，给发射者加分
                    if (!player.alive) {
                        for (auto& p : players) {
                            if (p.ownerId == bullet.ownerId) {
                                p.addKill();
                                break;
                            }
                        }
                    }
                    break;  // 子弹已命中，不再检测
                }
            }
        }
    }

    // ============ 解决玩家碰撞 ============
    /**
     * @brief 处理两个玩家的碰撞（弹开）
     * @param a 玩家 A
     * @param b 玩家 B
     *
     * 【算法】
     * 1. 计算穿透深度
     * 2. 沿连线方向各退一半
     *
     * 【特殊情况】
     * 如果完全重叠，给一个固定方向避免除零
     */
    void resolvePlayerCollision(Player& a, Player& b) {
        FixedVec2 diff = a.pos - b.pos;
        Fixed dist = diff.length();

        // 处理完全重叠的情况
        if (dist.rawValue() == 0) {
            // 给个固定方向（确定性）
            diff = {Fixed::fromFloat(1.0f), Fixed::fromFloat(0.0f)};
            dist = Fixed::fromFloat(1.0f);
        }

        FixedVec2 normal = diff / dist;  // 单位方向向量
        Fixed overlap = (a.radius + b.radius) - dist;  // 穿透深度

        if (overlap > Fixed::fromInt(0)) {
            // 各退一半
            FixedVec2 push = normal * (overlap / Fixed::fromInt(2));
            a.pos = a.pos + push;
            b.pos = b.pos - push;
        }
    }

    // ============ 清理死亡实体 ============
    /**
     * @brief 移除已死亡的实体
     *
     * 【算法】
     * 使用 erase-remove idiom 高效删除
     *
     * 【为什么玩家不删除】
     * 玩家死亡后保留实体，用于：
     * - 显示死亡状态
     * - 统计面板
     * - 可能的复活机制
     */
    void cleanupDeadEntities() {
        bullets.erase(
            std::remove_if(bullets.begin(), bullets.end(),
                [](const Bullet& b) { return !b.alive; }),
            bullets.end()
        );
    }

    // ============ 计算世界状态校验和 ============
    /**
     * @brief 计算整个游戏世界的状态哈希
     * @return 32 位校验和
     *
     * 【用途】
     * 同步校验：服务器和客户端计算相同的校验和
     * 如果不一致，说明发生了不同步
     *
     * 【算法】
     * 1. 从帧号开始
     * 2. 依次加入每个实体的哈希
     * 3. 使用乘法哈希组合
     *
     * 【确定性保证】
     * - players 遍历顺序固定（按索引）
     * - bullets 遍历顺序固定（按添加顺序）
     * - 每个实体的 hash() 是确定性的
     */
    uint32_t calcChecksum() const {
        uint32_t hash = currentFrame;

        // 按固定顺序计算哈希
        for (const auto& player : players) {
            hash = hash * 31 + player.hash();
        }

        for (const auto& bullet : bullets) {
            hash = hash * 31 + bullet.hash();
        }

        return hash;
    }

    // ============ 游戏状态查询 ============
    /**
     * @brief 获取存活玩家数量
     * @return 存活玩家数
     */
    int getAlivePlayerCount() const {
        int count = 0;
        for (const auto& p : players) {
            if (p.alive) count++;
        }
        return count;
    }

    /**
     * @brief 检查游戏是否结束
     * @return 游戏结束返回 true
     *
     * 【结束条件】
     * 存活玩家 <= 1 且总玩家数 > 1
     * （单人游戏永不结束）
     */
    bool isGameOver() const {
        return getAlivePlayerCount() <= 1 && players.size() > 1;
    }

    /**
     * @brief 获取获胜者 ID
     * @return 获胜者的 ownerId，无获胜者返回 -1
     */
    int getWinnerId() const {
        for (const auto& p : players) {
            if (p.alive) return static_cast<int>(p.ownerId);
        }
        return -1;
    }

    // ============ 调试功能 ============
    /**
     * @brief 打印世界状态（调试用）
     *
     * 【输出内容】
     * - 当前帧号
     * - 校验和
     * - 每个玩家的状态
     * - 子弹数量
     */
    void debugPrint() const {
        std::cout << "=== Frame " << currentFrame << " ===" << std::endl;
        std::cout << "Checksum: " << calcChecksum() << std::endl;

        for (const auto& p : players) {
            std::cout << "Player " << p.ownerId
                      << ": pos=" << p.pos
                      << " hp=" << p.hp
                      << " alive=" << p.alive
                      << " kills=" << p.kills
                      << std::endl;
        }

        std::cout << "Bullets: " << bullets.size() << std::endl;
    }
};

} // namespace lockstep

/**
 * 【扩展阅读】
 *
 * 1. 帧同步的帧更新优化：
 *    | 优化技术 | 描述 | 效果 |
 *    |----------|------|------|
 *    | 输入压缩 | 只发送有变化的输入 | 减少带宽 |
 *    | 预测执行 | 客户端预测下一帧 | 减少延迟感 |
 *    | 回滚机制 | 预测错误时回滚重算 | 保证正确性 |
 *
 * 2. 不同步的常见原因：
 *    - 浮点数运算差异
 *    - 遍历顺序不一致
 *    - 随机数调用不一致
 *    - 条件分支导致的不同路径
 *    - 未初始化的变量
 *
 * 3. 不同步排查技巧：
 *    - 记录每帧校验和
 *    - 录像功能：保存输入序列
 *    - 快照对比：保存详细状态
 *    - 二分查找：定位出问题的帧
 *
 * 4. 生产环境的 GameWorld 扩展：
 *    - 状态快照：支持保存/恢复
 *    - 增量更新：只同步变化的部分
 *    - 事件系统：解耦游戏逻辑
 *    - 可视化调试：绘制碰撞框等
 *
 * 5. 帧同步 vs 状态同步的选择：
 *    | 因素 | 帧同步 | 状态同步 |
 *    |------|--------|----------|
 *    | 玩家数 | 适合少量（<10） | 适合大量（>10） |
 *    | 计算量 | 客户端负担大 | 服务器负担大 |
 *    | 带宽 | 低（只传输入） | 高（传状态） |
 *    | 回放 | 天然支持 | 需要额外存储 |
 *    | 作弊 | 较难检测 | 服务器权威 |
 */
