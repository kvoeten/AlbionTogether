#pragma once

#include "Multiplayer/Protocol/EntityVitalsMessage.h"

#include <cstddef>
#include <cstdint>

namespace fable::multiplayer::protocol
{
    bool EncodeEntityVitalsMessage(
        const EntityVitalsMessage& message,
        std::uint8_t* destination,
        std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept;
    bool DecodeEntityVitalsMessage(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        EntityVitalsMessage& message) noexcept;
}
