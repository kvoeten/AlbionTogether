#pragma once

#include "Game/HeroPawn/Abilities/HeroAbility.h"

#include <cstdint>

namespace fable::game::hero_pawn::abilities
{
    struct HeroAbilityEvent final
    {
        void* sourceCreature = nullptr;
        std::uint64_t sourceThingUid = 0;
        void* targetCreature = nullptr;
        std::uint64_t targetThingUid = 0;
        HeroAbility ability = HeroAbility::None;
        HeroAbilityCommand command = HeroAbilityCommand::None;
        std::int32_t progressionState = -1;
        std::uint32_t threadId = 0;
        std::uint64_t observedAt = 0;
    };
}
