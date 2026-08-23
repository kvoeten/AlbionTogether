#include "PlayerActorStateReplication.h"

#include "Multiplayer/Runtime/MultiplayerSessionContexts.h"
#include "Multiplayer/Protocol/PacketEnvelope.h"
#include "Multiplayer/Protocol/PlayerActorStateCodec.h"
#include "Multiplayer/Replication/LocalHeroReplication.h"
#include "Multiplayer/Replication/RemotePlayerChannels.h"
#include "Multiplayer/Transport/ReliableSinkDescriptorRegistry.h"
#include "Multiplayer/Transport/UdpPeer.h"

#include <Windows.h>

#include <cstdio>
#include <unordered_set>
#include <utility>

namespace
{
    bool IsNewer(const std::uint32_t candidate, const std::uint32_t current)
        noexcept
    {
        return current == 0 ||
            static_cast<std::int32_t>(candidate - current) > 0;
    }

    bool IsSaneLocal(const fable::multiplayer::PlayerState& state) noexcept
    {
        return state.actorId != 0 && state.authorityEpoch != 0 &&
            state.mapId != 0 && !state.playerId.empty() &&
            !state.mapName.empty() && !state.appearanceDefinition.empty() &&
            state.heroMorph.IsSane() && state.heroClothing.IsSane() &&
            state.heroBoneScales.IsSane() &&
            state.heroAppearanceModifiers.IsSane() &&
            state.heroEquipment.IsSane();
    }
}

namespace fable::multiplayer::replication
{
    void PlayerActorStateReplication::Initialize(
        const PeerRole role,
        const std::uint64_t localActorId,
        UdpPeer& transport,
        const std::uint32_t authorityEpoch,
        LocalHeroReplication& localHero,
        RemotePlayerChannels& remoteChannels,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        role_ = role;
        localActorId_ = localActorId;
        transport_ = &transport;
        localHero_ = &localHero;
        remoteChannels_ = &remoteChannels;
        diagnostics_ = diagnostics;
        publicationQueue_.Initialize(diagnostics_);
        localAuthorityEpoch_ = authorityEpoch == 0 ? 1 : authorityEpoch;
        initialized_ = true;
        diagnostics_.Event(
            "MultiplayerPlayerActorStateReady",
            "reliable replace-in-place player actor lifecycle is active");
    }

