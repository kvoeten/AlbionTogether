#include "DummyVillagerMutationHook.h"

#include "Game/Entity/Native/ThingComponentAccess.h"

#include <array>
#include <climits>
#include <cstdio>
#include <cstring>

namespace fable::game::npc::simulation
{
    DummyVillagerMutationHook* DummyVillagerMutationHook::active_ = nullptr;
    thread_local unsigned int
        DummyVillagerMutationHook::authoritativeApplyDepth_ = 0;

    bool DummyVillagerMutationHook::Install(
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
            "Hook: dummy-villager mutation observation is only supported by the x86 client.");
        return false;
#else
        if (active_ != nullptr && active_ != this)
        {
            return false;
        }
        if (active_ == this)
        {
            diagnostics_.Log(
                "Hook: dummy-villager installation is partially active; shutdown is required before retrying.");
            return false;
        }
        std::uint8_t* materialize = nullptr;
        std::uint8_t* schedule = nullptr;
        std::uint8_t* serialize = nullptr;
        if (!native::DummyVillagerFunctions::ResolveMaterialize(
                gameModule, materialize) ||
            !native::DummyVillagerFunctions::ResolveSchedule(
                gameModule, schedule) ||
            !native::DummyVillagerFunctions::ResolveSerialize(
                gameModule, serialize))
        {
            diagnostics_.Log(
                "Hook: CTCDummyVillager update definitions failed validation.");
            return false;
        }
        gameModule_ = gameModule;
        active_ = this;
        if (!InstallDetour(
                materialize,
                reinterpret_cast<void*>(
                    &DummyVillagerMutationHook::MaterializeIntercept),
                native::DummyVillagerFunctions::MaterializeDisplacedBytes,
                materializeDetour_,
                originalMaterialize_))
        {
            active_ = nullptr;
            gameModule_ = nullptr;
            return false;
        }
        if (!InstallSerializeDetour(
                serialize,
                reinterpret_cast<void*>(
                    &DummyVillagerMutationHook::SerializeIntercept),
                native::DummyVillagerFunctions::SerializeDisplacedBytes,
                serializeDetour_,
                originalSerialize_))
        {
            bool rollbackRestored = true;
            rollbackRestored = RestoreDetour(scheduleDetour_) && rollbackRestored;
            rollbackRestored = RestoreDetour(materializeDetour_) && rollbackRestored;
            if (!rollbackRestored)
            {
                diagnostics_.Log(
                    "Hook: dummy-villager serialize rollback deferred because a target is owned by another hook.");
                return false;
            }
            originalSchedule_ = nullptr;
            originalSerialize_ = nullptr;
            originalMaterialize_ = nullptr;
            active_ = nullptr;
            gameModule_ = nullptr;
            return false;
        }
        if (!InstallDetour(
                schedule,
                reinterpret_cast<void*>(
                    &DummyVillagerMutationHook::ScheduleIntercept),
                native::DummyVillagerFunctions::ScheduleDisplacedBytes,
                scheduleDetour_,
                originalSchedule_))
        {
            bool rollbackRestored = true;
            rollbackRestored = RestoreDetour(serializeDetour_) && rollbackRestored;
            rollbackRestored = RestoreDetour(materializeDetour_) && rollbackRestored;
            if (!rollbackRestored)
            {
                diagnostics_.Log(
                    "Hook: dummy-villager schedule rollback deferred because a target is owned by another hook.");
                return false;
            }
            originalSerialize_ = nullptr;
            originalMaterialize_ = nullptr;
            active_ = nullptr;
            gameModule_ = nullptr;
            return false;
        }
        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "materialize=%p materialize_trampoline=%p schedule=%p schedule_trampoline=%p serialize=%p serialize_trampoline=%p component=0xD6",
            materialize,
            materializeDetour_.Original(),
            schedule,
            scheduleDetour_.Original(),
            serialize,
            serializeDetour_.Original());
        diagnostics_.Event("DummyVillagerMutationHookReady", detail);
        return true;
