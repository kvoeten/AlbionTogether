#pragma once

#include <cstdint>

namespace fable::game::creature::combat
{
    // Emitted from the verified player ATTACK ability-submission caller. The
    // target is the current native Hero-targeting selection, not mouse input.
    struct PlayerAttackEvent final
    {
        void* sourceCreature = nullptr;
        void* targetCreature = nullptr;
        std::uint64_t targetThingUid = 0;
        std::uint32_t abilityId = 0;
        float charge = 0.0f;
        std::uint64_t observedAt = 0;
    };
}
