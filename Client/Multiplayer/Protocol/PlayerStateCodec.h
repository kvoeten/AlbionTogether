#pragma once

#include "Multiplayer/Protocol/PlayerState.h"

#include <cstddef>
#include <cstdint>

namespace fable::multiplayer::protocol
{
    bool EncodePlayerState(
        const PlayerState& state,
        std::uint8_t* destination,
        std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept;
    bool DecodePlayerState(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        PlayerState& state) noexcept;
}
