/**
 * @file Fixed.h
 * @brief Q16.16 定点数库 - 帧同步游戏的确定性计算基础
 *
 * 【技术概述】
 * 定点数（Fixed-Point Number）是帧同步游戏的核心技术，通过用整数运算
 * 模拟小数运算，确保在不同平台（x86、ARM、不同编译器）上的计算结果
 * 完全一致。这是帧同步能够工作的数学基础。
 *
 * Q16.16 格式说明：
 * - 总共 32 位 (int32_t)
 * - 高 16 位：整数部分（含符号位），范围 -32768 ~ 32767
 * - 低 16 位：小数部分，精度 1/65536 ≈ 0.0000153
 *
 * 二进制表示示例：
 * 数值 2.5 = 0x00028000
 *   整数部分: 0x0002 = 2
 *   小数部分: 0x8000 = 32768 = 0.5 × 65536
 *
 * 【面试要点】
 * 1. Q: 为什么帧同步必须使用定点数而不是浮点数？
 *    A: IEEE 754 浮点数在不同硬件/编译器上存在以下不确定性：
 *       - x87 FPU 使用 80 位扩展精度，结果与 64 位 SSE 不同
 *       - ARM 的浮点实现与 x86 有微小差异
 *       - 编译器优化可能改变运算顺序，影响浮点精度
 *       - 同样的代码，Debug 和 Release 模式结果可能不同
 *       这些微小差异在帧同步中会累积，导致"蝴蝶效应"式的状态分歧
 *
 * 2. Q: Q16.16 和 Q8.24 有什么区别？如何选择？
 *    A: - Q16.16：整数范围大（±32767），精度一般，适合游戏坐标
 *       - Q8.24：整数范围小（±127），精度高，适合需要高精度的场景
 *       选择取决于业务需求，游戏通常用 Q16.16
 *
 * 3. Q: 定点数乘法为什么需要 64 位中间结果？
 *    A: 两个 32 位数相乘会产生 64 位结果。如果直接用 32 位会溢出。
 *       例如：1.5 × 1.5 = 2.25
 *       定点表示：0x18000 × 0x18000 = 0x240000000（超过 32 位）
 *       必须用 int64_t 存储中间结果，然后右移 16 位
 *
 * 4. Q: 为什么不使用编译器的 /fp:strict 选项？
 *    A: 即使使用严格浮点模式，也无法保证跨平台一致性。而且：
 *       - 不同编译器的实现不同
 *       - 第三方库可能不遵守
 *       - 性能损失较大
 *       定点数从根本上消除了浮点不确定性
 *
 * 【生产实践】
 * 1. 范围检测：生产代码应添加溢出检测，溢出时报警或限制
 * 2. 性能优化：乘法是最频繁的操作，可考虑 SIMD 优化
 * 3. 调试支持：添加与浮点数的转换方法，便于调试
 * 4. 行业标准：商业引擎如 Unity DOTS 也提供定点数库
 */
#pragma once
#include <cstdint>
#include <iostream>

/**
 * @class Fixed
 * @brief Q16.16 定点数类
 *
 * 【设计理念】
 * 1. 值语义：像 int/float 一样使用，支持拷贝、赋值
 * 2. 运算符重载：支持自然的数学表达式
 * 3. 工厂方法：避免构造函数歧义
 * 4. 内存布局：仅包含一个 int32_t，与 int 大小相同
 *
 * 【内存占用】
 * sizeof(Fixed) = 4 字节，与 float 相同
 */
