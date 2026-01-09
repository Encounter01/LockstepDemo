/**
 * LockstepClient.h - 帧同步客户端
 *
 * 职责：
 * 1. 连接服务器
 * 2. 发送玩家输入
 * 3. 接收帧数据并执行游戏逻辑
 * 4. 渲染游戏状态
 */
#pragma once
#include <iostream>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include "../common/Network.h"
#include "../common/Protocol.h"
#include "../common/GameWorld.h"

namespace lockstep {

class LockstepClient {
public:
    enum class State {
        DISCONNECTED,
        CONNECTING,
        WAITING,     // 等待其他玩家
        PLAYING,
        GAME_OVER
    };

private:
    UdpSocket socket_;
    sockaddr_in serverAddr_{};
    std::string serverHost_;
    uint16_t serverPort_;

    // 游戏状态
    GameWorld world_;
    uint32_t playerId_ = 0;
    State state_ = State::DISCONNECTED;

    // 帧队列
    std::queue<FrameData> pendingFrames_;
    std::mutex frameMutex_;

    // 输入状态
    std::atomic<uint8_t> moveDir_{8};      // 8=静止
    std::atomic<uint8_t> actions_{0};
    std::atomic<int32_t> targetX_{0};
    std::atomic<int32_t> targetY_{0};

    // 网络线程
    std::thread networkThread_;
    std::atomic<bool> running_{false};

    // 接收缓冲区
    static constexpr size_t RECV_BUFFER_SIZE = 65536;
    uint8_t recvBuffer_[RECV_BUFFER_SIZE];

    // 心跳
    std::chrono::steady_clock::time_point lastHeartbeat_;
    static constexpr int HEARTBEAT_INTERVAL_MS = 1000;

public:
    LockstepClient() = default;

    ~LockstepClient() {
        disconnect();
    }

    // ============ 连接服务器 ============
    bool connect(const std::string& host, uint16_t port) {
        serverHost_ = host;
        serverPort_ = port;

        init_network();

        if (!socket_.create()) {
            std::cerr << "[Client] Failed to create socket" << std::endl;
            return false;
        }

        socket_.setNonBlocking();
        serverAddr_ = UdpSocket::makeAddr(host, port);

        state_ = State::CONNECTING;
        running_ = true;

        // 启动网络线程
        networkThread_ = std::thread(&LockstepClient::networkLoop, this);

        // 发送加入请求
        std::vector<uint8_t> msg;
        writeU8(msg, static_cast<uint8_t>(MsgType::JOIN));
        socket_.sendTo(msg.data(), msg.size(), serverAddr_);

        std::cout << "[Client] Connecting to " << host << ":" << port << std::endl;

        return true;
    }

    void disconnect() {
        running_ = false;
        if (networkThread_.joinable()) {
            networkThread_.join();
        }
        socket_.close();
        cleanup_network();
        state_ = State::DISCONNECTED;
    }

    // ============ 设置输入 ============
    void setMoveDirection(uint8_t dir) {
        moveDir_ = dir;
    }

    void setAction(uint8_t actionBit, bool pressed) {
        if (pressed) {
            actions_ = actions_ | actionBit;
        } else {
            actions_ = actions_ & ~actionBit;
        }
    }

    void setTarget(int32_t x, int32_t y) {
        targetX_ = x;
        targetY_ = y;
    }

    // ============ 游戏主循环更新 ============
    bool update() {
        if (state_ != State::PLAYING) {
            return false;
        }

        // 发送当前输入
        sendInput();

        // 处理帧数据
        FrameData frame;
        bool hasFrame = false;

        {
            std::lock_guard<std::mutex> lock(frameMutex_);
            if (!pendingFrames_.empty()) {
                frame = pendingFrames_.front();
                pendingFrames_.pop();
                hasFrame = true;
            }
        }

        if (hasFrame) {
            // 执行游戏逻辑
            world_.tick(frame);

            // 校验（如果服务器发了checksum）
            if (frame.checksum != 0) {
                uint32_t localChecksum = world_.calcChecksum();
                if (localChecksum != frame.checksum) {
                    std::cerr << "[Client] DESYNC at frame " << frame.frameId
                              << "! Local=" << localChecksum
                              << " Server=" << frame.checksum << std::endl;
                }
            }

            // 检查游戏结束
            if (world_.isGameOver()) {
                state_ = State::GAME_OVER;
                int winnerId = world_.getWinnerId();
                if (winnerId == static_cast<int>(playerId_)) {
                    std::cout << "[Client] YOU WIN!" << std::endl;
                } else {
                    std::cout << "[Client] GAME OVER - Winner: Player " << winnerId << std::endl;
                }
            }

            return true;
        }

        return false;
    }

