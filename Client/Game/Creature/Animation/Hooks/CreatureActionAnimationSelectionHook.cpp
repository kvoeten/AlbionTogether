#include "CreatureActionAnimationSelectionHook.h"

#include "Game/Creature/Actions/Hooks/CreatureActionLifecycleObserver.h"
#include "Game/Entity/Native/ThingComponentAccess.h"

#include <climits>
#include <cstdio>
#include <cstring>

namespace
{
    constexpr std::array<std::uint8_t, 3> kSubmitPrefix = {
        0x6A, 0xFF, 0x68,
    };
    constexpr std::array<std::uint8_t, 9> kValidatePrefix = {
        0x56, 0x8B, 0x74, 0x24, 0x08, 0x85, 0xF6, 0x7E, 0x19,
    };
}

namespace fable::game::creature::animation
{
    CreatureActionAnimationSelectionHook*
        CreatureActionAnimationSelectionHook::active_ = nullptr;
    thread_local std::uint64_t
        CreatureActionAnimationSelectionHook::scopedSelectionToken_ = 0;

    bool CreatureActionAnimationSelectionHook::Install(
        HMODULE gameModule,
        const core::Diagnostics& diagnostics) noexcept
    {
        if (IsInstalled())
        {
            return true;
        }
        Shutdown();
        diagnostics_ = diagnostics;
#if !defined(_M_IX86)
        return false;
#else
        if (gameModule == nullptr || active_ != nullptr)
        {
            return false;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        auto* const target = reinterpret_cast<std::uint8_t*>(
            base + native::AnimationPlaybackFunctions::SubmitRequestRva);
        auto* const validate = reinterpret_cast<std::uint8_t*>(
            base + native::AnimationPlaybackFunctions::ValidateAnimationRva);
        std::uintptr_t exceptionHandler = 0;
        __try
        {
            if (std::memcmp(
                    target, kSubmitPrefix.data(), kSubmitPrefix.size()) != 0 ||
                std::memcmp(
                    validate, kValidatePrefix.data(), kValidatePrefix.size()) != 0)
            {
                return false;
            }
            std::memcpy(
                &exceptionHandler,
                target + kSubmitPrefix.size(),
                sizeof(exceptionHandler));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        if (exceptionHandler != base +
                native::AnimationPlaybackFunctions::
                    SubmitRequestExceptionHandlerRva)
        {
            return false;
        }

        constexpr std::size_t displacedBytes = 7;
        auto* const trampoline = static_cast<std::uint8_t*>(VirtualAlloc(
            nullptr,
            displacedBytes + 5,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_EXECUTE_READWRITE));
        if (trampoline == nullptr)
        {
            return false;
        }
        std::memcpy(originalBytes_.data(), target, displacedBytes);
        std::memcpy(trampoline, target, displacedBytes);
        trampoline[displacedBytes] = 0xE9;
        const std::intptr_t resumeDisplacement =
            reinterpret_cast<std::intptr_t>(target + displacedBytes) -
            (reinterpret_cast<std::intptr_t>(trampoline + displacedBytes) + 5);
        const std::intptr_t interceptDisplacement =
            reinterpret_cast<std::intptr_t>(&Intercept) -
            (reinterpret_cast<std::intptr_t>(target) + 5);
        if (resumeDisplacement < INT32_MIN || resumeDisplacement > INT32_MAX ||
            interceptDisplacement < INT32_MIN ||
            interceptDisplacement > INT32_MAX)
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }
        const auto resume = static_cast<std::int32_t>(resumeDisplacement);
        std::memcpy(trampoline + displacedBytes + 1, &resume, sizeof(resume));
        FlushInstructionCache(
            GetCurrentProcess(), trampoline, displacedBytes + 5);

        std::array<std::uint8_t, displacedBytes> patch = {};
        patch.fill(0x90);
        patch[0] = 0xE9;
        const auto intercept = static_cast<std::int32_t>(interceptDisplacement);
        std::memcpy(patch.data() + 1, &intercept, sizeof(intercept));
        DWORD previousProtection = 0;
        if (!VirtualProtect(
                target,
                patch.size(),
                PAGE_EXECUTE_READWRITE,
                &previousProtection))
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }
        std::memcpy(target, patch.data(), patch.size());
        FlushInstructionCache(GetCurrentProcess(), target, patch.size());
        DWORD discarded = 0;
        VirtualProtect(
            target, patch.size(), previousProtection, &discarded);

        gameModule_ = gameModule;
        target_ = target;
        trampoline_ = trampoline;
        original_ = reinterpret_cast<SubmitRequestPointer>(trampoline);
        validateAnimation_ = reinterpret_cast<ValidateAnimationPointer>(
            validate);
        active_ = this;
        diagnostics_.Event(
            "CreatureActionAnimationSelectionHookReady",
            "replicated native actions retain the owner-selected animation");
        return true;
#endif
    }

