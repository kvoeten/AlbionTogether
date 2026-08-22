#include "PillarAbilityLifecycleHook.h"

#include "Game/Entity/Native/ThingComponentAccess.h"

#include <cstdio>
#include <cstring>

namespace
{
    constexpr char BuildUpDivine[] =
        "CCreatureAction_BuildUpDivineWrathSpell";
    constexpr char CastDivine[] =
        "CCreatureAction_CastDivineWrathSpell";
    constexpr char BuildUpUnholy[] =
        "CCreatureAction_BuildUpUnholyPowerSpell";
    constexpr char CastUnholy[] =
        "CCreatureAction_CastUnholyPowerSpell";
    constexpr char DivineToken[] = "DivineWrathSpell";
    constexpr char UnholyToken[] = "UnholyPowerSpell";
    constexpr char DivineComponent[] = "CTCDivineWrath";
    constexpr char UnholyComponent[] = "CTCUnholyPower";
    constexpr std::size_t CreatureActiveActionOffset = 0x120;
    constexpr std::size_t ActionFinishedOffset = 0x61;
    constexpr std::size_t ActionCloneVtableOffset = 0x2C;
}

namespace fable::game::hero_pawn::abilities::hooks
{
    PillarAbilityLifecycleHook::~PillarAbilityLifecycleHook()
    {
        auto* const observer = observer_;
        observer_ = nullptr;
        if (observer != nullptr)
        {
            observer->RemovePostUpdateSink(&OnPostUpdate, this);
            observer->RemoveEventSink(&OnActionEvent, this);
        }
        ReleaseAllPending();
    }

    bool PillarAbilityLifecycleHook::Install(
        game::creature::actions::CreatureActionLifecycleObserver& observer,
        const core::Diagnostics& diagnostics)
    {
        if (IsInstalled())
        {
            return true;
        }
        if (observer_ != nullptr || !observer.IsInstalled())
        {
            return false;
        }

        diagnostics_ = diagnostics;
        observer_ = &observer;
        if (!observer.AddEventSink(&OnActionEvent, this) ||
            !observer.AddPostUpdateSink(&OnPostUpdate, this))
        {
            observer.RemovePostUpdateSink(&OnPostUpdate, this);
            observer.RemoveEventSink(&OnActionEvent, this);
            observer_ = nullptr;
            diagnostics_.Event(
                "PillarAbilityLifecycleHookRejected",
                "creature action lifecycle subscriptions were unavailable");
            return false;
        }

        diagnostics_.Event(
            "PillarAbilityLifecycleHookReady",
            "policy=actor-scoped-native-buildup-retry lifecycle=Cast-BuildUp native-effect=CTC-owned");
        return true;
    }

    bool PillarAbilityLifecycleHook::IsInstalled() const noexcept
    {
        return observer_ != nullptr && observer_->IsInstalled();
    }

    void PillarAbilityLifecycleHook::OnActionEvent(
        void* context,
        const game::creature::actions::CreatureActionLifecycleEvent& event)
    {
        auto* const hook = static_cast<PillarAbilityLifecycleHook*>(context);
        if (hook != nullptr)
        {
            hook->ReportPillarComponentState(event);
            hook->CaptureRejectedBuildUp(event);
        }
    }

    void PillarAbilityLifecycleHook::OnPostUpdate(
        void* context,
        game::creature::actions::CreatureActionLifecycleObserver& observer,
        void* creature)
    {
        auto* const hook = static_cast<PillarAbilityLifecycleHook*>(context);
        if (hook != nullptr)
        {
            hook->RetryAfterActionUpdate(observer, creature);
        }
    }

