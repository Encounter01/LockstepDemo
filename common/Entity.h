/**
 * @file Entity.h
 * @brief 游戏实体定义 - 帧同步游戏的核心对象模型
 *
 * 【技术概述】
 * 本文件定义了游戏中的所有实体类型（玩家、子弹等）。
 * 所有实体都使用定点数（Fixed）进行计算，确保在不同客户端上
 * 的计算结果完全一致，这是帧同步的基础。
 *
 * 【实体系统设计】
 * - Entity：基类，包含位置、速度、碰撞等通用属性
 * - Player：玩家实体，继承自 Entity，添加攻击、分数等
 * - Bullet：子弹实体，继承自 Entity，添加伤害、生命周期
 *
 * 【面试要点】
 * 1. Q: 为什么使用预计算的方向向量表？
 *    A: 避免运行时调用三角函数（sin/cos），原因：
 *       - 三角函数在不同平台上可能有微小差异
 *       - 预计算的定点数是完全确定的
 *       - 查表比计算快
 *
 * 2. Q: 碰撞检测为什么用距离平方而不是距离？
 *    A: 避免开平方运算：
 *       - distSq < radiusSum² 等价于 dist < radiusSum
 *       - 开平方是昂贵的操作
 *       - 对于大量碰撞检测，节省的计算量很大
 *
 * 3. Q: 状态哈希的作用是什么？
 *    A: 同步校验：
 *       - 每个实体计算自己的状态哈希
 *       - 所有实体的哈希组合成帧校验和
 *       - 客户端间对比校验和，检测不同步
 *
 * 【生产实践】
 * 1. 对象池：频繁创建/销毁的实体（如子弹）应使用对象池
 * 2. 空间分区：大量实体时使用四叉树优化碰撞检测
 * 3. 组件化：复杂游戏可采用 ECS（Entity-Component-System）架构
 * 4. 序列化：支持实体状态的保存/恢复，用于重连
 */
#pragma once
#include "Fixed.h"
#include "Protocol.h"
#include <cstdint>

namespace lockstep {

// ============ 8 方向向量表 ============
/**
 * 【方向向量查找表】
 *
 * 预计算的 8 方向单位向量，用于将移动方向编码转换为速度向量。
 *
 * 方向编码布局：
 *     7   0   1
 *      \  |  /
 *   6 ←  ·  → 2
 *      /  |  \
 *     5   4   3
 *
 * 【为什么是 9 个元素】
 * 0-7：8 个移动方向
 * 8：静止（零向量）
 *
 * 【数值说明】
 * - 正交方向 (0,2,4,6): 使用 (±1, 0) 或 (0, ±1)
 * - 对角方向 (1,3,5,7): 使用 (±0.707, ±0.707)
 *   0.707 ≈ 1/√2，使对角向量也是单位向量
 *
 * 【确定性保证】
 * 使用 Fixed::fromFloat() 预计算，所有客户端得到相同值
 * 虽然用了 fromFloat，但这是编译时常量，不影响运行时确定性
 *
 * 【面试考点】
 * Q: 如果对角方向用 (1, 1) 会怎样？
 * A: 向量长度变成 √2 ≈ 1.414
 *    对角移动速度会比正交快 41%
 *    玩家会发现斜着走更快，这是经典的游戏 bug
 *
 * 【inline 关键字说明】
 * 全局变量在头文件中定义必须用 inline（C++17）
 * 避免多个编译单元包含时的重复定义错误
 */
inline const FixedVec2 DIR_TABLE[9] = {
    {Fixed::fromFloat(0.0f),    Fixed::fromFloat(-1.0f)},   // 0: 上 (Y轴负方向)
    {Fixed::fromFloat(0.707f),  Fixed::fromFloat(-0.707f)}, // 1: 右上
    {Fixed::fromFloat(1.0f),    Fixed::fromFloat(0.0f)},    // 2: 右 (X轴正方向)
    {Fixed::fromFloat(0.707f),  Fixed::fromFloat(0.707f)},  // 3: 右下
    {Fixed::fromFloat(0.0f),    Fixed::fromFloat(1.0f)},    // 4: 下 (Y轴正方向)
    {Fixed::fromFloat(-0.707f), Fixed::fromFloat(0.707f)},  // 5: 左下
    {Fixed::fromFloat(-1.0f),   Fixed::fromFloat(0.0f)},    // 6: 左 (X轴负方向)
    {Fixed::fromFloat(-0.707f), Fixed::fromFloat(-0.707f)}, // 7: 左上
    {Fixed::fromFloat(0.0f),    Fixed::fromFloat(0.0f)},    // 8: 静止 (零向量)
};

// ============ 实体类型 ============
/**
 * @enum EntityType
 * @brief 实体类型枚举
 *
 * 【设计说明】
 * 使用 uint8_t 底层类型，节省内存和网络带宽
 *
 * 【扩展性】
 * 可以添加更多类型：ENEMY, NPC, OBSTACLE 等
 */
enum class EntityType : uint8_t {
    PLAYER = 0,  // 玩家
    BULLET = 1,  // 子弹
    ITEM   = 2,  // 道具
};

// ============ 基础实体 ============
/**
 * @struct Entity
 * @brief 实体基类
 *
 * 【设计理念】
 * 所有游戏对象的公共基类，包含：
 * - 标识信息（id, ownerId, type）
 * - 物理属性（pos, vel, speed, radius）
 * - 状态信息（hp, maxHp, alive）
 *
 * 【内存布局】
 * 使用 struct 而非 class，默认公开所有成员
 * 简化访问，适合数据导向设计
 *
 * 【帧同步关键】
 * 所有数值类型都使用 Fixed（定点数）
 * 确保不同客户端的计算结果一致
 */
struct Entity {
    uint32_t id = 0;           // 实体唯一 ID
    uint32_t ownerId = 0;      // 所属玩家 ID（子弹的发射者等）
    EntityType type = EntityType::PLAYER;  // 实体类型

