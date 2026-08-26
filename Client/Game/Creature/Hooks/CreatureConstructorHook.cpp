#include "CreatureConstructorHook.h"

#include <cstdint>
#include <cstdio>

namespace fable::game::creature
{
    CreatureConstructorHook* CreatureConstructorHook::active_ = nullptr;

    bool CreatureConstructorHook::Install(
        HMODULE gameModule,
        const core::Diagnostics& diagnostics)
    {
        if (IsInstalled())
        {
            return true;
        }
        diagnostics_ = diagnostics;

#if !defined(_M_IX86)
        diagnostics_.Log(
            "Hook: CThingCreature construction observation is only supported by the x86 client.");
        return false;
#else
        if (active_ != nullptr && active_ != this)
        {
            diagnostics_.Log(
                "Hook: another CThingCreature constructor observer is already active.");
            return false;
        }

        std::uint8_t* target = nullptr;
        if (!native::CreatureConstructorFunction::Resolve(gameModule, target))
        {
            diagnostics_.Log(
                "Hook: CThingCreature constructor definition validation failed; the executable ABI drifted.");
            return false;
        }

        if (!hook_.Install(
                target,
                native::CreatureConstructorFunction::ExpectedPrefix.data(),
                native::CreatureConstructorFunction::ExpectedPrefix.size(),
                reinterpret_cast<void*>(&CreatureConstructorHook::Observe),
                native::CreatureConstructorFunction::DisplacedBytes))
        {
            diagnostics_.Log(
                "Hook: CThingCreature constructor patch installation failed.");
            return false;
        }

        original_ = reinterpret_cast<native::CreatureConstructorFunction::Pointer>(
            hook_.Original());
        active_ = this;

        char detail[256] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "target=%p replacement=%p trampoline=%p",
            target,
            &CreatureConstructorHook::Observe,
            hook_.Original());
        diagnostics_.Log("Hook: CThingCreature constructor observer installed.");
        diagnostics_.Event(
            "CreatureLifecycleHookReady",
            "CThingCreature constructor observed; native pointer remains generation-scoped and is not a server identity");
        diagnostics_.Event("CreatureLifecycleHookAddresses", detail);
        return true;
#endif
    }

    void CreatureConstructorHook::Shutdown() noexcept
    {
        if (hook_.IsInstalled() && !hook_.Shutdown())
        {
            diagnostics_.Log(
                "Hook: CThingCreature constructor shutdown skipped because its target changed.");
            return;
        }
        if (active_ == this) active_ = nullptr;
        original_ = nullptr;
        diagnostics_ = {};
    }

    bool CreatureConstructorHook::IsInstalled() const noexcept
    {
        return original_ != nullptr && hook_.IsInstalled() && active_ == this;
    }

    unsigned int CreatureConstructorHook::ConstructionCount() const noexcept
    {
        return constructionCount_.load(std::memory_order_acquire);
    }

    DWORD CreatureConstructorHook::FirstConstructionThread() const noexcept
    {
        return firstConstructionThread_.load(std::memory_order_acquire);
    }

    DWORD CreatureConstructorHook::LastConstructionThread() const noexcept
    {
        return lastConstructionThread_.load(std::memory_order_acquire);
    }

    void* __fastcall CreatureConstructorHook::Observe(void* creature, void*)
    {
        CreatureConstructorHook* const hook = active_;
        void* const constructed = hook != nullptr && hook->original_ != nullptr
            ? hook->original_(creature)
            : creature;
        if (hook != nullptr)
        {
            const DWORD threadId = GetCurrentThreadId();
            DWORD noThread = 0;
            hook->firstConstructionThread_.compare_exchange_strong(
                noThread,
                threadId,
                std::memory_order_acq_rel);
            hook->lastConstructionThread_.store(
                threadId,
                std::memory_order_release);
            hook->constructionCount_.fetch_add(1, std::memory_order_acq_rel);
        }
        return constructed;
    }
}
