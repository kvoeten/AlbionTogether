#include "FollowCreatureActionHook.h"

#include <array>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
    using Patch = std::array<
        std::uint8_t,
        fable::game::creature::locomotion::native::
            FollowCreatureActionFunctions::DisplacedBytes>;

    bool BuildTrampoline(
        std::uint8_t* target,
        const void* replacement,
        void*& trampolineResult,
        Patch& patchResult) noexcept
    {
        constexpr std::size_t displacedBytes =
            fable::game::creature::locomotion::native::
                FollowCreatureActionFunctions::DisplacedBytes;
        trampolineResult = nullptr;
        patchResult.fill(0x90);

        auto* const trampoline = static_cast<std::uint8_t*>(VirtualAlloc(
            nullptr,
            displacedBytes + 5,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_EXECUTE_READWRITE));
        if (trampoline == nullptr)
        {
            return false;
        }

        std::memcpy(trampoline, target, displacedBytes);
        trampoline[displacedBytes] = 0xE9;
        const std::intptr_t resumeDisplacement =
            reinterpret_cast<std::intptr_t>(target + displacedBytes) -
            (reinterpret_cast<std::intptr_t>(trampoline + displacedBytes) + 5);
        const std::intptr_t observerDisplacement =
            reinterpret_cast<std::intptr_t>(replacement) -
            (reinterpret_cast<std::intptr_t>(target) + 5);
        if (resumeDisplacement < INT32_MIN || resumeDisplacement > INT32_MAX ||
            observerDisplacement < INT32_MIN || observerDisplacement > INT32_MAX)
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }

        const std::int32_t resumeRelative =
            static_cast<std::int32_t>(resumeDisplacement);
        std::memcpy(
            trampoline + displacedBytes + 1,
            &resumeRelative,
            sizeof(resumeRelative));
        FlushInstructionCache(
            GetCurrentProcess(),
            trampoline,
            displacedBytes + 5);

        patchResult[0] = 0xE9;
        const std::int32_t observerRelative =
            static_cast<std::int32_t>(observerDisplacement);
        std::memcpy(
            patchResult.data() + 1,
            &observerRelative,
            sizeof(observerRelative));
        trampolineResult = trampoline;
        return true;
    }
}

namespace fable::game::creature::locomotion
{
    FollowCreatureActionHook* FollowCreatureActionHook::active_ = nullptr;

    bool FollowCreatureActionHook::Install(
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
            "Hook: follow-action lifecycle observation is only supported by the x86 client.");
        return false;
#else
        if (active_ != nullptr && active_ != this)
        {
            diagnostics_.Log(
                "Hook: another follow-action lifecycle observer is already active.");
            return false;
        }

        std::uint8_t* startTarget = nullptr;
        std::uint8_t* tickTarget = nullptr;
        if (!native::FollowCreatureActionFunctions::ResolveStart(
                gameModule,
                startTarget) ||
            !native::FollowCreatureActionFunctions::ResolveTick(
                gameModule,
                tickTarget))
        {
            diagnostics_.Log(
                "Hook: native follow-action Start/Tick definitions failed validation; the executable ABI drifted.");
            return false;
        }

        Patch startPatch = {};
        Patch tickPatch = {};
        void* startTrampoline = nullptr;
        void* tickTrampoline = nullptr;
        if (!BuildTrampoline(
                startTarget,
                reinterpret_cast<const void*>(&FollowCreatureActionHook::ObserveStart),
                startTrampoline,
                startPatch) ||
            !BuildTrampoline(
                tickTarget,
                reinterpret_cast<const void*>(&FollowCreatureActionHook::ObserveTick),
                tickTrampoline,
                tickPatch))
        {
            if (startTrampoline != nullptr)
            {
                VirtualFree(startTrampoline, 0, MEM_RELEASE);
            }
            if (tickTrampoline != nullptr)
            {
                VirtualFree(tickTrampoline, 0, MEM_RELEASE);
            }
            diagnostics_.Log(
                "Hook: follow-action lifecycle trampoline construction failed.");
            return false;
        }

        DWORD startProtection = 0;
        DWORD tickProtection = 0;
        if (!VirtualProtect(
                startTarget,
                startPatch.size(),
                PAGE_EXECUTE_READWRITE,
                &startProtection))
        {
            VirtualFree(startTrampoline, 0, MEM_RELEASE);
            VirtualFree(tickTrampoline, 0, MEM_RELEASE);
            diagnostics_.Log(
                "Hook: follow-action Start protection change failed.");
            return false;
        }
        if (!VirtualProtect(
                tickTarget,
                tickPatch.size(),
                PAGE_EXECUTE_READWRITE,
                &tickProtection))
        {
            DWORD discarded = 0;
            VirtualProtect(
                startTarget,
                startPatch.size(),
                startProtection,
                &discarded);
            VirtualFree(startTrampoline, 0, MEM_RELEASE);
            VirtualFree(tickTrampoline, 0, MEM_RELEASE);
            diagnostics_.Log(
                "Hook: follow-action Tick protection change failed.");
            return false;
        }

        startTrampoline_ = startTrampoline;
        tickTrampoline_ = tickTrampoline;
        startTarget_ = startTarget;
        tickTarget_ = tickTarget;
        originalStart_ = reinterpret_cast<
            native::FollowCreatureActionFunctions::ActionMethodPointer>(
                startTrampoline_);
        originalTick_ = reinterpret_cast<
            native::FollowCreatureActionFunctions::ActionMethodPointer>(
                tickTrampoline_);
        active_ = this;

        std::memcpy(startTarget, startPatch.data(), startPatch.size());
        std::memcpy(tickTarget, tickPatch.data(), tickPatch.size());
        FlushInstructionCache(
            GetCurrentProcess(),
            startTarget,
            startPatch.size());
        FlushInstructionCache(
            GetCurrentProcess(),
            tickTarget,
            tickPatch.size());

