#include "Multiplayer/Replication/EntityActionReplication.h"
#include "Multiplayer/Replication/EntityActions/EntityActionSemantics.h"
#include "Multiplayer/Runtime/MultiplayerSessionContexts.h"
#include "Multiplayer/Transport/ReliableSinkDescriptorRegistry.h"


namespace fable::multiplayer::replication
{

    bool EntityActionReplication::HostAcceptIntent(
        protocol::EntityActionMessage intent,
        std::uint64_t sourceActorId)
    {
        const bool requestedCombatEngagement = intent.kind ==
                protocol::EntityActionKind::Combat &&
            intent.semanticName == "PlayerAttackEngagement";
        if (requestedCombatEngagement)
        {
            char detail[320] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "stage=received thing_uid=%016llX generation=%u action_id=%llu source_actor=%llu map=%s map_epoch=%u",
                static_cast<unsigned long long>(intent.entityUid),
                intent.entityGeneration,
                static_cast<unsigned long long>(intent.actionId),
                static_cast<unsigned long long>(sourceActorId),
                intent.mapName.c_str(),
                intent.mapEpoch);
            diagnostics_.Event("MultiplayerCombatEngagementHostTrace", detail);
        }
        const entities::WorldEntityRecord* const entity =
            lifecycle_->Directory().Find(intent.entityUid);
        if (entity == nullptr || !entity->live || !entity->available ||
            entity->mapName != intent.mapName ||
            (intent.entityGeneration != 0 &&
                intent.entityGeneration != entity->generation))
        {
            diagnostics_.Event(
                "MultiplayerEntityActionRejected",
                "action intent did not address the current live generation");
            return true;
        }
        if (activeActions_.find(intent.actionId) != activeActions_.end() &&
            activeActions_.at(intent.actionId).ownerActorId != sourceActorId)
        {
            diagnostics_.Event(
                "MultiplayerEntityActionRejected",
                "action identifier collided with another owner");
            return true;
        }

        intent.entityGeneration = entity->generation;
        intent.ownerActorId = sourceActorId;
        authority::ActionAuthorityLease granted;
        const authority::EntityAuthorityKey key{
            intent.entityUid, intent.entityGeneration};
        const authority::ActionAuthorityLease* const existingLease =
            authority_->FindActionLease(key);
        const bool combatEngagement = requestedCombatEngagement;
        const unsigned int requiredPriority =
            entity_actions::Priority(intent);
        const bool borrowExistingLease = !combatEngagement &&
            existingLease != nullptr &&
            existingLease->actorId == sourceActorId &&
            existingLease->mapEpoch == intent.mapEpoch &&
            existingLease->mapName == intent.mapName &&
            entity_actions::LeasePriority(existingLease->kind) >=
                requiredPriority;
        const bool mapScoped = entity_actions::IsMapScoped(intent.kind) &&
            existingLease == nullptr &&
            authority_->IsMapPublisher(
                intent.mapName, sourceActorId, intent.mapEpoch);
        if (borrowExistingLease)
        {
            granted = *existingLease;
            authority_->TouchActionLease(
                key, sourceActorId, granted.actionEpoch);
        }
        else if (mapScoped)
        {
            granted.entity = key;
            granted.kind = protocol::ActionLeaseKind::Ambient;
            granted.actorId = sourceActorId;
            granted.mapEpoch = intent.mapEpoch;
            // Map-scoped actions are fenced by the existing map epoch. They
            // do not create another authority record or network grant.
            granted.actionEpoch = intent.mapEpoch;
            granted.mapName = intent.mapName;
            granted.localAuthority = sourceActorId == localActorId_;
        }
        else if (!authority_->RequestActionLease(
                intent, sourceActorId, granted))
        {
            diagnostics_.Event(
                "MultiplayerEntityActionRejected",
                "host denied the entity action lease");
            const auto local = activeActions_.find(intent.actionId);
            if (local != activeActions_.end() && local->second.localOrigin)
            {
                ForgetCombatEngagement(local->second);
                localActionIds_.erase(local->second.nativeAction);
                activeActions_.erase(local);
            }
            return true;
        }

        if (combatEngagement)
        {
            char detail[320] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "stage=lease-granted thing_uid=%016llX generation=%u source_actor=%llu action_epoch=%u lease_kind=%u",
                static_cast<unsigned long long>(intent.entityUid),
                intent.entityGeneration,
                static_cast<unsigned long long>(sourceActorId),
                granted.actionEpoch,
                static_cast<unsigned int>(granted.kind));
            diagnostics_.Event("MultiplayerCombatEngagementHostTrace", detail);
        }

        ActiveAction& active = activeActions_[intent.actionId];
        active.entityUid = intent.entityUid;
        active.entityGeneration = intent.entityGeneration;
        active.actionId = intent.actionId;
        active.ownerActorId = sourceActorId;
        active.targetEntityUid = intent.targetEntityUid;
        active.targetEntityGeneration = intent.targetEntityGeneration;
        active.targetPlayerActorId = intent.targetPlayerActorId;
        active.mapEpoch = intent.mapEpoch;
        active.actionEpoch = granted.actionEpoch;
        active.kind = intent.kind;
        active.flags = intent.flags;
        active.abilityId = intent.abilityId;
        active.abilityCharge = intent.abilityCharge;
        active.mapName = intent.mapName;
        active.semanticName = intent.semanticName;
        active.nativeAbility = intent.semanticName == "CreatureAbility";
        active.combatEngagement = combatEngagement;
        active.ownsLease = !borrowExistingLease && !mapScoped;
        if (active.combatEngagement)
        {
            active.lastActivityAt = GetTickCount64();
            combatActionIds_[active.entityUid] = active.actionId;
        }
        protocol::EntityActionMessage begin = ToMessage(
            active,
            protocol::EntityActionPhase::Begin);
        if (active.combatEngagement)
        {
            diagnostics_.Event(
                "MultiplayerCombatEngagementHostTrace",
                "stage=begin-queued");
        }
        if (!Queue(std::move(begin)))
        {
            return false;
        }
        char detail[384] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "thing_uid=%016llX generation=%u action_id=%llu owner=%llu kind=%u semantic=%s local_owner=%s",
            static_cast<unsigned long long>(active.entityUid),
            active.entityGeneration,
            static_cast<unsigned long long>(active.actionId),
            static_cast<unsigned long long>(active.ownerActorId),
            static_cast<unsigned int>(active.kind),
            active.semanticName.c_str(),
            active.ownerActorId == localActorId_ ? "true" : "false");
        diagnostics_.Event("MultiplayerEntityActionBegan", detail);
        if (!replay_.ReplayAbility(active))
        {
            return false;
        }
        if (!replay_.ReplayNativeAction(active))
        {
            return false;
        }
        if (active.finished)
        {
            return QueueEnd(active);
        }
        return true;
    }


}

