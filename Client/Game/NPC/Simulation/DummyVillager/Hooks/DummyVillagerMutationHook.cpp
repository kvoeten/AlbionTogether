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
            RestoreDetour(scheduleDetour_);
            RestoreDetour(materializeDetour_);
            originalSchedule_ = nullptr;
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
            RestoreDetour(serializeDetour_);
            RestoreDetour(materializeDetour_);
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
            materializeDetour_.trampoline,
            schedule,
            scheduleDetour_.trampoline,
            serialize,
            serializeDetour_.trampoline);
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
            materializeDetour_.trampoline != nullptr &&
            scheduleDetour_.trampoline != nullptr &&
            serializeDetour_.trampoline != nullptr;
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
                        DummyVillagerFunctions::ThingUidOffset);
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
        Detour& detour,
        native::DummyVillagerFunctions::UpdatePointer& original) noexcept
    {
        if (target == nullptr || replacement == nullptr ||
            displacedBytes < 5 ||
            displacedBytes > detour.originalBytes.size())
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
        const std::intptr_t resumeDisplacement =
            reinterpret_cast<std::intptr_t>(target + displacedBytes) -
            (reinterpret_cast<std::intptr_t>(trampoline + displacedBytes) + 5);
        const std::intptr_t hookDisplacement =
            reinterpret_cast<std::intptr_t>(replacement) -
            (reinterpret_cast<std::intptr_t>(target) + 5);
        if (resumeDisplacement < INT32_MIN ||
            resumeDisplacement > INT32_MAX ||
            hookDisplacement < INT32_MIN ||
            hookDisplacement > INT32_MAX)
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }
        const auto resume = static_cast<std::int32_t>(resumeDisplacement);
        std::memcpy(trampoline + displacedBytes + 1, &resume, sizeof(resume));
        std::array<std::uint8_t, 8> patch = {};
        patch.fill(0x90);
        patch[0] = 0xE9;
        const auto replacementRelative = static_cast<std::int32_t>(
            hookDisplacement);
        std::memcpy(
            patch.data() + 1,
            &replacementRelative,
            sizeof(replacementRelative));
        DWORD previousProtection = 0;
        if (!VirtualProtect(
                target,
                displacedBytes,
                PAGE_EXECUTE_READWRITE,
                &previousProtection))
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }
        std::memcpy(target, patch.data(), displacedBytes);
        FlushInstructionCache(GetCurrentProcess(), target, displacedBytes);
        FlushInstructionCache(
            GetCurrentProcess(), trampoline, displacedBytes + 5);
        DWORD discarded = 0;
        VirtualProtect(
            target, displacedBytes, previousProtection, &discarded);
        detour.target = target;
        detour.trampoline = trampoline;
        detour.displacedBytes = displacedBytes;
        original = reinterpret_cast<
            native::DummyVillagerFunctions::UpdatePointer>(trampoline);
        return true;
    }

    bool DummyVillagerMutationHook::InstallSerializeDetour(
        std::uint8_t* target,
        void* replacement,
        std::size_t displacedBytes,
        Detour& detour,
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
                detour.trampoline);
        return true;
    }

    void DummyVillagerMutationHook::RestoreDetour(Detour& detour) noexcept
    {
        if (detour.target == nullptr || detour.displacedBytes == 0)
        {
            return;
        }
        DWORD previousProtection = 0;
        if (VirtualProtect(
                detour.target,
                detour.displacedBytes,
                PAGE_EXECUTE_READWRITE,
                &previousProtection))
        {
            std::memcpy(
                detour.target,
                detour.originalBytes.data(),
                detour.displacedBytes);
            FlushInstructionCache(
                GetCurrentProcess(), detour.target, detour.displacedBytes);
            DWORD discarded = 0;
            VirtualProtect(
                detour.target,
                detour.displacedBytes,
                previousProtection,
                &discarded);
        }
        if (detour.trampoline != nullptr)
        {
            VirtualFree(detour.trampoline, 0, MEM_RELEASE);
        }
        detour = {};
    }
}
