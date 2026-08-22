#include "HeroWillAbilityHook.h"

#include "Game/Entity/Presence/TransientEntityCreationScope.h"
#include "Game/HeroPawn/Abilities/HeroWillAbilityService.h"

#include <array>
#include <climits>
#include <cstdio>
#include <cstring>

namespace
{
    thread_local unsigned int g_authoritativeEligibilityDepth = 0;
    thread_local unsigned int g_authoritativeEligibilityBypasses = 0;
    thread_local unsigned int g_turncoatSubmissionDepth = 0;
    thread_local unsigned int g_turncoatStateSubmissions = 0;

    class AuthoritativeEligibilityScope final
    {
    public:
        AuthoritativeEligibilityScope() noexcept
        {
            ++g_authoritativeEligibilityDepth;
        }

        ~AuthoritativeEligibilityScope()
        {
            --g_authoritativeEligibilityDepth;
        }
    };

    class TurncoatSubmissionScope final
    {
    public:
        TurncoatSubmissionScope() noexcept
        {
            ++g_turncoatSubmissionDepth;
        }

        ~TurncoatSubmissionScope()
        {
            --g_turncoatSubmissionDepth;
        }
    };

    int ReadTurncoatState(void* state) noexcept
    {
        if (state == nullptr)
        {
            return 0;
        }
        int value = 0;
        __try
        {
            value = *reinterpret_cast<const int*>(
                static_cast<const std::uint8_t*>(state) + 0x20);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            value = 0;
        }
        return value;
    }
}

namespace fable::game::hero_pawn::abilities::hooks
{
    HeroWillAbilityHook* HeroWillAbilityHook::active_ = nullptr;

    bool HeroWillAbilityHook::Detour::Prepare(
        std::uint8_t* targetAddress,
        void*,
        std::size_t bytes) noexcept
    {
        Reset();
        if (targetAddress == nullptr || bytes < 5 || bytes > original.size())
        {
            return false;
        }
        auto* const allocated = static_cast<std::uint8_t*>(VirtualAlloc(
            nullptr,
            bytes + 5,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_EXECUTE_READWRITE));
        if (allocated == nullptr)
        {
            return false;
        }
        std::memcpy(original.data(), targetAddress, bytes);
        std::memcpy(allocated, targetAddress, bytes);
        allocated[bytes] = 0xE9;
        const std::intptr_t displacement =
            reinterpret_cast<std::intptr_t>(targetAddress + bytes) -
            (reinterpret_cast<std::intptr_t>(allocated + bytes) + 5);
        if (displacement < INT32_MIN || displacement > INT32_MAX)
        {
            VirtualFree(allocated, 0, MEM_RELEASE);
            return false;
        }
        const auto relative = static_cast<std::int32_t>(displacement);
        std::memcpy(allocated + bytes + 1, &relative, sizeof(relative));
        FlushInstructionCache(GetCurrentProcess(), allocated, bytes + 5);
        target = targetAddress;
        trampoline = allocated;
        displacedBytes = bytes;
        return true;
    }

    bool HeroWillAbilityHook::Detour::Activate(void* replacement) noexcept
    {
        if (target == nullptr || trampoline == nullptr || replacement == nullptr)
        {
            return false;
        }
        const std::size_t displaced = displacedBytes;
        if (displaced < 5 || displaced > original.size())
        {
            return false;
        }
        const std::intptr_t displacement =
            reinterpret_cast<std::intptr_t>(replacement) -
            (reinterpret_cast<std::intptr_t>(target) + 5);
        if (displacement < INT32_MIN || displacement > INT32_MAX)
        {
            return false;
        }
        std::array<
            std::uint8_t,
            native::HeroWillAbilityFunctions::TurncoatStateDisplacedBytes>
                patch = {};
        patch.fill(0x90);
        patch[0] = 0xE9;
        const auto relative = static_cast<std::int32_t>(displacement);
        std::memcpy(patch.data() + 1, &relative, sizeof(relative));
        DWORD previousProtection = 0;
        if (!VirtualProtect(
                target,
                displaced,
                PAGE_EXECUTE_READWRITE,
                &previousProtection))
        {
            return false;
        }
        std::memcpy(target, patch.data(), displaced);
        FlushInstructionCache(GetCurrentProcess(), target, displaced);
        DWORD discardedProtection = 0;
        VirtualProtect(
            target, displaced, previousProtection, &discardedProtection);
        active = true;
        return true;
    }

