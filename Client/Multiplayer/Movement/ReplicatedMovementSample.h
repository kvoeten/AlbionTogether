#pragma once

#include "Game/Math/Vector3.h"

#include <cstdint>
#include <string>

namespace fable::multiplayer::movement
{
    // Entity-neutral movement state. Player, NPC, and physics authority
    // channels translate their protocol data into this bounded current sample.
    struct ReplicatedMovementSample final
    {
        std::uint64_t actorId = 0;
        std::uint32_t authorityEpoch = 0;
        std::uint32_t sequence = 0;
        std::string mapName;
        game::Vector3 position = {};
        game::Vector3 velocity = {};
        float facing = 0.0f;
        float angularVelocity = 0.0f;
        bool moving = false;
        std::uint64_t receivedAt = 0;
    };
}
