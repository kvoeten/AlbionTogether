#include "QuestLifecycleReplicationAdapter.h"

#include "Game/Quest/Runtime/QuestLifecycleHook.h"
#include "Game/Quest/Policy/QuestPolicyRegistry.h"
#include "Multiplayer/Authority/AuthorityReplication.h"
#include "Multiplayer/Persistence/QuestMutationReplication.h"
#include "Multiplayer/Persistence/QuestStateAuthorityService.h"
#include "Multiplayer/Protocol/QuestMutationMessage.h"
#include "Multiplayer/Replication/LocalHeroReplication.h"
#include "Multiplayer/Replication/RemotePlayerChannels.h"

#include <algorithm>
#include <cstdio>

namespace fable::multiplayer::persistence
{
    void QuestLifecycleReplicationAdapter::Initialize(
        const PeerRole role,
        const std::uint64_t localActorId,
        QuestMutationReplication& mutations,
        QuestStateAuthorityService& questState,
        authority::AuthorityReplication& authority,
        replication::LocalHeroReplication& localHero,
        replication::RemotePlayerChannels& remotePlayers,
        const core::Diagnostics& diagnostics) noexcept
    {
        Shutdown();
        role_ = role;
        localActorId_ = localActorId;
        mutations_ = &mutations;
        questState_ = &questState;
        authority_ = &authority;
        localHero_ = &localHero;
        remotePlayers_ = &remotePlayers;
        diagnostics_ = diagnostics;
        mutations_->SetValidator(&ValidateProposal, this);
        mutations_->SetApplySink(&ApplyCommit, this);
        mutations_->SetCommitSink(&ObserveCommit, this);
        mutations_->SetRepairSink(&ObserveRepair, this);
        initialized_ = true;
    }

    bool QuestLifecycleReplicationAdapter::Attach(
        game::quest::QuestLifecycleHook& hook) noexcept
    {
        if (!initialized_ || !hook.IsInstalled())
        {
            return false;
        }
        if (hook_ != nullptr && hook_ != &hook)
        {
            hook_->SetEventSink(nullptr, nullptr);
        }
        hook_ = &hook;
        hook_->SetEventSink(&OnLifecycleEvent, this);
        return true;
    }

    bool QuestLifecycleReplicationAdapter::Process()
    {
        if (!initialized_ || mutations_ == nullptr ||
            !SynchronizeCampaignBaseline())
        {
            return false;
        }

        if (role_ == PeerRole::Host && repairPending_)
        {
            if (questState_ == nullptr || !questState_->CaptureHostCurrent())
            {
                // The guest has already fenced its semantic lane. Keep
                // retrying the authoritative snapshot instead of losing a
                // one-shot repair request to transient native unavailability.
                return mutations_->Process();
            }
            repairPending_ = false;
        }

        if (role_ == PeerRole::Guest && questState_ != nullptr &&
            questState_->HasStagedSnapshot() &&
            !questState_->StagedSnapshotApplied() &&
            questState_->StagedSnapshotRevision() != boundSnapshotRevision_)
        {
            // A repair/reconnect snapshot supersedes optimistic local work.
            // Keep receiving bounded commits, but apply neither mutations nor
            // native events until the replacement baseline is authoritative.
            liveEventsEnabled_ = false;
            return true;
        }

        if (repairPending_)
        {
            // The mutation service cleared the rejected optimistic proposals
            // when it delivered RepairNeeded. Keep its reliable lane alive
            // for the replacement snapshot's later commit traffic, but never
            // turn newly observed native events into proposals meanwhile.
            return mutations_->Process();
        }

        const std::uint32_t dropped = droppedEvents_.exchange(
            0, std::memory_order_acq_rel);
        if (dropped != 0 && !RecoverFromEventOverflow(dropped))
        {
            droppedEvents_.fetch_add(dropped, std::memory_order_release);
            return true;
        }
        if (liveEventsEnabled_)
        {
            for (std::size_t processed = 0; processed < EventBudget;
                 ++processed)
            {
                game::quest::QuestLifecycleEvent event;
                if (!PeekEvent(event))
                {
                    break;
                }
                if (!PublishEvent(event))
                {
                    // Map identity/authority may still be converging during
                    // travel. Preserve event order and retry next frame.
                    break;
                }
                PopEvent();
            }
        }
        return mutations_->Process();
    }