class Fixed {
public:
    /**
     * 【常量定义】
     * FRAC_BITS: 小数位数，决定了精度
     * ONE: 定点数中的 1.0，用于转换计算
     * HALF: 定点数中的 0.5，用于四舍五入
     *
     * 【数值表示】
     * ONE = 65536 = 2^16
     * 精度 = 1/65536 ≈ 0.0000153
     * 整数范围 = -32768 ~ 32767
     */
    static constexpr int FRAC_BITS = 16;
    static constexpr int32_t ONE = 1 << FRAC_BITS;      // 65536
    static constexpr int32_t HALF = ONE >> 1;            // 32768

private:
    /**
     * 【内部存储】
     * 使用 int32_t 存储定点数值
     * - 有符号，支持负数
     * - 32 位，足够表示游戏中的坐标和数值
     */
    int32_t value_;

public:
    /**
     * @brief 默认构造函数，初始化为 0
     *
     * 【注意】这会初始化为 0.0，不是未定义值
     */
    Fixed() : value_(0) {}

    /**
     * @brief 原始值构造（私有使用）
     * @param raw 原始整数值
     * @param 占位参数，区分重载
     *
     * 【设计说明】
     * 使用 bool 占位参数来区分"原始值构造"和"整数值构造"
     * 这是一种避免构造函数歧义的技巧
     *
     * 【替代方案】
     * 也可以使用 tag dispatch：
     * struct RawTag {};
     * Fixed(int32_t raw, RawTag) : value_(raw) {}
     */
    explicit Fixed(int32_t raw, bool) : value_(raw) {}

    // ============ 工厂方法 ============
    /**
     * 【工厂方法设计】
     * 使用静态工厂方法而不是重载构造函数，原因：
     * 1. 命名清晰，表达意图（fromInt vs fromFloat）
     * 2. 避免隐式转换带来的歧义
     * 3. 可以返回错误或特殊值
     *
     * 【面试考点】
     * Q: 工厂方法相比构造函数有什么优势？
     * A: 1. 有名字，可读性更好
     *    2. 不一定要创建新对象（可以返回缓存对象）
     *    3. 可以返回派生类对象
     *    4. 可以失败（返回 nullptr 或 std::optional）
     */

    /**
     * @brief 从整数创建定点数
     * @param i 整数值
     * @return 对应的定点数
     *
     * 【实现原理】
     * 整数左移 FRAC_BITS 位 = 乘以 65536
     * 例如：fromInt(2) = 2 << 16 = 0x20000 = 131072
     *
     * 【溢出风险】
     * 如果 |i| > 32767，会发生溢出
     * 生产代码应添加范围检查
     */
    static Fixed fromInt(int i) {
        return Fixed(i << FRAC_BITS, true);
    }

    /**
     * @brief 从浮点数创建定点数（仅用于初始化）
     * @param f 浮点数值
     * @return 对应的定点数
     *
     * 【重要警告】
     * 此方法仅用于常量初始化，如：
     * Fixed speed = Fixed::fromFloat(1.5f);
     *
     * 【禁止在运行时使用】
     * 运行时使用浮点数会破坏确定性！
     * 应该在编译时预计算所有需要的定点数常量
     *
     * 【实现原理】
     * 浮点数乘以 ONE，然后截断为整数
     * 例如：fromFloat(2.5f) = (int)(2.5 × 65536) = 163840
     */
    static Fixed fromFloat(float f) {
        return Fixed(static_cast<int32_t>(f * ONE), true);
    }

    /**
     * @brief 从双精度浮点数创建定点数（仅用于初始化）
     * @param d 双精度浮点数值
     * @return 对应的定点数
     *
     * 【使用场景】
     * 与 fromFloat 类似，但精度更高
     * 适合需要更精确初始化的场景
     */
    static Fixed fromDouble(double d) {
        return Fixed(static_cast<int32_t>(d * ONE), true);
    }

    /**
     * @brief 从原始值创建定点数
     * @param v 原始整数值（已经是定点格式）
     * @return 对应的定点数
     *
     * 【使用场景】
     * 1. 运算符实现中创建结果
     * 2. 网络反序列化后恢复定点数
     * 3. 从存储中加载定点数
     */
    static Fixed raw(int32_t v) {
        return Fixed(v, true);
    }

