#include "RemotePlayerChannels.h"

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

    bool IsOlderIncarnation(
        const fable::multiplayer::replication::RemotePlayerLifecycle& current,
        const fable::multiplayer::protocol::PlayerActorStateMessage& incoming)
        noexcept
    {
        return incoming.actorGeneration < current.actorGeneration ||
            (incoming.actorGeneration == current.actorGeneration &&
                incoming.mapEpoch < current.mapEpoch);
    }

    bool IsSameIncarnation(
        const fable::multiplayer::replication::RemotePlayerLifecycle& current,
        const fable::multiplayer::protocol::PlayerActorStateMessage& incoming)
        noexcept
    {
        return current.actorGeneration == incoming.actorGeneration &&
            current.mapEpoch == incoming.mapEpoch;
    }
}

namespace fable::multiplayer::replication
{
    bool RemotePlayerChannels::ApplyActorState(
        const protocol::PlayerActorStateMessage& message,
        std::uint64_t receivedAt,
        const std::uint64_t connectionNonce)
    {
        if (message.actorId == 0 || message.authorityEpoch == 0 ||
            message.actorGeneration == 0 || message.mapEpoch == 0 ||
            message.structuralRevision == 0)
        {
            return false;
        }

        const auto existing = channels_.find(message.actorId);
        const bool hasExisting = existing != channels_.end();
        if (message.operation == protocol::PlayerActorStateOperation::Retire)
        {
            if (!hasExisting)
            {
                return true;
            }
            if (existing->second.state.authorityEpoch !=
                message.authorityEpoch ||
                !IsSameIncarnation(existing->second.lifecycle, message) ||
                message.structuralRevision <=
                    existing->second.lifecycle.structuralRevision ||
                (existing->second.lifecycle.connectionNonce != 0 &&
                    (connectionNonce == 0 || connectionNonce !=
                        existing->second.lifecycle.connectionNonce)))
            {
                return false;
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

        if (hasExisting && IsOlderIncarnation(
                existing->second.lifecycle, message))
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
                MapTransition &&
            !IsSameIncarnation(existing->second.lifecycle, message))
        {
            if (message.actorGeneration < existing->second.lifecycle.
                    actorGeneration ||
                (message.actorGeneration == existing->second.lifecycle.
                    actorGeneration &&
                    message.mapEpoch <= existing->second.lifecycle.mapEpoch) ||
                message.structuralRevision <=
                    existing->second.lifecycle.structuralRevision)
            {
                return false;
            }
            RemotePlayerSnapshot next = existing->second;
            next.receivedAt = receivedAt;
            next.state.authorityEpoch = message.authorityEpoch;
            next.state.actorGeneration = message.actorGeneration;
            next.state.mapEpoch = message.mapEpoch;
            next.state.role = message.role;
            next.lifecycle.actorGeneration = message.actorGeneration;
            next.lifecycle.mapEpoch = message.mapEpoch;
            next.lifecycle.structuralRevision = message.structuralRevision;
            if (connectionNonce != 0)
            {
                next.lifecycle.connectionNonce = connectionNonce;
            }
            next.state.mapName = message.mapName;
            next.state.mapId = message.mapId;
            next.state.position = message.initialPosition;
            next.state.velocity = {};
            next.state.facing = NormalizeFacing(message.initialFacing);
            next.state.angularVelocity = 0.0f;
            next.state.moving = false;
            next.state.sequence = 0;
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

        if (!IsSameIncarnation(existing->second.lifecycle, message))
        {
            return false;
        }

        // Appearance and equipment are mandatory for the lifetime of this
        // remote Hero incarnation. A delta that clears either component would
        // leave an existing native presentation with no valid baseline.
        if (message.operation == protocol::PlayerActorStateOperation::
                ComponentDelta &&
            ((message.AppearanceChanged() && !message.AppearancePresent()) ||
                (message.EquipmentChanged() && !message.EquipmentPresent())))
        {
            return false;
        }

        RemotePlayerSnapshot& snapshot = existing->second;
        if (message.structuralRevision <
            snapshot.lifecycle.structuralRevision)
        {
            return false;
        }
        if (message.structuralRevision ==
            snapshot.lifecycle.structuralRevision)
        {
            return true;
        }
        if (message.authorityEpoch != snapshot.state.authorityEpoch)
        {
            return false;
        }

        if (message.operation == protocol::PlayerActorStateOperation::
                ComponentDelta)
        {
            if (message.AppearanceChanged())
            {
                snapshot.lifecycle.appearancePresent =
                    message.AppearancePresent();
                snapshot.lifecycle.appearanceReady =
                    snapshot.lifecycle.appearancePresent &&
                    message.heroMorph.IsSane() && message.heroClothing.IsSane() &&
                    message.heroBoneScales.IsSane() &&
                    message.heroAppearanceModifiers.IsSane();
                if (snapshot.lifecycle.appearancePresent)
                {
                    snapshot.state.heroMorph = message.heroMorph;
                    snapshot.state.heroClothing = message.heroClothing;
                    snapshot.state.heroBoneScales = message.heroBoneScales;
                    snapshot.state.heroAppearanceModifiers =
                        message.heroAppearanceModifiers;
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
                    message.EquipmentPresent();
                snapshot.lifecycle.equipmentReady =
                    snapshot.lifecycle.equipmentPresent &&
                    message.heroEquipment.IsSane();
                snapshot.state.heroEquipment =
                    snapshot.lifecycle.equipmentPresent
                    ? message.heroEquipment
                    : game::hero_pawn::equipment::HeroEquipmentState{};
            }
        }
        else
        {
            snapshot.state.mapName = message.mapName;
            snapshot.state.mapId = message.mapId;
            snapshot.state.position = message.initialPosition;
            snapshot.state.facing = NormalizeFacing(message.initialFacing);
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
