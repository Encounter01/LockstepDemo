/**
 * Fixed.h - Q16.16定点数库
 *
 * 定点数是帧同步的核心，确保跨平台计算结果一致
 * 使用16位整数部分 + 16位小数部分
 */
#pragma once
#include <cstdint>
#include <iostream>

class Fixed {
public:
    static constexpr int FRAC_BITS = 16;
    static constexpr int32_t ONE = 1 << FRAC_BITS;      // 65536
    static constexpr int32_t HALF = ONE >> 1;            // 32768

private:
    int32_t value_;

public:
    // 默认构造函数
    Fixed() : value_(0) {}

    // 原始值构造（私有使用）
    explicit Fixed(int32_t raw, bool) : value_(raw) {}

    // ============ 工厂方法 ============
    static Fixed fromInt(int i) {
        return Fixed(i << FRAC_BITS, true);
    }

    static Fixed fromFloat(float f) {
        return Fixed(static_cast<int32_t>(f * ONE), true);
    }

    static Fixed fromDouble(double d) {
        return Fixed(static_cast<int32_t>(d * ONE), true);
    }

    static Fixed raw(int32_t v) {
        return Fixed(v, true);
    }

    // ============ 转换方法 ============
    int toInt() const {
        return value_ >> FRAC_BITS;
    }

    float toFloat() const {
        return static_cast<float>(value_) / ONE;
    }

    double toDouble() const {
        return static_cast<double>(value_) / ONE;
    }

    int32_t rawValue() const {
        return value_;
    }

    // ============ 四则运算 ============
    Fixed operator+(Fixed other) const {
        return raw(value_ + other.value_);
    }

    Fixed operator-(Fixed other) const {
        return raw(value_ - other.value_);
    }

    Fixed operator-() const {
        return raw(-value_);
    }

    Fixed operator*(Fixed other) const {
        // 使用64位中间结果防止溢出
        int64_t result = static_cast<int64_t>(value_) * other.value_;
        return raw(static_cast<int32_t>(result >> FRAC_BITS));
    }

    Fixed operator/(Fixed other) const {
        if (other.value_ == 0) {
            return raw(value_ >= 0 ? INT32_MAX : INT32_MIN);
        }
        int64_t result = (static_cast<int64_t>(value_) << FRAC_BITS) / other.value_;
        return raw(static_cast<int32_t>(result));
    }

    // ============ 复合赋值 ============
    Fixed& operator+=(Fixed other) { value_ += other.value_; return *this; }
    Fixed& operator-=(Fixed other) { value_ -= other.value_; return *this; }
    Fixed& operator*=(Fixed other) { *this = *this * other; return *this; }
    Fixed& operator/=(Fixed other) { *this = *this / other; return *this; }

    // ============ 比较运算符 ============
    bool operator<(Fixed other) const { return value_ < other.value_; }
    bool operator>(Fixed other) const { return value_ > other.value_; }
    bool operator<=(Fixed other) const { return value_ <= other.value_; }
    bool operator>=(Fixed other) const { return value_ >= other.value_; }
    bool operator==(Fixed other) const { return value_ == other.value_; }
    bool operator!=(Fixed other) const { return value_ != other.value_; }

    // ============ 数学函数 ============
    Fixed abs() const {
        return raw(value_ >= 0 ? value_ : -value_);
    }

    // 牛顿迭代法求平方根（确定性）
    static Fixed sqrt(Fixed x) {
        if (x.value_ <= 0) return raw(0);

        // 使用整数平方根算法
        uint32_t val = static_cast<uint32_t>(x.value_);
        uint32_t result = 0;
        uint32_t bit = 1u << 30;

        while (bit > val) bit >>= 2;

        while (bit != 0) {
            if (val >= result + bit) {
                val -= result + bit;
                result = (result >> 1) + bit;
            } else {
                result >>= 1;
            }
            bit >>= 2;
        }

        // 调整精度
        return raw(result << (FRAC_BITS / 2));
    }

    // 最小值/最大值
    static Fixed min(Fixed a, Fixed b) {
        return a.value_ < b.value_ ? a : b;
    }

    static Fixed max(Fixed a, Fixed b) {
        return a.value_ > b.value_ ? a : b;
    }

    // 限制范围
    Fixed clamp(Fixed minVal, Fixed maxVal) const {
        if (value_ < minVal.value_) return minVal;
        if (value_ > maxVal.value_) return maxVal;
        return *this;
    }

    // 输出流支持
    friend std::ostream& operator<<(std::ostream& os, Fixed f) {
        os << f.toFloat();
        return os;
    }
};

// ============ 定点数二维向量 ============
struct FixedVec2 {
    Fixed x, y;

    FixedVec2() = default;
    FixedVec2(Fixed x_, Fixed y_) : x(x_), y(y_) {}

    // 向量运算
    FixedVec2 operator+(FixedVec2 other) const {
        return {x + other.x, y + other.y};
    }

    FixedVec2 operator-(FixedVec2 other) const {
        return {x - other.x, y - other.y};
    }

    FixedVec2 operator*(Fixed s) const {
        return {x * s, y * s};
    }

    FixedVec2 operator/(Fixed s) const {
        return {x / s, y / s};
    }

    FixedVec2& operator+=(FixedVec2 other) {
        x += other.x;
        y += other.y;
        return *this;
    }

    FixedVec2& operator-=(FixedVec2 other) {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    // 点积
    Fixed dot(FixedVec2 other) const {
        return x * other.x + y * other.y;
    }

    // 叉积（返回标量）
    Fixed cross(FixedVec2 other) const {
        return x * other.y - y * other.x;
    }

    // 长度平方
    Fixed lengthSq() const {
        return dot(*this);
    }

    // 长度
    Fixed length() const {
        return Fixed::sqrt(lengthSq());
    }

    // 归一化
    FixedVec2 normalize() const {
        Fixed len = length();
        if (len.rawValue() == 0) {
            return {Fixed::fromInt(0), Fixed::fromInt(0)};
        }
        return {x / len, y / len};
    }

    // 距离
    Fixed distanceTo(FixedVec2 other) const {
        return (*this - other).length();
    }

    // 距离平方
    Fixed distanceSqTo(FixedVec2 other) const {
        return (*this - other).lengthSq();
    }

    // 零向量
    static FixedVec2 zero() {
        return {Fixed::fromInt(0), Fixed::fromInt(0)};
    }

    // 输出
    friend std::ostream& operator<<(std::ostream& os, FixedVec2 v) {
        os << "(" << v.x << ", " << v.y << ")";
        return os;
    }
};