    void QuestLifecycleReplicationAdapter::Shutdown() noexcept
    {
        if (hook_ != nullptr)
        {
            hook_->SetEventSink(nullptr, nullptr);
        }
        if (mutations_ != nullptr)
        {
            mutations_->SetValidator(nullptr, nullptr);
            mutations_->SetApplySink(nullptr, nullptr);
            mutations_->SetCommitSink(nullptr, nullptr);
            mutations_->SetRepairSink(nullptr, nullptr);
        }
        ClearEvents();
        mutations_ = nullptr;
        questState_ = nullptr;
        authority_ = nullptr;
        localHero_ = nullptr;
        remotePlayers_ = nullptr;
        hook_ = nullptr;
        diagnostics_ = {};
        role_ = PeerRole::Guest;
        localActorId_ = 0;
        boundAuthorityEpoch_ = 0;
        boundSessionRevision_ = 0;
        boundSnapshotRevision_ = 0;
        repairAuthorityEpoch_ = 0;
        repairSessionRevision_ = 0;
        repairCampaignRevision_ = 0;
        droppedEvents_.store(0, std::memory_order_release);
        repairPending_ = false;
        liveEventsEnabled_ = false;
        initialized_ = false;
    }

    void QuestLifecycleReplicationAdapter::OnLifecycleEvent(
        void* const context,
        const game::quest::QuestLifecycleEvent& event) noexcept
    {
        auto* const adapter = static_cast<QuestLifecycleReplicationAdapter*>(
            context);
        if (adapter != nullptr)
        {
            adapter->QueueEvent(event);
        }
    }

    bool QuestLifecycleReplicationAdapter::ValidateProposal(
        void* const context,
        const protocol::QuestMutationMessage& proposal) noexcept
    {
        auto* const adapter = static_cast<QuestLifecycleReplicationAdapter*>(
            context);
        if (adapter == nullptr || adapter->authority_ == nullptr)
        {
            return false;
        }
        const PlayerState* const player = adapter->FindPlayer(
            proposal.initiatorActorId);
        const authority::MapAuthorityLease* const lease =
            adapter->authority_->FindMapLease(proposal.mapId);
        const game::quest::policy::QuestPolicy& policy =
            game::quest::policy::QuestPolicyRegistry::Resolve(
                proposal.questName.c_str());
        const bool lifecycleAllowed =
            policy.lifecycle == game::quest::policy::Lifecycle::Shared ||
            (policy.lifecycle == game::quest::policy::Lifecycle::HostOnly &&
                proposal.initiatorActorId == adapter->localActorId_);
        // Any player currently in the map may advance a shared quest. The map
        // publisher restriction belongs to consequential script commands
        // such as spawning, not to the quest state-machine edge itself.
        return lifecycleAllowed && player != nullptr &&
            player->mapId == proposal.mapId &&
            lease != nullptr && lease->epoch == proposal.mapEpoch;
    }

    bool QuestLifecycleReplicationAdapter::ApplyCommit(
        void* const context,
        const protocol::QuestMutationMessage& commit) noexcept
    {
        auto* const adapter = static_cast<QuestLifecycleReplicationAdapter*>(
            context);
        if (adapter == nullptr || adapter->hook_ == nullptr)
        {
            return false;
        }
        const std::uint32_t arguments[3] = {
            commit.arguments[0], commit.arguments[1], commit.arguments[2]};
        bool changed = false;
        return adapter->hook_->ApplyAuthoritative(
            ToNative(commit.operation), commit.questName.c_str(), arguments,
            changed);
    }

    void QuestLifecycleReplicationAdapter::ObserveCommit(
        void* const context,
        const protocol::QuestMutationMessage& commit,
        const bool) noexcept
    {
        auto* const adapter = static_cast<QuestLifecycleReplicationAdapter*>(
            context);
        if (adapter != nullptr && adapter->role_ == PeerRole::Host &&
            adapter->questState_ != nullptr &&
            !adapter->questState_->UpdateHostCampaignRevision(
                commit.campaignRevision))
        {
            adapter->Report("MultiplayerQuestCampaignFenceRejected",
                "host quest snapshot rejected a regressing campaign revision");
        }
    }