    // ============ 转换方法 ============
    /**
     * @brief 转换为整数（截断小数部分）
     * @return 整数部分
     *
     * 【实现原理】
     * 右移 FRAC_BITS 位，等于除以 65536 后取整
     * 例如：0x28000 (2.5) >> 16 = 0x2 = 2
     *
     * 【注意事项】
     * 这是截断而非四舍五入
     * 如需四舍五入：(value_ + HALF) >> FRAC_BITS
     */
    int toInt() const {
        return value_ >> FRAC_BITS;
    }

    /**
     * @brief 转换为浮点数（仅用于调试/显示）
     * @return 浮点数近似值
     *
     * 【重要警告】
     * 返回的浮点数仅用于：
     * - 调试输出
     * - 日志记录
     * - UI 显示
     *
     * 【禁止用于计算】
     * 不要用返回值参与游戏逻辑计算！
     */
    float toFloat() const {
        return static_cast<float>(value_) / ONE;
    }

    /**
     * @brief 转换为双精度浮点数（仅用于调试/显示）
     * @return 双精度浮点数近似值
     */
    double toDouble() const {
        return static_cast<double>(value_) / ONE;
    }

    /**
     * @brief 获取原始整数值
     * @return 内部存储的 int32_t 值
     *
     * 【使用场景】
     * 1. 网络序列化：发送原始值
     * 2. 状态哈希：计算校验和
     * 3. 调试：查看内部表示
     */
    int32_t rawValue() const {
        return value_;
    }

    // ============ 四则运算 ============
    /**
     * 【定点数运算原理】
     *
     * 加减法：直接操作，无需调整
     *   (a × 2^16) + (b × 2^16) = (a + b) × 2^16 ✓
     *
     * 乘法：需要右移 16 位
     *   (a × 2^16) × (b × 2^16) = ab × 2^32
     *   需要 >> 16 得到 ab × 2^16
     *
     * 除法：需要左移 16 位
     *   (a × 2^16) / (b × 2^16) = a/b
     *   需要 << 16 得到 a/b × 2^16
     */

    /**
     * @brief 加法运算符
     * @param other 另一个定点数
     * @return 两数之和
     *
     * 【实现说明】
     * 定点数加法直接相加即可，不需要额外处理
     *
     * 【溢出风险】
     * 两个大数相加可能溢出 int32_t
     * 生产代码可添加饱和运算（溢出时返回最大/最小值）
     */
    Fixed operator+(Fixed other) const {
        return raw(value_ + other.value_);
    }

    /**
     * @brief 减法运算符
     * @param other 另一个定点数
     * @return 两数之差
     */
    Fixed operator-(Fixed other) const {
        return raw(value_ - other.value_);
    }

    /**
     * @brief 取负运算符
     * @return 相反数
     *
     * 【注意事项】
     * 对 INT32_MIN 取负会溢出（因为 |INT32_MIN| > INT32_MAX）
     */
    Fixed operator-() const {
        return raw(-value_);
    }

    /**
     * @brief 乘法运算符
     * @param other 另一个定点数
     * @return 两数之积
     *
     * 【核心算法】这是定点数运算的核心
     *
     * 【数学原理】
     * 设 a 和 b 是实际值，存储值分别是 A = a×2^16 和 B = b×2^16
     * 直接相乘：A × B = ab × 2^32
     * 需要右移 16 位：ab × 2^32 >> 16 = ab × 2^16 ✓
     *
     * 【为什么需要 64 位中间结果】
     * 两个 32 位数相乘，结果可能是 64 位
     * 例如：1.5 × 1.5 = 2.25
     *       0x18000 × 0x18000 = 0x240000000 (需要 34 位)
     *
     * 【面试考点】
     * Q: 如果不用 64 位会怎样？
     * A: 会发生溢出，结果错误。例如 1.5 × 1.5 可能得到负数
     *
     * 【性能提示】
     * 64 位乘法在 32 位 CPU 上较慢
     * 可考虑：
     * 1. 使用 SIMD 指令优化
     * 2. 对于小数值使用 32 位快速路径
     */
    Fixed operator*(Fixed other) const {
        // 使用64位中间结果防止溢出
        int64_t result = static_cast<int64_t>(value_) * other.value_;
        return raw(static_cast<int32_t>(result >> FRAC_BITS));
    }

