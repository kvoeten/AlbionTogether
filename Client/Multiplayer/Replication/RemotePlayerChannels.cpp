#include "RemotePlayerChannels.h"

#include "Multiplayer/Protocol/EquipmentTransitionTiming.h"
#include "Multiplayer/Replication/PlayerActorLifecycleReducer.h"

#include <cmath>
#include <utility>

namespace
{
    bool IsNewerSequence(
        std::uint32_t candidate,
        std::uint32_t current) noexcept
    {
        return current == 0 ||
            static_cast<std::int32_t>(candidate - current) > 0;
    }

    float NormalizeFacing(float facing) noexcept
    {
        if (!std::isfinite(facing))
        {
            return 0.0f;
        }
        facing -= std::floor(facing);
        return facing < 0.0f ? facing + 1.0f : facing;
    }

    bool IsSameIncarnation(
        const fable::multiplayer::replication::RemotePlayerLifecycle& current,
        const fable::multiplayer::protocol::PlayerActorStateMessage& incoming)
        noexcept
    {
        return current.actorGeneration == incoming.actorGeneration &&
            current.mapEpoch == incoming.mapEpoch;
    }

    fable::multiplayer::protocol::PlayerActorStateMessage
        LifecycleMessage(
            const fable::multiplayer::replication::RemotePlayerSnapshot&
                snapshot)
    {
        using namespace fable::multiplayer;
        protocol::PlayerActorStateMessage message;
        message.operation = protocol::PlayerActorStateOperation::Construct;
        message.actorId = snapshot.state.actorId;
        message.authorityEpoch = snapshot.state.authorityEpoch;
        message.actorGeneration = snapshot.lifecycle.actorGeneration;
        message.mapEpoch = snapshot.lifecycle.mapEpoch;
        message.structuralRevision =
            snapshot.lifecycle.structuralRevision;
        message.role = snapshot.state.role;
        message.mapId = snapshot.state.mapId;
        message.initialPosition = snapshot.state.position;
        message.initialFacing = snapshot.state.facing;
        message.playerId = snapshot.state.playerId;
        message.mapName = snapshot.state.mapName;
        message.appearanceDefinition = snapshot.state.appearanceDefinition;
        message.heroMorph = snapshot.state.heroMorph;
        message.heroClothing = snapshot.state.heroClothing;
        message.heroBoneScales = snapshot.state.heroBoneScales;
        message.heroAppearanceModifiers =
            snapshot.state.heroAppearanceModifiers;
        message.heroEquipment = snapshot.state.heroEquipment;
        message.componentFlags =
            (snapshot.lifecycle.appearancePresent
                ? protocol::player_actor_state_flag::AppearancePresent
                : 0) |
            (snapshot.lifecycle.equipmentPresent
                ? protocol::player_actor_state_flag::EquipmentPresent
                : 0);
        return message;
    }

    fable::multiplayer::replication::RemoteEquipmentTransition
        MaterializeEquipmentTransition(
            const fable::multiplayer::protocol::PlayerActorStateMessage&
                message,
            const std::uint64_t receivedAt,
            const fable::multiplayer::protocol::SessionTimeMs sessionNow)
        noexcept
    {
        using namespace fable::multiplayer;
        replication::RemoteEquipmentTransition result;
        if (!protocol::equipment_transition_timing::HasValidMetadata(
                message.heroEquipment.transitionActionId,
                message.transitionStartedAtSessionTimeMs,
                message.transitionAnimationId,
                message.transitionDurationMs,
                message.attachmentNotifyOffsetMs))
        {
            return result;
        }
        result.actionId = message.heroEquipment.transitionActionId;
        result.animationId = message.transitionAnimationId;
        result.durationMs = message.transitionDurationMs;
        result.attachmentNotifyOffsetMs =
            message.attachmentNotifyOffsetMs;
        result.startedAtLocalMs = receivedAt;
        if (sessionNow != protocol::SessionTimeUnset)
        {
            if (!protocol::equipment_transition_timing::ProjectStartToLocal(
                    receivedAt,
                    sessionNow,
                    message.transitionStartedAtSessionTimeMs,
                    result.startedAtLocalMs))
            {
                return {};
            }
        }
        return result;
    }
}