    bool PlayerActorStateReplication::Process()
    {
        if (!initialized_ || transport_ == nullptr || localHero_ == nullptr)
        {
            return false;
        }
        const std::uint64_t connectionNonce = transport_->ConnectionNonce();
        if (role_ == PeerRole::Guest)
        {
            const std::uint64_t peerRevision = transport_->PeerSetRevision();
            if (knownPeerRevision_ != 0 &&
                peerRevision != knownPeerRevision_)
            {
                lifecycles_.clear();
                publicationQueue_.Clear();
                if (remoteChannels_ != nullptr)
                {
                    remoteChannels_->Clear();
                }
                localActiveAcknowledged_ = false;
                localConstructSent_ = false;
                localRetired_ = false;
                diagnostics_.Event(
                    "MultiplayerPlayerActorStatePeerReconnect",
                    "host transport session changed; reopening actor baseline");
            }
            knownPeerRevision_ = peerRevision;
        }
        if (role_ == PeerRole::Guest && connectionNonce != 0 &&
            transportConnectionNonce_ != 0 &&
            transportConnectionNonce_ != connectionNonce)
        {
            // A transport restart is a new reliable session even when the
            // local Hero remains in the same map/incarnation. Re-open the
            // complete baseline and discard remote lifecycle state so late
            // packets from the previous session cannot resurrect actors.
            lifecycles_.clear();
            publicationQueue_.Clear();
            if (remoteChannels_ != nullptr)
            {
                remoteChannels_->Clear();
            }
            localActiveAcknowledged_ = false;
            localConstructSent_ = false;
            localRetired_ = false;
            diagnostics_.Event(
                "MultiplayerPlayerActorStateReconnect",
                "guest transport session changed; reopening actor baseline");
        }
        transportConnectionNonce_ = connectionNonce;
        const PlayerState* const state = localHero_->CurrentState();
        if (state != nullptr && IsSaneLocal(*state))
        {
            if (localRetired_ &&
                (state->actorGeneration != retiredGeneration_ ||
                 state->mapEpoch != retiredMapEpoch_))
            {
                localRetired_ = false;
                localConstructSent_ = false;
            }
            if (!EnsureLocalConstruct(*state) || !ReconcileLocal(*state))
            {
                return false;
            }
        }
        if (role_ == PeerRole::Host)
        {
            const std::uint64_t peerRevision = transport_->PeerSetRevision();
            if (peerRevision != knownPeerRevision_)
            {
                const std::vector<std::uint64_t> connected =
                    transport_->ConnectedActorIds();
                const std::unordered_set<std::uint64_t> connectedSet(
                    connected.begin(), connected.end());
                for (auto iterator = lifecycles_.begin();
                     iterator != lifecycles_.end();)
                {
                    if (iterator->second.role != PeerRole::Guest ||
                        connectedSet.find(iterator->first) !=
                            connectedSet.end())
                    {
                        ++iterator;
                        continue;
                    }
                    protocol::PlayerActorStateMessage retire = iterator->second;
                    retire.operation = protocol::PlayerActorStateOperation::Retire;
                    retire.componentFlags = 0;
                    retire.structuralRevision = NextRevision();
                    const std::uint64_t actorId = iterator->first;
                    if (!QueueAuthoritative(retire))
                    {
                        return false;
                    }
                    lifecycleConnectionNonces_.erase(actorId);
                    iterator = lifecycles_.erase(iterator);
                    char detail[160] = {};
                    std::snprintf(
                        detail, sizeof(detail),
                        "actor=%llu disconnected from host peer set",
                        static_cast<unsigned long long>(actorId));
                    diagnostics_.Event(
                        "MultiplayerPlayerActorStateRetired", detail);
                }
                if (publicationQueue_.Size() + lifecycles_.size() >
                    PlayerActorStatePublicationQueue::Capacity)
                {
                    diagnostics_.Event(
                        "MultiplayerPlayerActorStateOverflow",
                        "peer baseline exceeded the bounded actor lifecycle queue");
                    return false;
                }
                for (const auto& [actorId, lifecycle] : lifecycles_)
                {
                    (void)actorId;
                    if (!publicationQueue_.Append(lifecycle))
                    {
                        diagnostics_.Event(
                            "MultiplayerPlayerActorStateOverflow",
                            "peer baseline exceeded the bounded actor lifecycle queue");
                        return false;
                    }
                }
                knownPeerRevision_ = peerRevision;
            }
        }
        return PublishPending();
    }

    bool PlayerActorStateReplication::HandleReliableMessage(
        const TransportMessage& transportMessage)
    {
        if (!initialized_ || transport_ == nullptr ||
            transportMessage.type != protocol::PacketType::PlayerActorState)
        {
            return false;
        }
        protocol::PlayerActorStateMessage message;
        if (!protocol::DecodePlayerActorStateMessage(
                transportMessage.payload.data(),
                transportMessage.payloadSize,
                message))
        {
            diagnostics_.Event(
                "MultiplayerPlayerActorStateRejected", "invalid payload");
            return true;
        }
        if (role_ == PeerRole::Host)
        {
            return AcceptHost(std::move(message),
                transportMessage.sourceActorId,
                transportMessage.connectionNonce) && PublishPending();
        }
        return AcceptAuthoritative(
            message, transportMessage.connectionNonce);
    }

