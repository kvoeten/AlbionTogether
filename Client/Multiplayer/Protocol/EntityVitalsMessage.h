#pragma once

#include <cstdint>
#include <string>

namespace fable::multiplayer::protocol
{
    enum class EntityVitalsSubject : std::uint8_t
    {
        None = 0,
        Player = 1,
        WorldEntity = 2,
    };

    struct EntityVitalsMessage final
    {
        EntityVitalsSubject subject = EntityVitalsSubject::None;
        std::uint64_t playerActorId = 0;
        std::uint64_t entityUid = 0;
        std::uint32_t entityGeneration = 0;
        std::uint64_t ownerActorId = 0;
        std::uint32_t mapEpoch = 0;
        std::uint32_t revision = 0;
        float currentHealth = 0.0f;
        float maximumHealth = 0.0f;
        std::string mapName;
    };
}
