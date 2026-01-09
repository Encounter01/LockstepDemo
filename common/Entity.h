/**
 * Entity.h - 游戏实体定义
 *
 * 使用定点数确保所有客户端计算结果一致
 */
#pragma once
#include "Fixed.h"
#include "Protocol.h"
#include <cstdint>

namespace lockstep {

// 8方向向量表（确定性！使用预计算的定点数）
inline const FixedVec2 DIR_TABLE[9] = {
    {Fixed::fromFloat(0.0f),    Fixed::fromFloat(-1.0f)},   // 0: 上
    {Fixed::fromFloat(0.707f),  Fixed::fromFloat(-0.707f)}, // 1: 右上
    {Fixed::fromFloat(1.0f),    Fixed::fromFloat(0.0f)},    // 2: 右
    {Fixed::fromFloat(0.707f),  Fixed::fromFloat(0.707f)},  // 3: 右下
    {Fixed::fromFloat(0.0f),    Fixed::fromFloat(1.0f)},    // 4: 下
    {Fixed::fromFloat(-0.707f), Fixed::fromFloat(0.707f)},  // 5: 左下
    {Fixed::fromFloat(-1.0f),   Fixed::fromFloat(0.0f)},    // 6: 左
    {Fixed::fromFloat(-0.707f), Fixed::fromFloat(-0.707f)}, // 7: 左上
    {Fixed::fromFloat(0.0f),    Fixed::fromFloat(0.0f)},    // 8: 静止
};

// ============ 实体类型 ============
enum class EntityType : uint8_t {
    PLAYER = 0,
    BULLET = 1,
    ITEM   = 2,
};

// ============ 基础实体 ============
struct Entity {
    uint32_t id = 0;
    uint32_t ownerId = 0;  // 所属玩家ID
    EntityType type = EntityType::PLAYER;

    FixedVec2 pos;      // 位置
    FixedVec2 vel;      // 速度
    Fixed speed;         // 移动速度
    Fixed radius;        // 碰撞半径

    int32_t hp = 100;
    int32_t maxHp = 100;
    bool alive = true;

    // 应用玩家输入
    void applyInput(const PlayerInput& input) {
        if (!alive) return;

        // 处理移动
        if (input.moveDir < 8) {
            vel = DIR_TABLE[input.moveDir] * speed;
        } else {
            vel = FixedVec2::zero();
        }
    }

    // 更新位置
    void update() {
        if (!alive) return;

        // 移动
        pos = pos + vel;
    }

    // 边界检测
    void clampToMap(Fixed mapWidth, Fixed mapHeight) {
        Fixed zero = Fixed::fromInt(0);
        if (pos.x < zero) pos.x = zero;
        if (pos.y < zero) pos.y = zero;
        if (pos.x > mapWidth) pos.x = mapWidth;
        if (pos.y > mapHeight) pos.y = mapHeight;
    }

    // 碰撞检测（圆形）
    bool collidesWith(const Entity& other) const {
        if (!alive || !other.alive) return false;

        FixedVec2 diff = pos - other.pos;
        Fixed distSq = diff.lengthSq();
        Fixed radiusSum = radius + other.radius;
        return distSq < radiusSum * radiusSum;
    }

    // 计算状态哈希（用于同步校验）
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
struct Player : public Entity {
    uint32_t score = 0;
    uint32_t kills = 0;
    uint32_t deaths = 0;

    // 攻击冷却
    int32_t attackCooldown = 0;
    static constexpr int32_t ATTACK_CD = 10;  // 10帧冷却

    Player() {
        type = EntityType::PLAYER;
        speed = Fixed::fromFloat(5.0f);
        radius = Fixed::fromFloat(15.0f);
        hp = 100;
        maxHp = 100;
    }

    void update() {
        Entity::update();

        // 更新冷却
        if (attackCooldown > 0) {
            attackCooldown--;
        }
    }

    bool canAttack() const {
        return alive && attackCooldown <= 0;
    }

    void startAttack() {
        attackCooldown = ATTACK_CD;
    }

    void takeDamage(int32_t damage) {
        if (!alive) return;

        hp -= damage;
        if (hp <= 0) {
            hp = 0;
            alive = false;
            deaths++;
        }
    }

    void addKill() {
        kills++;
        score += 100;
    }
};

// ============ 子弹实体 ============
struct Bullet : public Entity {
    int32_t damage = 10;
    int32_t lifeTime = 60;  // 60帧后消失

    Bullet() {
        type = EntityType::BULLET;
        speed = Fixed::fromFloat(10.0f);
        radius = Fixed::fromFloat(5.0f);
    }

    void update() {
        if (!alive) return;

        Entity::update();

        lifeTime--;
        if (lifeTime <= 0) {
            alive = false;
        }
    }
};

} // namespace lockstep
