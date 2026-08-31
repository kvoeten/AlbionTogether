#include "PlayerActionReplication.h"
#include "Multiplayer/Runtime/MultiplayerSessionContexts.h"
#include "Multiplayer/Transport/ReliableSinkDescriptorRegistry.h"

#include "Game/Creature/Combat/CreatureCombatService.h"
#include "Game/HeroPawn/Abilities/HeroWillAbilityService.h"
#include "Multiplayer/Combat/PlayerCombatantDirectory.h"
#include "Multiplayer/Combat/CombatActionLedger.h"
#include "Multiplayer/Presentation/RemotePlayerRegistry.h"
#include "Multiplayer/Entities/EntityNetworkIdentityRegistry.h"
#include "Multiplayer/Entities/EntityPresenceReplication.h"
#include "Multiplayer/Replication/LocalHeroReplication.h"
#include "Multiplayer/Replication/RemotePlayerChannels.h"
#include "Multiplayer/Transport/UdpPeer.h"

#include <Windows.h>

#include <array>
#include <algorithm>
#include <cstdio>
#include <unordered_set>
#include <utility>

namespace
{
    fable::multiplayer::ReliableMessageSink* ResolvePlayerActionSink(
        fable::multiplayer::MultiplayerSessionContexts& contexts) noexcept
    {
        return &contexts.actions.playerActions;
    }
}

FABLE_RELIABLE_SINK_DESCRIPTOR(
    g_fableReliableSinkPlayerAction,
    0x1004u,
    "player-action",
    400u,
    "multiplayer-player-action-dispatch",
    ResolvePlayerActionSink);

namespace fable::multiplayer::replication
{
    void PlayerActionReplication::Initialize(
        PeerRole role,
        std::uint64_t localActorId,
        UdpPeer& transport,
        LocalHeroReplication& localHero,
        RemotePlayerChannels& remoteChannels,
        presentation::RemotePlayerRegistry& remotePlayers,
        entities::EntityNetworkIdentityRegistry& identities,
        entities::EntityPresenceReplication& presence,
        multiplayer::combat::PlayerCombatantDirectory& combatants,
        multiplayer::combat::CombatActionLedger& combatLedger,
        game::creature::combat::CreatureCombatService& combat,
        game::hero_pawn::abilities::HeroWillAbilityService& abilities,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        role_ = role;
        localActorId_ = localActorId;
        transport_ = &transport;
        localHero_ = &localHero;
        remoteChannels_ = &remoteChannels;
        remotePlayers_ = &remotePlayers;
        identities_ = &identities;
        presence_ = &presence;
        combatants_ = &combatants;
        combatLedger_ = &combatLedger;
        combat_ = &combat;
        abilities_ = &abilities;
        diagnostics_ = diagnostics;
        if (!presentation_.Initialize(
                transport,
                localHero,
                remoteChannels,
                remotePlayers,
                presence,
                combatants,
                diagnostics,
                localActorId))
        {
            Shutdown();
            return;
        }
        localCapture_.Initialize(
            role,
            localActorId,
            transport,
            localHero,
            combatants,
            identities,
            combat,
            abilities,
            diagnostics);
        if (!localCapture_.IsInitialized())
        {
            // Local capture reports registration failures through its own
            // diagnostics and leaves no accepting ingress in that case.
            Shutdown();
            return;
        }
        initialized_ = true;
        diagnostics_.Event(
            "MultiplayerPlayerActionReady",
            "accepted Hero actions use native actor-scoped action submission");
    }

    bool PlayerActionReplication::AttachActionObserver(
        game::creature::actions::CreatureActionLifecycleObserver& observer)
    {
        if (!initialized_ || !localCapture_.AttachActionObserver(observer))
        {
            return false;
        }
        diagnostics_.Event(
            "MultiplayerPlayerActionLifecycleAttached",
            "input requests are causally paired with Fable's accepted native Hero action");
        return true;
    }

    bool PlayerActionReplication::AttachModeObserver(
        game::creature::locomotion::CreatureModeManagerObserver& observer)
    {
        if (!initialized_ || !localCapture_.AttachModeObserver(observer))
        {
            return false;
        }
        diagnostics_.Event(
            "MultiplayerPlayerRangedModeAttached",
            "native Hero ranged aim source 25 publishes ordered cancel state");
        return true;
    }