    void HeroWillAbilityHook::Detour::Reset() noexcept
    {
        if (active && target != nullptr)
        {
            DWORD previousProtection = 0;
            if (VirtualProtect(
                    target,
                    displacedBytes,
                    PAGE_EXECUTE_READWRITE,
                    &previousProtection))
            {
                std::memcpy(target, original.data(), displacedBytes);
                FlushInstructionCache(
                    GetCurrentProcess(), target, displacedBytes);
                DWORD discardedProtection = 0;
                VirtualProtect(
                    target,
                    displacedBytes,
                    previousProtection,
                    &discardedProtection);
            }
        }
        if (trampoline != nullptr)
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
        }
        target = nullptr;
        trampoline = nullptr;
        original = {};
        displacedBytes = 0;
        active = false;
    }

    bool HeroWillAbilityHook::Install(
        HMODULE gameModule,
        HeroWillAbilityService& service,
        const core::Diagnostics& diagnostics)
    {
        if (IsInstalled())
        {
            return true;
        }
        if (active_ != nullptr && active_ != this)
        {
            return false;
        }
        std::uint8_t* use = nullptr;
        std::uint8_t* toggle = nullptr;
        std::uint8_t* cancel = nullptr;
        std::uint8_t* eligibility = nullptr;
        std::uint8_t* turncoatState = nullptr;
        if (!native::HeroWillAbilityFunctions::ResolveCommand(
                gameModule, HeroAbilityCommand::Use, use) ||
            !native::HeroWillAbilityFunctions::ResolveCommand(
                gameModule, HeroAbilityCommand::Toggle, toggle) ||
            !native::HeroWillAbilityFunctions::ResolveCommand(
                gameModule, HeroAbilityCommand::Cancel, cancel) ||
            !native::HeroWillAbilityFunctions::ResolveEligibility(
                gameModule, eligibility) ||
            !native::HeroWillAbilityFunctions::ResolveTurncoatState(
                gameModule, turncoatState) ||
            !use_.Prepare(use, reinterpret_cast<void*>(&InterceptUse)) ||
            !toggle_.Prepare(
                toggle, reinterpret_cast<void*>(&InterceptToggle)) ||
            !cancel_.Prepare(
                cancel, reinterpret_cast<void*>(&InterceptCancel)) ||
            !eligibility_.Prepare(
                eligibility,
                reinterpret_cast<void*>(&InterceptEligibility)) ||
            !turncoatState_.Prepare(
                turncoatState,
                reinterpret_cast<void*>(&InterceptTurncoatState),
                native::HeroWillAbilityFunctions::
                    TurncoatStateDisplacedBytes))
        {
            char detail[320] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "use=%p prepared=%s toggle=%p prepared=%s cancel=%p prepared=%s eligibility=%p prepared=%s turncoat_state=%p prepared=%s",
                use,
                use_.trampoline != nullptr ? "true" : "false",
                toggle,
                toggle_.trampoline != nullptr ? "true" : "false",
                cancel,
                cancel_.trampoline != nullptr ? "true" : "false",
                eligibility,
                eligibility_.trampoline != nullptr ? "true" : "false",
                turncoatState,
                turncoatState_.trampoline != nullptr ? "true" : "false");
            diagnostics.Event("HeroWillAbilityHookRejected", detail);
            use_.Reset();
            toggle_.Reset();
            cancel_.Reset();
            eligibility_.Reset();
            turncoatState_.Reset();
            return false;
        }

        service_ = &service;
        gameModule_ = gameModule;
        diagnostics_ = diagnostics;
        originalUse_ = reinterpret_cast<
            native::HeroWillAbilityFunctions::CommandPointer>(use_.trampoline);
        originalToggle_ = reinterpret_cast<
            native::HeroWillAbilityFunctions::CommandPointer>(
                toggle_.trampoline);
        originalCancel_ = reinterpret_cast<
            native::HeroWillAbilityFunctions::CommandPointer>(
                cancel_.trampoline);
        originalEligibility_ = reinterpret_cast<
            native::HeroWillAbilityFunctions::CommandPointer>(
                eligibility_.trampoline);
        originalTurncoatState_ = reinterpret_cast<
            native::HeroWillAbilityFunctions::TurncoatStatePointer>(
                turncoatState_.trampoline);
        active_ = this;
        if (!use_.Activate(reinterpret_cast<void*>(&InterceptUse)) ||
            !toggle_.Activate(reinterpret_cast<void*>(&InterceptToggle)) ||
            !cancel_.Activate(reinterpret_cast<void*>(&InterceptCancel)) ||
            !eligibility_.Activate(
                reinterpret_cast<void*>(&InterceptEligibility)) ||
            !turncoatState_.Activate(
                reinterpret_cast<void*>(&InterceptTurncoatState)))
        {
            active_ = nullptr;
            service_ = nullptr;
            originalUse_ = nullptr;
            originalToggle_ = nullptr;
            originalCancel_ = nullptr;
            originalEligibility_ = nullptr;
            originalTurncoatState_ = nullptr;
            use_.Reset();
            toggle_.Reset();
            cancel_.Reset();
            eligibility_.Reset();
            turncoatState_.Reset();
            return false;
        }

        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "use=%p toggle=%p cancel=%p eligibility=%p turncoat_state=%p controller_vtable_rva=0x%08X",
            use,
            toggle,
            cancel,
            eligibility,
            turncoatState,
            static_cast<unsigned int>(
                native::HeroWillAbilityFunctions::ControllerVtableRva));
        diagnostics_.Event("HeroWillAbilityHookReady", detail);
        return true;
    }

    bool HeroWillAbilityHook::IsInstalled() const noexcept
    {
        return active_ == this && service_ != nullptr && gameModule_ != nullptr &&
            originalUse_ != nullptr && originalToggle_ != nullptr &&
            originalCancel_ != nullptr && use_.active && toggle_.active &&
            cancel_.active && originalEligibility_ != nullptr &&
            eligibility_.active && originalTurncoatState_ != nullptr &&
            turncoatState_.active;
    }

    bool HeroWillAbilityHook::SubmitReplicated(
        void* component,
        HeroAbility ability,
        HeroAbilityCommand command) noexcept
    {
        const AuthoritativeEligibilityScope authoritativeEligibility;
        return Submit(component, ability, command);
    }

    bool HeroWillAbilityHook::SubmitLocal(
        void* component,
        HeroAbility ability,
        HeroAbilityCommand command) noexcept
    {
        return Submit(component, ability, command);
    }

    bool HeroWillAbilityHook::Submit(
        void* component,
        HeroAbility ability,
        HeroAbilityCommand command) noexcept
    {
        if (!IsInstalled() || component == nullptr || !IsValid(ability) ||
            !IsValid(command))
        {
            return false;
        }
        if (!IsMultiplayerSupported(ability))
        {
            diagnostics_.Event(
                "HeroWillAbilityBlocked",
                "ability=2 name=Time reason=process-local-world-time");
            return false;
        }
        if (command == HeroAbilityCommand::Use)
        {
            const entity::presence::TransientEntityCreationScope
                transientEntityCreation;
            const bool traceForcePush = ability == HeroAbility::ForcePush;
            void* const owner = traceForcePush
                ? native::HeroWillAbilityFunctions::ReadOwner(component)
                : nullptr;
            void* const creature = traceForcePush
                ? native::HeroWillAbilityFunctions::ReadCreature(component)
                : nullptr;
            const bool powerBefore = traceForcePush &&
                entity::native::ThingComponentAccess::Has(
                    creature,
                    entity::native::ThingComponentType::ForcePushPower);
            const bool abilityInventoryPresent = traceForcePush &&
                entity::native::ThingComponentAccess::Has(
                    owner,
                    entity::native::ThingComponentType::HeroAbilityInventory);
            g_authoritativeEligibilityBypasses = 0;
            g_turncoatStateSubmissions = 0;
            bool accepted = false;
            if (ability == HeroAbility::Turncoat)
            {
                const TurncoatSubmissionScope turncoat;
                accepted = InvokeCommandSafely(
                    originalUse_, component, ability);
                accepted = accepted || g_turncoatStateSubmissions != 0;
            }
            else
            {
                accepted = InvokeCommandSafely(
                    originalUse_, component, ability);
            }
            if (traceForcePush)
            {
                const bool powerAfter =
                    entity::native::ThingComponentAccess::Has(
                        creature,
                        entity::native::ThingComponentType::ForcePushPower);
                char detail[256] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "component=%p owner=%p creature=%p eligibility_bypasses=%u power_before=%s power_after=%s ability_inventory=%s accepted=%s",
                    component,
                    owner,
                    creature,
                    g_authoritativeEligibilityBypasses,
                    powerBefore ? "true" : "false",
                    powerAfter ? "true" : "false",
                    abilityInventoryPresent ? "true" : "false",
                    accepted ? "true" : "false");
                diagnostics_.Event("HeroWillForcePushReplayTrace", detail);
            }
            return accepted;
        }
        const auto function = command == HeroAbilityCommand::Toggle
            ? originalToggle_
            : originalCancel_;
        const bool actionWasActive =
            command == HeroAbilityCommand::Cancel &&
            native::HeroWillAbilityFunctions::HasActiveAction(
                component, ability);
        const bool accepted = InvokeCommandSafely(
            function, component, ability);
        // Several held/charged Will cancel handlers use a void-like return
        // contract. Presence of the matching live action before the request
        // is the authoritative acceptance predicate for those commands.
        return accepted || actionWasActive;
    }

    bool __fastcall HeroWillAbilityHook::InterceptUse(
        void* component,
        void*,
        HeroAbility ability)
    {
        return Intercept(component, ability, HeroAbilityCommand::Use);
    }

    bool __fastcall HeroWillAbilityHook::InterceptToggle(
        void* component,
        void*,
        HeroAbility ability)
    {
        return Intercept(component, ability, HeroAbilityCommand::Toggle);
    }

    bool __fastcall HeroWillAbilityHook::InterceptCancel(
        void* component,
        void*,
        HeroAbility ability)
    {
        return Intercept(component, ability, HeroAbilityCommand::Cancel);
    }

    bool __fastcall HeroWillAbilityHook::InterceptEligibility(
        void* component,
        void*,
        HeroAbility ability)
    {
        HeroWillAbilityHook* const hook = active_;
        if (hook == nullptr || hook->originalEligibility_ == nullptr ||
            component == nullptr)
        {
            return false;
        }
        if (g_authoritativeEligibilityDepth != 0)
        {
            ++g_authoritativeEligibilityBypasses;
            return true;
        }
        return InvokeCommandSafely(
            hook->originalEligibility_, component, ability);
    }

    void __fastcall HeroWillAbilityHook::InterceptTurncoatState(
        void* state,
        void*,
        float amount)
    {
        HeroWillAbilityHook* const hook = active_;
        if (hook == nullptr || hook->originalTurncoatState_ == nullptr ||
            state == nullptr)
        {
            return;
        }
        const bool observedSubmission = g_turncoatSubmissionDepth != 0;
        const int before = observedSubmission ? ReadTurncoatState(state) : 0;
        const bool completed = InvokeTurncoatStateSafely(
            hook->originalTurncoatState_, state, amount);
        if (!observedSubmission)
        {
            return;
        }
        const int after = ReadTurncoatState(state);
        if (completed)
        {
            ++g_turncoatStateSubmissions;
        }
        char detail[192] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "state=%p amount=%.3f before=%d after=%d completed=%s",
            state,
            amount,
            before,
            after,
            completed ? "true" : "false");
        hook->diagnostics_.Event("HeroWillTurncoatStateSubmitted", detail);
    }

    bool HeroWillAbilityHook::Intercept(
        void* component,
        HeroAbility ability,
        HeroAbilityCommand command) noexcept
    {
        HeroWillAbilityHook* const hook = active_;
        if (hook == nullptr || component == nullptr)
        {
            return false;
        }
        if (!IsMultiplayerSupported(ability))
        {
            hook->diagnostics_.Event(
                "HeroWillAbilityBlocked",
                "ability=2 name=Time reason=process-local-world-time");
            return false;
        }
        const auto function = command == HeroAbilityCommand::Use
            ? hook->originalUse_
            : command == HeroAbilityCommand::Toggle
                ? hook->originalToggle_
                : hook->originalCancel_;
        g_turncoatStateSubmissions = 0;
        const entity::presence::TransientEntityCreationScope
            transientEntityCreation(
                command == HeroAbilityCommand::Use);
        bool accepted = false;
        if (command == HeroAbilityCommand::Use &&
            ability == HeroAbility::Turncoat)
        {
            const TurncoatSubmissionScope turncoat;
            accepted = InvokeCommandSafely(function, component, ability);
            accepted = accepted || g_turncoatStateSubmissions != 0;
        }
        else
        {
            const bool actionWasActive =
                command == HeroAbilityCommand::Cancel &&
                native::HeroWillAbilityFunctions::HasActiveAction(
                    component, ability);
            accepted = InvokeCommandSafely(function, component, ability) ||
                actionWasActive;
        }
        if (accepted && hook->service_ != nullptr)
        {
            hook->service_->Observe(component, ability, command);
        }
        return accepted;
    }

    bool HeroWillAbilityHook::InvokeCommandSafely(
        native::HeroWillAbilityFunctions::CommandPointer command,
        void* component,
        HeroAbility ability) noexcept
    {
        if (command == nullptr || component == nullptr)
        {
            return false;
        }
        bool accepted = false;
        __try
        {
            accepted = command(component, ability);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            accepted = false;
        }
        return accepted;
    }

    bool HeroWillAbilityHook::InvokeTurncoatStateSafely(
        native::HeroWillAbilityFunctions::TurncoatStatePointer command,
        void* state,
        float amount) noexcept
    {
        if (command == nullptr || state == nullptr)
        {
            return false;
        }
        bool completed = false;
        __try
        {
            command(state, amount);
            completed = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            completed = false;
        }
        return completed;
    }
}
