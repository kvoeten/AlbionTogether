#include "FollowCreatureActionHook.h"

#include <cstdint>
#include <cstdio>

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

        const bool startInstalled = startHook_.Install(
                startTarget,
                native::FollowCreatureActionFunctions::StartExpectedPrefix.data(),
                native::FollowCreatureActionFunctions::StartExpectedPrefix.size(),
                reinterpret_cast<void*>(&FollowCreatureActionHook::ObserveStart),
                native::FollowCreatureActionFunctions::DisplacedBytes);
        const bool tickInstalled = startInstalled && tickHook_.Install(
                tickTarget,
                native::FollowCreatureActionFunctions::TickExpectedPrefix.data(),
                native::FollowCreatureActionFunctions::TickExpectedPrefix.size(),
                reinterpret_cast<void*>(&FollowCreatureActionHook::ObserveTick),
                native::FollowCreatureActionFunctions::DisplacedBytes);
        if (!startInstalled || !tickInstalled)
        {
            const bool startRemoved = !startHook_.IsInstalled() ||
                startHook_.Shutdown();
            const bool tickRemoved = !tickHook_.IsInstalled() ||
                tickHook_.Shutdown();
            if (!startRemoved || !tickRemoved)
            {
                originalStart_ = startHook_.IsInstalled()
                    ? reinterpret_cast<
                        native::FollowCreatureActionFunctions::ActionMethodPointer>(
                            startHook_.Original())
                    : nullptr;
                originalTick_ = tickHook_.IsInstalled()
                    ? reinterpret_cast<
                        native::FollowCreatureActionFunctions::ActionMethodPointer>(
                            tickHook_.Original())
                    : nullptr;
                active_ = this;
                diagnostics_.Log(
                    "Hook: follow-action install failed and installed patches could not all be rolled back; callback state retained.");
                return false;
            }
            if (startHook_.ProtectionRestoreFailed() ||
                tickHook_.ProtectionRestoreFailed())
            {
                diagnostics_.Log(
                    "Hook: follow-action install rollback restored bytes, but code protection restoration failed.");
            }
            diagnostics_.Log(
                "Hook: follow-action lifecycle patch installation failed.");
            return false;
        }

        originalStart_ = reinterpret_cast<
            native::FollowCreatureActionFunctions::ActionMethodPointer>(
                startHook_.Original());
        originalTick_ = reinterpret_cast<
            native::FollowCreatureActionFunctions::ActionMethodPointer>(
                tickHook_.Original());
        active_ = this;

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
        const bool startRemoved = !startHook_.IsInstalled() || startHook_.Shutdown();
        const bool tickRemoved = !tickHook_.IsInstalled() || tickHook_.Shutdown();
        if (!startRemoved || !tickRemoved)
        {
            diagnostics_.Log(
                "Hook: follow-action shutdown skipped because a target changed.");
            return;
        }
        if (startHook_.ProtectionRestoreFailed() ||
            tickHook_.ProtectionRestoreFailed())
        {
            diagnostics_.Log(
                "Hook: follow-action bytes restored, but code protection restoration failed.");
        }
        if (active_ == this) active_ = nullptr;
        originalStart_ = nullptr;
        originalTick_ = nullptr;
        gameModule_ = nullptr;
        diagnostics_ = {};
    }

    bool FollowCreatureActionHook::IsInstalled() const noexcept
    {
        return active_ == this &&
            originalStart_ != nullptr && originalTick_ != nullptr &&
            startHook_.IsInstalled() && tickHook_.IsInstalled();
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
