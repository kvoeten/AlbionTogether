#include "HeroWillAbilityAcceptanceDriver.h"

#include "Game/Creature/Combat/Native/HeroTargetingComponent.h"
#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"
#include "Game/Entity/Native/ThingComponentAccess.h"
#include "Game/HeroPawn/Abilities/HeroWillAbilityService.h"

#include <Windows.h>

#include <array>
#include <cstdio>

namespace
{
    constexpr char TargetScriptName[] =
        "SCRIPT_NAME_FABLE_TOGETHER_COMBAT_TARGET";
    constexpr char ArenaMap[] = "FrescoDome";
    constexpr std::uint64_t HostOpeningDelayMilliseconds = 2'000;
    // The host's final world effect is deliberately given a full quiet window
    // before the guest starts its local sequence. This keeps acceptance from
    // stacking two authoritative long-lived Will effects in one world.
    constexpr std::uint64_t GuestOpeningDelayMilliseconds = 130'000;
    constexpr std::uint64_t PillarGuestOpeningDelayMilliseconds = 67'000;
    constexpr std::uint64_t AbilityEffectMilliseconds = 1'800;
    // Divine Wrath and Unholy Power own an automatic multi-stage native
    // Cast -> BuildUp -> world-effect lifecycle. At maximum progression the
    // effect outlives the short actions used by ordinary Will abilities, so
    // keep the fixture quiet until the complete pillar sequence is visible.
    constexpr std::uint64_t WorldEffectMilliseconds = 30'000;
    constexpr int FixtureProgressionState = 3;
    constexpr std::uint64_t InterAbilityMilliseconds = 600;
    constexpr std::uint64_t RetryMilliseconds = 250;
    constexpr unsigned int MaximumFailedAttempts = 30;
    constexpr std::size_t DivineWrathSequenceIndex = 16;
    constexpr std::size_t PillarSequenceEndIndex = 18;
    constexpr std::array<fable::game::hero_pawn::abilities::HeroAbility, 19>
        AbilitySequence = {
            fable::game::hero_pawn::abilities::HeroAbility::ForcePush,
            fable::game::hero_pawn::abilities::HeroAbility::Time,
            fable::game::hero_pawn::abilities::HeroAbility::Enflame,
            fable::game::hero_pawn::abilities::HeroAbility::PhysicalShield,
            fable::game::hero_pawn::abilities::HeroAbility::Turncoat,
            fable::game::hero_pawn::abilities::HeroAbility::DrainLife,
            fable::game::hero_pawn::abilities::HeroAbility::RaiseDead,
            fable::game::hero_pawn::abilities::HeroAbility::Berserk,
            fable::game::hero_pawn::abilities::HeroAbility::DoubleStrike,
            fable::game::hero_pawn::abilities::HeroAbility::Summon,
            fable::game::hero_pawn::abilities::HeroAbility::Lightning,
            fable::game::hero_pawn::abilities::HeroAbility::BattleCharge,
            fable::game::hero_pawn::abilities::HeroAbility::AssassinRush,
            fable::game::hero_pawn::abilities::HeroAbility::HealLife,
            fable::game::hero_pawn::abilities::HeroAbility::GhostSword,
            fable::game::hero_pawn::abilities::HeroAbility::MultiArrow,
            fable::game::hero_pawn::abilities::HeroAbility::DivineWrath,
            fable::game::hero_pawn::abilities::HeroAbility::UnholyPower,
            // Keep the known-stalling script-dispatched Fireball last so its
            // isolated lifecycle diagnosis cannot hide results for 17-19.
            fable::game::hero_pawn::abilities::HeroAbility::Fireball,
        };

    bool IsLongWorldEffect(
        fable::game::hero_pawn::abilities::HeroAbility ability) noexcept
    {
        using fable::game::hero_pawn::abilities::HeroAbility;
        return ability == HeroAbility::DivineWrath ||
            ability == HeroAbility::UnholyPower;
    }

