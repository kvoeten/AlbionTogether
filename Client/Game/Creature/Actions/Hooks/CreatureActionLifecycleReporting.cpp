#include "CreatureActionLifecycleObserver.h"
#include "Game/Creature/Actions/Native/CreatureActionInspector.h"
#include "Game/Creature/Actions/CreatureActionSubmissionContext.h"
#include "Game/Creature/Expression/Native/CreatureExpressionActionInspector.h"

#include <cstdio>
#include <cstring>
#include <iterator>

namespace fable::game::creature::actions
{
    bool CreatureActionLifecycleObserver::DescribeActionType(
        void* action,
        char* name,
        std::size_t capacity) noexcept
    {
        return native::CreatureActionInspector::DescribeType(
            action, name, capacity);
    }

    unsigned int CreatureActionLifecycleObserver::SubmissionCount() const noexcept
    {
        return submissionCount_.load(std::memory_order_acquire);
    }

    unsigned int CreatureActionLifecycleObserver::FinishCount() const noexcept
    {
        return finishCount_.load(std::memory_order_acquire);
    }

    void CreatureActionLifecycleObserver::ReportSubmission(
        void* creature,
        void* requestedAction,
        const char* requestedType,
        bool accepted,
        bool authorityDenied) noexcept
    {
        const unsigned int ordinal = submissionCount_.fetch_add(
            1,
            std::memory_order_acq_rel) + 1;

        void* const activeAction =
            native::CreatureActionInspector::ActiveAction(creature);
        char activeType[ActionNameCapacity] = {};
        ReadActionType(activeAction, activeType);
        const ThingContext context = ReadThingContext(creature);

        CreatureActionLifecycleEvent event;
        event.phase = CreatureActionLifecyclePhase::Submitted;
        event.thingUid = context.uid;
        event.observedAt = GetTickCount64();
        event.actionCorrelationId =
            ScopedActionSubmissionContext::CurrentCorrelationId(creature);
        event.mapId = context.mapId;
        event.threadId = GetCurrentThreadId();
        event.accepted = accepted;
        event.creature = creature;
        event.action = accepted && activeAction != nullptr
            ? activeAction
            : requestedAction;
        strncpy_s(
            event.actionType,
            accepted && activeType[0] != '\0'
                ? activeType
                : requestedType != nullptr ? requestedType : "",
            _TRUNCATE);
        event.animationId = accepted &&
                ActionMayCarryAnimation(event.actionType)
            ? ReadAnimationId(event.action)
            : 0;
        expression::native::CreatureExpressionActionDetails expression;
        const bool expressionRead = accepted &&
            expression::native::CreatureExpressionActionInspector::Inspect(
                gameModule_, event.action, event.actionType, expression);
        if (expressionRead)
        {
            strncpy_s(
                event.expressionName,
                expression.definition,
                _TRUNCATE);
            event.expressionDurationTicks = expression.durationTicks;
            event.expressionTriggerTicks = expression.triggerTicks;
            event.targetCreature = expression.target;
        }
        else
        {
            event.targetCreature = accepted
                ? ReadAttackTarget(gameModule_, event.action, event.actionType)
                : nullptr;
        }
        event.targetThingUid = ReadThingContext(event.targetCreature).uid;
        Notify(event);
        if (ordinal > DiagnosticEventLimit)
        {
            return;
        }

        char detail[640] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "ordinal=%u accepted=%s authority_denied=%s correlation_id=%llu thing_uid=%016llX map_id=%u creature=%p requested=%p requested_type=%s active=%p active_type=%s animation_id=%u expression=%s expression_duration_ticks=%d expression_trigger_ticks=%d target=%p target_uid=%016llX context_readable=%s thread=%lu",
            ordinal,
            accepted ? "true" : "false",
            authorityDenied ? "true" : "false",
            static_cast<unsigned long long>(event.actionCorrelationId),
            static_cast<unsigned long long>(context.uid),
            static_cast<unsigned int>(context.mapId),
            creature,
            requestedAction,
            requestedType != nullptr && requestedType[0] != '\0' ? requestedType : "<unknown>",
            activeAction,
            activeType[0] != '\0' ? activeType : "<unknown>",
            event.animationId,
            event.expressionName[0] != '\0'
                ? event.expressionName
                : "<none>",
            event.expressionDurationTicks,
            event.expressionTriggerTicks,
            event.targetCreature,
            static_cast<unsigned long long>(event.targetThingUid),
            context.readable ? "true" : "false",
            static_cast<unsigned long>(GetCurrentThreadId()));
        diagnostics_.Event("CreatureActionSubmitted", detail);
    }

    void CreatureActionLifecycleObserver::ReportFinish(
        void* action,
        void* creature,
        const char* actionType) noexcept
    {
        const unsigned int ordinal = finishCount_.fetch_add(
            1,
            std::memory_order_acq_rel) + 1;

        const ThingContext context = ReadThingContext(creature);
        CreatureActionLifecycleEvent event;
        event.phase = CreatureActionLifecyclePhase::Finished;
        event.thingUid = context.uid;
        event.observedAt = GetTickCount64();
        event.mapId = context.mapId;
        event.threadId = GetCurrentThreadId();
        event.accepted = true;
        event.creature = creature;
        event.action = action;
        strncpy_s(
            event.actionType,
            actionType != nullptr ? actionType : "",
            _TRUNCATE);
        Notify(event);
        if (ordinal > DiagnosticEventLimit)
        {
            return;
        }
        char detail[512] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "ordinal=%u thing_uid=%016llX map_id=%u creature=%p action=%p action_type=%s context_readable=%s thread=%lu",
            ordinal,
            static_cast<unsigned long long>(context.uid),
            static_cast<unsigned int>(context.mapId),
            creature,
            action,
            actionType != nullptr && actionType[0] != '\0' ? actionType : "<unknown>",
            context.readable ? "true" : "false",
            static_cast<unsigned long>(GetCurrentThreadId()));
        diagnostics_.Event("CreatureActionFinished", detail);
    }

    bool CreatureActionLifecycleObserver::ReadActionType(
        void* action,
        char (&name)[ActionNameCapacity]) noexcept
    {
        return DescribeActionType(action, name, ActionNameCapacity);
    }

    std::uint32_t CreatureActionLifecycleObserver::ReadAnimationId(
        void* action) noexcept
    {
        return native::CreatureActionInspector::AnimationId(action);
    }

    std::uint32_t CreatureActionLifecycleObserver::
        DescribeActionAnimationId(void* action) noexcept
    {
        return ReadAnimationId(action);
    }

    void* CreatureActionLifecycleObserver::ReadAttackTarget(
        HMODULE gameModule,
        void* action,
        const char* actionType) noexcept
    {
        return native::CreatureActionInspector::AttackTarget(
            gameModule, action, actionType);
    }

    bool CreatureActionLifecycleObserver::ActionMayCarryAnimation(
        const char* actionType) noexcept
    {
        return native::CreatureActionInspector::MayCarryAnimation(actionType);
    }

    void* CreatureActionLifecycleObserver::ResolveActionOwner(void* action) noexcept
    {
        return native::CreatureActionInspector::Owner(action);
    }

    CreatureActionLifecycleObserver::ThingContext
        CreatureActionLifecycleObserver::ReadThingContext(void* creature) noexcept
    {
        ThingContext context;
        __try
        {
            if (creature != nullptr)
            {
                const auto* const bytes = static_cast<const std::uint8_t*>(creature);
                context.uid = *reinterpret_cast<const std::uint64_t*>(bytes + 0x14);
                context.mapId = *reinterpret_cast<const std::uint16_t*>(bytes + 0x9A);
                context.readable = true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            context = {};
        }
        return context;
    }
}
