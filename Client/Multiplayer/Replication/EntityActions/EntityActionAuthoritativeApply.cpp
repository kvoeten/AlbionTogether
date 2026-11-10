#include "Multiplayer/Replication/EntityActionReplication.h"
#include "Multiplayer/Replication/EntityActions/EntityActionSemantics.h"
#include "Multiplayer/Runtime/MultiplayerSessionContexts.h"
#include "Multiplayer/Transport/ReliableSinkDescriptorRegistry.h"


namespace fable::multiplayer::replication
{

    bool EntityActionReplication::AcceptAuthoritative(
        const protocol::EntityActionMessage& message)
    {
        if (!protocol::RequiresSharedEntityAuthority(message.kind))
        {
            return true;
        }
        const authority::EntityAuthorityKey key{
            message.entityUid,
            message.entityGeneration,
        };
        const authority::ActionAuthorityLease* const lease =
            authority_->FindActionLease(key);
        if (message.phase == protocol::EntityActionPhase::Begin)
        {
            const bool mapScoped = entity_actions::IsMapScoped(message.kind) &&
                message.actionEpoch == message.mapEpoch &&
                authority_->IsMapPublisher(
                    message.mapName,
                    message.ownerActorId,
                    message.mapEpoch);
            const bool leaseScoped = lease != nullptr &&
                lease->actorId == message.ownerActorId &&
                lease->mapEpoch == message.mapEpoch &&
                lease->actionEpoch == message.actionEpoch;
            if (!mapScoped && !leaseScoped)
            {
                diagnostics_.Event(
                    "MultiplayerEntityActionStale",
                    "action begin arrived without its ordered map or action authority fence");
                return true;
            }
            TrackCombatAction(message);
            const auto existing = activeActions_.find(message.actionId);
            if (existing != activeActions_.end() &&
                existing->second.actionEpoch == message.actionEpoch &&
                existing->second.entityUid == message.entityUid &&
                existing->second.entityGeneration ==
                    message.entityGeneration &&
                existing->second.ownerActorId == message.ownerActorId &&
                existing->second.mapEpoch == message.mapEpoch &&
                existing->second.mapName == message.mapName &&
                existing->second.kind == message.kind &&
                existing->second.semanticName == message.semanticName &&
                existing->second.targetEntityUid ==
                    message.targetEntityUid &&
                existing->second.targetEntityGeneration ==
                    message.targetEntityGeneration &&
                existing->second.targetPlayerActorId ==
                    message.targetPlayerActorId &&
                existing->second.abilityId == message.abilityId &&
                existing->second.abilityCharge == message.abilityCharge)
            {
                // Peer-set baselines and reliable retransmission can repeat an
                // accepted Begin. Existing observers must not restart a
                // one-shot native action; a new observer has no entry and
                // still consumes this message normally.
                return true;
            }
            ActiveAction& active = activeActions_[message.actionId];
            const bool localOrigin = active.localOrigin;
            void* const nativeAction = active.nativeAction;
            const bool alreadyFinished = active.finished;
            const bool combatEngagement = active.combatEngagement ||
                (message.kind == protocol::EntityActionKind::Combat &&
                    message.semanticName == "PlayerAttackEngagement");
            const std::uint64_t lastActivityAt = active.lastActivityAt;
            active = {};
            active.entityUid = message.entityUid;
            active.entityGeneration = message.entityGeneration;
            active.actionId = message.actionId;
            active.ownerActorId = message.ownerActorId;
            active.targetEntityUid = message.targetEntityUid;
            active.targetEntityGeneration = message.targetEntityGeneration;
            active.targetPlayerActorId = message.targetPlayerActorId;
            active.mapEpoch = message.mapEpoch;
            active.actionEpoch = message.actionEpoch;
            active.kind = message.kind;
            active.flags = message.flags;
            active.abilityId = message.abilityId;
            active.abilityCharge = message.abilityCharge;
            active.mapName = message.mapName;
            active.semanticName = message.semanticName;
            active.nativeAbility =
                message.semanticName == "CreatureAbility";
            active.localOrigin = localOrigin;
            active.nativeAction = nativeAction;
            active.finished = alreadyFinished;
            active.combatEngagement = combatEngagement;
            active.ownsLease = !mapScoped &&
                !entity_actions::IsMapScoped(message.kind) &&
                !(lease != nullptr &&
                    lease->kind ==
                        protocol::ActionLeaseKind::PrimaryAttacker &&
                    !combatEngagement);
            active.lastActivityAt = lastActivityAt != 0
                ? lastActivityAt
                : GetTickCount64();
            if (active.combatEngagement)
            {
                combatActionIds_[active.entityUid] = active.actionId;
            }

            char detail[384] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "thing_uid=%016llX generation=%u action_id=%llu owner=%llu kind=%u semantic=%s local_owner=%s",
                static_cast<unsigned long long>(message.entityUid),
                message.entityGeneration,
                static_cast<unsigned long long>(message.actionId),
                static_cast<unsigned long long>(message.ownerActorId),
                static_cast<unsigned int>(message.kind),
                message.semanticName.c_str(),
                message.ownerActorId == localActorId_ ? "true" : "false");
            diagnostics_.Event("MultiplayerEntityActionBegan", detail);
            if (!replay_.ReplayAbility(active))
            {
                return false;
            }
            if (!replay_.ReplayNativeAction(active))
            {
                return false;
            }
            if (active.localOrigin && active.finished)
            {
                return QueueEnd(active) && publication_.Publish();
            }
            // Concrete verified codecs will replay native actions here. Until
            // then the authoritative semantic action is retained without
            // copying native action object memory.
            return true;
        }
        if (message.phase == protocol::EntityActionPhase::End)
        {
            TrackCombatAction(message);
            const auto active = activeActions_.find(message.actionId);
            if (active != activeActions_.end())
            {
                ForgetCombatEngagement(active->second);
                if (active->second.nativeAction != nullptr)
                {
                    localActionIds_.erase(active->second.nativeAction);
                }
                activeActions_.erase(active);
            }
            diagnostics_.Event(
                "MultiplayerEntityActionEnded",
                message.semanticName.c_str());
            return true;
        }
        if (message.phase == protocol::EntityActionPhase::Update)
        {
            const auto active = activeActions_.find(message.actionId);
            const bool mapScoped = active != activeActions_.end() &&
                !active->second.ownsLease &&
                entity_actions::IsMapScoped(active->second.kind) &&
                active->second.actionEpoch == active->second.mapEpoch &&
                authority_->IsMapPublisher(
                    active->second.mapName,
                    message.ownerActorId,
                    active->second.mapEpoch);
            const bool leaseScoped = lease != nullptr &&
                lease->actorId == message.ownerActorId &&
                lease->actionEpoch == message.actionEpoch;
            if (active == activeActions_.end() ||
                active->second.entityUid != message.entityUid ||
                active->second.entityGeneration !=
                    message.entityGeneration ||
                active->second.ownerActorId != message.ownerActorId ||
                active->second.mapEpoch != message.mapEpoch ||
                active->second.actionEpoch != message.actionEpoch ||
                active->second.mapName != message.mapName ||
                active->second.kind != message.kind ||
                active->second.semanticName != message.semanticName ||
                active->second.targetEntityUid != message.targetEntityUid ||
                active->second.targetEntityGeneration !=
                    message.targetEntityGeneration ||
                active->second.targetPlayerActorId !=
                    message.targetPlayerActorId ||
                active->second.abilityId != message.abilityId ||
                active->second.abilityCharge != message.abilityCharge ||
                (!mapScoped && !leaseScoped))
            {
                return true;
            }
            if (active->second.combatEngagement)
            {
                active->second.lastActivityAt = GetTickCount64();
            }
            TrackCombatAction(message);
            return true;
        }
        return false;
    }

    bool EntityActionReplication::FinalizeCompletedHostActions()
    {
        if (role_ != PeerRole::Host || !publication_.Empty())
        {
            return true;
        }
        std::vector<std::uint64_t> completed;
        for (const auto& entry : activeActions_)
        {
            if (entry.second.pendingRelease)
            {
                completed.push_back(entry.first);
            }
        }
        for (const std::uint64_t actionId : completed)
        {
            const auto active = activeActions_.find(actionId);
            if (active == activeActions_.end())
            {
                continue;
            }
            const authority::EntityAuthorityKey key{
                active->second.entityUid,
                active->second.entityGeneration,
            };
            const authority::ActionAuthorityLease* const lease =
                authority_->FindActionLease(key);
            if (lease == nullptr ||
                lease->actorId != active->second.ownerActorId ||
                lease->actionEpoch != active->second.actionEpoch)
            {
                if (active->second.nativeAction != nullptr)
                {
                    localActionIds_.erase(active->second.nativeAction);
                }
                ForgetCombatEngagement(active->second);
                activeActions_.erase(active);
                continue;
            }
            if (active->second.ownsLease &&
                !authority_->ReleaseActionLease(
                    key,
                    active->second.ownerActorId,
                    active->second.actionEpoch))
            {
                return false;
            }
            if (active->second.nativeAction != nullptr)
            {
                localActionIds_.erase(active->second.nativeAction);
            }
            ForgetCombatEngagement(active->second);
            activeActions_.erase(active);
        }
        return true;
    }

    void EntityActionReplication::PruneFencedActions()
    {
        if (authority_ == nullptr)
        {
            return;
        }
        std::vector<std::uint64_t> stale;
        stale.reserve(activeActions_.size());
        for (const auto& entry : activeActions_)
        {
            const ActiveAction& action = entry.second;
            if (action.actionEpoch == 0)
            {
                if (action.nativeAbility && action.nativeAction == nullptr &&
                    action.lastActivityAt != 0 &&
                    GetTickCount64() > action.lastActivityAt + 1'000)
                {
                    stale.push_back(entry.first);
                }
                continue;
            }
            const authority::EntityAuthorityKey key{
                action.entityUid,
                action.entityGeneration,
            };
            const authority::ActionAuthorityLease* const lease =
                authority_->FindActionLease(key);
            const bool mapScoped = entity_actions::IsMapScoped(action.kind) &&
                action.actionEpoch == action.mapEpoch &&
                authority_->IsMapPublisher(
                    action.mapName,
                    action.ownerActorId,
                    action.mapEpoch);
            const bool leaseScoped = lease != nullptr &&
                lease->actorId == action.ownerActorId &&
                lease->mapEpoch == action.mapEpoch &&
                lease->actionEpoch == action.actionEpoch;
            if (!mapScoped && !leaseScoped)
            {
                stale.push_back(entry.first);
            }
        }
        for (const std::uint64_t actionId : stale)
        {
            const auto active = activeActions_.find(actionId);
            if (active == activeActions_.end())
            {
                continue;
            }
            if (active->second.nativeAction != nullptr)
            {
                localActionIds_.erase(active->second.nativeAction);
            }
            ForgetPendingAbility(active->second);
            ForgetCombatEngagement(active->second);
            diagnostics_.Event(
                "MultiplayerEntityActionFenced",
                active->second.semanticName.c_str());
            activeActions_.erase(active);
        }
    }

}
