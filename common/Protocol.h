/**
 * Protocol.h - 网络协议定义
 *
 * 定义客户端与服务器之间的通信协议
 */
#pragma once
#include <vector>
#include <cstdint>
#include <cstring>

namespace lockstep {

// ============ 消息类型 ============
enum class MsgType : uint8_t {
    JOIN        = 1,    // C->S 加入房间请求
    JOIN_ACK    = 2,    // S->C 加入确认（返回playerId）
    START       = 3,    // S->C 游戏开始
    INPUT       = 4,    // C->S 玩家输入
    FRAME       = 5,    // S->C 帧数据广播
    RECONNECT   = 6,    // C->S 断线重连请求
    SYNC        = 7,    // S->C 重连同步数据
    HEARTBEAT   = 8,    // 心跳包
    GAME_OVER   = 9,    // S->C 游戏结束
};

// ============ 序列化辅助函数 ============
inline void writeU8(std::vector<uint8_t>& buf, uint8_t v) {
    buf.push_back(v);
}

inline void writeU16(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(v & 0xFF);
    buf.push_back((v >> 8) & 0xFF);
}

inline void writeU32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(v & 0xFF);
    buf.push_back((v >> 8) & 0xFF);
    buf.push_back((v >> 16) & 0xFF);
    buf.push_back((v >> 24) & 0xFF);
}

inline void writeI32(std::vector<uint8_t>& buf, int32_t v) {
    writeU32(buf, static_cast<uint32_t>(v));
}

inline uint8_t readU8(const uint8_t* data, size_t& offset) {
    return data[offset++];
}

inline uint16_t readU16(const uint8_t* data, size_t& offset) {
    uint16_t v = data[offset] | (data[offset + 1] << 8);
    offset += 2;
    return v;
}

inline uint32_t readU32(const uint8_t* data, size_t& offset) {
    uint32_t v = data[offset] | (data[offset + 1] << 8) |
                 (data[offset + 2] << 16) | (data[offset + 3] << 24);
    offset += 4;
    return v;
}

inline int32_t readI32(const uint8_t* data, size_t& offset) {
    return static_cast<int32_t>(readU32(data, offset));
}

// ============ 玩家输入结构 ============
struct PlayerInput {
    uint32_t playerId = 0;
    uint32_t frameId = 0;

    // 移动方向: 0-7表示8方向，8表示静止
    uint8_t moveDir = 8;

    // 动作位域: bit0=攻击, bit1=技能1, bit2=技能2...
    uint8_t actions = 0;

    // 目标位置（用于技能释放等）
    int32_t targetX = 0;
    int32_t targetY = 0;

    void serialize(std::vector<uint8_t>& buf) const {
        writeU32(buf, playerId);
        writeU32(buf, frameId);
        writeU8(buf, moveDir);
        writeU8(buf, actions);
        writeI32(buf, targetX);
        writeI32(buf, targetY);
    }

    void deserialize(const uint8_t* data, size_t& offset) {
        playerId = readU32(data, offset);
        frameId = readU32(data, offset);
        moveDir = readU8(data, offset);
        actions = readU8(data, offset);
        targetX = readI32(data, offset);
        targetY = readI32(data, offset);
    }

    static constexpr size_t SIZE = 4 + 4 + 1 + 1 + 4 + 4;  // 18字节
};

// ============ 帧数据结构 ============
struct FrameData {
    uint32_t frameId = 0;
    std::vector<PlayerInput> inputs;
    uint32_t checksum = 0;  // 用于同步校验

    void serialize(std::vector<uint8_t>& buf) const {
        writeU32(buf, frameId);
        writeU8(buf, static_cast<uint8_t>(inputs.size()));

        for (const auto& input : inputs) {
            input.serialize(buf);
        }

        writeU32(buf, checksum);
    }

    void deserialize(const uint8_t* data, size_t& offset, size_t maxLen) {
        frameId = readU32(data, offset);
        uint8_t inputCount = readU8(data, offset);

        inputs.clear();
        inputs.reserve(inputCount);

        for (uint8_t i = 0; i < inputCount && offset < maxLen; ++i) {
            PlayerInput input;
            input.deserialize(data, offset);
            inputs.push_back(input);
        }

        if (offset + 4 <= maxLen) {
            checksum = readU32(data, offset);
        }
    }
};

// ============ 加入确认消息 ============
struct JoinAckMsg {
    uint32_t playerId = 0;
    uint32_t playerCount = 0;

    void serialize(std::vector<uint8_t>& buf) const {
        writeU8(buf, static_cast<uint8_t>(MsgType::JOIN_ACK));
        writeU32(buf, playerId);
        writeU32(buf, playerCount);
    }

    void deserialize(const uint8_t* data, size_t& offset) {
        playerId = readU32(data, offset);
        playerCount = readU32(data, offset);
    }
};

// ============ 游戏开始消息 ============
struct StartMsg {
    uint32_t playerCount = 0;
    uint32_t randomSeed = 0;

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
struct MsgHeader {
    MsgType type;
    uint16_t length;

    static constexpr size_t SIZE = 3;
};

} // namespace lockstep
