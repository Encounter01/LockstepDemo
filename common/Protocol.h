/**
 * @file Protocol.h
 * @brief 帧同步网络协议定义 - 客户端与服务器通信规范
 *
 * 【技术概述】
 * 本文件定义了帧同步游戏的完整网络协议，包括：
 * 1. 消息类型枚举（MsgType）
 * 2. 二进制序列化/反序列化函数
 * 3. 各类消息结构体
 *
 * 【协议设计原则】
 * 1. 紧凑性：使用定长字段，最小化网络带宽
 * 2. 跨平台：使用小端序，确保不同架构兼容
 * 3. 简单性：二进制格式，解析效率高
 * 4. 可扩展：消息类型使用枚举，便于添加新消息
 *
 * 【消息格式概览】
 * +----------+----------+------------------+
 * | MsgType  |  Length  |     Payload      |
 * | 1 byte   | 2 bytes  |   变长数据       |
 * +----------+----------+------------------+
 *
 * 【面试要点】
 * 1. Q: 为什么使用自定义二进制协议而不是 JSON/Protobuf？
 *    A: 游戏对延迟和带宽敏感：
 *       - 二进制比 JSON 节省 50%+ 空间
 *       - 解析速度比文本快 10 倍以上
 *       - 帧同步协议简单固定，无需 Protobuf 的灵活性
 *
 * 2. Q: 为什么选择小端序而不是网络字节序（大端）？
 *    A: 现代 CPU（x86、ARM）大多是小端序：
 *       - 小端序可以直接 memcpy，无需转换
 *       - 性能更好
 *       - 只要统一使用一种字节序即可
 *       网络字节序是历史遗留，TCP/IP 协议头用大端，但应用层可自选
 *
 * 3. Q: 消息大小设计有什么考量？
 *    A: PlayerInput 设计为 18 字节，4 人游戏每帧约 80 字节：
 *       - 控制在 MTU（1500）以内，避免 IP 分片
 *       - 15 FPS 下带宽约 1.2 KB/s，非常节省
 *
 * 【生产实践】
 * 1. 协议版本：生产环境应在消息头添加版本号
 * 2. 魔数校验：添加魔数（如 0xDEAD）防止无效包
 * 3. CRC 校验：关键消息添加 CRC 确保完整性
 * 4. 压缩：对于大消息可使用 LZ4 快速压缩
 */
#pragma once
#include <vector>
#include <cstdint>
#include <cstring>

namespace lockstep {

// ============ 消息类型 ============
/**
 * @enum MsgType
 * @brief 消息类型枚举
 *
 * 【消息流程图】
 *
 * 客户端                    服务器
 *   |                         |
 *   |------- JOIN --------->  |  玩家请求加入
 *   |<------ JOIN_ACK --------|  返回 playerId
 *   |                         |
 *   |       [等待其他玩家]     |
 *   |                         |
 *   |<------ START -----------|  游戏开始，附带随机种子
 *   |                         |
 *   |======== 游戏循环 =======|
 *   |                         |
 *   |------- INPUT --------->|  每帧发送输入
 *   |<------ FRAME -----------|  广播所有玩家输入
 *   |------- HEARTBEAT ----->|  心跳保活
 *   |                         |
 *   |======== 断线重连 =======|
 *   |                         |
 *   |------- RECONNECT ----->|  请求重连
 *   |<------ SYNC ------------|  补发缺失帧
 *   |                         |
 *   |<------ GAME_OVER -------|  游戏结束
 *
 * 【设计说明】
 * - 使用 uint8_t 底层类型，只占 1 字节
 * - 显式指定数值，便于调试和抓包分析
 * - C->S 表示客户端到服务器，S->C 反之
 */
enum class MsgType : uint8_t {
    JOIN        = 1,    // C->S 加入房间请求
    JOIN_ACK    = 2,    // S->C 加入确认（返回playerId）
    START       = 3,    // S->C 游戏开始
    INPUT       = 4,    // C->S 玩家输入
    FRAME       = 5,    // S->C 帧数据广播
    RECONNECT   = 6,    // C->S 断线重连请求
    SYNC        = 7,    // S->C 重连同步数据
    HEARTBEAT   = 8,    // 心跳包（双向）
    GAME_OVER   = 9,    // S->C 游戏结束
};

// ============ 序列化辅助函数 ============
/**
 * 【序列化设计】
 *
 * 采用小端序（Little-Endian）：
 * - 低字节在前，高字节在后
 * - 与 x86/ARM 的内存布局一致
 *
 * 例如：uint32_t v = 0x12345678
 * 序列化后：[0x78, 0x56, 0x34, 0x12]
 *
 * 【为什么使用 inline 函数】
 * 1. 头文件定义，避免链接错误
 * 2. 编译器会内联优化，无函数调用开销
 * 3. 简洁清晰，便于使用
 *
 * 【面试考点】
 * Q: 如何判断当前机器的字节序？
 * A: int n = 1;
 *    bool isLittle = (*(char*)&n == 1);
 *    或使用 C++20 的 std::endian
 */

/**
 * @brief 写入 8 位无符号整数
 * @param buf 目标缓冲区
 * @param v 要写入的值
 *
 * 8 位数据无字节序问题，直接追加
 */
inline void writeU8(std::vector<uint8_t>& buf, uint8_t v) {
    buf.push_back(v);
}

/**
 * @brief 写入 16 位无符号整数（小端序）
 * @param buf 目标缓冲区
 * @param v 要写入的值
 *
 * 【实现说明】
 * 先写低 8 位，再写高 8 位
 * v = 0x1234 → [0x34, 0x12]
 */
inline void writeU16(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(v & 0xFF);         // 低 8 位
    buf.push_back((v >> 8) & 0xFF);  // 高 8 位
}

/**
 * @brief 写入 32 位无符号整数（小端序）
 * @param buf 目标缓冲区
 * @param v 要写入的值
 *
 * 【实现说明】
 * 从低字节到高字节依次写入
 * v = 0x12345678 → [0x78, 0x56, 0x34, 0x12]
 */
inline void writeU32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(v & 0xFF);          // 字节 0（最低）
    buf.push_back((v >> 8) & 0xFF);   // 字节 1
    buf.push_back((v >> 16) & 0xFF);  // 字节 2
    buf.push_back((v >> 24) & 0xFF);  // 字节 3（最高）
}

