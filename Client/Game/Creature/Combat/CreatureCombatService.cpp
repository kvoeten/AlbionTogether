#include "CreatureCombatService.h"

#include "Game/Creature/Native/CreatureFrameFunctions.h"
#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"

#include <cstdio>

namespace fable::game::creature::combat
{
    std::uint64_t CreatureCombatService::ReadThingUid(
        void* const thing) noexcept
    {
        if (thing == nullptr)
        {
            return 0;
        }
        std::uint64_t uid = 0;
        __try
        {
            uid = *reinterpret_cast<const std::uint64_t*>(
                static_cast<const std::uint8_t*>(thing) + 0x14);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            uid = 0;
        }
        return uid;
    }

    CreatureCombatService::~CreatureCombatService()
    {
        Shutdown();
    }

    void CreatureCombatService::Shutdown() noexcept
    {
        ClearPlayerCombat();
        AcquireSRWLockExclusive(&abilitySinkLock_);
        abilitySinks_ = {};
        ReleaseSRWLockExclusive(&abilitySinkLock_);
        AcquireSRWLockExclusive(&healthMutationSinkLock_);
        healthMutationSinks_ = {};
        ReleaseSRWLockExclusive(&healthMutationSinkLock_);
        AcquireSRWLockExclusive(&resolvedHitSinkLock_);
        resolvedHitSinks_ = {};
        ReleaseSRWLockExclusive(&resolvedHitSinkLock_);
        creatureHitResolutionHook_.Shutdown();
        combatHealthMutationHook_.Shutdown();
        playerAttackAbilityHook_.Shutdown();
        entities_ = nullptr;
        animation_ = nullptr;
        diagnostics_ = {};
    }

    bool CreatureCombatService::Initialize(
        EntityService& entities,
        animation::CreatureAnimationService& animation,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        entities_ = &entities;
        animation_ = &animation;
        diagnostics_ = diagnostics;
        observedPlayerAttackCount_.store(0, std::memory_order_release);
        const bool attackHookInstalled =
            playerAttackAbilityHook_.Install(
                entities.GameModule(),
                *this,
                diagnostics_);
        const bool healthHookInstalled =
            combatHealthMutationHook_.Install(
                entities.GameModule(), diagnostics_);
        const bool hitHookInstalled =
            creatureHitResolutionHook_.Install(
                entities.GameModule(), diagnostics_);
        if (hitHookInstalled)
        {
            creatureHitResolutionHook_.SetEventSink(
                &CreatureCombatService::CaptureResolvedHit, this);
        }
        if (healthHookInstalled)
        {
            combatHealthMutationHook_.SetEventSink(
                &CreatureCombatService::CaptureHealthMutation, this);
        }
        diagnostics_.Event(
            "CreatureCombatAbiValidated",
            attackHookInstalled && healthHookInstalled && hitHookInstalled
                ? "CThingCreature ability submission, player ATTACK caller, shared health mutation, and native OnHit resolution validated"
                : "one or more CThingCreature combat definitions failed validation");
        diagnostics_.Log(attackHookInstalled
            ? "Creature combat: deep native player ATTACK-to-creature ability routing validated."
            : "Creature combat: current-build player ATTACK ability routing definition failed validation.");
        if (!attackHookInstalled || !healthHookInstalled || !hitHookInstalled)
        {
            Shutdown();
            return false;
        }
        return true;
    }

    bool CreatureCombatService::RoutePlayerCombat(Entity* hero, Entity* puppet)
    {
        if (entities_ == nullptr || hero == nullptr || puppet == nullptr ||
            hero == puppet || !hero->IsValid() || !puppet->IsValid())
        {
            return false;
        }

        void* const puppetThing = entities_->ResolveNative(puppet->NativeHandle());
        if (!::fable::game::creature::native::CreatureFrameFunctions::ValidateCreature(
                entities_->GameModule(),
                puppetThing))
        {
            return false;
        }

        ClearPlayerCombat();
        hero->AddRef();
        puppet->AddRef();
        AcquireSRWLockExclusive(&routeLock_);
        retainedHero_ = hero;
        retainedPuppet_ = puppet;
        routedAttackCount_.store(0, std::memory_order_release);
        ReleaseSRWLockExclusive(&routeLock_);

        char detail[256] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "hero=%p puppet=%p trigger=CThingCreature::UseAbility caller=PlayerAttackCommandHandler target_source=native-weapon-sweep hidden_hero_ability_replaced=true",
            entities_->ResolveNative(hero->NativeHandle()),
            puppetThing);
        diagnostics_.Event("CreaturePlayerCombatRouterBound", detail);
        return true;
    }

    void CreatureCombatService::ClearPlayerCombat() noexcept
    {
        AcquireSRWLockExclusive(&routeLock_);
        Entity* const retainedHero = retainedHero_;
        Entity* const retainedPuppet = retainedPuppet_;
        retainedHero_ = nullptr;
        retainedPuppet_ = nullptr;
        ReleaseSRWLockExclusive(&routeLock_);

        const bool wasBound = retainedHero != nullptr || retainedPuppet != nullptr;
        if (retainedHero != nullptr)
        {
            retainedHero->Release();
        }
        if (retainedPuppet != nullptr)
        {
            retainedPuppet->Release();
        }
        if (wasBound)
        {
            diagnostics_.Event(
                "CreaturePlayerCombatRouterCleared",
                "NPC attack routing released");
        }
    }

    bool CreatureCombatService::IsPlayerCombatRouted() const noexcept
    {
        AcquireSRWLockShared(&routeLock_);
        const bool routed = retainedHero_ != nullptr && retainedPuppet_ != nullptr;
        ReleaseSRWLockShared(&routeLock_);
        return routed;
    }

    unsigned int CreatureCombatService::RoutedPlayerAttackCount() const noexcept
    {
        return routedAttackCount_.load(std::memory_order_acquire);
    }

    unsigned int CreatureCombatService::InterceptedHeroAttackCount() const noexcept
    {
        return playerAttackAbilityHook_.InterceptedAttackCount();
    }

    bool CreatureCombatService::ResolvePlayerAttackCreature(
        void* sourceCreature,
        void*& routedCreature) noexcept
    {
        routedCreature = sourceCreature;
        if (sourceCreature == nullptr || entities_ == nullptr)
        {
            return false;
        }

        AcquireSRWLockShared(&routeLock_);
        if (retainedHero_ == nullptr || retainedPuppet_ == nullptr)
        {
            ReleaseSRWLockShared(&routeLock_);
            return false;
        }
        void* const heroThing = entities_->ResolveNative(retainedHero_->NativeHandle());
        void* const puppetThing = entities_->ResolveNative(retainedPuppet_->NativeHandle());
        if (sourceCreature != heroThing ||
            puppetThing == nullptr || puppetThing == heroThing)
        {
            ReleaseSRWLockShared(&routeLock_);
            return false;
        }

        routedCreature = puppetThing;
        routedAttackCount_.fetch_add(1, std::memory_order_acq_rel);
        ReleaseSRWLockShared(&routeLock_);
        return true;
    }
}
