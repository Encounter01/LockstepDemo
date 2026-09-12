/**
 * @file Random.h
 * @brief 确定性随机数生成器 - 帧同步的随机性保障
 *
 * 【技术概述】
 * 在帧同步游戏中，随机事件（如暴击、掉落、AI 行为）必须在所有客户端
 * 产生相同的结果。本文件实现了基于 LCG（Linear Congruential Generator，
 * 线性同余生成器）的确定性随机数生成器。
 *
 * 核心原理：相同的种子 + 相同的调用序列 = 相同的随机数序列
 *
 * 【LCG 算法公式】
 * X_{n+1} = (a × X_n + c) mod m
 * 其中：
 * - a = 1103515245（乘法因子）
 * - c = 12345（增量）
 * - m = 2^32（模数，利用 uint32_t 溢出自动取模）
 *
 * 【面试要点】
 * 1. Q: 为什么不能使用系统的 rand() 或 std::random？
 *    A: 系统随机数库的实现：
 *       - 不同平台实现不同（Windows vs Linux）
 *       - 同平台不同版本可能不同
 *       - 无法保证确定性
 *       帧同步必须自己实现随机数生成器
 *
 * 2. Q: LCG 算法的优缺点是什么？
 *    A: 优点：
 *       - 简单高效，只需一次乘法和加法
 *       - 完全确定性
 *       - 状态小（只需保存 seed）
 *       缺点：
 *       - 低位周期较短，随机性差
 *       - 相邻数字有相关性
 *       - 不适合密码学用途
 *       对于游戏场景，LCG 足够使用
 *
 * 3. Q: 如何保证所有客户端的随机数同步？
 *    A: 三个关键点：
 *       1. 相同的初始种子（服务器分发）
 *       2. 相同的调用顺序（帧同步保证）
 *       3. 相同的算法实现（本文件）
 *
 * 4. Q: 游戏中有哪些地方需要确定性随机？
 *    A: 暴击判定、伤害浮动、技能效果、AI 决策、
 *       掉落物品、天气变化、地图生成等
 *
 * 【生产实践】
 * 1. 种子管理：服务器生成种子，开局同步给所有客户端
 * 2. 调用纪律：确保随机数调用顺序完全一致
 * 3. 调试支持：记录每帧的种子值，便于复现问题
 * 4. 隔离使用：游戏逻辑和 UI 特效应使用不同的随机数实例
 */
#pragma once
#include <cstdint>
#include "Fixed.h"

/**
 * @class DeterministicRandom
 * @brief 确定性随机数生成器
 *
 * 【设计理念】
 * 1. 状态最小化：只保存一个 uint32_t 种子
 * 2. 接口丰富：提供整数、定点数、布尔、方向等多种随机方法
 * 3. 确定性保证：相同种子产生相同序列
 *
 * 【使用示例】
 * DeterministicRandom rng(12345);  // 初始种子
 * int damage = rng.range(90, 110);  // 90-110 的随机伤害
 * bool isCrit = rng.nextBool();     // 50% 暴击率
 *
 * 【重要警告】
 * 随机数调用必须严格按顺序！
 * 错误示例：
 *   if (debug) { rng.next(); }  // 调试代码改变了调用序列！
 */
class DeterministicRandom {
private:
    /**
     * 【随机数状态】
     * 只需保存一个 32 位种子
     * 种子通过 LCG 公式迭代更新
     *
     * 【周期性】
     * LCG 的周期最大为 m（这里是 2^32）
     * 使用 glibc 参数可达到最大周期
     */
    uint32_t seed_;

public:
    /**
     * @brief 构造函数
     * @param seed 初始种子，默认 12345
     *
     * 【种子选择】
     * - 开发阶段：使用固定种子便于调试复现
     * - 生产环境：服务器生成随机种子，同步给所有客户端
     *
     * 【种子分发流程】
     * 1. 服务器开局时生成种子（可用时间戳等）
     * 2. 在 GAME_START 消息中广播种子
     * 3. 所有客户端使用相同种子初始化
     */
    explicit DeterministicRandom(uint32_t seed = 12345) : seed_(seed) {}

    /**
     * @brief 设置种子
     * @param seed 新的种子值
     *
     * 【使用场景】
     * 1. 开局时设置服务器分发的种子
     * 2. 重置随机数状态（如加载存档）
     *
     * 【注意】
     * 设置种子会重置随机数序列，需谨慎使用
     */
    void setSeed(uint32_t seed) {
        seed_ = seed;
    }

    /**
     * @brief 获取当前种子
     * @return 当前种子值
     *
     * 【使用场景】
     * 1. 调试：记录每帧种子便于复现
     * 2. 存档：保存种子以恢复随机状态
     * 3. 校验：对比各客户端种子是否一致
     */
    uint32_t getSeed() const {
        return seed_;
    }