    void CreatureActionAnimationSelectionHook::Shutdown() noexcept
    {
        DetachActionLifecycleObserver();
        scopedSelectionToken_ = 0;
        if (active_ == this)
        {
            active_ = nullptr;
        }
        if (target_ != nullptr && trampoline_ != nullptr)
        {
            DWORD previousProtection = 0;
            if (VirtualProtect(
                    target_,
                    originalBytes_.size(),
                    PAGE_EXECUTE_READWRITE,
                    &previousProtection))
            {
                std::memcpy(
                    target_, originalBytes_.data(), originalBytes_.size());
                FlushInstructionCache(
                    GetCurrentProcess(), target_, originalBytes_.size());
                DWORD discarded = 0;
                VirtualProtect(
                    target_,
                    originalBytes_.size(),
                    previousProtection,
                    &discarded);
            }
        }
        if (trampoline_ != nullptr)
        {
            VirtualFree(trampoline_, 0, MEM_RELEASE);
        }
        gameModule_ = nullptr;
        target_ = nullptr;
        trampoline_ = nullptr;
        original_ = nullptr;
        validateAnimation_ = nullptr;
        originalBytes_ = {};
        AcquireSRWLockExclusive(&selectionLock_);
        selections_ = {};
        pendingSelectionCount_.store(0, std::memory_order_release);
        ReleaseSRWLockExclusive(&selectionLock_);
        nextSelectionToken_.store(0, std::memory_order_release);
        diagnostics_ = {};
    }

    bool CreatureActionAnimationSelectionHook::BeginSelection(
        void* creature,
        const char* actionType,
        const std::uint32_t animationId) noexcept
    {
        if (!IsInstalled() || creature == nullptr || actionType == nullptr ||
            actionType[0] == '\0' || animationId == 0 ||
            animationId > native::AnimationPlaybackFunctions::
                MaximumAnimationId ||
            scopedSelectionToken_ != 0)
        {
            return false;
        }

        const std::uint64_t now = GetTickCount64();
        void* const animationState = ResolveAnimationState(creature);
        if (animationState == nullptr)
        {
            return false;
        }
        ReportExpiredSelections(now);
        Selection displaced;
        Selection* available = nullptr;
        AcquireSRWLockExclusive(&selectionLock_);
        for (Selection& pending : selections_)
        {
            if (pending.creature == creature)
            {
                displaced = pending;
                available = &pending;
                break;
            }
            if (available == nullptr && pending.token == 0)
            {
                available = &pending;
            }
        }
        if (available == nullptr)
        {
            ReleaseSRWLockExclusive(&selectionLock_);
            return false;
        }
        std::uint64_t token = nextSelectionToken_.fetch_add(
            1, std::memory_order_acq_rel) + 1;
        if (token == 0)
        {
            token = nextSelectionToken_.fetch_add(
                1, std::memory_order_acq_rel) + 1;
        }
        const bool replacing = available->token != 0;
        *available = {};
        available->token = token;
        available->expiresAt = now + SelectionLifetimeMilliseconds;
        available->creature = creature;
        available->animationState = animationState;
        available->animationId = animationId;
        strncpy_s(
            available->actionType.data(),
            available->actionType.size(),
            actionType,
            _TRUNCATE);
        if (!replacing)
        {
            pendingSelectionCount_.fetch_add(1, std::memory_order_acq_rel);
        }
        ReleaseSRWLockExclusive(&selectionLock_);
        if (displaced.token != 0)
        {
            ReportSelection(displaced);
        }
        scopedSelectionToken_ = token;
        return true;
    }

    void CreatureActionAnimationSelectionHook::EndSelection() noexcept
    {
        // Native actions usually request their animation on a later creature
        // update, often on another engine thread. End only closes the caller's
        // submission scope; the bounded actor-scoped selection remains live
        // until it is consumed, superseded, or expires.
        const std::uint64_t token = scopedSelectionToken_;
        scopedSelectionToken_ = 0;
        if (token == 0)
        {
            return;
        }
        Selection unbound;
        AcquireSRWLockExclusive(&selectionLock_);
        for (Selection& pending : selections_)
        {
            if (pending.token == token && pending.action == nullptr)
            {
                unbound = pending;
                pending = {};
                pendingSelectionCount_.fetch_sub(
                    1, std::memory_order_acq_rel);
                break;
            }
        }
        ReleaseSRWLockExclusive(&selectionLock_);
        if (unbound.token != 0)
        {
            ReportSelection(unbound);
        }
    }