    bool PlayerActorStateReplication::EnsureLocalConstruct(
        const PlayerState& state)
    {
        if (localActiveAcknowledged_ || state.mapName.empty() || state.mapId == 0)
        {
            return true;
        }
        if (localRetired_)
        {
            return true;
        }
        if (role_ == PeerRole::Guest &&
            publicationQueue_.HasConstruct(localActorId_))
        {
            return true;
        }
        if (role_ == PeerRole::Guest && localConstructSent_)
        {
            return true;
        }
        if (!IsSaneLocal(state))
        {
            return true;
        }
        if (localMapName_.empty())
        {
            localMapName_ = state.mapName;
            localMapId_ = state.mapId;
        }
        protocol::PlayerActorStateMessage message = MakeLocalMessage(
            state, protocol::PlayerActorStateOperation::Construct);
        if (role_ == PeerRole::Host)
        {
            return AcceptHostLocal(std::move(message));
        }
        if (!Publish(std::move(message)))
        {
            return false;
        }
        localConstructSent_ = true;
        diagnostics_.Event(
            "MultiplayerPlayerActorStateConstructQueued",
            "guest actor baseline is awaiting host acknowledgment");
        return true;
    }

    bool PlayerActorStateReplication::ReconcileLocal(const PlayerState& state)
    {
        const auto current = lifecycles_.find(localActorId_);
        if (current == lifecycles_.end() || !localActiveAcknowledged_)
        {
            return true;
        }
        const bool mapChanged = state.mapName != localMapName_ ||
            state.mapId != localMapId_;
        const bool nativeIncarnationChanged =
            (state.actorGeneration != 0 &&
                state.actorGeneration != current->second.actorGeneration) ||
            (state.mapEpoch != 0 &&
                state.mapEpoch != current->second.mapEpoch);
        if (mapChanged || nativeIncarnationChanged)
        {
            localMapEpoch_ = state.mapEpoch != 0
                ? state.mapEpoch : localMapEpoch_ + 1;
            if (localMapEpoch_ == 0) localMapEpoch_ = 1;
            protocol::PlayerActorStateMessage retire = current->second;
            retire.operation = protocol::PlayerActorStateOperation::Retire;
            retire.componentFlags = 0;
            protocol::PlayerActorStateMessage construct = MakeLocalMessage(
                state, protocol::PlayerActorStateOperation::Construct);
            construct.mapEpoch = localMapEpoch_;
            construct.actorGeneration = state.actorGeneration != 0
                ? state.actorGeneration
                : current->second.actorGeneration + 1;
            if (construct.actorGeneration == 0)
            {
                construct.actorGeneration = 1;
            }
            const std::uint32_t destinationGeneration =
                construct.actorGeneration;
            construct.authorityEpoch = current->second.authorityEpoch;
            if (role_ == PeerRole::Host)
            {
                if (!AcceptHostLocal(std::move(retire)) ||
                    !AcceptHostLocal(std::move(construct)))
                {
                    return false;
                }
            }
            else if (!Publish(std::move(retire)) ||
                !Publish(std::move(construct)))
            {
                return false;
            }
            localActiveAcknowledged_ = role_ == PeerRole::Host;
            localConstructSent_ = role_ == PeerRole::Guest;
            localActorGeneration_ = destinationGeneration;
            localMapName_ = state.mapName;
            localMapId_ = state.mapId;
            return true;
        }

        const bool appearanceChanged = !SameAppearance(current->second, state);
        const bool equipmentChanged = !SameEquipment(current->second, state);
        if (!appearanceChanged && !equipmentChanged)
        {
            return true;
        }
        protocol::PlayerActorStateMessage delta = MakeLocalMessage(
            state, protocol::PlayerActorStateOperation::ComponentDelta);
        delta.actorGeneration = current->second.actorGeneration;
        delta.authorityEpoch = current->second.authorityEpoch;
        delta.mapEpoch = current->second.mapEpoch;
        delta.componentFlags = 0;
        if (appearanceChanged)
        {
            delta.componentFlags |= protocol::player_actor_state_flag::
                AppearanceChanged;
            if (state.heroMorph.IsSane() && state.heroClothing.IsSane() &&
                state.heroBoneScales.IsSane() &&
                state.heroAppearanceModifiers.IsSane())
            {
                delta.componentFlags |= protocol::player_actor_state_flag::
                    AppearancePresent;
            }
        }
        if (equipmentChanged)
        {
            delta.componentFlags |= protocol::player_actor_state_flag::
                EquipmentChanged;
            if (state.heroEquipment.IsSane())
            {
                delta.componentFlags |= protocol::player_actor_state_flag::
                    EquipmentPresent;
            }
        }
        if (role_ == PeerRole::Host)
        {
            return AcceptHostLocal(std::move(delta));
        }
        const protocol::PlayerActorStateMessage queued = delta;
        if (!Publish(std::move(delta)))
        {
            return false;
        }
        // Keep a bounded local submission watermark so a reliable packet that
        // is awaiting host echo is not re-enqueued every frame. The host
        // authoritative revision replaces this optimistic structural revision
        // when it arrives.
        protocol::PlayerActorStateMessage optimistic =
            MergeDelta(current->second, queued);
        // The guest intent revision is not comparable to the host's global
        // structural stream. Keep the last acknowledged revision while using
        // the merged value only as a duplicate-submission watermark.
        optimistic.structuralRevision = current->second.structuralRevision;
        lifecycles_[localActorId_] = std::move(optimistic);
        return true;
    }