    FixedVec2 pos;             // 位置（定点数向量）
    FixedVec2 vel;             // 速度（定点数向量）
    Fixed speed;               // 移动速度标量
    Fixed radius;              // 碰撞半径（用于圆形碰撞）

    int32_t hp = 100;          // 当前生命值
    int32_t maxHp = 100;       // 最大生命值
    bool alive = true;         // 是否存活

    /**
     * @brief 应用玩家输入
     * @param input 玩家输入数据
     *
     * 【核心方法】帧同步的输入处理
     *
     * 【流程】
     * 1. 检查是否存活
     * 2. 根据移动方向编码查表获取方向向量
     * 3. 方向向量 × 速度 = 速度向量
     *
     * 【确定性保证】
     * - 使用预计算的方向表
     * - 使用定点数乘法
     * - 所有客户端执行相同逻辑
     */
    void applyInput(const PlayerInput& input) {
        if (!alive) return;

        // 处理移动：查表获取方向，乘以速度
        if (input.moveDir < 8) {
            vel = DIR_TABLE[input.moveDir] * speed;
        } else {
            vel = FixedVec2::zero();  // 静止
        }
    }

    /**
     * @brief 更新实体状态（每帧调用）
     *
     * 【基本物理更新】
     * 位置 += 速度（简单欧拉积分）
     *
     * 【派生类扩展】
     * 子类可以 override 添加额外逻辑（如冷却更新）
     */
    void update() {
        if (!alive) return;

        // 位置更新：pos += vel
        pos = pos + vel;
    }

    /**
     * @brief 限制实体在地图边界内
     * @param mapWidth 地图宽度
     * @param mapHeight 地图高度
     *
     * 【边界处理策略】
     * 使用 clamp：超出边界时贴在边界上
     * 也可以用：
     * - 反弹：vel = -vel
     * - 环绕：pos = pos % mapSize
     */
    void clampToMap(Fixed mapWidth, Fixed mapHeight) {
        Fixed zero = Fixed::fromInt(0);
        if (pos.x < zero) pos.x = zero;
        if (pos.y < zero) pos.y = zero;
        if (pos.x > mapWidth) pos.x = mapWidth;
        if (pos.y > mapHeight) pos.y = mapHeight;
    }

