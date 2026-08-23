#include "VillageMembershipMutationHook.h"

#include "Game/Entity/Native/ThingComponentAccess.h"

#include <array>
#include <climits>
#include <cstdio>
#include <cstring>

namespace fable::game::npc::village
{
    VillageMembershipMutationHook* VillageMembershipMutationHook::active_ =
        nullptr;
    thread_local unsigned int
        VillageMembershipMutationHook::authoritativeApplyDepth_ = 0;

    bool VillageMembershipMutationHook::Install(
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
            "Hook: village-membership mutation observation is only supported by the x86 client.");
        return false;
#else
        if (active_ != nullptr && active_ != this)
        {
            return false;
        }
        std::uint8_t* target = nullptr;
        if (!native::VillageMembershipFunctions::ResolveSetVillagePointer(
                gameModule,
                target))
        {
            diagnostics_.Log(
                "Hook: CTCVillageMember setter definition validation failed.");
            return false;
        }
        constexpr std::size_t displacedBytes = native::
            VillageMembershipFunctions::SetVillagePointerDisplacedBytes;
        auto* const trampoline = static_cast<std::uint8_t*>(VirtualAlloc(
            nullptr,
            displacedBytes + 5,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_EXECUTE_READWRITE));
        if (trampoline == nullptr)
        {
            return false;
        }
        std::memcpy(originalBytes_.data(), target, displacedBytes);
        std::memcpy(trampoline, target, displacedBytes);
        trampoline[displacedBytes] = 0xE9;
        const std::intptr_t resumeDisplacement =
            reinterpret_cast<std::intptr_t>(target + displacedBytes) -
            (reinterpret_cast<std::intptr_t>(trampoline + displacedBytes) + 5);
        const std::intptr_t hookDisplacement =
            reinterpret_cast<std::intptr_t>(
                &VillageMembershipMutationHook::Intercept) -
            (reinterpret_cast<std::intptr_t>(target) + 5);
        if (resumeDisplacement < INT32_MIN ||
            resumeDisplacement > INT32_MAX ||
            hookDisplacement < INT32_MIN ||
            hookDisplacement > INT32_MAX)
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }
        const auto resumeRelative = static_cast<std::int32_t>(
            resumeDisplacement);
        std::memcpy(
            trampoline + displacedBytes + 1,
            &resumeRelative,
            sizeof(resumeRelative));
        std::array<std::uint8_t, displacedBytes> patch = {};
        patch.fill(0x90);
        patch[0] = 0xE9;
        const auto hookRelative = static_cast<std::int32_t>(hookDisplacement);
        std::memcpy(
            patch.data() + 1,
            &hookRelative,
            sizeof(hookRelative));

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
        gameModule_ = gameModule;
        target_ = target;
        trampoline_ = trampoline;
        original_ = reinterpret_cast<
            native::VillageMembershipFunctions::SetVillagePointer>(
                trampoline);
        active_ = this;
        std::memcpy(target, patch.data(), patch.size());
        FlushInstructionCache(GetCurrentProcess(), target, patch.size());
        FlushInstructionCache(
            GetCurrentProcess(),
            trampoline,
            displacedBytes + 5);
        DWORD discarded = 0;
        VirtualProtect(target, patch.size(), previousProtection, &discarded);

        char detail[256] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "target=%p replacement=%p trampoline=%p function_rva=0x%08X",
            target,
            &VillageMembershipMutationHook::Intercept,
            trampoline_,
            static_cast<unsigned int>(
                native::VillageMembershipFunctions::SetVillagePointerRva));
        diagnostics_.Event("VillageMembershipMutationHookReady", detail);
        return true;
