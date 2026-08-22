#include "CreatureActionLifecycleObserver.h"

#include <array>
#include <climits>
#include <cstdio>
#include <cstring>

namespace fable::game::creature::actions
{
    CreatureActionLifecycleObserver* CreatureActionLifecycleObserver::active_ = nullptr;
    thread_local unsigned int
        CreatureActionLifecycleObserver::authoritativeReplayDepth_ = 0;
    thread_local void*
        CreatureActionLifecycleObserver::submissionReceiptCreature_ = nullptr;
    thread_local unsigned int
        CreatureActionLifecycleObserver::submissionReceiptDepth_ = 0;
    thread_local bool
        CreatureActionLifecycleObserver::submissionReceiptObserved_ = false;
    thread_local bool
        CreatureActionLifecycleObserver::submissionReceiptAccepted_ = false;

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
                updateDetour_.trampoline);

        if (!InstallDetour(
                submitTarget,
                reinterpret_cast<void*>(&CreatureActionLifecycleObserver::ObserveSubmission),
                native::CreatureActionFunctions::DisplacedBytes,
                submitDetour_))
        {
            RestoreDetour(updateDetour_);
            originalUpdate_ = nullptr;
            active_ = nullptr;
            diagnostics_.Log("Hook: creature action submission detour installation failed.");
            return false;
        }
        originalSubmit_ = reinterpret_cast<native::CreatureActionFunctions::SubmitPointer>(
            submitDetour_.trampoline);

        if (!InstallDetour(
                finishTarget,
                reinterpret_cast<void*>(&CreatureActionLifecycleObserver::ObserveFinish),
                native::CreatureActionFunctions::DisplacedBytes,
                finishDetour_))
        {
            RestoreDetour(submitDetour_);
            RestoreDetour(updateDetour_);
            originalSubmit_ = nullptr;
            originalUpdate_ = nullptr;
            active_ = nullptr;
            diagnostics_.Log("Hook: creature action finish detour installation failed.");
            return false;
        }
        originalFinish_ = reinterpret_cast<native::CreatureActionFunctions::FinishPointer>(
                finishDetour_.trampoline);

        char detail[384] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "update=%p update_trampoline=%p submit=%p submit_trampoline=%p finish=%p finish_trampoline=%p event_limit=%u",
            updateDetour_.target,
            updateDetour_.trampoline,
            submitDetour_.target,
            submitDetour_.trampoline,
            finishDetour_.target,
            finishDetour_.trampoline,
            DiagnosticEventLimit);
        diagnostics_.Log(
            "Hook: native creature action update/begin/end boundaries installed.");
        diagnostics_.Event("CreatureActionLifecycleObserverReady", detail);
        return true;
