#pragma once

#include "Multiplayer/Protocol/CombatHitMessage.h"

#include <cstddef>
#include <cstdint>

namespace fable::multiplayer::protocol
{
    bool EncodeCombatHitMessage(
        const CombatHitMessage& message,
        std::uint8_t* destination,
        std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept;
    bool DecodeCombatHitMessage(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        CombatHitMessage& message) noexcept;
}