    bool PillarOnlyRequested() noexcept
    {
        char value[8] = {};
        const DWORD length = GetEnvironmentVariableA(
            "FABLE_TOGETHER_HERO_WILL_PILLAR_ONLY",
            value,
            static_cast<DWORD>(std::size(value)));
        return length != 0 && length < std::size(value) && value[0] == '1';
    }

    bool RequiresImmediateRelease(
        fable::game::hero_pawn::abilities::HeroAbility ability) noexcept
    {
        using fable::game::hero_pawn::abilities::HeroAbility;
        // Fireball uses a separate 0x9D spell component. Lightning and the
        // script-created CTCMultiArrow both attach verified held-action
        // components that must be released before the sequence can continue.
        return ability == HeroAbility::Lightning ||
            ability == HeroAbility::MultiArrow;
    }

    fable::game::hero_pawn::abilities::HeroAbilityCommand TeardownCommand(
        fable::game::hero_pawn::abilities::HeroAbility ability) noexcept
    {
        using fable::game::hero_pawn::abilities::HeroAbility;
        using fable::game::hero_pawn::abilities::HeroAbilityCommand;
        if (ability == HeroAbility::PhysicalShield)
        {
            return HeroAbilityCommand::Toggle;
        }
        if (ability == HeroAbility::Summon ||
            ability == HeroAbility::GhostSword ||
            RequiresImmediateRelease(ability))
        {
            return HeroAbilityCommand::Cancel;
        }
        return HeroAbilityCommand::None;
    }
}

namespace fable::automation::multiplayer::abilities
{
    void HeroWillAbilityAcceptanceDriver::Initialize(
        bool enabled,
        bool hostRole,
        bool focused,
        game::EntityService& entities,
        game::hero_pawn::abilities::HeroWillAbilityService& abilities,
        const core::Diagnostics& diagnostics) noexcept
    {
        Shutdown();
        entities_ = &entities;
        abilities_ = &abilities;
        diagnostics_ = diagnostics;
        hostRole_ = hostRole;
        pillarOnly_ = PillarOnlyRequested();
        abilityIndex_ = pillarOnly_ ? DivineWrathSequenceIndex : 0;
        enabled_ = enabled;
        if (enabled_)
        {
            diagnostics_.Event(
                "MultiplayerHeroWillSequenceEnabled",
                hostRole_
                    ? (focused
                        ? (pillarOnly_
                            ? "role=host abilities=18-19 pillar-focus"
                            : "role=host abilities=1-19 ordered-after-target")
                        : (pillarOnly_
                            ? "role=host abilities=18-19 pillar-focus"
                            : "role=host abilities=1-19 ordered-after-melee"))
                    : (focused
                        ? (pillarOnly_
                            ? "role=guest abilities=18-19 pillar-focus"
                            : "role=guest abilities=1-19 ordered-after-host-target")
                        : (pillarOnly_
                            ? "role=guest abilities=18-19 pillar-focus"
                            : "role=guest abilities=1-19 ordered-after-host")));
        }
    }