    // ============ Getter ============
    State getState() const { return state_; }
    uint32_t getPlayerId() const { return playerId_; }
    const GameWorld& getWorld() const { return world_; }
    uint32_t getCurrentFrame() const { return world_.currentFrame; }

    bool isPlaying() const { return state_ == State::PLAYING; }
    bool isConnected() const { return state_ != State::DISCONNECTED; }

private:
    // ============ 网络线程 ============
    void networkLoop() {
        lastHeartbeat_ = std::chrono::steady_clock::now();

        while (running_) {
            // 接收消息
            sockaddr_in fromAddr{};
            int len = socket_.recvFrom(recvBuffer_, RECV_BUFFER_SIZE, fromAddr);

            while (len > 0) {
                handleMessage(recvBuffer_, len);
                len = socket_.recvFrom(recvBuffer_, RECV_BUFFER_SIZE, fromAddr);
            }

            // 心跳
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lastHeartbeat_
            ).count();

            if (elapsed >= HEARTBEAT_INTERVAL_MS) {
                sendHeartbeat();
                lastHeartbeat_ = now;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    void handleMessage(const uint8_t* data, size_t len) {
        if (len < 1) return;

        MsgType type = static_cast<MsgType>(data[0]);

        switch (type) {
            case MsgType::JOIN_ACK:
                handleJoinAck(data + 1, len - 1);
                break;
            case MsgType::START:
                handleStart(data + 1, len - 1);
                break;
            case MsgType::FRAME:
                handleFrame(data + 1, len - 1);
                break;
            case MsgType::SYNC:
                handleSync(data + 1, len - 1);
                break;
            case MsgType::GAME_OVER:
                handleGameOver(data + 1, len - 1);
                break;
            default:
                break;
        }
    }

    // ============ 处理加入确认 ============
    void handleJoinAck(const uint8_t* data, size_t len) {
        if (len < 8) return;

        JoinAckMsg ack;
        size_t offset = 0;
        ack.deserialize(data, offset);

        playerId_ = ack.playerId;
        state_ = State::WAITING;

        std::cout << "[Client] Joined as Player " << playerId_
                  << " (" << ack.playerCount << " players in room)" << std::endl;
    }

    // ============ 处理游戏开始 ============
    void handleStart(const uint8_t* data, size_t len) {
        if (len < 8) return;

        StartMsg start;
        size_t offset = 0;
        start.deserialize(data, offset);

        // 初始化游戏世界
        world_.init(start.playerCount, start.randomSeed);

        state_ = State::PLAYING;

        std::cout << "[Client] Game started! Players: " << start.playerCount
                  << ", Seed: " << start.randomSeed << std::endl;
        std::cout << "[Client] You are Player " << playerId_ << std::endl;
    }

    // ============ 处理帧数据 ============
    void handleFrame(const uint8_t* data, size_t len) {
        FrameData frame;
        size_t offset = 0;
        frame.deserialize(data, offset, len);

        std::lock_guard<std::mutex> lock(frameMutex_);
        pendingFrames_.push(frame);
    }

    // ============ 处理同步数据 ============
    void handleSync(const uint8_t* data, size_t len) {
        if (len < 2) return;

        size_t offset = 0;
        uint16_t frameCount = readU16(data, offset);

        std::cout << "[Client] Receiving " << frameCount << " sync frames" << std::endl;

        std::lock_guard<std::mutex> lock(frameMutex_);

        for (uint16_t i = 0; i < frameCount && offset < len; ++i) {
            FrameData frame;
            frame.deserialize(data, offset, len);
            pendingFrames_.push(frame);
        }
    }

    // ============ 处理游戏结束 ============
    void handleGameOver(const uint8_t* data, size_t len) {
        state_ = State::GAME_OVER;
        std::cout << "[Client] Game Over!" << std::endl;
    }

    // ============ 发送输入 ============
    void sendInput() {
        PlayerInput input;
        input.playerId = playerId_;
        input.frameId = world_.currentFrame;
        input.moveDir = moveDir_.load();
        input.actions = actions_.load();
        input.targetX = targetX_.load();
        input.targetY = targetY_.load();

        std::vector<uint8_t> msg;
        writeU8(msg, static_cast<uint8_t>(MsgType::INPUT));
        input.serialize(msg);

        socket_.sendTo(msg.data(), msg.size(), serverAddr_);
    }

    // ============ 发送心跳 ============
    void sendHeartbeat() {
        std::vector<uint8_t> msg;
        writeU8(msg, static_cast<uint8_t>(MsgType::HEARTBEAT));
        writeU32(msg, playerId_);
        socket_.sendTo(msg.data(), msg.size(), serverAddr_);
    }
};

} // namespace lockstep