namespace fable::multiplayer::replication
{
    bool RemotePlayerChannels::ApplyActorState(
        const protocol::PlayerActorStateMessage& message,
        std::uint64_t receivedAt,
        const std::uint64_t connectionNonce,
        const protocol::SessionTimeMs sessionNow)
    {
        if (message.actorId == 0 || message.authorityEpoch == 0 ||
            message.actorGeneration == 0 || message.mapEpoch == 0 ||
            message.structuralRevision == 0)
        {
            return false;
        }

        const auto existing = channels_.find(message.actorId);
        const bool hasExisting = existing != channels_.end();
        if (message.operation == protocol::PlayerActorStateOperation::Construct &&
            !hasExisting && channels_.size() >= MaxTrackedActors)
        {
            return false;
        }
        if (message.operation == protocol::PlayerActorStateOperation::Retire)
        {
            if (!hasExisting)
            {
                return true;
            }
            if (existing->second.state.authorityEpoch != message.authorityEpoch ||
                !IsSameIncarnation(existing->second.lifecycle, message) ||
                (existing->second.lifecycle.connectionNonce != 0 &&
                    (connectionNonce == 0 || connectionNonce !=
                        existing->second.lifecycle.connectionNonce)))
            {
                return false;
            }
            const protocol::PlayerActorStateMessage current =
                LifecycleMessage(existing->second);
            protocol::PlayerActorStateMessage next;
            const auto reduction = PlayerActorLifecycleReducer::Reduce(
                &current, message, next);
            if (reduction != PlayerActorLifecycleReduction::Applied)
            {
                return reduction == PlayerActorLifecycleReduction::Ignored &&
                    message.structuralRevision ==
                        existing->second.lifecycle.structuralRevision;
            }
            if (!allActorsInvalidated_ && invalidatedActors_.size() < 1024)
            {
                invalidatedActors_.insert(message.actorId);
            }
            else if (!allActorsInvalidated_)
            {
                invalidatedActors_.clear();
                allActorsInvalidated_ = true;
            }
            channels_.erase(existing);
            return true;
        }
        if (message.playerId.empty() || message.mapName.empty() ||
            message.appearanceDefinition.empty())
        {
            return false;
        }

        if (hasExisting &&
            IsSameIncarnation(existing->second.lifecycle, message) &&
            message.authorityEpoch != existing->second.state.authorityEpoch)
        {
            // Authority changes must be accompanied by a new actor
            // incarnation; otherwise an old presentation could be reused by
            // a newer authority without a fresh Construct baseline.
            return false;
        }
        if (hasExisting &&
            message.operation != protocol::PlayerActorStateOperation::
                Construct &&
            message.authorityEpoch != existing->second.state.authorityEpoch)
        {
            return false;
        }
        if (hasExisting &&
            message.operation != protocol::PlayerActorStateOperation::Construct &&
            existing->second.lifecycle.connectionNonce != 0 &&
            (connectionNonce == 0 ||
                connectionNonce !=
                    existing->second.lifecycle.connectionNonce))
        {
            return false;
        }

        protocol::PlayerActorStateMessage current;
        if (hasExisting)
        {
            current = LifecycleMessage(existing->second);
        }
        if (hasExisting && message.operation ==
                protocol::PlayerActorStateOperation::Construct &&
            PlayerActorLifecycleReducer::IsOlderIncarnation(current, message))
        {
            return false;
        }
        const bool replacementConstruct = hasExisting &&
            message.operation ==
                protocol::PlayerActorStateOperation::Construct &&
            connectionNonce != 0 &&
            existing->second.lifecycle.connectionNonce != 0 &&
            connectionNonce != existing->second.lifecycle.connectionNonce;
        protocol::PlayerActorStateMessage reduced;
        const auto reduction = PlayerActorLifecycleReducer::Reduce(
            hasExisting && !replacementConstruct ? &current : nullptr,
            message,
            reduced);
        if (reduction != PlayerActorLifecycleReduction::Applied)
        {
            if (!hasExisting)
            {
                return false;
            }
            return reduction == PlayerActorLifecycleReduction::Ignored &&
                message.structuralRevision ==
                existing->second.lifecycle.structuralRevision;
        }
        const protocol::PlayerActorStateMessage& canonical = reduced;

        if (message.operation == protocol::PlayerActorStateOperation::Construct)
        {
            // A remote Hero is only valid when both mandatory presentation
            // components are present in its construction baseline. Do not
            // materialize a partial actor that could later accept movement,
            // actions, or vitals against an incomplete native graph.
            if (!message.AppearancePresent() || !message.EquipmentPresent())
            {
                return false;
            }
            if (hasExisting && IsSameIncarnation(
                    existing->second.lifecycle, message) &&
                (connectionNonce == 0 ||
                    existing->second.lifecycle.connectionNonce == 0 ||
                    connectionNonce ==
                        existing->second.lifecycle.connectionNonce))
            {
                // Reliable retransmission of Construct is harmless. A
                // later structural revision must arrive as a ComponentDelta,
                // so never let Construct rewrite current presentation state.
                return message.structuralRevision ==
                    existing->second.lifecycle.structuralRevision;
            }

            RemotePlayerSnapshot snapshot;
            snapshot.receivedAt = receivedAt;
            snapshot.lifecycle.actorGeneration = message.actorGeneration;
            snapshot.lifecycle.mapEpoch = message.mapEpoch;
            snapshot.lifecycle.structuralRevision =
                message.structuralRevision;
            snapshot.lifecycle.connectionNonce = connectionNonce;
            snapshot.lifecycle.appearancePresent =
                message.AppearancePresent();
            snapshot.lifecycle.equipmentPresent =
                message.EquipmentPresent();
            snapshot.lifecycle.appearanceReady =
                snapshot.lifecycle.appearancePresent &&
                message.heroMorph.IsSane() && message.heroClothing.IsSane() &&
                message.heroBoneScales.IsSane() &&
                message.heroAppearanceModifiers.IsSane();
            snapshot.lifecycle.equipmentReady =
                snapshot.lifecycle.equipmentPresent &&
                message.heroEquipment.IsSane();
            snapshot.lifecycle.active = snapshot.lifecycle.appearanceReady &&
                snapshot.lifecycle.equipmentReady;
            snapshot.equipmentTransition = MaterializeEquipmentTransition(
                message, receivedAt, sessionNow);

            PlayerState& state = snapshot.state;
            state.actorId = message.actorId;
            state.authorityEpoch = message.authorityEpoch;
            state.actorGeneration = message.actorGeneration;
            state.mapEpoch = message.mapEpoch;
            state.role = message.role;
            state.playerId = message.playerId;
            state.mapName = message.mapName;
            state.mapId = message.mapId;
            state.appearanceDefinition = message.appearanceDefinition;
            state.position = message.initialPosition;
            state.facing = NormalizeFacing(message.initialFacing);
            state.heroMorph = message.heroMorph;
            state.heroClothing = message.heroClothing;
            state.heroBoneScales = message.heroBoneScales;
            state.heroAppearanceModifiers = message.heroAppearanceModifiers;
            state.heroEquipment = message.heroEquipment;
            state.changedProperties = player_property::Identity |
                player_property::Map | player_property::Movement;
            if (snapshot.lifecycle.appearancePresent)
            {
                state.changedProperties |= player_property::Appearance;
            }
            if (snapshot.lifecycle.equipmentPresent)
            {
                state.changedProperties |= player_property::Equipment;
            }
            if (hasExisting)
            {
                if (!allActorsInvalidated_ &&
                    invalidatedActors_.size() < 1024)
                {
                    invalidatedActors_.insert(message.actorId);
                }
                else if (!allActorsInvalidated_)
                {
                    invalidatedActors_.clear();
                    allActorsInvalidated_ = true;
                }
            }
            channels_[message.actorId] = std::move(snapshot);
            AdvanceObserverReadinessRevision();
            return true;
        }

        if (message.operation != protocol::PlayerActorStateOperation::
                ComponentDelta &&
            message.operation != protocol::PlayerActorStateOperation::
                MapTransition)
        {
            return false;
        }
        if (!hasExisting)
        {
            // A delta/map transition cannot create an actor. This is what
            // prevents stale reliable packets from resurrecting a retired
            // presentation.
            return false;
        }

        if (message.operation == protocol::PlayerActorStateOperation::
                MapTransition)
        {
            RemotePlayerSnapshot next = existing->second;
            next.receivedAt = receivedAt;
            next.state.authorityEpoch = canonical.authorityEpoch;
            next.state.actorGeneration = canonical.actorGeneration;
            next.state.mapEpoch = canonical.mapEpoch;
            next.state.role = canonical.role;
            next.lifecycle.actorGeneration = canonical.actorGeneration;
            next.lifecycle.mapEpoch = canonical.mapEpoch;
            next.lifecycle.structuralRevision = canonical.structuralRevision;
            if (connectionNonce != 0)
            {
                next.lifecycle.connectionNonce = connectionNonce;
            }
            next.state.mapName = canonical.mapName;
            next.state.mapId = canonical.mapId;
            next.state.position = canonical.initialPosition;
            next.state.velocity = {};
            next.state.facing = NormalizeFacing(canonical.initialFacing);
            next.state.angularVelocity = 0.0f;
            next.state.moving = false;
            next.state.sequence = 0;
            next.equipmentTransition = {};
            next.state.changedProperties = player_property::Identity |
                player_property::Map | player_property::Movement;
            if (next.lifecycle.appearancePresent)
            {
                next.state.changedProperties |= player_property::Appearance;
            }
            if (next.lifecycle.equipmentPresent)
            {
                next.state.changedProperties |= player_property::Equipment;
            }
            if (!allActorsInvalidated_ && invalidatedActors_.size() < 1024)
            {
                invalidatedActors_.insert(message.actorId);
            }
            else if (!allActorsInvalidated_)
            {
                invalidatedActors_.clear();
                allActorsInvalidated_ = true;
            }
            channels_[message.actorId] = std::move(next);
            AdvanceObserverReadinessRevision();
            return true;
        }

        RemotePlayerSnapshot& snapshot = existing->second;

        if (message.operation == protocol::PlayerActorStateOperation::
                ComponentDelta)
        {
            if (message.AppearanceChanged())
            {
                snapshot.lifecycle.appearancePresent =
                    canonical.AppearancePresent();
                snapshot.lifecycle.appearanceReady =
                    snapshot.lifecycle.appearancePresent &&
                    canonical.heroMorph.IsSane() &&
                    canonical.heroClothing.IsSane() &&
                    canonical.heroBoneScales.IsSane() &&
                    canonical.heroAppearanceModifiers.IsSane();
                if (snapshot.lifecycle.appearancePresent)
                {
                    snapshot.state.heroMorph = canonical.heroMorph;
                    snapshot.state.heroClothing = canonical.heroClothing;
                    snapshot.state.heroBoneScales = canonical.heroBoneScales;
                    snapshot.state.heroAppearanceModifiers =
                        canonical.heroAppearanceModifiers;
                }
                else
                {
                    snapshot.state.heroMorph = {};
                    snapshot.state.heroClothing = {};
                    snapshot.state.heroBoneScales = {};
                    snapshot.state.heroAppearanceModifiers = {};
                }
            }
            if (message.EquipmentChanged())
            {
                snapshot.lifecycle.equipmentPresent =
                    canonical.EquipmentPresent();
                snapshot.lifecycle.equipmentReady =
                    snapshot.lifecycle.equipmentPresent &&
                    canonical.heroEquipment.IsSane();
                snapshot.state.heroEquipment =
                    snapshot.lifecycle.equipmentPresent
                    ? canonical.heroEquipment
                    : game::hero_pawn::equipment::HeroEquipmentState{};
                snapshot.equipmentTransition =
                    MaterializeEquipmentTransition(
                        message, receivedAt, sessionNow);
            }
            else if (message.AppearanceChanged())
            {
                // This revision does not describe equipment. Do not leave a
                // prior transition record looking like a new event to the
                // presentation layer.
                snapshot.equipmentTransition = {};
            }
        }
        else
        {
            snapshot.state.mapName = message.mapName;
            snapshot.state.mapId = message.mapId;
            snapshot.state.position = message.initialPosition;
            snapshot.state.facing = NormalizeFacing(message.initialFacing);
            snapshot.equipmentTransition = {};
        }
        snapshot.lifecycle.structuralRevision = message.structuralRevision;
        snapshot.lifecycle.active = snapshot.lifecycle.appearanceReady &&
            snapshot.lifecycle.equipmentReady;
        snapshot.state.changedProperties = player_property::Identity |
            player_property::Map | player_property::Movement;
        if (snapshot.lifecycle.appearancePresent)
        {
            snapshot.state.changedProperties |= player_property::Appearance;
        }
        if (snapshot.lifecycle.equipmentPresent)
        {
            snapshot.state.changedProperties |= player_property::Equipment;
        }
        snapshot.receivedAt = receivedAt;
        return true;
    }