    /**
     * @brief 圆形碰撞检测
     * @param other 另一个实体
     * @return 是否发生碰撞
     *
     * 【算法原理】
     * 两个圆相交的条件：
     * 圆心距离 < 半径之和
     * 即：|pos1 - pos2| < radius1 + radius2
     *
     * 【优化】
     * 比较距离平方避免开方：
     * distSq < (r1 + r2)²
     *
     * 【复杂度】
     * O(1)，但 N 个实体两两检测是 O(N²)
     * 生产环境需要空间分区优化
     *
     * 【面试考点】
     * Q: 如何优化大量实体的碰撞检测？
     * A: - 四叉树/八叉树：空间分区
     *    - 网格法：将空间分成格子
     *    - 包围盒预检测：先用 AABB 粗检测
     *    - 分离轴定理(SAT)：凸多边形碰撞
     */
    bool collidesWith(const Entity& other) const {
        // 死亡实体不参与碰撞
        if (!alive || !other.alive) return false;

        FixedVec2 diff = pos - other.pos;
        Fixed distSq = diff.lengthSq();  // 距离平方
        Fixed radiusSum = radius + other.radius;
        return distSq < radiusSum * radiusSum;  // 避免开方
    }

    /**
     * @brief 计算实体状态哈希
     * @return 32 位哈希值
     *
     * 【用途】
     * 同步校验：所有客户端计算相同的哈希
     * 如果哈希不一致，说明状态不同步
     *
     * 【哈希算法】
     * 使用简单的乘法哈希：
     * h = h * 31 + value
     * 31 是常用的哈希乘数（质数，有良好的分布性）
     *
     * 【包含字段】
     * - id：实体标识
     * - pos.x, pos.y：位置
     * - vel.x, vel.y：速度
     * - hp：生命值
     * - alive：存活状态
     *
     * 【面试考点】
     * Q: 为什么用 31 作为乘数？
     * A: - 31 是质数，减少哈希冲突
     *    - 31 = 32 - 1 = 2^5 - 1
     *    - 编译器可优化为位移：31 * x = (x << 5) - x
     */
    uint32_t hash() const {
        uint32_t h = id;
        h = h * 31 + static_cast<uint32_t>(pos.x.rawValue());
        h = h * 31 + static_cast<uint32_t>(pos.y.rawValue());
        h = h * 31 + static_cast<uint32_t>(vel.x.rawValue());
        h = h * 31 + static_cast<uint32_t>(vel.y.rawValue());
        h = h * 31 + static_cast<uint32_t>(hp);
        h = h * 31 + (alive ? 1 : 0);
        return h;
    }
};

// ============ 玩家实体 ============
/**
 * @struct Player
 * @brief 玩家实体
 *
 * 【继承设计】
 * 继承自 Entity，添加玩家特有属性：
 * - 分数系统（score, kills, deaths）
 * - 攻击冷却（attackCooldown）
 *
 * 【帧同步注意】
 * 所有逻辑都必须确定性：
 * - 冷却递减是整数运算
 * - 伤害计算是整数运算
 * - 无任何浮点数参与
 */
struct Player : public Entity {
    uint32_t score = 0;    // 分数
    uint32_t kills = 0;    // 击杀数
    uint32_t deaths = 0;   // 死亡数

    // 攻击冷却计数器（帧数）
    int32_t attackCooldown = 0;

    /**
     * 【攻击冷却设计】
     * 10 帧冷却 = 15 FPS 下约 0.67 秒
     *
     * 使用帧数而非时间是关键：
     * - 帧数是整数，确定性
     * - 时间涉及浮点，不确定
     */
    static constexpr int32_t ATTACK_CD = 10;  // 10 帧冷却

    /**
     * @brief 构造函数：初始化玩家属性
     *
     * 【默认属性】
     * - 速度 5.0
     * - 碰撞半径 15.0
     * - 生命值 100
     */
    Player() {
        type = EntityType::PLAYER;
        speed = Fixed::fromFloat(5.0f);
        radius = Fixed::fromFloat(15.0f);
        hp = 100;
        maxHp = 100;
    }

    /**
     * @brief 更新玩家状态
     *
     * 【更新内容】
     * 1. 调用基类 update（位置更新）
     * 2. 更新攻击冷却
     */
    void update() {
        Entity::update();

        // 冷却递减
        if (attackCooldown > 0) {
            attackCooldown--;
        }
    }