    /**
     * @brief 生成下一个随机数（核心方法）
     * @return 32 位无符号随机数
     *
     * 【LCG 算法详解】
     * 公式：seed = seed × 1103515245 + 12345
     *
     * 这组参数来自 glibc（GNU C Library），被广泛使用：
     * - a = 1103515245
     * - c = 12345
     * - m = 2^32（利用 uint32_t 溢出）
     *
     * 【为什么选择这组参数】
     * 1. 经过数学验证，可达到 2^32 的满周期
     * 2. 通过了基本的随机性统计测试
     * 3. 计算简单高效
     *
     * 【面试考点】
     * Q: LCG 参数的选择有什么要求？
     * A: 要达到满周期，需满足 Hull-Dobell 定理：
     *    1. c 和 m 互质
     *    2. a-1 能被 m 的所有质因子整除
     *    3. 如果 m 是 4 的倍数，a-1 也是 4 的倍数
     *
     * 【溢出处理】
     * uint32_t 溢出时自动对 2^32 取模
     * 这是 C++ 对无符号整数的保证（有符号溢出是未定义行为）
     */
    uint32_t next() {
        seed_ = seed_ * 1103515245 + 12345;
        return seed_;
    }

    /**
     * @brief 生成 [0, max) 范围的整数
     * @param max 上界（不包含）
     * @return [0, max) 范围的随机整数
     *
     * 【实现说明】
     * 使用取模运算限制范围
     *
     * 【偏差问题】
     * 当 max 不能整除 2^32 时，低值会略微偏多
     * 例如：max=3 时，0,1,2 的概率分别约为 33.33%
     * 但实际上 0 会多出 (2^32 mod 3) / 2^32 的概率
     *
     * 对于游戏场景，这种偏差可以忽略
     * 如需更均匀的分布，可使用"拒绝采样"
     */
    uint32_t nextUInt(uint32_t max) {
        if (max == 0) return 0;
        return next() % max;
    }

    /**
     * @brief 生成 [min, max] 范围的整数（包含两端）
     * @param min 下界
     * @param max 上界
     * @return [min, max] 范围的随机整数
     *
     * 【参数校验】
     * 如果 min > max，自动交换
     * 这是防御性编程，避免产生错误结果
     *
     * 【使用示例】
     * int damage = rng.range(90, 110);  // 伤害在 90-110 之间
     * int dropCount = rng.range(1, 3);   // 掉落 1-3 个物品
     */
    int range(int min, int max) {
        if (min > max) {
            int temp = min;
            min = max;
            max = temp;
        }
        return min + static_cast<int>(next() % (max - min + 1));
    }

    /**
     * @brief 生成 [0, 1) 范围的定点数
     * @return [0, 1) 范围的随机定点数
     *
     * 【实现原理】
     * 取随机数的低 16 位作为定点数的小数部分
     * 0x0000 ~ 0xFFFF 映射到 0.0 ~ 0.99998...
     *
     * 【为什么取低 16 位】
     * LCG 的高位比低位更随机，但这里我们只需要简单的随机小数
     * 取低 16 位刚好对应定点数的小数精度
     *
     * 【使用示例】
     * Fixed t = rng.nextFixed();  // 用于插值
     * Fixed result = a + (b - a) * t;  // a 到 b 之间的随机值
     */
    Fixed nextFixed() {
        // 取低16位作为小数部分
        return Fixed::raw(next() & 0xFFFF);
    }

    /**
     * @brief 生成 [-1, 1) 范围的定点数
     * @return [-1, 1) 范围的随机定点数
     *
     * 【实现原理】
     * 1. 取低 16 位：0x0000 ~ 0xFFFF
     * 2. 减去 0x8000：得到 -32768 ~ 32767
     * 3. 乘以 2：得到 -65536 ~ 65534，即 [-1, 1)
     *
     * 【使用场景】
     * 生成随机扰动、随机偏移等需要正负值的场景
     */
    Fixed nextFixedSigned() {
        int32_t raw = static_cast<int32_t>(next() & 0xFFFF) - 0x8000;
        return Fixed::raw(raw * 2);
    }

    /**
     * @brief 生成指定范围的定点数
     * @param min 下界
     * @param max 上界
     * @return [min, max) 范围的随机定点数
     *
     * 【实现原理】
     * 使用线性插值：result = min + (max - min) × t
     * 其中 t 是 [0, 1) 的随机数
     *
     * 【使用示例】
     * Fixed speed = rng.rangeFixed(
     *     Fixed::fromFloat(0.8f),
     *     Fixed::fromFloat(1.2f)
     * );  // 0.8 到 1.2 之间的随机速度
     */
    Fixed rangeFixed(Fixed min, Fixed max) {
        Fixed t = nextFixed();
        return min + (max - min) * t;
    }