    bool PlayerActorStateReplication::AcceptHost(
        protocol::PlayerActorStateMessage message,
        const std::uint64_t sourceActorId,
        const std::uint64_t sourceConnectionNonce)
    {
        if (sourceActorId == 0 || sourceActorId == localActorId_ ||
            sourceConnectionNonce == 0 || message.actorId != sourceActorId ||
            message.role != PeerRole::Guest)
        {
            diagnostics_.Event(
                "MultiplayerPlayerActorStateRejected",
                "guest actor lifecycle source or ownership did not match transport");
            return true;
        }
        const auto existing = lifecycles_.find(message.actorId);
        if (message.operation == protocol::PlayerActorStateOperation::Construct)
        {
            if ((message.componentFlags &
                    (protocol::player_actor_state_flag::AppearancePresent |
                     protocol::player_actor_state_flag::EquipmentPresent)) !=
                (protocol::player_actor_state_flag::AppearancePresent |
                 protocol::player_actor_state_flag::EquipmentPresent))
            {
                diagnostics_.Event(
                    "MultiplayerPlayerActorStateRejected",
                    "player actor construct omitted a required Hero component baseline");
                return true;
            }
            if (existing != lifecycles_.end() &&
                existing->second.authorityEpoch == message.authorityEpoch &&
                lifecycleConnectionNonces_[message.actorId] ==
                    sourceConnectionNonce)
            {
                // A retransmitted or stale Construct from the same owner
                // session cannot replace its current incarnation.
                return true;
            }
            if (existing != lifecycles_.end())
            {
                // The transport has rebound this stable actor ID to a new
                // connected endpoint/session. Retire the old authority before
                // constructing the replacement in the same reliable order.
                protocol::PlayerActorStateMessage retire = existing->second;
                retire.operation = protocol::PlayerActorStateOperation::Retire;
                retire.componentFlags = 0;
                retire.structuralRevision = NextRevision();
                if (!QueueAuthoritative(retire))
                {
                    return false;
                }
                lifecycles_.erase(existing);
                lifecycleConnectionNonces_.erase(message.actorId);
            }
            // The player owner authors its incarnation token; the host owns
            // connection validation and the global structural revision.
            lifecycleConnectionNonces_[message.actorId] =
                sourceConnectionNonce;
            message.structuralRevision = NextRevision();
            lifecycles_[message.actorId] = message;
            return QueueAuthoritative(message);
        }
        if (existing == lifecycles_.end() ||
            existing->second.authorityEpoch != message.authorityEpoch ||
            existing->second.actorGeneration != message.actorGeneration ||
            lifecycleConnectionNonces_[message.actorId] !=
                sourceConnectionNonce)
        {
            return true;
        }
        if (message.operation == protocol::PlayerActorStateOperation::
                MapTransition)
        {
            if (message.mapId == 0 || message.mapName.empty() ||
                !IsNewer(message.mapEpoch, existing->second.mapEpoch))
            {
                return true;
            }
            protocol::PlayerActorStateMessage transition = existing->second;
            transition.operation = message.operation;
            transition.componentFlags = 0;
            transition.mapEpoch = message.mapEpoch;
            transition.mapId = message.mapId;
            transition.mapName = message.mapName;
            transition.initialPosition = message.initialPosition;
            transition.initialFacing = message.initialFacing;
            transition.structuralRevision = NextRevision();
            lifecycles_[message.actorId] = transition;
            return QueueAuthoritative(transition);
        }
        if (message.operation == protocol::PlayerActorStateOperation::
                ComponentDelta)
        {
            if (message.mapEpoch != existing->second.mapEpoch ||
                message.mapId != existing->second.mapId ||
                message.mapName != existing->second.mapName ||
                (message.componentFlags &
                    (protocol::player_actor_state_flag::AppearanceChanged |
                     protocol::player_actor_state_flag::EquipmentChanged)) == 0)
            {
                return true;
            }
            message = MergeDelta(existing->second, message);
            message.structuralRevision = NextRevision();
            lifecycles_[message.actorId] = message;
            return QueueAuthoritative(message);
        }
        if (message.operation == protocol::PlayerActorStateOperation::Retire)
        {
            message = existing->second;
            message.operation = protocol::PlayerActorStateOperation::Retire;
            message.componentFlags = 0;
            message.structuralRevision = NextRevision();
            if (!QueueAuthoritative(message))
            {
                return false;
            }
            lifecycles_.erase(existing);
            lifecycleConnectionNonces_.erase(message.actorId);
            return true;
        }
        return true;
    }

