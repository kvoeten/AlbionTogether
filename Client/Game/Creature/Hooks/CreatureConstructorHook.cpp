#include "CreatureConstructorHook.h"

#include <array>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>

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

        constexpr std::size_t displacedBytes =
            native::CreatureConstructorFunction::DisplacedBytes;
        auto* const trampoline = static_cast<std::uint8_t*>(VirtualAlloc(
            nullptr,
            displacedBytes + 5,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_EXECUTE_READWRITE));
        if (trampoline == nullptr)
        {
            diagnostics_.Log(
                "Hook: CThingCreature constructor trampoline allocation failed.");
            return false;
        }

        std::memcpy(trampoline, target, displacedBytes);
        trampoline[displacedBytes] = 0xE9;
        const std::intptr_t trampolineDisplacement =
            reinterpret_cast<std::intptr_t>(target + displacedBytes) -
            (reinterpret_cast<std::intptr_t>(trampoline + displacedBytes) + 5);
        const std::intptr_t observerDisplacement =
            reinterpret_cast<std::intptr_t>(&CreatureConstructorHook::Observe) -
            (reinterpret_cast<std::intptr_t>(target) + 5);
        if (trampolineDisplacement < INT32_MIN ||
            trampolineDisplacement > INT32_MAX ||
            observerDisplacement < INT32_MIN ||
            observerDisplacement > INT32_MAX)
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            diagnostics_.Log(
                "Hook: CThingCreature constructor observer is outside the x86 relative-jump range.");
            return false;
        }

        const std::int32_t trampolineRelative =
            static_cast<std::int32_t>(trampolineDisplacement);
        std::memcpy(
            trampoline + displacedBytes + 1,
            &trampolineRelative,
            sizeof(trampolineRelative));
        FlushInstructionCache(
            GetCurrentProcess(),
            trampoline,
            displacedBytes + 5);

        std::array<std::uint8_t, displacedBytes> patch = {};
        patch.fill(0x90);
        patch[0] = 0xE9;
        const std::int32_t observerRelative =
            static_cast<std::int32_t>(observerDisplacement);
        std::memcpy(
            patch.data() + 1,
            &observerRelative,
            sizeof(observerRelative));

        DWORD previousProtection = 0;
        if (!VirtualProtect(
                target,
                patch.size(),
                PAGE_EXECUTE_READWRITE,
                &previousProtection))
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            diagnostics_.Log(
                "Hook: CThingCreature constructor code protection change failed.");
            return false;
        }

        trampoline_ = trampoline;
        target_ = target;
        original_ = reinterpret_cast<native::CreatureConstructorFunction::Pointer>(
            trampoline_);
        active_ = this;
        std::memcpy(target, patch.data(), patch.size());
        FlushInstructionCache(GetCurrentProcess(), target, patch.size());

        DWORD discardedProtection = 0;
        if (!VirtualProtect(
                target,
                patch.size(),
                previousProtection,
                &discardedProtection))
        {
            diagnostics_.Log(
                "Hook: CThingCreature constructor hook installed, but code protection restoration failed.");
        }

        char detail[256] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "target=%p replacement=%p trampoline=%p",
            target,
            &CreatureConstructorHook::Observe,
            trampoline_);
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
#if defined(_M_IX86)
        if (target_ != nullptr && trampoline_ != nullptr)
        {
            constexpr std::size_t displacedBytes = native::CreatureConstructorFunction::DisplacedBytes;
            DWORD protection = 0;
            if (VirtualProtect(target_, displacedBytes, PAGE_EXECUTE_READWRITE, &protection))
            {
                std::memcpy(target_, trampoline_, displacedBytes);
                FlushInstructionCache(GetCurrentProcess(), target_, displacedBytes);
                DWORD discarded = 0;
                VirtualProtect(target_, displacedBytes, protection, &discarded);
            }
        }
#endif
        if (active_ == this) active_ = nullptr;
        original_ = nullptr;
        target_ = nullptr;
        if (trampoline_ != nullptr) VirtualFree(trampoline_, 0, MEM_RELEASE);
        trampoline_ = nullptr;
        diagnostics_ = {};
    }

    bool CreatureConstructorHook::IsInstalled() const noexcept
    {
        return original_ != nullptr && trampoline_ != nullptr && active_ == this;
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
