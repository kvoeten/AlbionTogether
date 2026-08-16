#include "CreatureCombatService.h"

#include "Game/Creature/Combat/Native/HeroTargetingComponent.h"
#include "Game/Creature/Native/CreatureFrameFunctions.h"
#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"
#include "Game/Entity/Native/ThingComponentAccess.h"

#include <Windows.h>

#include <cstdio>

namespace
{
    std::uint64_t ReadThingUid(void* thing) noexcept
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
}

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
        observedPlayerAttackCount_.store(0, std::memory_order_release);
        const bool attackHookInstalled =
            playerAttackAbilityHook_.Install(
                entities.GameModule(),
                *this,
                diagnostics_);
        const bool healthHookInstalled =
            combatHealthMutationHook_.Install(
                entities.GameModule(), diagnostics_);
        diagnostics_.Event(
            "CreatureCombatAbiValidated",
            attackHookInstalled && healthHookInstalled
                ? "CThingCreature ability submission, player ATTACK caller, and shared player/NPC health mutation validated"
                : "one or more CThingCreature combat definitions failed validation");
        diagnostics_.Log(attackHookInstalled
            ? "Creature combat: deep native player ATTACK-to-creature ability routing validated."
            : "Creature combat: current-build player ATTACK ability routing definition failed validation.");
        return attackHookInstalled && healthHookInstalled;
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

    void CreatureCombatService::ObservePlayerAttack(
        void* sourceCreature,
        unsigned int abilityId,
        float charge) noexcept
    {
        const PlayerAttackSink sink = playerAttackSink_.load(
            std::memory_order_acquire);
        if (sink == nullptr || entities_ == nullptr || sourceCreature == nullptr)
        {
            return;
        }

        void* const targeting = entity::native::ThingComponentAccess::Find(
            sourceCreature,
            entity::native::ThingComponentType::Targeting);
        native::HeroTargetingSnapshot targets;
        const bool targetsRead = native::HeroTargetingComponent::ReadTargets(
            entities_->GameModule(), targeting, targets);
        (void)targetsRead;
        void* target = targets.selected != nullptr
            ? targets.selected
            : (targets.candidatePrimary != nullptr
                ? targets.candidatePrimary
                : targets.candidateSecondary);
        if (!::fable::game::creature::native::CreatureFrameFunctions::
                ValidateCreature(entities_->GameModule(), target))
        {
            target = nullptr;
        }

        PlayerAttackEvent event;
        event.sourceCreature = sourceCreature;
        event.targetCreature = target;
        event.targetThingUid = ReadThingUid(target);
        event.abilityId = abilityId;
        event.charge = charge;
        event.observedAt = GetTickCount64();
        const unsigned int observed =
            observedPlayerAttackCount_.fetch_add(
                1,
                std::memory_order_acq_rel) + 1;
        if (observed <= 8)
        {
            char detail[384] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "event=%u source=%p targeting=%p read=%s selected=%p primary=%p secondary=%p accepted_target=%p target_uid=%llu ability=%u charge=%.3f",
                observed,
                sourceCreature,
                targeting,
                targetsRead ? "true" : "false",
                targets.selected,
                targets.candidatePrimary,
                targets.candidateSecondary,
                target,
                static_cast<unsigned long long>(event.targetThingUid),
                abilityId,
                charge);
            diagnostics_.Event("CreaturePlayerAttackTargetObserved", detail);
        }
        sink(
            playerAttackSinkContext_.load(std::memory_order_acquire),
            event);
    }

    void CreatureCombatService::SetPlayerAttackSink(
        PlayerAttackSink sink,
        void* context) noexcept
    {
        playerAttackSinkContext_.store(context, std::memory_order_release);
        playerAttackSink_.store(sink, std::memory_order_release);
    }

    void CreatureCombatService::SetHealthMutationSink(
        HealthMutationSink sink,
        void* context) noexcept
    {
        combatHealthMutationHook_.SetEventSink(sink, context);
    }

    bool CreatureCombatService::ReadCombatHealth(
        void* creature,
        float& currentHealth,
        float& maximumHealth) const noexcept
    {
        return combatHealthMutationHook_.Read(
            creature, currentHealth, maximumHealth);
    }

    bool CreatureCombatService::ApplyAuthoritativeCombatHealth(
        void* creature,
        float currentHealth,
        float maximumHealth) noexcept
    {
        return combatHealthMutationHook_.ApplyAuthoritative(
            creature, currentHealth, maximumHealth);
    }
}
