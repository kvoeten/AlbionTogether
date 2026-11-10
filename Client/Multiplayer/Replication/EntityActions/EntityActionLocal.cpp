#include "Multiplayer/Replication/EntityActionReplication.h"
#include "Multiplayer/Replication/EntityActions/EntityActionSemantics.h"
#include "Multiplayer/Runtime/MultiplayerSessionContexts.h"
#include "Multiplayer/Transport/ReliableSinkDescriptorRegistry.h"


namespace fable::multiplayer::replication
{

    bool EntityActionReplication::ProcessEvent(
        const game::creature::actions::CreatureActionLifecycleEvent& event,
        const std::string& localMap,
        std::uint32_t mapEpoch)
    {
        using game::creature::actions::CreatureActionLifecyclePhase;
        if (event.phase == CreatureActionLifecyclePhase::Submitted)
        {
            const AbilityActionBinding ability =
                BindLocalAbilityAction(event);
            if (ability == AbilityActionBinding::Failed)
            {
                return false;
            }
            if (ability == AbilityActionBinding::Handled)
            {
                return true;
            }
            return !event.accepted ||
                BeginLocalAction(event, localMap, mapEpoch);
        }
        if (event.phase == CreatureActionLifecyclePhase::Finished)
        {
            return FinishLocalAction(event.action);
        }
        return true;
    }

    bool EntityActionReplication::BeginLocalAction(
        const game::creature::actions::CreatureActionLifecycleEvent& event,
        const std::string& localMap,
        std::uint32_t mapEpoch)
    {
        const protocol::EntityActionKind kind =
            entity_actions::Classify(event.actionType);
        if (!protocol::RequiresSharedEntityAuthority(kind))
        {
            // Local shopping must not claim the merchant or leak another
            // player's transaction/interaction into the shared action stream.
            return true;
        }
        if (event.thingUid == 0 || event.action == nullptr ||
            localActionIds_.find(event.action) != localActionIds_.end())
        {
            return true;
        }
        const entities::WorldEntityRecord* const entity =
            lifecycle_->Directory().Find(event.thingUid);
        if (entity == nullptr || !entity->live || !entity->available ||
            entity->mapName != localMap)
        {
            return true;
        }

        ActiveAction active;
        active.entityUid = event.thingUid;
        active.entityGeneration = entity->generation;
        active.actionId = NextActionId();
        active.ownerActorId = localActorId_;
        active.mapEpoch = mapEpoch;
        active.kind = kind;
        active.flags = entity_actions::DefaultFlags(active.kind);
        active.targetPlayerActorId = combatants_ != nullptr
            ? combatants_->FindActor(event.targetCreature)
            : 0;
        if (active.targetPlayerActorId != 0)
        {
            active.flags |= protocol::entity_action_flag::HasPlayerTarget;
        }
        else if (event.targetThingUid != 0)
        {
            const entities::WorldEntityRecord* const target =
                lifecycle_->Directory().Find(event.targetThingUid);
            if (target != nullptr && target->live && target->available &&
                target->generation != 0 && target->mapName == localMap)
            {
                active.targetEntityUid = target->thingUid;
                active.targetEntityGeneration = target->generation;
                active.flags |=
                    protocol::entity_action_flag::HasEntityTarget;
            }
        }
        active.mapName = localMap;
        active.semanticName = event.actionType[0] != '\0'
            ? event.actionType
            : "UnknownNativeAction";
        active.nativeAction = event.action;
        active.localOrigin = true;
        const std::uint64_t actionId = active.actionId;
        activeActions_.emplace(actionId, active);
        localActionIds_[event.action] = actionId;

        protocol::EntityActionMessage intent = ToMessage(
            activeActions_.at(actionId),
            protocol::EntityActionPhase::Intent);
        if (role_ == PeerRole::Host)
        {
            return HostAcceptIntent(std::move(intent), localActorId_);
        }
        return Queue(std::move(intent));
    }

