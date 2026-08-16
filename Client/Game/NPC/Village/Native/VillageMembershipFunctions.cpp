#include "VillageMembershipFunctions.h"

#include "Game/Entity/Native/ThingComponentAccess.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace
{
    using namespace fable::game::npc::village::native;

    bool IsReadableRange(const void* address, std::size_t bytes) noexcept
    {
        if (address == nullptr || bytes == 0)
        {
            return false;
        }
        const auto start = reinterpret_cast<std::uintptr_t>(address);
        if (start > (std::numeric_limits<std::uintptr_t>::max)() - bytes)
        {
            return false;
        }
        const auto end = start + bytes;
        auto cursor = start;
        while (cursor < end)
        {
            MEMORY_BASIC_INFORMATION information = {};
            if (VirtualQuery(
                    reinterpret_cast<const void*>(cursor),
                    &information,
                    sizeof(information)) != sizeof(information) ||
                information.State != MEM_COMMIT ||
                (information.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
            {
                return false;
            }
            const std::uintptr_t regionEnd =
                reinterpret_cast<std::uintptr_t>(information.BaseAddress) +
                static_cast<std::uintptr_t>(information.RegionSize);
            if (regionEnd <= cursor)
            {
                return false;
            }
            cursor = (std::min)(regionEnd, end);
        }
        return true;
    }

}

namespace fable::game::npc::village::native
{
    bool VillageMembershipFunctions::Read(
        HMODULE gameModule,
        void* nativeThing,
        VillageMembershipState& state) noexcept
    {
        state = {};
        if (gameModule == nullptr || nativeThing == nullptr)
        {
            return false;
        }
        void* const component = entity::native::ThingComponentAccess::Find(
            nativeThing,
            entity::native::ThingComponentType::VillageMember);
        if (component == nullptr)
        {
            return true;
        }
        ReconcileVillage reconcile = nullptr;
        if (!ValidateMemberComponent(gameModule, component, reconcile))
        {
            return false;
        }

        state.componentPresent = true;
        __try
        {
            const auto* const bytes = static_cast<const std::uint8_t*>(
                component);
            if (*reinterpret_cast<void* const*>(bytes + OwnerThingOffset) !=
                nativeThing)
            {
                state = {};
                return false;
            }
            state.villageUid = *reinterpret_cast<const std::uint64_t*>(
                bytes + VillageUidOffset);
            void* const control = *reinterpret_cast<void* const*>(
                bytes + IntelligentPointerOffset +
                    IntelligentPointerControlOffset);
            if (control != nullptr)
            {
                void* const linkedThing = *static_cast<void* const*>(control);
                if (linkedThing != nullptr)
                {
                    state.linkedVillageUid =
                        *reinterpret_cast<const std::uint64_t*>(
                            static_cast<const std::uint8_t*>(linkedThing) +
                            ThingUidOffset);
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            state = {};
            return false;
        }
        return true;
    }

    bool VillageMembershipFunctions::ResolveSetVillagePointer(
        HMODULE gameModule,
        std::uint8_t*& address) noexcept
    {
        address = nullptr;
        if (gameModule == nullptr)
        {
            return false;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        auto* const candidate = reinterpret_cast<std::uint8_t*>(
            base + SetVillagePointerRva);
        constexpr std::array<std::uint8_t, 3> prefix = {
            0x6A, 0xFF, 0x68,
        };
        bool valid = false;
        __try
        {
            valid = std::memcmp(
                    candidate,
                    prefix.data(),
                    prefix.size()) == 0 &&
                *reinterpret_cast<const std::uintptr_t*>(
                    candidate + prefix.size()) ==
                    base + SetVillagePointerSehRva;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        if (valid)
        {
            address = candidate;
        }
        return valid;
    }

    bool VillageMembershipFunctions::ValidateMemberComponent(
        HMODULE gameModule,
        void* villageMember,
        ReconcileVillage& reconcile) noexcept
    {
        reconcile = nullptr;
        if (gameModule == nullptr ||
            !IsReadableRange(villageMember, 0x20))
        {
            return false;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        const auto expectedVtable = base + VillageMemberVtableRva;
        const auto reconcileAddress = base + ReconcileVillageRva;
        constexpr std::array<std::uint8_t, 6> reconcilePrefix = {
            0x83, 0xEC, 0x08, 0x56, 0x8B, 0xF1,
        };
        bool valid = false;
        __try
        {
            void* const vtable = *static_cast<void**>(villageMember);
            valid = reinterpret_cast<std::uintptr_t>(vtable) ==
                    expectedVtable &&
                std::memcmp(
                    reinterpret_cast<const void*>(reconcileAddress),
                    reconcilePrefix.data(),
                    reconcilePrefix.size()) == 0 &&
                reinterpret_cast<const std::uintptr_t*>(vtable)
                    [ReconcileVtableSlot] == reconcileAddress;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        if (valid)
        {
            reconcile = reinterpret_cast<ReconcileVillage>(
                reconcileAddress);
        }
        return valid;
    }
}
