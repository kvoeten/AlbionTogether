#include "HeroWillAbilityService.h"

#include "Game/Creature/Actions/Hooks/CreatureActionLifecycleObserver.h"
#include "Game/Creature/Combat/Native/HeroTargetingComponent.h"
#include "Game/Creature/Native/CreatureFrameFunctions.h"
#include "Game/Entity/EntityService.h"
#include "Game/Entity/Native/ThingComponentAccess.h"
#include "Game/HeroPawn/Abilities/Native/HeroWillAbilityFunctions.h"

#include <cstdio>

namespace
{
    class ReplayScope final
    {
    public:
        ReplayScope() noexcept
        {
            fable::game::creature::actions::
                CreatureActionLifecycleObserver::BeginAuthoritativeReplay();
        }

        ~ReplayScope()
        {
            fable::game::creature::actions::
                CreatureActionLifecycleObserver::EndAuthoritativeReplay();
        }
    };

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

namespace fable::game::hero_pawn::abilities
{
    bool HeroWillAbilityService::Initialize(
        game::EntityService& entities,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        entities_ = &entities;
        diagnostics_ = diagnostics;
        const bool installed =
            hook_.Install(entities.GameModule(), *this, diagnostics_);
        diagnostics_.Event(
            "HeroWillAbilityAbiValidated",
            installed
                ? "native Use, Toggle, Cancel, and replay eligibility validated"
                : "native Hero Will ability definitions failed validation");
        if (!installed)
        {
            Shutdown();
            return false;
        }
        return true;
    }

    void HeroWillAbilityService::Shutdown() noexcept
    {
        pillarLifecycleHook_.Shutdown();
        hook_.Shutdown();
        AcquireSRWLockExclusive(&sinkLock_);
        sinks_ = {};
        ReleaseSRWLockExclusive(&sinkLock_);
        entities_ = nullptr;
        diagnostics_ = {};
    }

    bool HeroWillAbilityService::AttachActionLifecycleObserver(
        game::creature::actions::CreatureActionLifecycleObserver& observer)
    {
        const bool attached = pillarLifecycleHook_.Install(
            observer, diagnostics_);
        diagnostics_.Event(
            attached
                ? "HeroWillActionLifecycleAttached"
                : "HeroWillActionLifecycleAttachmentFailed",
            attached
                ? "pillar Cast-BuildUp-Release transitions use the observed native action stack"
                : "pillar native action transition policy could not attach");
        return attached;
    }

    void HeroWillAbilityService::DetachActionLifecycleObserver() noexcept
    {
        pillarLifecycleHook_.Shutdown();
    }

    bool HeroWillAbilityService::AddEventSink(
        EventSink sink,
        void* context) noexcept
    {
        if (sink == nullptr)
        {
            return false;
        }
        AcquireSRWLockExclusive(&sinkLock_);
        for (const SinkEntry& entry : sinks_)
        {
            if (entry.sink == sink && entry.context == context)
            {
                ReleaseSRWLockExclusive(&sinkLock_);
                return true;
            }
        }
        for (SinkEntry& entry : sinks_)
        {
            if (entry.sink == nullptr)
            {
                entry = {sink, context};
                ReleaseSRWLockExclusive(&sinkLock_);
                return true;
            }
        }
        ReleaseSRWLockExclusive(&sinkLock_);
        return false;
    }

    void HeroWillAbilityService::RemoveEventSink(
        EventSink sink,
        void* context) noexcept
    {
        AcquireSRWLockExclusive(&sinkLock_);
        for (SinkEntry& entry : sinks_)
        {
            if (entry.sink == sink && entry.context == context)
            {
                entry = {};
            }
        }
        ReleaseSRWLockExclusive(&sinkLock_);
    }

    bool HeroWillAbilityService::SubmitAuthoritative(
        void* hero,
        HeroAbility ability,
        HeroAbilityCommand command) noexcept
    {
        return Submit(hero, ability, command, true);
    }

    bool HeroWillAbilityService::Replay(
        void* hero,
        HeroAbility ability,
        HeroAbilityCommand command) noexcept
    {
        return Submit(hero, ability, command, false);
    }

    bool HeroWillAbilityService::ApplyProgressionState(
        void* hero,
        HeroAbility ability,
        int state) noexcept
    {
        if (entities_ == nullptr || hero == nullptr || !IsValid(ability) ||
            state < 0 || state > 3)
        {
            return false;
        }
        void* const component = native::HeroWillAbilityFunctions::FindComponent(
            hero, entities_->GameModule());
        const bool applied =
            native::HeroWillAbilityFunctions::ApplyAbilityProgressionState(
                component, ability, state);
        if (!applied)
        {
            char detail[192] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "hero=%p component=%p ability=%u name=%s state=%d",
                hero,
                component,
                static_cast<unsigned int>(ability),
                Name(ability),
                state);
            diagnostics_.Event(
                "HeroWillAbilityProgressionApplyFailed", detail);
        }
        return applied;
    }

