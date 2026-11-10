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
            const actions::ScopedActionSubmissionOrigin replay{
                actions::ActionSubmissionOrigin::ReplicatedPresentation};
            if (::fable::game::creature::actions::native::
                    CreatureActionFunctions::SubmitImmediateAttack(
                        gameModule, creature, targetCreature))
            {
                return true;
            }
        }

        // A publisher hand-off can leave the pre-handoff action active.
        // Its updates are fenced, so it cannot finish on its own. Retire only
        // that locally-originated action and resubmit through Fable's normal
        // action replacement boundary.
        if (!::fable::game::creature::actions::
                CreatureActionLifecycleObserver::
                    RetireLocalActionForReplicatedReplay(creature))
        {
            return false;
        }
        const actions::ScopedActionSubmissionOrigin replay{
            actions::ActionSubmissionOrigin::ReplicatedPresentation};
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

        const actions::ScopedActionSubmissionOrigin replay{
            actions::ActionSubmissionOrigin::ReplicatedPresentation};
        const char* const reactionType = knockdown
            ? "CCombatAction_GenericStrikeResponseKnockdown"
            : "CCombatAction_GenericStrikeResponse";
        const bool selectionArmed = resolvedAnimationId != 0 &&
            animation_->BeginReplicatedActionSelection(
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
            animation_->EndReplicatedActionSelection();
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

        const actions::ScopedActionSubmissionOrigin replay{
            actions::ActionSubmissionOrigin::ReplicatedPresentation};
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
        const actions::ScopedActionSubmissionOrigin replay{
            actions::ActionSubmissionOrigin::ReplicatedPresentation};
        const bool selectionArmed = resolvedActionType != nullptr &&
            resolvedActionType[0] != '\0' && resolvedAnimationId != 0 &&
            animation_->BeginReplicatedActionSelection(
                creature, resolvedActionType, resolvedAnimationId);
        const bool submitted = ::fable::game::creature::actions::native::
            CreatureActionFunctions::SubmitUntargetedAttack(
                gameModule, creature, targetPosition);
        if (selectionArmed)
        {
            animation_->EndReplicatedActionSelection();
        }
        return submitted;
    }

}
