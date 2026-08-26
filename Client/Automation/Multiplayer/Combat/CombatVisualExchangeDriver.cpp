#include "CombatVisualExchangeDriver.h"

#include "Game/Creature/Combat/CreatureCombatService.h"
#include "Game/Creature/Combat/Native/HeroTargetingComponent.h"
#include "Game/Creature/Equipment/CreatureWeaponFamily.h"
#include "Game/Creature/CreatureService.h"
#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"
#include "Game/Entity/Native/ThingComponentAccess.h"
#include "Game/HeroPawn/Equipment/HeroEquipmentState.h"
#include "Game/HeroPawn/Equipment/Native/HeroWeaponComponent.h"

#include <Windows.h>

#include <cmath>
#include <cstdio>

namespace
{
    constexpr char TargetScriptName[] =
        "SCRIPT_NAME_ALBION_TOGETHER_COMBAT_TARGET";
    constexpr char RemotePlayerScriptName[] =
        "SCRIPT_NAME_ALBION_TOGETHER_REMOTE_PLAYER";
    constexpr char ArenaMap[] = "FrescoDome";
    constexpr unsigned int HeroMeleeAttackAbility = 1101;
    constexpr std::uint64_t FixtureSettleMilliseconds = 20'000;
    constexpr std::uint64_t GuestOpeningOffsetMilliseconds = 4'000;
    constexpr std::uint64_t RetryMilliseconds = 250;
    constexpr std::uint64_t AttackSpacingMilliseconds = 1'500;
    constexpr unsigned int MaximumFailedAttempts = 80;
    constexpr unsigned int MaximumTargetKillAttacks = 48;
    constexpr std::uint64_t TargetKillAttackSpacingMilliseconds = 900;
    constexpr std::uint64_t TargetKillTimeoutMilliseconds = 45'000;
    constexpr std::uint64_t GuestPvpCompletionGraceMilliseconds = 8'000;
    constexpr float ExpectedTargetMaximumHealth = 60.0f;
    constexpr float TerminalHealthEpsilon = 0.001f;

    bool AssignHeroTarget(
        HMODULE gameModule,
        void* heroThing,
        void* targetThing) noexcept
    {
        void* const targeting =
            fable::game::entity::native::ThingComponentAccess::Find(
                heroThing,
                fable::game::entity::native::ThingComponentType::Targeting);
        return targeting != nullptr &&
            fable::game::creature::combat::native::HeroTargetingComponent::
                AssignSelectedTarget(gameModule, targeting, targetThing);
    }

    bool HeroTargets(
        HMODULE gameModule,
        void* heroThing,
        void* targetThing) noexcept
    {
        void* const targeting =
            fable::game::entity::native::ThingComponentAccess::Find(
                heroThing,
                fable::game::entity::native::ThingComponentType::Targeting);
        fable::game::creature::combat::native::HeroTargetingSnapshot snapshot;
        return targeting != nullptr && targetThing != nullptr &&
            fable::game::creature::combat::native::HeroTargetingComponent::
                ReadTargets(gameModule, targeting, snapshot) &&
            (snapshot.selected == targetThing ||
             snapshot.candidatePrimary == targetThing ||
             snapshot.candidateSecondary == targetThing);
    }

    bool ClearHeroTargets(HMODULE gameModule, void* heroThing) noexcept
    {
        void* const targeting =
            fable::game::entity::native::ThingComponentAccess::Find(
                heroThing,
                fable::game::entity::native::ThingComponentType::Targeting);
        return targeting != nullptr &&
            fable::game::creature::combat::native::HeroTargetingComponent::
                ClearTargets(gameModule, targeting);
    }
}

namespace fable::automation::multiplayer::combat
{
    void CombatVisualExchangeDriver::Initialize(
        bool enabled,
        bool hostRole,
        game::EntityService& entities,
        game::CreatureService& creatures,
        game::creature::combat::CreatureCombatService& combat,
        const core::Diagnostics& diagnostics) noexcept
    {
        Shutdown();
        entities_ = &entities;
        creatures_ = &creatures;
        combat_ = &combat;
        diagnostics_ = diagnostics;
        hostRole_ = hostRole;
        enabled_ = enabled;
        if (enabled_)
        {
            diagnostics_.Event(
                "MultiplayerCombatVisualExchangeArmed",
                hostRole_
                    ? "role=host sequence=hero-enemy,enemy-both-heroes,pvp-guest"
                    : "role=guest sequence=hero-enemy,pvp-host");
        }
    }

