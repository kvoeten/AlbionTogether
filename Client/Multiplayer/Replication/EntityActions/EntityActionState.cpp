#include "Multiplayer/Replication/EntityActionReplication.h"
#include "Multiplayer/Replication/EntityActions/EntityActionSemantics.h"
#include "Multiplayer/Runtime/MultiplayerSessionContexts.h"
#include "Multiplayer/Transport/ReliableSinkDescriptorRegistry.h"


namespace fable::multiplayer::replication
{

    bool EntityActionReplication::ReplayPendingAbilities()
    {
        for (auto& entry : activeActions_)
        {
            if (!replay_.ReplayAbility(entry.second))
            {
                return false;
            }
        }
        return true;
    }

    bool EntityActionReplication::ReplayPendingNativeActions()
    {
        for (auto& entry : activeActions_)
        {
            if (!replay_.ReplayNativeAction(entry.second))
            {
                return false;
            }
        }
        return true;
    }

    bool EntityActionReplication::PublishPeerBaseline()
    {
        if (role_ != PeerRole::Host || transport_ == nullptr)
        {
            return true;
        }
        const std::uint64_t peerRevision = transport_->PeerSetRevision();
        if (peerRevision == knownPeerRevision_)
        {
            return true;
        }
        for (const auto& [actionId, action] : activeActions_)
        {
            (void)actionId;
            if (action.actionEpoch == 0 || action.endQueued)
            {
                continue;
            }
            if (!Queue(ToMessage(
                    action,
                    protocol::EntityActionPhase::Begin)))
            {
                return false;
            }
        }
        if (!publication_.Publish())
        {
            return false;
        }
        knownPeerRevision_ = peerRevision;
        diagnostics_.Event(
            "MultiplayerEntityActionBaselinePublished",
            "current authoritative actions were replayed for the changed peer set");
        return true;
    }

    bool EntityActionReplication::QueueEnd(ActiveAction& action)
    {
        if (action.endQueued || action.actionEpoch == 0)
        {
            return true;
        }
        protocol::EntityActionMessage end = ToMessage(
            action,
            protocol::EntityActionPhase::End);
        end.outcome = protocol::EntityActionOutcome::Completed;
        if (!Queue(std::move(end)))
        {
            return false;
        }
        action.endQueued = true;
        action.pendingRelease = role_ == PeerRole::Host;
        return true;
    }

    bool EntityActionReplication::Queue(
        protocol::EntityActionMessage message)
    {
        TrackCombatAction(message);
        return publication_.Enqueue(std::move(message));
    }

    void EntityActionReplication::TrackCombatAction(
        const protocol::EntityActionMessage& message) noexcept
    {
        if (combatLedger_ == nullptr || message.actionId == 0 ||
            message.entityUid == 0 || message.entityGeneration == 0 ||
            message.mapEpoch == 0 || message.actionEpoch == 0)
        {
            return;
        }
        const combat::CombatSourceAction action{
            {combat::CombatSubjectKind::WorldEntity,
             message.entityUid,
             message.entityGeneration,
             message.mapEpoch},
            message.actionId,
            message.actionEpoch};
        switch (message.phase)
        {
        case protocol::EntityActionPhase::Begin:
            (void)combatLedger_->Begin(action, GetTickCount64());
            break;
        case protocol::EntityActionPhase::Update:
            (void)combatLedger_->Touch(action);
            break;
        case protocol::EntityActionPhase::End:
            (void)combatLedger_->Finish(action, GetTickCount64());
            break;
        default:
            break;
        }
    }

    protocol::EntityActionMessage EntityActionReplication::ToMessage(
        const ActiveAction& action,
        protocol::EntityActionPhase phase) const
    {
        protocol::EntityActionMessage message;
        message.phase = phase;
        message.kind = action.kind;
        message.flags = action.flags;
        message.entityUid = action.entityUid;
        message.entityGeneration = action.entityGeneration;
        message.actionId = action.actionId;
        message.ownerActorId = action.ownerActorId;
        message.targetEntityUid = action.targetEntityUid;
        message.targetEntityGeneration = action.targetEntityGeneration;
        message.targetPlayerActorId = action.targetPlayerActorId;
        message.mapEpoch = action.mapEpoch;
        message.actionEpoch = phase == protocol::EntityActionPhase::Intent
            ? 0
            : action.actionEpoch;
        message.abilityId = action.abilityId;
        message.abilityCharge = action.abilityCharge;
        message.mapName = action.mapName;
        message.semanticName = action.semanticName;
        return message;
    }

    std::uint64_t EntityActionReplication::NextActionId() noexcept
    {
        ++nextActionId_;
        if (nextActionId_ == 0)
        {
            ++nextActionId_;
        }
        constexpr std::uint64_t kGoldenRatio = 0x9E3779B97F4A7C15ull;
        const std::uint64_t mixed =
            (nextActionId_ * kGoldenRatio) ^ localActorId_;
        return mixed != 0 ? mixed : nextActionId_;
    }

}

