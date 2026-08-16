#pragma once

#include <cstdint>
#include <string>

namespace fable::multiplayer::protocol
{
    struct EntityLowSimulationMessage final
    {
        std::uint64_t entityUid = 0;
        std::uint32_t entityGeneration = 0;
        std::uint64_t ownerActorId = 0;
        std::uint32_t mapEpoch = 0;
        std::uint32_t revision = 0;
        std::int32_t recreationDay = 0;
        std::int32_t recreationFrame = 0;
        bool respawnable = false;
        bool guard = false;
        std::string mapName;
    };
}
