#pragma once

#include <cstdint>

namespace fable::game::creature::combat
{
    struct CombatHealthMutationEvent final
    {
        void* creature = nullptr;
        std::uint64_t thingUid = 0;
        float previousHealth = 0.0f;
        float currentHealth = 0.0f;
        float maximumHealth = 0.0f;
        float requestedDelta = 0.0f;
        std::uint64_t observedAt = 0;
        bool combatFlag = false;
    };
}