    /**
     * @brief 除法运算符
     * @param other 除数
     * @return 商
     *
     * 【数学原理】
     * 设 a 和 b 是实际值，存储值分别是 A = a×2^16 和 B = b×2^16
     * 直接相除：A / B = a/b（丢失了 2^16 因子）
     * 需要先左移：(A << 16) / B = a/b × 2^16 ✓
     *
     * 【除零处理】
     * 返回最大值或最小值（取决于被除数符号）
     * 生产代码可能需要更复杂的处理（如抛出异常、返回错误码）
     *
     * 【精度损失】
     * 整数除法会丢失余数，例如：
     * 1 / 3 = 0.333... → 实际得到 0.33331298...
     * 这是定点数的固有限制
     */
    Fixed operator/(Fixed other) const {
        if (other.value_ == 0) {
            // 除零保护：返回最大值或最小值
            return raw(value_ >= 0 ? INT32_MAX : INT32_MIN);
        }
        // 先左移被除数，再除
        int64_t result = (static_cast<int64_t>(value_) << FRAC_BITS) / other.value_;
        return raw(static_cast<int32_t>(result));
    }

    // ============ 复合赋值 ============
    /**
     * 【复合赋值运算符】
     * 提供 +=、-=、*=、/= 运算符
     * 返回引用以支持链式调用：a += b += c
     *
     * 【注意】
     * *= 和 /= 复用了 * 和 / 运算符，确保语义一致
     */
    Fixed& operator+=(Fixed other) { value_ += other.value_; return *this; }
    Fixed& operator-=(Fixed other) { value_ -= other.value_; return *this; }
    Fixed& operator*=(Fixed other) { *this = *this * other; return *this; }
    Fixed& operator/=(Fixed other) { *this = *this / other; return *this; }

    // ============ 比较运算符 ============
    /**
     * 【比较运算符】
     * 定点数比较直接比较内部值即可
     * 因为定点格式是单调的：a < b ⟺ raw(a) < raw(b)
     *
     * 【面试考点】
     * Q: 为什么定点数可以直接比较原始值？
     * A: Q16.16 格式保持了数值的顺序关系
     *    两个定点数比大小，等价于两个整数比大小
     */
    bool operator<(Fixed other) const { return value_ < other.value_; }
    bool operator>(Fixed other) const { return value_ > other.value_; }
    bool operator<=(Fixed other) const { return value_ <= other.value_; }
    bool operator>=(Fixed other) const { return value_ >= other.value_; }
    bool operator==(Fixed other) const { return value_ == other.value_; }
    bool operator!=(Fixed other) const { return value_ != other.value_; }

    // ============ 数学函数 ============
    /**
     * @brief 绝对值
     * @return 当前值的绝对值
     *
     * 【注意】对 INT32_MIN 取绝对值会溢出
     */
    Fixed abs() const {
        return raw(value_ >= 0 ? value_ : -value_);
    }

