#pragma once

#include "Multiplayer/Protocol/PlayerActorStateMessage.h"

#include <cstddef>
#include <cstdint>

namespace fable::multiplayer::protocol
{
    bool EncodePlayerActorStateMessage(
        const PlayerActorStateMessage& message,
        std::uint8_t* destination,
        std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept;
    bool DecodePlayerActorStateMessage(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        PlayerActorStateMessage& message) noexcept;
}
