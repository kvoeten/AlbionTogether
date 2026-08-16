#include "ActionAuthorityCoordinator.h"

#include <Windows.h>

#include <cstdio>
#include <utility>
#include <vector>

namespace fable::multiplayer::authority
{
    void ActionAuthorityCoordinator::Initialize(
        PeerRole localRole,
        std::uint64_t localActorId,
        const core::Diagnostics& diagnostics)
    {
        Clear();
        localRole_ = localRole;
        localActorId_ = localActorId;
        diagnostics_ = diagnostics;
    }

    bool ActionAuthorityCoordinator::HostAcquire(
        const protocol::EntityActionMessage& intent,
        std::uint64_t sourceActorId,
        const MapAuthorityLease& mapLease,
        ActionAuthorityLease& grantedLease)
    {
        grantedLease = {};
        if (localRole_ != PeerRole::Host ||
            intent.phase != protocol::EntityActionPhase::Intent ||
            sourceActorId == 0 || intent.ownerActorId != sourceActorId ||
            intent.entityUid == 0 || intent.entityGeneration == 0 ||
            intent.mapName != mapLease.mapName ||
            intent.mapEpoch != mapLease.epoch)
        {
            return false;
        }

        const protocol::ActionLeaseKind leaseKind = LeaseKindFor(intent);
        if (leaseKind == protocol::ActionLeaseKind::None)
        {
            return false;
        }
        const EntityAuthorityKey key{
            intent.entityUid,
            intent.entityGeneration,
        };
        const auto existing = leases_.find(key);
        if (existing != leases_.end())
        {
            const std::uint64_t now = GetTickCount64();
            if (existing->second.actorId == sourceActorId &&
                existing->second.kind == leaseKind &&
                existing->second.mapEpoch == mapLease.epoch &&
                existing->second.mapName == mapLease.mapName)
            {
                existing->second.lastActivityAt = now;
                grantedLease = existing->second;
                return true;
            }
            const bool primaryAttackerHandoff =
                leaseKind == protocol::ActionLeaseKind::PrimaryAttacker &&
                existing->second.kind ==
                    protocol::ActionLeaseKind::PrimaryAttacker &&
                now - existing->second.grantedAt >=
                    PrimaryAttackerMinimumHoldMilliseconds &&
                now - existing->second.lastActivityAt >=
                    PrimaryAttackerIdleHandoffMilliseconds;
            if (primaryAttackerHandoff ||
                PriorityFor(leaseKind) > PriorityFor(existing->second.kind))
            {
                const ActionAuthorityLease released = existing->second;
                leases_.erase(existing);
                QueueRelease(released);
                ReportChange(
                    released,
                    primaryAttackerHandoff
                        ? "primary-attacker-handoff"
                        : "priority-preempt");
            }
            else
            {
                // Conversation, trade, quest, and combat actions are
                // exclusive at this boundary. Combat handoff is an explicit
                // release/regrant so its epoch changes and stale attacker
                // updates are fenced.
                return false;
            }
        }

        ActionAuthorityLease lease;
        lease.entity = key;
        lease.kind = leaseKind;
        lease.actorId = sourceActorId;
        lease.mapEpoch = mapLease.epoch;
        lease.actionEpoch = NextEpoch();
        lease.grantedAt = GetTickCount64();
        lease.lastActivityAt = lease.grantedAt;
        lease.mapName = mapLease.mapName;
        lease.localAuthority = sourceActorId == localActorId_;
        leases_.emplace(key, lease);
        QueueGrant(lease);
        ReportChange(lease, "grant");
        grantedLease = lease;
        return true;
    }

    bool ActionAuthorityCoordinator::HostRelease(
        const EntityAuthorityKey& entity,
        std::uint64_t requestingActorId,
        std::uint32_t actionEpoch)
    {
        if (localRole_ != PeerRole::Host)
        {
            return false;
        }
        const auto existing = leases_.find(entity);
        if (existing == leases_.end() ||
            existing->second.actorId != requestingActorId ||
            existing->second.actionEpoch != actionEpoch)
        {
            return false;
        }
        const ActionAuthorityLease released = existing->second;
        leases_.erase(existing);
        QueueRelease(released);
        ReportChange(released, "release");
        return true;
    }

    bool ActionAuthorityCoordinator::HostTouch(
        const EntityAuthorityKey& entity,
        std::uint64_t actorId,
        std::uint32_t actionEpoch) noexcept
    {
        if (localRole_ != PeerRole::Host)
        {
            return false;
        }
        const auto existing = leases_.find(entity);
        if (existing == leases_.end() || existing->second.actorId != actorId ||
            existing->second.actionEpoch != actionEpoch)
        {
            return false;
        }
        existing->second.lastActivityAt = GetTickCount64();
        return true;
    }

