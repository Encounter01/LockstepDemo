#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "GameWorld.h"

namespace lockstep {
namespace business {

enum class RoomState : uint8_t {
    WAITING = 0,
    PLAYING = 1,
    FINISHED = 2,
    CLOSED = 3,
};

enum class RejectReason : uint8_t {
    NONE = 0,
    ROOM_NOT_ACCEPTING,
    ROOM_FULL,
    DUPLICATE_PLAYER,
    PLAYER_NOT_FOUND,
    INVALID_FRAME,
    INVALID_INPUT,
    PLAYER_OFFLINE,
};

struct PlayerProfile {
    uint32_t playerId = 0;
    std::string nickname;
    bool ready = false;
    bool connected = true;
    uint32_t score = 0;
    uint32_t kills = 0;
    uint32_t deaths = 0;
};

struct MatchResult {
    int winnerId = -1;
    uint32_t endFrame = 0;
    std::string reason;
    std::vector<PlayerProfile> leaderboard;
};

struct ReplayFrame {
    uint32_t frameId = 0;
    std::vector<PlayerInput> inputs;
    uint32_t checksum = 0;
};

/**
 * 房间业务聚合根：集中处理入房、准备、断线保留、指令校验、结算和回放。
 * 网络层仍由 LockstepServer 负责，业务规则可以独立测试和复用。
 */
class MatchRoom {
public:
    MatchRoom(std::string roomId, uint32_t minPlayers = 2,
              uint32_t maxPlayers = 4)
        : roomId_(std::move(roomId)),
          minPlayers_(minPlayers),
          maxPlayers_(std::max(minPlayers, maxPlayers)) {}

    RejectReason join(uint32_t playerId, std::string nickname) {
        if (state_ != RoomState::WAITING) return RejectReason::ROOM_NOT_ACCEPTING;
        if (players_.size() >= maxPlayers_) return RejectReason::ROOM_FULL;
        if (players_.count(playerId) != 0) return RejectReason::DUPLICATE_PLAYER;

        PlayerProfile player;
        player.playerId = playerId;
        player.nickname = std::move(nickname);
        players_.emplace(playerId, std::move(player));
        return RejectReason::NONE;
    }

    RejectReason setReady(uint32_t playerId, bool ready) {
        auto it = players_.find(playerId);
        if (it == players_.end()) return RejectReason::PLAYER_NOT_FOUND;
        if (state_ != RoomState::WAITING) return RejectReason::ROOM_NOT_ACCEPTING;
        it->second.ready = ready;
        return RejectReason::NONE;
    }

    RejectReason disconnect(uint32_t playerId) {
        auto it = players_.find(playerId);
        if (it == players_.end()) return RejectReason::PLAYER_NOT_FOUND;
        it->second.connected = false;
        return RejectReason::NONE;
    }

    RejectReason reconnect(uint32_t playerId) {
        auto it = players_.find(playerId);
        if (it == players_.end()) return RejectReason::PLAYER_NOT_FOUND;
        it->second.connected = true;
        return RejectReason::NONE;
    }

    bool canStart() const {
        if (players_.size() < minPlayers_) return false;
        return std::all_of(players_.begin(), players_.end(),
                           [](const auto& entry) { return entry.second.ready; });
    }

    bool start(uint32_t seed, uint32_t startFrame = 0) {
        if (state_ != RoomState::WAITING || !canStart()) return false;
        state_ = RoomState::PLAYING;
        seed_ = seed;
        currentFrame_ = startFrame;
        return true;
    }

    RejectReason submitInput(const PlayerInput& input, uint32_t frameId) {
        auto it = players_.find(input.playerId);
        if (state_ != RoomState::PLAYING) return RejectReason::ROOM_NOT_ACCEPTING;
        if (it == players_.end()) return RejectReason::PLAYER_NOT_FOUND;
        if (!it->second.connected) return RejectReason::PLAYER_OFFLINE;
        if (frameId != currentFrame_ || input.frameId != frameId) {
            return RejectReason::INVALID_FRAME;
        }
        if (input.moveDir > 8) return RejectReason::INVALID_INPUT;
        pendingInputs_[input.playerId] = input;
        return RejectReason::NONE;
    }

    FrameData collectFrame(uint32_t frameId) {
        FrameData frame;
        frame.frameId = frameId;
        for (const auto& entry : players_) {
            PlayerInput input;
            input.playerId = entry.first;
            input.frameId = frameId;
            input.moveDir = 8;
            auto pending = pendingInputs_.find(entry.first);
            if (pending != pendingInputs_.end()) input = pending->second;
            frame.inputs.push_back(input);
        }
        pendingInputs_.clear();
        currentFrame_ = frameId + 1;
        return frame;
    }

    void recordFrame(const FrameData& frame) {
        replay_.push_back({frame.frameId, frame.inputs, frame.checksum});
    }

    bool finish(int winnerId, uint32_t endFrame, std::string reason) {
        if (state_ != RoomState::PLAYING) return false;
        state_ = RoomState::FINISHED;
        result_.winnerId = winnerId;
        result_.endFrame = endFrame;
        result_.reason = std::move(reason);
        result_.leaderboard.clear();
        for (const auto& entry : players_) result_.leaderboard.push_back(entry.second);
        std::sort(result_.leaderboard.begin(), result_.leaderboard.end(),
                  [](const PlayerProfile& lhs, const PlayerProfile& rhs) {
                      if (lhs.score != rhs.score) return lhs.score > rhs.score;
                      return lhs.playerId < rhs.playerId;
                  });
        return true;
    }

    void close() { state_ = RoomState::CLOSED; }

    const std::string& roomId() const { return roomId_; }
    RoomState state() const { return state_; }
    uint32_t seed() const { return seed_; }
    uint32_t currentFrame() const { return currentFrame_; }
    size_t playerCount() const { return players_.size(); }
    size_t replayFrameCount() const { return replay_.size(); }
    const MatchResult& result() const { return result_; }

    void printSummary(std::ostream& out) const {
        out << "room=" << roomId_ << " state=" << stateName()
            << " players=" << players_.size()
            << " frames=" << replay_.size();
        if (state_ == RoomState::FINISHED) {
            out << " winner=" << result_.winnerId
                << " reason=" << result_.reason;
        }
        out << '\n';
    }

private:
    const char* stateName() const {
        switch (state_) {
            case RoomState::WAITING: return "WAITING";
            case RoomState::PLAYING: return "PLAYING";
            case RoomState::FINISHED: return "FINISHED";
            case RoomState::CLOSED: return "CLOSED";
        }
        return "UNKNOWN";
    }

    std::string roomId_;
    uint32_t minPlayers_;
    uint32_t maxPlayers_;
    RoomState state_ = RoomState::WAITING;
    uint32_t seed_ = 0;
    uint32_t currentFrame_ = 0;
    std::map<uint32_t, PlayerProfile> players_;
    std::map<uint32_t, PlayerInput> pendingInputs_;
    std::vector<ReplayFrame> replay_;
    MatchResult result_;
};

} // namespace business
} // namespace lockstep

