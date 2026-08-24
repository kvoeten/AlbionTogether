#include "CreatureCombatService.h"

#include "Game/Creature/Actions/Native/CreatureActionFunctions.h"
#include "Game/Creature/Actions/Hooks/CreatureActionLifecycleObserver.h"
#include "Game/Creature/Combat/Native/CreatureDeathFunctions.h"
#include "Game/Creature/Animation/CreatureAnimationService.h"
#include "Game/Creature/Combat/Native/HeroTargetingComponent.h"
#include "Game/Creature/Combat/Native/CreatureHitReactionFunctions.h"
#include "Game/Creature/Native/CreatureFrameFunctions.h"
#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"
#include "Game/Entity/Native/ThingComponentAccess.h"

#include <Windows.h>

#include <cstdio>

namespace
{
    class AuthoritativeReplayScope final
    {
    public:
        AuthoritativeReplayScope() noexcept
        {
            fable::game::creature::actions::
                CreatureActionLifecycleObserver::BeginAuthoritativeReplay();
        }

        ~AuthoritativeReplayScope()
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

namespace fable::game::creature::combat
{
    CreatureCombatService::~CreatureCombatService()
    {
        Shutdown();
    }

    void CreatureCombatService::Shutdown() noexcept
    {
        ClearPlayerCombat();
        SetHealthMutationSink(nullptr, nullptr);
        AcquireSRWLockExclusive(&abilitySinkLock_);
        abilitySinks_ = {};
        ReleaseSRWLockExclusive(&abilitySinkLock_);
        AcquireSRWLockExclusive(&resolvedHitSinkLock_);
        resolvedHitSinks_ = {};
        ReleaseSRWLockExclusive(&resolvedHitSinkLock_);
        creatureHitResolutionHook_.Shutdown();
        combatHealthMutationHook_.Shutdown();
        playerAttackAbilityHook_.Shutdown();
        actionAnimationSelectionHook_.Shutdown();
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
        const bool animationSelectionHookInstalled =
            actionAnimationSelectionHook_.Install(
                entities.GameModule(), diagnostics_);
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
        diagnostics_.Event(
            "CreatureCombatAbiValidated",
            attackHookInstalled && healthHookInstalled && hitHookInstalled &&
                    animationSelectionHookInstalled
                ? "CThingCreature ability submission, player ATTACK caller, shared health mutation, native OnHit resolution, and action animation selection validated"
                : "one or more CThingCreature combat definitions failed validation");
        diagnostics_.Log(attackHookInstalled
            ? "Creature combat: deep native player ATTACK-to-creature ability routing validated."
            : "Creature combat: current-build player ATTACK ability routing definition failed validation.");
        if (!attackHookInstalled || !healthHookInstalled || !hitHookInstalled ||
            !animationSelectionHookInstalled)
        {
            Shutdown();
            return false;
        }
        return true;
    }

    bool CreatureCombatService::AttachActionLifecycleObserver(
        actions::CreatureActionLifecycleObserver& observer) noexcept
    {
        return actionAnimationSelectionHook_.AttachActionLifecycleObserver(
            observer);
    }