    /**
     * @brief 平方根（确定性整数算法）
     * @param x 被开方数
     * @return 平方根
     *
     * 【核心算法】这是定点数数学库的关键
     *
     * 【算法说明】
     * 使用逐位逼近法（Digit-by-digit Algorithm）计算整数平方根
     * 这是确定性的，不依赖任何浮点运算
     *
     * 【算法原理】
     * 从高位到低位逐个确定结果的每一位：
     * 1. 从最高有效位开始
     * 2. 如果当前位可以设为 1 而不超过目标，则设为 1
     * 3. 继续下一位
     *
     * 【为什么不用牛顿迭代法】
     * 牛顿迭代法通常用浮点数实现，需要除法
     * 这里的整数算法更简单且完全确定
     *
     * 【精度调整】
     * 结果需要左移 FRAC_BITS/2 = 8 位
     * 原因：sqrt(a × 2^16) = sqrt(a) × 2^8
     *
     * 【面试考点】
     * Q: 为什么平方根结果要左移 8 位而不是 16 位？
     * A: sqrt(x × 2^16) = sqrt(x) × sqrt(2^16) = sqrt(x) × 2^8
     *    所以只需要左移 8 位来补偿
     */
    static Fixed sqrt(Fixed x) {
        if (x.value_ <= 0) return raw(0);

        // 使用整数平方根算法
        uint32_t val = static_cast<uint32_t>(x.value_);
        uint32_t result = 0;
        uint32_t bit = 1u << 30;  // 从最高位开始

        // 找到第一个有效位
        while (bit > val) bit >>= 2;

        // 逐位确定结果
        while (bit != 0) {
            if (val >= result + bit) {
                val -= result + bit;
                result = (result >> 1) + bit;
            } else {
                result >>= 1;
            }
            bit >>= 2;
        }

        // 调整精度：左移 FRAC_BITS/2 位
        return raw(result << (FRAC_BITS / 2));
    }

    /**
     * @brief 取较小值
     * @param a 第一个数
     * @param b 第二个数
     * @return 较小的那个
     *
     * 【设计说明】
     * 使用静态方法而非自由函数，保持命名空间整洁
     * 调用方式：Fixed::min(a, b)
     */
    static Fixed min(Fixed a, Fixed b) {
        return a.value_ < b.value_ ? a : b;
    }

    /**
     * @brief 取较大值
     * @param a 第一个数
     * @param b 第二个数
     * @return 较大的那个
     */
    static Fixed max(Fixed a, Fixed b) {
        return a.value_ > b.value_ ? a : b;
    }

    /**
     * @brief 限制值在指定范围内
     * @param minVal 最小值
     * @param maxVal 最大值
     * @return 限制后的值
     *
     * 【使用场景】
     * 1. 限制坐标在地图边界内
     * 2. 限制速度在最大值以内
     * 3. 限制生命值在 0~100
     *
     * 【性能提示】
     * 无分支版本：
     * return min(max(*this, minVal), maxVal);
     * 但编译器通常能优化好有分支的版本
     */
    Fixed clamp(Fixed minVal, Fixed maxVal) const {
        if (value_ < minVal.value_) return minVal;
        if (value_ > maxVal.value_) return maxVal;
        return *this;
    }

    /**
     * @brief 输出流支持
     *
     * 【使用场景】
     * 便于调试输出：std::cout << myFixed << std::endl;
     *
     * 【注意】
     * 输出的是浮点近似值，仅用于人类可读
     * 实际存储的是整数
     */
    friend std::ostream& operator<<(std::ostream& os, Fixed f) {
        os << f.toFloat();
        return os;
    }
};

// ============ 定点数二维向量 ============
/**
 * @struct FixedVec2
 * @brief 定点数二维向量 - 用于游戏中的位置、速度等
 *
 * 【设计理念】
 * 封装常用的二维向量运算，全部基于定点数实现
 * 保证所有向量运算的确定性
 *
 * 【使用场景】
 * 1. 实体位置：player.position = FixedVec2(x, y)
 * 2. 移动速度：player.velocity = FixedVec2(vx, vy)
 * 3. 碰撞检测：distance = a.distanceTo(b)
 *
 * 【面试考点】
 * Q: 为什么用 struct 而不是 class？
 * A: C++ 中 struct 默认 public，class 默认 private
 *    对于简单的数据结构，struct 更简洁
 *    这里 FixedVec2 就是一个值类型，所有成员都是公开的
 */
struct FixedVec2 {
    Fixed x, y;  // 二维坐标

