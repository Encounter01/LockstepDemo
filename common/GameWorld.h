/**
 * GameWorld.h - 游戏世界
 *
 * 管理所有游戏实体，执行游戏逻辑
 * 核心：确保确定性！相同输入必须产生相同结果
 */
#pragma once
#include <vector>
#include <algorithm>
#include <iostream>
#include "Entity.h"
#include "Random.h"
#include "Protocol.h"

namespace lockstep {

class GameWorld {
public:
    // 地图大小
    static constexpr int MAP_WIDTH = 800;
    static constexpr int MAP_HEIGHT = 600;

    // 当前帧号
    uint32_t currentFrame = 0;

    // 游戏实体
    std::vector<Player> players;
    std::vector<Bullet> bullets;

    // 确定性随机数生成器
    DeterministicRandom random;

    // 下一个实体ID
    uint32_t nextEntityId = 1000;

    // ============ 初始化 ============
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
            player.id = i;
            player.ownerId = i;

            // 随机出生位置（避免边缘）
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
    void tick(const FrameData& frame) {
        // 1. 按playerId排序输入（确保确定性）
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

        currentFrame++;
    }

    // ============ 应用玩家输入 ============
    void applyPlayerInput(Player& player, const PlayerInput& input) {
        if (!player.alive) return;

        // 移动
        player.applyInput(input);

        // 攻击
        if ((input.actions & 0x01) && player.canAttack()) {
            spawnBullet(player, input);
            player.startAttack();
        }
    }

    // ============ 生成子弹 ============
    void spawnBullet(const Player& player, const PlayerInput& input) {
        Bullet bullet;
        bullet.id = nextEntityId++;
        bullet.ownerId = player.ownerId;
        bullet.pos = player.pos;

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
                // 不能打自己
                if (bullet.ownerId == player.ownerId) continue;
                if (!player.alive) continue;

                if (bullet.collidesWith(player)) {
                    // 命中
                    player.takeDamage(bullet.damage);
                    bullet.alive = false;

                    // 如果击杀
                    if (!player.alive) {
                        for (auto& p : players) {
                            if (p.ownerId == bullet.ownerId) {
                                p.addKill();
                                break;
                            }
                        }
                    }
                    break;
                }
            }
        }
    }

    // ============ 解决玩家碰撞 ============
    void resolvePlayerCollision(Player& a, Player& b) {
        FixedVec2 diff = a.pos - b.pos;
        Fixed dist = diff.length();

        if (dist.rawValue() == 0) {
            // 完全重叠，给个随机方向
            diff = {Fixed::fromFloat(1.0f), Fixed::fromFloat(0.0f)};
            dist = Fixed::fromFloat(1.0f);
        }

        FixedVec2 normal = diff / dist;
        Fixed overlap = (a.radius + b.radius) - dist;

        if (overlap > Fixed::fromInt(0)) {
            // 各退一半
            FixedVec2 push = normal * (overlap / Fixed::fromInt(2));
            a.pos = a.pos + push;
            b.pos = b.pos - push;
        }
    }

    // ============ 清理死亡实体 ============
    void cleanupDeadEntities() {
        bullets.erase(
            std::remove_if(bullets.begin(), bullets.end(),
                [](const Bullet& b) { return !b.alive; }),
            bullets.end()
        );
    }

    // ============ 计算世界状态校验和 ============
    uint32_t calcChecksum() const {
        uint32_t hash = currentFrame;

        // 按ID排序计算哈希（确保确定性）
        for (const auto& player : players) {
            hash = hash * 31 + player.hash();
        }

        for (const auto& bullet : bullets) {
            hash = hash * 31 + bullet.hash();
        }

        return hash;
    }

    // ============ 获取存活玩家数 ============
    int getAlivePlayerCount() const {
        int count = 0;
        for (const auto& p : players) {
            if (p.alive) count++;
        }
        return count;
    }

    // ============ 检查游戏是否结束 ============
    bool isGameOver() const {
        return getAlivePlayerCount() <= 1 && players.size() > 1;
    }

    // ============ 获取获胜者 ============
    int getWinnerId() const {
        for (const auto& p : players) {
            if (p.alive) return static_cast<int>(p.ownerId);
        }
        return -1;
    }

    // ============ 调试：打印世界状态 ============
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