    bool PlayerActorStateReplication::AcceptHostLocal(
        protocol::PlayerActorStateMessage message)
    {
        message.actorId = localActorId_;
        message.role = role_;
        const auto existing = lifecycles_.find(localActorId_);
        if (message.operation == protocol::PlayerActorStateOperation::Construct)
        {
            if (existing != lifecycles_.end())
            {
                localActiveAcknowledged_ = true;
                return true;
            }
            message.authorityEpoch = localAuthorityEpoch_;
            message.actorGeneration = message.actorGeneration != 0
                ? message.actorGeneration : NextGeneration();
            localActorGeneration_ = message.actorGeneration;
            lastLocalGeneration_ = localActorGeneration_;
            message.structuralRevision = NextRevision();
            lifecycles_[localActorId_] = message;
            lifecycleConnectionNonces_[localActorId_] =
                transport_ != nullptr ? transport_->ConnectionNonce() : 0;
            localActiveAcknowledged_ = true;
            localRetired_ = false;
        }
        else
        {
            if (existing == lifecycles_.end())
            {
                return true;
            }
            message.authorityEpoch = existing->second.authorityEpoch;
            message.actorGeneration = existing->second.actorGeneration;
            if (message.operation == protocol::PlayerActorStateOperation::
                    ComponentDelta)
            {
                message = MergeDelta(existing->second, message);
            }
            else if (message.operation == protocol::PlayerActorStateOperation::
                         MapTransition)
            {
                if (message.mapId == 0 || message.mapName.empty() ||
                    !IsNewer(message.mapEpoch, existing->second.mapEpoch))
                {
                    return true;
                }
                protocol::PlayerActorStateMessage transition = existing->second;
                transition.operation = message.operation;
                transition.componentFlags = 0;
                transition.mapEpoch = message.mapEpoch;
                transition.mapId = message.mapId;
                transition.mapName = message.mapName;
                transition.initialPosition = message.initialPosition;
                transition.initialFacing = message.initialFacing;
                message = std::move(transition);
            }
            message.structuralRevision = NextRevision();
            if (message.operation == protocol::PlayerActorStateOperation::Retire)
            {
                lifecycles_.erase(existing);
                lifecycleConnectionNonces_.erase(localActorId_);
                localActiveAcknowledged_ = false;
                localRetired_ = true;
            }
            else
            {
                lifecycles_[localActorId_] = message;
            }
        }
        return QueueAuthoritative(message);
    }