    /**
     * @brief 随机布尔值
     * @return true 或 false（50% 概率）
     *
     * 【实现原理】
     * 取随机数的最低位
     *
     * 【使用场景】
     * 50% 概率判定：暴击、闪避等
     *
     * 【扩展】
     * 如需其他概率，使用：
     * bool happened = (rng.nextUInt(100) < 30);  // 30% 概率
     */
    bool nextBool() {
        return (next() & 1) != 0;
    }

    /**
     * @brief 随机方向向量（8 个基本方向之一）
     * @return 单位向量，指向 8 个方向之一
     *
     * 【设计说明】
     * 使用预定义的 8 方向表，避免运行时的三角函数计算
     * 这保证了：
     * 1. 确定性（不依赖浮点三角函数）
     * 2. 高效（查表比计算快）
     *
     * 【8 方向分布】
     *     7   0   1
     *      \  |  /
     *   6 ←  ·  → 2
     *      /  |  \
     *     5   4   3
     *
     * 【向量值说明】
     * - 正交方向（上下左右）：长度为 1
     * - 对角方向：使用 0.707 ≈ 1/√2，保持单位长度
     *
     * 【注意】
     * fromFloat 在此处是安全的，因为这些是编译时常量
     * 所有客户端会得到完全相同的预计算值
     *
     * 【面试考点】
     * Q: 为什么对角方向用 0.707 而不是 1？
     * A: 保持向量长度为 1（单位向量）
     *    如果对角用 (1,1)，长度会是 √2 ≈ 1.414
     *    这会导致对角移动速度比正交快 41%
     */
    FixedVec2 randomDirection() {
        // 使用8个基本方向加上一些变化
        int dir = range(0, 7);
        static const FixedVec2 directions[8] = {
            {Fixed::fromFloat(0.0f),    Fixed::fromFloat(-1.0f)},   // 上 (0)
            {Fixed::fromFloat(0.707f),  Fixed::fromFloat(-0.707f)}, // 右上 (1)
            {Fixed::fromFloat(1.0f),    Fixed::fromFloat(0.0f)},    // 右 (2)
            {Fixed::fromFloat(0.707f),  Fixed::fromFloat(0.707f)},  // 右下 (3)
            {Fixed::fromFloat(0.0f),    Fixed::fromFloat(1.0f)},    // 下 (4)
            {Fixed::fromFloat(-0.707f), Fixed::fromFloat(0.707f)},  // 左下 (5)
            {Fixed::fromFloat(-1.0f),   Fixed::fromFloat(0.0f)},    // 左 (6)
            {Fixed::fromFloat(-0.707f), Fixed::fromFloat(-0.707f)}, // 左上 (7)
        };
        return directions[dir];
    }

    /**
     * @brief 在矩形范围内生成随机点
     * @param minX 左边界
     * @param minY 上边界
     * @param maxX 右边界
     * @param maxY 下边界
     * @return 矩形范围内的随机点
     *
     * 【使用场景】
     * 1. 随机刷怪点
     * 2. 随机掉落位置
     * 3. 随机粒子位置
     *
     * 【坐标系假设】
     * 本项目使用屏幕坐标系：Y 轴向下
     * minY 是上边界，maxY 是下边界
     */
    FixedVec2 randomInRect(Fixed minX, Fixed minY, Fixed maxX, Fixed maxY) {
        return {
            rangeFixed(minX, maxX),
            rangeFixed(minY, maxY)
        };
    }
};

/**
 * 【扩展阅读】
 *
 * 1. 其他常用的确定性随机数算法：
 *    | 算法 | 状态大小 | 周期 | 特点 |
 *    |------|----------|------|------|
 *    | LCG | 4 字节 | 2^32 | 简单快速，本项目使用 |
 *    | Xorshift | 4-16 字节 | 2^128-1 | 更好的随机性 |
 *    | PCG | 16 字节 | 2^64 | 现代推荐 |
 *    | Mersenne Twister | 2.5KB | 2^19937-1 | 高质量但状态大 |
 *
 * 2. 不同场景的随机数需求：
 *    - 游戏逻辑：确定性随机（本类）
 *    - 密码学：加密安全随机（如 /dev/urandom）
 *    - 模拟测试：高质量随机（如 MT19937）
 *
 * 3. 帧同步中的随机数陷阱：
 *    - 陷阱1：UI 特效调用了游戏随机数
 *      解决：UI 和逻辑使用不同的随机数实例
 *    - 陷阱2：条件执行改变了调用次数
 *      解决：确保所有分支的随机调用次数一致
 *    - 陷阱3：遍历顺序不确定
 *      解决：排序后再遍历
 *
 * 4. 调试技巧：
 *    - 记录每帧开始时的种子
 *    - 检测种子不一致时立即报警
 *    - 保存随机数序列用于回放
 *
 * 5. 商业游戏的做法：
 *    - 王者荣耀：服务器分发种子，客户端使用确定性随机
 *    - 守望先锋：服务器端计算所有随机事件
 *    - Dota 2：混合方案，关键随机服务器验证
 */