    void CombatVisualExchangeDriver::Tick(bool remotePresentationReady)
    {
        if (!enabled_ || completed_ || !remotePresentationReady ||
            entities_ == nullptr || combat_ == nullptr)
        {
            return;
        }

        game::Entity* const hero = entities_->GetHero();
        game::Entity* const remote =
            entities_->FindByScriptName(RemotePlayerScriptName);
        if (target_ == nullptr)
        {
            target_ = entities_->FindByScriptName(TargetScriptName);
            if (target_ != nullptr)
            {
                targetUid_ = target_->GetUid();
            }
        }
        game::Entity* const target = target_;
        const bool targetReady = target != nullptr &&
            (visualSequenceComplete_ ||
                (target->IsValid() &&
                 target->GetCurrentMapName() == ArenaMap));
        const bool ready = hero != nullptr && hero->IsValid() &&
            remote != nullptr && remote->IsValid() &&
            targetReady &&
            hero->GetCurrentMapName() == ArenaMap &&
            remote->GetCurrentMapName() == ArenaMap;
        if (!ready)
        {
            if (remote != nullptr)
            {
                remote->Release();
            }
            if (hero != nullptr)
            {
                hero->Release();
            }
            return;
        }

        // Keep the fixture neutral to ambient AI. Direct semantic attacks still
        // exercise hit reactions and retaliation, while a hostile Hobbe makes
        // ambient guards enter the exchange and opens Fable's crime dialogue,
        // obscuring the two combatants the acceptance test is meant to prove.
        target->SetFriendsWithEverything(true);
        target->SetAttackable(true);
        target->SetDamageable(true);

        void* const heroThing = entities_->ResolveNative(hero->NativeHandle());
        void* const remoteThing =
            entities_->ResolveNative(remote->NativeHandle());
        void* const targetThing =
            entities_->ResolveNative(target->NativeHandle());
        const std::uint64_t now = GetTickCount64();
        if (fixtureReadyAt_ == 0)
        {
            fixtureReadyAt_ = now;
            nextActionAt_ = fixtureReadyAt_ +
                FixtureSettleMilliseconds +
                (hostRole_ ? 0 : GuestOpeningOffsetMilliseconds);
            diagnostics_.Event(
                "MultiplayerCombatVisualFixtureReady",
                hostRole_
                    ? "role=host map=FrescoDome target=neutral-restrained-hobbe"
                    : "role=guest map=FrescoDome target=neutral-restrained-hobbe");
        }

        using game::creature::equipment::CreatureWeaponFamily;
        game::hero_pawn::equipment::HeroEquipmentState equipment;
        const bool equipmentRead =
            game::hero_pawn::equipment::native::HeroWeaponComponent::Capture(
                heroThing, equipment);
        if (!meleeRequested_ && equipmentRead && equipment.IsSane() &&
            equipment.meleeDefinitionIndex > 0)
        {
            meleeRequested_ = game::hero_pawn::equipment::native::
                HeroWeaponComponent::RequestActiveFamily(
                    entities_->Interface(),
                    hero->NativeHandle(),
                    CreatureWeaponFamily::Melee);
        }
        meleeReady_ = meleeReady_ || (equipmentRead &&
            equipment.activeFamily == CreatureWeaponFamily::Melee);

        bool submitted = false;
        if (meleeReady_ && heroThing != nullptr && remoteThing != nullptr &&
            targetThing != nullptr && now >= nextActionAt_)
        {
            if (hostRole_)
            {
                switch (step_)
                {
                case 0:
                case 1:
                    submitted = SubmitHeroAttack(
                        heroThing, targetThing, "enemy", step_ + 1);
                    if (submitted)
                    {
                        Advance(
                            now,
                            step_ == 0
                                ? AttackSpacingMilliseconds
                                : 12'000);
                    }
                    break;
                case 2:
                case 4:
                    submitted = SubmitEnemyAttack(
                        targetThing,
                        heroThing,
                        "host-local-hero",
                        step_ - 1);
                    if (submitted)
                    {
                        Advance(now, 2'000);
                    }
                    break;
                case 3:
                case 5:
                    submitted = SubmitEnemyAttack(
                        targetThing,
                        remoteThing,
                        "guest-remote-hero",
                        step_ - 1);
                    if (submitted)
                    {
                        Advance(now, step_ == 3 ? 2'000 : 4'000);
                    }
                    break;
                case 6:
                case 7:
                    submitted = SubmitPlayerTargetedHeroAttack(
                        heroThing,
                        remoteThing,
                        "guest-remote-hero",
                        step_ - 5,
                        now);
                    if (submitted)
                    {
                        Advance(
                            now,
                            step_ == 6
                                ? AttackSpacingMilliseconds
                                : GuestPvpCompletionGraceMilliseconds);
                    }
                    break;
                case 8:
                    CompleteVisualSequence();
                    submitted = ExecuteTargetKill(
                        target, heroThing, targetThing, now);
                    break;
                default:
                    completed_ = true;
                    break;
                }
            }
            else
            {
                switch (step_)
                {
                case 0:
                case 1:
                    submitted = SubmitHeroAttack(
                        heroThing, targetThing, "enemy", step_ + 1);
                    if (submitted)
                    {
                        Advance(
                            now,
                            step_ == 0
                                ? AttackSpacingMilliseconds
                                : 22'000);
                    }
                    break;
                case 2:
                case 3:
                    submitted = SubmitPlayerTargetedHeroAttack(
                        heroThing,
                        remoteThing,
                        "host-remote-hero",
                        step_ - 1,
                        now);
                    if (submitted)
                    {
                        Advance(now, AttackSpacingMilliseconds);
                    }
                    break;
                default:
                    CompleteVisualSequence();
                    submitted = ObserveTargetTerminal(target);
                    break;
                }
            }

            if (!submitted && !completed_ && !visualSequenceComplete_)
            {
                ++failedAttempts_;
                nextActionAt_ = now + RetryMilliseconds;
                if (failedAttempts_ >= MaximumFailedAttempts)
                {
                    char detail[192] = {};
                    std::snprintf(
                        detail,
                        sizeof(detail),
                        "role=%s step=%u attempts=%u",
                        hostRole_ ? "host" : "guest",
                        step_,
                        failedAttempts_);
                    diagnostics_.Event(
                        "MultiplayerCombatVisualExchangeFailed", detail);
                    diagnostics_.Event(
                        "ClientFailed",
                        "multiplayer-combat-visual-exchange-failed");
                    completed_ = true;
                }
            }
        }

        remote->Release();
        hero->Release();
    }

    bool CombatVisualExchangeDriver::SubmitHeroAttack(
        void* heroThing,
        void* targetThing,
        const char* targetRole,
        unsigned int ordinal,
        bool assignTarget) noexcept
    {
        if ((assignTarget && !AssignHeroTarget(
                entities_->GameModule(), heroThing, targetThing)) ||
            !combat_->SubmitReplicatedAbility(
                heroThing, HeroMeleeAttackAbility, 0.0f))
        {
            return false;
        }
        combat_->ObservePlayerAbility(
            heroThing, HeroMeleeAttackAbility, 0.0f, true);
        char detail[256] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "source=%s-local-hero target=%s ordinal=%u ability_id=%u",
            hostRole_ ? "host" : "guest",
            targetRole,
            ordinal,
            HeroMeleeAttackAbility);
        diagnostics_.Event("MultiplayerCombatHeroAttackSubmitted", detail);
        return true;
    }

    void CombatVisualExchangeDriver::CompleteVisualSequence() noexcept
    {
        if (visualSequenceComplete_)
        {
            return;
        }
        visualSequenceComplete_ = true;
        diagnostics_.Event(
            "MultiplayerCombatVisualExchangeComplete",
            hostRole_
                ? "role=host hero_enemy=2 enemy_host=2 enemy_guest=2 pvp_guest=2"
                : "role=guest hero_enemy=2 pvp_host=2");
    }

    bool CombatVisualExchangeDriver::ObserveTargetTerminal(
        game::Entity* target) noexcept
    {
        if (target == nullptr || creatures_ == nullptr)
        {
            return false;
        }
        const float current = creatures_->GetHealth(target);
        const float maximum = creatures_->GetMaximumHealth(target);
        const bool dead = target->IsDead();
        if (!std::isfinite(current) || !std::isfinite(maximum) ||
            std::fabs(maximum - ExpectedTargetMaximumHealth) >
                TerminalHealthEpsilon ||
            current > TerminalHealthEpsilon || !dead)
        {
            return false;
        }

        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "role=%s thing_uid=%016llX script_name=%s health=%.3f maximum=%.3f dead=true attacks=%u",
            hostRole_ ? "host" : "guest",
            static_cast<unsigned long long>(targetUid_),
            TargetScriptName,
            current,
            maximum,
            targetKillAttackCount_);
        diagnostics_.Event(
            "MultiplayerCombatTargetTerminalObserved", detail);
        completed_ = true;
        return true;
    }

    bool CombatVisualExchangeDriver::ExecuteTargetKill(
        game::Entity* target,
        void* heroThing,
        void* targetThing,
        std::uint64_t now) noexcept
    {
        if (ObserveTargetTerminal(target))
        {
            return true;
        }
        if (targetKillStartedAt_ == 0)
        {
            const float current = creatures_->GetHealth(target);
            const float maximum = creatures_->GetMaximumHealth(target);
            if (!std::isfinite(current) || !std::isfinite(maximum) ||
                std::fabs(maximum - ExpectedTargetMaximumHealth) >
                    TerminalHealthEpsilon)
            {
                diagnostics_.Event(
                    "ClientFailed",
                    "multiplayer-combat-target-not-60-health");
                completed_ = true;
                return false;
            }
            targetKillStartedAt_ = now;
            char detail[256] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "thing_uid=%016llX script_name=%s health=%.3f maximum=%.3f",
                static_cast<unsigned long long>(targetUid_),
                TargetScriptName,
                current,
                maximum);
            diagnostics_.Event(
                "MultiplayerCombatTargetKillStarted", detail);
        }

        if (targetKillAttackCount_ >= MaximumTargetKillAttacks ||
            now - targetKillStartedAt_ >= TargetKillTimeoutMilliseconds)
        {
            diagnostics_.Event(
                "ClientFailed",
                "multiplayer-combat-target-terminal-state-timeout");
            completed_ = true;
            return false;
        }

        if (!SubmitHeroAttack(
                heroThing,
                targetThing,
                "enemy-finisher",
                targetKillAttackCount_ + 1))
        {
            nextActionAt_ = now + RetryMilliseconds;
            return false;
        }
        ++targetKillAttackCount_;
        nextActionAt_ = now + TargetKillAttackSpacingMilliseconds;
        return true;
    }

