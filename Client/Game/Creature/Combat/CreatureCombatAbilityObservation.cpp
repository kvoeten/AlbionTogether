#include "CreatureCombatService.h"

#include "Game/Creature/Actions/Native/CreatureActionFunctions.h"
#include "Game/Creature/Actions/Hooks/CreatureActionLifecycleObserver.h"
#include "Game/Creature/Actions/CreatureActionSubmissionContext.h"
#include "Game/Creature/Combat/Native/CreatureDeathFunctions.h"
#include "Game/Creature/Animation/CreatureAnimationService.h"
#include "Game/Creature/Combat/Native/HeroTargetingComponent.h"
#include "Game/Creature/Combat/Native/CreatureHitReactionFunctions.h"
#include "Game/Creature/Native/CreatureFrameFunctions.h"
#include "Game/Entity/EntityService.h"
#include "Game/Entity/Native/ThingComponentAccess.h"

#include <Windows.h>

#include <cstdio>

namespace fable::game::creature::combat
{

    void CreatureCombatService::ObservePlayerAbility(
        void* sourceCreature,
        unsigned int abilityId,
        float charge,
        bool attackCommand,
        const std::uint64_t actionCorrelationId) noexcept
    {
        std::array<AbilitySinkEntry, AbilitySinkCapacity> sinks = {};
        AcquireSRWLockShared(&abilitySinkLock_);
        sinks = abilitySinks_;
        ReleaseSRWLockShared(&abilitySinkLock_);
        bool hasSink = false;
        for (const AbilitySinkEntry& entry : sinks)
        {
            hasSink = hasSink || entry.sink != nullptr;
        }
        if (!hasSink || entities_ == nullptr || sourceCreature == nullptr)
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
                ValidateCreature(entities_->GameModule(), target) &&
            !::fable::game::creature::native::CreatureFrameFunctions::
                ValidatePlayerCreature(entities_->GameModule(), target))
        {
            target = nullptr;
        }

        CreatureAbilityEvent event;
        event.sourceCreature = sourceCreature;
        event.sourceThingUid = ReadThingUid(sourceCreature);
        event.targetCreature = target;
        event.targetThingUid = ReadThingUid(target);
        event.actionCorrelationId = actionCorrelationId;
        event.abilityId = abilityId;
        event.threadId = GetCurrentThreadId();
        event.charge = charge;
        event.observedAt = GetTickCount64();
        event.attackCommand = attackCommand;
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
                "event=%u correlation_id=%llu source=%p source_uid=%016llX targeting=%p read=%s selected=%p primary=%p secondary=%p accepted_target=%p target_uid=%016llX ability=%u charge=%.3f attack_command=%s",
                observed,
                static_cast<unsigned long long>(actionCorrelationId),
                sourceCreature,
                static_cast<unsigned long long>(event.sourceThingUid),
                targeting,
                targetsRead ? "true" : "false",
                targets.selected,
                targets.candidatePrimary,
                targets.candidateSecondary,
                target,
                static_cast<unsigned long long>(event.targetThingUid),
                abilityId,
                charge,
                attackCommand ? "true" : "false");
            diagnostics_.Event("CreaturePlayerAbilityObserved", detail);
        }
        for (const AbilitySinkEntry& entry : sinks)
        {
            if (entry.sink != nullptr)
            {
                entry.sink(entry.context, event);
            }
        }
    }

    bool CreatureCombatService::AddAbilitySink(
        AbilitySink sink,
        void* context) noexcept
    {
        if (sink == nullptr)
        {
            return false;
        }
        AcquireSRWLockExclusive(&abilitySinkLock_);
        for (const AbilitySinkEntry& entry : abilitySinks_)
        {
            if (entry.sink == sink && entry.context == context)
            {
                ReleaseSRWLockExclusive(&abilitySinkLock_);
                return true;
            }
        }
        for (AbilitySinkEntry& entry : abilitySinks_)
        {
            if (entry.sink == nullptr)
            {
                entry = {sink, context};
                ReleaseSRWLockExclusive(&abilitySinkLock_);
                return true;
            }
        }
        ReleaseSRWLockExclusive(&abilitySinkLock_);
        return false;
    }

    void CreatureCombatService::RemoveAbilitySink(
        AbilitySink sink,
        void* context) noexcept
    {
        AcquireSRWLockExclusive(&abilitySinkLock_);
        for (AbilitySinkEntry& entry : abilitySinks_)
        {
            if (entry.sink == sink && entry.context == context)
            {
                entry = {};
            }
        }
        ReleaseSRWLockExclusive(&abilitySinkLock_);
    }

}
