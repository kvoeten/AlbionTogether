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
        const actions::ScopedActionSubmissionOrigin replay{
            actions::ActionSubmissionOrigin::ReplicatedPresentation};
        const bool selectionArmed = resolvedActionType != nullptr &&
            resolvedActionType[0] != '\0' && resolvedAnimationId != 0 &&
            animation_->BeginReplicatedActionSelection(
                creature, resolvedActionType, resolvedAnimationId);
        const bool receiptArmed = actions::CreatureActionLifecycleObserver::
            BeginSubmissionReceipt(creature);
        const bool invoked = playerAttackAbilityHook_.SubmitReplicatedAbility(
            creature, abilityId, charge);
        if (selectionArmed)
        {
            animation_->EndReplicatedActionSelection();
        }
        bool accepted = false;
        const bool submissionObserved = receiptArmed &&
            actions::CreatureActionLifecycleObserver::EndSubmissionReceipt(
                creature, accepted);
        return invoked && (!receiptArmed ||
            (submissionObserved && accepted));
    }

    bool CreatureCombatService::SubmitReplicatedRangedFire(
        void* creature,
        void* rangedWeapon,
        const char* resolvedActionType,
        const std::uint32_t resolvedAnimationId) noexcept
    {
        return SubmitReplicatedRangedAction(
            creature,
            rangedWeapon,
            resolvedActionType,
            resolvedAnimationId,
            false);
    }

    bool CreatureCombatService::SubmitReplicatedRangedAim(
        void* creature,
        void* rangedWeapon,
        const char* resolvedActionType,
        const std::uint32_t resolvedAnimationId) noexcept
    {
        return SubmitReplicatedRangedAction(
            creature,
            rangedWeapon,
            resolvedActionType,
            resolvedAnimationId,
            true);
    }

    bool CreatureCombatService::SubmitReplicatedRangedAction(
        void* creature,
        void* rangedWeapon,
        const char* resolvedActionType,
        const std::uint32_t resolvedAnimationId,
        const bool aimStage) noexcept
    {
        if (entities_ == nullptr || creature == nullptr ||
            rangedWeapon == nullptr)
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

        const actions::ScopedActionSubmissionOrigin replay{
            actions::ActionSubmissionOrigin::ReplicatedPresentation};
        const bool selectionArmed = resolvedActionType != nullptr &&
            resolvedActionType[0] != '\0' && resolvedAnimationId != 0 &&
            animation_->BeginReplicatedActionSelection(
                creature, resolvedActionType, resolvedAnimationId);
        const bool receiptArmed = actions::CreatureActionLifecycleObserver::
            BeginSubmissionReceipt(creature);
        const auto result = aimStage
            ? actions::native::CreatureActionFunctions::SubmitRangedAim(
                gameModule, creature, rangedWeapon)
            : actions::native::CreatureActionFunctions::SubmitRangedFire(
                gameModule, creature, rangedWeapon);
        if (selectionArmed)
        {
            animation_->EndReplicatedActionSelection();
        }
        bool receiptAccepted = false;
        const bool submissionObserved = receiptArmed &&
            actions::CreatureActionLifecycleObserver::EndSubmissionReceipt(
                creature, receiptAccepted);
        const bool accepted = result.accepted && (!receiptArmed ||
            (submissionObserved && receiptAccepted));
        if (accepted && !result.cleanupSucceeded)
        {
            diagnostics_.Event(
                aimStage
                    ? "CreatureRangedAimAcceptedWithCleanupFault"
                    : "CreatureRangedFireAcceptedWithCleanupFault",
                aimStage
                    ? "native HeroLoadRangedWeapon accepted; caller action cleanup faulted without retry"
                    : "native FireMissileWeapon accepted; caller action cleanup faulted without retry");
        }

        // The remote Hero accepts the native ranged action and advances the
        // carried weapon, but unlike the local player path it does not ask its
        // AnimationComplex to play the action's body animation. Submit that
        // exact owner-selected animation while the native action is active.
        // Animation failure must not retry an already accepted action: doing
        // so would duplicate arrows and weapon state transitions.
        const bool bodyAnimationPlayed = accepted &&
            resolvedAnimationId != 0 && animation_ != nullptr &&
            animation_->PlayAuthoritative(creature, resolvedAnimationId);
        if (accepted && !bodyAnimationPlayed)
        {
            char detail[224] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "stage=%s creature=%p animation_id=%u",
                aimStage ? "aim" : "fire",
                creature,
                resolvedAnimationId);
            diagnostics_.Event("CreatureRangedBodyAnimationFailed", detail);
        }
        return accepted;
    }

}