    void CreatureCombatService::DetachActionLifecycleObserver() noexcept
    {
        actionAnimationSelectionHook_.DetachActionLifecycleObserver();
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

    void CreatureCombatService::ObservePlayerAbility(
        void* sourceCreature,
        unsigned int abilityId,
        float charge,
        bool attackCommand) noexcept
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
                "event=%u source=%p source_uid=%016llX targeting=%p read=%s selected=%p primary=%p secondary=%p accepted_target=%p target_uid=%016llX ability=%u charge=%.3f attack_command=%s",
                observed,
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

    bool CreatureCombatService::SubmitReplicatedAbility(
        void* creature,
        unsigned int abilityId,
        float charge) noexcept
    {
        return SubmitReplicatedAbility(
            creature, abilityId, charge, nullptr, 0);
    }

    bool CreatureCombatService::SubmitReplicatedAbility(
        void* creature,
        unsigned int abilityId,
        float charge,
        const char* resolvedActionType,
        const std::uint32_t resolvedAnimationId) noexcept
    {
        if (entities_ == nullptr)
        {
            return false;
        }
        const HMODULE gameModule = entities_->GameModule();
        const bool creatureValid =
            ::fable::game::creature::native::CreatureFrameFunctions::
                ValidateCreature(gameModule, creature) ||
            ::fable::game::creature::native::CreatureFrameFunctions::
                ValidatePlayerCreature(gameModule, creature);
        if (!creatureValid)
        {
            return false;
        }
        const AuthoritativeReplayScope replay;
        const bool selectionArmed = resolvedActionType != nullptr &&
            resolvedActionType[0] != '\0' && resolvedAnimationId != 0 &&
            actionAnimationSelectionHook_.BeginSelection(
                creature, resolvedActionType, resolvedAnimationId);
        const bool receiptArmed = actions::CreatureActionLifecycleObserver::
            BeginSubmissionReceipt(creature);
        const bool invoked = playerAttackAbilityHook_.SubmitReplicatedAbility(
            creature, abilityId, charge);
        if (selectionArmed)
        {
            actionAnimationSelectionHook_.EndSelection();
        }
        bool accepted = false;
        const bool submissionObserved = receiptArmed &&
            actions::CreatureActionLifecycleObserver::EndSubmissionReceipt(
                creature, accepted);
        return invoked && (!receiptArmed ||
            (submissionObserved && accepted));
    }

    bool CreatureCombatService::SubmitReplicatedImmediateAttack(
        void* creature,
        void* targetCreature) noexcept
    {
        if (entities_ == nullptr || creature == nullptr ||
            targetCreature == nullptr)
        {
            return false;
        }
        const HMODULE gameModule = entities_->GameModule();
        const bool creatureValid =
            ::fable::game::creature::native::CreatureFrameFunctions::
                ValidateCreature(gameModule, creature) ||
            ::fable::game::creature::native::CreatureFrameFunctions::
                ValidatePlayerCreature(gameModule, creature);
        const bool targetValid =
            ::fable::game::creature::native::CreatureFrameFunctions::
                ValidateCreature(gameModule, targetCreature) ||
            ::fable::game::creature::native::CreatureFrameFunctions::
                ValidatePlayerCreature(gameModule, targetCreature);
        if (!creatureValid || !targetValid)
        {
            return false;
        }
        {
            const AuthoritativeReplayScope replay;
            if (::fable::game::creature::actions::native::
                    CreatureActionFunctions::SubmitImmediateAttack(
                        gameModule, creature, targetCreature))
            {
                return true;
            }
        }

        // A publisher hand-off can leave the pre-handoff action in +0x120.
        // Its updates are fenced, so it cannot finish on its own. Retire only
        // that locally-originated action and resubmit through Fable's normal
        // action replacement boundary.
        if (!::fable::game::creature::actions::
                CreatureActionLifecycleObserver::
                    RetireLocalActionForAuthoritativeReplay(creature))
        {
            return false;
        }
        const AuthoritativeReplayScope replay;
        return ::fable::game::creature::actions::native::
            CreatureActionFunctions::SubmitImmediateAttack(
                gameModule, creature, targetCreature);
    }

    bool CreatureCombatService::SubmitAuthoritativeImmediateAttack(
        void* creature,
        void* targetCreature) noexcept
    {
        if (entities_ == nullptr || creature == nullptr ||
            targetCreature == nullptr)
        {
            return false;
        }
        const HMODULE gameModule = entities_->GameModule();
        const auto validCombatant = [gameModule](void* candidate) noexcept
        {
            return ::fable::game::creature::native::CreatureFrameFunctions::
                    ValidateCreature(gameModule, candidate) ||
                ::fable::game::creature::native::CreatureFrameFunctions::
                    ValidatePlayerCreature(gameModule, candidate);
        };
        if (!validCombatant(creature) || !validCombatant(targetCreature))
        {
            return false;
        }

        return ::fable::game::creature::actions::native::
            CreatureActionFunctions::SubmitImmediateAttack(
                gameModule, creature, targetCreature);
    }

    bool CreatureCombatService::SubmitReplicatedHitReaction(
        void* target,
        void* source,
        const float (&position)[3],
        const float (&direction)[3],
        bool knockdown) noexcept
    {
        return SubmitReplicatedHitReaction(
            target, source, position, direction, knockdown, 0);
    }

    bool CreatureCombatService::SubmitReplicatedHitReaction(
        void* target,
        void* source,
        const float (&position)[3],
        const float (&direction)[3],
        bool knockdown,
        const std::uint32_t resolvedAnimationId) noexcept
    {
        if (entities_ == nullptr || target == nullptr || source == nullptr)
        {
            return false;
        }
        const HMODULE gameModule = entities_->GameModule();
        const auto validCombatant = [gameModule](void* candidate) noexcept
        {
            return ::fable::game::creature::native::CreatureFrameFunctions::
                    ValidateCreature(gameModule, candidate) ||
                ::fable::game::creature::native::CreatureFrameFunctions::
                    ValidatePlayerCreature(gameModule, candidate);
        };
        if (!validCombatant(target) || !validCombatant(source))
        {
            return false;
        }

        const AuthoritativeReplayScope replay;
        const char* const reactionType = knockdown
            ? "CCombatAction_GenericStrikeResponseKnockdown"
            : "CCombatAction_GenericStrikeResponse";
        const bool selectionArmed = resolvedAnimationId != 0 &&
            actionAnimationSelectionHook_.BeginSelection(
                target, reactionType, resolvedAnimationId);
        const bool receiptArmed = actions::CreatureActionLifecycleObserver::
            BeginSubmissionReceipt(target);
        const native::CreatureHitReactionFunctions::SubmissionResult result =
            native::CreatureHitReactionFunctions::Submit(
            gameModule,
            target,
            source,
            position,
            direction,
            knockdown);
        if (selectionArmed)
        {
            actionAnimationSelectionHook_.EndSelection();
        }
        if (result.accepted && !result.cleanupSucceeded)
        {
            diagnostics_.Event(
                "CreatureHitReactionAcceptedWithCleanupFault",
                "native reaction accepted; stack-input cleanup fault is non-retryable");
        }
        bool accepted = false;
        const bool submissionObserved = receiptArmed &&
            actions::CreatureActionLifecycleObserver::EndSubmissionReceipt(
                target,
                accepted);
        const bool receiptAccepted = submissionObserved && accepted;
        // Either positive signal is terminal. The direct return is the native
        // SubmitAction result; the observer receipt independently confirms an
        // accepted detoured submission. Never retry an action after either
        // boundary says Fable accepted it.
        const bool submitted = result.invoked &&
            (result.accepted || receiptAccepted);
        const bool animationPlayed = submitted && resolvedAnimationId != 0 &&
            animation_ != nullptr && animation_->PlayAuthoritative(
                target, resolvedAnimationId);
        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "target_uid=%016llX source_uid=%016llX knockdown=%s accepted=%s animation_id=%u animation_played=%s",
            static_cast<unsigned long long>(ReadThingUid(target)),
            static_cast<unsigned long long>(ReadThingUid(source)),
            knockdown ? "true" : "false",
            submitted ? "true" : "false",
            resolvedAnimationId,
            animationPlayed ? "true" : "false");
        diagnostics_.Event("MultiplayerCombatHitReactionSubmitted", detail);
        return submitted;
    }

