#include "PhysicsMovementFunctions.h"

#include <cmath>
#include <cstring>

namespace
{
    bool BytesMatch(
        const void* address,
        const std::uint8_t* expected,
        std::size_t size) noexcept
    {
        if (address == nullptr || expected == nullptr || size == 0)
        {
            return false;
        }
        __try
        {
            return std::memcmp(address, expected, size) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool HasVtable(void* component, const void* expected) noexcept
    {
        if (component == nullptr || expected == nullptr)
        {
            return false;
        }
        __try
        {
            return *static_cast<void**>(component) == expected;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

namespace fable::game::creature::locomotion::native
{
    bool PhysicsWorldPositionFunctions::ResolveControlledVtableSlot(
        HMODULE gameModule,
        void*** slot,
        SetWorldPositionPointer& function) noexcept
    {
        if (slot != nullptr)
        {
            *slot = nullptr;
        }
        function = nullptr;
        if (gameModule == nullptr || slot == nullptr)
        {
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        auto** const vtable = reinterpret_cast<void**>(
            base + PhysicsControlledVtableRva);
        auto** const candidateSlot = vtable + SetWorldPositionSlot;
        const auto controlledFunction = reinterpret_cast<SetWorldPositionPointer>(
            base + PhysicsControlledSetWorldPositionRva);
        const auto navigatorFunction = reinterpret_cast<SetWorldPositionPointer>(
            base + PhysicsNavigatorSetWorldPositionRva);
        bool valid = false;
        __try
        {
            auto** const navigatorVtable = reinterpret_cast<void**>(
                base + PhysicsNavigatorVtableRva);
            valid = *candidateSlot == reinterpret_cast<void*>(controlledFunction) &&
                navigatorVtable[SetWorldPositionSlot] ==
                    reinterpret_cast<void*>(navigatorFunction);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        valid = valid &&
            BytesMatch(
                reinterpret_cast<const void*>(controlledFunction),
                ControlledExpectedPrefix.data(),
                ControlledExpectedPrefix.size()) &&
            BytesMatch(
                reinterpret_cast<const void*>(navigatorFunction),
                NavigatorExpectedPrefix.data(),
                NavigatorExpectedPrefix.size());
        if (!valid)
        {
            return false;
        }

        *slot = candidateSlot;
        function = controlledFunction;
        return true;
    }

    bool PhysicsWorldPositionFunctions::ValidateControlledComponent(
        HMODULE gameModule,
        void* component) noexcept
    {
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        return gameModule != nullptr && HasVtable(
            component,
            reinterpret_cast<const void*>(base + PhysicsControlledVtableRva));
    }

    bool PhysicsWorldPositionFunctions::ValidateNavigatorComponent(
        HMODULE gameModule,
        void* component) noexcept
    {
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        return gameModule != nullptr && HasVtable(
            component,
            reinterpret_cast<const void*>(base + PhysicsNavigatorVtableRva));
    }

    bool PhysicsWorldPositionFunctions::SetNavigatorWorldPosition(
        HMODULE gameModule,
        void* component,
        const Vector3& worldPosition) noexcept
    {
        if (!ValidateNavigatorComponent(gameModule, component) ||
            !std::isfinite(worldPosition.x) ||
            !std::isfinite(worldPosition.y) ||
            !std::isfinite(worldPosition.z))
        {
            return false;
        }

        bool applied = false;
        __try
        {
            auto** const vtable = *reinterpret_cast<void***>(component);
            const auto function = reinterpret_cast<SetWorldPositionPointer>(
                vtable[SetWorldPositionSlot]);
            function(component, &worldPosition);
            applied = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            applied = false;
        }
        return applied;
    }

    bool PhysicsWorldPositionFunctions::SetControlledWorldPosition(
        HMODULE gameModule,
        void* component,
        const Vector3& worldPosition) noexcept
    {
        if (!ValidateControlledComponent(gameModule, component) ||
            !std::isfinite(worldPosition.x) ||
            !std::isfinite(worldPosition.y) ||
            !std::isfinite(worldPosition.z))
        {
            return false;
        }

        bool applied = false;
        __try
        {
            auto** const vtable = *reinterpret_cast<void***>(component);
            const auto function = reinterpret_cast<SetWorldPositionPointer>(
                vtable[SetWorldPositionSlot]);
            function(component, &worldPosition);
            applied = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            applied = false;
        }
        return applied;
    }
}