    void QuestLifecycleReplicationAdapter::ObserveRepair(
        void* const context,
        const std::uint64_t hostCampaignRevision,
        const protocol::QuestMutationRejectionReason reason) noexcept
    {
        auto* const adapter = static_cast<QuestLifecycleReplicationAdapter*>(
            context);
        if (adapter == nullptr)
        {
            return;
        }
        if (adapter->role_ == PeerRole::Host)
        {
            if (adapter->questState_ == nullptr ||
                !adapter->questState_->CaptureHostCurrent())
            {
                adapter->repairPending_ = true;
                adapter->Report("MultiplayerQuestRepairDeferred",
                    "host could not capture an authoritative quest repair snapshot; capture will retry");
            }
            else
            {
                adapter->repairPending_ = false;
            }
            return;
        }
        adapter->repairPending_ = true;
        adapter->liveEventsEnabled_ = false;
        adapter->repairCampaignRevision_ = (std::max)(
            adapter->repairCampaignRevision_, hostCampaignRevision);
        if (adapter->questState_ != nullptr)
        {
            adapter->repairAuthorityEpoch_ =
                adapter->questState_->AcceptedAuthorityEpoch();
            adapter->repairSessionRevision_ =
                adapter->questState_->AcceptedSessionRevision();
        }
        if (adapter->repairAuthorityEpoch_ == 0)
        {
            adapter->repairAuthorityEpoch_ = adapter->boundAuthorityEpoch_;
        }
        if (adapter->repairSessionRevision_ == 0)
        {
            adapter->repairSessionRevision_ = adapter->boundSessionRevision_;
        }
        adapter->ClearEvents();
        adapter->Report("MultiplayerQuestRepairPending",
            reason == protocol::QuestMutationRejectionReason::QueueOverflow
                ? "guest paused quest proposals after queue overflow and awaits the replacement host snapshot"
                : "guest paused quest proposals after rejection and awaits the replacement host snapshot");
    }

    void QuestLifecycleReplicationAdapter::QueueEvent(
        const game::quest::QuestLifecycleEvent& event) noexcept
    {
        AcquireSRWLockExclusive(&eventLock_);
        if (eventCount_ == events_.size())
        {
            if (!overflowEventPresent_)
            {
                overflowEvent_ = event;
                overflowEventPresent_ = true;
            }
            droppedEvents_.fetch_add(1, std::memory_order_relaxed);
            ReleaseSRWLockExclusive(&eventLock_);
            return;
        }
        const std::size_t write = (eventRead_ + eventCount_) % events_.size();
        events_[write] = event;
        ++eventCount_;
        ReleaseSRWLockExclusive(&eventLock_);
    }

    bool QuestLifecycleReplicationAdapter::PeekEvent(
        game::quest::QuestLifecycleEvent& event) noexcept
    {
        AcquireSRWLockShared(&eventLock_);
        const bool present = eventCount_ != 0;
        if (present)
        {
            event = events_[eventRead_];
        }
        ReleaseSRWLockShared(&eventLock_);
        return present;
    }

    void QuestLifecycleReplicationAdapter::PopEvent() noexcept
    {
        AcquireSRWLockExclusive(&eventLock_);
        if (eventCount_ != 0)
        {
            events_[eventRead_] = {};
            eventRead_ = (eventRead_ + 1) % events_.size();
            --eventCount_;
        }
        ReleaseSRWLockExclusive(&eventLock_);
    }

    void QuestLifecycleReplicationAdapter::ClearEvents() noexcept
    {
        AcquireSRWLockExclusive(&eventLock_);
        for (auto& event : events_)
        {
            event = {};
        }
        eventRead_ = 0;
        eventCount_ = 0;
        overflowEvent_ = {};
        overflowEventPresent_ = false;
        ReleaseSRWLockExclusive(&eventLock_);
    }