    void ActionAuthorityCoordinator::HostFenceAgainstMaps(
        const MapAuthorityCoordinator& maps,
        const std::unordered_map<std::uint64_t, std::string>& actorMaps)
    {
        if (localRole_ != PeerRole::Host)
        {
            return;
        }
        std::vector<EntityAuthorityKey> stale;
        stale.reserve(leases_.size());
        for (const auto& entry : leases_)
        {
            const MapAuthorityLease* const map = maps.Find(
                entry.second.mapName);
            const auto actor = actorMaps.find(entry.second.actorId);
            if (map == nullptr || map->epoch != entry.second.mapEpoch ||
                actor == actorMaps.end() ||
                actor->second != entry.second.mapName)
            {
                stale.push_back(entry.first);
            }
        }
        for (const EntityAuthorityKey& key : stale)
        {
            const auto existing = leases_.find(key);
            if (existing == leases_.end())
            {
                continue;
            }
            const ActionAuthorityLease released = existing->second;
            leases_.erase(existing);
            QueueRelease(released);
            ReportChange(released, "map-or-actor-fence-release");
        }
    }

    bool ActionAuthorityCoordinator::Apply(
        const protocol::AuthorityMessage& message)
    {
        if (message.scope != protocol::AuthorityScope::EntityAction ||
            message.entityUid == 0 || message.entityGeneration == 0 ||
            message.actionEpoch == 0 || message.mapEpoch == 0 ||
            message.mapName.empty())
        {
            return false;
        }
        const EntityAuthorityKey key{
            message.entityUid,
            message.entityGeneration,
        };
        const auto existing = leases_.find(key);
        if (existing != leases_.end() &&
            (message.actionEpoch < existing->second.actionEpoch ||
                (message.operation == protocol::AuthorityOperation::Grant &&
                    message.actionEpoch == existing->second.actionEpoch)))
        {
            return false;
        }

        if (message.operation == protocol::AuthorityOperation::Release)
        {
            // A release for an already retired local lease is harmless. It is
            // still accepted because reliable ordering may deliver it after a
            // local map fence erased presentation state.
            if (existing != leases_.end())
            {
                const ActionAuthorityLease released = existing->second;
                leases_.erase(existing);
                ReportChange(released, "release");
            }
            return true;
        }
        if (message.operation != protocol::AuthorityOperation::Grant ||
            message.ownerActorId == 0 ||
            message.actionKind == protocol::ActionLeaseKind::None)
        {
            return false;
        }

        ActionAuthorityLease lease;
        lease.entity = key;
        lease.kind = message.actionKind;
        lease.actorId = message.ownerActorId;
        lease.mapEpoch = message.mapEpoch;
        lease.actionEpoch = message.actionEpoch;
        lease.grantedAt = GetTickCount64();
        lease.lastActivityAt = lease.grantedAt;
        lease.mapName = message.mapName;
        lease.localAuthority = lease.actorId == localActorId_;
        leases_[key] = lease;
        if (lease.actionEpoch > nextEpoch_)
        {
            nextEpoch_ = lease.actionEpoch;
        }
        ReportChange(lease, "grant");
        return true;
    }

    void ActionAuthorityCoordinator::QueueBaseline()
    {
        if (localRole_ != PeerRole::Host)
        {
            return;
        }
        for (const auto& entry : leases_)
        {
            QueueGrant(entry.second);
        }
    }

    bool ActionAuthorityCoordinator::TakePending(
        protocol::AuthorityMessage& message)
    {
        if (pending_.empty())
        {
            return false;
        }
        message = std::move(pending_.front());
        pending_.pop_front();
        return true;
    }

    void ActionAuthorityCoordinator::RestorePending(
        protocol::AuthorityMessage message)
    {
        pending_.push_front(std::move(message));
    }

    const ActionAuthorityLease* ActionAuthorityCoordinator::Find(
        const EntityAuthorityKey& entity) const noexcept
    {
        const auto match = leases_.find(entity);
        return match != leases_.end() ? &match->second : nullptr;
    }

    bool ActionAuthorityCoordinator::HasOwnedOverride(
        const std::string& mapName,
        std::uint64_t actorId,
        std::uint32_t mapEpoch) const noexcept
    {
        if (mapName.empty() || actorId == 0 || mapEpoch == 0)
        {
            return false;
        }
        for (const auto& entry : leases_)
        {
            const ActionAuthorityLease& lease = entry.second;
            if (lease.kind != protocol::ActionLeaseKind::Ambient &&
                lease.actorId == actorId && lease.mapEpoch == mapEpoch &&
                lease.mapName == mapName)
            {
                return true;
            }
        }
        return false;
    }