#endif
    }

    bool CreatureActionLifecycleObserver::IsInstalled() const noexcept
    {
        return active_ == this &&
            originalSubmit_ != nullptr &&
            originalFinish_ != nullptr &&
            originalUpdate_ != nullptr &&
            updateDetour_.target != nullptr &&
            submitDetour_.target != nullptr &&
            finishDetour_.target != nullptr;
    }

    bool CreatureActionLifecycleObserver::AddEventSink(
        EventSink sink,
        void* context) noexcept
    {
        if (sink == nullptr)
        {
            return false;
        }
        AcquireSRWLockExclusive(&eventSinkLock_);
        for (EventSubscription& subscription : eventSinks_)
        {
            if (subscription.sink == sink && subscription.context == context)
            {
                ReleaseSRWLockExclusive(&eventSinkLock_);
                return true;
            }
        }
        for (EventSubscription& subscription : eventSinks_)
        {
            if (subscription.sink == nullptr)
            {
                subscription.context = context;
                subscription.sink = sink;
                ReleaseSRWLockExclusive(&eventSinkLock_);
                return true;
            }
        }
        ReleaseSRWLockExclusive(&eventSinkLock_);
        return false;
    }

    void CreatureActionLifecycleObserver::RemoveEventSink(
        EventSink sink,
        void* context) noexcept
    {
        AcquireSRWLockExclusive(&eventSinkLock_);
        for (EventSubscription& subscription : eventSinks_)
        {
            if (subscription.sink != sink || subscription.context != context)
            {
                continue;
            }
            subscription = {};
        }
        ReleaseSRWLockExclusive(&eventSinkLock_);
    }

    bool CreatureActionLifecycleObserver::AddPostUpdateSink(
        PostUpdateSink sink,
        void* context) noexcept
    {
        if (sink == nullptr)
        {
            return false;
        }
        AcquireSRWLockExclusive(&postUpdateSinkLock_);
        for (PostUpdateSubscription& subscription : postUpdateSinks_)
        {
            if (subscription.sink == sink && subscription.context == context)
            {
                ReleaseSRWLockExclusive(&postUpdateSinkLock_);
                return true;
            }
        }
        for (PostUpdateSubscription& subscription : postUpdateSinks_)
        {
            if (subscription.sink == nullptr)
            {
                subscription = {sink, context};
                ReleaseSRWLockExclusive(&postUpdateSinkLock_);
                return true;
            }
        }
        ReleaseSRWLockExclusive(&postUpdateSinkLock_);
        return false;
    }

    void CreatureActionLifecycleObserver::RemovePostUpdateSink(
        PostUpdateSink sink,
        void* context) noexcept
    {
        AcquireSRWLockExclusive(&postUpdateSinkLock_);
        for (PostUpdateSubscription& subscription : postUpdateSinks_)
        {
            if (subscription.sink == sink && subscription.context == context)
            {
                subscription = {};
            }
        }
        ReleaseSRWLockExclusive(&postUpdateSinkLock_);
    }

    void CreatureActionLifecycleObserver::SetEventSink(
        EventSink sink,
        void* context) noexcept
    {
        AcquireSRWLockExclusive(&eventSinkLock_);
        for (EventSubscription& subscription : eventSinks_)
        {
            subscription = {};
        }
        if (sink != nullptr)
        {
            eventSinks_[0] = {sink, context};
        }
        ReleaseSRWLockExclusive(&eventSinkLock_);
    }

    void CreatureActionLifecycleObserver::SetAuthorityGate(
        AuthorityGate gate,
        void* context) noexcept
    {
        if (gate == nullptr)
        {
            authorityGate_.store(nullptr, std::memory_order_release);
            authorityGateContext_.store(nullptr, std::memory_order_release);
            return;
        }
        authorityGateContext_.store(context, std::memory_order_release);
        authorityGate_.store(gate, std::memory_order_release);
    }

    void CreatureActionLifecycleObserver::BeginAuthoritativeReplay() noexcept
    {
        ++authoritativeReplayDepth_;
    }

    void CreatureActionLifecycleObserver::EndAuthoritativeReplay() noexcept
    {
        if (authoritativeReplayDepth_ != 0)
        {
            --authoritativeReplayDepth_;
        }
    }

    bool CreatureActionLifecycleObserver::BeginSubmissionReceipt(
        void* creature) noexcept
    {
        if (active_ == nullptr || active_->originalSubmit_ == nullptr ||
            creature == nullptr || submissionReceiptDepth_ != 0)
        {
            return false;
        }
        submissionReceiptCreature_ = creature;
        submissionReceiptDepth_ = 1;
        submissionReceiptObserved_ = false;
        submissionReceiptAccepted_ = false;
        return true;
    }

    bool CreatureActionLifecycleObserver::EndSubmissionReceipt(
        void* creature,
        bool& accepted) noexcept
    {
        accepted = false;
        if (submissionReceiptDepth_ == 0 ||
            submissionReceiptCreature_ != creature)
        {
            return false;
        }
        const bool observed = submissionReceiptObserved_;
        accepted = submissionReceiptAccepted_;
        submissionReceiptCreature_ = nullptr;
        submissionReceiptDepth_ = 0;
        submissionReceiptObserved_ = false;
        submissionReceiptAccepted_ = false;
        return observed;
    }

    bool CreatureActionLifecycleObserver::
        RetireLocalActionForAuthoritativeReplay(void* creature) noexcept
    {
        CreatureActionLifecycleObserver* const observer = active_;
        if (observer == nullptr || observer->originalFinish_ == nullptr ||
            creature == nullptr)
        {
            return false;
        }

        void* activeAction = nullptr;
        bool alreadyFinished = false;
        __try
        {
            activeAction = *reinterpret_cast<void**>(
                static_cast<std::uint8_t*>(creature) + 0x120);
            alreadyFinished = activeAction != nullptr &&
                *reinterpret_cast<const std::uint8_t*>(
                    static_cast<const std::uint8_t*>(activeAction) + 0x61) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        if (activeAction == nullptr || alreadyFinished)
        {
            return true;
        }
        // A prior replicated action is allowed to finish or arbitrate the new
        // action through the retail priority rules. Only frozen local work is
        // stale when this process no longer owns the creature.
        if (observer->IsAuthoritativeReplay(activeAction))
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
            retired = *reinterpret_cast<const std::uint8_t*>(
                static_cast<const std::uint8_t*>(activeAction) + 0x61) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            retired = false;
        }
        if (!retired)
        {
            return false;
        }

        observer->ForgetAuthoritativeReplay(activeAction);
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
        bool authoritativeReplay) noexcept
    {
        if (active_ != this || originalSubmit_ == nullptr ||
            creature == nullptr || action == nullptr)
        {
            return false;
        }
        if (authoritativeReplay)
        {
            BeginAuthoritativeReplay();
        }
        const bool accepted = ObserveSubmission(creature, nullptr, action);
        if (authoritativeReplay)
        {
            EndAuthoritativeReplay();
        }
        return accepted;
    }

    bool CreatureActionLifecycleObserver::IsAuthoritativeReplayAction(
        void* action) const noexcept
    {
        return IsAuthoritativeReplay(action);
    }

    bool CreatureActionLifecycleObserver::DescribeActionType(
        void* action,
        char* name,
        std::size_t capacity) noexcept
    {
        if (name == nullptr || capacity == 0)
        {
            return false;
        }
        name[0] = '\0';
        if (action == nullptr)
        {
            return false;
        }

        bool valid = false;
        __try
        {
            auto** const vtable = *reinterpret_cast<void***>(action);
            const auto* const locator =
                reinterpret_cast<const std::uint8_t*>(vtable[-1]);
            const auto* const typeDescriptor =
                *reinterpret_cast<const std::uint8_t* const*>(locator + 0x0C);
            const char* rawName =
                reinterpret_cast<const char*>(typeDescriptor + 0x08);
            if (rawName[0] == '.' && rawName[1] == '?' &&
                rawName[2] == 'A' &&
                (rawName[3] == 'V' || rawName[3] == 'U'))
            {
                rawName += 4;
            }

            std::size_t length = 0;
            while (length + 1 < capacity && rawName[length] != '\0')
            {
                if (rawName[length] == '@' && rawName[length + 1] == '@')
                {
                    break;
                }
                name[length] = rawName[length];
                ++length;
            }
            name[length] = '\0';
            valid = length != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            name[0] = '\0';
            valid = false;
        }
        return valid;
    }

    unsigned int CreatureActionLifecycleObserver::SubmissionCount() const noexcept
    {
        return submissionCount_.load(std::memory_order_acquire);
    }

    unsigned int CreatureActionLifecycleObserver::FinishCount() const noexcept
    {
        return finishCount_.load(std::memory_order_acquire);
    }

    bool CreatureActionLifecycleObserver::InstallDetour(
        std::uint8_t* target,
        void* replacement,
        std::size_t displacedBytes,
        Detour& detour) noexcept
    {
        if (target == nullptr || replacement == nullptr ||
            detour.target != nullptr || displacedBytes < 5 ||
            displacedBytes > detour.originalBytes.size())
        {
            return false;
        }

        auto* const trampoline = static_cast<std::uint8_t*>(VirtualAlloc(
            nullptr,
            displacedBytes + 5,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_EXECUTE_READWRITE));
        if (trampoline == nullptr)
        {
            return false;
        }

        std::memcpy(detour.originalBytes.data(), target, displacedBytes);
        std::memcpy(trampoline, target, displacedBytes);
        trampoline[displacedBytes] = 0xE9;
        const std::intptr_t trampolineDisplacement =
            reinterpret_cast<std::intptr_t>(target + displacedBytes) -
            (reinterpret_cast<std::intptr_t>(trampoline + displacedBytes) + 5);
        const std::intptr_t replacementDisplacement =
            reinterpret_cast<std::intptr_t>(replacement) -
            (reinterpret_cast<std::intptr_t>(target) + 5);
        if (trampolineDisplacement < INT32_MIN ||
            trampolineDisplacement > INT32_MAX ||
            replacementDisplacement < INT32_MIN ||
            replacementDisplacement > INT32_MAX)
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }

        const std::int32_t trampolineRelative =
            static_cast<std::int32_t>(trampolineDisplacement);
        std::memcpy(
            trampoline + displacedBytes + 1,
            &trampolineRelative,
            sizeof(trampolineRelative));

        std::array<std::uint8_t, 8> patch = {};
        patch.fill(0x90);
        patch[0] = 0xE9;
        const std::int32_t replacementRelative =
            static_cast<std::int32_t>(replacementDisplacement);
        std::memcpy(patch.data() + 1, &replacementRelative, sizeof(replacementRelative));

        DWORD previousProtection = 0;
        if (!VirtualProtect(
                target,
                displacedBytes,
                PAGE_EXECUTE_READWRITE,
                &previousProtection))
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }

        detour.target = target;
        detour.trampoline = trampoline;
        detour.displacedBytes = displacedBytes;
        std::memcpy(target, patch.data(), displacedBytes);
        FlushInstructionCache(GetCurrentProcess(), target, displacedBytes);
        FlushInstructionCache(
            GetCurrentProcess(),
            trampoline,
            displacedBytes + 5);

        DWORD discarded = 0;
        VirtualProtect(target, displacedBytes, previousProtection, &discarded);
        return true;
    }

    void CreatureActionLifecycleObserver::RestoreDetour(Detour& detour) noexcept
    {
        if (detour.target == nullptr)
        {
            return;
        }

        DWORD previousProtection = 0;
        if (VirtualProtect(
                detour.target,
                detour.displacedBytes,
                PAGE_EXECUTE_READWRITE,
                &previousProtection))
        {
            std::memcpy(
                detour.target,
                detour.originalBytes.data(),
                detour.displacedBytes);
            FlushInstructionCache(
                GetCurrentProcess(),
                detour.target,
                detour.displacedBytes);
            DWORD discarded = 0;
            VirtualProtect(
                detour.target,
                detour.displacedBytes,
                previousProtection,
                &discarded);
        }
        if (detour.trampoline != nullptr)
        {
            VirtualFree(detour.trampoline, 0, MEM_RELEASE);
        }
        detour = {};
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

        void* activeAction = nullptr;
        __try
        {
            if (creature != nullptr)
            {
                activeAction = *reinterpret_cast<void**>(
                    static_cast<std::uint8_t*>(creature) + 0x120);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            activeAction = nullptr;
        }
        const AuthorityGate gate = observer->authorityGate_.load(
            std::memory_order_acquire);
        if (!observer->IsAuthoritativeReplay(activeAction) &&
            gate != nullptr && !gate(
                observer->authorityGateContext_.load(
                    std::memory_order_acquire),
                creature,
                activeAction))
        {
            return;
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
        const bool replaySubmission = authoritativeReplayDepth_ != 0;
        const bool authorityDenied = !replaySubmission &&
            gate != nullptr && !gate(
            observer->authorityGateContext_.load(
                std::memory_order_acquire),
            creature,
            action);
        const bool accepted = !authorityDenied &&
            observer->originalSubmit_(creature, action);
        if (submissionReceiptDepth_ != 0 &&
            submissionReceiptCreature_ == creature)
        {
            submissionReceiptObserved_ = true;
            submissionReceiptAccepted_ =
                submissionReceiptAccepted_ || accepted;
        }
        if (accepted && replaySubmission)
        {
            void* activeAction = nullptr;
            __try
            {
                activeAction = creature != nullptr
                    ? *reinterpret_cast<void**>(
                        static_cast<std::uint8_t*>(creature) + 0x120)
                    : nullptr;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                activeAction = nullptr;
            }
            observer->RememberAuthoritativeReplay(activeAction);
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
        observer->ForgetAuthoritativeReplay(action);
        observer->ReportFinish(action, creature, actionType);
    }

    void CreatureActionLifecycleObserver::ReportSubmission(
        void* creature,
        void* requestedAction,
        const char* requestedType,
        bool accepted,
        bool authorityDenied) noexcept
    {
        const unsigned int ordinal = submissionCount_.fetch_add(
            1,
            std::memory_order_acq_rel) + 1;

        void* activeAction = nullptr;
        char activeType[ActionNameCapacity] = {};
        __try
        {
            if (creature != nullptr)
            {
                activeAction = *reinterpret_cast<void**>(
                    static_cast<std::uint8_t*>(creature) + 0x120);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            activeAction = nullptr;
        }
        ReadActionType(activeAction, activeType);
        const ThingContext context = ReadThingContext(creature);

        CreatureActionLifecycleEvent event;
        event.phase = CreatureActionLifecyclePhase::Submitted;
        event.thingUid = context.uid;
        event.observedAt = GetTickCount64();
        event.mapId = context.mapId;
        event.threadId = GetCurrentThreadId();
        event.accepted = accepted;
        event.creature = creature;
        event.action = accepted && activeAction != nullptr
            ? activeAction
            : requestedAction;
        strncpy_s(
            event.actionType,
            accepted && activeType[0] != '\0'
                ? activeType
                : requestedType != nullptr ? requestedType : "",
            _TRUNCATE);
        event.animationId = accepted &&
                ActionMayCarryAnimation(event.actionType)
            ? ReadAnimationId(event.action)
            : 0;
        event.targetCreature = accepted
            ? ReadAttackTarget(gameModule_, event.action, event.actionType)
            : nullptr;
        event.targetThingUid = ReadThingContext(event.targetCreature).uid;
        Notify(event);
        if (ordinal > DiagnosticEventLimit)
        {
            return;
        }

        char detail[640] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "ordinal=%u accepted=%s authority_denied=%s thing_uid=%016llX map_id=%u creature=%p requested=%p requested_type=%s active=%p active_type=%s animation_id=%u target=%p target_uid=%016llX context_readable=%s thread=%lu",
            ordinal,
            accepted ? "true" : "false",
            authorityDenied ? "true" : "false",
            static_cast<unsigned long long>(context.uid),
            static_cast<unsigned int>(context.mapId),
            creature,
            requestedAction,
            requestedType != nullptr && requestedType[0] != '\0' ? requestedType : "<unknown>",
            activeAction,
            activeType[0] != '\0' ? activeType : "<unknown>",
            event.animationId,
            event.targetCreature,
            static_cast<unsigned long long>(event.targetThingUid),
            context.readable ? "true" : "false",
            static_cast<unsigned long>(GetCurrentThreadId()));
        diagnostics_.Event("CreatureActionSubmitted", detail);
    }

    void CreatureActionLifecycleObserver::ReportFinish(
        void* action,
        void* creature,
        const char* actionType) noexcept
    {
        const unsigned int ordinal = finishCount_.fetch_add(
            1,
            std::memory_order_acq_rel) + 1;

        const ThingContext context = ReadThingContext(creature);
        CreatureActionLifecycleEvent event;
        event.phase = CreatureActionLifecyclePhase::Finished;
        event.thingUid = context.uid;
        event.observedAt = GetTickCount64();
        event.mapId = context.mapId;
        event.threadId = GetCurrentThreadId();
        event.accepted = true;
        event.creature = creature;
        event.action = action;
        strncpy_s(
            event.actionType,
            actionType != nullptr ? actionType : "",
            _TRUNCATE);
        Notify(event);
        if (ordinal > DiagnosticEventLimit)
        {
            return;
        }
        char detail[512] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "ordinal=%u thing_uid=%016llX map_id=%u creature=%p action=%p action_type=%s context_readable=%s thread=%lu",
            ordinal,
            static_cast<unsigned long long>(context.uid),
            static_cast<unsigned int>(context.mapId),
            creature,
            action,
            actionType != nullptr && actionType[0] != '\0' ? actionType : "<unknown>",
            context.readable ? "true" : "false",
            static_cast<unsigned long>(GetCurrentThreadId()));
        diagnostics_.Event("CreatureActionFinished", detail);
    }

    bool CreatureActionLifecycleObserver::ReadActionType(
        void* action,
        char (&name)[ActionNameCapacity]) noexcept
    {
        return DescribeActionType(action, name, ActionNameCapacity);
    }

    std::uint32_t CreatureActionLifecycleObserver::ReadAnimationId(
        void* action) noexcept
    {
        if (action == nullptr)
        {
            return 0;
        }
        std::int32_t animationId = 0;
        __try
        {
            void* const resource = *reinterpret_cast<void**>(
                static_cast<std::uint8_t*>(action) + 0x74);
            if (resource != nullptr)
            {
                animationId = *static_cast<const std::int32_t*>(resource);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            animationId = 0;
        }
        return animationId > 0 && animationId <= 0xFFFF
            ? static_cast<std::uint32_t>(animationId)
            : 0;
    }

    void* CreatureActionLifecycleObserver::ReadAttackTarget(
        HMODULE gameModule,
        void* action,
        const char* actionType) noexcept
    {
        // CCreatureAction_InterruptableMidAttack and its concrete variants
        // derive from the verified attack base constructed at retail RVA
        // 0x017A70E0. That base owns its target as a weak pointer at +0xB4.
        // Resolve it through Fable's weak getter so no object memory or raw
        // address is ever serialized.
        if (gameModule == nullptr || action == nullptr ||
            actionType == nullptr ||
            std::strstr(actionType, "InterruptableMidAttack") == nullptr)
        {
            return nullptr;
        }
        constexpr std::uintptr_t WeakPointerGetRva = 0x012E6EA0;
        constexpr std::size_t TargetWeakPointerOffset = 0xB4;
        constexpr std::array<std::uint8_t, 6> ExpectedPrefix = {
            0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x04,
        };
        void* target = nullptr;
        __try
        {
            auto* const getterAddress = reinterpret_cast<std::uint8_t*>(
                reinterpret_cast<std::uintptr_t>(gameModule) +
                WeakPointerGetRva);
            if (std::memcmp(
                    getterAddress,
                    ExpectedPrefix.data(),
                    ExpectedPrefix.size()) != 0)
            {
                return nullptr;
            }
            using WeakPointerGet = void* (__thiscall*)(void* weakPointer);
            target = reinterpret_cast<WeakPointerGet>(getterAddress)(
                static_cast<std::uint8_t*>(action) +
                    TargetWeakPointerOffset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            target = nullptr;
        }
        return target;
    }

    bool CreatureActionLifecycleObserver::ActionMayCarryAnimation(
        const char* actionType) noexcept
    {
        if (actionType == nullptr || actionType[0] == '\0')
        {
            return false;
        }
        constexpr const char* markers[] = {
            "Animation",
            "Attack",
            "Combat",
            "Strike",
            "Weapon",
            "Projectile",
            "Block",
            "Parry",
            "Dodge",
            "Hit",
            "Damage",
            "Response",
            "Unsheathe",
            "Sheathe",
            "Cast",
            "Spell",
            "Ability",
        };
        for (const char* marker : markers)
        {
            if (std::strstr(actionType, marker) != nullptr)
            {
                return true;
            }
        }
        return false;
    }

    void* CreatureActionLifecycleObserver::ResolveActionOwner(void* action) noexcept
    {
        void* creature = nullptr;
        __try
        {
            if (action != nullptr)
            {
                void* const countedPointer = *reinterpret_cast<void**>(
                    static_cast<std::uint8_t*>(action) + 0x0C);
                if (countedPointer != nullptr)
                {
                    creature = *reinterpret_cast<void**>(countedPointer);
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            creature = nullptr;
        }
        return creature;
    }

    CreatureActionLifecycleObserver::ThingContext
        CreatureActionLifecycleObserver::ReadThingContext(void* creature) noexcept
    {
        ThingContext context;
        __try
        {
            if (creature != nullptr)
            {
                const auto* const bytes = static_cast<const std::uint8_t*>(creature);
                context.uid = *reinterpret_cast<const std::uint64_t*>(bytes + 0x14);
                context.mapId = *reinterpret_cast<const std::uint16_t*>(bytes + 0x9A);
                context.readable = true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            context = {};
        }
        return context;
    }

    void CreatureActionLifecycleObserver::Notify(
        const CreatureActionLifecycleEvent& event) noexcept
    {
        std::array<EventSubscription, EventSinkCapacity> subscriptions = {};
        AcquireSRWLockShared(&eventSinkLock_);
        subscriptions = eventSinks_;
        ReleaseSRWLockShared(&eventSinkLock_);
        for (const EventSubscription& subscription : subscriptions)
        {
            if (subscription.sink == nullptr)
            {
                continue;
            }
            subscription.sink(subscription.context, event);
        }
    }

    void CreatureActionLifecycleObserver::NotifyPostUpdate(
        void* creature) noexcept
    {
        std::array<PostUpdateSubscription, PostUpdateSinkCapacity>
            subscriptions = {};
        AcquireSRWLockShared(&postUpdateSinkLock_);
        subscriptions = postUpdateSinks_;
        ReleaseSRWLockShared(&postUpdateSinkLock_);
        for (const PostUpdateSubscription& subscription : subscriptions)
        {
            if (subscription.sink != nullptr)
            {
                subscription.sink(subscription.context, *this, creature);
            }
        }
    }

    bool CreatureActionLifecycleObserver::IsAuthoritativeReplay(
        void* action) const noexcept
    {
        if (action == nullptr)
        {
            return false;
        }
        AcquireSRWLockShared(&authoritativeReplayLock_);
        const bool found = authoritativeReplayActions_.find(action) !=
            authoritativeReplayActions_.end();
        ReleaseSRWLockShared(&authoritativeReplayLock_);
        return found;
    }

    void CreatureActionLifecycleObserver::RememberAuthoritativeReplay(
        void* action) noexcept
    {
        if (action == nullptr)
        {
            return;
        }
        AcquireSRWLockExclusive(&authoritativeReplayLock_);
        authoritativeReplayActions_.insert(action);
        ReleaseSRWLockExclusive(&authoritativeReplayLock_);
    }

    void CreatureActionLifecycleObserver::ForgetAuthoritativeReplay(
        void* action) noexcept
    {
        if (action == nullptr)
        {
            return;
        }
        AcquireSRWLockExclusive(&authoritativeReplayLock_);
        authoritativeReplayActions_.erase(action);
        ReleaseSRWLockExclusive(&authoritativeReplayLock_);
    }
}
