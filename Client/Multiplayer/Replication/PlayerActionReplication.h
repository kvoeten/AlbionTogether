#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Actions/CreatureActionLifecycleEvent.h"
#include "Game/Creature/Combat/CreatureAbilityEvent.h"
#include "Game/HeroPawn/Abilities/HeroAbilityEvent.h"
#include "Multiplayer/Protocol/PlayerActionMessage.h"
#include "Multiplayer/Protocol/PlayerState.h"
#include "Multiplayer/Replication/PlayerActionEventQueue.h"
#include "Multiplayer/Transport/ReliableMessageDispatcher.h"

#include <cstddef>
#include <cstdint>
#include <deque>

namespace fable::game::creature::combat
{
    class CreatureCombatService;
}

namespace fable::game::creature::actions
{
    class CreatureActionLifecycleObserver;
}

namespace fable::game::creature::locomotion
{
    class CreatureModeManagerObserver;
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
    class CombatActionLedger;
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
        [[nodiscard]] ReliableMessageTypeSet HandledPacketTypes()
            const noexcept override
        {
            static constexpr protocol::PacketType types[] = {
                protocol::PacketType::PlayerAction};
            return {types, sizeof(types) / sizeof(types[0])};
        }

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
            combat::CombatActionLedger& combatLedger,
            game::creature::combat::CreatureCombatService& combat,
            game::hero_pawn::abilities::HeroWillAbilityService& abilities,
            const core::Diagnostics& diagnostics);
        bool AttachActionObserver(
            game::creature::actions::CreatureActionLifecycleObserver&
                observer);
        bool AttachModeObserver(
            game::creature::locomotion::CreatureModeManagerObserver&
                observer);
        bool ProcessPending();
        bool HandleReliableMessage(
            const TransportMessage& message) override;
        void InvalidateActor(std::uint64_t actorId) noexcept;
        void InvalidateAllRemote() noexcept;
        void Shutdown() noexcept;

    private:
        static constexpr std::size_t PendingEventCapacity = 1024;
        static constexpr std::size_t PendingMessageCapacity = 1024;
        static constexpr std::size_t PendingReplayCapacity = 256;
        static constexpr std::uint64_t ActionPairWindowMilliseconds = 250;
        static constexpr std::uint64_t
            WeaponTransitionCaptureWindowMilliseconds = 1'500;
        // Fable mutates CTCCarrying in several steps after accepting a
        // draw/stow action. Publish only after a mutation belonging to the
        // action has occurred and the carrying graph has been quiet long
        // enough to represent its final state.
        static constexpr std::uint64_t
            WeaponTransitionMutationSettleMilliseconds = 100;
        static constexpr std::uint64_t TargetResolutionGraceMilliseconds =
            1'000;
        static constexpr std::uint64_t NativeReplayFailureGraceMilliseconds =
            2'000;
        static constexpr std::uint64_t ReplayRetryMilliseconds = 50;
        struct PendingReplay final
        {
            protocol::PlayerActionMessage message;
            std::uint64_t queuedAt = 0;
            std::uint64_t nativeReadyAt = 0;
            std::uint64_t nextAttemptAt = 0;
            std::uint64_t sourceConnectionNonce = 0;
            bool diagnosticEmitted = false;
        };
        struct PendingPublication final
        {
            protocol::PlayerActionMessage message;
            std::uint64_t sourceConnectionNonce = 0;
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
        static void CaptureModeSource(
            void* context,
            const game::creature::locomotion::CreatureModeSourceEvent&
                event);
        bool PairAcceptedLocalActions();
        bool CaptureLocal(
            const game::creature::combat::CreatureAbilityEvent& event,
            const game::creature::actions::CreatureActionLifecycleEvent*
                resolvedAction = nullptr);
        bool CaptureLocalWeaponTransition(
            const game::creature::actions::CreatureActionLifecycleEvent&
                action,
            const game::hero_pawn::equipment::HeroEquipmentState& equipment);
        bool CaptureLocalRangedAction(
            const game::creature::actions::CreatureActionLifecycleEvent&
                action);
        bool CaptureLocalRangedAimEnd(
            const game::creature::locomotion::CreatureModeSourceEvent&
                event);
        bool CaptureLocalHeroAbility(
            const game::hero_pawn::abilities::HeroAbilityEvent& event);
        bool AcceptIntent(
            protocol::PlayerActionMessage message,
            std::uint64_t sourceActorId,
            std::uint64_t sourceConnectionNonce);
        bool AcceptAuthoritative(
            protocol::PlayerActionMessage message,
            std::uint64_t sourceConnectionNonce);
        bool RecordCombatAction(
            const protocol::PlayerActionMessage& message,
            const char* rejectionDetail);
        bool Queue(
            protocol::PlayerActionMessage message,
            std::uint64_t sourceConnectionNonce = 0);
        bool QueueReplay(
            protocol::PlayerActionMessage message,
            std::uint64_t sourceConnectionNonce);
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
        combat::CombatActionLedger* combatLedger_ = nullptr;
        game::creature::combat::CreatureCombatService* combat_ =
            nullptr;
        game::hero_pawn::abilities::HeroWillAbilityService* abilities_ =
            nullptr;
        game::creature::actions::CreatureActionLifecycleObserver*
            actionObserver_ = nullptr;
        game::creature::locomotion::CreatureModeManagerObserver*
            modeObserver_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        PeerRole role_ = PeerRole::Guest;
        std::uint64_t localActorId_ = 0;
        std::uint64_t nextActionId_ = 0;
        PlayerActionEventQueue eventQueue_;
        std::deque<game::creature::combat::CreatureAbilityEvent>
            unmatchedAbilities_;
        std::deque<game::creature::actions::CreatureActionLifecycleEvent>
            unmatchedActions_;
        std::deque<game::creature::actions::CreatureActionLifecycleEvent>
            pendingWeaponTransitions_;
        std::deque<PendingPublication> pendingMessages_;
        std::deque<PendingReplay> pendingReplays_;
        bool publishBackpressured_ = false;
        bool initialized_ = false;
    };
}