/**
 * @brief 写入 32 位有符号整数
 * @param buf 目标缓冲区
 * @param v 要写入的值
 *
 * 【实现说明】
 * 有符号和无符号在二进制表示上相同（补码）
 * 直接转换为 uint32_t 写入
 */
inline void writeI32(std::vector<uint8_t>& buf, int32_t v) {
    writeU32(buf, static_cast<uint32_t>(v));
}

/**
 * @brief 读取 8 位无符号整数
 * @param data 源数据
 * @param offset 当前偏移（会自动递增）
 * @return 读取的值
 *
 * 【设计说明】
 * offset 使用引用传递，读取后自动更新位置
 * 这样连续读取时无需手动计算偏移
 */
inline uint8_t readU8(const uint8_t* data, size_t& offset) {
    return data[offset++];
}

/**
 * @brief 读取 16 位无符号整数（小端序）
 * @param data 源数据
 * @param offset 当前偏移（会自动递增 2）
 * @return 读取的值
 *
 * 【实现说明】
 * 低字节 | (高字节 << 8)
 * [0x34, 0x12] → 0x1234
 */
inline uint16_t readU16(const uint8_t* data, size_t& offset) {
    uint16_t v = data[offset] | (data[offset + 1] << 8);
    offset += 2;
    return v;
}

/**
 * @brief 读取 32 位无符号整数（小端序）
 * @param data 源数据
 * @param offset 当前偏移（会自动递增 4）
 * @return 读取的值
 *
 * 【实现说明】
 * 将 4 个字节按权重组合
 * [0x78, 0x56, 0x34, 0x12] → 0x12345678
 */
inline uint32_t readU32(const uint8_t* data, size_t& offset) {
    uint32_t v = data[offset] | (data[offset + 1] << 8) |
                 (data[offset + 2] << 16) | (data[offset + 3] << 24);
    offset += 4;
    return v;
}

/**
 * @brief 读取 32 位有符号整数
 * @param data 源数据
 * @param offset 当前偏移（会自动递增 4）
 * @return 读取的值
 */
inline int32_t readI32(const uint8_t* data, size_t& offset) {
    return static_cast<int32_t>(readU32(data, offset));
}

// ============ 玩家输入结构 ============
/**
 * @struct PlayerInput
 * @brief 玩家单帧输入数据
 *
 * 【设计理念】
 * 帧同步只传输输入，不传输状态。
 * 这是帧同步节省带宽的核心：无论场景多复杂，每帧只需发送操作。
 *
 * 【字段设计】
 * +----------+--------+----------------------------------------+
 * | 字段     | 大小   | 说明                                   |
 * +----------+--------+----------------------------------------+
 * | playerId | 4 字节 | 玩家唯一标识                           |
 * | frameId  | 4 字节 | 帧序号，用于排序和校验                 |
 * | moveDir  | 1 字节 | 移动方向（0-7）或静止（8）             |
 * | actions  | 1 字节 | 动作位域（攻击、技能等）               |
 * | targetX  | 4 字节 | 目标 X 坐标（定点数原始值）            |
 * | targetY  | 4 字节 | 目标 Y 坐标（定点数原始值）            |
 * +----------+--------+----------------------------------------+
 * | 总计     | 18 字节 |                                        |
 * +----------+--------+----------------------------------------+
 *
 * 【移动方向编码】
 *     7   0   1
 *      \  |  /
 *   6 ←  ·  → 2
 *      /  |  \
 *     5   4   3
 *   8 = 静止
 *
 * 【动作位域】
 * bit 0: 攻击
 * bit 1: 技能1
 * bit 2: 技能2
 * ...
 *
 * 【面试考点】
 * Q: 为什么用位域表示动作？
 * A: 1. 节省空间：8 个动作只需 1 字节
 *    2. 支持组合：可以同时按多个键
 *    3. 扩展方便：增加新动作不改变结构大小
 */
