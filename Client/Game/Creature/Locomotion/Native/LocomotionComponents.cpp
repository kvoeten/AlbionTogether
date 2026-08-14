#include "LocomotionComponents.h"

#include "Game/Entity/Native/ThingComponentAccess.h"

#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <limits>

namespace
{
    using namespace fable::game::creature::locomotion::native;

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

            const auto regionStart = reinterpret_cast<std::uintptr_t>(
                information.BaseAddress);
            const auto regionEnd = regionStart + information.RegionSize;
            if (regionEnd <= cursor)
            {
                return false;
            }
            cursor = regionEnd < end ? regionEnd : end;
        }
        return true;
    }

    bool HasVtable(void* instance, std::uintptr_t expected) noexcept
    {
        if (!IsReadableRange(instance, sizeof(void*)))
        {
            return false;
        }
        void* vtable = nullptr;
        __try
        {
            vtable = *static_cast<void**>(instance);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return reinterpret_cast<std::uintptr_t>(vtable) == expected;
    }

    std::uint32_t HashBytes(const void* address, std::size_t bytes) noexcept
    {
        if (!IsReadableRange(address, bytes))
        {
            return 0;
        }

        std::uint32_t hash = 2166136261u;
        __try
        {
            const auto* data = static_cast<const std::uint8_t*>(address);
            for (std::size_t index = 0; index < bytes; ++index)
            {
                hash ^= data[index];
                hash *= 16777619u;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
        return hash;
    }
}

namespace fable::game::creature::locomotion::native
{
    bool LocomotionComponentDefinition::Inspect(
        HMODULE gameModule,
        void* nativeThing,
        LocomotionComponentSnapshot& snapshot) noexcept
    {
        snapshot = {};
        if (gameModule == nullptr ||
            !IsReadableRange(nativeThing, DirectPhysicsComponentOffset + sizeof(void*)))
        {
            return false;
        }

        const auto moduleBase = reinterpret_cast<std::uintptr_t>(gameModule);
        const auto physicsVtable = moduleBase + PhysicsNavigatorVtableRva;
        const auto controlledPhysicsVtable =
            moduleBase + PhysicsControlledVtableRva;
        const auto navigationVtable = moduleBase + CreatureNavigationVtableRva;
        const auto animationVtable = moduleBase + AnimationComplexVtableRva;
        const auto* thingBytes = static_cast<const std::uint8_t*>(nativeThing);

        entity::native::ThingComponentRange components;
        void* directPhysics = nullptr;
        __try
        {
            directPhysics = *reinterpret_cast<void* const*>(
                thingBytes + DirectPhysicsComponentOffset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }

        if (!entity::native::ThingComponentAccess::ReadRange(
                nativeThing,
                components))
        {
            return false;
        }

        snapshot.componentCount = components.count;
        snapshot.physicsNavigator = directPhysics;
        __try
        {
            for (const entity::native::ThingComponentEntry* entry = components.begin;
                 entry != components.end;
                 ++entry)
            {
                switch (static_cast<entity::native::ThingComponentType>(entry->type))
                {
                case entity::native::ThingComponentType::PhysicsNavigator:
                    if (snapshot.physicsNavigator == nullptr)
                    {
                        snapshot.physicsNavigator = entry->instance;
                    }
                    break;
                case entity::native::ThingComponentType::CreatureNavigation:
                    snapshot.creatureNavigation = entry->instance;
                    break;
                case entity::native::ThingComponentType::AnimationComplex:
                    snapshot.animationComplex = entry->instance;
                    break;
                default:
                    break;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            snapshot = {};
            return false;
        }

        snapshot.physicsNavigatorValidated =
            HasVtable(snapshot.physicsNavigator, physicsVtable) ||
            HasVtable(snapshot.physicsNavigator, controlledPhysicsVtable);
        snapshot.creatureNavigationValidated = HasVtable(
            snapshot.creatureNavigation,
            navigationVtable);
        snapshot.animationComplexValidated = HasVtable(
            snapshot.animationComplex,
            animationVtable);

        if (snapshot.physicsNavigatorValidated &&
            IsReadableRange(
                static_cast<const std::uint8_t*>(snapshot.physicsNavigator) +
                    PhysicsPositionOffset,
                sizeof(Vector3)))
        {
            __try
            {
                snapshot.physicsPosition = *reinterpret_cast<const Vector3*>(
                    static_cast<const std::uint8_t*>(snapshot.physicsNavigator) +
                    PhysicsPositionOffset);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                snapshot.physicsNavigatorValidated = false;
            }
            snapshot.physicsNavigatorValidated =
                snapshot.physicsNavigatorValidated &&
                std::isfinite(snapshot.physicsPosition.x) &&
                std::isfinite(snapshot.physicsPosition.y) &&
                std::isfinite(snapshot.physicsPosition.z);
        }

        if (snapshot.creatureNavigationValidated &&
            IsReadableRange(
                static_cast<const std::uint8_t*>(snapshot.creatureNavigation) +
                    NavigationSolutionCachedOffset,
                sizeof(std::uint8_t)))
        {
            __try
            {
                snapshot.navigationSolutionCached =
                    *(static_cast<const std::uint8_t*>(snapshot.creatureNavigation) +
                        NavigationSolutionCachedOffset) != 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                snapshot.navigationSolutionCached = false;
            }
        }

        if (snapshot.animationComplexValidated &&
            IsReadableRange(
                static_cast<const std::uint8_t*>(snapshot.animationComplex) +
                    AnimationStateOffset,
                sizeof(void*)))
        {
            __try
            {
                snapshot.animationState = *reinterpret_cast<void* const*>(
                    static_cast<const std::uint8_t*>(snapshot.animationComplex) +
                    AnimationStateOffset);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                snapshot.animationState = nullptr;
            }
            snapshot.animationStateHash = HashBytes(
                snapshot.animationState,
                AnimationStateHashBytes);
        }

        snapshot.valid = snapshot.physicsNavigatorValidated &&
            snapshot.creatureNavigationValidated &&
            snapshot.animationComplexValidated &&
            snapshot.animationState != nullptr &&
            snapshot.animationStateHash != 0;
        return snapshot.valid;
    }
}
