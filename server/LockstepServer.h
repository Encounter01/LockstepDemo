/**
 * LockstepServer.h - 帧同步服务器
 *
 * 职责：
 * 1. 接收玩家连接
 * 2. 收集每帧输入
 * 3. 广播帧数据
 * 4. 处理断线重连
 */
#pragma once
#include <iostream>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include "../common/Network.h"
#include "../common/Protocol.h"
#include "../common/GameWorld.h"

namespace lockstep {

class LockstepServer {
public:
    // 配置常量
    static constexpr int LOGIC_FPS = 15;                    // 逻辑帧率
    static constexpr int FRAME_MS = 1000 / LOGIC_FPS;       // 每帧毫秒数
    static constexpr uint32_t MAX_PLAYERS = 4;              // 最大玩家数
    static constexpr uint32_t MIN_PLAYERS = 2;              // 最少开始人数
    static constexpr size_t MAX_HISTORY = 1000;             // 历史帧缓存数量

private:
    // 玩家会话
    struct PlayerSession {
        uint32_t playerId;
        sockaddr_in endpoint;
        PlayerInput lastInput;
        bool inputReceived = false;
        std::chrono::steady_clock::time_point lastHeartbeat;
        bool connected = true;
    };

    UdpSocket socket_;
    uint16_t port_;

    std::unordered_map<uint32_t, PlayerSession> players_;
    std::mutex playersMutex_;

    uint32_t currentFrame_ = 0;
    bool gameStarted_ = false;
    uint32_t randomSeed_ = 0;

    // 帧历史（用于重连）
    std::vector<FrameData> frameHistory_;
    std::mutex historyMutex_;

    // 运行状态
    std::atomic<bool> running_{false};

    // 接收缓冲区
    static constexpr size_t RECV_BUFFER_SIZE = 4096;
    uint8_t recvBuffer_[RECV_BUFFER_SIZE];

public:
    LockstepServer(uint16_t port) : port_(port) {
        randomSeed_ = static_cast<uint32_t>(
            std::chrono::system_clock::now().time_since_epoch().count()
        );
    }

    ~LockstepServer() {
        stop();
    }

    bool start() {
        init_network();

        if (!socket_.create()) {
            std::cerr << "[Server] Failed to create socket" << std::endl;
            return false;
        }

        if (!socket_.bind(port_)) {
            std::cerr << "[Server] Failed to bind to port " << port_ << std::endl;
            return false;
        }

        socket_.setNonBlocking();
        running_ = true;

        std::cout << "[Server] Started on port " << port_ << std::endl;
        std::cout << "[Server] Waiting for " << MIN_PLAYERS << " players..." << std::endl;

        return true;
    }

    void stop() {
        running_ = false;
        socket_.close();
        cleanup_network();
    }

    void run() {
        auto lastFrameTime = std::chrono::steady_clock::now();

        while (running_) {
            // 处理网络消息
            processNetwork();

            // 如果游戏已开始，执行帧逻辑
            if (gameStarted_) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - lastFrameTime
                ).count();

                if (elapsed >= FRAME_MS) {
                    tick();
                    lastFrameTime = now;
                }
            }

            // 短暂休眠避免CPU满载
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

private:
    // ============ 网络处理 ============
    void processNetwork() {
        sockaddr_in fromAddr{};
        int len = socket_.recvFrom(recvBuffer_, RECV_BUFFER_SIZE, fromAddr);

        while (len > 0) {
            handleMessage(recvBuffer_, len, fromAddr);
            len = socket_.recvFrom(recvBuffer_, RECV_BUFFER_SIZE, fromAddr);
        }
    }

    void handleMessage(const uint8_t* data, size_t len, const sockaddr_in& sender) {
        if (len < 1) return;

        MsgType type = static_cast<MsgType>(data[0]);

        switch (type) {
            case MsgType::JOIN:
                handleJoin(sender);
                break;
            case MsgType::INPUT:
                handleInput(data + 1, len - 1);
                break;
            case MsgType::RECONNECT:
                handleReconnect(data + 1, len - 1, sender);
                break;
            case MsgType::HEARTBEAT:
                handleHeartbeat(data + 1, len - 1, sender);
                break;
            default:
                break;
        }
    }

    // ============ 处理加入请求 ============
    void handleJoin(const sockaddr_in& sender) {
        std::lock_guard<std::mutex> lock(playersMutex_);

        if (gameStarted_) {
            std::cout << "[Server] Game already started, rejecting join" << std::endl;
            return;
        }

        if (players_.size() >= MAX_PLAYERS) {
            std::cout << "[Server] Room full, rejecting join" << std::endl;
            return;
        }

        // 检查是否已存在
        for (const auto& [id, session] : players_) {
            if (UdpSocket::addrEquals(session.endpoint, sender)) {
                std::cout << "[Server] Player already joined" << std::endl;
                return;
            }
        }

        // 分配playerId
        uint32_t playerId = static_cast<uint32_t>(players_.size());

        PlayerSession session;
        session.playerId = playerId;
        session.endpoint = sender;
        session.lastInput = {};
        session.lastInput.playerId = playerId;
        session.lastInput.moveDir = 8;  // 静止
        session.inputReceived = false;
        session.lastHeartbeat = std::chrono::steady_clock::now();
        session.connected = true;

        players_[playerId] = session;

        std::cout << "[Server] Player " << playerId << " joined from "
                  << UdpSocket::addrToString(sender) << std::endl;

        // 发送加入确认
        sendJoinAck(playerId, sender);

        // 检查是否可以开始游戏
        if (players_.size() >= MIN_PLAYERS) {
            startGame();
        }
    }