    bool CreatureActionAnimationSelectionHook::AttachActionLifecycleObserver(
        actions::CreatureActionLifecycleObserver& observer) noexcept
    {
        if (!IsInstalled() || !observer.IsInstalled())
        {
            return false;
        }
        if (actionObserver_ == &observer)
        {
            return true;
        }
        DetachActionLifecycleObserver();
        if (!observer.AddEventSink(
                &CreatureActionAnimationSelectionHook::ObserveActionLifecycle,
                this))
        {
            return false;
        }
        actionObserver_ = &observer;
        return true;
    }

    void CreatureActionAnimationSelectionHook::
        DetachActionLifecycleObserver() noexcept
    {
        if (actionObserver_ != nullptr)
        {
            actionObserver_->RemoveEventSink(
                &CreatureActionAnimationSelectionHook::ObserveActionLifecycle,
                this);
        }
        actionObserver_ = nullptr;
    }

    bool CreatureActionAnimationSelectionHook::IsInstalled() const noexcept
    {
        return active_ == this && gameModule_ != nullptr && target_ != nullptr &&
            trampoline_ != nullptr && original_ != nullptr &&
            validateAnimation_ != nullptr;
    }

    bool CreatureActionAnimationSelectionHook::ActiveActionMatches(
        const Selection& selection) noexcept
    {
        if (selection.creature == nullptr || selection.action == nullptr ||
            selection.actionType[0] == '\0')
        {
            return false;
        }
        void* activeAction = nullptr;
        __try
        {
            activeAction = *reinterpret_cast<void**>(
                static_cast<std::uint8_t*>(selection.creature) + 0x120);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        if (activeAction != selection.action)
        {
            return false;
        }
        char actionType[ActionTypeCapacity] = {};
        return actions::CreatureActionLifecycleObserver::DescribeActionType(
                activeAction, actionType, sizeof(actionType)) &&
            std::strcmp(actionType, selection.actionType.data()) == 0;
    }

    bool CreatureActionAnimationSelectionHook::AnimationStateMatches(
        const Selection& selection,
        void* animationState) noexcept
    {
        return selection.animationState != nullptr &&
            selection.animationState == animationState;
    }

    void* CreatureActionAnimationSelectionHook::ResolveAnimationState(
        void* creature) noexcept
    {
        if (creature == nullptr)
        {
            return nullptr;
        }
        void* const component = entity::native::ThingComponentAccess::Find(
            creature,
            entity::native::ThingComponentType::AnimationComplex);
        if (component == nullptr)
        {
            return nullptr;
        }
        void* expectedState = nullptr;
        __try
        {
            expectedState = *reinterpret_cast<void**>(
                static_cast<std::uint8_t*>(component) +
                native::AnimationPlaybackFunctions::AnimationStateOffset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            expectedState = nullptr;
        }
        return expectedState;
    }

    void CreatureActionAnimationSelectionHook::ObserveActionLifecycle(
        void* context,
        const actions::CreatureActionLifecycleEvent& event) noexcept
    {
        if (context != nullptr)
        {
            static_cast<CreatureActionAnimationSelectionHook*>(context)->
                HandleActionLifecycle(event);
        }
    }

    void CreatureActionAnimationSelectionHook::HandleActionLifecycle(
        const actions::CreatureActionLifecycleEvent& event) noexcept
    {
        Selection retired;
        if (event.phase == actions::CreatureActionLifecyclePhase::Submitted)
        {
            const std::uint64_t token = scopedSelectionToken_;
            if (token == 0)
            {
                return;
            }
            void* const animationState = event.accepted
                ? ResolveAnimationState(event.creature)
                : nullptr;
            AcquireSRWLockExclusive(&selectionLock_);
            for (Selection& pending : selections_)
            {
                if (pending.token != token ||
                    pending.creature != event.creature)
                {
                    continue;
                }
                if (!event.accepted || event.action == nullptr ||
                    event.actionType[0] == '\0' ||
                    std::strcmp(
                        pending.actionType.data(), event.actionType) != 0 ||
                    animationState == nullptr)
                {
                    retired = pending;
                    pending = {};
                    pendingSelectionCount_.fetch_sub(
                        1, std::memory_order_acq_rel);
                }
                else
                {
                    pending.action = event.action;
                    pending.animationState = animationState;
                    pending.expiresAt = event.observedAt +
                        SelectionLifetimeMilliseconds;
                }
                break;
            }
            ReleaseSRWLockExclusive(&selectionLock_);
        }
        else if (event.phase ==
            actions::CreatureActionLifecyclePhase::Finished)
        {
            AcquireSRWLockExclusive(&selectionLock_);
            for (Selection& pending : selections_)
            {
                if (pending.token != 0 && pending.creature == event.creature &&
                    pending.action == event.action)
                {
                    retired = pending;
                    pending = {};
                    pendingSelectionCount_.fetch_sub(
                        1, std::memory_order_acq_rel);
                    break;
                }
            }
            ReleaseSRWLockExclusive(&selectionLock_);
        }
        if (retired.token != 0)
        {
            ReportSelection(retired);
        }
    }

    bool CreatureActionAnimationSelectionHook::TryApplySelection(
        void* animationState,
        const native::AnimationPlaybackRequest& request,
        native::AnimationPlaybackRequest& replacement) noexcept
    {
        if (pendingSelectionCount_.load(std::memory_order_acquire) == 0)
        {
            return false;
        }
        ReportExpiredSelections(GetTickCount64());
        Selection candidate;
        AcquireSRWLockShared(&selectionLock_);
        for (const Selection& pending : selections_)
        {
            if (pending.token == 0 ||
                !AnimationStateMatches(pending, animationState) ||
                !ActiveActionMatches(pending))
            {
                continue;
            }
            candidate = pending;
            break;
        }
        ReleaseSRWLockShared(&selectionLock_);
        if (candidate.token == 0)
        {
            return false;
        }
        bool valid = false;
        __try
        {
            valid = validateAnimation_(
                candidate.creature,
                static_cast<std::int32_t>(candidate.animationId));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        if (!valid)
        {
            return false;
        }
        Selection completed;
        AcquireSRWLockExclusive(&selectionLock_);
        for (Selection& pending : selections_)
        {
            if (pending.token != candidate.token ||
                pending.creature != candidate.creature ||
                pending.action != candidate.action ||
                !AnimationStateMatches(pending, animationState) ||
                !ActiveActionMatches(pending))
            {
                continue;
            }
            replacement = request;
            pending.observedAnimationId =
                static_cast<std::uint32_t>(request.animationId);
            replacement.animationId =
                static_cast<std::int32_t>(pending.animationId);
            pending.applied = true;
            completed = pending;
            pending = {};
            pendingSelectionCount_.fetch_sub(1, std::memory_order_acq_rel);
            break;
        }
        ReleaseSRWLockExclusive(&selectionLock_);
        if (completed.token != 0)
        {
            ReportSelection(completed);
            return true;
        }
        return false;
    }

    void CreatureActionAnimationSelectionHook::ReportExpiredSelections(
        const std::uint64_t now) noexcept
    {
        if (pendingSelectionCount_.load(std::memory_order_acquire) == 0)
        {
            return;
        }
        for (;;)
        {
            Selection expired;
            AcquireSRWLockExclusive(&selectionLock_);
            for (Selection& pending : selections_)
            {
                if (pending.token != 0 && pending.expiresAt <= now)
                {
                    expired = pending;
                    pending = {};
                    pendingSelectionCount_.fetch_sub(
                        1, std::memory_order_acq_rel);
                    break;
                }
            }
            ReleaseSRWLockExclusive(&selectionLock_);
            if (expired.token == 0)
            {
                break;
            }
            ReportSelection(expired);
        }
    }

    void __fastcall CreatureActionAnimationSelectionHook::Intercept(
        void* animationState,
        void*,
        const native::AnimationPlaybackRequest* request,
        const std::int32_t blendFrames,
        const std::int32_t options)
    {
        CreatureActionAnimationSelectionHook* const hook = active_;
        if (hook == nullptr || hook->original_ == nullptr)
        {
            return;
        }
        const native::AnimationPlaybackRequest* submitted = request;
        native::AnimationPlaybackRequest replacement = {};
        if (request != nullptr && hook->TryApplySelection(
                animationState, *request, replacement))
        {
            submitted = &replacement;
        }
        hook->original_(animationState, submitted, blendFrames, options);
    }

    void CreatureActionAnimationSelectionHook::ReportSelection(
        const Selection& selection) const noexcept
    {
        char detail[384] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "creature=%p action=%s owner_animation_id=%u local_selection_id=%u applied=%s",
            selection.creature,
            selection.actionType.data(),
            selection.animationId,
            selection.observedAnimationId,
            selection.applied ? "true" : "false");
        diagnostics_.Event(
            selection.applied
                ? "CreatureActionAnimationSelectionApplied"
                : "CreatureActionAnimationSelectionMissed",
            detail);
    }
}
