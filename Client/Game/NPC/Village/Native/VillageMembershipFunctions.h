#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>

namespace fable::game::npc::village::native
{
    struct VillageMembershipState final
    {
        std::uint64_t villageUid = 0;
        std::uint64_t linkedVillageUid = 0;
        bool componentPresent = false;
    };

    // Typed access to CTCVillageMember (component 0x23). VillageUID is the
    // durable save field; the cached intelligent pointer is reconciled through
    // the retail setter before an NPC brain is allowed to run.
    class VillageMembershipFunctions final
    {
    public:
        using SetVillagePointer = void(__thiscall*)(
            void* villageMember,
            void* villageComponent);
        using ReconcileVillage = void(__thiscall*)(void* villageMember);

        [[nodiscard]] static bool Read(
            HMODULE gameModule,
            void* nativeThing,
            VillageMembershipState& state) noexcept;
        [[nodiscard]] static bool ResolveSetVillagePointer(
            HMODULE gameModule,
            std::uint8_t*& address) noexcept;
        [[nodiscard]] static bool ValidateMemberComponent(
            HMODULE gameModule,
            void* villageMember,
            ReconcileVillage& reconcile) noexcept;

        // Exposed for build-specific validation helpers; gameplay callers use
        // VillageMembershipService rather than invoking these addresses.
        static constexpr std::uintptr_t VillageMemberVtableRva =
            0x02B0E9FC;
        static constexpr std::uintptr_t SetVillagePointerRva =
            0x01B11730;
        static constexpr std::uintptr_t SetVillagePointerSehRva =
            0x0252EC30;
        static constexpr std::uintptr_t ReconcileVillageRva =
            0x01B11C80;
        static constexpr std::size_t OwnerThingOffset = 0x04;
        static constexpr std::size_t IntelligentPointerOffset = 0x10;
        static constexpr std::size_t IntelligentPointerControlOffset = 0x04;
        static constexpr std::size_t VillageUidOffset = 0x18;
        static constexpr std::size_t ThingUidOffset = 0x14;
        static constexpr std::size_t ReconcileVtableSlot = 7;
        static constexpr std::size_t SetVillagePointerDisplacedBytes = 7;
    };
}