    /**
     * @brief 检查是否可以攻击
     * @return 可以攻击返回 true
     *
     * 【条件】
     * 1. 存活
     * 2. 冷却已完成
     */
    bool canAttack() const {
        return alive && attackCooldown <= 0;
    }

    /**
     * @brief 开始攻击（重置冷却）
     *
     * 【调用时机】
     * 成功发起攻击后调用
     */
    void startAttack() {
        attackCooldown = ATTACK_CD;
    }

    /**
     * @brief 受到伤害
     * @param damage 伤害值
     *
     * 【逻辑】
     * 1. 减少 HP
     * 2. HP <= 0 时标记死亡
     * 3. 增加死亡计数
     */
    void takeDamage(int32_t damage) {
        if (!alive) return;

        hp -= damage;
        if (hp <= 0) {
            hp = 0;
            alive = false;
            deaths++;
        }
    }

    /**
     * @brief 增加击杀记录
     *
     * 【调用时机】
     * 当此玩家的攻击击杀了其他玩家
     */
    void addKill() {
        kills++;
        score += 100;  // 每次击杀 +100 分
    }
};

// ============ 子弹实体 ============
/**
 * @struct Bullet
 * @brief 子弹实体
 *
 * 【生命周期设计】
 * 子弹有有限的生存时间（lifeTime）
 * 每帧递减，归零时标记死亡
 *
 * 【帧同步要点】
 * 子弹的创建、移动、销毁都必须确定性：
 * - 创建时机由玩家输入决定
 * - 移动速度是定点数
 * - 生命周期是帧数
 */
struct Bullet : public Entity {
    int32_t damage = 10;       // 伤害值
    int32_t lifeTime = 60;     // 剩余生存帧数

    /**
     * @brief 构造函数：初始化子弹属性
     *
     * 【默认属性】
     * - 速度 10.0（比玩家快）
     * - 碰撞半径 5.0（比玩家小）
     *
     * 【生存时间】
     * 60 帧 ÷ 15 FPS = 4 秒
     */
    Bullet() {
        type = EntityType::BULLET;
        speed = Fixed::fromFloat(10.0f);
        radius = Fixed::fromFloat(5.0f);
    }

    /**
     * @brief 更新子弹状态
     *
     * 【更新内容】
     * 1. 位置更新
     * 2. 生命周期递减
     * 3. 超时则标记死亡
     */
    void update() {
        if (!alive) return;

        Entity::update();  // 移动

        lifeTime--;
        if (lifeTime <= 0) {
            alive = false;  // 超时消失
        }
    }
};

} // namespace lockstep

/**
 * 【扩展阅读】
 *
 * 1. 游戏实体设计模式对比：
 *    | 模式 | 优点 | 缺点 | 适用场景 |
 *    |------|------|------|----------|
 *    | 继承 | 简单直观 | 类爆炸 | 小型游戏（本项目） |
 *    | 组件 | 灵活组合 | 复杂 | 中型游戏 |
 *    | ECS | 高性能 | 学习曲线 | 大型游戏 |
 *
 * 2. 碰撞检测优化技术：
 *    | 技术 | 适用场景 | 复杂度 |
 *    |------|----------|--------|
 *    | 暴力检测 | <100 实体 | O(N²) |
 *    | 网格法 | 均匀分布 | O(N) |
 *    | 四叉树 | 非均匀分布 | O(N log N) |
 *    | BVH | 静态场景 | O(N log N) |
 *
 * 3. 帧同步实体管理要点：
 *    - ID 分配必须确定性（不能用随机数）
 *    - 实体创建顺序必须一致
 *    - 实体销毁时机必须一致
 *    - 遍历顺序必须一致（用有序容器或排序）
 *
 * 4. 生产环境优化：
 *    - 对象池：避免频繁 new/delete
 *    - 内存对齐：提高缓存命中率
 *    - 数据局部性：紧凑存储同类实体
 *    - SIMD：向量化碰撞检测
 *
 * 5. 可扩展的实体类型：
 *    - Enemy：AI 控制的敌人
 *    - Item：可拾取的道具
 *    - Obstacle：静态障碍物
 *    - Trigger：触发区域
 *    - Projectile：导弹等追踪弹
 */