    bool PlayerActorStateReplication::AcceptAuthoritative(
        const protocol::PlayerActorStateMessage& message,
        const std::uint64_t sourceConnectionNonce)
    {
        const auto existing = lifecycles_.find(message.actorId);
        if (existing != lifecycles_.end() &&
            !IsNewer(message.structuralRevision,
                existing->second.structuralRevision))
        {
            return true;
        }
        if (message.operation == protocol::PlayerActorStateOperation::Retire)
        {
            lifecycles_.erase(message.actorId);
        }
        else if (message.operation == protocol::PlayerActorStateOperation::
                     ComponentDelta && existing != lifecycles_.end())
        {
            lifecycles_[message.actorId] = MergeDelta(existing->second, message);
        }
        else if (message.operation == protocol::PlayerActorStateOperation::
                     MapTransition && existing != lifecycles_.end())
        {
            if (message.mapId == 0 || message.mapName.empty() ||
                !IsNewer(message.mapEpoch, existing->second.mapEpoch))
            {
                return true;
            }
            protocol::PlayerActorStateMessage transition = existing->second;
            transition.operation = message.operation;
            transition.componentFlags = 0;
            transition.mapEpoch = message.mapEpoch;
            transition.mapId = message.mapId;
            transition.mapName = message.mapName;
            transition.initialPosition = message.initialPosition;
            transition.initialFacing = message.initialFacing;
            transition.structuralRevision = message.structuralRevision;
            lifecycles_[message.actorId] = std::move(transition);
        }
        else if (message.operation == protocol::PlayerActorStateOperation::
                     MapTransition)
        {
            return true;
        }
        else
        {
            lifecycles_[message.actorId] = message;
        }
        if (message.actorId == localActorId_ && message.role == role_ &&
            message.operation != protocol::PlayerActorStateOperation::Retire)
        {
            localActiveAcknowledged_ = true;
            localConstructSent_ = false;
            localRetired_ = false;
            localActorGeneration_ = message.actorGeneration;
            localAuthorityEpoch_ = message.authorityEpoch;
            localMapEpoch_ = message.mapEpoch;
            localMapName_ = message.mapName;
            localMapId_ = message.mapId;
        }
        else if (message.actorId == localActorId_ &&
            message.operation == protocol::PlayerActorStateOperation::Retire)
        {
            localActiveAcknowledged_ = false;
            localConstructSent_ = false;
            localRetired_ = true;
        }
        if (remoteChannels_ != nullptr && message.actorId != localActorId_)
        {
            remoteChannels_->ApplyActorState(
                message, GetTickCount64(), sourceConnectionNonce);
        }
        return true;
    }

    bool PlayerActorStateReplication::QueueAuthoritative(
        const protocol::PlayerActorStateMessage& message)
    {
        if (message.actorId != localActorId_ && remoteChannels_ != nullptr)
        {
            const auto source = lifecycleConnectionNonces_.find(
                message.actorId);
            remoteChannels_->ApplyActorState(
                message,
                GetTickCount64(),
                source != lifecycleConnectionNonces_.end()
                    ? source->second
                    : 0);
        }
        return Publish(message);
    }

    bool PlayerActorStateReplication::Publish(
        protocol::PlayerActorStateMessage message)
    {
        if (!publicationQueue_.Enqueue(
                std::move(message), &PlayerActorStateReplication::MergeDelta))
        {
            return false;
        }
        return PublishPending();
    }

    bool PlayerActorStateReplication::PublishPending()
    {
        return transport_ != nullptr &&
            publicationQueue_.PublishPending(*transport_);
    }