    bool RemotePlayerChannels::Apply(
        const PlayerState& update,
        std::uint64_t receivedAt)
    {
        const std::uint32_t changed = update.changedProperties &
            player_property::All;
        if (changed != player_property::Movement || update.actorId == 0 ||
            update.authorityEpoch == 0)
        {
            return false;
        }
        const auto existing = channels_.find(update.actorId);
        if (existing == channels_.end() ||
            update.authorityEpoch != existing->second.state.authorityEpoch)
        {
            return false;
        }
        if (update.actorGeneration != existing->second.lifecycle.actorGeneration ||
            update.mapEpoch != existing->second.lifecycle.mapEpoch)
        {
            return false;
        }
        if (update.mapId != existing->second.state.mapId)
        {
            return false;
        }
        if (update.sequence != existing->second.state.sequence &&
            !IsNewerSequence(update.sequence, existing->second.state.sequence))
        {
            return false;
        }
        RemotePlayerSnapshot& snapshot = existing->second;
        PlayerState& state = snapshot.state;
        state.position = update.position;
        state.velocity = update.velocity;
        state.facing = NormalizeFacing(update.facing);
        state.angularVelocity = std::isfinite(update.angularVelocity)
            ? update.angularVelocity
            : 0.0f;
        state.moving = update.moving;
        state.movementSampleTimeMs = update.movementSampleTimeMs;
        state.movementSampleAt = update.movementSampleAt;
        state.sequence = update.sequence;
        state.changedProperties = changed;
        snapshot.receivedAt = receivedAt;
        return true;
    }

