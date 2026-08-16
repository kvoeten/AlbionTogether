#pragma once

#include "Multiplayer/Protocol/PacketEnvelope.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace fable::multiplayer
{
    struct TransportMessage final
    {
        protocol::PacketType type = protocol::PacketType::Authority;
        std::uint64_t sourceActorId = 0;
        std::uint32_t sequence = 0;
        std::size_t payloadSize = 0;
        std::array<
            std::uint8_t,
            protocol::MaximumDatagramBytes - protocol::PacketHeaderBytes>
            payload = {};
    };
}
