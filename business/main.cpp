#include <iostream>

#include "../common/MatchDomain.h"

using lockstep::FrameData;
using lockstep::GameWorld;
using lockstep::PlayerInput;
using lockstep::business::MatchRoom;

int main() {
    constexpr uint32_t seed = 20260912;
    MatchRoom room("ranked-demo-001", 2, 4);

    room.join(0, "Alice");
    room.join(1, "Bob");
    room.setReady(0, true);
    room.setReady(1, true);

    if (!room.start(seed)) {
        std::cerr << "failed to start match\n";
        return 1;
    }

    GameWorld world;
    world.init(2, seed);

    for (uint32_t frameId = 0; frameId < 90; ++frameId) {
        if (frameId == 35) room.disconnect(1);
        if (frameId == 45) room.reconnect(1);

        for (uint32_t playerId = 0; playerId < 2; ++playerId) {
            PlayerInput input;
            input.playerId = playerId;
            input.frameId = frameId;
            input.moveDir = static_cast<uint8_t>((frameId + playerId) % 8);
            input.actions = (frameId % 15 == 0) ? 0x01 : 0;
            room.submitInput(input, frameId);
        }

        FrameData frame = room.collectFrame(frameId);
        world.tick(frame);
        frame.checksum = world.calcChecksum();
        room.recordFrame(frame);
    }

    room.finish(0, world.currentFrame, "demo_timeout");
    room.printSummary(std::cout);
    std::cout << "deterministic_checksum=" << world.calcChecksum() << '\n';
    std::cout << "reconnect_flow=frame35:offline,frame45:reconnected\n";
    return 0;
}

