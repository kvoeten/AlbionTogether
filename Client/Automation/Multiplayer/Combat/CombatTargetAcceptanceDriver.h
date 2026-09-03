#pragma once

#include "Core/Diagnostics/Diagnostics.h"

#include <cstdint>

namespace fable::game
{
    class CreatureService;
    class Entity;
    class EntityService;
    class NpcService;
    class ScriptControl;
}

namespace fable::game::creature::combat
{
    class CreatureCombatService;
}

namespace fable::automation::multiplayer::combat
{
    // Test-only arena driver. The fixture already starts in the Chamber of
    // Fate; the launcher separates the overlapping Heroes through ordinary
    // player input, while this class owns the restrained target/combat
    // assertions.
    // The host creates one restrained combat
    // target so lifecycle replication, native targeting, per-entity combat
    // authority, and both player and NPC health mutations can be exercised
    // without unrelated town AI entering the fight.
    class CombatTargetAcceptanceDriver final
    {
    public:
        void Initialize(
            bool enabled,
            bool spawnTarget,
            bool targetOnly,
            game::EntityService& entities,
            game::CreatureService& creatures,
            game::creature::combat::CreatureCombatService& combat,
            game::NpcService& npcs,
            const core::Diagnostics& diagnostics) noexcept;
        void Tick(bool remotePresentationReady);
        [[nodiscard]] bool IsTargetReady() const noexcept;
        void AllowTargetDeath() noexcept;
        void Shutdown() noexcept;

    private:
        void MaintainAcceptanceTargetHealth(std::uint64_t now) noexcept;

        game::EntityService* entities_ = nullptr;
        game::CreatureService* creatures_ = nullptr;
        game::creature::combat::CreatureCombatService* combat_ = nullptr;
        game::NpcService* npcs_ = nullptr;
        game::Entity* target_ = nullptr;
        game::ScriptControl* hostSpawnControl_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        std::uint64_t armedAt_ = 0;
        std::uint64_t nextHealthMutationAt_ = 0;
        std::uint64_t nextTargetHealthMutationAt_ = 0;
        std::uint64_t nextTargetMaintenanceAt_ = 0;
        std::uint64_t nextAttemptAt_ = 0;
        std::uint64_t meleeRequestedAt_ = 0;
        std::uint64_t meleeReadyAt_ = 0;
        std::uint64_t nextAttackAttemptAt_ = 0;
        std::uint64_t nextWeaponTransitionAt_ = 0;
        unsigned int attempts_ = 0;
        unsigned int targetHealthMutations_ = 0;
        unsigned int targetHealthRestorations_ = 0;
        unsigned int sustainedAttackCount_ = 0;
        bool scriptRetained_ = false;
        bool peerStaged_ = false;
        bool arenaConverged_ = false;
        bool spawnTarget_ = false;
        bool targetOnly_ = false;
        bool targetArmed_ = false;
        bool meleeRequested_ = false;
        bool meleeReady_ = false;
        bool untargetedAttackSubmitted_ = false;
        bool nativeAttackSubmitted_ = false;
        bool sheatheRequested_ = false;
        bool sheatheReady_ = false;
        bool unarmedAttackSubmitted_ = false;
        bool redrawRequested_ = false;
        bool redrawReady_ = false;
        bool healthMutationApplied_ = false;
        bool maintainTargetHealth_ = true;
        bool enabled_ = false;
        bool completed_ = false;
    };
}