    /**
     * @brief 默认构造函数
     *
     * 【注意】使用 = default，成员会被默认初始化
     * 这里 x 和 y 会被初始化为 0（因为 Fixed 默认构造是 0）
     */
    FixedVec2() = default;

    /**
     * @brief 带参数的构造函数
     * @param x_ x 坐标
     * @param y_ y 坐标
     */
    FixedVec2(Fixed x_, Fixed y_) : x(x_), y(y_) {}

    // ============ 向量运算 ============
    /**
     * @brief 向量加法
     *
     * 【几何意义】
     * 向量 a + b 表示从原点出发，先走 a，再走 b
     * 常用于：position += velocity
     */
    FixedVec2 operator+(FixedVec2 other) const {
        return {x + other.x, y + other.y};
    }

    /**
     * @brief 向量减法
     *
     * 【几何意义】
     * a - b 表示从点 b 指向点 a 的向量
     * 常用于：direction = target - source
     */
    FixedVec2 operator-(FixedVec2 other) const {
        return {x - other.x, y - other.y};
    }

    /**
     * @brief 标量乘法
     * @param s 标量因子
     *
     * 【几何意义】
     * 向量缩放，s > 1 放大，0 < s < 1 缩小
     * 常用于：velocity * deltaTime
     */
    FixedVec2 operator*(Fixed s) const {
        return {x * s, y * s};
    }

    /**
     * @brief 标量除法
     * @param s 标量除数
     *
     * 【使用场景】
     * 归一化时除以长度：v / v.length()
     */
    FixedVec2 operator/(Fixed s) const {
        return {x / s, y / s};
    }

    /**
     * @brief 向量加法赋值
     */
    FixedVec2& operator+=(FixedVec2 other) {
        x += other.x;
        y += other.y;
        return *this;
    }

