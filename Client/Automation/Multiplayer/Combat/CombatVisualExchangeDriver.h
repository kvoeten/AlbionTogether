#pragma once

#include "Core/Diagnostics/Diagnostics.h"

#include <cstdint>

namespace fable::game
{
    class Entity;
    class EntityService;
}

namespace fable::game::creature::combat
{
    class CreatureCombatService;
}

namespace fable::automation::multiplayer::combat
{
    // Test-only semantic combat sequencer. Fixture creation and authority
    // checks remain in CombatTargetAcceptanceDriver; this class owns only the
    // visible, ordered exchange between both Heroes and the replicated enemy.
    class CombatVisualExchangeDriver final
    {
    public:
        void Initialize(
            bool enabled,
            bool hostRole,
            game::EntityService& entities,
            game::creature::combat::CreatureCombatService& combat,
            const core::Diagnostics& diagnostics) noexcept;
        void Tick(bool remotePresentationReady);
        [[nodiscard]] bool IsComplete() const noexcept;
        void Shutdown() noexcept;

    private:
        bool SubmitHeroAttack(
            void* heroThing,
            void* targetThing,
            const char* targetRole,
            unsigned int ordinal) noexcept;
        bool SubmitEnemyAttack(
            void* enemyThing,
            void* targetThing,
            const char* targetRole,
            unsigned int ordinal) noexcept;
        void Advance(std::uint64_t now, std::uint64_t delay) noexcept;

        game::EntityService* entities_ = nullptr;
        game::creature::combat::CreatureCombatService* combat_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        std::uint64_t fixtureReadyAt_ = 0;
        std::uint64_t nextActionAt_ = 0;
        unsigned int step_ = 0;
        unsigned int failedAttempts_ = 0;
        bool meleeRequested_ = false;
        bool meleeReady_ = false;
        bool hostRole_ = false;
        bool enabled_ = false;
        bool completed_ = false;
    };
}