    void sendJoinAck(uint32_t playerId, const sockaddr_in& addr) {
        std::vector<uint8_t> msg;
        JoinAckMsg ack;
        ack.playerId = playerId;
        ack.playerCount = static_cast<uint32_t>(players_.size());
        ack.serialize(msg);

        socket_.sendTo(msg.data(), msg.size(), addr);
    }

    // ============ 开始游戏 ============
    void startGame() {
        gameStarted_ = true;
        currentFrame_ = 0;

        std::cout << "[Server] Game starting with " << players_.size()
                  << " players, seed=" << randomSeed_ << std::endl;

        // 广播游戏开始消息
        std::vector<uint8_t> msg;
        StartMsg start;
        start.playerCount = static_cast<uint32_t>(players_.size());
        start.randomSeed = randomSeed_;
        start.serialize(msg);

        broadcast(msg);
    }

    // ============ 处理输入 ============
    void handleInput(const uint8_t* data, size_t len) {
        if (len < PlayerInput::SIZE) return;

        PlayerInput input;
        size_t offset = 0;
        input.deserialize(data, offset);

        std::lock_guard<std::mutex> lock(playersMutex_);

        if (players_.count(input.playerId)) {
            players_[input.playerId].lastInput = input;
            players_[input.playerId].inputReceived = true;
            players_[input.playerId].lastHeartbeat = std::chrono::steady_clock::now();
        }
    }

    // ============ 帧更新 ============
    void tick() {
        FrameData frame;
        frame.frameId = currentFrame_;

        {
            std::lock_guard<std::mutex> lock(playersMutex_);

            // 收集所有玩家输入
            for (auto& [id, session] : players_) {
                if (session.inputReceived) {
                    frame.inputs.push_back(session.lastInput);
                } else {
                    // 没收到输入，使用空输入
                    PlayerInput empty{};
                    empty.playerId = id;
                    empty.frameId = currentFrame_;
                    empty.moveDir = 8;  // 静止
                    frame.inputs.push_back(empty);
                }
                session.inputReceived = false;
            }
        }

        // 排序确保确定性
        std::sort(frame.inputs.begin(), frame.inputs.end(),
            [](const PlayerInput& a, const PlayerInput& b) {
                return a.playerId < b.playerId;
            });

        // 广播帧数据
        std::vector<uint8_t> msg;
        writeU8(msg, static_cast<uint8_t>(MsgType::FRAME));
        frame.serialize(msg);

        broadcast(msg);

        // 保存历史
        {
            std::lock_guard<std::mutex> lock(historyMutex_);
            frameHistory_.push_back(frame);
            if (frameHistory_.size() > MAX_HISTORY) {
                frameHistory_.erase(frameHistory_.begin());
            }
        }

        currentFrame_++;

        // 每100帧输出状态
        if (currentFrame_ % 100 == 0) {
            std::cout << "[Server] Frame " << currentFrame_ << std::endl;
        }
    }

    // ============ 处理重连 ============
    void handleReconnect(const uint8_t* data, size_t len, const sockaddr_in& sender) {
        if (len < 8) return;

        size_t offset = 0;
        uint32_t playerId = readU32(data, offset);
        uint32_t lastFrame = readU32(data, offset);

        std::lock_guard<std::mutex> lock(playersMutex_);

        if (!players_.count(playerId)) return;

        // 更新endpoint
        players_[playerId].endpoint = sender;
        players_[playerId].connected = true;
        players_[playerId].lastHeartbeat = std::chrono::steady_clock::now();

        std::cout << "[Server] Player " << playerId << " reconnecting from frame "
                  << lastFrame << std::endl;

        // 发送遗漏的帧
        sendSyncFrames(playerId, lastFrame, sender);
    }

    void sendSyncFrames(uint32_t playerId, uint32_t lastFrame, const sockaddr_in& addr) {
        std::lock_guard<std::mutex> lock(historyMutex_);

        if (frameHistory_.empty()) return;

        // 找到起始位置
        size_t startIdx = 0;
        if (!frameHistory_.empty() && lastFrame >= frameHistory_.front().frameId) {
            startIdx = lastFrame - frameHistory_.front().frameId + 1;
        }

        if (startIdx >= frameHistory_.size()) return;

        // 构建同步消息
        std::vector<uint8_t> msg;
        writeU8(msg, static_cast<uint8_t>(MsgType::SYNC));

        uint16_t count = static_cast<uint16_t>(frameHistory_.size() - startIdx);
        writeU16(msg, count);

        for (size_t i = startIdx; i < frameHistory_.size(); ++i) {
            frameHistory_[i].serialize(msg);
        }

        socket_.sendTo(msg.data(), msg.size(), addr);

        std::cout << "[Server] Sent " << count << " sync frames to player "
                  << playerId << std::endl;
    }

    // ============ 处理心跳 ============
    void handleHeartbeat(const uint8_t* data, size_t len, const sockaddr_in& sender) {
        std::lock_guard<std::mutex> lock(playersMutex_);

        for (auto& [id, session] : players_) {
            if (UdpSocket::addrEquals(session.endpoint, sender)) {
                session.lastHeartbeat = std::chrono::steady_clock::now();
                break;
            }
        }
    }

    // ============ 广播消息 ============
    void broadcast(const std::vector<uint8_t>& msg) {
        std::lock_guard<std::mutex> lock(playersMutex_);

        for (const auto& [id, session] : players_) {
            if (session.connected) {
                socket_.sendTo(msg.data(), msg.size(), session.endpoint);
            }
        }
    }
};

} // namespace lockstep
