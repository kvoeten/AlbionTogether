#pragma once

#include "Game/Math/Vector3.h"

#include <cstdint>

namespace fable::multiplayer::protocol
{
    // Loss-tolerant, replace-in-place transform sample. Actor identity, map
    // membership, appearance, equipment, and retirement are established by
    // the reliable PlayerActorState lifecycle before this sample is accepted.
    struct PlayerMovementMessage final
    {
        std::uint64_t actorId = 0;
        std::uint32_t authorityEpoch = 0;
        std::uint32_t actorGeneration = 0;
        std::uint32_t mapEpoch = 0;
        std::uint32_t sequence = 0;
        std::uint16_t mapId = 0;
        bool moving = false;
        game::Vector3 position = {};
        game::Vector3 velocity = {};
        float facing = 0.0f;
        float angularVelocity = 0.0f;
    };
}