    void HeroWillAbilityAcceptanceDriver::Tick(
        bool remotePresentationReady,
        bool combatExchangeComplete)
    {
        if (!enabled_ || completed_ || !remotePresentationReady ||
            !combatExchangeComplete || entities_ == nullptr ||
            abilities_ == nullptr)
        {
            return;
        }

        game::Entity* const hero = entities_->GetHero();
        game::Entity* const target =
            entities_->FindByScriptName(TargetScriptName);
        const bool ready = hero != nullptr && hero->IsValid() &&
            target != nullptr && target->IsValid() &&
            hero->GetCurrentMapName() == ArenaMap &&
            target->GetCurrentMapName() == ArenaMap;
        if (!ready)
        {
            if (target != nullptr)
            {
                target->Release();
            }
            if (hero != nullptr)
            {
                hero->Release();
            }
            return;
        }

        // Keep autonomous AI restrained through the combat target driver, but
        // preserve the Hobbe's real hostility and damage traits. Targeted Will
        // abilities reject friendly or invulnerable creatures before their
        // native action/effect submission path is reached.
        target->SetFriendsWithEverything(false);
        target->SetAttackable(true);
        target->SetDamageable(true);

        void* const heroThing = entities_->ResolveNative(hero->NativeHandle());
        void* const targetThing = entities_->ResolveNative(
            target->NativeHandle());
        const std::uint64_t now = GetTickCount64();
        if (heroThing != nullptr && !progressionPrepared_)
        {
            bool prepared = true;
            const std::size_t sequenceEnd = pillarOnly_
                ? PillarSequenceEndIndex
                : AbilitySequence.size();
            for (std::size_t index = abilityIndex_;
                 index < sequenceEnd;
                 ++index)
            {
                prepared = abilities_->ApplyProgressionState(
                    heroThing,
                    AbilitySequence[index],
                    FixtureProgressionState) && prepared;
            }
            if (!prepared)
            {
                Fail("reason=isolated-fixture-progression-preparation-failed");
                target->Release();
                hero->Release();
                return;
            }
            progressionPrepared_ = true;
            diagnostics_.Event(
                "MultiplayerHeroWillFixtureProgressionPrepared",
                pillarOnly_
                    ? "abilities=18-19 state=3 scope=isolated-run-specific-save"
                    : "abilities=1-19 state=3 scope=isolated-run-specific-save");
        }
        if (!armed_)
        {
            armed_ = true;
            nextActionAt_ = now + (hostRole_
                ? HostOpeningDelayMilliseconds
                : (pillarOnly_
                    ? PillarGuestOpeningDelayMilliseconds
                    : GuestOpeningDelayMilliseconds));
            diagnostics_.Event(
                "MultiplayerHeroWillSequenceArmed",
                hostRole_
                    ? "role=host target=restrained-enemy"
                    : "role=guest target=restrained-enemy");
        }

        if (heroThing == nullptr || targetThing == nullptr ||
            now < nextActionAt_)
        {
            target->Release();
            hero->Release();
            return;
        }

        const std::size_t sequenceEnd = pillarOnly_
            ? PillarSequenceEndIndex
            : AbilitySequence.size();
        if (abilityIndex_ >= sequenceEnd)
        {
            completed_ = true;
            char detail[192] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "role=%s accepted=%u expected_unsupported=%u total=%zu",
                hostRole_ ? "host" : "guest",
                acceptedUses_,
                expectedUnsupported_,
                sequenceEnd - (pillarOnly_ ? DivineWrathSequenceIndex : 0));
            diagnostics_.Event(
                "MultiplayerHeroWillSequenceComplete", detail);
            target->Release();
            hero->Release();
            return;
        }

