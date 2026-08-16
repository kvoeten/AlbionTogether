#include "CombatHealthMutationHook.h"

#include <array>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
    constexpr std::size_t kMaximumHealthOffset = 0xCC;
    constexpr std::size_t kHealthOffset = 0xD0;
}

namespace fable::game::creature::combat
{
    CombatHealthMutationHook* CombatHealthMutationHook::active_ = nullptr;

    bool CombatHealthMutationHook::Install(
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
            "Hook: combat-health mutation observation is only supported by the x86 client.");
        return false;
#else
        if (active_ != nullptr && active_ != this)
        {
            return false;
        }
        std::uint8_t* target = nullptr;
        if (!native::CombatHealthMutationFunction::Resolve(gameModule, target))
        {
            diagnostics_.Log(
                "Hook: shared CThingCreature combat-health mutation definition validation failed.");
            return false;
        }
        constexpr std::size_t displacedBytes =
            native::CombatHealthMutationFunction::DisplacedBytes;
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
        const std::intptr_t hookDisplacement =
            reinterpret_cast<std::intptr_t>(&CombatHealthMutationHook::Intercept) -
            (reinterpret_cast<std::intptr_t>(target) + 5);
        if (resumeDisplacement < INT32_MIN || resumeDisplacement > INT32_MAX ||
            hookDisplacement < INT32_MIN || hookDisplacement > INT32_MAX)
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }
        const auto resumeRelative = static_cast<std::int32_t>(resumeDisplacement);
        std::memcpy(
            trampoline + displacedBytes + 1,
            &resumeRelative,
            sizeof(resumeRelative));
        std::array<std::uint8_t, displacedBytes> patch = {};
        patch.fill(0x90);
        patch[0] = 0xE9;
        const auto hookRelative = static_cast<std::int32_t>(hookDisplacement);
        std::memcpy(patch.data() + 1, &hookRelative, sizeof(hookRelative));

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
        gameModule_ = gameModule;
        trampoline_ = trampoline;
        original_ = reinterpret_cast<
            native::CombatHealthMutationFunction::Pointer>(trampoline);
        active_ = this;
        std::memcpy(target, patch.data(), patch.size());
        FlushInstructionCache(GetCurrentProcess(), target, patch.size());
        FlushInstructionCache(
            GetCurrentProcess(), trampoline, displacedBytes + 5);
        DWORD discarded = 0;
        VirtualProtect(target, patch.size(), previousProtection, &discarded);

        char detail[256] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "target=%p replacement=%p trampoline=%p function_rva=0x%08X",
            target,
            &CombatHealthMutationHook::Intercept,
            trampoline_,
            static_cast<unsigned int>(
                native::CombatHealthMutationFunction::AddressRva));
        diagnostics_.Event("CombatHealthMutationHookReady", detail);
        diagnostics_.Log(
            "Hook: native player/NPC combat-health mutation boundary installed.");
        return true;
#endif
    }

    void CombatHealthMutationHook::SetEventSink(
        EventSink sink,
        void* context) noexcept
    {
        if (sink == nullptr)
        {
            eventSink_.store(nullptr, std::memory_order_release);
            eventSinkContext_.store(nullptr, std::memory_order_release);
            return;
        }
        eventSinkContext_.store(context, std::memory_order_release);
        eventSink_.store(sink, std::memory_order_release);
    }

    bool CombatHealthMutationHook::Read(
        void* creature,
        float& currentHealth,
        float& maximumHealth) const noexcept
    {
        currentHealth = -1.0f;
        maximumHealth = -1.0f;
        if (creature == nullptr)
        {
            return false;
        }
        __try
        {
            const auto* const bytes = static_cast<const std::uint8_t*>(creature);
            currentHealth = *reinterpret_cast<const float*>(
                bytes + kHealthOffset);
            maximumHealth = *reinterpret_cast<const float*>(
                bytes + kMaximumHealthOffset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            currentHealth = -1.0f;
            maximumHealth = -1.0f;
        }
        return std::isfinite(currentHealth) &&
            std::isfinite(maximumHealth) && maximumHealth > 0.0f &&
            currentHealth >= 0.0f && currentHealth <= maximumHealth + 0.01f;
    }

    bool CombatHealthMutationHook::ApplyAuthoritative(
        void* creature,
        float currentHealth,
        float maximumHealth) noexcept
    {
        if (!IsInstalled() || creature == nullptr ||
            !std::isfinite(currentHealth) || !std::isfinite(maximumHealth) ||
            maximumHealth <= 0.0f || currentHealth < 0.0f ||
            currentHealth > maximumHealth + 0.01f)
        {
            return false;
        }
        float previous = 0.0f;
        float previousMaximum = 0.0f;
        if (!Read(creature, previous, previousMaximum))
        {
            return false;
        }
        bool applied = false;
        __try
        {
            auto* const bytes = static_cast<std::uint8_t*>(creature);
            *reinterpret_cast<float*>(bytes + kMaximumHealthOffset) =
                maximumHealth;
            original_(creature, currentHealth - previous, false);
            applied = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            applied = false;
        }
        return applied;
    }

    bool CombatHealthMutationHook::IsInstalled() const noexcept
    {
        return active_ == this && original_ != nullptr &&
            trampoline_ != nullptr && gameModule_ != nullptr;
    }

    std::uint64_t CombatHealthMutationHook::ReadThingUid(
        void* creature) noexcept
    {
        if (creature == nullptr)
        {
            return 0;
        }
        std::uint64_t uid = 0;
        __try
        {
            uid = *reinterpret_cast<const std::uint64_t*>(
                static_cast<const std::uint8_t*>(creature) + 0x14);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            uid = 0;
        }
        return uid;
    }

    void __fastcall CombatHealthMutationHook::Intercept(
        void* creature,
        void*,
        float delta,
        bool combatFlag)
    {
        CombatHealthMutationHook* const hook = active_;
        if (hook == nullptr || hook->original_ == nullptr)
        {
            return;
        }
        float previous = -1.0f;
        float previousMaximum = -1.0f;
        hook->Read(creature, previous, previousMaximum);
        hook->original_(creature, delta, combatFlag);
        float current = -1.0f;
        float maximum = -1.0f;
        if (!hook->Read(creature, current, maximum) ||
            (std::fabs(current - previous) < 0.0001f &&
                std::fabs(maximum - previousMaximum) < 0.0001f))
        {
            return;
        }
        const EventSink sink = hook->eventSink_.load(
            std::memory_order_acquire);
        if (sink == nullptr)
        {
            return;
        }
        CombatHealthMutationEvent event;
        event.creature = creature;
        event.thingUid = ReadThingUid(creature);
        event.previousHealth = previous;
        event.currentHealth = current;
        event.maximumHealth = maximum;
        event.requestedDelta = delta;
        event.observedAt = GetTickCount64();
        event.combatFlag = combatFlag;
        sink(
            hook->eventSinkContext_.load(std::memory_order_acquire),
            event);
        const unsigned int ordinal = hook->observedCount_.fetch_add(
            1, std::memory_order_acq_rel) + 1;
        if (ordinal <= 32)
        {
            char detail[320] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "ordinal=%u thing_uid=%016llX creature=%p previous=%.3f current=%.3f maximum=%.3f delta=%.3f flag=%s",
                ordinal,
                static_cast<unsigned long long>(event.thingUid),
                creature,
                previous,
                current,
                maximum,
                delta,
                combatFlag ? "true" : "false");
            hook->diagnostics_.Event("CombatHealthMutated", detail);
        }
    }
}
