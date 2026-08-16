#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace fable::multiplayer::protocol
{
    // Host-owned output of Fable's Albion low-detail random-population pass.
    // Region-local high-detail simulation consumes this bounded current state;
    // it is not an event history or an independently advancing guest model.
    struct PopulationStateMessage final
    {
        static constexpr std::size_t RegionCount = 4;
        static constexpr std::size_t PopulationKindCount = 3;

        std::uint8_t region = 0;
        bool active = false;
        std::uint64_t revision = 0;
        std::array<std::int32_t, PopulationKindCount> targetCounts = {};
        std::array<float, RegionCount> regionFactors = {};
    };

    bool EncodePopulationStateMessage(
        const PopulationStateMessage& message,
        std::uint8_t* destination,
        std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept;
    bool DecodePopulationStateMessage(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        PopulationStateMessage& message) noexcept;
}