    bool CreatureCombatService::SubmitReplicatedDeath(void* creature) noexcept
    {
        if (entities_ == nullptr || creature == nullptr)
        {
            return false;
        }
        const HMODULE gameModule = entities_->GameModule();
        if (!::fable::game::creature::native::CreatureFrameFunctions::
                ValidateCreature(gameModule, creature) &&
            !::fable::game::creature::native::CreatureFrameFunctions::
                ValidatePlayerCreature(gameModule, creature))
        {
            return false;
        }

        const AuthoritativeReplayScope replay;
        const bool receiptArmed = actions::CreatureActionLifecycleObserver::
            BeginSubmissionReceipt(creature);
        const native::CreatureDeathFunctions::SubmissionResult result =
            native::CreatureDeathFunctions::Submit(gameModule, creature);
        bool receiptAccepted = false;
        const bool submissionObserved = receiptArmed &&
            actions::CreatureActionLifecycleObserver::EndSubmissionReceipt(
                creature, receiptAccepted);
        const bool accepted = result.invoked &&
            (result.accepted || (submissionObserved && receiptAccepted));
        if (accepted && !result.cleanupSucceeded)
        {
            diagnostics_.Event(
                "CreatureDeathAcceptedWithCleanupFault",
                "native death accepted; stack-input cleanup fault is non-retryable");
        }

        char detail[192] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "thing_uid=%016llX accepted=%s",
            static_cast<unsigned long long>(ReadThingUid(creature)),
            accepted ? "true" : "false");
        diagnostics_.Event("MultiplayerCreatureDeathSubmitted", detail);
        return accepted;
    }

