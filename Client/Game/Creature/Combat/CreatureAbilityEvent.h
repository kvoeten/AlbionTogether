#pragma once

#include <cstdint>

namespace fable::game::creature::combat
{
    // Emitted at CThingCreature::SubmitAbility. The verified player ATTACK
    // caller is identified explicitly; indirect spell/ranged callers can use
    // the same semantic boundary without being mistaken for mouse input.
    struct CreatureAbilityEvent final
    {
        void* sourceCreature = nullptr;
        std::uint64_t sourceThingUid = 0;
        void* targetCreature = nullptr;
        std::uint64_t targetThingUid = 0;
        std::uint32_t abilityId = 0;
        std::uint32_t threadId = 0;
        float charge = 0.0f;
        std::uint64_t observedAt = 0;
        bool attackCommand = false;
    };
}
