#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Combat/ResolvedHitEvent.h"
#include "Multiplayer/Combat/CombatTerminalTransitionState.h"
#include "Multiplayer/Replication/CombatHitDeliveryState.h"

#include <cstdint>

namespace fable::game::creature::combat
{
    class CreatureCombatService;
}

namespace fable::multiplayer::entities
{
    class EntityLifecycleReplication;
    class EntityNetworkIdentityRegistry;
    class EntityPresenceReplication;
}

namespace fable::multiplayer::replication
{
    class LocalHeroReplication;
    class RemotePlayerChannels;
}

namespace fable::multiplayer::protocol
{
    struct CombatHitMessage;
}

namespace fable::multiplayer::combat
{
    class PlayerCombatantDirectory;

    // Resolves a fenced network target to this process's current native
    // presentation. It replays the semantic victim response only; health and
    // death remain exclusively owned by EntityVitalsReplication so two
    // reliable streams can never race to write the same property.
    class CombatHitApplicator final
    {
    public:
        void Initialize(
            std::uint64_t localActorId,
            PlayerCombatantDirectory& combatants,
            entities::EntityLifecycleReplication& lifecycle,
            entities::EntityNetworkIdentityRegistry& identities,
            entities::EntityPresenceReplication& presence,
            replication::LocalHeroReplication& localHero,
            replication::RemotePlayerChannels& remotePlayers,
            game::creature::combat::CreatureCombatService& combat,
            const core::Diagnostics& diagnostics) noexcept;
        [[nodiscard]] bool ObserveNativeHit(
            const game::creature::combat::ResolvedHitEvent& event) noexcept;
        void ClearNativeHitObservations() noexcept;
        bool Apply(const protocol::CombatHitMessage& result) noexcept;
        void Shutdown() noexcept;

    private:
        [[nodiscard]] void* ResolveTarget(
            const protocol::CombatHitMessage& result) const noexcept;
        [[nodiscard]] void* ResolveSource(
            const protocol::CombatHitMessage& result) const noexcept;
        [[nodiscard]] bool ResolveLifecycle(
            std::uint64_t thingUid,
            CombatLifecycle& lifecycle) const noexcept;
        [[nodiscard]] bool IsLocalTargetAuthority(
            const protocol::CombatHitMessage& result) const noexcept;

        PlayerCombatantDirectory* combatants_ = nullptr;
        entities::EntityLifecycleReplication* lifecycle_ = nullptr;
        entities::EntityNetworkIdentityRegistry* identities_ = nullptr;
        entities::EntityPresenceReplication* presence_ = nullptr;
        replication::LocalHeroReplication* localHero_ = nullptr;
        replication::RemotePlayerChannels* remotePlayers_ = nullptr;
        game::creature::combat::CreatureCombatService* combat_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        std::uint64_t localActorId_ = 0;
        replication::CombatHitObservationCache nativeObservations_;
        CombatTerminalTransitionState terminalTransitions_;
    };
}