    bool CreatureCombatService::SubmitReplicatedUntargetedAttack(
        void* creature,
        const float (&targetPosition)[3]) noexcept
    {
        return SubmitReplicatedUntargetedAttack(
            creature, targetPosition, nullptr, 0);
    }

    bool CreatureCombatService::SubmitReplicatedUntargetedAttack(
        void* creature,
        const float (&targetPosition)[3],
        const char* resolvedActionType,
        const std::uint32_t resolvedAnimationId) noexcept
    {
        if (entities_ == nullptr || creature == nullptr)
        {
            return false;
        }
        const HMODULE gameModule = entities_->GameModule();
        const bool creatureValid =
            ::fable::game::creature::native::CreatureFrameFunctions::
                ValidateCreature(gameModule, creature) ||
            ::fable::game::creature::native::CreatureFrameFunctions::
                ValidatePlayerCreature(gameModule, creature);
        if (!creatureValid)
        {
            return false;
        }
        const AuthoritativeReplayScope replay;
        const bool selectionArmed = resolvedActionType != nullptr &&
            resolvedActionType[0] != '\0' && resolvedAnimationId != 0 &&
            actionAnimationSelectionHook_.BeginSelection(
                creature, resolvedActionType, resolvedAnimationId);
        const bool submitted = ::fable::game::creature::actions::native::
            CreatureActionFunctions::SubmitUntargetedAttack(
                gameModule, creature, targetPosition);
        if (selectionArmed)
        {
            actionAnimationSelectionHook_.EndSelection();
        }
        return submitted;
    }

    void CreatureCombatService::SetHealthMutationSink(
        HealthMutationSink sink,
        void* context) noexcept
    {
        combatHealthMutationHook_.SetEventSink(sink, context);
    }

    void CreatureCombatService::CaptureResolvedHit(
        void* context,
        const ResolvedHitEvent& event) noexcept
    {
        auto* const service = static_cast<CreatureCombatService*>(context);
        if (service == nullptr)
        {
            return;
        }
        std::array<ResolvedHitSinkEntry, ResolvedHitSinkCapacity> sinks = {};
        AcquireSRWLockShared(&service->resolvedHitSinkLock_);
        sinks = service->resolvedHitSinks_;
        ReleaseSRWLockShared(&service->resolvedHitSinkLock_);
        for (const ResolvedHitSinkEntry& entry : sinks)
        {
            if (entry.sink != nullptr)
            {
                entry.sink(entry.context, event);
            }
        }
    }

    bool CreatureCombatService::AddResolvedHitSink(
        ResolvedHitSink sink,
        void* context) noexcept
    {
        if (sink == nullptr)
        {
            return false;
        }
        AcquireSRWLockExclusive(&resolvedHitSinkLock_);
        for (const ResolvedHitSinkEntry& entry : resolvedHitSinks_)
        {
            if (entry.sink == sink && entry.context == context)
            {
                ReleaseSRWLockExclusive(&resolvedHitSinkLock_);
                return true;
            }
        }
        for (ResolvedHitSinkEntry& entry : resolvedHitSinks_)
        {
            if (entry.sink == nullptr)
            {
                entry = {sink, context};
                ReleaseSRWLockExclusive(&resolvedHitSinkLock_);
                return true;
            }
        }
        ReleaseSRWLockExclusive(&resolvedHitSinkLock_);
        return false;
    }

    void CreatureCombatService::RemoveResolvedHitSink(
        ResolvedHitSink sink,
        void* context) noexcept
    {
        AcquireSRWLockExclusive(&resolvedHitSinkLock_);
        for (ResolvedHitSinkEntry& entry : resolvedHitSinks_)
        {
            if (entry.sink == sink && entry.context == context)
            {
                entry = {};
            }
        }
        ReleaseSRWLockExclusive(&resolvedHitSinkLock_);
    }

    bool CreatureCombatService::SetReplicaHealthProtection(
        void* creature,
        bool protectedReplica) noexcept
    {
        return combatHealthMutationHook_.SetReplicaProtected(
            creature, protectedReplica);
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

    bool CreatureCombatService::ApplyOwnedCombatDamage(
        void* creature,
        const float damage) noexcept
    {
        return combatHealthMutationHook_.ApplyOwnedCombatDamage(
            creature, damage);
    }
}
