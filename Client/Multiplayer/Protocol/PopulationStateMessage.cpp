#include "PopulationStateMessage.h"

#include <cmath>
#include <cstring>
#include <type_traits>

namespace
{
#pragma pack(push, 1)
    struct WirePopulationStateMessage final
    {
        std::uint8_t region = 0;
        std::uint8_t active = 0;
        std::uint16_t reserved = 0;
        std::uint64_t revision = 0;
        std::int32_t targetCounts[3] = {};
        float regionFactors[4] = {};
    };
#pragma pack(pop)

    static_assert(std::is_trivially_copyable_v<WirePopulationStateMessage>);
    static_assert(sizeof(WirePopulationStateMessage) == 40);

    bool IsSane(
        const fable::multiplayer::protocol::PopulationStateMessage& message)
        noexcept
    {
        using Message =
            fable::multiplayer::protocol::PopulationStateMessage;
        if (message.region >= Message::RegionCount || message.revision == 0)
        {
            return false;
        }
        for (const std::int32_t count : message.targetCounts)
        {
            if (count < 0 || count > 10000)
            {
                return false;
            }
        }
        for (const float factor : message.regionFactors)
        {
            if (!std::isfinite(factor) || factor < -1000.0f ||
                factor > 1000.0f)
            {
                return false;
            }
        }
        return true;
    }
}

namespace fable::multiplayer::protocol
{
    bool EncodePopulationStateMessage(
        const PopulationStateMessage& message,
        std::uint8_t* destination,
        std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept
    {
        encodedSize = 0;
        if (!IsSane(message) || destination == nullptr ||
            destinationCapacity < sizeof(WirePopulationStateMessage))
        {
            return false;
        }
        WirePopulationStateMessage wire;
        wire.region = message.region;
        wire.active = message.active ? 1u : 0u;
        wire.revision = message.revision;
        std::memcpy(
            wire.targetCounts,
            message.targetCounts.data(),
            sizeof(wire.targetCounts));
        std::memcpy(
            wire.regionFactors,
            message.regionFactors.data(),
            sizeof(wire.regionFactors));
        std::memcpy(destination, &wire, sizeof(wire));
        encodedSize = sizeof(wire);
        return true;
    }

    bool DecodePopulationStateMessage(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        PopulationStateMessage& message) noexcept
    {
        message = {};
        if (bytes == nullptr ||
            byteCount != sizeof(WirePopulationStateMessage))
        {
            return false;
        }
        WirePopulationStateMessage wire;
        std::memcpy(&wire, bytes, sizeof(wire));
        if (wire.reserved != 0 || wire.active > 1)
        {
            return false;
        }
        message.region = wire.region;
        message.active = wire.active != 0;
        message.revision = wire.revision;
        std::memcpy(
            message.targetCounts.data(),
            wire.targetCounts,
            sizeof(wire.targetCounts));
        std::memcpy(
            message.regionFactors.data(),
            wire.regionFactors,
            sizeof(wire.regionFactors));
        return IsSane(message);
    }
}