    /**
     * @brief 向量减法赋值
     */
    FixedVec2& operator-=(FixedVec2 other) {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    /**
     * @brief 点积（内积）
     * @param other 另一个向量
     * @return 点积结果（标量）
     *
     * 【数学定义】
     * a · b = |a| |b| cos(θ)
     * 其中 θ 是两向量夹角
     *
     * 【几何意义】
     * - 结果 > 0：夹角 < 90°（同向）
     * - 结果 = 0：夹角 = 90°（垂直）
     * - 结果 < 0：夹角 > 90°（反向）
     *
     * 【使用场景】
     * 1. 判断两向量方向关系
     * 2. 投影计算
     * 3. 光照计算（法线点乘光照方向）
     *
     * 【面试考点】
     * Q: 如何判断敌人是否在玩家前方？
     * A: 计算玩家朝向向量和（敌人位置-玩家位置）的点积
     *    点积 > 0 表示敌人在前方
     */
    Fixed dot(FixedVec2 other) const {
        return x * other.x + y * other.y;
    }

    /**
     * @brief 叉积（外积）
     * @param other 另一个向量
     * @return 叉积结果（标量，表示 z 分量）
     *
     * 【数学定义】
     * 在 2D 中，叉积结果是标量：
     * a × b = |a| |b| sin(θ)
     *
     * 【几何意义】
     * - 结果 > 0：b 在 a 的逆时针方向
     * - 结果 = 0：a 和 b 平行
     * - 结果 < 0：b 在 a 的顺时针方向
     * - 绝对值 = a 和 b 围成的平行四边形面积
     *
     * 【使用场景】
     * 1. 判断转向方向（左转/右转）
     * 2. 计算三角形面积
     * 3. 判断点在线段的哪一侧
     */
    Fixed cross(FixedVec2 other) const {
        return x * other.y - y * other.x;
    }

    /**
     * @brief 长度的平方
     * @return |v|²
     *
     * 【性能提示】
     * 避免平方根运算，比 length() 快
     *
     * 【使用场景】
     * 比较两个距离时，可以比较平方值：
     * if (a.lengthSq() < b.lengthSq()) { ... }
     * 比 if (a.length() < b.length()) 更高效
     */
    Fixed lengthSq() const {
        return dot(*this);
    }

    /**
     * @brief 向量长度（模）
     * @return |v| = sqrt(x² + y²)
     *
     * 【性能提示】
     * 涉及平方根运算，较慢
     * 如果只需要比较长度，用 lengthSq()
     */
    Fixed length() const {
        return Fixed::sqrt(lengthSq());
    }

    /**
     * @brief 归一化（单位向量）
     * @return 长度为 1 的同方向向量
     *
     * 【数学定义】
     * v̂ = v / |v|
     *
     * 【零向量处理】
     * 如果是零向量，返回零向量（避免除零）
     *
     * 【使用场景】
     * 获取方向向量：direction = (target - source).normalize()
     *
     * 【面试考点】
     * Q: 归一化后的向量有什么性质？
     * A: 长度为 1，保留方向信息
     *    常用于表示方向，与速度大小相乘得到速度向量
     */
    FixedVec2 normalize() const {
        Fixed len = length();
        if (len.rawValue() == 0) {
            return {Fixed::fromInt(0), Fixed::fromInt(0)};
        }
        return {x / len, y / len};
    }

    /**
     * @brief 到另一个点的距离
     * @param other 目标点
     * @return 两点间的距离
     *
     * 【几何意义】
     * 欧几里得距离：sqrt((x2-x1)² + (y2-y1)²)
     */
    Fixed distanceTo(FixedVec2 other) const {
        return (*this - other).length();
    }

    /**
     * @brief 到另一个点的距离平方
     * @param other 目标点
     * @return 两点间距离的平方
     *
     * 【性能提示】
     * 比 distanceTo() 快，用于距离比较
     *
     * 【使用示例】
     * 判断是否在攻击范围内：
     * if (enemy.distanceSqTo(player) < attackRange * attackRange) { ... }
     */
    Fixed distanceSqTo(FixedVec2 other) const {
        return (*this - other).lengthSq();
    }

    /**
     * @brief 创建零向量
     * @return (0, 0)
     *
     * 【使用场景】
     * 初始化速度为静止：velocity = FixedVec2::zero()
     */
    static FixedVec2 zero() {
        return {Fixed::fromInt(0), Fixed::fromInt(0)};
    }

    /**
     * @brief 输出流支持
     *
     * 输出格式：(x, y)
     * 便于调试
     */
    friend std::ostream& operator<<(std::ostream& os, FixedVec2 v) {
        os << "(" << v.x << ", " << v.y << ")";
        return os;
    }
};

/**
 * 【扩展阅读】
 *
 * 1. 定点数的历史：
 *    - 早期游戏机（如 SNES、PS1）没有浮点硬件，全部使用定点数
 *    - Doom、Quake 1 等经典游戏使用定点数实现 3D 引擎
 *    - 现代帧同步游戏重新采用定点数保证确定性
 *
 * 2. 其他定点数格式：
 *    | 格式    | 整数位 | 小数位 | 用途 |
 *    |---------|--------|--------|------|
 *    | Q8.8    | 8      | 8      | 颜色值 |
 *    | Q16.16  | 16     | 16     | 游戏坐标（本项目） |
 *    | Q8.24   | 8      | 24     | 高精度计算 |
 *    | Q31.32  | 31     | 32     | 64 位高精度 |
 *
 * 3. 商业引擎的做法：
 *    - Unity DOTS: 提供 fp (fixed point) 类型
 *    - Unreal Engine: 有确定性物理引擎选项
 *    - 王者荣耀: 自研定点数库 + 帧同步
 *
 * 4. 进一步优化方向：
 *    - SIMD 指令：使用 SSE/AVX 并行计算多个定点数
 *    - 查表法：对于 sin/cos 等函数，预计算查表更快
 *    - 内联汇编：对关键路径进行手动优化
 */
