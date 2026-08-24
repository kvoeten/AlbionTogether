#pragma once

#include "Core/Diagnostics/Diagnostics.h"

#include <cstdint>

namespace fable::game
{
    class CreatureService;
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
            game::CreatureService& creatures,
            game::creature::combat::CreatureCombatService& combat,
            const core::Diagnostics& diagnostics) noexcept;
        void Tick(bool remotePresentationReady);
        [[nodiscard]] bool IsComplete() const noexcept;
        [[nodiscard]] bool WantsTargetDeath() const noexcept;
        void Shutdown() noexcept;

    private:
        bool SubmitHeroAttack(
            void* heroThing,
            void* targetThing,
            const char* targetRole,
            unsigned int ordinal,
            bool assignTarget = true) noexcept;
        bool SubmitPlayerTargetedHeroAttack(
            void* heroThing,
            void* targetThing,
            const char* targetRole,
            unsigned int ordinal,
            std::uint64_t now) noexcept;
        void CompleteVisualSequence() noexcept;
        bool ObserveTargetTerminal(game::Entity* target) noexcept;
        bool ExecuteTargetKill(
            game::Entity* target,
            void* heroThing,
            void* targetThing,
            std::uint64_t now) noexcept;
        bool SubmitEnemyAttack(
            void* enemyThing,
            void* targetThing,
            const char* targetRole,
            unsigned int ordinal) noexcept;
        void Advance(std::uint64_t now, std::uint64_t delay) noexcept;

        game::EntityService* entities_ = nullptr;
        game::CreatureService* creatures_ = nullptr;
        game::creature::combat::CreatureCombatService* combat_ = nullptr;
        game::Entity* target_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        std::uint64_t fixtureReadyAt_ = 0;
        std::uint64_t nextActionAt_ = 0;
        std::uint64_t targetKillStartedAt_ = 0;
        std::uint64_t targetUid_ = 0;
        unsigned int step_ = 0;
        unsigned int failedAttempts_ = 0;
        unsigned int targetKillAttackCount_ = 0;
        bool meleeRequested_ = false;
        bool meleeReady_ = false;
        bool playerTargetRequested_ = false;
        bool visualSequenceComplete_ = false;
        bool hostRole_ = false;
        bool enabled_ = false;
        bool completed_ = false;
    };
}