    std::vector<RemotePlayerSnapshot> RemotePlayerChannels::Snapshots() const
    {
        std::vector<RemotePlayerSnapshot> result;
        result.reserve(channels_.size());
        for (const auto& [actorId, snapshot] : channels_)
        {
            (void)actorId;
            result.push_back(snapshot);
        }
        return result;
    }

    const PlayerState* RemotePlayerChannels::Find(
        std::uint64_t actorId) const noexcept
    {
        const auto iterator = channels_.find(actorId);
        return iterator != channels_.end()
            ? &iterator->second.state
            : nullptr;
    }

    const RemotePlayerLifecycle* RemotePlayerChannels::FindLifecycle(
        std::uint64_t actorId) const noexcept
    {
        const auto iterator = channels_.find(actorId);
        return iterator != channels_.end()
            ? &iterator->second.lifecycle
            : nullptr;
    }

    bool RemotePlayerChannels::IsLifecycleActive(
        std::uint64_t actorId,
        std::uint32_t actorGeneration,
        std::uint32_t mapEpoch) const noexcept
    {
        const auto lifecycle = FindLifecycle(actorId);
        return lifecycle != nullptr && lifecycle->active &&
            lifecycle->actorGeneration == actorGeneration &&
            lifecycle->mapEpoch == mapEpoch;
    }

