#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Multiplayer/Protocol/PlayerActionMessage.h"
#include "Multiplayer/Protocol/PlayerState.h"
#include "Multiplayer/Presentation/RemotePlayerActionPresentation.h"
#include "Multiplayer/Replication/LocalPlayerActionCapture.h"
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
        // Drain native callbacks into bounded semantic records. This phase
        // may mark actor components dirty, but never publishes them; the
        // frame coordinator first captures and queues those component states
        // so dependent action events follow them on the same actor stream.
        bool CaptureLocalPending();
        // Publish already captured local action events after the actor-state
        // service has queued this frame's component patches.
        bool PublishLocalPending();
        // Present authoritative remote events after their preceding actor
        // patches have been reconciled into the native remote Hero.
        bool ReplayRemotePending();
        bool ProcessPending();
        bool HandleReliableMessage(
            const TransportMessage& message) override;
        void InvalidateActor(std::uint64_t actorId) noexcept;
        void InvalidateAllRemote() noexcept;
        void Shutdown() noexcept;

    private:
        static constexpr std::size_t PendingMessageCapacity = 1024;
        struct PendingPublication final
        {
            protocol::PlayerActionMessage message;
            std::uint64_t sourceConnectionNonce = 0;
        };

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
        bool PublishPending();
        bool ImportLocalCaptured();
        bool EnsurePresentationTiming(
            protocol::PlayerActionMessage& message,
            std::uint64_t observedAt,
            std::uint32_t durationMs);

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
        core::Diagnostics diagnostics_ = {};
        PeerRole role_ = PeerRole::Guest;
        std::uint64_t localActorId_ = 0;
        std::deque<PendingPublication> pendingMessages_;
        LocalPlayerActionCapture localCapture_;
        presentation::RemotePlayerActionPresentation presentation_;
        std::uint32_t nextPresentationRevision_ = 0;
        bool publishBackpressured_ = false;
        bool initialized_ = false;
    };
}
