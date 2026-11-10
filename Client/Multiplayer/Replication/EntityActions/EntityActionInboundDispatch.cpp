#include "Multiplayer/Replication/EntityActionReplication.h"
#include "Multiplayer/Replication/EntityActions/EntityActionSemantics.h"
#include "Multiplayer/Runtime/MultiplayerSessionContexts.h"
#include "Multiplayer/Transport/ReliableSinkDescriptorRegistry.h"


namespace fable::multiplayer::replication
{

    bool EntityActionReplication::HandleReliableMessage(
        const TransportMessage& transportMessage)
    {
        if (!initialized_ || authority_ == nullptr || lifecycle_ == nullptr ||
            transportMessage.type != protocol::PacketType::EntityAction)
        {
            return false;
        }
        protocol::EntityActionMessage message;
        if (!protocol::DecodeEntityActionMessage(
                transportMessage.payload.data(),
                transportMessage.payloadSize,
                message))
        {
            diagnostics_.Event(
                "MultiplayerEntityActionRejected",
                "invalid entity action payload");
            return true;
        }

        if (!protocol::RequiresSharedEntityAuthority(message.kind))
        {
            return true;
        }

        if (role_ == PeerRole::Host)
        {
            if (message.phase == protocol::EntityActionPhase::Intent)
            {
                if (message.ownerActorId != transportMessage.sourceActorId)
                {
                    return true;
                }
                return HostAcceptIntent(
                    std::move(message),
                    transportMessage.sourceActorId) && publication_.Publish();
            }
            if (message.phase == protocol::EntityActionPhase::Update)
            {
                const auto active = activeActions_.find(message.actionId);
                const authority::EntityAuthorityKey key{
                    message.entityUid,
                    message.entityGeneration,
                };
                const authority::ActionAuthorityLease* const lease =
                    authority_->FindActionLease(key);
                const bool mapScoped = active != activeActions_.end() &&
                    !active->second.ownsLease &&
                    entity_actions::IsMapScoped(active->second.kind) &&
                    active->second.actionEpoch == active->second.mapEpoch &&
                    authority_->IsMapPublisher(
                        active->second.mapName,
                        transportMessage.sourceActorId,
                        active->second.mapEpoch);
                const bool leaseScoped = lease != nullptr &&
                    lease->actorId == transportMessage.sourceActorId &&
                    lease->actionEpoch == message.actionEpoch;
                if (active == activeActions_.end() ||
                    active->second.ownerActorId !=
                        transportMessage.sourceActorId ||
                    active->second.entityUid != message.entityUid ||
                    active->second.entityGeneration !=
                        message.entityGeneration ||
                    active->second.mapEpoch != message.mapEpoch ||
                    active->second.actionEpoch != message.actionEpoch ||
                    active->second.mapName != message.mapName ||
                    active->second.kind != message.kind ||
                    active->second.semanticName != message.semanticName ||
                    active->second.targetEntityUid !=
                        message.targetEntityUid ||
                    active->second.targetEntityGeneration !=
                        message.targetEntityGeneration ||
                    active->second.targetPlayerActorId !=
                        message.targetPlayerActorId ||
                    active->second.abilityId != message.abilityId ||
                    active->second.abilityCharge != message.abilityCharge ||
                    (!mapScoped && !leaseScoped))
                {
                    diagnostics_.Event(
                        "MultiplayerEntityActionStale",
                        "entity action update was fenced by its current authority lease");
                    return true;
                }
                if (active->second.combatEngagement &&
                    !authority_->TouchActionLease(
                        key,
                        message.ownerActorId,
                        message.actionEpoch))
                {
                    return true;
                }
                if (active->second.combatEngagement)
                {
                    active->second.lastActivityAt = GetTickCount64();
                }
                return Queue(std::move(message)) && publication_.Publish();
            }
            if (message.phase != protocol::EntityActionPhase::End)
            {
                diagnostics_.Event(
                    "MultiplayerEntityActionRejected",
                    "guest attempted to author a non-intent action phase");
                return true;
            }
            const auto active = activeActions_.find(message.actionId);
            const authority::EntityAuthorityKey key{
                message.entityUid,
                message.entityGeneration,
            };
            const authority::ActionAuthorityLease* const lease =
                authority_->FindActionLease(key);
            const bool mapScoped = active != activeActions_.end() &&
                !active->second.ownsLease &&
                entity_actions::IsMapScoped(active->second.kind) &&
                active->second.actionEpoch == active->second.mapEpoch &&
                authority_->IsMapPublisher(
                    active->second.mapName,
                    transportMessage.sourceActorId,
                    active->second.mapEpoch);
            const bool leaseScoped = lease != nullptr &&
                lease->actorId == transportMessage.sourceActorId &&
                lease->actionEpoch == message.actionEpoch;
            if (active == activeActions_.end() ||
                active->second.ownerActorId !=
                    transportMessage.sourceActorId ||
                active->second.entityUid != message.entityUid ||
                active->second.entityGeneration !=
                    message.entityGeneration ||
                active->second.mapEpoch != message.mapEpoch ||
                active->second.actionEpoch != message.actionEpoch ||
                (!mapScoped && !leaseScoped))
            {
                diagnostics_.Event(
                    "MultiplayerEntityActionStale",
                    "guest action end was fenced by the active lease");
                return true;
            }
            active->second.finished = true;
            active->second.endQueued = true;
            active->second.pendingRelease = true;
            if (!Queue(std::move(message)) || !publication_.Publish())
            {
                return false;
            }
            return FinalizeCompletedHostActions();
        }

        if (message.phase == protocol::EntityActionPhase::Intent)
        {
            diagnostics_.Event(
                "MultiplayerEntityActionRejected",
                "guest ignored a non-host action intent");
            return true;
        }
        return AcceptAuthoritative(message);
    }


}