    protocol::PlayerActorStateMessage PlayerActorStateReplication::MakeLocalMessage(
        const PlayerState& state,
        const protocol::PlayerActorStateOperation operation)
    {
        protocol::PlayerActorStateMessage message;
        message.operation = operation;
        message.actorId = localActorId_;
        message.authorityEpoch = state.authorityEpoch != 0
            ? state.authorityEpoch : localAuthorityEpoch_;
        message.actorGeneration = state.actorGeneration != 0
            ? state.actorGeneration
            : (localActorGeneration_ == 0 ? 1 : localActorGeneration_);
        message.mapEpoch = state.mapEpoch != 0 ? state.mapEpoch : localMapEpoch_;
        message.structuralRevision = localIntentRevision_++;
        message.role = role_;
        message.mapId = state.mapId;
        message.initialPosition = state.position;
        message.initialFacing = state.facing;
        message.playerId = state.playerId;
        message.mapName = state.mapName;
        message.appearanceDefinition = state.appearanceDefinition;
        message.heroMorph = state.heroMorph;
        message.heroClothing = state.heroClothing;
        message.heroBoneScales = state.heroBoneScales;
        message.heroAppearanceModifiers = state.heroAppearanceModifiers;
        message.heroEquipment = state.heroEquipment;
        if (operation == protocol::PlayerActorStateOperation::Construct)
        {
            message.componentFlags = protocol::player_actor_state_flag::
                AppearanceChanged |
                protocol::player_actor_state_flag::EquipmentChanged |
                protocol::player_actor_state_flag::AppearancePresent |
                protocol::player_actor_state_flag::EquipmentPresent;
        }
        return message;
    }

    protocol::PlayerActorStateMessage PlayerActorStateReplication::MergeDelta(
        const protocol::PlayerActorStateMessage& current,
        const protocol::PlayerActorStateMessage& delta)
    {
        protocol::PlayerActorStateMessage merged = current;
        merged.operation = delta.operation;
        merged.componentFlags = delta.componentFlags;
        if ((delta.componentFlags & protocol::player_actor_state_flag::
                AppearanceChanged) != 0)
        {
            merged.appearanceDefinition = delta.appearanceDefinition;
            if ((delta.componentFlags & protocol::player_actor_state_flag::
                    AppearancePresent) != 0)
            {
                merged.heroMorph = delta.heroMorph;
                merged.heroClothing = delta.heroClothing;
                merged.heroBoneScales = delta.heroBoneScales;
                merged.heroAppearanceModifiers = delta.heroAppearanceModifiers;
            }
            else
            {
                merged.heroMorph = {};
                merged.heroClothing = {};
                merged.heroBoneScales = {};
                merged.heroAppearanceModifiers = {};
            }
        }
        if ((delta.componentFlags & protocol::player_actor_state_flag::
                EquipmentChanged) != 0)
        {
            merged.heroEquipment = (delta.componentFlags &
                protocol::player_actor_state_flag::EquipmentPresent) != 0
                ? delta.heroEquipment
                : game::hero_pawn::equipment::HeroEquipmentState{};
        }
        return merged;
    }

    std::uint32_t PlayerActorStateReplication::NextGeneration() noexcept
    {
        ++lastLocalGeneration_;
        if (lastLocalGeneration_ == 0)
        {
            lastLocalGeneration_ = 1;
        }
        return lastLocalGeneration_;
    }

    std::uint32_t PlayerActorStateReplication::NextRevision() noexcept
    {
        const std::uint32_t revision = nextStructuralRevision_++;
        if (nextStructuralRevision_ == 0)
        {
            nextStructuralRevision_ = 1;
        }
        return revision == 0 ? NextRevision() : revision;
    }

    bool PlayerActorStateReplication::SameAppearance(
        const protocol::PlayerActorStateMessage& left,
        const PlayerState& right) noexcept
    {
        return left.appearanceDefinition == right.appearanceDefinition &&
            left.heroMorph.Equals(right.heroMorph) &&
            left.heroClothing.Equals(right.heroClothing) &&
            left.heroBoneScales.Equals(right.heroBoneScales) &&
            left.heroAppearanceModifiers.Equals(right.heroAppearanceModifiers);
    }

    bool PlayerActorStateReplication::SameEquipment(
        const protocol::PlayerActorStateMessage& left,
        const PlayerState& right) noexcept
    {
        return left.heroEquipment.Equals(right.heroEquipment);
    }