    bool CombatVisualExchangeDriver::SubmitPlayerTargetedHeroAttack(
        void* heroThing,
        void* targetThing,
        const char* targetRole,
        unsigned int ordinal,
        std::uint64_t now) noexcept
    {
        if (!playerTargetRequested_)
        {
            // Each ordinal needs a fresh player-owned target action. Clear the
            // previous native selection first so a lingering target from the
            // preceding attack cannot satisfy this step before Space arrives.
            if (!ClearHeroTargets(entities_->GameModule(), heroThing))
            {
                return false;
            }
            char detail[192] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "source=%s-local-hero target=%s ordinal=%u action=SPACE",
                hostRole_ ? "host" : "guest",
                targetRole,
                ordinal);
            diagnostics_.Event(
                "MultiplayerCombatPvpTargetInputRequested", detail);
            playerTargetRequested_ = true;
            nextActionAt_ = now + RetryMilliseconds;
            return false;
        }

        if (!HeroTargets(
                entities_->GameModule(), heroThing, targetThing))
        {
            nextActionAt_ = now + RetryMilliseconds;
            return false;
        }

        if (!SubmitHeroAttack(
                heroThing, targetThing, targetRole, ordinal, false))
        {
            return false;
        }

        char detail[192] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "source=%s-local-hero target=%s ordinal=%u action=SPACE",
            hostRole_ ? "host" : "guest",
            targetRole,
            ordinal);
        diagnostics_.Event(
            "MultiplayerCombatPvpFriendlyTargetObserved", detail);
        return true;
    }

    bool CombatVisualExchangeDriver::SubmitEnemyAttack(
        void* enemyThing,
        void* targetThing,
        const char* targetRole,
        unsigned int ordinal) noexcept
    {
        if (!combat_->SubmitAuthoritativeImmediateAttack(
                enemyThing, targetThing))
        {
            return false;
        }
        char detail[256] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "source=host-authoritative-enemy target=%s ordinal=%u route=native-immediate-attack",
            targetRole,
            ordinal);
        diagnostics_.Event(
            "MultiplayerCombatEnemyCounterattackSubmitted", detail);
        return true;
    }

    void CombatVisualExchangeDriver::Advance(
        std::uint64_t now,
        std::uint64_t delay) noexcept
    {
        ++step_;
        failedAttempts_ = 0;
        playerTargetRequested_ = false;
        nextActionAt_ = now + delay;
    }

    bool CombatVisualExchangeDriver::IsComplete() const noexcept
    {
        return completed_;
    }

    bool CombatVisualExchangeDriver::WantsTargetDeath() const noexcept
    {
        return enabled_ && hostRole_ && step_ >= 8 && !completed_;
    }

    void CombatVisualExchangeDriver::Shutdown() noexcept
    {
        if (target_ != nullptr)
        {
            target_->Release();
            target_ = nullptr;
        }
        entities_ = nullptr;
        creatures_ = nullptr;
        combat_ = nullptr;
        diagnostics_ = {};
        fixtureReadyAt_ = 0;
        nextActionAt_ = 0;
        targetKillStartedAt_ = 0;
        targetUid_ = 0;
        step_ = 0;
        failedAttempts_ = 0;
        targetKillAttackCount_ = 0;
        meleeRequested_ = false;
        meleeReady_ = false;
        playerTargetRequested_ = false;
        visualSequenceComplete_ = false;
        hostRole_ = false;
        enabled_ = false;
        completed_ = false;
    }
}
