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

namespace fable::automation::multiplayer::combat
{
    // Test-only world driver. The host creates one ordinary hostile creature
    // after the multiplayer roster is stable so lifecycle replication, native
    // targeting, per-entity combat authority, and both player and NPC health
    // mutations can be exercised together.
    class CombatTargetAcceptanceDriver final
    {
    public:
        void Initialize(
            bool enabled,
            bool spawnTarget,
            game::EntityService& entities,
            game::CreatureService& creatures,
            game::NpcService& npcs,
            const core::Diagnostics& diagnostics) noexcept;
        void Tick(bool remotePresentationReady);
        void Shutdown() noexcept;

    private:
        game::EntityService* entities_ = nullptr;
        game::CreatureService* creatures_ = nullptr;
        game::NpcService* npcs_ = nullptr;
        game::Entity* target_ = nullptr;
        game::ScriptControl* hostSpawnControl_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        std::uint64_t armedAt_ = 0;
        std::uint64_t releaseHostSpawnControlAt_ = 0;
        std::uint64_t nextHealthMutationAt_ = 0;
        std::uint64_t nextTargetHealthMutationAt_ = 0;
        std::uint64_t nextAttemptAt_ = 0;
        unsigned int attempts_ = 0;
        unsigned int targetHealthMutations_ = 0;
        bool scriptRetained_ = false;
        bool spawnTarget_ = false;
        bool targetArmed_ = false;
        bool healthMutationApplied_ = false;
        bool enabled_ = false;
        bool completed_ = false;
    };
}