    const protocol::PlayerActorStateMessage*
    PlayerActorStateReplication::Lifecycle(const std::uint64_t actorId)
        const noexcept
    {
        const auto iterator = lifecycles_.find(actorId);
        return iterator == lifecycles_.end() ? nullptr : &iterator->second;
    }

    const protocol::PlayerActorStateMessage*
        PlayerActorStateReplication::Lifecycle(
            const std::uint64_t actorId,
            const std::uint32_t actorGeneration,
            const std::uint32_t mapEpoch) const noexcept
    {
        const protocol::PlayerActorStateMessage* const lifecycle =
            Lifecycle(actorId);
        return lifecycle != nullptr &&
                lifecycle->actorGeneration == actorGeneration &&
                lifecycle->mapEpoch == mapEpoch
            ? lifecycle
            : nullptr;
    }

    bool PlayerActorStateReplication::IsLifecycleActive(
        const std::uint64_t actorId) const noexcept
    {
        return Lifecycle(actorId) != nullptr;
    }

    bool PlayerActorStateReplication::IsLifecycleActive(
        const std::uint64_t actorId,
        const std::uint32_t actorGeneration,
        const std::uint32_t mapEpoch) const noexcept
    {
        return Lifecycle(actorId, actorGeneration, mapEpoch) != nullptr;
    }

    bool PlayerActorStateReplication::IsLocalActiveAcknowledged() const noexcept
    {
        return localActiveAcknowledged_ &&
            IsLifecycleActive(localActorId_);
    }

    std::uint32_t PlayerActorStateReplication::LocalActorGeneration()
        const noexcept
    {
        return localActorGeneration_;
    }

    std::uint32_t PlayerActorStateReplication::LocalAuthorityEpoch()
        const noexcept
    {
        return localAuthorityEpoch_;
    }

    bool PlayerActorStateReplication::RetireLocal()
    {
        const auto current = lifecycles_.find(localActorId_);
        if (!initialized_ || current == lifecycles_.end())
        {
            return true;
        }
        protocol::PlayerActorStateMessage message = current->second;
        message.operation = protocol::PlayerActorStateOperation::Retire;
        message.componentFlags = 0;
        retiredGeneration_ = current->second.actorGeneration;
        retiredMapEpoch_ = current->second.mapEpoch;
        if (role_ == PeerRole::Host)
        {
            return AcceptHostLocal(std::move(message));
        }
        localActiveAcknowledged_ = false;
        localConstructSent_ = false;
        localRetired_ = true;
        lifecycles_.erase(current);
        return Publish(std::move(message));
    }

    void PlayerActorStateReplication::Shutdown() noexcept
    {
        lifecycles_.clear();
        lifecycleConnectionNonces_.clear();
        publicationQueue_.Clear();
        role_ = PeerRole::Guest;
        localActorId_ = 0;
        transport_ = nullptr;
        localHero_ = nullptr;
        remoteChannels_ = nullptr;
        diagnostics_ = {};
        knownPeerRevision_ = 0;
        nextStructuralRevision_ = 1;
        localMapEpoch_ = 1;
        localIntentRevision_ = 1;
        localActorGeneration_ = 0;
        lastLocalGeneration_ = 0;
        localAuthorityEpoch_ = 1;
        localMapName_.clear();
        localMapId_ = 0;
        localActiveAcknowledged_ = false;
        localConstructSent_ = false;
        localRetired_ = false;
        retiredGeneration_ = 0;
        retiredMapEpoch_ = 0;
        transportConnectionNonce_ = 0;
        initialized_ = false;
    }
}

namespace
{
    fable::multiplayer::ReliableMessageSink* ResolvePlayerActorStateSink(
        fable::multiplayer::MultiplayerSessionContexts& contexts) noexcept
    {
        return &contexts.players.actorState;
    }
}

FABLE_RELIABLE_SINK_DESCRIPTOR(
    g_fableReliableSinkPlayerActorState,
    0x100Du,
    "player-actor-state",
    210u,
    "multiplayer-player-actor-state-dispatch",
    ResolvePlayerActorStateSink);
