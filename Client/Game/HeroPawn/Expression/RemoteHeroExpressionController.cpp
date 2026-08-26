#include "RemoteHeroExpressionController.h"

#include "Game/Creature/Actions/Hooks/CreatureActionLifecycleObserver.h"
#include "Game/Creature/Animation/CreatureAnimationService.h"
#include "Game/Creature/Expression/Native/CreatureExpressionActionFunctions.h"
#include "Game/Creature/Native/CreatureFrameFunctions.h"
#include "Game/Entity/EntityService.h"

#include <cstdio>

namespace fable::game::hero_pawn::expression
{
    void RemoteHeroExpressionController::Initialize(
        game::EntityService& entities,
        game::creature::animation::CreatureAnimationService& animation,
        const core::Diagnostics& diagnostics) noexcept
    {
        entities_ = &entities;
        animation_ = &animation;
        diagnostics_ = diagnostics;
    }

    bool RemoteHeroExpressionController::Perform(
        void* performer,
        void* target,
        const std::string& expressionDefinition,
        const std::string& resolvedActionType,
        const std::uint32_t resolvedAnimationId,
        const std::int32_t durationTicks,
        const std::int32_t triggerTicks)
    {
        if (entities_ == nullptr || animation_ == nullptr ||
            performer == nullptr ||
            expressionDefinition.empty())
        {
            return false;
        }

        const HMODULE gameModule = entities_->GameModule();
        if (!game::creature::native::CreatureFrameFunctions::ValidateCreature(
                gameModule, performer) &&
            !game::creature::native::CreatureFrameFunctions::
                ValidatePlayerCreature(gameModule, performer))
        {
            return false;
        }

        using game::creature::actions::CreatureActionLifecycleObserver;
        CreatureActionLifecycleObserver::BeginAuthoritativeReplay();
        const bool receiptArmed = CreatureActionLifecycleObserver::
            BeginSubmissionReceipt(performer);
        const auto result = game::creature::expression::native::
            CreatureExpressionActionFunctions::Submit(
                gameModule,
                performer,
                target,
                expressionDefinition.c_str(),
                resolvedAnimationId,
                durationTicks,
                triggerTicks);
        CreatureActionLifecycleObserver::EndAuthoritativeReplay();

        bool receiptAccepted = false;
        const bool submissionObserved = receiptArmed &&
            CreatureActionLifecycleObserver::EndSubmissionReceipt(
                performer, receiptAccepted);
        const bool accepted = result.invoked &&
            (result.accepted || (submissionObserved && receiptAccepted));
        if (accepted && !result.cleanupSucceeded)
        {
            diagnostics_.Event(
                "CreatureExpressionAcceptedWithCleanupFault",
                "native expression accepted; stack-input cleanup fault is non-retryable");
        }

        char detail[512] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "expression=%s action=%s performer=%p target=%p definition_resolved=%s animation_overridden=%s local_animation_id=%u invoked=%s accepted=%s animation_id=%u duration_ticks=%d trigger_ticks=%d receipt_observed=%s receipt_accepted=%s",
            expressionDefinition.c_str(),
            resolvedActionType.c_str(),
            performer,
            target,
            result.definitionResolved ? "true" : "false",
            result.animationOverridden ? "true" : "false",
            result.locallySelectedAnimationId,
            result.invoked ? "true" : "false",
            accepted ? "true" : "false",
            resolvedAnimationId,
            durationTicks,
            triggerTicks,
            submissionObserved ? "true" : "false",
            receiptAccepted ? "true" : "false");
        // A bad semantic definition is retried by the ordered action replay
        // queue. Do not turn that bounded retry into per-frame log spam.
        if (result.definitionResolved || accepted)
        {
            diagnostics_.Event(
                "MultiplayerRemoteExpressionNativeSubmit", detail);
        }
        return accepted;
    }

    void RemoteHeroExpressionController::Shutdown() noexcept
    {
        entities_ = nullptr;
        animation_ = nullptr;
        diagnostics_ = {};
    }
}