    void ActionAuthorityCoordinator::Clear() noexcept
    {
        leases_.clear();
        pending_.clear();
        localRole_ = PeerRole::Guest;
        localActorId_ = 0;
        nextEpoch_ = 0;
        diagnostics_ = {};
    }

    protocol::ActionLeaseKind ActionAuthorityCoordinator::LeaseKindFor(
        const protocol::EntityActionMessage& intent) noexcept
    {
        if (intent.kind == protocol::EntityActionKind::Combat &&
            intent.semanticName == "PlayerAttackEngagement")
        {
            return protocol::ActionLeaseKind::PrimaryAttacker;
        }
        switch (intent.kind)
        {
        case protocol::EntityActionKind::Conversation:
        case protocol::EntityActionKind::ConversationAnimation:
        case protocol::EntityActionKind::Trade:
            return protocol::ActionLeaseKind::Conversation;
        case protocol::EntityActionKind::Combat:
            return protocol::ActionLeaseKind::Combat;
        case protocol::EntityActionKind::QuestOrCutscene:
            return protocol::ActionLeaseKind::QuestOrCutscene;
        case protocol::EntityActionKind::Native:
        case protocol::EntityActionKind::Movement:
            // Ordinary native/movement actions inherit the already ordered
            // map publisher lease. Creating a second per-entity lease for
            // every idle/animation action only adds churn and can race a real
            // exclusive combat or conversation handoff.
            return protocol::ActionLeaseKind::None;
        default:
            return protocol::ActionLeaseKind::None;
        }
    }

    unsigned int ActionAuthorityCoordinator::PriorityFor(
        protocol::ActionLeaseKind kind) noexcept
    {
        switch (kind)
        {
        case protocol::ActionLeaseKind::Ambient:
            return 1;
        case protocol::ActionLeaseKind::Conversation:
            return 2;
        case protocol::ActionLeaseKind::Combat:
            return 3;
        case protocol::ActionLeaseKind::PrimaryAttacker:
            return 4;
        case protocol::ActionLeaseKind::QuestOrCutscene:
            return 5;
        default:
            return 0;
        }
    }

    std::uint32_t ActionAuthorityCoordinator::NextEpoch() noexcept
    {
        ++nextEpoch_;
        if (nextEpoch_ == 0)
        {
            ++nextEpoch_;
        }
        return nextEpoch_;
    }

    void ActionAuthorityCoordinator::QueueGrant(
        const ActionAuthorityLease& lease)
    {
        protocol::AuthorityMessage message;
        message.operation = protocol::AuthorityOperation::Grant;
        message.scope = protocol::AuthorityScope::EntityAction;
        message.actionKind = lease.kind;
        message.ownerActorId = lease.actorId;
        message.entityUid = lease.entity.thingUid;
        message.entityGeneration = lease.entity.generation;
        message.mapEpoch = lease.mapEpoch;
        message.actionEpoch = lease.actionEpoch;
        message.mapName = lease.mapName;
        pending_.push_back(std::move(message));
    }

    void ActionAuthorityCoordinator::QueueRelease(
        const ActionAuthorityLease& lease)
    {
        protocol::AuthorityMessage message;
        message.operation = protocol::AuthorityOperation::Release;
        message.scope = protocol::AuthorityScope::EntityAction;
        message.actionKind = lease.kind;
        message.entityUid = lease.entity.thingUid;
        message.entityGeneration = lease.entity.generation;
        message.mapEpoch = lease.mapEpoch;
        message.actionEpoch = lease.actionEpoch;
        message.mapName = lease.mapName;
        pending_.push_back(std::move(message));
    }

    void ActionAuthorityCoordinator::ReportChange(
        const ActionAuthorityLease& lease,
        const char* operation)
    {
        char detail[384] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "operation=%s thing_uid=%016llX generation=%u kind=%u authority_actor_id=%llu map=%s map_epoch=%u action_epoch=%u local=%s resolver=%s",
            operation,
            static_cast<unsigned long long>(lease.entity.thingUid),
            lease.entity.generation,
            static_cast<unsigned int>(lease.kind),
            static_cast<unsigned long long>(lease.actorId),
            lease.mapName.c_str(),
            lease.mapEpoch,
            lease.actionEpoch,
            lease.actorId != 0 && lease.actorId == localActorId_
                ? "true"
                : "false",
            localRole_ == PeerRole::Host ? "host" : "host-message");
        diagnostics_.Event("MultiplayerActionAuthorityChanged", detail);
    }
}
