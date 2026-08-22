#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/HeroPawn/Abilities/HeroAbility.h"

#include <cstdint>

namespace fable::game
{
    class EntityService;
}

namespace fable::game::hero_pawn::abilities
{
    class HeroWillAbilityService;

    class RemoteHeroAbilityController final
    {
    public:
        bool Initialize(
            game::EntityService& entities,
            HeroWillAbilityService& abilities,
            const core::Diagnostics& diagnostics) noexcept;
        [[nodiscard]] bool Bind(
            void* nativeHero,
            std::uint64_t actorId) noexcept;
        [[nodiscard]] bool Perform(
            HeroAbility ability,
            HeroAbilityCommand command,
            std::int32_t progressionState,
            void* targetCreature) noexcept;
        void Unbind() noexcept;
        void Shutdown() noexcept;

    private:
        game::EntityService* entities_ = nullptr;
        HeroWillAbilityService* abilities_ = nullptr;
        void* nativeHero_ = nullptr;
        std::uint64_t actorId_ = 0;
        bool nativeAbilityInventoryPresent_ = false;
        core::Diagnostics diagnostics_ = {};
        HeroAbility lastFailureAbility_ = HeroAbility::None;
        HeroAbilityCommand lastFailureCommand_ = HeroAbilityCommand::None;
    };
}