    bool QuestLifecycleReplicationAdapter::RecoverFromEventOverflow(
        const std::uint32_t droppedEvents)
    {
        game::quest::QuestLifecycleEvent event;
        bool hasEvent = false;
        AcquireSRWLockShared(&eventLock_);
        if (overflowEventPresent_)
        {
            event = overflowEvent_;
            hasEvent = true;
        }
        ReleaseSRWLockShared(&eventLock_);

        if (!liveEventsEnabled_ || !hasEvent)
        {
            // Load-time mutations are already represented by the snapshot
            // that enables live delivery, so they are not replayed.
            ClearEvents();
            return true;
        }
        if (questState_ == nullptr || localHero_ == nullptr ||
            authority_ == nullptr || mutations_ == nullptr)
        {
            return false;
        }
        const PlayerState* const local = localHero_->CurrentState();
        const authority::MapAuthorityLease* const lease = local != nullptr
            ? authority_->FindMapLease(local->mapId)
            : nullptr;
        if (local == nullptr || local->mapId == 0 || lease == nullptr ||
            lease->epoch == 0)
        {
            return false;
        }

        bool requested = false;
        if (role_ == PeerRole::Host)
        {
            requested = questState_->CaptureHostCurrent();
        }
        else
        {
            requested = mutations_->RequestRepair(
                ToProtocol(event.operation), local->mapId, lease->epoch,
                event.Name(),
                protocol::QuestMutationRejectionReason::QueueOverflow);
            if (requested)
            {
                repairPending_ = true;
                liveEventsEnabled_ = false;
            }
        }
        if (!requested)
        {
            return false;
        }

        char detail[160] = {};
        std::snprintf(detail, sizeof(detail),
            "dropped=%u; live quest delivery paused for authoritative snapshot repair",
            droppedEvents);
        Report("MultiplayerQuestMutationRepairRequested", detail);
        ClearEvents();
        return true;
    }

    bool QuestLifecycleReplicationAdapter::SynchronizeCampaignBaseline()
        noexcept
    {
        if (questState_ == nullptr || mutations_ == nullptr)
        {
            return false;
        }
        if (role_ == PeerRole::Host)
        {
            if (!questState_->HasCurrentSnapshot())
            {
                return true;
            }
            const std::uint32_t authorityEpoch =
                questState_->CurrentAuthorityEpoch();
            const std::uint64_t sessionRevision =
                questState_->CurrentSessionRevision();
            if (liveEventsEnabled_ &&
                boundAuthorityEpoch_ == authorityEpoch &&
                boundSessionRevision_ == sessionRevision)
            {
                return true;
            }
            if (!mutations_->BindHostCampaign(
                    authorityEpoch, sessionRevision,
                    questState_->CurrentCampaignRevision()))
            {
                return false;
            }
            boundAuthorityEpoch_ = authorityEpoch;
            boundSessionRevision_ = sessionRevision;
            boundSnapshotRevision_ = questState_->CurrentSnapshotRevision();
            ClearEvents();
            liveEventsEnabled_ = true;
            return true;
        }

        if (!questState_->StagedSnapshotApplied())
        {
            return true;
        }
        const std::uint64_t snapshotRevision =
            questState_->StagedSnapshotRevision();
        if (repairPending_ &&
            (snapshotRevision == boundSnapshotRevision_ ||
                questState_->StagedAuthorityEpoch() !=
                    repairAuthorityEpoch_ ||
                questState_->StagedSessionRevision() !=
                    repairSessionRevision_ ||
                questState_->StagedCampaignRevision() <
                    repairCampaignRevision_))
        {
            // A previously staged baseline (or a stale snapshot) cannot
            // release a repair fence. Wait for the matching host campaign
            // revision to be applied before rebinding mutation delivery.
            liveEventsEnabled_ = false;
            return true;
        }
        if (liveEventsEnabled_ &&
            boundSnapshotRevision_ == snapshotRevision)
        {
            return true;
        }
        if (!mutations_->BindGuestAppliedCampaign(
                questState_->AcceptedAuthorityEpoch(),
                questState_->AcceptedSessionRevision(),
                questState_->CurrentCampaignRevision(),
                questState_->CurrentHostActorId()))
        {
            return false;
        }
        boundAuthorityEpoch_ = questState_->AcceptedAuthorityEpoch();
        boundSessionRevision_ = questState_->AcceptedSessionRevision();
        boundSnapshotRevision_ = snapshotRevision;
        ClearEvents();
        repairPending_ = false;
        repairAuthorityEpoch_ = 0;
        repairSessionRevision_ = 0;
        repairCampaignRevision_ = 0;
        liveEventsEnabled_ = true;
        return true;
    }