    bool PlayerActionReplication::CaptureLocalPending()
    {
        if (!initialized_ || transport_ == nullptr || localHero_ == nullptr ||
            remoteChannels_ == nullptr || remotePlayers_ == nullptr ||
            combat_ == nullptr || abilities_ == nullptr)
        {
            return false;
        }
        return localCapture_.CapturePending();
    }

    bool PlayerActionReplication::PublishLocalPending()
    {
        if (!initialized_ || transport_ == nullptr || localHero_ == nullptr ||
            remoteChannels_ == nullptr || remotePlayers_ == nullptr ||
            combat_ == nullptr || abilities_ == nullptr)
        {
            return false;
        }
        return ImportLocalCaptured() && PublishPending();
    }

    bool PlayerActionReplication::ReplayRemotePending()
    {
        if (!initialized_ || remoteChannels_ == nullptr ||
            remotePlayers_ == nullptr || localHero_ == nullptr)
        {
            return false;
        }
        return presentation_.Process();
    }

    bool PlayerActionReplication::ProcessPending()
    {
        return CaptureLocalPending() && PublishLocalPending() &&
            ReplayRemotePending();
    }

    bool PlayerActionReplication::EnsurePresentationTiming(
        protocol::PlayerActionMessage& message,
        const std::uint64_t observedAt,
        const std::uint32_t durationMs)
    {
        return transport_ != nullptr &&
            presentation::RemotePlayerActionPresentation::EnsureTiming(
                message, *transport_, observedAt, durationMs,
                nextPresentationRevision_);
    }

    bool PlayerActionReplication::ImportLocalCaptured()
    {
        while (const protocol::PlayerActionMessage* const message =
                   localCapture_.PendingFront())
        {
            if (!Queue(*message, 0))
            {
                return false;
            }
            localCapture_.PopPending();
        }
        return true;
    }