struct PlayerInput {
    uint32_t playerId = 0;  // 玩家 ID
    uint32_t frameId = 0;   // 帧序号

    // 移动方向: 0-7表示8方向，8表示静止
    uint8_t moveDir = 8;

    // 动作位域: bit0=攻击, bit1=技能1, bit2=技能2...
    uint8_t actions = 0;

    // 目标位置（用于技能释放等）- 定点数原始值
    int32_t targetX = 0;
    int32_t targetY = 0;

    /**
     * @brief 序列化为二进制
     * @param buf 目标缓冲区
     *
     * 【注意】序列化不包含消息类型，由外层添加
     */
    void serialize(std::vector<uint8_t>& buf) const {
        writeU32(buf, playerId);
        writeU32(buf, frameId);
        writeU8(buf, moveDir);
        writeU8(buf, actions);
        writeI32(buf, targetX);
        writeI32(buf, targetY);
    }

    /**
     * @brief 从二进制反序列化
     * @param data 源数据
     * @param offset 当前偏移（会自动更新）
     */
    void deserialize(const uint8_t* data, size_t& offset) {
        playerId = readU32(data, offset);
        frameId = readU32(data, offset);
        moveDir = readU8(data, offset);
        actions = readU8(data, offset);
        targetX = readI32(data, offset);
        targetY = readI32(data, offset);
    }

    /**
     * 【编译期常量】
     * 结构体序列化后的固定大小
     * 用于预分配缓冲区和校验
     */
    static constexpr size_t SIZE = 4 + 4 + 1 + 1 + 4 + 4;  // 18字节
};

// ============ 帧数据结构 ============
/**
 * @struct FrameData
 * @brief 单帧的完整数据
 *
 * 【设计说明】
 * FrameData 是服务器广播给所有客户端的帧包
 * 包含了这一帧所有玩家的输入
 *
 * 【结构】
 * +----------+------------+------------------+----------+
 * | frameId  | inputCount | PlayerInput × N  | checksum |
 * | 4 bytes  | 1 byte     | 18 × N bytes     | 4 bytes  |
 * +----------+------------+------------------+----------+
 *
 * 【checksum 用途】
 * 1. 服务器计算游戏状态哈希
 * 2. 客户端执行帧后比对
 * 3. 不一致时可触发重连或报警
 *
 * 【帧大小分析】
 * - 2 人游戏：4 + 1 + 18×2 + 4 = 45 字节
 * - 4 人游戏：4 + 1 + 18×4 + 4 = 81 字节
 * - 15 FPS，4 人：81 × 15 = 1215 字节/秒 ≈ 1.2 KB/s
 *
 * 这个带宽对于任何网络都是可接受的！
 */
struct FrameData {
    uint32_t frameId = 0;              // 帧序号
    std::vector<PlayerInput> inputs;   // 所有玩家的输入
    uint32_t checksum = 0;             // 状态校验和

    /**
     * @brief 序列化帧数据
     * @param buf 目标缓冲区
     */
    void serialize(std::vector<uint8_t>& buf) const {
        writeU32(buf, frameId);
        writeU8(buf, static_cast<uint8_t>(inputs.size()));

        for (const auto& input : inputs) {
            input.serialize(buf);
        }

        writeU32(buf, checksum);
    }

    /**
     * @brief 反序列化帧数据
     * @param data 源数据
     * @param offset 当前偏移
     * @param maxLen 数据最大长度（边界检查）
     *
     * 【安全考虑】
     * maxLen 参数防止越界读取
     * 恶意数据包可能声称有很多输入，实际数据不够
     */
    void deserialize(const uint8_t* data, size_t& offset, size_t maxLen) {
        frameId = readU32(data, offset);
        uint8_t inputCount = readU8(data, offset);

        inputs.clear();
        inputs.reserve(inputCount);

        // 边界检查：确保有足够的数据
        for (uint8_t i = 0; i < inputCount && offset < maxLen; ++i) {
            PlayerInput input;
            input.deserialize(data, offset);
            inputs.push_back(input);
        }

        // 读取校验和（如果有）
        if (offset + 4 <= maxLen) {
            checksum = readU32(data, offset);
        }
    }
};