    bool QuestLifecycleReplicationAdapter::PublishEvent(
        const game::quest::QuestLifecycleEvent& event)
    {
        if (mutations_ == nullptr || localHero_ == nullptr ||
            authority_ == nullptr)
        {
            return false;
        }
        const PlayerState* const local = localHero_->CurrentState();
        if (local == nullptr || local->actorId != localActorId_ ||
            local->mapId == 0)
        {
            return false;
        }
        const authority::MapAuthorityLease* const lease =
            authority_->FindMapLease(local->mapId);
        if (lease == nullptr || lease->epoch == 0)
        {
            return false;
        }
        const game::quest::policy::QuestPolicy& policy =
            game::quest::policy::QuestPolicyRegistry::Resolve(event.Name());
        char policyDetail[320] = {};
        std::snprintf(policyDetail, sizeof(policyDetail),
            "quest=%s operation=%u lifecycle=%u world=%u decision=%u transition=%u reward=%u confidence=%u",
            event.Name(),
            static_cast<unsigned int>(event.operation),
            static_cast<unsigned int>(policy.lifecycle),
            static_cast<unsigned int>(policy.world),
            static_cast<unsigned int>(policy.decision),
            static_cast<unsigned int>(policy.transition),
            static_cast<unsigned int>(policy.reward),
            static_cast<unsigned int>(policy.confidence));
        if (policy.lifecycle == game::quest::policy::Lifecycle::LocalOnly ||
            (policy.lifecycle == game::quest::policy::Lifecycle::HostOnly &&
                role_ != PeerRole::Host))
        {
            Report("MultiplayerQuestPolicyStayedLocal", policyDetail);
            return true;
        }
        if (policy.lifecycle == game::quest::policy::Lifecycle::Blocked)
        {
            Report("MultiplayerQuestPolicyBlocked",
                policyDetail);
            return true;
        }
        Report("MultiplayerQuestPolicyShared", policyDetail);
        return mutations_->ObserveLocalMutation(
            ToProtocol(event.operation), local->mapId, lease->epoch,
            event.Name(), event.arguments);
    }

    const PlayerState* QuestLifecycleReplicationAdapter::FindPlayer(
        const std::uint64_t actorId) const noexcept
    {
        if (actorId == 0)
        {
            return nullptr;
        }
        if (actorId == localActorId_)
        {
            return localHero_ != nullptr ? localHero_->CurrentState() : nullptr;
        }
        return remotePlayers_ != nullptr ? remotePlayers_->Find(actorId)
                                         : nullptr;
    }

    protocol::QuestMutationOperation
        QuestLifecycleReplicationAdapter::ToProtocol(
            const game::quest::QuestLifecycleOperation operation) noexcept
    {
        using Native = game::quest::QuestLifecycleOperation;
        using Protocol = protocol::QuestMutationOperation;
        switch (operation)
        {
        case Native::Activate: return Protocol::Activate;
        case Native::Deactivate: return Protocol::Deactivate;
        case Native::Complete: return Protocol::Complete;
        case Native::Fail: return Protocol::Fail;
        default: return Protocol::Activate;
        }
    }

    game::quest::QuestLifecycleOperation
        QuestLifecycleReplicationAdapter::ToNative(
            const protocol::QuestMutationOperation operation) noexcept
    {
        using Native = game::quest::QuestLifecycleOperation;
        using Protocol = protocol::QuestMutationOperation;
        switch (operation)
        {
        case Protocol::Activate: return Native::Activate;
        case Protocol::Deactivate: return Native::Deactivate;
        case Protocol::Complete: return Native::Complete;
        case Protocol::Fail: return Native::Fail;
        default: return Native::Activate;
        }
    }

    void QuestLifecycleReplicationAdapter::Report(
        const char* const event,
        const char* const detail) const noexcept
    {
        diagnostics_.Event(event != nullptr ? event : "MultiplayerQuest",
            detail != nullptr ? detail : "unknown");
    }
}
