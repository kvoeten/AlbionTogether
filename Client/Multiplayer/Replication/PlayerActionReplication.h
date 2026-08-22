#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Actions/CreatureActionLifecycleEvent.h"
#include "Game/Creature/Combat/CreatureAbilityEvent.h"
#include "Game/HeroPawn/Abilities/HeroAbilityEvent.h"
#include "Multiplayer/Protocol/PlayerActionMessage.h"
#include "Multiplayer/Protocol/PlayerState.h"
#include "Multiplayer/Transport/ReliableMessageDispatcher.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>

namespace fable::game::creature::combat
{
    class CreatureCombatService;
}

namespace fable::game::creature::actions
{
    class CreatureActionLifecycleObserver;
}

namespace fable::game::hero_pawn::abilities
{
    class HeroWillAbilityService;
}

namespace fable::multiplayer
{
    class UdpPeer;
}

namespace fable::multiplayer::combat
{
    class PlayerCombatantDirectory;
}

namespace fable::multiplayer::entities
{
    class EntityNetworkIdentityRegistry;
    class EntityPresenceReplication;
}

namespace fable::multiplayer::presentation
{
    class RemotePlayerRegistry;
}

namespace fable::multiplayer::replication
{
    class LocalHeroReplication;
    class RemotePlayerChannels;

    // Actor-scoped one-shot actions are distinct from authoritative world-NPC
    // actions. The owning client proposes its Hero action, the host relays the
    // validated semantic event, and every observer replays it on that actor's
    // map-scoped remote Hero presentation.
    class PlayerActionReplication final : public ReliableMessageSink
    {
    public:
        void Initialize(
            PeerRole role,
            std::uint64_t localActorId,
            UdpPeer& transport,
            LocalHeroReplication& localHero,
            RemotePlayerChannels& remoteChannels,
            presentation::RemotePlayerRegistry& remotePlayers,
            entities::EntityNetworkIdentityRegistry& identities,
            entities::EntityPresenceReplication& presence,
            combat::PlayerCombatantDirectory& combatants,
            game::creature::combat::CreatureCombatService& combat,
            game::hero_pawn::abilities::HeroWillAbilityService& abilities,
            const core::Diagnostics& diagnostics);
        bool AttachActionObserver(
            game::creature::actions::CreatureActionLifecycleObserver&
                observer);
        bool ProcessPending();
        bool HandleReliableMessage(
            const TransportMessage& message) override;
        void Shutdown() noexcept;

    private:
        static constexpr std::size_t PendingEventCapacity = 1024;
        static constexpr std::size_t PendingMessageCapacity = 1024;
        static constexpr std::size_t PendingReplayCapacity = 256;
        static constexpr std::uint64_t ActionPairWindowMilliseconds = 250;
        static constexpr std::uint64_t
            WeaponTransitionCaptureWindowMilliseconds = 1'500;
        static constexpr std::uint64_t TargetResolutionGraceMilliseconds =
            1'000;
        static constexpr std::uint64_t NativeReplayFailureGraceMilliseconds =
            2'000;
        static constexpr std::uint64_t ReplayRetryMilliseconds = 50;
        struct PendingReplay final
        {
            protocol::PlayerActionMessage message;
            std::uint64_t queuedAt = 0;
            std::uint64_t nextAttemptAt = 0;
            bool diagnosticEmitted = false;
        };

        static void CaptureAbility(
            void* context,
            const game::creature::combat::CreatureAbilityEvent& event);
        static void CaptureAction(
            void* context,
            const game::creature::actions::CreatureActionLifecycleEvent&
                event);
        static void CaptureHeroAbility(
            void* context,
            const game::hero_pawn::abilities::HeroAbilityEvent& event);
        void EnqueueAbility(
            const game::creature::combat::CreatureAbilityEvent& event)
            noexcept;
        void EnqueueAction(
            const game::creature::actions::CreatureActionLifecycleEvent&
                event) noexcept;
        void EnqueueHeroAbility(
            const game::hero_pawn::abilities::HeroAbilityEvent& event)
            noexcept;
        bool PairAcceptedLocalActions();
        bool CaptureLocal(
            const game::creature::combat::CreatureAbilityEvent& event,
            const game::creature::actions::CreatureActionLifecycleEvent*
                resolvedAction = nullptr);
        bool CaptureLocalWeaponTransition(
            const game::creature::actions::CreatureActionLifecycleEvent&
                action,
            const game::hero_pawn::equipment::HeroEquipmentState& equipment);
        bool CaptureLocalHeroAbility(
            const game::hero_pawn::abilities::HeroAbilityEvent& event);
        bool AcceptIntent(
            protocol::PlayerActionMessage message,
            std::uint64_t sourceActorId);
        bool AcceptAuthoritative(
            protocol::PlayerActionMessage message);
        bool Queue(protocol::PlayerActionMessage message);
        bool QueueReplay(protocol::PlayerActionMessage message);
        bool PublishPending();
        bool ReplayPending();
        [[nodiscard]] std::uint64_t NextActionId() noexcept;

        UdpPeer* transport_ = nullptr;
        LocalHeroReplication* localHero_ = nullptr;
        RemotePlayerChannels* remoteChannels_ = nullptr;
        presentation::RemotePlayerRegistry* remotePlayers_ = nullptr;
        entities::EntityNetworkIdentityRegistry* identities_ = nullptr;
        entities::EntityPresenceReplication* presence_ = nullptr;
        combat::PlayerCombatantDirectory* combatants_ = nullptr;
        game::creature::combat::CreatureCombatService* combat_ =
            nullptr;
        game::hero_pawn::abilities::HeroWillAbilityService* abilities_ =
            nullptr;
        game::creature::actions::CreatureActionLifecycleObserver*
            actionObserver_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        PeerRole role_ = PeerRole::Guest;
        std::uint64_t localActorId_ = 0;
        std::uint64_t nextActionId_ = 0;
        std::mutex eventMutex_;
        std::deque<game::creature::combat::CreatureAbilityEvent>
            inboundAbilities_;
        std::deque<game::creature::actions::CreatureActionLifecycleEvent>
            inboundActions_;
        std::deque<game::hero_pawn::abilities::HeroAbilityEvent>
            inboundHeroAbilities_;
        std::deque<game::creature::combat::CreatureAbilityEvent>
            unmatchedAbilities_;
        std::deque<game::creature::actions::CreatureActionLifecycleEvent>
            unmatchedActions_;
        std::deque<game::creature::actions::CreatureActionLifecycleEvent>
            pendingWeaponTransitions_;
        std::deque<protocol::PlayerActionMessage> pendingMessages_;
        std::deque<PendingReplay> pendingReplays_;
        std::atomic_bool acceptingEvents_{false};
        std::atomic_uint droppedEvents_{0};
        bool publishBackpressured_ = false;
        bool initialized_ = false;
    };
}
