#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Actions/Hooks/CreatureActionLifecycleObserver.h"
#include "Game/Creature/Combat/CreatureAbilityEvent.h"
#include "Multiplayer/Protocol/EntityActionMessage.h"
#include "Multiplayer/Protocol/PlayerState.h"
#include "Multiplayer/Replication/EntityActions/EntityActionCaptureQueue.h"
#include "Multiplayer/Replication/EntityActions/EntityActionPublicationQueue.h"
#include "Multiplayer/Replication/EntityActions/EntityActionReplay.h"
#include "Multiplayer/Transport/ReliableMessageDispatcher.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace fable::multiplayer
{
    class UdpPeer;
}

namespace fable::multiplayer::authority
{
    class AuthorityReplication;
}

namespace fable::multiplayer::entities
{
    class EntityLifecycleReplication;
    class EntityNetworkIdentityRegistry;
    class EntityPresenceReplication;
}

namespace fable::multiplayer::combat
{
    class CombatActionLedger;
    class PlayerCombatantDirectory;
}

namespace fable::game::creature::combat
{
    class CreatureCombatService;
}

namespace fable::multiplayer::replication
{
    // Converts accepted native creature actions into fenced semantic actions.
    // Native callbacks only enqueue; authority and transport remain game-thread
    // work. Unknown action classes are named but never serialized by memory.
    class EntityActionReplication final : public ReliableMessageSink
    {
    public:
        [[nodiscard]] ReliableMessageTypeSet HandledPacketTypes()
            const noexcept override
        {
            static constexpr protocol::PacketType types[] = {
                protocol::PacketType::EntityAction};
            return {types, sizeof(types) / sizeof(types[0])};
        }

        void Initialize(
            PeerRole role,
            std::uint64_t localActorId,
            UdpPeer& transport,
            authority::AuthorityReplication& authority,
            entities::EntityLifecycleReplication& lifecycle,
            entities::EntityNetworkIdentityRegistry& identities,
            entities::EntityPresenceReplication& presence,
            combat::PlayerCombatantDirectory& combatants,
            combat::CombatActionLedger& combatLedger,
            game::creature::combat::CreatureCombatService& combat,
            const core::Diagnostics& diagnostics);
        bool Attach(
            game::creature::actions::CreatureActionLifecycleObserver& observer);
        bool ProcessPending(
            const std::string& localMap,
            bool ownerRosterReady);
        bool HandleReliableMessage(
            const TransportMessage& message) override;
        void Shutdown() noexcept;

    private:
        enum class AbilityActionBinding : std::uint8_t
        {
            NotMatched,
            Handled,
            Failed,
        };

        // A newly materialized Fable creature can wait several seconds for
        // its first retail AI-brain dispatch. Keep the primary-attacker lease
        // alive long enough for that decision and its combat actions to remain
        // on the attacking peer before declaring the engagement idle.
        static constexpr std::uint64_t CombatIdleMilliseconds = 10'000;

        using ActiveAction = entity_actions::ActiveAction;

        bool ProcessEvent(
            const game::creature::actions::CreatureActionLifecycleEvent& event,
            const std::string& localMap,
            std::uint32_t mapEpoch);
        bool BeginLocalAction(
            const game::creature::actions::CreatureActionLifecycleEvent& event,
            const std::string& localMap,
            std::uint32_t mapEpoch);
        bool BeginLocalAbility(
            const game::creature::combat::CreatureAbilityEvent& event,
            const std::string& localMap,
            std::uint32_t mapEpoch);
        AbilityActionBinding BindLocalAbilityAction(
            const game::creature::actions::CreatureActionLifecycleEvent& event);
        void ForgetPendingAbility(const ActiveAction& action) noexcept;
        bool FinishLocalAction(void* nativeAction);
        bool BeginOrRefreshCombatEngagement(
            const game::creature::combat::CreatureAbilityEvent& event,
            const std::string& localMap,
            std::uint32_t mapEpoch);
        bool QueueUpdate(ActiveAction& action);
        bool ExpireCombatEngagements(std::uint64_t now);
        void ForgetCombatEngagement(const ActiveAction& action) noexcept;
        bool HostAcceptIntent(
            protocol::EntityActionMessage intent,
            std::uint64_t sourceActorId);
        bool AcceptAuthoritative(
            const protocol::EntityActionMessage& message);
        bool ReplayPendingAbilities();
        bool ReplayPendingNativeActions();
        bool PublishPeerBaseline();
        bool QueueEnd(ActiveAction& action);
        bool Queue(protocol::EntityActionMessage message);
        void TrackCombatAction(
            const protocol::EntityActionMessage& message) noexcept;
        void PruneFencedActions();
        bool FinalizeCompletedHostActions();
        [[nodiscard]] protocol::EntityActionMessage ToMessage(
            const ActiveAction& action,
            protocol::EntityActionPhase phase) const;
        [[nodiscard]] std::uint64_t NextActionId() noexcept;
        entity_actions::EntityActionCaptureQueue capture_;
        entity_actions::EntityActionPublicationQueue publication_;
        entity_actions::EntityActionReplay replay_;
        UdpPeer* transport_ = nullptr;
        authority::AuthorityReplication* authority_ = nullptr;
        entities::EntityLifecycleReplication* lifecycle_ = nullptr;
        entities::EntityNetworkIdentityRegistry* identities_ = nullptr;
        entities::EntityPresenceReplication* presence_ = nullptr;
        combat::PlayerCombatantDirectory* combatants_ = nullptr;
        combat::CombatActionLedger* combatLedger_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        PeerRole role_ = PeerRole::Guest;
        std::uint64_t localActorId_ = 0;
        std::uint64_t nextActionId_ = 0;
        std::uint64_t knownPeerRevision_ = 0;
        std::unordered_map<std::uint64_t, ActiveAction> activeActions_;
        std::unordered_map<void*, std::uint64_t> localActionIds_;
        std::unordered_map<std::uint64_t, std::uint64_t> combatActionIds_;
        // Exact runtime requests are indexed by scoped correlation ID. The
        // entity-keyed lane exists only for synthetic/legacy producers that
        // cannot establish a native submission scope.
        std::unordered_map<std::uint64_t, std::uint64_t>
            pendingAbilityCorrelations_;
        std::unordered_map<std::uint64_t, std::uint64_t>
            pendingLegacyAbilities_;
        unsigned int reportedDroppedEvents_ = 0;
        bool nonOwnerActionReported_ = false;
        bool initialized_ = false;
    };
}