        DWORD discarded = 0;
        if (!VirtualProtect(
                tickTarget,
                tickPatch.size(),
                tickProtection,
                &discarded) ||
            !VirtualProtect(
                startTarget,
                startPatch.size(),
                startProtection,
                &discarded))
        {
            diagnostics_.Log(
                "Hook: follow-action lifecycle observers installed, but code protection restoration failed.");
        }

        char detail[256] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "start=%p tick=%p",
            startTarget,
            tickTarget);
        diagnostics_.Log(
            "Hook: native follow-action Start/Tick lifecycle observers installed.");
        diagnostics_.Event("FollowActionHooksReady", detail);
        return true;
#endif
    }

    void FollowCreatureActionHook::Shutdown() noexcept
    {
#if defined(_M_IX86)
        if (startTarget_ != nullptr && tickTarget_ != nullptr)
        {
            auto restore = [](std::uint8_t* target, void* trampoline) noexcept
            {
                constexpr std::size_t bytes = native::FollowCreatureActionFunctions::DisplacedBytes;
                if (target == nullptr || trampoline == nullptr) return;
                DWORD protection = 0;
                if (VirtualProtect(target, bytes, PAGE_EXECUTE_READWRITE, &protection))
                {
                    std::memcpy(target, trampoline, bytes);
                    FlushInstructionCache(GetCurrentProcess(), target, bytes);
                    DWORD discarded = 0;
                    VirtualProtect(target, bytes, protection, &discarded);
                }
            };
            restore(tickTarget_, tickTrampoline_);
            restore(startTarget_, startTrampoline_);
        }
#endif
        if (active_ == this) active_ = nullptr;
        originalStart_ = nullptr;
        originalTick_ = nullptr;
        if (startTrampoline_ != nullptr) VirtualFree(startTrampoline_, 0, MEM_RELEASE);
        if (tickTrampoline_ != nullptr) VirtualFree(tickTrampoline_, 0, MEM_RELEASE);
        startTrampoline_ = nullptr;
        tickTrampoline_ = nullptr;
        startTarget_ = nullptr;
        tickTarget_ = nullptr;
        gameModule_ = nullptr;
        diagnostics_ = {};
    }

    bool FollowCreatureActionHook::IsInstalled() const noexcept
    {
        return active_ == this &&
            originalStart_ != nullptr && originalTick_ != nullptr &&
            startTrampoline_ != nullptr && tickTrampoline_ != nullptr;
    }

    unsigned int FollowCreatureActionHook::StartCount() const noexcept
    {
        return startCount_.load(std::memory_order_acquire);
    }

    unsigned int FollowCreatureActionHook::TickCount() const noexcept
    {
        return tickCount_.load(std::memory_order_acquire);
    }

    void FollowCreatureActionHook::Report(
        const char* state,
        const char* boundary,
        void* action,
        unsigned int ordinal) const
    {
        void* controllerHandle = nullptr;
        void* ownerThing = nullptr;
        void* targetImplementation = nullptr;
        float stopDistance = 0.0f;
        bool avoidDynamicObstacles = false;
        bool readable = false;
        __try
        {
            const auto* const bytes = static_cast<const std::uint8_t*>(action);
            controllerHandle = *reinterpret_cast<void* const*>(
                bytes + native::FollowCreatureActionFunctions::ActionControllerOffset);
            if (controllerHandle != nullptr)
            {
                ownerThing = *(static_cast<void**>(controllerHandle) + 1);
            }
            targetImplementation = *reinterpret_cast<void* const*>(
                bytes + native::FollowCreatureActionFunctions::ActionTargetOffset +
                    sizeof(void*));
            stopDistance = *reinterpret_cast<const float*>(
                bytes + native::FollowCreatureActionFunctions::ActionDistanceOffset);
            avoidDynamicObstacles = bytes[
                native::FollowCreatureActionFunctions::
                    ActionAvoidDynamicObstaclesOffset] != 0;
            readable = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            readable = false;
        }

        char detail[512] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "boundary=%s action=%p ordinal=%u controller=%p owner=%p target=%p stop_distance=%.3f avoid_dynamic_obstacles=%s readable=%s thread=%lu",
            boundary,
            action,
            ordinal,
            controllerHandle,
            ownerThing,
            targetImplementation,
            stopDistance,
            avoidDynamicObstacles ? "true" : "false",
            readable ? "true" : "false",
            static_cast<unsigned long>(GetCurrentThreadId()));
        diagnostics_.Event(state, detail);
    }

    void __fastcall FollowCreatureActionHook::ObserveStart(void* action, void*)
    {
        FollowCreatureActionHook* const hook = active_;
        if (hook == nullptr || hook->originalStart_ == nullptr)
        {
            return;
        }
        const unsigned int ordinal =
            hook->startCount_.fetch_add(1, std::memory_order_acq_rel) + 1;
        hook->originalStart_(action);
        if (ordinal <= 16)
        {
            hook->Report(
                "FollowActionStartObserved",
                "start",
                action,
                ordinal);
        }
    }

    void __fastcall FollowCreatureActionHook::ObserveTick(void* action, void*)
    {
        FollowCreatureActionHook* const hook = active_;
        if (hook == nullptr || hook->originalTick_ == nullptr)
        {
            return;
        }
        const unsigned int ordinal =
            hook->tickCount_.fetch_add(1, std::memory_order_acq_rel) + 1;
        hook->originalTick_(action);
        if (ordinal == 1 || ordinal == 2 || ordinal == 10 || ordinal == 60)
        {
            hook->Report(
                "FollowActionTickObserved",
                "tick",
                action,
                ordinal);
        }
    }
}