// ============ 加入确认消息 ============
/**
 * @struct JoinAckMsg
 * @brief 服务器对加入请求的响应
 *
 * 【使用场景】
 * 客户端发送 JOIN → 服务器返回 JOIN_ACK
 *
 * 【字段说明】
 * - playerId: 分配给客户端的唯一 ID（从 0 开始）
 * - playerCount: 当前房间人数（用于显示"等待中..."）
 */
struct JoinAckMsg {
    uint32_t playerId = 0;      // 分配的玩家 ID
    uint32_t playerCount = 0;   // 当前玩家数量

    /**
     * @brief 序列化（包含消息类型）
     * @param buf 目标缓冲区
     *
     * 【注意】此方法会写入 MsgType
     */
    void serialize(std::vector<uint8_t>& buf) const {
        writeU8(buf, static_cast<uint8_t>(MsgType::JOIN_ACK));
        writeU32(buf, playerId);
        writeU32(buf, playerCount);
    }

    /**
     * @brief 反序列化（不包含消息类型）
     * @param data 源数据（已跳过 MsgType）
     * @param offset 当前偏移
     */
    void deserialize(const uint8_t* data, size_t& offset) {
        playerId = readU32(data, offset);
        playerCount = readU32(data, offset);
    }
};

// ============ 游戏开始消息 ============
/**
 * @struct StartMsg
 * @brief 游戏开始通知
 *
 * 【使用场景】
 * 当玩家人数达到要求时，服务器广播此消息
 *
 * 【关键字段】
 * randomSeed：随机数种子，所有客户端使用相同种子
 * 这是保证随机事件一致的关键！
 *
 * 【流程】
 * 1. 服务器生成随机种子
 * 2. 通过 START 消息广播
 * 3. 所有客户端用此种子初始化随机数生成器
 */
struct StartMsg {
    uint32_t playerCount = 0;   // 参与游戏的玩家数
    uint32_t randomSeed = 0;    // 随机数种子（关键！）

    void serialize(std::vector<uint8_t>& buf) const {
        writeU8(buf, static_cast<uint8_t>(MsgType::START));
        writeU32(buf, playerCount);
        writeU32(buf, randomSeed);
    }

    void deserialize(const uint8_t* data, size_t& offset) {
        playerCount = readU32(data, offset);
        randomSeed = readU32(data, offset);
    }
};

// ============ 消息头 ============
/**
 * @struct MsgHeader
 * @brief 通用消息头
 *
 * 【格式】
 * +----------+----------+
 * | MsgType  |  Length  |
 * | 1 byte   | 2 bytes  |
 * +----------+----------+
 *
 * 【设计说明】
 * - type: 消息类型，用于分发处理
 * - length: 载荷长度（不包含头部）
 *
 * 【为什么需要 length】
 * UDP 是报文协议，其实可以不需要
 * 但添加 length 便于：
 * 1. 调试时校验完整性
 * 2. 未来可能改为 TCP
 * 3. 跳过不识别的消息类型
 */
struct MsgHeader {
    MsgType type;       // 消息类型
    uint16_t length;    // 载荷长度

    static constexpr size_t SIZE = 3;  // 1 + 2 字节
};

} // namespace lockstep

/**
 * 【扩展阅读】
 *
 * 1. 序列化库对比：
 *    | 方案 | 优点 | 缺点 | 适用场景 |
 *    |------|------|------|----------|
 *    | 手写二进制 | 最快最小 | 需手动维护 | 游戏协议（本项目） |
 *    | Protobuf | 通用灵活 | 体积较大 | 通用 RPC |
 *    | FlatBuffers | 零拷贝 | 复杂 | 高性能游戏 |
 *    | JSON | 可读 | 慢且大 | 配置文件 |
 *
 * 2. 字节序处理方案：
 *    - 方案1：统一使用网络字节序（大端），标准但需转换
 *    - 方案2：统一使用小端序（本项目），现代 CPU 友好
 *    - 方案3：消息头标记字节序，灵活但复杂
 *
 * 3. 协议安全考虑：
 *    - 长度校验：防止越界读取
 *    - 类型校验：未知消息类型的处理
 *    - 频率限制：防止消息洪水攻击
 *    - 加密：敏感数据应加密传输
 *
 * 4. 常见游戏协议设计：
 *    - 王者荣耀：自定义二进制 + UDP
 *    - Dota 2：Protobuf + 增量压缩
 *    - 英雄联盟：自定义二进制 + 混合同步
 */