        if (cancelling_)
        {
            const auto teardown = TeardownCommand(activeAbility_);
            if (RequiresImmediateRelease(activeAbility_))
            {
                const bool actionActive = abilities_->HasActiveAction(
                    heroThing, activeAbility_);
                chargedActionObserved_ = chargedActionObserved_ ||
                    actionActive;
                if (!chargedReleaseRequested_)
                {
                    if (!actionActive)
                    {
                        ++failedAttempts_;
                        if (failedAttempts_ >= MaximumFailedAttempts)
                        {
                            char reason[192] = {};
                            std::snprintf(
                                reason,
                                sizeof(reason),
                                "ability=%u name=%s reason=charged-action-component-did-not-attach",
                                static_cast<unsigned int>(activeAbility_),
                                game::hero_pawn::abilities::Name(
                                    activeAbility_));
                            Fail(reason);
                        }
                        else
                        {
                            nextActionAt_ = now + RetryMilliseconds;
                        }
                        target->Release();
                        hero->Release();
                        return;
                    }

                    chargedReleaseRequested_ =
                        abilities_->SubmitAuthoritative(
                            heroThing, activeAbility_, teardown);
                    char release[224] = {};
                    std::snprintf(
                        release,
                        sizeof(release),
                        "role=%s ability=%u name=%s action_active=true release_requested=%s",
                        hostRole_ ? "host" : "guest",
                        static_cast<unsigned int>(activeAbility_),
                        game::hero_pawn::abilities::Name(activeAbility_),
                        chargedReleaseRequested_ ? "true" : "false");
                    diagnostics_.Event(
                        "MultiplayerHeroWillImmediateRelease", release);
                    ++failedAttempts_;
                    nextActionAt_ = now + RetryMilliseconds;
                    if (!chargedReleaseRequested_ &&
                        failedAttempts_ >= MaximumFailedAttempts)
                    {
                        char reason[192] = {};
                        std::snprintf(
                            reason,
                            sizeof(reason),
                            "ability=%u name=%s reason=charged-release-not-accepted",
                            static_cast<unsigned int>(activeAbility_),
                            game::hero_pawn::abilities::Name(
                                activeAbility_));
                        Fail(reason);
                    }
                    target->Release();
                    hero->Release();
                    return;
                }

                if (!actionActive)
                {
                    char completion[224] = {};
                    std::snprintf(
                        completion,
                        sizeof(completion),
                        "role=%s ability=%u name=%s action-observed=%s action-component-retired=true",
                        hostRole_ ? "host" : "guest",
                        static_cast<unsigned int>(activeAbility_),
                        game::hero_pawn::abilities::Name(activeAbility_),
                        chargedActionObserved_ ? "true" : "false");
                    diagnostics_.Event(
                        "MultiplayerHeroWillTeardownComplete", completion);
                    Advance(now);
                    target->Release();
                    hero->Release();
                    return;
                }
            }
            if (RequiresImmediateRelease(activeAbility_))
            {
                ++failedAttempts_;
                if (failedAttempts_ >= MaximumFailedAttempts)
                {
                    char reason[192] = {};
                    std::snprintf(
                        reason,
                        sizeof(reason),
                        "ability=%u name=%s reason=active-action-component-did-not-retire",
                        static_cast<unsigned int>(activeAbility_),
                        game::hero_pawn::abilities::Name(activeAbility_));
                    Fail(reason);
                }
                else
                {
                    nextActionAt_ = now + RetryMilliseconds;
                }
                target->Release();
                hero->Release();
                return;
            }
            if (!game::hero_pawn::abilities::IsValid(teardown))
            {
                char detail[192] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "role=%s ordinal=%zu ability=%u name=%s reason=no-verified-active-teardown-command",
                    hostRole_ ? "host" : "guest",
                    abilityIndex_ + 1,
                    static_cast<unsigned int>(activeAbility_),
                    game::hero_pawn::abilities::Name(activeAbility_));
                diagnostics_.Event(
                    "MultiplayerHeroWillTeardownSkipped", detail);
                Advance(now);
                target->Release();
                hero->Release();
                return;
            }
            const bool cancelled = abilities_->SubmitAuthoritative(
                heroThing,
                activeAbility_,
                teardown);
            char detail[224] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "role=%s ordinal=%zu ability=%u name=%s command=%u accepted=%s",
                hostRole_ ? "host" : "guest",
                abilityIndex_ + 1,
                static_cast<unsigned int>(activeAbility_),
                game::hero_pawn::abilities::Name(activeAbility_),
                static_cast<unsigned int>(teardown),
                cancelled ? "true" : "false");
            diagnostics_.Event(
                "MultiplayerHeroWillTeardownSubmitted", detail);
            Advance(now);
            target->Release();
            hero->Release();
            return;
        }

        const auto ability = AbilitySequence[abilityIndex_];
        void* const targeting = game::entity::native::ThingComponentAccess::Find(
            heroThing,
            game::entity::native::ThingComponentType::Targeting);
        const bool targetAssigned = targeting != nullptr &&
            game::creature::combat::native::HeroTargetingComponent::
                AssignSelectedTarget(
                    entities_->GameModule(), targeting, targetThing);
        const bool accepted = targetAssigned &&
            abilities_->SubmitAuthoritative(
                heroThing,
                ability,
                game::hero_pawn::abilities::HeroAbilityCommand::Use);
        if (accepted)
        {
            activeAbility_ = ability;
            cancelling_ = true;
            failedAttempts_ = 0;
            ++acceptedUses_;
            nextActionAt_ = now + (IsLongWorldEffect(ability)
                ? WorldEffectMilliseconds
                : AbilityEffectMilliseconds);
            char detail[224] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "role=%s ordinal=%zu ability=%u name=%s target=%p",
                hostRole_ ? "host" : "guest",
                abilityIndex_ + 1,
                static_cast<unsigned int>(ability),
                game::hero_pawn::abilities::Name(ability),
                targetThing);
            diagnostics_.Event(
                "MultiplayerHeroWillUseSubmitted", detail);
            if (RequiresImmediateRelease(ability))
            {
                // The charged-action component is attached asynchronously by
                // the native action stack. Poll for it on later main-thread
                // ticks before requesting release; same-frame absence is not
                // evidence that the accepted Use failed.
                chargedActionObserved_ = false;
                chargedReleaseRequested_ = false;
                nextActionAt_ = now + RetryMilliseconds;
            }
        }
        else if (!game::hero_pawn::abilities::IsMultiplayerSupported(ability))
        {
            ++expectedUnsupported_;
            diagnostics_.Event(
                "MultiplayerHeroWillExpectedUnsupported",
                "ability=2 name=Time reason=process-local-world-time-disabled");
            Advance(now);
        }
        else if (ability == game::hero_pawn::abilities::HeroAbility::RaiseDead)
        {
            ++expectedUnsupported_;
            diagnostics_.Event(
                "MultiplayerHeroWillExpectedUnsupported",
                "ability=7 name=Raise Dead reason=retail-dispatcher-rejects");
            Advance(now);
        }
        else if (IsLongWorldEffect(ability))
        {
            char reason[224] = {};
            std::snprintf(
                reason,
                sizeof(reason),
                "ability=%u name=%s target_assigned=%s no_retry=true reason=long-native-world-effect-rejected",
                static_cast<unsigned int>(ability),
                game::hero_pawn::abilities::Name(ability),
                targetAssigned ? "true" : "false");
            Fail(reason);
        }
        else
        {
            ++failedAttempts_;
            nextActionAt_ = now + RetryMilliseconds;
            if (failedAttempts_ >= MaximumFailedAttempts)
            {
                char reason[192] = {};
                std::snprintf(
                    reason,
                    sizeof(reason),
                    "ability=%u name=%s target_assigned=%s",
                    static_cast<unsigned int>(ability),
                    game::hero_pawn::abilities::Name(ability),
                    targetAssigned ? "true" : "false");
                Fail(reason);
            }
        }

        target->Release();
        hero->Release();
    }

    void HeroWillAbilityAcceptanceDriver::Advance(std::uint64_t now) noexcept
    {
        ++abilityIndex_;
        failedAttempts_ = 0;
        activeAbility_ = game::hero_pawn::abilities::HeroAbility::None;
        cancelling_ = false;
        chargedActionObserved_ = false;
        chargedReleaseRequested_ = false;
        nextActionAt_ = now + InterAbilityMilliseconds;
    }

    void HeroWillAbilityAcceptanceDriver::Fail(const char* reason) noexcept
    {
        diagnostics_.Event(
            "MultiplayerHeroWillSequenceFailed",
            reason != nullptr ? reason : "unknown");
        diagnostics_.Event(
            "ClientFailed", "multiplayer-hero-will-sequence-failed");
        completed_ = true;
    }

    void HeroWillAbilityAcceptanceDriver::Shutdown() noexcept
    {
        entities_ = nullptr;
        abilities_ = nullptr;
        diagnostics_ = {};
        nextActionAt_ = 0;
        abilityIndex_ = 0;
        failedAttempts_ = 0;
        acceptedUses_ = 0;
        expectedUnsupported_ = 0;
        activeAbility_ = game::hero_pawn::abilities::HeroAbility::None;
        hostRole_ = false;
        enabled_ = false;
        armed_ = false;
        progressionPrepared_ = false;
        cancelling_ = false;
        chargedActionObserved_ = false;
        chargedReleaseRequested_ = false;
        pillarOnly_ = false;
        completed_ = false;
    }
}