    bool PlayerActionReplication::HandleReliableMessage(
        const TransportMessage& transportMessage)
    {
        if (!initialized_ ||
            transportMessage.type != protocol::PacketType::PlayerAction)
        {
            return false;
        }
        protocol::PlayerActionMessage message;
        if (!protocol::DecodePlayerActionMessage(
                transportMessage.payload.data(),
                transportMessage.payloadSize,
                message))
        {
            diagnostics_.Event(
                "MultiplayerPlayerActionRejected",
                "invalid player action payload");
            return true;
        }
        if (message.kind == protocol::PlayerActionKind::HeroAbility)
        {
            char detail[192] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "source_actor_id=%llu owner_actor_id=%llu action_id=%llu ability_id=%u command=%u phase=%u",
                static_cast<unsigned long long>(
                    transportMessage.sourceActorId),
                static_cast<unsigned long long>(message.ownerActorId),
                static_cast<unsigned long long>(message.actionId),
                message.abilityId,
                static_cast<unsigned int>(message.heroAbilityCommand),
                static_cast<unsigned int>(message.phase));
            diagnostics_.Event(
                "MultiplayerRemoteHeroAbilityReceived", detail);
        }
        else if (message.kind == protocol::PlayerActionKind::AbilityRequest ||
            message.kind == protocol::PlayerActionKind::RangedAim ||
            message.kind == protocol::PlayerActionKind::RangedAimEnd ||
            message.kind == protocol::PlayerActionKind::Expression)
        {
            char detail[224] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "source_actor_id=%llu owner_actor_id=%llu action_id=%llu target_player=%llu target_uid=%016llX phase=%u",
                static_cast<unsigned long long>(
                    transportMessage.sourceActorId),
                static_cast<unsigned long long>(message.ownerActorId),
                static_cast<unsigned long long>(message.actionId),
                static_cast<unsigned long long>(
                    message.targetPlayerActorId),
                static_cast<unsigned long long>(message.targetThingUid),
                static_cast<unsigned int>(message.phase));
            diagnostics_.Event(
                "MultiplayerRemotePlayerActionReceived", detail);
        }
        if (role_ == PeerRole::Host)
        {
            if (message.phase != protocol::PlayerActionPhase::Intent ||
                message.ownerActorId != transportMessage.sourceActorId)
            {
                diagnostics_.Event(
                    "MultiplayerPlayerActionRejected",
                    "guest attempted to author an invalid player action");
                return true;
            }
            return AcceptIntent(
                std::move(message),
                transportMessage.sourceActorId,
                transportMessage.connectionNonce);
        }
        if (message.phase != protocol::PlayerActionPhase::Perform)
        {
            diagnostics_.Event(
                "MultiplayerPlayerActionRejected",
                "guest received a non-authoritative player action");
            return true;
        }
        return AcceptAuthoritative(
            std::move(message), transportMessage.connectionNonce);
    }

    bool PlayerActionReplication::AcceptIntent(
        protocol::PlayerActionMessage message,
        std::uint64_t sourceActorId,
        const std::uint64_t sourceConnectionNonce)
    {
        if (message.kind == protocol::PlayerActionKind::WeaponTransition)
        {
            diagnostics_.Event(
                "MultiplayerPlayerActionDiscarded",
                "legacy weapon transition intent ignored; equipment state is authoritative");
            return true;
        }
        const PlayerState* const owner = remoteChannels_->Find(sourceActorId);
        const RemotePlayerLifecycle* const lifecycle =
            remoteChannels_->FindLifecycle(sourceActorId);
        if (owner == nullptr || owner->actorId != message.ownerActorId ||
            owner->authorityEpoch != message.authorityEpoch ||
            lifecycle == nullptr || !lifecycle->active ||
            (lifecycle->connectionNonce != 0 &&
                lifecycle->connectionNonce != sourceConnectionNonce) ||
            lifecycle->actorGeneration != message.actorGeneration ||
            lifecycle->mapEpoch != message.mapEpoch ||
            owner->mapName != message.mapName)
        {
            diagnostics_.Event(
                "MultiplayerPlayerActionStale",
                "player action intent did not match its current actor channel");
            return true;
        }
        message.phase = protocol::PlayerActionPhase::Perform;
        if (!EnsurePresentationTiming(
                message,
                0,
                presentation::RemotePlayerActionPresentation::DefaultDurationMs(
                    message.kind)))
        {
            return false;
        }
        const protocol::PlayerActionMessage replayMessage = message;
        if (!Queue(message, sourceConnectionNonce) ||
            !presentation_.Offer(replayMessage, sourceConnectionNonce))
        {
            return false;
        }
        return PublishPending();
    }

    bool PlayerActionReplication::AcceptAuthoritative(
        protocol::PlayerActionMessage message,
        const std::uint64_t sourceConnectionNonce)
    {
        if (message.kind == protocol::PlayerActionKind::WeaponTransition)
        {
            diagnostics_.Event(
                "MultiplayerPlayerActionDiscarded",
                "legacy weapon transition perform ignored; equipment state is authoritative");
            return true;
        }
        if (message.ownerActorId == localActorId_)
        {
            // A guest publishes Intent and receives the host-approved Perform
            // for its own action. It must not replay that action locally, but
            // the source-owner hit resolver still needs the approved action
            // in its ledger before the native weapon sweep lands.
            return RecordCombatAction(
                message,
                "self-authoritative player action did not match the current actor lifecycle");
        }
        const RemotePlayerLifecycle* const lifecycle =
            remoteChannels_->FindLifecycle(message.ownerActorId);
        if (lifecycle != nullptr &&
            (!lifecycle->active ||
                (lifecycle->connectionNonce != 0 &&
                    lifecycle->connectionNonce != sourceConnectionNonce) ||
                lifecycle->actorGeneration !=
                message.actorGeneration || lifecycle->mapEpoch !=
                message.mapEpoch))
        {
            diagnostics_.Event(
                "MultiplayerPlayerActionStale",
                "authoritative player action did not match its lifecycle channel");
            return true;
        }
        if (!RecordCombatAction(
                message,
                "player action replay did not match the current actor lifecycle"))
        {
            return false;
        }
        return presentation_.Offer(std::move(message), sourceConnectionNonce);
    }

    bool PlayerActionReplication::RecordCombatAction(
        const protocol::PlayerActionMessage& message,
        const char* const rejectionDetail)
    {
        if (message.kind == protocol::PlayerActionKind::RangedAimEnd ||
            message.kind == protocol::PlayerActionKind::Expression)
        {
            return true;
        }
        const bool predictedLocalIntent =
            message.phase == protocol::PlayerActionPhase::Intent &&
            message.ownerActorId == localActorId_;
        if ((message.phase != protocol::PlayerActionPhase::Perform &&
                !predictedLocalIntent) || combatLedger_ == nullptr)
        {
            return true;
        }
        const combat::CombatSourceAction action{
            {combat::CombatSubjectKind::PlayerActor,
             message.ownerActorId,
             message.actorGeneration,
             message.mapEpoch},
            message.actionId,
            message.authorityEpoch};
        if (combatLedger_->Begin(action, GetTickCount64()) ||
            combatLedger_->IsCurrent(action))
        {
            return true;
        }
        diagnostics_.Event(
            "MultiplayerCombatActionLedgerRejected", rejectionDetail);
        return false;
    }

    bool PlayerActionReplication::Queue(
        protocol::PlayerActionMessage message,
        const std::uint64_t sourceConnectionNonce)
    {
        if (!RecordCombatAction(
                message,
                "player action perform did not match the current actor lifecycle"))
        {
            return false;
        }
        std::size_t actorMessages = 0;
        for (const auto& pending : pendingMessages_)
        {
            if (pending.message.ownerActorId == message.ownerActorId)
            {
                ++actorMessages;
            }
        }
        if (actorMessages >= PendingMessageCapacity / 4)
        {
            diagnostics_.Event(
                "MultiplayerPlayerActionOverflow",
                "bounded player action publication queue is full for actor lifecycle");
            return false;
        }
        if (pendingMessages_.size() >= PendingMessageCapacity)
        {
            diagnostics_.Event(
                "MultiplayerPlayerActionOverflow",
                "bounded player action publication queue is full");
            return false;
        }
        pendingMessages_.push_back({
            std::move(message), sourceConnectionNonce});
        return true;
    }

    bool PlayerActionReplication::PublishPending()
    {
        const std::size_t scheduled = pendingMessages_.size();
        std::unordered_set<std::uint64_t> attemptedActors;
        bool deferred = false;
        for (std::size_t attempt = 0;
             attempt < scheduled && !pendingMessages_.empty(); ++attempt)
        {
            const protocol::PlayerActionMessage& queued =
                pendingMessages_.front().message;
            const PlayerState* owner = queued.ownerActorId == localActorId_
                ? localHero_->CurrentState()
                : remoteChannels_->Find(queued.ownerActorId);
            const RemotePlayerLifecycle* lifecycle =
                queued.ownerActorId == localActorId_
                ? nullptr
                : remoteChannels_->FindLifecycle(queued.ownerActorId);
            const bool lifecycleMatches = queued.ownerActorId == localActorId_
                ? owner != nullptr && owner->actorGeneration ==
                        queued.actorGeneration && owner->mapEpoch ==
                        queued.mapEpoch
                : lifecycle != nullptr && lifecycle->active &&
                    lifecycle->actorGeneration == queued.actorGeneration &&
                    lifecycle->mapEpoch == queued.mapEpoch;
            if (owner == nullptr || owner->authorityEpoch !=
                    queued.authorityEpoch || !lifecycleMatches ||
                owner->mapName != queued.mapName)
            {
                diagnostics_.Event(
                    "MultiplayerPlayerActionStale",
                    "local player action no longer matched its lifecycle before publication");
                pendingMessages_.pop_front();
                continue;
            }
            if (!attemptedActors.insert(queued.ownerActorId).second)
            {
                pendingMessages_.push_back(
                    std::move(pendingMessages_.front()));
                pendingMessages_.pop_front();
                continue;
            }
            std::array<std::uint8_t, protocol::MaximumDatagramBytes> payload = {};
            std::size_t payloadSize = 0;
            if (!protocol::EncodePlayerActionMessage(
                    pendingMessages_.front().message,
                    payload.data(),
                    protocol::MaximumPayloadBytes(),
                    payloadSize))
            {
                return false;
            }
            if (!transport_->SubmitReliable(
                    reliable_stream::Actor(queued.ownerActorId),
                    protocol::PacketType::PlayerAction,
                    payload.data(),
                    payloadSize))
            {
                if (transport_->HasFailed())
                {
                    return false;
                }
                deferred = true;
                pendingMessages_.push_back(
                    std::move(pendingMessages_.front()));
                pendingMessages_.pop_front();
                continue;
            }
            if (pendingMessages_.front().message.kind ==
                    protocol::PlayerActionKind::HeroAbility)
            {
                char detail[160] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "actor_id=%llu action_id=%llu ability_id=%u command=%u",
                    static_cast<unsigned long long>(
                        pendingMessages_.front().message.ownerActorId),
                    static_cast<unsigned long long>(
                        pendingMessages_.front().message.actionId),
                    pendingMessages_.front().message.abilityId,
                    static_cast<unsigned int>(
                        pendingMessages_.front().message.heroAbilityCommand));
                diagnostics_.Event(
                    "MultiplayerLocalHeroAbilityPublished", detail);
            }
            else if (pendingMessages_.front().message.kind ==
                    protocol::PlayerActionKind::AbilityRequest ||
                pendingMessages_.front().message.kind ==
                    protocol::PlayerActionKind::RangedAim ||
                pendingMessages_.front().message.kind ==
                    protocol::PlayerActionKind::RangedAimEnd ||
                pendingMessages_.front().message.kind ==
                    protocol::PlayerActionKind::Expression)
            {
                char detail[192] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "actor_id=%llu action_id=%llu target_player=%llu target_uid=%016llX",
                    static_cast<unsigned long long>(
                        pendingMessages_.front().message.ownerActorId),
                    static_cast<unsigned long long>(
                        pendingMessages_.front().message.actionId),
                    static_cast<unsigned long long>(
                        pendingMessages_.front().message.targetPlayerActorId),
                    static_cast<unsigned long long>(
                        pendingMessages_.front().message.targetThingUid));
                diagnostics_.Event(
                    "MultiplayerLocalPlayerActionPublished", detail);
            }
            pendingMessages_.pop_front();
        }
        if (deferred && !publishBackpressured_)
        {
            diagnostics_.Event(
                "MultiplayerPlayerActionPublishDeferred",
                "one or more actor action streams are waiting for transport capacity");
            publishBackpressured_ = true;
        }
        else if (!deferred && publishBackpressured_)
        {
            diagnostics_.Event(
                "MultiplayerPlayerActionPublishResumed",
                "queued player actions entered the ordered transport");
            publishBackpressured_ = false;
        }
        return true;
    }

    void PlayerActionReplication::InvalidateActor(
        const std::uint64_t actorId) noexcept
    {
        if (actorId == 0)
        {
            return;
        }
        const PlayerState* const current = remoteChannels_ != nullptr
            ? remoteChannels_->Find(actorId)
            : nullptr;
        const RemotePlayerLifecycle* const lifecycle =
            remoteChannels_ != nullptr
                ? remoteChannels_->FindLifecycle(actorId)
                : nullptr;
        const auto isCurrentOwner = [&](
            const protocol::PlayerActionMessage& message,
            const std::uint64_t sourceConnectionNonce) noexcept
        {
            return current != nullptr && lifecycle != nullptr &&
                lifecycle->active &&
                current->authorityEpoch == message.authorityEpoch &&
                lifecycle->actorGeneration == message.actorGeneration &&
                lifecycle->mapEpoch == message.mapEpoch &&
                (lifecycle->connectionNonce == 0 ||
                    (sourceConnectionNonce != 0 &&
                        lifecycle->connectionNonce ==
                            sourceConnectionNonce));
        };
        for (auto pending = pendingMessages_.begin();
             pending != pendingMessages_.end();)
        {
            if ((pending->message.ownerActorId == actorId &&
                    !isCurrentOwner(
                        pending->message,
                        pending->sourceConnectionNonce)) ||
                pending->message.targetPlayerActorId == actorId)
            {
                pending = pendingMessages_.erase(pending);
            }
            else
            {
                ++pending;
            }
        }
        presentation_.InvalidateActor(actorId);
    }

    void PlayerActionReplication::InvalidateAllRemote() noexcept
    {
        for (auto pending = pendingMessages_.begin();
             pending != pendingMessages_.end();)
        {
            if (pending->message.ownerActorId != localActorId_ ||
                pending->message.targetPlayerActorId != 0)
            {
                pending = pendingMessages_.erase(pending);
            }
            else
            {
                ++pending;
            }
        }
        presentation_.InvalidateAllRemote();
    }

    void PlayerActionReplication::Shutdown() noexcept
    {
        localCapture_.Shutdown();
        pendingMessages_.clear();
        presentation_.Shutdown();
        transport_ = nullptr;
        localHero_ = nullptr;
        remoteChannels_ = nullptr;
        remotePlayers_ = nullptr;
        identities_ = nullptr;
        presence_ = nullptr;
        combatants_ = nullptr;
        combatLedger_ = nullptr;
        combat_ = nullptr;
        abilities_ = nullptr;
        diagnostics_ = {};
        role_ = PeerRole::Guest;
        localActorId_ = 0;
        nextPresentationRevision_ = 0;
        publishBackpressured_ = false;
        initialized_ = false;
    }
}
