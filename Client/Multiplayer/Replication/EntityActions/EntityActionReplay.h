#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Multiplayer/Replication/EntityActions/EntityActionState.h"

#include <cstdint>

namespace fable::game::creature::combat
{
    class CreatureCombatService;
}

namespace fable::multiplayer::combat
{
    class PlayerCombatantDirectory;
}

namespace fable::multiplayer::entities
{
    class EntityLifecycleReplication;
    class EntityPresenceReplication;
}

namespace fable::multiplayer::replication::entity_actions
{
    // Applies an already-authorized semantic entity action to the current
    // native Fable creature. Authority and transport remain outside this
    // class; it owns only lifecycle validation and native replay readiness.
    class EntityActionReplay final
    {
    public:
        void Initialize(
            std::uint64_t localActorId,
            entities::EntityLifecycleReplication& lifecycle,
            entities::EntityPresenceReplication& presence,
            combat::PlayerCombatantDirectory& combatants,
            game::creature::combat::CreatureCombatService& combat,
            const core::Diagnostics& diagnostics) noexcept;

        [[nodiscard]] bool ReplayAbility(ActiveAction& action);
        [[nodiscard]] bool ReplayNativeAction(ActiveAction& action);
        void Shutdown() noexcept;

    private:
        entities::EntityLifecycleReplication* lifecycle_ = nullptr;
        entities::EntityPresenceReplication* presence_ = nullptr;
        combat::PlayerCombatantDirectory* combatants_ = nullptr;
        game::creature::combat::CreatureCombatService* combat_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        std::uint64_t localActorId_ = 0;
    };
}
