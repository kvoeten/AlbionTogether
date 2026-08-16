#pragma once

#include "Multiplayer/Protocol/EntityLowSimulationMessage.h"

#include <cstddef>
#include <cstdint>

namespace fable::multiplayer::protocol
{
    bool EncodeEntityLowSimulationMessage(
        const EntityLowSimulationMessage& message,
        std::uint8_t* destination,
        std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept;
    bool DecodeEntityLowSimulationMessage(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        EntityLowSimulationMessage& message) noexcept;
}
