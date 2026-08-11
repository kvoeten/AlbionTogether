#include "CreatureCombatService.h"

#include "Game/Creature/Native/CreatureFrameFunctions.h"
#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"

#include <cstdio>

namespace fable::game::creature::combat
{
    CreatureCombatService::~CreatureCombatService()
    {
        ClearPlayerCombat();
    }

    bool CreatureCombatService::Initialize(
        EntityService& entities,
        const core::Diagnostics& diagnostics)
    {
        entities_ = &entities;
        diagnostics_ = diagnostics;
        const bool attackHookInstalled =
            playerAttackAbilityHook_.Install(
                entities.GameModule(),
                *this,
                diagnostics_);
        diagnostics_.Event(
            "CreatureCombatAbiValidated",
            attackHookInstalled
                ? "CThingCreature ability submission and player ATTACK caller validated"
                : "CThingCreature ability submission validation failed");
        diagnostics_.Log(attackHookInstalled
            ? "Creature combat: deep native player ATTACK-to-creature ability routing validated."
            : "Creature combat: current-build player ATTACK ability routing definition failed validation.");
        return attackHookInstalled;
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
