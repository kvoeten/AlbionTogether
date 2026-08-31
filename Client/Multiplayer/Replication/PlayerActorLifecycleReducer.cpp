#include "PlayerActorLifecycleReducer.h"

namespace fable::multiplayer::replication
{
    namespace
    {
        bool IsSameIncarnation(
            const protocol::PlayerActorStateMessage& current,
            const protocol::PlayerActorStateMessage& incoming) noexcept
        {
            return current.actorGeneration == incoming.actorGeneration &&
                current.mapEpoch == incoming.mapEpoch;
        }

        bool HasCompleteConstructBaseline(
            const protocol::PlayerActorStateMessage& message) noexcept
        {
            using namespace protocol;
            return message.playerId.size() != 0 &&
                message.mapName.size() != 0 &&
                message.appearanceDefinition.size() != 0 &&
                message.AppearancePresent() && message.EquipmentPresent() &&
                message.heroMorph.IsSane() && message.heroClothing.IsSane() &&
                message.heroBoneScales.IsSane() &&
                message.heroAppearanceModifiers.IsSane() &&
                message.heroEquipment.IsSane();
        }

        bool IsKnownOperation(
            const protocol::PlayerActorStateOperation operation) noexcept
        {
            using protocol::PlayerActorStateOperation;
            switch (operation)
            {
            case PlayerActorStateOperation::Construct:
            case PlayerActorStateOperation::ComponentDelta:
            case PlayerActorStateOperation::MapTransition:
            case PlayerActorStateOperation::Retire:
                return true;
            }
            return false;
        }
    }

    bool PlayerActorLifecycleReducer::IsNewer(
        const std::uint32_t candidate,
        const std::uint32_t current) noexcept
    {
        return current == 0 ||
            static_cast<std::int32_t>(candidate - current) > 0;
    }

    bool PlayerActorLifecycleReducer::IsOlderIncarnation(
        const protocol::PlayerActorStateMessage& current,
        const protocol::PlayerActorStateMessage& incoming) noexcept
    {
        return incoming.actorGeneration < current.actorGeneration ||
            (incoming.actorGeneration == current.actorGeneration &&
                incoming.mapEpoch < current.mapEpoch);
    }

    void PlayerActorLifecycleReducer::ClearTransitionTiming(
        protocol::PlayerActorStateMessage& message) noexcept
    {
        message.transitionStartedAtSessionTimeMs =
            protocol::SessionTimeUnset;
        message.transitionAnimationId = 0;
        message.transitionDurationMs = 0;
        message.attachmentNotifyOffsetMs = 0;
    }

    void PlayerActorLifecycleReducer::ClearStructuralTiming(
        protocol::PlayerActorStateMessage& message) noexcept
    {
        message.constructionSnapshotTimeMs = protocol::SessionTimeUnset;
        message.componentPatchEffectiveTimeMs = protocol::SessionTimeUnset;
        ClearTransitionTiming(message);
    }

