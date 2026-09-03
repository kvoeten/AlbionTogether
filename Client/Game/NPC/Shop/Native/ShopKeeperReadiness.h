#pragma once

#include <Windows.h>

#include <cstdint>

namespace fable::game::npc::shop::native
{
    // These are RVAs in the current sanitized PE (preferred image base
    // 0x00400000).  They are deliberately kept here rather than treated as
    // persistent native pointers; callers resolve them from their module.
    namespace rva
    {
        inline constexpr std::uintptr_t SetupWaresVtable = 0x02AB1364;
        inline constexpr std::uintptr_t SetupWaresPredicate = 0x0175CD80;
        // RTTI for CTCShop is immediately before the vtable at preferred VA
        // 0x02F12BF4 (RVA 0x02B12BF4); 0x02F12BF0 is the COL pointer slot.
        // SetupWares obtains this component as
        // type 0x53 from the selected linked/owner Thing.
        inline constexpr std::uintptr_t ShopVtable = 0x02B12BF4;
        inline constexpr std::uintptr_t ShopKeeperVtable = 0x02B12DC4;
        inline constexpr std::uintptr_t WeakThingPointerGet = 0x012E6EA0;
    }

    inline constexpr std::int32_t ShopKeeperComponentType = 0x90;
    inline constexpr std::int32_t ShopKeeperFallbackComponentType = 0x53;

    enum class ShopKeeperReadiness : std::uint8_t
    {
        Ready,
        MissingModule,
        MissingThing,
        NativeThingUnreadable,
        MissingSetupGroupContext,
        MissingOwnerThing,
        MissingComponent,
        WrongComponentType,
        ComponentUnreadable,
        OwnerThingUnreadable,
        OwnerComponentMissing,
        LinkedShopUnreadable,
    };

    // This is a transient observation.  It intentionally contains no native
    // pointers: actor identity and generation remain owned by the caller.
    struct ShopKeeperReadinessResult final
    {
        ShopKeeperReadiness state = ShopKeeperReadiness::MissingThing;
        bool componentPresent = false;
        bool ownerThingPresent = false;
        bool ownerComponentPresent = false;
        bool linkedShopHeaderPresent = false;

        [[nodiscard]] bool IsReady() const noexcept
        {
            return state == ShopKeeperReadiness::Ready;
        }
    };

    class ShopKeeperReadinessAdapter final
    {
    public:
        // Read-only, bounded inspection.  It never calls SetupWares or the
        // intelligent-pointer getter, because both are unsafe on a partial
        // graph and the getter can release a malformed control block.
        [[nodiscard]] static ShopKeeperReadinessResult Inspect(
            HMODULE gameModule,
            void* nativeThing) noexcept;

        // The verified predicate starts with [stateGroup+4]+0x20 and passes
        // that native Thing to its component lookup.  Resolve those two
        // transient links with bounded reads before allowing the predicate to
        // execute; no pointer is retained in the result or adapter.
        [[nodiscard]] static ShopKeeperReadinessResult InspectSetupWares(
            HMODULE gameModule,
            void* stateGroup) noexcept;
    };

}