    bool EntityActionReplication::BeginLocalAbility(
        const game::creature::combat::CreatureAbilityEvent& event,
        const std::string& localMap,
        std::uint32_t mapEpoch)
    {
        if (event.sourceThingUid == 0 || event.abilityId == 0 ||
            localMap.empty() || mapEpoch == 0)
        {
            return true;
        }
        const entities::WorldEntityRecord* const entity =
            lifecycle_->Directory().Find(event.sourceThingUid);
        if (entity == nullptr || !entity->live || !entity->available ||
            !entity->creature || entity->mapName != localMap)
        {
            return true;
        }

        auto& pendingIndex = event.actionCorrelationId != 0
            ? pendingAbilityCorrelations_
            : pendingLegacyAbilities_;
        const std::uint64_t pendingKey = event.actionCorrelationId != 0
            ? event.actionCorrelationId
            : event.sourceThingUid;
        const auto pending = pendingIndex.find(pendingKey);
        if (pending != pendingIndex.end())
        {
            const auto active = activeActions_.find(pending->second);
            if (active != activeActions_.end() &&
                active->second.localOrigin &&
                active->second.nativeAbility &&
                active->second.nativeAction == nullptr)
            {
                // Duplicate capture of the same semantic request is harmless.
                // A distinct runtime request has a distinct correlation ID and
                // therefore never collapses into this entry.
                return true;
            }
            pendingIndex.erase(pending);
        }

        ActiveAction active;
        active.entityUid = event.sourceThingUid;
        active.entityGeneration = entity->generation;
        active.actionId = NextActionId();
        active.ownerActorId = localActorId_;
        active.mapEpoch = mapEpoch;
        active.kind = protocol::EntityActionKind::Combat;
        active.flags = entity_actions::DefaultFlags(active.kind);
        active.targetPlayerActorId = combatants_ != nullptr
            ? combatants_->FindActor(event.targetCreature)
            : 0;
        if (active.targetPlayerActorId != 0)
        {
            active.flags |= protocol::entity_action_flag::HasPlayerTarget;
        }
        else if (event.targetThingUid != 0)
        {
            const entities::WorldEntityRecord* const target =
                lifecycle_->Directory().Find(event.targetThingUid);
            if (target != nullptr && target->live && target->available &&
                target->generation != 0 && target->mapName == localMap)
            {
                active.targetEntityUid = target->thingUid;
                active.targetEntityGeneration = target->generation;
                active.flags |=
                    protocol::entity_action_flag::HasEntityTarget;
            }
        }
        active.abilityId = event.abilityId;
        active.abilityCharge = event.charge;
        active.actionCorrelationId = event.actionCorrelationId;
        active.mapName = localMap;
        active.semanticName = "CreatureAbility";
        active.lastActivityAt = event.observedAt != 0
            ? event.observedAt
            : GetTickCount64();
        active.localOrigin = true;
        active.nativeAbility = true;
        const std::uint64_t actionId = active.actionId;
        activeActions_.emplace(actionId, active);
        pendingIndex[pendingKey] = actionId;
        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "thing_uid=%016llX action_id=%llu correlation_id=%llu ability_id=%u charge=%.3f map=%s",
            static_cast<unsigned long long>(active.entityUid),
            static_cast<unsigned long long>(active.actionId),
            static_cast<unsigned long long>(active.actionCorrelationId),
            active.abilityId,
            active.abilityCharge,
            localMap.c_str());
        diagnostics_.Event("MultiplayerLocalEntityAbilityStaged", detail);
        return true;
    }

    EntityActionReplication::AbilityActionBinding
        EntityActionReplication::BindLocalAbilityAction(
        const game::creature::actions::CreatureActionLifecycleEvent& event)
    {
        if (event.action == nullptr)
        {
            return AbilityActionBinding::NotMatched;
        }

        auto& pendingIndex = event.actionCorrelationId != 0
            ? pendingAbilityCorrelations_
            : pendingLegacyAbilities_;
        const std::uint64_t pendingKey = event.actionCorrelationId != 0
            ? event.actionCorrelationId
            : event.thingUid;
        const auto pending = pendingIndex.find(pendingKey);
        if (pending == pendingIndex.end())
        {
            if (!event.accepted)
            {
                return AbilityActionBinding::NotMatched;
            }
            for (const auto& [unusedActionId, active] : activeActions_)
            {
                (void)unusedActionId;
                if (active.nativeThingUid == event.thingUid &&
                    active.localOrigin && active.nativeAbility &&
                    !active.endQueued &&
                    GetTickCount64() <= active.lastActivityAt + 10'000)
                {
                    // Follow-up native actions are already generated on every
                    // observer by the replicated ability request. Publishing
                    // their resolved poses would fight that retail graph.
                    return AbilityActionBinding::Handled;
                }
            }
            return AbilityActionBinding::NotMatched;
        }
        const auto active = activeActions_.find(pending->second);
        if (active == activeActions_.end() ||
            !active->second.MatchesAbilitySubmission(
                event.actionCorrelationId, event.thingUid))
        {
            pendingIndex.erase(pending);
            return AbilityActionBinding::NotMatched;
        }
        if (!event.accepted)
        {
            diagnostics_.Event(
                "MultiplayerLocalEntityAbilitySuppressed",
                "correlated native action was rejected");
            pendingIndex.erase(pending);
            activeActions_.erase(active);
            return AbilityActionBinding::Handled;
        }
        active->second.nativeAction = event.action;
        active->second.nativeThingUid = event.thingUid;
        localActionIds_[event.action] = active->second.actionId;
        pendingIndex.erase(pending);
        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "semantic_uid=%016llX native_uid=%016llX action_id=%llu correlation_id=%llu ability_id=%u native_action=%p native_type=%s",
            static_cast<unsigned long long>(active->second.entityUid),
            static_cast<unsigned long long>(active->second.nativeThingUid),
            static_cast<unsigned long long>(active->second.actionId),
            static_cast<unsigned long long>(
                active->second.actionCorrelationId),
            active->second.abilityId,
            event.action,
            event.actionType[0] != '\0'
                ? event.actionType
                : "UnknownNativeAction");
        diagnostics_.Event("MultiplayerLocalEntityAbilityBound", detail);
        protocol::EntityActionMessage intent = ToMessage(
            active->second,
            protocol::EntityActionPhase::Intent);
        const bool admitted = role_ == PeerRole::Host
            ? HostAcceptIntent(std::move(intent), localActorId_)
            : Queue(std::move(intent));
        return admitted
            ? AbilityActionBinding::Handled
            : AbilityActionBinding::Failed;
    }

    void EntityActionReplication::ForgetPendingAbility(
        const ActiveAction& action) noexcept
    {
        auto& pendingIndex = action.actionCorrelationId != 0
            ? pendingAbilityCorrelations_
            : pendingLegacyAbilities_;
        const std::uint64_t key = action.actionCorrelationId != 0
            ? action.actionCorrelationId
            : action.entityUid;
        const auto pending = pendingIndex.find(key);
        if (pending != pendingIndex.end() &&
            pending->second == action.actionId)
        {
            pendingIndex.erase(pending);
        }
    }

    bool EntityActionReplication::FinishLocalAction(void* nativeAction)
    {
        const auto local = localActionIds_.find(nativeAction);
        if (local == localActionIds_.end())
        {
            return true;
        }
        const auto active = activeActions_.find(local->second);
        localActionIds_.erase(local);
        if (active == activeActions_.end())
        {
            return true;
        }
        active->second.finished = true;
        if (active->second.actionEpoch == 0)
        {
            return true;
        }
        return QueueEnd(active->second);
    }


}

