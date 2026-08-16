#pragma once

#include "Game/NPC/Simulation/DummyVillager/DummyVillagerState.h"

#include <cstdint>

namespace fable::game::npc::simulation
{
    struct DummyVillagerMutationEvent final
    {
        void* thing = nullptr;
        std::uint64_t thingUid = 0;
        DummyVillagerState previous = {};
        DummyVillagerState current = {};
        std::uint64_t observedAt = 0;
    };
}