#endif
    }

    void DummyVillagerMutationHook::SetProjectionSink(
        ProjectionSink sink,
        void* context) noexcept
    {
        if (sink == nullptr)
        {
            projectionSink_.store(nullptr, std::memory_order_release);
            projectionSinkContext_.store(nullptr, std::memory_order_release);
            return;
        }
        projectionSinkContext_.store(context, std::memory_order_release);
        projectionSink_.store(sink, std::memory_order_release);
    }

    void DummyVillagerMutationHook::SetEventSink(
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

    bool DummyVillagerMutationHook::Read(
        void* thing,
        DummyVillagerState& state) const noexcept
    {
        return IsInstalled() &&
            native::DummyVillagerFunctions::Read(thing, state);
    }

    bool DummyVillagerMutationHook::ApplyAuthoritative(
        void* thing,
        const DummyVillagerState& state,
        bool& changed) noexcept
    {
        changed = false;
        if (!IsInstalled() || thing == nullptr)
        {
            return false;
        }
        DummyVillagerState before;
        if (!Read(thing, before))
        {
            return false;
        }
        if (!before.componentPresent)
        {
            return !state.componentPresent;
        }
        if (!state.componentPresent)
        {
            return false;
        }
        if (before == state)
        {
            return true;
        }
        void* const component = entity::native::ThingComponentAccess::Find(
            thing,
            entity::native::ThingComponentType::DummyVillager);
        bool applied = false;
        ++authoritativeApplyDepth_;
        applied = native::DummyVillagerFunctions::WriteComponent(
            component, state);
        --authoritativeApplyDepth_;
        if (!applied)
        {
            return false;
        }
        DummyVillagerState after;
        changed = true;
        return Read(thing, after) && after == state;
    }

    bool DummyVillagerMutationHook::IsInstalled() const noexcept
    {
        return active_ == this && gameModule_ != nullptr &&
            originalMaterialize_ != nullptr && originalSchedule_ != nullptr &&
            originalSerialize_ != nullptr &&
            materializeDetour_.IsInstalled() && scheduleDetour_.IsInstalled() &&
            serializeDetour_.IsInstalled();
    }

    void __fastcall DummyVillagerMutationHook::MaterializeIntercept(
        void* component,
        void*)
    {
        DummyVillagerMutationHook* const hook = active_;
        if (hook == nullptr || hook->originalMaterialize_ == nullptr)
        {
            return;
        }
        DummyVillagerState previous;
        native::DummyVillagerFunctions::ReadComponent(component, previous);
        hook->originalMaterialize_(component);
        ObserveAfter(*hook, component, previous);
    }

    void __fastcall DummyVillagerMutationHook::ScheduleIntercept(
        void* component,
        void*)
    {
        DummyVillagerMutationHook* const hook = active_;
        if (hook == nullptr || hook->originalSchedule_ == nullptr)
        {
            return;
        }
        DummyVillagerState previous;
        native::DummyVillagerFunctions::ReadComponent(component, previous);
        hook->originalSchedule_(component);
        ObserveAfter(*hook, component, previous);
    }

    void DummyVillagerMutationHook::ObserveAfter(
        DummyVillagerMutationHook& hook,
        void* component,
        const DummyVillagerState& previous)
    {
        if (authoritativeApplyDepth_ != 0)
        {
            return;
        }
        void* const thing = ReadOwnerThing(component);
        DummyVillagerState current;
        if (thing == nullptr ||
            !native::DummyVillagerFunctions::ReadComponent(
                component, current) ||
            current == previous)
        {
            return;
        }
        const EventSink sink = hook.eventSink_.load(
            std::memory_order_acquire);
        if (sink == nullptr)
        {
            return;
        }
        DummyVillagerMutationEvent event;
        event.thing = thing;
        event.thingUid = ReadThingUid(thing);
        event.previous = previous;
        event.current = current;
        event.observedAt = GetTickCount64();
        if (event.thingUid == 0)
        {
            return;
        }
        sink(
            hook.eventSinkContext_.load(std::memory_order_acquire),
            event);
        const unsigned int ordinal = hook.observedCount_.fetch_add(
            1, std::memory_order_acq_rel) + 1;
        if (ordinal <= 64)
        {
            char detail[320] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "ordinal=%u thing_uid=%016llX day=%d frame=%d respawnable=%s guard=%s",
                ordinal,
                static_cast<unsigned long long>(event.thingUid),
                event.current.recreationDay,
                event.current.recreationFrame,
                event.current.respawnable ? "true" : "false",
                event.current.guard ? "true" : "false");
            hook.diagnostics_.Event("DummyVillagerStateMutated", detail);
        }
    }

    bool __fastcall DummyVillagerMutationHook::SerializeIntercept(
        void* component,
        void*,
        void* serializer,
        void* context)
    {
        DummyVillagerMutationHook* const hook = active_;
        if (hook == nullptr || hook->originalSerialize_ == nullptr)
        {
            return false;
        }
        std::int32_t mode = 0;
        __try
        {
            if (serializer != nullptr)
            {
                mode = *reinterpret_cast<const std::int32_t*>(
                    static_cast<const std::uint8_t*>(serializer) + 0x18);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            mode = 0;
        }
        const bool saving = mode == 1 || mode == 3;
        DummyVillagerState before;
        DummyVillagerState projected;
        bool projectionApplied = false;
        const ProjectionSink sink = hook->projectionSink_.load(
            std::memory_order_acquire);
        if (saving && sink != nullptr &&
            native::DummyVillagerFunctions::ReadComponent(component, before))
        {
            projected = before;
            void* const thing = ReadOwnerThing(component);
            const std::uint64_t uid = ReadThingUid(thing);
            if (uid != 0 && sink(
                    hook->projectionSinkContext_.load(
                        std::memory_order_acquire),
                    uid,
                    projected) &&
                projected.componentPresent && projected != before)
            {
                projectionApplied =
                    native::DummyVillagerFunctions::WriteComponent(
                        component, projected);
            }
        }
        const bool result = hook->originalSerialize_(
            component, serializer, context);
        if (projectionApplied)
        {
            native::DummyVillagerFunctions::WriteComponent(component, before);
        }
        return result;
    }

    void* DummyVillagerMutationHook::ReadOwnerThing(
        void* component) noexcept
    {
        void* thing = nullptr;
        __try
        {
            if (component != nullptr)
            {
                thing = *reinterpret_cast<void* const*>(
                    static_cast<const std::uint8_t*>(component) + native::
                        DummyVillagerFunctions::OwnerThingOffset);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            thing = nullptr;
        }
        return thing;
    }

    std::uint64_t DummyVillagerMutationHook::ReadThingUid(
        void* thing) noexcept
    {
        std::uint64_t uid = 0;
        __try
        {
            if (thing != nullptr)
            {
                uid = *reinterpret_cast<const std::uint64_t*>(
                    static_cast<const std::uint8_t*>(thing) + native::
                        DummyVillagerFunctions::OwnerThingUidOffset);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            uid = 0;
        }
        return uid;
    }

    bool DummyVillagerMutationHook::InstallDetour(
        std::uint8_t* target,
        void* replacement,
        std::size_t displacedBytes,
        core::hooking::InlineHook& detour,
        native::DummyVillagerFunctions::UpdatePointer& original) noexcept
    {
        if (target == nullptr || replacement == nullptr ||
            displacedBytes < 5)
        {
            return false;
        }
        if (!detour.Install(target, target, displacedBytes, replacement, displacedBytes))
        {
            return false;
        }
        original = reinterpret_cast<
            native::DummyVillagerFunctions::UpdatePointer>(detour.Original());
        return true;
    }

    bool DummyVillagerMutationHook::InstallSerializeDetour(
        std::uint8_t* target,
        void* replacement,
        std::size_t displacedBytes,
        core::hooking::InlineHook& detour,
        native::DummyVillagerFunctions::SerializePointer& original) noexcept
    {
        native::DummyVillagerFunctions::UpdatePointer erased = nullptr;
        if (!InstallDetour(
                target,
                replacement,
                displacedBytes,
                detour,
                erased))
        {
            return false;
        }
        original = reinterpret_cast<
            native::DummyVillagerFunctions::SerializePointer>(
                detour.Original());
        return true;
    }

    void DummyVillagerMutationHook::Shutdown() noexcept
    {
        bool allRestored = true;
        allRestored = RestoreDetour(scheduleDetour_) && allRestored;
        allRestored = RestoreDetour(serializeDetour_) && allRestored;
        allRestored = RestoreDetour(materializeDetour_) && allRestored;
        if (!allRestored)
        {
            diagnostics_.Log(
                "Hook: dummy-villager shutdown deferred because a target is owned by another hook.");
            return;
        }
        SetEventSink(nullptr, nullptr);
        SetProjectionSink(nullptr, nullptr);
        if (active_ == this)
        {
            active_ = nullptr;
        }
        originalSchedule_ = nullptr;
        originalSerialize_ = nullptr;
        originalMaterialize_ = nullptr;
        gameModule_ = nullptr;
        diagnostics_ = {};
    }

    bool DummyVillagerMutationHook::RestoreDetour(
        core::hooking::InlineHook& detour) noexcept
    {
        return detour.Shutdown();
    }
}