#endif
    }

    void VillageMembershipMutationHook::Shutdown() noexcept
    {
        SetEventSink(nullptr, nullptr);
        if (active_ == this)
        {
            active_ = nullptr;
        }
        if (target_ != nullptr && trampoline_ != nullptr)
        {
            DWORD previousProtection = 0;
            if (VirtualProtect(
                    target_, originalBytes_.size(), PAGE_EXECUTE_READWRITE,
                    &previousProtection))
            {
                std::memcpy(target_, originalBytes_.data(), originalBytes_.size());
                FlushInstructionCache(GetCurrentProcess(), target_, originalBytes_.size());
                DWORD discarded = 0;
                VirtualProtect(
                    target_, originalBytes_.size(), previousProtection, &discarded);
            }
        }
        if (trampoline_ != nullptr)
        {
            VirtualFree(trampoline_, 0, MEM_RELEASE);
        }
        target_ = nullptr;
        trampoline_ = nullptr;
        original_ = nullptr;
        originalBytes_ = {};
        gameModule_ = nullptr;
        diagnostics_ = {};
    }

    void VillageMembershipMutationHook::SetEventSink(
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

    bool VillageMembershipMutationHook::Read(
        void* thing,
        native::VillageMembershipState& state) const noexcept
    {
        return IsInstalled() &&
            native::VillageMembershipFunctions::Read(
                gameModule_, thing, state);
    }

    bool VillageMembershipMutationHook::ApplyAuthoritative(
        void* thing,
        std::uint64_t villageUid,
        bool& changed) noexcept
    {
        changed = false;
        if (!IsInstalled() || thing == nullptr)
        {
            return false;
        }
        native::VillageMembershipState before;
        if (!Read(thing, before))
        {
            return false;
        }
        if (!before.componentPresent)
        {
            return villageUid == 0;
        }
        if (before.villageUid == villageUid &&
            before.linkedVillageUid == villageUid)
        {
            return true;
        }
        void* const member = entity::native::ThingComponentAccess::Find(
            thing,
            entity::native::ThingComponentType::VillageMember);
        native::VillageMembershipFunctions::ReconcileVillage reconcile =
            nullptr;
        if (!native::VillageMembershipFunctions::ValidateMemberComponent(
                gameModule_, member, reconcile))
        {
            return false;
        }

        bool applied = false;
        ++authoritativeApplyDepth_;
        __try
        {
            original_(member, nullptr);
            *reinterpret_cast<std::uint64_t*>(
                static_cast<std::uint8_t*>(member) + native::
                    VillageMembershipFunctions::VillageUidOffset) =
                        villageUid;
            if (villageUid != 0)
            {
                reconcile(member);
            }
            applied = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            applied = false;
        }
        --authoritativeApplyDepth_;
        if (!applied)
        {
            return false;
        }
        changed = true;
        native::VillageMembershipState after;
        return Read(thing, after) && after.componentPresent &&
            after.villageUid == villageUid &&
            after.linkedVillageUid == villageUid;
    }

    bool VillageMembershipMutationHook::IsInstalled() const noexcept
    {
        return active_ == this && original_ != nullptr &&
            trampoline_ != nullptr && gameModule_ != nullptr;
    }

    void* VillageMembershipMutationHook::ReadOwnerThing(
        void* villageMember) noexcept
    {
        void* thing = nullptr;
        __try
        {
            if (villageMember != nullptr)
            {
                thing = *reinterpret_cast<void* const*>(
                    static_cast<const std::uint8_t*>(villageMember) +
                    native::VillageMembershipFunctions::OwnerThingOffset);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            thing = nullptr;
        }
        return thing;
    }

    std::uint64_t VillageMembershipMutationHook::ReadThingUid(
        void* thing) noexcept
    {
        std::uint64_t uid = 0;
        __try
        {
            if (thing != nullptr)
            {
                uid = *reinterpret_cast<const std::uint64_t*>(
                    static_cast<const std::uint8_t*>(thing) + native::
                        VillageMembershipFunctions::ThingUidOffset);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            uid = 0;
        }
        return uid;
    }

    std::uint64_t VillageMembershipMutationHook::ReadLinkedVillageUid(
        void* villageMember) noexcept
    {
        std::uint64_t uid = 0;
        __try
        {
            if (villageMember == nullptr)
            {
                return 0;
            }
            const auto* const bytes = static_cast<const std::uint8_t*>(
                villageMember);
            void* const control = *reinterpret_cast<void* const*>(
                bytes + native::VillageMembershipFunctions::
                    IntelligentPointerOffset + native::
                    VillageMembershipFunctions::
                        IntelligentPointerControlOffset);
            void* const linkedThing = control != nullptr
                ? *static_cast<void* const*>(control)
                : nullptr;
            uid = ReadThingUid(linkedThing);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            uid = 0;
        }
        return uid;
    }

    void __fastcall VillageMembershipMutationHook::Intercept(
        void* villageMember,
        void*,
        void* villageComponent)
    {
        VillageMembershipMutationHook* const hook = active_;
        if (hook == nullptr || hook->original_ == nullptr)
        {
            return;
        }
        const std::uint64_t previous = ReadLinkedVillageUid(villageMember);
        hook->original_(villageMember, villageComponent);
        if (authoritativeApplyDepth_ != 0)
        {
            return;
        }
        const std::uint64_t current = ReadLinkedVillageUid(villageMember);
        if (current == previous)
        {
            return;
        }
        const EventSink sink = hook->eventSink_.load(
            std::memory_order_acquire);
        if (sink == nullptr)
        {
            return;
        }
        VillageMembershipMutationEvent event;
        event.thing = ReadOwnerThing(villageMember);
        event.thingUid = ReadThingUid(event.thing);
        event.previousVillageUid = previous;
        event.villageUid = current;
        event.observedAt = GetTickCount64();
        if (event.thing == nullptr || event.thingUid == 0)
        {
            return;
        }
        sink(
            hook->eventSinkContext_.load(std::memory_order_acquire),
            event);
        const unsigned int ordinal = hook->observedCount_.fetch_add(
            1, std::memory_order_acq_rel) + 1;
        if (ordinal <= 64)
        {
            char detail[256] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "ordinal=%u thing_uid=%016llX previous_village_uid=%016llX village_uid=%016llX thing=%p",
                ordinal,
                static_cast<unsigned long long>(event.thingUid),
                static_cast<unsigned long long>(event.previousVillageUid),
                static_cast<unsigned long long>(event.villageUid),
                event.thing);
            hook->diagnostics_.Event("VillageMembershipMutated", detail);
        }
    }
}
