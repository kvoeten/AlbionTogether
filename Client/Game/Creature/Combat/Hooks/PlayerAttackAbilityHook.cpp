#include "PlayerAttackAbilityHook.h"

#include "Game/Creature/Combat/CreatureCombatService.h"

#include <intrin.h>

#include <array>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <cmath>

namespace fable::game::creature::combat
{
    PlayerAttackAbilityHook* PlayerAttackAbilityHook::active_ = nullptr;

    bool PlayerAttackAbilityHook::Install(
        HMODULE gameModule,
        CreatureCombatService& service,
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
            "Hook: native player attack ability routing is only supported by the x86 client.");
        return false;
#else
        if (active_ != nullptr && active_ != this)
        {
            diagnostics_.Log(
                "Hook: another native player attack ability router is already active.");
            return false;
        }

        std::uint8_t* target = nullptr;
        if (!native::CreatureAbilitySubmissionFunction::Resolve(
                gameModule,
                target))
        {
            diagnostics_.Log(
                "Hook: CThingCreature ability-submission definition validation failed; the executable ABI drifted.");
            return false;
        }

        constexpr std::size_t displacedBytes =
            native::CreatureAbilitySubmissionFunction::DisplacedBytes;
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
        const std::intptr_t trampolineDisplacement =
            reinterpret_cast<std::intptr_t>(target + displacedBytes) -
            (reinterpret_cast<std::intptr_t>(trampoline + displacedBytes) + 5);
        const std::intptr_t interceptorDisplacement =
            reinterpret_cast<std::intptr_t>(&PlayerAttackAbilityHook::Intercept) -
            (reinterpret_cast<std::intptr_t>(target) + 5);
        if (trampolineDisplacement < INT32_MIN ||
            trampolineDisplacement > INT32_MAX ||
            interceptorDisplacement < INT32_MIN ||
            interceptorDisplacement > INT32_MAX)
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
        FlushInstructionCache(
            GetCurrentProcess(),
            trampoline,
            displacedBytes + 5);

        std::array<std::uint8_t, displacedBytes> patch = {};
        patch.fill(0x90);
        patch[0] = 0xE9;
        const std::int32_t interceptorRelative =
            static_cast<std::int32_t>(interceptorDisplacement);
        std::memcpy(
            patch.data() + 1,
            &interceptorRelative,
            sizeof(interceptorRelative));

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

        service_ = &service;
        target_ = target;
        trampoline_ = trampoline;
        original_ = reinterpret_cast<
            native::CreatureAbilitySubmissionFunction::Pointer>(trampoline_);
        active_ = this;
        std::memcpy(target, patch.data(), patch.size());
        FlushInstructionCache(GetCurrentProcess(), target, patch.size());

        DWORD discardedProtection = 0;
        VirtualProtect(
            target,
            patch.size(),
            previousProtection,
            &discardedProtection);

        char detail[320] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "target=%p replacement=%p trampoline=%p function_rva=0x%08X player_attack_caller_return_rva=0x%08X",
            target,
            &PlayerAttackAbilityHook::Intercept,
            trampoline_,
            static_cast<unsigned int>(
                native::CreatureAbilitySubmissionFunction::AddressRva),
            static_cast<unsigned int>(
                native::CreatureAbilitySubmissionFunction::
                    PlayerAttackCallerReturnRva));
        diagnostics_.Log(
            "Hook: native player ATTACK ability router installed; no raw mouse input is observed.");
        diagnostics_.Event("PlayerAttackAbilityHookReady", detail);
        return true;
#endif
    }

    void PlayerAttackAbilityHook::Shutdown() noexcept
    {
        service_ = nullptr;
        if (active_ == this)
        {
            active_ = nullptr;
        }
        if (target_ != nullptr && trampoline_ != nullptr)
        {
            DWORD previousProtection = 0;
            if (VirtualProtect(
                    target_, originalBytes_.size(), PAGE_EXECUTE_READWRITE,
                    &previousProtection))
            {
                std::memcpy(target_, originalBytes_.data(), originalBytes_.size());
                FlushInstructionCache(GetCurrentProcess(), target_, originalBytes_.size());
                DWORD discarded = 0;
                VirtualProtect(
                    target_, originalBytes_.size(), previousProtection, &discarded);
            }
        }
        if (trampoline_ != nullptr)
        {
            VirtualFree(trampoline_, 0, MEM_RELEASE);
        }
        target_ = nullptr;
        trampoline_ = nullptr;
        original_ = nullptr;
        originalBytes_ = {};
        gameModule_ = nullptr;
        interceptedAttackCount_.store(0, std::memory_order_release);
        diagnostics_ = {};
    }

    bool PlayerAttackAbilityHook::IsInstalled() const noexcept
    {
        return active_ == this && service_ != nullptr && original_ != nullptr &&
            trampoline_ != nullptr && gameModule_ != nullptr;
    }

    unsigned int PlayerAttackAbilityHook::InterceptedAttackCount() const noexcept
    {
        return interceptedAttackCount_.load(std::memory_order_acquire);
    }

    bool PlayerAttackAbilityHook::SubmitReplicatedAbility(
        void* creature,
        unsigned int abilityId,
        float charge) noexcept
    {
        if (!IsInstalled() || creature == nullptr || abilityId == 0 ||
            abilityId >= 1'000'000 || !std::isfinite(charge) ||
            charge < -100.0f || charge > 100.0f)
        {
            return false;
        }
        bool submitted = false;
        __try
        {
            // Use the trampoline so observer-side playback does not re-enter
            // the capture hook and publish the replicated action again.
            original_(creature, abilityId, charge);
            submitted = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            submitted = false;
        }
        return submitted;
    }

    void __fastcall PlayerAttackAbilityHook::Intercept(
        void* creature,
        void*,
        unsigned int abilityId,
        float charge)
    {
        PlayerAttackAbilityHook* const hook = active_;
        if (hook == nullptr || hook->original_ == nullptr)
        {
            return;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(hook->gameModule_);
        const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        const bool playerAttackCommand = caller == base +
            native::CreatureAbilitySubmissionFunction::PlayerAttackCallerReturnRva;
        void* routedCreature = creature;
        const bool routed = playerAttackCommand && hook->service_ != nullptr &&
            hook->service_->ResolvePlayerAttackCreature(
                creature,
                routedCreature);

        if (hook->service_ != nullptr)
        {
            hook->service_->ObservePlayerAbility(
                creature,
                abilityId,
                charge,
                playerAttackCommand);
        }

        if (playerAttackCommand)
        {
            const unsigned int ordinal =
                hook->interceptedAttackCount_.fetch_add(
                    1,
                    std::memory_order_acq_rel) + 1;
            if (ordinal <= 16)
            {
                char detail[384] = {};
                std::snprintf(
                    detail,
                    std::size(detail),
                    "ordinal=%u caller=%p caller_rva=0x%08X source=%p routed=%p ability_id=%u charge=%.6f proxy_routed=%s",
                    ordinal,
                    reinterpret_cast<void*>(caller),
                    static_cast<unsigned int>(caller - base),
                    creature,
                    routedCreature,
                    abilityId,
                    charge,
                    routed ? "true" : "false");
                hook->diagnostics_.Event(
                    "PlayerAttackAbilityIntercepted",
                    detail);
            }
        }

        hook->original_(routedCreature, abilityId, charge);
    }
}