    bool RemotePlayerChannels::IsAppearanceReady(
        std::uint64_t actorId,
        std::uint32_t actorGeneration,
        std::uint32_t mapEpoch) const noexcept
    {
        const auto lifecycle = FindLifecycle(actorId);
        return lifecycle != nullptr &&
            lifecycle->actorGeneration == actorGeneration &&
            lifecycle->mapEpoch == mapEpoch && lifecycle->appearanceReady;
    }

    bool RemotePlayerChannels::IsEquipmentReady(
        std::uint64_t actorId,
        std::uint32_t actorGeneration,
        std::uint32_t mapEpoch) const noexcept
    {
        const auto lifecycle = FindLifecycle(actorId);
        return lifecycle != nullptr &&
            lifecycle->actorGeneration == actorGeneration &&
            lifecycle->mapEpoch == mapEpoch && lifecycle->equipmentReady;
    }

    void RemotePlayerChannels::Remove(std::uint64_t actorId) noexcept
    {
        if (channels_.erase(actorId) != 0)
        {
            if (!allActorsInvalidated_ && invalidatedActors_.size() < 1024)
            {
                invalidatedActors_.insert(actorId);
            }
            else if (!allActorsInvalidated_)
            {
                invalidatedActors_.clear();
                allActorsInvalidated_ = true;
            }
        }
    }

    void RemotePlayerChannels::Clear() noexcept
    {
        if (!channels_.empty())
        {
            invalidatedActors_.clear();
            allActorsInvalidated_ = true;
        }
        channels_.clear();
        observerReadinessRevision_ = 0;
    }

    void RemotePlayerChannels::ConsumeInvalidations(
        std::vector<std::uint64_t>& actorIds,
        bool& allActors) noexcept
    {
        actorIds.clear();
        allActors = allActorsInvalidated_;
        if (!allActors)
        {
            actorIds.reserve(invalidatedActors_.size());
            for (const std::uint64_t actorId : invalidatedActors_)
            {
                actorIds.push_back(actorId);
            }
        }
        invalidatedActors_.clear();
        allActorsInvalidated_ = false;
    }

    std::size_t RemotePlayerChannels::Size() const noexcept
    {
        return channels_.size();
    }

    std::uint64_t RemotePlayerChannels::ObserverReadinessRevision() const
        noexcept
    {
        return observerReadinessRevision_;
    }

    void RemotePlayerChannels::AdvanceObserverReadinessRevision() noexcept
    {
        ++observerReadinessRevision_;
        if (observerReadinessRevision_ == 0)
        {
            ++observerReadinessRevision_;
        }
    }
}
