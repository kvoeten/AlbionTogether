#pragma once

#include "Game/Math/Vector3.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace fable::multiplayer::protocol
{
    // One current, lossy movement sample for a host-generation-fenced Thing.
    // Reliability is intentionally provided by later samples, not retries.
    struct EntityMovementMessage final
    {
        std::uint64_t entityUid = 0;
        std::uint32_t entityGeneration = 0;
        std::uint64_t ownerActorId = 0;
        std::uint32_t mapEpoch = 0;
        std::uint32_t sequence = 0;
        std::string mapName;
        game::Vector3 position = {};
        game::Vector3 velocity = {};
        float facing = 0.0f;
        float angularVelocity = 0.0f;
        bool moving = false;
    };

    bool EncodeEntityMovementMessage(
        const EntityMovementMessage& message,
        std::uint8_t* destination,
        std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept;
    bool DecodeEntityMovementMessage(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        EntityMovementMessage& message) noexcept;
}