    protocol::PlayerActorStateMessage PlayerActorLifecycleReducer::MergeDelta(
        const protocol::PlayerActorStateMessage& current,
        const protocol::PlayerActorStateMessage& delta)
    {
        protocol::PlayerActorStateMessage merged = current;
        merged.operation = delta.operation;
        merged.componentFlags = delta.componentFlags;
        if (delta.operation == protocol::PlayerActorStateOperation::
                ComponentDelta)
        {
            merged.componentPatchEffectiveTimeMs =
                delta.componentPatchEffectiveTimeMs;
        }
        if (delta.AppearanceChanged())
        {
            merged.appearanceDefinition = delta.appearanceDefinition;
            if (delta.AppearancePresent())
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
        if (delta.EquipmentChanged())
        {
            merged.heroEquipment = delta.EquipmentPresent()
                ? delta.heroEquipment
                : game::hero_pawn::equipment::HeroEquipmentState{};
            merged.transitionStartedAtSessionTimeMs =
                delta.transitionStartedAtSessionTimeMs;
            merged.transitionAnimationId = delta.transitionAnimationId;
            merged.transitionDurationMs = delta.transitionDurationMs;
            merged.attachmentNotifyOffsetMs =
                delta.attachmentNotifyOffsetMs;
        }
        else
        {
            // A separately applied appearance-only revision is not a new
            // equipment event. Clear event metadata from the materialized
            // current state; CoalesceDelta snapshots and restores metadata
            // from an earlier unsent equipment patch when appropriate.
            ClearTransitionTiming(merged);
        }
        return merged;
    }

    protocol::PlayerActorStateMessage
        PlayerActorLifecycleReducer::CoalesceDelta(
            const protocol::PlayerActorStateMessage& current,
            const protocol::PlayerActorStateMessage& delta)
    {
        protocol::PlayerActorStateMessage merged = MergeDelta(current, delta);
        if (!delta.EquipmentChanged() && current.EquipmentChanged())
        {
            // MergeDelta intentionally clears stale timing for normal state
            // application. Publication coalescing is different: the unioned
            // equipment flag still represents the earlier unsent equipment
            // patch, so retain that patch's event metadata.
            merged.transitionStartedAtSessionTimeMs =
                current.transitionStartedAtSessionTimeMs;
            merged.transitionAnimationId = current.transitionAnimationId;
            merged.transitionDurationMs = current.transitionDurationMs;
            merged.attachmentNotifyOffsetMs =
                current.attachmentNotifyOffsetMs;
        }
        merged.componentFlags = current.componentFlags | delta.componentFlags;
        merged.constructionSnapshotTimeMs = protocol::SessionTimeUnset;
        merged.componentPatchEffectiveTimeMs =
            delta.componentPatchEffectiveTimeMs;
        return merged;
    }

    PlayerActorLifecycleReduction PlayerActorLifecycleReducer::Reduce(
        const protocol::PlayerActorStateMessage* const current,
        const protocol::PlayerActorStateMessage& incoming,
        protocol::PlayerActorStateMessage& next)
    {
        using protocol::PlayerActorStateOperation;
        next = {};
        if (!IsKnownOperation(incoming.operation) ||
            incoming.actorId == 0 || incoming.authorityEpoch == 0 ||
            incoming.actorGeneration == 0 || incoming.mapEpoch == 0 ||
            incoming.structuralRevision == 0)
        {
            return PlayerActorLifecycleReduction::Rejected;
        }
        if (current != nullptr && incoming.operation !=
                PlayerActorStateOperation::Construct && !IsNewer(
                incoming.structuralRevision, current->structuralRevision))
        {
            return PlayerActorLifecycleReduction::Ignored;
        }

        switch (incoming.operation)
        {
        case PlayerActorStateOperation::Construct:
            if (!HasCompleteConstructBaseline(incoming))
            {
                return PlayerActorLifecycleReduction::Rejected;
            }
            if (current != nullptr && IsOlderIncarnation(*current, incoming))
            {
                return PlayerActorLifecycleReduction::Rejected;
            }
            if (current != nullptr && IsSameIncarnation(*current, incoming))
            {
                return IsNewer(
                    incoming.structuralRevision,
                    current->structuralRevision)
                    ? PlayerActorLifecycleReduction::Rejected
                    : PlayerActorLifecycleReduction::Ignored;
            }
            next = incoming;
            return PlayerActorLifecycleReduction::Applied;

        case PlayerActorStateOperation::ComponentDelta:
            if (current == nullptr ||
                incoming.authorityEpoch != current->authorityEpoch ||
                !IsSameIncarnation(*current, incoming) ||
                incoming.mapId != current->mapId ||
                incoming.mapName != current->mapName ||
                (!incoming.AppearanceChanged() &&
                    !incoming.EquipmentChanged()) ||
                (incoming.AppearanceChanged() &&
                    (!incoming.AppearancePresent() ||
                        !incoming.heroMorph.IsSane() ||
                        !incoming.heroClothing.IsSane() ||
                        !incoming.heroBoneScales.IsSane() ||
                        !incoming.heroAppearanceModifiers.IsSane())) ||
                (incoming.EquipmentChanged() &&
                    (!incoming.EquipmentPresent() ||
                        !incoming.heroEquipment.IsSane())))
            {
                return PlayerActorLifecycleReduction::Rejected;
            }
            next = MergeDelta(*current, incoming);
            next.structuralRevision = incoming.structuralRevision;
            return PlayerActorLifecycleReduction::Applied;

        case PlayerActorStateOperation::MapTransition:
            if (current == nullptr || incoming.mapId == 0 ||
                incoming.mapName.empty())
            {
                return PlayerActorLifecycleReduction::Ignored;
            }
            if (incoming.authorityEpoch != current->authorityEpoch)
            {
                return PlayerActorLifecycleReduction::Rejected;
            }
            if (
                incoming.actorGeneration < current->actorGeneration ||
                (incoming.actorGeneration == current->actorGeneration &&
                    incoming.mapEpoch <= current->mapEpoch))
            {
                return PlayerActorLifecycleReduction::Ignored;
            }
            next = *current;
            next.operation = incoming.operation;
            next.componentFlags = 0;
            ClearStructuralTiming(next);
            next.actorGeneration = incoming.actorGeneration;
            next.mapEpoch = incoming.mapEpoch;
            next.mapId = incoming.mapId;
            next.mapName = incoming.mapName;
            next.initialPosition = incoming.initialPosition;
            next.initialFacing = incoming.initialFacing;
            next.role = incoming.role;
            next.structuralRevision = incoming.structuralRevision;
            return PlayerActorLifecycleReduction::Applied;

        case PlayerActorStateOperation::Retire:
            if (current == nullptr)
            {
                return PlayerActorLifecycleReduction::Ignored;
            }
            if (incoming.authorityEpoch != current->authorityEpoch ||
                !IsSameIncarnation(*current, incoming))
            {
                return PlayerActorLifecycleReduction::Rejected;
            }
            next = *current;
            next.operation = incoming.operation;
            next.componentFlags = 0;
            ClearStructuralTiming(next);
            next.structuralRevision = incoming.structuralRevision;
            return PlayerActorLifecycleReduction::Applied;
        }
        return PlayerActorLifecycleReduction::Rejected;
    }
}
