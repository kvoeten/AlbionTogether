#include "CreatureActionLifecycleObserver.h"
#include "Game/Creature/Actions/Native/CreatureActionInspector.h"
#include "Game/Creature/Actions/CreatureActionSubmissionContext.h"

#include <cstdio>
#include <iterator>

namespace fable::game::creature::actions
{
    bool CreatureActionLifecycleObserver::BeginSubmissionReceipt(
        void* creature) noexcept
    {
        if (active_ == nullptr || active_->originalSubmit_ == nullptr ||
            creature == nullptr)
        {
            return false;
        }
        return submissionReceipts_.Begin(creature, nativeSubmitDepth_);
    }

    bool CreatureActionLifecycleObserver::EndSubmissionReceipt(
        void* creature,
        bool& accepted) noexcept
    {
        return submissionReceipts_.End(creature, accepted);
    }

    bool CreatureActionLifecycleObserver::
        RetireLocalActionForReplicatedReplay(void* creature) noexcept
    {
        CreatureActionLifecycleObserver* const observer = active_;
        if (observer == nullptr || observer->originalFinish_ == nullptr ||
            creature == nullptr)
        {
            return false;
        }

        void* const activeAction =
            native::CreatureActionInspector::ActiveAction(creature);
        const bool alreadyFinished =
            native::CreatureActionInspector::IsFinished(activeAction);
        if (activeAction == nullptr || alreadyFinished)
        {
            return true;
        }
        // A prior replicated action is allowed to finish or arbitrate the new
        // action through the retail priority rules. Only frozen local work is
        // stale when this process no longer owns the creature.
        if (observer->IsReplicatedReplay(activeAction))
        {
            return false;
        }

        char actionType[ActionNameCapacity] = {};
        ReadActionType(activeAction, actionType);
        if (ResolveActionOwner(activeAction) != creature)
        {
            return false;
        }

        bool retired = false;
        __try
        {
            observer->originalFinish_(activeAction);
            retired = native::CreatureActionInspector::IsFinished(
                activeAction);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            retired = false;
        }
        if (!retired)
        {
            return false;
        }

        observer->ForgetReplicatedReplay(activeAction);
        observer->ReportFinish(activeAction, creature, actionType);
        char detail[384] = {};
        const ThingContext context = ReadThingContext(creature);
        std::snprintf(
            detail,
            std::size(detail),
            "thing_uid=%016llX creature=%p action=%p action_type=%s reason=authoritative-replacement",
            static_cast<unsigned long long>(context.uid),
            creature,
            activeAction,
            actionType[0] != '\0' ? actionType : "<unknown>");
        observer->diagnostics_.Event(
            "CreatureActionAuthorityRetired", detail);
        return true;
    }

    bool CreatureActionLifecycleObserver::RetrySubmission(
        void* creature,
        void* action,
        bool replicatedReplay) noexcept
    {
        if (active_ != this || originalSubmit_ == nullptr ||
            creature == nullptr || action == nullptr)
        {
            return false;
        }
        const ScopedActionSubmissionOrigin origin(
            replicatedReplay
                ? ActionSubmissionOrigin::ReplicatedPresentation
                : ActionSubmissionOrigin::Retail);
        (void)origin;
        const bool accepted = ObserveSubmission(creature, nullptr, action);
        return accepted;
    }

    bool CreatureActionLifecycleObserver::IsReplicatedReplayAction(
        void* action) const noexcept
    {
        return IsReplicatedReplay(action);
    }

    bool CreatureActionLifecycleObserver::
        IsActiveActionReplicatedReplay(void* creature) noexcept
    {
        CreatureActionLifecycleObserver* const observer = active_;
        if (observer == nullptr || creature == nullptr)
        {
            return false;
        }
        void* const action =
            native::CreatureActionInspector::ActiveAction(creature);
        return action != nullptr && observer->IsReplicatedReplay(action);
    }

    bool CreatureActionLifecycleObserver::IsReplicatedReplay(
        void* action) const noexcept
    {
        if (action == nullptr)
        {
            return false;
        }
        void* const creature = ResolveActionOwner(action);
        void* const activeAction =
            native::CreatureActionInspector::ActiveAction(creature);
        if (activeAction != action)
        {
            return false;
        }
        const std::uint64_t creatureUid = ReadThingContext(creature).uid;
        AcquireSRWLockShared(&replicatedReplayLock_);
        bool found = false;
        for (const ReplicatedReplayEntry& entry : replicatedReplayActions_)
        {
            if (entry.action == action && entry.creature == creature &&
                (entry.creatureUid == 0 || entry.creatureUid == creatureUid))
            {
                found = true;
                break;
            }
        }
        ReleaseSRWLockShared(&replicatedReplayLock_);
        return found;
    }

    void CreatureActionLifecycleObserver::RememberReplicatedReplay(
        void* action) noexcept
    {
        if (action == nullptr)
        {
            return;
        }
        void* creature = ResolveActionOwner(action);
        if (creature == nullptr)
        {
            return;
        }
        const ThingContext context = ReadThingContext(creature);
        PruneReplicatedReplay(creature);
        AcquireSRWLockExclusive(&replicatedReplayLock_);
        for (ReplicatedReplayEntry& entry : replicatedReplayActions_)
        {
            if (entry.action == action)
            {
                entry.creature = creature;
                entry.creatureUid = context.uid;
                ReleaseSRWLockExclusive(&replicatedReplayLock_);
                return;
            }
        }
        for (ReplicatedReplayEntry& entry : replicatedReplayActions_)
        {
            if (entry.action == nullptr)
            {
                entry = {action, creature, context.uid};
                ReleaseSRWLockExclusive(&replicatedReplayLock_);
                return;
            }
        }
        // A bounded table must never grow with action churn. If every slot is
        // still live, leave the marker absent rather than evicting a live
        // action's authority. The submission remains authoritative for its
        // native call; the next lifecycle finish/replacement frees capacity.
        ReleaseSRWLockExclusive(&replicatedReplayLock_);
    }

    void CreatureActionLifecycleObserver::ForgetReplicatedReplay(
        void* action) noexcept
    {
        if (action == nullptr)
        {
            return;
        }
        AcquireSRWLockExclusive(&replicatedReplayLock_);
        for (ReplicatedReplayEntry& entry : replicatedReplayActions_)
        {
            if (entry.action == action)
            {
                entry = {};
            }
        }
        ReleaseSRWLockExclusive(&replicatedReplayLock_);
    }

    void CreatureActionLifecycleObserver::PruneReplicatedReplay(
        void* creature) noexcept
    {
        AcquireSRWLockExclusive(&replicatedReplayLock_);
        for (ReplicatedReplayEntry& entry : replicatedReplayActions_)
        {
            if (entry.action == nullptr ||
                (creature != nullptr && entry.creature != creature))
            {
                continue;
            }
            void* const activeAction =
                native::CreatureActionInspector::ActiveAction(entry.creature);
            bool stale = activeAction != entry.action ||
                native::CreatureActionInspector::IsFinished(entry.action);
            if (entry.creatureUid != 0 &&
                ReadThingContext(entry.creature).uid != entry.creatureUid)
            {
                stale = true;
            }
            if (stale)
            {
                entry = {};
            }
        }
        ReleaseSRWLockExclusive(&replicatedReplayLock_);
    }
}
