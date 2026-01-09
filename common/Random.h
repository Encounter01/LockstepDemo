/**
 * Random.h - 确定性随机数生成器
 *
 * 使用线性同余生成器(LCG)确保所有客户端生成相同的随机序列
 * 关键：相同种子 = 相同随机序列
 */
#pragma once
#include <cstdint>
#include "Fixed.h"

class DeterministicRandom {
private:
    uint32_t seed_;

public:
    explicit DeterministicRandom(uint32_t seed = 12345) : seed_(seed) {}

    // 设置种子
    void setSeed(uint32_t seed) {
        seed_ = seed;
    }

    // 获取当前种子
    uint32_t getSeed() const {
        return seed_;
    }

    // 生成下一个随机数
    // 使用经典的LCG参数（来自glibc）
    uint32_t next() {
        seed_ = seed_ * 1103515245 + 12345;
        return seed_;
    }

    // 生成[0, max)范围的整数
    uint32_t nextUInt(uint32_t max) {
        if (max == 0) return 0;
        return next() % max;
    }

    // 生成[min, max]范围的整数
    int range(int min, int max) {
        if (min > max) {
            int temp = min;
            min = max;
            max = temp;
        }
        return min + static_cast<int>(next() % (max - min + 1));
    }

    // 生成[0, 1)范围的定点数
    Fixed nextFixed() {
        // 取低16位作为小数部分
        return Fixed::raw(next() & 0xFFFF);
    }

    // 生成[-1, 1)范围的定点数
    Fixed nextFixedSigned() {
        int32_t raw = static_cast<int32_t>(next() & 0xFFFF) - 0x8000;
        return Fixed::raw(raw * 2);
    }

    // 生成指定范围的定点数
    Fixed rangeFixed(Fixed min, Fixed max) {
        Fixed t = nextFixed();
        return min + (max - min) * t;
    }

    // 随机布尔值
    bool nextBool() {
        return (next() & 1) != 0;
    }

    // 随机方向向量（单位向量）
    FixedVec2 randomDirection() {
        // 使用8个基本方向加上一些变化
        int dir = range(0, 7);
        static const FixedVec2 directions[8] = {
            {Fixed::fromFloat(0.0f),    Fixed::fromFloat(-1.0f)},   // 上
            {Fixed::fromFloat(0.707f),  Fixed::fromFloat(-0.707f)}, // 右上
            {Fixed::fromFloat(1.0f),    Fixed::fromFloat(0.0f)},    // 右
            {Fixed::fromFloat(0.707f),  Fixed::fromFloat(0.707f)},  // 右下
            {Fixed::fromFloat(0.0f),    Fixed::fromFloat(1.0f)},    // 下
            {Fixed::fromFloat(-0.707f), Fixed::fromFloat(0.707f)},  // 左下
            {Fixed::fromFloat(-1.0f),   Fixed::fromFloat(0.0f)},    // 左
            {Fixed::fromFloat(-0.707f), Fixed::fromFloat(-0.707f)}, // 左上
        };
        return directions[dir];
    }

    // 在矩形范围内随机点
    FixedVec2 randomInRect(Fixed minX, Fixed minY, Fixed maxX, Fixed maxY) {
        return {
            rangeFixed(minX, maxX),
            rangeFixed(minY, maxY)
        };
    }
};
