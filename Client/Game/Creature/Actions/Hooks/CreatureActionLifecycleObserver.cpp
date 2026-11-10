#include "CreatureActionLifecycleObserver.h"
#include "Game/Creature/Actions/Native/CreatureActionInspector.h"
#include "Game/Creature/Actions/CreatureActionSubmissionContext.h"

#include <array>
#include <cstdio>
#include <iterator>

namespace fable::game::creature::actions
{
    CreatureActionLifecycleObserver* CreatureActionLifecycleObserver::active_ = nullptr;
    thread_local CreatureActionSubmissionReceiptStack
        CreatureActionLifecycleObserver::submissionReceipts_ = {};
    thread_local unsigned int
        CreatureActionLifecycleObserver::nativeSubmitDepth_ = 0;

    bool CreatureActionLifecycleObserver::Install(
        HMODULE gameModule,
        const core::Diagnostics& diagnostics)
    {
        if (IsInstalled())
        {
            return true;
        }
        diagnostics_ = diagnostics;
        gameModule_ = gameModule;

#if !defined(_M_IX86)
        diagnostics_.Log(
            "Hook: creature action lifecycle observation is only supported by the x86 client.");
        return false;
#else
        if (active_ != nullptr && active_ != this)
        {
            diagnostics_.Log(
                "Hook: another creature action lifecycle observer is already active.");
            return false;
        }

        std::uint8_t* updateTarget = nullptr;
        std::uint8_t* submitTarget = nullptr;
        std::uint8_t* finishTarget = nullptr;
        if (!native::CreatureActionFunctions::ResolveUpdate(
                gameModule,
                updateTarget) ||
            !native::CreatureActionFunctions::ResolveSubmit(
                gameModule,
                submitTarget) ||
            !native::CreatureActionFunctions::ResolveFinish(
                gameModule,
                finishTarget))
        {
            diagnostics_.Log(
                "Hook: creature action lifecycle definitions failed validation.");
            return false;
        }

        active_ = this;
        if (!InstallDetour(
                updateTarget,
                reinterpret_cast<void*>(&CreatureActionLifecycleObserver::ObserveUpdate),
                native::CreatureActionFunctions::DisplacedBytes,
                updateDetour_))
        {
            active_ = nullptr;
            diagnostics_.Log("Hook: creature action update detour installation failed.");
            return false;
        }
        originalUpdate_ = reinterpret_cast<
            native::CreatureActionFunctions::UpdatePointer>(
                updateDetour_.patch.Original());

        if (!InstallDetour(
                submitTarget,
                reinterpret_cast<void*>(&CreatureActionLifecycleObserver::ObserveSubmission),
                native::CreatureActionFunctions::DisplacedBytes,
                submitDetour_))
        {
            const bool updateRemoved = RestoreDetour(updateDetour_);
            if (!updateRemoved)
            {
                diagnostics_.Log(
                    "Hook: creature action submission install failed and the update detour could not be rolled back; callback state retained.");
                return false;
            }
            if (updateDetour_.patch.ProtectionRestoreFailed())
            {
                diagnostics_.Log(
                    "Hook: creature action update rollback restored bytes, but code protection restoration failed.");
            }
            updateDetour_.target = nullptr;
            updateDetour_.displacedBytes = 0;
            originalUpdate_ = nullptr;
            active_ = nullptr;
            diagnostics_.Log("Hook: creature action submission detour installation failed.");
            return false;
        }
        originalSubmit_ = reinterpret_cast<native::CreatureActionFunctions::SubmitPointer>(
            submitDetour_.patch.Original());

        if (!InstallDetour(
                finishTarget,
                reinterpret_cast<void*>(&CreatureActionLifecycleObserver::ObserveFinish),
                native::CreatureActionFunctions::DisplacedBytes,
                finishDetour_))
        {
            const bool submitRemoved = RestoreDetour(submitDetour_);
            const bool updateRemoved = RestoreDetour(updateDetour_);
            if (!submitRemoved || !updateRemoved)
            {
                diagnostics_.Log(
                    "Hook: creature action finish install failed and existing detours could not all be rolled back; callback state retained.");
                return false;
            }
            if (submitDetour_.patch.ProtectionRestoreFailed() ||
                updateDetour_.patch.ProtectionRestoreFailed())
            {
                diagnostics_.Log(
                    "Hook: creature action install rollback restored bytes, but code protection restoration failed.");
            }
            submitDetour_.target = nullptr;
            submitDetour_.displacedBytes = 0;
            updateDetour_.target = nullptr;
            updateDetour_.displacedBytes = 0;
            originalSubmit_ = nullptr;
            originalUpdate_ = nullptr;
            active_ = nullptr;
            diagnostics_.Log("Hook: creature action finish detour installation failed.");
            return false;
        }
        originalFinish_ = reinterpret_cast<native::CreatureActionFunctions::FinishPointer>(
            finishDetour_.patch.Original());

        char detail[384] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "update=%p update_trampoline=%p submit=%p submit_trampoline=%p finish=%p finish_trampoline=%p event_limit=%u",
            updateDetour_.target,
            updateDetour_.patch.Original(),
            submitDetour_.target,
            submitDetour_.patch.Original(),
            finishDetour_.target,
            finishDetour_.patch.Original(),
            DiagnosticEventLimit);
        diagnostics_.Log(
            "Hook: native creature action update/begin/end boundaries installed.");
        diagnostics_.Event("CreatureActionLifecycleObserverReady", detail);
        return true;
#endif
    }

    void CreatureActionLifecycleObserver::Shutdown() noexcept
    {
        const bool finishRemoved = RestoreDetour(finishDetour_);
        const bool submitRemoved = RestoreDetour(submitDetour_);
        const bool updateRemoved = RestoreDetour(updateDetour_);
        if (!finishRemoved || !submitRemoved || !updateRemoved)
        {
            diagnostics_.Log(
                "Hook: creature action lifecycle shutdown skipped because a target changed.");
            return;
        }
        if (finishDetour_.patch.ProtectionRestoreFailed() ||
            submitDetour_.patch.ProtectionRestoreFailed() ||
            updateDetour_.patch.ProtectionRestoreFailed())
        {
            diagnostics_.Log(
                "Hook: creature action lifecycle bytes restored, but code protection restoration failed.");
        }
        finishDetour_.target = nullptr;
        finishDetour_.displacedBytes = 0;
        submitDetour_.target = nullptr;
        submitDetour_.displacedBytes = 0;
        updateDetour_.target = nullptr;
        updateDetour_.displacedBytes = 0;
        SetAuthorityGate(nullptr, nullptr);
        AcquireSRWLockExclusive(&eventSinkLock_);
        for (auto& subscription : eventSinks_) subscription = {};
        ReleaseSRWLockExclusive(&eventSinkLock_);
        AcquireSRWLockExclusive(&postUpdateSinkLock_);
        for (auto& subscription : postUpdateSinks_) subscription = {};
        ReleaseSRWLockExclusive(&postUpdateSinkLock_);
        if (active_ == this) active_ = nullptr;
        originalFinish_ = nullptr;
        originalSubmit_ = nullptr;
        originalUpdate_ = nullptr;
        gameModule_ = nullptr;
        AcquireSRWLockExclusive(&replicatedReplayLock_);
        replicatedReplayActions_ = {};
        ReleaseSRWLockExclusive(&replicatedReplayLock_);
        diagnostics_ = {};
    }

    bool CreatureActionLifecycleObserver::IsInstalled() const noexcept
    {
        return active_ == this &&
            originalSubmit_ != nullptr &&
            originalFinish_ != nullptr &&
            originalUpdate_ != nullptr &&
            updateDetour_.patch.IsInstalled() &&
            submitDetour_.patch.IsInstalled() &&
            finishDetour_.patch.IsInstalled();
    }

    bool CreatureActionLifecycleObserver::InstallDetour(
        std::uint8_t* target,
        void* replacement,
        std::size_t displacedBytes,
        Detour& detour) noexcept
    {
        if (target == nullptr || replacement == nullptr ||
            detour.patch.IsInstalled())
        {
            return false;
        }
        if (!detour.patch.Install(
                target,
                native::CreatureActionFunctions::ExpectedPrefix.data(),
                native::CreatureActionFunctions::ExpectedPrefix.size(),
                replacement,
                displacedBytes))
        {
            return false;
        }
        detour.target = target;
        detour.displacedBytes = displacedBytes;
        return true;
    }

    bool CreatureActionLifecycleObserver::RestoreDetour(Detour& detour) noexcept
    {
        if (!detour.patch.IsInstalled())
        {
            return true;
        }
        if (!detour.patch.Shutdown())
        {
            return false;
        }
        return true;
    }

    void __fastcall CreatureActionLifecycleObserver::ObserveUpdate(
        void* creature,
        void*)
    {
        CreatureActionLifecycleObserver* const observer = active_;
        if (observer == nullptr || observer->originalUpdate_ == nullptr)
        {
            return;
        }

        void* const activeAction =
            native::CreatureActionInspector::ActiveAction(creature);
        const AuthorityGate gate = observer->authorityGate_.load(
            std::memory_order_acquire);
        if (!observer->IsReplicatedReplay(activeAction) && gate != nullptr)
        {
            char activeActionType[ActionNameCapacity] = {};
            ReadActionType(activeAction, activeActionType);
            if (!gate(
                    observer->authorityGateContext_.load(
                        std::memory_order_acquire),
                    creature,
                    activeActionType))
            {
                return;
            }
        }
        observer->originalUpdate_(creature);
        observer->NotifyPostUpdate(creature);
    }

    bool __fastcall CreatureActionLifecycleObserver::ObserveSubmission(
        void* creature,
        void*,
        void* action)
    {
        CreatureActionLifecycleObserver* const observer = active_;
        if (observer == nullptr || observer->originalSubmit_ == nullptr)
        {
            return false;
        }

        char requestedType[ActionNameCapacity] = {};
        ReadActionType(action, requestedType);
        const AuthorityGate gate = observer->authorityGate_.load(
            std::memory_order_acquire);
        const bool replaySubmission =
            ScopedActionSubmissionOrigin::Current() ==
                ActionSubmissionOrigin::ReplicatedPresentation;
        // A requested action can reuse an address retained from an earlier
        // replay if native storage is recycled. Remove that address before
        // native arbitration so pointer reuse cannot inherit authority.
        observer->ForgetReplicatedReplay(action);
        void* const previousAction =
            native::CreatureActionInspector::ActiveAction(creature);
        const bool authorityDenied = !replaySubmission &&
            gate != nullptr && !gate(
            observer->authorityGateContext_.load(
                std::memory_order_acquire),
            creature,
            requestedType);
        const unsigned int submitDepth = ++nativeSubmitDepth_;
        bool accepted = false;
        __try
        {
            accepted = !authorityDenied &&
                observer->originalSubmit_(creature, action);
        }
        __finally
        {
            --nativeSubmitDepth_;
        }
        submissionReceipts_.Record(creature, submitDepth, accepted);
        if (accepted && replaySubmission)
        {
            void* const activeAction =
                native::CreatureActionInspector::ActiveAction(creature);
            if (previousAction != nullptr && previousAction != activeAction)
            {
                observer->ForgetReplicatedReplay(previousAction);
            }
            observer->RememberReplicatedReplay(activeAction);
        }
        else if (previousAction != nullptr && previousAction != action)
        {
            // A native replacement that was denied or rejected must not leave
            // the old replay marker live indefinitely.
            observer->PruneReplicatedReplay(creature);
        }
        observer->ReportSubmission(
            creature,
            action,
            requestedType,
            accepted,
            authorityDenied);
        return accepted;
    }

    void __fastcall CreatureActionLifecycleObserver::ObserveFinish(
        void* action,
        void*)
    {
        CreatureActionLifecycleObserver* const observer = active_;
        if (observer == nullptr || observer->originalFinish_ == nullptr)
        {
            return;
        }

        char actionType[ActionNameCapacity] = {};
        ReadActionType(action, actionType);
        void* const creature = ResolveActionOwner(action);
        observer->originalFinish_(action);
        observer->ForgetReplicatedReplay(action);
        observer->ReportFinish(action, creature, actionType);
    }
}