    void PillarAbilityLifecycleHook::CaptureRejectedBuildUp(
        const game::creature::actions::CreatureActionLifecycleEvent& event)
        noexcept
    {
        if (event.phase !=
                game::creature::actions::CreatureActionLifecyclePhase::Submitted ||
            event.accepted || event.creature == nullptr ||
            event.action == nullptr)
        {
            return;
        }

        const PillarKind kind = ResolveBuildUp(event.actionType);
        if (kind == PillarKind::None)
        {
            return;
        }
        void* const activeAction = ReadActiveAction(event.creature);
        char activeType[ActionNameCapacity] = {};
        game::creature::actions::CreatureActionLifecycleObserver::
            DescribeActionType(
                activeAction, activeType, std::size(activeType));
        if (!MatchesCast(kind, activeType))
        {
            return;
        }

        AcquireSRWLockShared(&pendingLock_);
        for (const PendingAction& pending : pending_)
        {
            if (pending.creature == event.creature && pending.kind == kind)
            {
                ReleaseSRWLockShared(&pendingLock_);
                return;
            }
        }
        ReleaseSRWLockShared(&pendingLock_);

        void* const clone = CloneAction(event.action);
        if (clone == nullptr)
        {
            diagnostics_.Event(
                "PillarAbilityBuildUpDeferralFailed",
                "native action clone returned null");
            return;
        }

        const bool authoritativeReplay = observer_ != nullptr &&
            observer_->IsAuthoritativeReplayAction(activeAction);
        bool stored = false;
        bool duplicate = false;
        AcquireSRWLockExclusive(&pendingLock_);
        PendingAction* empty = nullptr;
        for (PendingAction& pending : pending_)
        {
            if (pending.creature == event.creature && pending.kind == kind)
            {
                duplicate = true;
                empty = nullptr;
                break;
            }
            if (empty == nullptr && pending.action == nullptr)
            {
                empty = &pending;
            }
        }
        if (empty != nullptr)
        {
            *empty = {
                event.creature,
                clone,
                event.thingUid,
                GetTickCount64(),
                kind,
                authoritativeReplay,
                false};
            stored = true;
        }
        ReleaseSRWLockExclusive(&pendingLock_);

        if (!stored)
        {
            DestroyAction(clone);
            diagnostics_.Event(
                "PillarAbilityBuildUpDeferralFailed",
                duplicate
                    ? "actor already had a pending pillar action"
                    : "pending pillar action capacity was exhausted");
            return;
        }

        const unsigned int ordinal = diagnosticCount_.fetch_add(
            1, std::memory_order_relaxed) + 1;
        if (ordinal <= DiagnosticLimit)
        {
            char detail[320] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "ordinal=%u ability=%s thing_uid=%016llX creature=%p action=%p active=%p active_type=%s replicated=%s thread=%lu",
                ordinal,
                Name(kind),
                static_cast<unsigned long long>(event.thingUid),
                event.creature,
                clone,
                activeAction,
                activeType[0] != '\0' ? activeType : "<unknown>",
                authoritativeReplay ? "true" : "false",
                GetCurrentThreadId());
            diagnostics_.Event("PillarAbilityBuildUpDeferred", detail);
        }
    }

    void PillarAbilityLifecycleHook::ReportPillarComponentState(
        const game::creature::actions::CreatureActionLifecycleEvent& event)
        noexcept
    {
        const PillarKind kind = ResolvePillarAction(event.actionType);
        if (kind == PillarKind::None || event.creature == nullptr)
        {
            return;
        }

        void* const component = FindPillarComponent(event.creature, kind);
        if (component == nullptr)
        {
            char detail[224] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "ability=%s phase=%u accepted=%s action_type=%s component=<not-found> creature=%p thread=%lu",
                Name(kind),
                static_cast<unsigned int>(event.phase),
                event.accepted ? "true" : "false",
                event.actionType != nullptr ? event.actionType : "<unknown>",
                event.creature,
                GetCurrentThreadId());
            diagnostics_.Event("PillarAbilityComponentState", detail);
            return;
        }

        bool readable = false;
        std::uint8_t active = 0;
        std::int32_t releaseCount = 0;
        std::int32_t stageLimit = 0;
        std::int32_t stageCurrent = 0;
        std::uint8_t terminal = 0;
        std::int32_t terminalCountdown = 0;
        std::uint8_t releaseAccepted = 0;
        __try
        {
            const auto* const bytes =
                static_cast<const std::uint8_t*>(component);
            active = bytes[0x18];
            releaseCount = *reinterpret_cast<const std::int32_t*>(
                bytes + 0x5C);
            stageLimit = *reinterpret_cast<const std::int32_t*>(
                bytes + 0x60);
            stageCurrent = *reinterpret_cast<const std::int32_t*>(
                bytes + 0x64);
            terminal = bytes[0x68];
            terminalCountdown = *reinterpret_cast<const std::int32_t*>(
                bytes + 0x6C);
            releaseAccepted = bytes[0x7A];
            readable = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            readable = false;
        }

        char detail[384] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "ability=%s phase=%u accepted=%s action_type=%s creature=%p component=%p active=%u release_count=%d stage=%d/%d terminal=%u terminal_countdown=%d release_accepted=%u readable=%s thread=%lu",
            Name(kind),
            static_cast<unsigned int>(event.phase),
            event.accepted ? "true" : "false",
            event.actionType != nullptr ? event.actionType : "<unknown>",
            event.creature,
            component,
            static_cast<unsigned int>(active),
            releaseCount,
            stageCurrent,
            stageLimit,
            static_cast<unsigned int>(terminal),
            terminalCountdown,
            static_cast<unsigned int>(releaseAccepted),
            readable ? "true" : "false",
            GetCurrentThreadId());
        diagnostics_.Event("PillarAbilityComponentState", detail);
    }

    void PillarAbilityLifecycleHook::RetryAfterActionUpdate(
        game::creature::actions::CreatureActionLifecycleObserver& observer,
        void* creature) noexcept
    {
        if (creature == nullptr)
        {
            return;
        }

        const std::uint64_t now = GetTickCount64();
        std::uint64_t previousSweep = lastSweepAt_.load(
            std::memory_order_relaxed);
        if (now - previousSweep >= SweepIntervalMilliseconds &&
            lastSweepAt_.compare_exchange_strong(
                previousSweep, now, std::memory_order_relaxed))
        {
            SweepExpired(now);
        }

        void* action = nullptr;
        PillarKind kind = PillarKind::None;
        bool authoritativeReplay = false;
        AcquireSRWLockExclusive(&pendingLock_);
        for (PendingAction& pending : pending_)
        {
            if (pending.creature == creature && pending.action != nullptr &&
                !pending.inFlight)
            {
                pending.inFlight = true;
                action = pending.action;
                kind = pending.kind;
                authoritativeReplay = pending.authoritativeReplay;
                break;
            }
        }
        ReleaseSRWLockExclusive(&pendingLock_);
        if (action == nullptr)
        {
            return;
        }

        void* const activeAction = ReadActiveAction(creature);
        char activeType[ActionNameCapacity] = {};
        game::creature::actions::CreatureActionLifecycleObserver::
            DescribeActionType(
                activeAction, activeType, std::size(activeType));
        if (MatchesCast(kind, activeType) && !IsFinished(activeAction))
        {
            AcquireSRWLockExclusive(&pendingLock_);
            for (PendingAction& pending : pending_)
            {
                if (pending.creature == creature && pending.action == action)
                {
                    pending.inFlight = false;
                    break;
                }
            }
            ReleaseSRWLockExclusive(&pendingLock_);
            return;
        }

        const bool accepted = observer.RetrySubmission(
            creature, action, authoritativeReplay);
        bool destroy = false;
        AcquireSRWLockExclusive(&pendingLock_);
        for (PendingAction& pending : pending_)
        {
            if (pending.creature != creature || pending.action != action)
            {
                continue;
            }
            if (accepted)
            {
                pending = {};
                destroy = true;
            }
            else
            {
                pending.inFlight = false;
            }
            break;
        }
        ReleaseSRWLockExclusive(&pendingLock_);
        if (destroy)
        {
            DestroyAction(action);
        }

        const unsigned int ordinal = diagnosticCount_.fetch_add(
            1, std::memory_order_relaxed) + 1;
        if (ordinal <= DiagnosticLimit)
        {
            char detail[288] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "ordinal=%u ability=%s creature=%p action=%p accepted=%s active=%p active_type=%s replicated=%s thread=%lu",
                ordinal,
                Name(kind),
                creature,
                action,
                accepted ? "true" : "false",
                activeAction,
                activeType[0] != '\0' ? activeType : "<none>",
                authoritativeReplay ? "true" : "false",
                GetCurrentThreadId());
            diagnostics_.Event("PillarAbilityBuildUpRetried", detail);
        }
    }

    void PillarAbilityLifecycleHook::SweepExpired(std::uint64_t now) noexcept
    {
        std::array<void*, PendingCapacity> expired = {};
        std::size_t count = 0;
        AcquireSRWLockExclusive(&pendingLock_);
        for (PendingAction& pending : pending_)
        {
            if (pending.action == nullptr || pending.inFlight ||
                now - pending.capturedAt < PendingLifetimeMilliseconds)
            {
                continue;
            }
            expired[count++] = pending.action;
            pending = {};
        }
        ReleaseSRWLockExclusive(&pendingLock_);

        for (std::size_t index = 0; index < count; ++index)
        {
            DestroyAction(expired[index]);
        }
        if (count != 0)
        {
            char detail[128] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "count=%zu lifetime_ms=%llu",
                count,
                static_cast<unsigned long long>(PendingLifetimeMilliseconds));
            diagnostics_.Event("PillarAbilityBuildUpExpired", detail);
        }
    }

    void PillarAbilityLifecycleHook::ReleaseAllPending() noexcept
    {
        std::array<void*, PendingCapacity> actions = {};
        std::size_t count = 0;
        AcquireSRWLockExclusive(&pendingLock_);
        for (PendingAction& pending : pending_)
        {
            if (pending.action != nullptr)
            {
                actions[count++] = pending.action;
            }
            pending = {};
        }
        ReleaseSRWLockExclusive(&pendingLock_);
        for (std::size_t index = 0; index < count; ++index)
        {
            DestroyAction(actions[index]);
        }
    }

    PillarAbilityLifecycleHook::PillarKind
        PillarAbilityLifecycleHook::ResolveBuildUp(
            const char* actionType) noexcept
    {
        if (actionType == nullptr)
        {
            return PillarKind::None;
        }
        if (std::strcmp(actionType, BuildUpDivine) == 0)
        {
            return PillarKind::DivineWrath;
        }
        if (std::strcmp(actionType, BuildUpUnholy) == 0)
        {
            return PillarKind::UnholyPower;
        }
        return PillarKind::None;
    }

    PillarAbilityLifecycleHook::PillarKind
        PillarAbilityLifecycleHook::ResolvePillarAction(
            const char* actionType) noexcept
    {
        if (actionType == nullptr)
        {
            return PillarKind::None;
        }
        if (std::strstr(actionType, DivineToken) != nullptr)
        {
            return PillarKind::DivineWrath;
        }
        if (std::strstr(actionType, UnholyToken) != nullptr)
        {
            return PillarKind::UnholyPower;
        }
        return PillarKind::None;
    }

    bool PillarAbilityLifecycleHook::MatchesCast(
        PillarKind kind,
        const char* actionType) noexcept
    {
        if (actionType == nullptr)
        {
            return false;
        }
        return (kind == PillarKind::DivineWrath &&
                std::strcmp(actionType, CastDivine) == 0) ||
            (kind == PillarKind::UnholyPower &&
                std::strcmp(actionType, CastUnholy) == 0);
    }

    const char* PillarAbilityLifecycleHook::Name(PillarKind kind) noexcept
    {
        switch (kind)
        {
        case PillarKind::DivineWrath:
            return "Divine Wrath";
        case PillarKind::UnholyPower:
            return "Unholy Power";
        default:
            return "Unknown";
        }
    }

    void* PillarAbilityLifecycleHook::ReadActiveAction(
        void* creature) noexcept
    {
        void* action = nullptr;
        __try
        {
            action = creature != nullptr
                ? *reinterpret_cast<void**>(
                    static_cast<std::uint8_t*>(creature) +
                        CreatureActiveActionOffset)
                : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            action = nullptr;
        }
        return action;
    }

    bool PillarAbilityLifecycleHook::IsFinished(void* action) noexcept
    {
        bool finished = true;
        __try
        {
            finished = action == nullptr ||
                *reinterpret_cast<const std::uint8_t*>(
                    static_cast<const std::uint8_t*>(action) +
                        ActionFinishedOffset) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            finished = true;
        }
        return finished;
    }

    void* PillarAbilityLifecycleHook::CloneAction(void* action) noexcept
    {
        void* clone = nullptr;
        __try
        {
            if (action != nullptr)
            {
                auto** const vtable = *reinterpret_cast<void***>(action);
                using ClonePointer = void* (__thiscall*)(void*);
                const auto cloneFunction = reinterpret_cast<ClonePointer>(
                    vtable[ActionCloneVtableOffset / sizeof(void*)]);
                clone = cloneFunction != nullptr
                    ? cloneFunction(action)
                    : nullptr;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            clone = nullptr;
        }
        return clone;
    }

    void PillarAbilityLifecycleHook::DestroyAction(void* action) noexcept
    {
        __try
        {
            if (action != nullptr)
            {
                auto** const vtable = *reinterpret_cast<void***>(action);
                using DeletingDestructor =
                    void* (__thiscall*)(void*, unsigned int);
                const auto destroy =
                    reinterpret_cast<DeletingDestructor>(vtable[0]);
                if (destroy != nullptr)
                {
                    destroy(action, 1);
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void* PillarAbilityLifecycleHook::FindPillarComponent(
        void* creature,
        PillarKind kind) noexcept
    {
        game::entity::native::ThingComponentRange range = {};
        if (!game::entity::native::ThingComponentAccess::ReadRange(
                creature, range))
        {
            return nullptr;
        }

        const char* const expected = kind == PillarKind::DivineWrath
            ? DivineComponent
            : kind == PillarKind::UnholyPower
                ? UnholyComponent
                : nullptr;
        if (expected == nullptr)
        {
            return nullptr;
        }
        for (const auto* entry = range.begin; entry != range.end; ++entry)
        {
            char typeName[ActionNameCapacity] = {};
            if (entry->instance != nullptr &&
                DescribeNativeType(
                    entry->instance, typeName, std::size(typeName)) &&
                std::strstr(typeName, expected) != nullptr)
            {
                return entry->instance;
            }
        }
        return nullptr;
    }

    bool PillarAbilityLifecycleHook::DescribeNativeType(
        void* object,
        char* name,
        std::size_t capacity) noexcept
    {
        if (object == nullptr || name == nullptr || capacity == 0)
        {
            return false;
        }
        name[0] = '\0';
        bool valid = false;
        __try
        {
            auto** const vtable = *reinterpret_cast<void***>(object);
            const auto* const locator =
                reinterpret_cast<const std::uint8_t*>(vtable[-1]);
            const auto* const typeDescriptor =
                *reinterpret_cast<const std::uint8_t* const*>(locator + 0x0C);
            const char* rawName =
                reinterpret_cast<const char*>(typeDescriptor + 0x08);
            if (rawName[0] == '.' && rawName[1] == '?' &&
                rawName[2] == 'A' &&
                (rawName[3] == 'V' || rawName[3] == 'U'))
            {
                rawName += 4;
            }

            std::size_t length = 0;
            while (length + 1 < capacity && rawName[length] != '\0')
            {
                if (rawName[length] == '@' && rawName[length + 1] == '@')
                {
                    break;
                }
                name[length] = rawName[length];
                ++length;
            }
            name[length] = '\0';
            valid = length != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            name[0] = '\0';
            valid = false;
        }
        return valid;
    }
}
