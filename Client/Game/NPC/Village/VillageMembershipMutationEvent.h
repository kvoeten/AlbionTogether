#pragma once

#include <cstdint>

namespace fable::game::npc::village
{
    struct VillageMembershipMutationEvent final
    {
        void* thing = nullptr;
        std::uint64_t thingUid = 0;
        std::uint64_t previousVillageUid = 0;
        std::uint64_t villageUid = 0;
        std::uint64_t observedAt = 0;
    };
}
