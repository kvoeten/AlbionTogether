#include "ShopKeeperReadiness.h"

#include "Game/Entity/Native/ThingComponentAccess.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>

namespace
{
    constexpr std::size_t kShopKeeperObjectBytes = 0x50;
    constexpr std::size_t kOwnerThingBytes = sizeof(void*);
    // CTCShop's weak pointer starts at +0x10 and its control-block header is
    // at +0x14. Include the complete eight-byte wrapper.
    constexpr std::size_t kComponent53Bytes = 0x18;
    constexpr std::size_t kVirtualHasComponentSlot = 0x0C;
    constexpr std::size_t kVirtualLinkedShopStateSlot = 0x10;
    constexpr std::int32_t kMaximumWeakReferenceCount = 1 << 24;

    [[nodiscard]] bool IsReadableRange(
        const void* address,
        std::size_t bytes) noexcept
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

            const auto regionStart = reinterpret_cast<std::uintptr_t>(
                information.BaseAddress);
            if (regionStart >
                (std::numeric_limits<std::uintptr_t>::max)() -
                    information.RegionSize)
            {
                return false;
            }
            const auto regionEnd = regionStart + information.RegionSize;
            if (regionEnd <= cursor)
            {
                return false;
            }
            cursor = regionEnd < end ? regionEnd : end;
        }
        return true;
    }

    [[nodiscard]] bool IsExecutableAddress(const void* address) noexcept
    {
        if (address == nullptr)
        {
            return false;
        }

        MEMORY_BASIC_INFORMATION information = {};
        if (VirtualQuery(
                address,
                &information,
                sizeof(information)) != sizeof(information) ||
            information.State != MEM_COMMIT ||
            (information.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
        {
            return false;
        }

        constexpr DWORD executable = PAGE_EXECUTE |
            PAGE_EXECUTE_READ |
            PAGE_EXECUTE_READWRITE |
            PAGE_EXECUTE_WRITECOPY;
        return (information.Protect & executable) != 0;
    }

    template <typename T>
    [[nodiscard]] bool ReadValue(const void* address, T& value) noexcept
    {
        if (!IsReadableRange(address, sizeof(T)))
        {
            return false;
        }
        __try
        {
            value = *static_cast<const T*>(address);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return true;
    }

    [[nodiscard]] bool IsCallableVirtual(
        const void* object,
        std::size_t slotOffset) noexcept
    {
        void* vtable = nullptr;
        void* function = nullptr;
        if (!ReadValue(object, vtable) || vtable == nullptr)
        {
            return false;
        }
        return ReadValue(
                static_cast<const std::uint8_t*>(vtable) + slotOffset,
                function) &&
            IsExecutableAddress(function);
    }

    [[nodiscard]] bool IsThingForComponentLookup(void* thing) noexcept
    {
        return IsReadableRange(thing, kOwnerThingBytes) &&
            IsCallableVirtual(thing, kVirtualHasComponentSlot);
    }

    [[nodiscard]] bool IsWeakPointerUsable(
        const void* owner,
        std::size_t wrapperOffset,
        void*& linkedThing,
        bool& headerPresent) noexcept
    {
        linkedThing = nullptr;
        headerPresent = false;
        if (!IsReadableRange(owner, wrapperOffset + sizeof(void*) * 2))
        {
            return false;
        }

        void* controlBlock = nullptr;
        if (!ReadValue(
                static_cast<const std::uint8_t*>(owner) + wrapperOffset +
                    sizeof(void*),
                controlBlock))
        {
            return false;
        }
        if (controlBlock == nullptr)
        {
            return true;
        }

        headerPresent = true;
        if (!IsReadableRange(controlBlock, sizeof(void*) + sizeof(std::int32_t)))
        {
            return false;
        }

        std::int32_t references = 0;
        if (!ReadValue(controlBlock, linkedThing) ||
            !ReadValue(
                static_cast<const std::uint8_t*>(controlBlock) + sizeof(void*),
                references) ||
            references <= 0 || references > kMaximumWeakReferenceCount)
        {
            // The retail getter decrements this count when the target is
            // null. A readable but nonsensical header is not safe to enter.
            return false;
        }
        return true;
    }

    [[nodiscard]] bool IsComponent53Usable(
        HMODULE gameModule,
        void* component) noexcept
    {
        // SetupWares immediately calls native 0x01EB7D90 on this component;
        // that routine reads its +0x10 smart-pointer wrapper and +0x14 header
        // without first checking the object itself. Match CTCShop's current
        // vtable as well; a type-id range hit alone is not sufficient.
        if (gameModule == nullptr ||
            !IsReadableRange(component, kComponent53Bytes))
        {
            return false;
        }

        void* vtable = nullptr;
        if (!ReadValue(component, vtable) ||
            vtable != reinterpret_cast<void*>(
                reinterpret_cast<std::uintptr_t>(gameModule) +
                fable::game::npc::shop::native::rva::ShopVtable))
        {
            return false;
        }

        // CTCShop reads this owning Thing during its first predicate call.
        void* ownerThing = nullptr;
        if (!ReadValue(
                static_cast<const std::uint8_t*>(component) + 0x04,
                ownerThing) ||
            !IsThingForComponentLookup(ownerThing))
        {
            return false;
        }

        void* linkedThing = nullptr;
        bool headerPresent = false;
        if (!IsWeakPointerUsable(
                component,
                0x10,
                linkedThing,
                headerPresent))
        {
            return false;
        }
        return linkedThing == nullptr ||
            (IsThingForComponentLookup(linkedThing) &&
             IsCallableVirtual(linkedThing, kVirtualLinkedShopStateSlot));
    }
}

namespace fable::game::npc::shop::native
{
    ShopKeeperReadinessResult ShopKeeperReadinessAdapter::Inspect(
        HMODULE gameModule,
        void* nativeThing) noexcept
    {
        ShopKeeperReadinessResult result;
        if (gameModule == nullptr)
        {
            result.state = ShopKeeperReadiness::MissingModule;
            return result;
        }
        if (nativeThing == nullptr)
        {
            result.state = ShopKeeperReadiness::MissingThing;
            return result;
        }
        // SetupWares first dispatches nativeThing's HasComponent virtual at
        // vtable+0x0c.  Validate that callable slot before the shared helper
        // walks the range, so readiness never blesses a data-only fake Thing.
        if (!IsThingForComponentLookup(nativeThing))
        {
            result.state = ShopKeeperReadiness::NativeThingUnreadable;
            return result;
        }

        // Find() performs the authoritative bounded component-range walk. It
        // is important that the destination starts empty; there is no
        // uninitialized-output fallback when a component is absent.
        const auto component = entity::native::ThingComponentAccess::Find(
            nativeThing,
            static_cast<entity::native::ThingComponentType>(
                ShopKeeperComponentType));
        if (component == nullptr)
        {
            result.state = ShopKeeperReadiness::MissingComponent;
            return result;
        }
        result.componentPresent = true;

        if (!IsReadableRange(component, kShopKeeperObjectBytes))
        {
            result.state = ShopKeeperReadiness::ComponentUnreadable;
            return result;
        }

        void* componentVtable = nullptr;
        if (!ReadValue(component, componentVtable) ||
            componentVtable != reinterpret_cast<void*>(
                reinterpret_cast<std::uintptr_t>(gameModule) +
                rva::ShopKeeperVtable))
        {
            result.state = ShopKeeperReadiness::WrongComponentType;
            return result;
        }

        // The predicate's getter first tries CTCShopKeeper::Shop at +0x14.
        // A live linked Thing supplies the CTCShop component; only a null
        // weak target follows the retail fallback to owner at +0x04. Do not
        // require the owner graph before checking a valid linked shop.
        void* linkedShop = nullptr;
        bool linkedHeaderPresent = false;
        if (!IsWeakPointerUsable(
                component,
                0x14,
                linkedShop,
                linkedHeaderPresent))
        {
            result.state = ShopKeeperReadiness::LinkedShopUnreadable;
            return result;
        }
        result.linkedShopHeaderPresent = linkedHeaderPresent;

        void* selectedThing = linkedShop;
        if (selectedThing == nullptr)
        {
            void* ownerThing = nullptr;
            if (!ReadValue(
                    static_cast<const std::uint8_t*>(component) + 0x04,
                    ownerThing) ||
                !IsThingForComponentLookup(ownerThing))
            {
                result.state = ShopKeeperReadiness::OwnerThingUnreadable;
                return result;
            }
            selectedThing = ownerThing;
            result.ownerThingPresent = true;
        }

        if (linkedShop != nullptr &&
            (!IsThingForComponentLookup(linkedShop) ||
             !IsCallableVirtual(linkedShop, kVirtualLinkedShopStateSlot)))
        {
            result.state = ShopKeeperReadiness::LinkedShopUnreadable;
            return result;
        }

        const auto selectedComponent = entity::native::ThingComponentAccess::Find(
            selectedThing,
            static_cast<entity::native::ThingComponentType>(
                ShopKeeperFallbackComponentType));
        if (!IsComponent53Usable(gameModule, selectedComponent))
        {
            result.state = linkedShop == nullptr
                ? ShopKeeperReadiness::OwnerComponentMissing
                : ShopKeeperReadiness::LinkedShopUnreadable;
            return result;
        }
        result.ownerComponentPresent = true;

        result.state = ShopKeeperReadiness::Ready;
        return result;
    }

    ShopKeeperReadinessResult ShopKeeperReadinessAdapter::InspectSetupWares(
        HMODULE gameModule,
        void* stateGroup) noexcept
    {
        ShopKeeperReadinessResult result;
        if (gameModule == nullptr)
        {
            result.state = ShopKeeperReadiness::MissingModule;
            return result;
        }
        if (stateGroup == nullptr)
        {
            result.state = ShopKeeperReadiness::MissingThing;
            return result;
        }

        // These are the exact two dereferences at the beginning of
        // 0x01B5CD80.  Never pass a guessed/uninitialized pointer to Inspect.
        void* stateContext = nullptr;
        if (!ReadValue(
                static_cast<const std::uint8_t*>(stateGroup) + 0x04,
                stateContext) ||
            stateContext == nullptr)
        {
            result.state = ShopKeeperReadiness::MissingSetupGroupContext;
            return result;
        }

        void* nativeThing = nullptr;
        if (!ReadValue(
                static_cast<const std::uint8_t*>(stateContext) + 0x20,
                nativeThing) ||
            nativeThing == nullptr)
        {
            result.state = ShopKeeperReadiness::MissingThing;
            return result;
        }
        return Inspect(gameModule, nativeThing);
    }

}
