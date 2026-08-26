#include "PlayerAttackAbilityHook.h"

#include "Game/Creature/Combat/CreatureCombatService.h"

#include <intrin.h>

#include <cstdint>
#include <cstdio>
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

        if (!hook_.Install(
                target,
                native::CreatureAbilitySubmissionFunction::ExpectedPrefix.data(),
                native::CreatureAbilitySubmissionFunction::ExpectedPrefix.size(),
                reinterpret_cast<void*>(&PlayerAttackAbilityHook::Intercept),
                native::CreatureAbilitySubmissionFunction::DisplacedBytes))
        {
            return false;
        }

        service_ = &service;
        original_ = reinterpret_cast<
            native::CreatureAbilitySubmissionFunction::Pointer>(hook_.Original());
        active_ = this;

        char detail[320] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "target=%p replacement=%p trampoline=%p function_rva=0x%08X player_attack_caller_return_rva=0x%08X",
            target,
            &PlayerAttackAbilityHook::Intercept,
            hook_.Original(),
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
        if (hook_.IsInstalled() && !hook_.Shutdown())
        {
            diagnostics_.Log(
                "Hook: player attack ability shutdown skipped because its target changed.");
            return;
        }
        if (hook_.ProtectionRestoreFailed())
        {
            diagnostics_.Log(
                "Hook: player attack ability bytes restored, but code protection restoration failed.");
        }
        service_ = nullptr;
        if (active_ == this) active_ = nullptr;
        original_ = nullptr;
        gameModule_ = nullptr;
        interceptedAttackCount_.store(0, std::memory_order_release);
        diagnostics_ = {};
    }

    bool PlayerAttackAbilityHook::IsInstalled() const noexcept
    {
        return active_ == this && service_ != nullptr && original_ != nullptr &&
            hook_.IsInstalled() && gameModule_ != nullptr;
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
