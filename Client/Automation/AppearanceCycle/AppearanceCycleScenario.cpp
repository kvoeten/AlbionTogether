#include "AppearanceCycleScenario.h"

#include "Scripting/Runtime/Host/ScriptHost.h"

#include <Windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace fable::automation::appearance_cycle
{
    void AppearanceCycleScenario::Initialize(
        scripting::ScriptHost& scriptHost,
        const core::Diagnostics& diagnostics)
    {
        scriptHost_ = &scriptHost;
        diagnostics_ = diagnostics;
        initialized_.store(true, std::memory_order_release);
    }

    void AppearanceCycleScenario::Shutdown() noexcept
    {
        initialized_.store(false, std::memory_order_release);
        scriptHost_ = nullptr;
        diagnostics_ = {};
    }

    void AppearanceCycleScenario::ObserveScriptEvent(const char* state)
    {
        if (!initialized_.load(std::memory_order_acquire) || state == nullptr)
        {
            return;
        }

        if (std::strcmp(state, "AppearanceFormReady") == 0)
        {
            appearanceFormsReady_.fetch_add(1, std::memory_order_acq_rel);
        }
        else if (std::strcmp(state, "ProxyHostilityPolicyApplied") == 0)
        {
            hostilityPoliciesApplied_.fetch_add(1, std::memory_order_acq_rel);
        }
        else if (std::strcmp(state, "HeroShadowFollowBound") == 0)
        {
            heroShadowBindings_.fetch_add(1, std::memory_order_acq_rel);
        }
        else if (std::strcmp(state, "HeroShadowFollowUpdated") == 0)
        {
            heroShadowUpdated_.store(true, std::memory_order_release);
        }
        else if (std::strcmp(state, "CreaturePlayerCombatRouterBound") == 0)
        {
            combatRouterBindings_.fetch_add(1, std::memory_order_acq_rel);
        }
        else if (std::strcmp(state, "AppearanceHeroRestored") == 0)
        {
            appearanceRestored_.store(true, std::memory_order_release);
        }
        else if (std::strcmp(state, "PlayerFrameInputMovementObserved") == 0)
        {
            playerFrameInputMovementObserved_.store(
                true,
                std::memory_order_release);
        }
    }

    void AppearanceCycleScenario::Tick(const CharacterSnapshot& character)
    {
        constexpr int kBowerstoneNorthRegionIndex = 32;
        constexpr float kAdultCombatHealthMinimum = 100.0f;
        constexpr std::uint64_t kFormDwellMilliseconds = 3'000;
        constexpr std::uint64_t kMovementTimeoutMilliseconds = 8'000;
        constexpr std::uint64_t kRestoreSoakMilliseconds = 5'000;

        if (!initialized_.load(std::memory_order_acquire) ||
            scriptHost_ == nullptr)
        {
            return;
        }

        const unsigned int stage = stage_.load(std::memory_order_acquire);
        if (stage >= 100)
        {
            return;
        }

        const std::uint64_t now = GetTickCount64();
        if (stage == 0)
        {
            if (character.regionIndex != kBowerstoneNorthRegionIndex ||
                character.combatHealthMaximum < kAdultCombatHealthMinimum ||
                character.progressionHealthValue <= 0)
            {
                char detail[256] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "adult-town fixture rejected: region_index=%d combat_health_maximum=%.3f progression_health=%d",
                    character.regionIndex,
                    character.combatHealthMaximum,
                    character.progressionHealthValue);
                FinishWithFailure(detail);
                return;
            }

            char fixtureDetail[256] = {};
            std::snprintf(
                fixtureDetail,
                sizeof(fixtureDetail),
                "location=Bowerstone North region_index=%d combat_health=%.3f combat_health_maximum=%.3f progression_health=%d",
                character.regionIndex,
                character.combatHealth,
                character.combatHealthMaximum,
                character.progressionHealthValue);
            diagnostics_.Event("AdultTownFixtureReady", fixtureDetail);

            baseline_ = character;
            appearanceFormsReady_.store(0, std::memory_order_release);
            hostilityPoliciesApplied_.store(0, std::memory_order_release);
            heroShadowBindings_.store(0, std::memory_order_release);
            combatRouterBindings_.store(0, std::memory_order_release);
            heroShadowUpdated_.store(false, std::memory_order_release);
            appearanceRestored_.store(false, std::memory_order_release);
            playerFrameInputMovementObserved_.store(
                false,
                std::memory_order_release);
            scriptHost_->DispatchKeyPressed('1', false);
            stageStartedAt_.store(now, std::memory_order_release);
            stage_.store(1, std::memory_order_release);
            return;
        }

        const char* failure = nullptr;
        if (!HeroStateMatchesBaseline(character, failure))
        {
            FinishWithFailure(
                failure != nullptr
                    ? failure
                    : "scripted appearance Hero baseline mismatch");
            return;
        }

        const std::uint64_t startedAt =
            stageStartedAt_.load(std::memory_order_acquire);
        if (stage == 1)
        {
            if (now - startedAt >= kMovementTimeoutMilliseconds)
            {
                FinishWithFailure(
                    "AngelScript guard did not produce verified player-frame input routing and native movement evidence before the timeout");
                return;
            }
            if (appearanceFormsReady_.load(std::memory_order_acquire) < 1 ||
                now - startedAt < kFormDwellMilliseconds ||
                !playerFrameInputMovementObserved_.load(
                    std::memory_order_acquire))
            {
                return;
            }

            scriptHost_->DispatchKeyPressed('1', false);
            stageStartedAt_.store(now, std::memory_order_release);
            stage_.store(2, std::memory_order_release);
            return;
        }

        if (stage == 2 || stage == 3)
        {
            if (appearanceFormsReady_.load(std::memory_order_acquire) < stage ||
                now - startedAt < kFormDwellMilliseconds)
            {
                return;
            }

            scriptHost_->DispatchKeyPressed('1', stage == 3);
            stageStartedAt_.store(now, std::memory_order_release);
            stage_.store(stage + 1, std::memory_order_release);
            return;
        }

        if (stage == 4 &&
            appearanceRestored_.load(std::memory_order_acquire) &&
            now - startedAt >= kRestoreSoakMilliseconds)
        {
            if (appearanceFormsReady_.load(std::memory_order_acquire) != 3 ||
                hostilityPoliciesApplied_.load(std::memory_order_acquire) != 3 ||
                heroShadowBindings_.load(std::memory_order_acquire) != 3 ||
                combatRouterBindings_.load(std::memory_order_acquire) != 3 ||
                !heroShadowUpdated_.load(std::memory_order_acquire))
            {
                char detail[320] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "proxy lifecycle coverage mismatch: forms=%u hostility=%u shadow_bindings=%u combat_bindings=%u shadow_updated=%s",
                    appearanceFormsReady_.load(std::memory_order_acquire),
                    hostilityPoliciesApplied_.load(std::memory_order_acquire),
                    heroShadowBindings_.load(std::memory_order_acquire),
                    combatRouterBindings_.load(std::memory_order_acquire),
                    heroShadowUpdated_.load(std::memory_order_acquire) ? "true" : "false");
                FinishWithFailure(detail);
                return;
            }
            diagnostics_.Event(
                "AppearanceCyclePassed",
                "AngelScript cycled guard, villager, and hobbe; every form applied friendly decision ownership, native Hero-frame locomotion, player-owned facing, hidden-Hero shadow follow, and NPC combat-router binding; Hero identity, region, and combat health stayed stable");
            stage_.store(100, std::memory_order_release);
        }
    }

    bool AppearanceCycleScenario::IsComplete() const noexcept
    {
        return stage_.load(std::memory_order_acquire) >= 100;
    }

    bool AppearanceCycleScenario::HeroStateMatchesBaseline(
        const CharacterSnapshot& character,
        const char*& failure) const
    {
        constexpr float kHealthTolerance = 0.01f;
        if (character.creature != baseline_.creature ||
            character.creatureVtable != baseline_.creatureVtable)
        {
            failure = "Hero pointer or CThingPlayerCreature vtable changed";
            return false;
        }
        if (character.regionIndex != baseline_.regionIndex)
        {
            failure = "Hero region changed";
            return false;
        }
        if (std::fabs(character.combatHealth - baseline_.combatHealth) >
            kHealthTolerance)
        {
            failure = "Hero combat health changed";
            return false;
        }

        failure = nullptr;
        return true;
    }

    void AppearanceCycleScenario::FinishWithFailure(const char* detail)
    {
        diagnostics_.Event(
            "ClientFailed",
            detail != nullptr ? detail : "appearance-cycle-failed");
        stage_.store(100, std::memory_order_release);
    }
}
