#pragma once

#include "Multiplayer/Protocol/PlayerMovementMessage.h"

#include <cstddef>
#include <cstdint>

namespace fable::multiplayer::protocol
{
    bool EncodePlayerMovementMessage(
        const PlayerMovementMessage& message,
        std::uint8_t* destination,
        std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept;

    bool DecodePlayerMovementMessage(
        const std::uint8_t* payload,
        std::size_t payloadSize,
        PlayerMovementMessage& message) noexcept;
}