    bool HeroWillAbilityService::HasActiveAction(
        void* hero,
        HeroAbility ability) const noexcept
    {
        if (entities_ == nullptr || hero == nullptr || !IsValid(ability))
        {
            return false;
        }
        void* const component = native::HeroWillAbilityFunctions::FindComponent(
            hero, entities_->GameModule());
        return native::HeroWillAbilityFunctions::HasActiveAction(
            component, ability);
    }

    bool HeroWillAbilityService::Submit(
        void* hero,
        HeroAbility ability,
        HeroAbilityCommand command,
        bool publish) noexcept
    {
        if (entities_ == nullptr || hero == nullptr || !IsValid(ability) ||
            !IsValid(command))
        {
            return false;
        }
        const HMODULE module = entities_->GameModule();
        if (!creature::native::CreatureFrameFunctions::ValidatePlayerCreature(
                module, hero) &&
            !creature::native::CreatureFrameFunctions::ValidateCreature(
                module, hero))
        {
            return false;
        }
        void* const component = native::HeroWillAbilityFunctions::FindComponent(
            hero, module);
        if (component == nullptr)
        {
            return false;
        }
        const ReplayScope replay;
        const bool accepted = publish
            ? hook_.SubmitLocal(component, ability, command)
            : hook_.SubmitReplicated(component, ability, command);
        if (accepted && publish)
        {
            Observe(component, ability, command);
        }
        int abilityProgressionState = -1;
        const bool abilityProgressionReadable =
            native::HeroWillAbilityFunctions::ReadAbilityProgressionState(
                component, ability, abilityProgressionState);
        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "hero=%p component=%p ability=%u name=%s command=%u publish=%s accepted=%s progression_state=%d progression_readable=%s",
            hero,
            component,
            static_cast<unsigned int>(ability),
            Name(ability),
            static_cast<unsigned int>(command),
            publish ? "true" : "false",
            accepted ? "true" : "false",
            abilityProgressionState,
            abilityProgressionReadable ? "true" : "false");
        diagnostics_.Event(
            accepted
                ? "HeroWillAbilitySubmitted"
                : "HeroWillAbilityRejected",
            detail);
        return accepted;
    }

    void HeroWillAbilityService::Observe(
        void* component,
        HeroAbility ability,
        HeroAbilityCommand command) noexcept
    {
        std::array<SinkEntry, SinkCapacity> sinks = {};
        AcquireSRWLockShared(&sinkLock_);
        sinks = sinks_;
        ReleaseSRWLockShared(&sinkLock_);
        void* const source = native::HeroWillAbilityFunctions::ReadOwner(
            component);
        if (source == nullptr || entities_ == nullptr)
        {
            return;
        }

        void* target = nullptr;
        void* const targeting = entity::native::ThingComponentAccess::Find(
            source, entity::native::ThingComponentType::Targeting);
        creature::combat::native::HeroTargetingSnapshot targets;
        if (creature::combat::native::HeroTargetingComponent::ReadTargets(
                entities_->GameModule(), targeting, targets))
        {
            target = targets.selected != nullptr
                ? targets.selected
                : targets.candidatePrimary != nullptr
                    ? targets.candidatePrimary
                    : targets.candidateSecondary;
        }
        if (!creature::native::CreatureFrameFunctions::ValidateCreature(
                entities_->GameModule(), target) &&
            !creature::native::CreatureFrameFunctions::ValidatePlayerCreature(
                entities_->GameModule(), target))
        {
            target = nullptr;
        }

        HeroAbilityEvent event;
        event.sourceCreature = source;
        event.sourceThingUid = ReadThingUid(source);
        event.targetCreature = target;
        event.targetThingUid = ReadThingUid(target);
        event.ability = ability;
        event.command = command;
        if (!native::HeroWillAbilityFunctions::ReadAbilityProgressionState(
                component, ability, event.progressionState) ||
            event.progressionState < 0 || event.progressionState > 3)
        {
            char detail[160] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "source=%p ability=%u command=%u state=%d",
                source,
                static_cast<unsigned int>(ability),
                static_cast<unsigned int>(command),
                event.progressionState);
            diagnostics_.Event(
                "HeroWillAbilityObservationRejected", detail);
            return;
        }
        event.threadId = GetCurrentThreadId();
        event.observedAt = GetTickCount64();
        for (const SinkEntry& entry : sinks)
        {
            if (entry.sink != nullptr)
            {
                entry.sink(entry.context, event);
            }
        }
    }
}
