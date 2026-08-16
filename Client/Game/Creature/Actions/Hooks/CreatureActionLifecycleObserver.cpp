#include "CreatureActionLifecycleObserver.h"

#include <array>
#include <climits>
#include <cstdio>
#include <cstring>

namespace fable::game::creature::actions
{
    CreatureActionLifecycleObserver* CreatureActionLifecycleObserver::active_ = nullptr;

    bool CreatureActionLifecycleObserver::Install(
        HMODULE gameModule,
        const core::Diagnostics& diagnostics)
    {
        if (IsInstalled())
        {
            return true;
        }
        diagnostics_ = diagnostics;

#if !defined(_M_IX86)
        diagnostics_.Log(
            "Hook: creature action lifecycle observation is only supported by the x86 client.");
        return false;
#else
        if (active_ != nullptr && active_ != this)
        {
            diagnostics_.Log(
                "Hook: another creature action lifecycle observer is already active.");
            return false;
        }

        std::uint8_t* updateTarget = nullptr;
        std::uint8_t* submitTarget = nullptr;
        std::uint8_t* finishTarget = nullptr;
        if (!native::CreatureActionFunctions::ResolveUpdate(
                gameModule,
                updateTarget) ||
            !native::CreatureActionFunctions::ResolveSubmit(
                gameModule,
                submitTarget) ||
            !native::CreatureActionFunctions::ResolveFinish(
                gameModule,
                finishTarget))
        {
            diagnostics_.Log(
                "Hook: creature action lifecycle definitions failed validation.");
            return false;
        }

        active_ = this;
        if (!InstallDetour(
                updateTarget,
                reinterpret_cast<void*>(&CreatureActionLifecycleObserver::ObserveUpdate),
                updateDetour_))
        {
            active_ = nullptr;
            diagnostics_.Log("Hook: creature action update detour installation failed.");
            return false;
        }
        originalUpdate_ = reinterpret_cast<
            native::CreatureActionFunctions::UpdatePointer>(
                updateDetour_.trampoline);

        if (!InstallDetour(
                submitTarget,
                reinterpret_cast<void*>(&CreatureActionLifecycleObserver::ObserveSubmission),
                submitDetour_))
        {
            RestoreDetour(updateDetour_);
            originalUpdate_ = nullptr;
            active_ = nullptr;
            diagnostics_.Log("Hook: creature action submission detour installation failed.");
            return false;
        }
        originalSubmit_ = reinterpret_cast<native::CreatureActionFunctions::SubmitPointer>(
            submitDetour_.trampoline);

        if (!InstallDetour(
                finishTarget,
                reinterpret_cast<void*>(&CreatureActionLifecycleObserver::ObserveFinish),
                finishDetour_))
        {
            RestoreDetour(submitDetour_);
            RestoreDetour(updateDetour_);
            originalSubmit_ = nullptr;
            originalUpdate_ = nullptr;
            active_ = nullptr;
            diagnostics_.Log("Hook: creature action finish detour installation failed.");
            return false;
        }
        originalFinish_ = reinterpret_cast<native::CreatureActionFunctions::FinishPointer>(
            finishDetour_.trampoline);

        char detail[384] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "update=%p update_trampoline=%p submit=%p submit_trampoline=%p finish=%p finish_trampoline=%p event_limit=%u",
            updateDetour_.target,
            updateDetour_.trampoline,
            submitDetour_.target,
            submitDetour_.trampoline,
            finishDetour_.target,
            finishDetour_.trampoline,
            DiagnosticEventLimit);
        diagnostics_.Log(
            "Hook: native creature action update/begin/end boundaries installed.");
        diagnostics_.Event("CreatureActionLifecycleObserverReady", detail);
        return true;
#endif
    }

    bool CreatureActionLifecycleObserver::IsInstalled() const noexcept
    {
        return active_ == this &&
            originalSubmit_ != nullptr &&
            originalFinish_ != nullptr &&
            originalUpdate_ != nullptr &&
            updateDetour_.target != nullptr &&
            submitDetour_.target != nullptr &&
            finishDetour_.target != nullptr;
    }

    void CreatureActionLifecycleObserver::SetEventSink(
        EventSink sink,
        void* context) noexcept
    {
        if (sink == nullptr)
        {
            eventSink_.store(nullptr, std::memory_order_release);
            eventSinkContext_.store(nullptr, std::memory_order_release);
            return;
        }
        eventSinkContext_.store(context, std::memory_order_release);
        eventSink_.store(sink, std::memory_order_release);
    }

    void CreatureActionLifecycleObserver::SetAuthorityGate(
        AuthorityGate gate,
        void* context) noexcept
    {
        if (gate == nullptr)
        {
            authorityGate_.store(nullptr, std::memory_order_release);
            authorityGateContext_.store(nullptr, std::memory_order_release);
            return;
        }
        authorityGateContext_.store(context, std::memory_order_release);
        authorityGate_.store(gate, std::memory_order_release);
    }

    unsigned int CreatureActionLifecycleObserver::SubmissionCount() const noexcept
    {
        return submissionCount_.load(std::memory_order_acquire);
    }

    unsigned int CreatureActionLifecycleObserver::FinishCount() const noexcept
    {
        return finishCount_.load(std::memory_order_acquire);
    }

    bool CreatureActionLifecycleObserver::InstallDetour(
        std::uint8_t* target,
        void* replacement,
        Detour& detour) noexcept
    {
        constexpr std::size_t displacedBytes =
            native::CreatureActionFunctions::DisplacedBytes;
        if (target == nullptr || replacement == nullptr || detour.target != nullptr)
        {
            return false;
        }

        auto* const trampoline = static_cast<std::uint8_t*>(VirtualAlloc(
            nullptr,
            displacedBytes + 5,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_EXECUTE_READWRITE));
        if (trampoline == nullptr)
        {
            return false;
        }

        std::memcpy(detour.originalBytes.data(), target, displacedBytes);
        std::memcpy(trampoline, target, displacedBytes);
        trampoline[displacedBytes] = 0xE9;
        const std::intptr_t trampolineDisplacement =
            reinterpret_cast<std::intptr_t>(target + displacedBytes) -
            (reinterpret_cast<std::intptr_t>(trampoline + displacedBytes) + 5);
        const std::intptr_t replacementDisplacement =
            reinterpret_cast<std::intptr_t>(replacement) -
            (reinterpret_cast<std::intptr_t>(target) + 5);
        if (trampolineDisplacement < INT32_MIN ||
            trampolineDisplacement > INT32_MAX ||
            replacementDisplacement < INT32_MIN ||
            replacementDisplacement > INT32_MAX)
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }

        const std::int32_t trampolineRelative =
            static_cast<std::int32_t>(trampolineDisplacement);
        std::memcpy(
            trampoline + displacedBytes + 1,
            &trampolineRelative,
            sizeof(trampolineRelative));

        std::array<std::uint8_t, displacedBytes> patch = {};
        patch.fill(0x90);
        patch[0] = 0xE9;
        const std::int32_t replacementRelative =
            static_cast<std::int32_t>(replacementDisplacement);
        std::memcpy(patch.data() + 1, &replacementRelative, sizeof(replacementRelative));

        DWORD previousProtection = 0;
        if (!VirtualProtect(
                target,
                patch.size(),
                PAGE_EXECUTE_READWRITE,
                &previousProtection))
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }

        detour.target = target;
        detour.trampoline = trampoline;
        std::memcpy(target, patch.data(), patch.size());
        FlushInstructionCache(GetCurrentProcess(), target, patch.size());
        FlushInstructionCache(
            GetCurrentProcess(),
            trampoline,
            displacedBytes + 5);

        DWORD discarded = 0;
        VirtualProtect(target, patch.size(), previousProtection, &discarded);
        return true;
    }

    void CreatureActionLifecycleObserver::RestoreDetour(Detour& detour) noexcept
    {
        if (detour.target == nullptr)
        {
            return;
        }

        DWORD previousProtection = 0;
        if (VirtualProtect(
                detour.target,
                detour.originalBytes.size(),
                PAGE_EXECUTE_READWRITE,
                &previousProtection))
        {
            std::memcpy(
                detour.target,
                detour.originalBytes.data(),
                detour.originalBytes.size());
            FlushInstructionCache(
                GetCurrentProcess(),
                detour.target,
                detour.originalBytes.size());
            DWORD discarded = 0;
            VirtualProtect(
                detour.target,
                detour.originalBytes.size(),
                previousProtection,
                &discarded);
        }
        if (detour.trampoline != nullptr)
        {
            VirtualFree(detour.trampoline, 0, MEM_RELEASE);
        }
        detour = {};
    }

    void __fastcall CreatureActionLifecycleObserver::ObserveUpdate(
        void* creature,
        void*)
    {
        CreatureActionLifecycleObserver* const observer = active_;
        if (observer == nullptr || observer->originalUpdate_ == nullptr)
        {
            return;
        }

        void* activeAction = nullptr;
        __try
        {
            if (creature != nullptr)
            {
                activeAction = *reinterpret_cast<void**>(
                    static_cast<std::uint8_t*>(creature) + 0x120);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            activeAction = nullptr;
        }
        const AuthorityGate gate = observer->authorityGate_.load(
            std::memory_order_acquire);
        if (gate != nullptr && !gate(
                observer->authorityGateContext_.load(
                    std::memory_order_acquire),
                creature,
                activeAction))
        {
            return;
        }
        observer->originalUpdate_(creature);
    }

    bool __fastcall CreatureActionLifecycleObserver::ObserveSubmission(
        void* creature,
        void*,
        void* action)
    {
        CreatureActionLifecycleObserver* const observer = active_;
        if (observer == nullptr || observer->originalSubmit_ == nullptr)
        {
            return false;
        }

        char requestedType[ActionNameCapacity] = {};
        ReadActionType(action, requestedType);
        const AuthorityGate gate = observer->authorityGate_.load(
            std::memory_order_acquire);
        const bool authorityDenied = gate != nullptr && !gate(
            observer->authorityGateContext_.load(
                std::memory_order_acquire),
            creature,
            action);
        const bool accepted = !authorityDenied &&
            observer->originalSubmit_(creature, action);
        observer->ReportSubmission(
            creature,
            action,
            requestedType,
            accepted,
            authorityDenied);
        return accepted;
    }

    void __fastcall CreatureActionLifecycleObserver::ObserveFinish(
        void* action,
        void*)
    {
        CreatureActionLifecycleObserver* const observer = active_;
        if (observer == nullptr || observer->originalFinish_ == nullptr)
        {
            return;
        }

        char actionType[ActionNameCapacity] = {};
        ReadActionType(action, actionType);
        void* const creature = ResolveActionOwner(action);
        observer->originalFinish_(action);
        observer->ReportFinish(action, creature, actionType);
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

        void* activeAction = nullptr;
        char activeType[ActionNameCapacity] = {};
        __try
        {
            if (creature != nullptr)
            {
                activeAction = *reinterpret_cast<void**>(
                    static_cast<std::uint8_t*>(creature) + 0x120);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            activeAction = nullptr;
        }
        ReadActionType(activeAction, activeType);
        const ThingContext context = ReadThingContext(creature);

        CreatureActionLifecycleEvent event;
        event.phase = CreatureActionLifecyclePhase::Submitted;
        event.thingUid = context.uid;
        event.mapId = context.mapId;
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
        Notify(event);
        if (ordinal > DiagnosticEventLimit)
        {
            return;
        }

        char detail[640] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "ordinal=%u accepted=%s authority_denied=%s thing_uid=%016llX map_id=%u creature=%p requested=%p requested_type=%s active=%p active_type=%s animation_id=%u context_readable=%s thread=%lu",
            ordinal,
            accepted ? "true" : "false",
            authorityDenied ? "true" : "false",
            static_cast<unsigned long long>(context.uid),
            static_cast<unsigned int>(context.mapId),
            creature,
            requestedAction,
            requestedType != nullptr && requestedType[0] != '\0' ? requestedType : "<unknown>",
            activeAction,
            activeType[0] != '\0' ? activeType : "<unknown>",
            event.animationId,
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
        event.mapId = context.mapId;
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
        name[0] = '\0';
        if (action == nullptr)
        {
            return false;
        }

        bool valid = false;
        __try
        {
            auto** const vtable = *reinterpret_cast<void***>(action);
            const auto* const locator = reinterpret_cast<const std::uint8_t*>(vtable[-1]);
            const auto* const typeDescriptor =
                *reinterpret_cast<const std::uint8_t* const*>(locator + 0x0C);
            const char* rawName = reinterpret_cast<const char*>(typeDescriptor + 0x08);
            if (rawName[0] == '.' && rawName[1] == '?' && rawName[2] == 'A' &&
                (rawName[3] == 'V' || rawName[3] == 'U'))
            {
                rawName += 4;
            }

            std::size_t length = 0;
            while (length + 1 < ActionNameCapacity && rawName[length] != '\0')
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

    std::uint32_t CreatureActionLifecycleObserver::ReadAnimationId(
        void* action) noexcept
    {
        if (action == nullptr)
        {
            return 0;
        }
        std::int32_t animationId = 0;
        __try
        {
            void* const resource = *reinterpret_cast<void**>(
                static_cast<std::uint8_t*>(action) + 0x74);
            if (resource != nullptr)
            {
                animationId = *static_cast<const std::int32_t*>(resource);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            animationId = 0;
        }
        return animationId > 0 && animationId <= 0xFFFF
            ? static_cast<std::uint32_t>(animationId)
            : 0;
    }

    bool CreatureActionLifecycleObserver::ActionMayCarryAnimation(
        const char* actionType) noexcept
    {
        if (actionType == nullptr || actionType[0] == '\0')
        {
            return false;
        }
        constexpr const char* markers[] = {
            "Animation",
            "Attack",
            "Combat",
            "Strike",
            "Weapon",
            "Projectile",
            "Block",
            "Parry",
            "Dodge",
            "Hit",
            "Damage",
            "Response",
            "Unsheathe",
            "Sheathe",
            "Cast",
            "Spell",
            "Ability",
        };
        for (const char* marker : markers)
        {
            if (std::strstr(actionType, marker) != nullptr)
            {
                return true;
            }
        }
        return false;
    }

    void* CreatureActionLifecycleObserver::ResolveActionOwner(void* action) noexcept
    {
        void* creature = nullptr;
        __try
        {
            if (action != nullptr)
            {
                void* const countedPointer = *reinterpret_cast<void**>(
                    static_cast<std::uint8_t*>(action) + 0x0C);
                if (countedPointer != nullptr)
                {
                    creature = *reinterpret_cast<void**>(countedPointer);
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            creature = nullptr;
        }
        return creature;
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

    void CreatureActionLifecycleObserver::Notify(
        const CreatureActionLifecycleEvent& event) noexcept
    {
        const EventSink sink = eventSink_.load(std::memory_order_acquire);
        if (sink == nullptr)
        {
            return;
        }
        void* const context =
            eventSinkContext_.load(std::memory_order_acquire);
        sink(context, event);
    }
}
