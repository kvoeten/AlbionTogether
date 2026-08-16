#pragma once

#include <array>
#include <cstdint>

namespace fable::game::world::travel
{
    struct WorldTravelPreparation final
    {
        std::uint64_t sourceExitUid = 0;
        std::uint64_t destinationEntranceUid = 0;
        std::uint16_t sourceMapId = 0;
        std::uint16_t destinationMapId = 0;
        std::array<char, 96> destinationMapName = {};
    };
}
