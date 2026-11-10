#include "Multiplayer/Replication/EntityActionReplication.h"
#include "Multiplayer/Replication/EntityActions/EntityActionSemantics.h"
#include "Multiplayer/Runtime/MultiplayerSessionContexts.h"
#include "Multiplayer/Transport/ReliableSinkDescriptorRegistry.h"


namespace fable::multiplayer::replication
{

    bool EntityActionReplication::BeginOrRefreshCombatEngagement(
        const game::creature::combat::CreatureAbilityEvent& event,
        const std::string& localMap,
        std::uint32_t mapEpoch)
    {
        if (event.targetThingUid == 0 || localMap.empty() || mapEpoch == 0)
        {
            return true;
        }
        const entities::WorldEntityRecord* const entity =
            lifecycle_->Directory().Find(event.targetThingUid);
        if (entity == nullptr || !entity->live || !entity->available ||
            !entity->creature || entity->mapName != localMap)
        {
            return true;
        }

        const auto indexed = combatActionIds_.find(event.targetThingUid);
        if (indexed != combatActionIds_.end())
        {
            const auto active = activeActions_.find(indexed->second);
            if (active != activeActions_.end() &&
                active->second.combatEngagement &&
                active->second.localOrigin &&
                active->second.ownerActorId == localActorId_ &&
                active->second.entityGeneration == entity->generation &&
                !active->second.endQueued)
            {
                active->second.lastActivityAt = event.observedAt != 0
                    ? event.observedAt
                    : GetTickCount64();
                if (active->second.actionEpoch != 0)
                {
                    return QueueUpdate(active->second);
                }

                protocol::EntityActionMessage retry = ToMessage(
                    active->second,
                    protocol::EntityActionPhase::Intent);
                if (role_ == PeerRole::Host)
                {
                    return HostAcceptIntent(
                        std::move(retry),
                        localActorId_);
                }
                return Queue(std::move(retry));
            }
            combatActionIds_.erase(indexed);
        }

        ActiveAction active;
        active.entityUid = event.targetThingUid;
        active.entityGeneration = entity->generation;
        active.actionId = NextActionId();
        active.ownerActorId = localActorId_;
        active.mapEpoch = mapEpoch;
        active.kind = protocol::EntityActionKind::Combat;
        active.flags = entity_actions::DefaultFlags(active.kind);
        active.mapName = localMap;
        active.semanticName = "PlayerAttackEngagement";
        active.lastActivityAt = event.observedAt != 0
            ? event.observedAt
            : GetTickCount64();
        active.localOrigin = true;
        active.combatEngagement = true;
        const std::uint64_t actionId = active.actionId;
        activeActions_.emplace(actionId, active);
        combatActionIds_[active.entityUid] = actionId;

        protocol::EntityActionMessage intent = ToMessage(
            activeActions_.at(actionId),
            protocol::EntityActionPhase::Intent);
        if (role_ == PeerRole::Host)
        {
            return HostAcceptIntent(std::move(intent), localActorId_);
        }
        diagnostics_.Event(
            "MultiplayerCombatEngagementRequested",
            "local player attack requested primary-attacker authority for its selected NPC");
        return Queue(std::move(intent));
    }

    bool EntityActionReplication::QueueUpdate(ActiveAction& action)
    {
        if (action.actionEpoch == 0 || action.endQueued)
        {
            return true;
        }
        if (role_ == PeerRole::Host && action.combatEngagement)
        {
            const authority::EntityAuthorityKey key{
                action.entityUid,
                action.entityGeneration,
            };
            if (!authority_->TouchActionLease(
                    key,
                    action.ownerActorId,
                    action.actionEpoch))
            {
                return true;
            }
        }
        return Queue(ToMessage(action, protocol::EntityActionPhase::Update));
    }

    bool EntityActionReplication::ExpireCombatEngagements(
        std::uint64_t now)
    {
        for (auto& entry : activeActions_)
        {
            ActiveAction& action = entry.second;
            if (!action.combatEngagement || action.endQueued ||
                action.lastActivityAt == 0 ||
                now - action.lastActivityAt < CombatIdleMilliseconds)
            {
                continue;
            }
            // The host is the final timeout authority. A guest only closes
            // the engagement it originated; observers wait for host End.
            if (role_ == PeerRole::Host || action.localOrigin)
            {
                action.finished = true;
                if (!QueueEnd(action))
                {
                    return false;
                }
            }
        }
        return true;
    }

    void EntityActionReplication::ForgetCombatEngagement(
        const ActiveAction& action) noexcept
    {
        if (!action.combatEngagement)
        {
            return;
        }
        const auto indexed = combatActionIds_.find(action.entityUid);
        if (indexed != combatActionIds_.end() &&
            indexed->second == action.actionId)
        {
            combatActionIds_.erase(indexed);
        }
    }

}
