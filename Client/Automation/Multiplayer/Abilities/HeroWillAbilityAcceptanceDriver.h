#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/HeroPawn/Abilities/HeroAbility.h"

#include <cstddef>
#include <cstdint>

namespace fable::game
{
    class EntityService;
}

namespace fable::game::hero_pawn::abilities
{
    class HeroWillAbilityService;
}

namespace fable::automation::multiplayer::abilities
{
    // Test-only ordered Will sequencer. It starts after the melee acceptance
    // exchange and submits real Hero ability setup/cancel requests one at a
    // time so each native action and its remote replay remain attributable.
    class HeroWillAbilityAcceptanceDriver final
    {
    public:
        void Initialize(
            bool enabled,
            bool hostRole,
            bool focused,
            game::EntityService& entities,
            game::hero_pawn::abilities::HeroWillAbilityService& abilities,
            const core::Diagnostics& diagnostics) noexcept;
        void Tick(
            bool remotePresentationReady,
            bool combatExchangeComplete);
        void Shutdown() noexcept;

    private:
        void Advance(std::uint64_t now) noexcept;
        void Fail(const char* reason) noexcept;

        game::EntityService* entities_ = nullptr;
        game::hero_pawn::abilities::HeroWillAbilityService* abilities_ =
            nullptr;
        core::Diagnostics diagnostics_ = {};
        std::uint64_t nextActionAt_ = 0;
        std::size_t abilityIndex_ = 0;
        unsigned int failedAttempts_ = 0;
        unsigned int acceptedUses_ = 0;
        unsigned int expectedUnsupported_ = 0;
        game::hero_pawn::abilities::HeroAbility activeAbility_ =
            game::hero_pawn::abilities::HeroAbility::None;
        bool hostRole_ = false;
        bool enabled_ = false;
        bool armed_ = false;
        bool progressionPrepared_ = false;
        bool cancelling_ = false;
        bool chargedActionObserved_ = false;
        bool chargedReleaseRequested_ = false;
        bool pillarOnly_ = false;
        bool completed_ = false;
    };
}
