#include "CreatureLookFunctions.h"

#include "Game/Native/Addresses.h"

#include <array>
#include <cmath>
#include <cstring>

namespace fable::game::creature::look::native
{
    bool CreatureLookFunctions::ValidateDefinitions(HMODULE gameModule) noexcept
    {
        if (gameModule == nullptr)
        {
            return false;
        }

        constexpr std::array<std::uint8_t, 6> lookPrefix = {
            0x51, 0x56, 0x8B, 0x74, 0x24, 0x0C,
        };
        constexpr std::array<std::uint8_t, 6> facingPrefix = {
            0x83, 0xEC, 0x24, 0x56, 0x8B, 0xF1,
        };
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        auto** const interfaceVtable = reinterpret_cast<void**>(
            base + ::fable::game::native::rva::GameScriptInterfaceVtable);
        auto** const navigatorVtable = reinterpret_cast<void**>(
            base + PhysicsNavigatorVtableRva);
        const auto* const force = reinterpret_cast<const void*>(
            base + ForceLookAtNothingRva);
        const auto* const reset = reinterpret_cast<const void*>(
            base + ResetForceLookAtRva);
        const auto* const setFacing = reinterpret_cast<const void*>(
            base + SetNavigatorFacingRva);
        const auto* const getFacing = reinterpret_cast<const void*>(
            base + GetNavigatorFacingRva);
        bool valid = false;
        __try
        {
            valid = interfaceVtable[ForceLookAtNothingSlot] == force &&
                interfaceVtable[ResetForceLookAtSlot] == reset &&
                navigatorVtable[SetNavigatorFacingSlot] == setFacing &&
                navigatorVtable[GetNavigatorFacingSlot] == getFacing &&
                std::memcmp(force, lookPrefix.data(), lookPrefix.size()) == 0 &&
                std::memcmp(reset, lookPrefix.data(), lookPrefix.size()) == 0 &&
                std::memcmp(
                    setFacing,
                    facingPrefix.data(),
                    facingPrefix.size()) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        return valid;
    }

    bool CreatureLookFunctions::ValidateNavigator(
        HMODULE gameModule,
        void* physicsNavigator) noexcept
    {
        if (gameModule == nullptr || physicsNavigator == nullptr)
        {
            return false;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        bool valid = false;
        __try
        {
            const void* const vtable = *static_cast<void**>(physicsNavigator);
            valid = vtable == reinterpret_cast<void*>(
                    base + PhysicsNavigatorVtableRva) ||
                vtable == reinterpret_cast<void*>(
                    base + PhysicsControlledVtableRva);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        return valid;
    }

    bool CreatureLookFunctions::ForceLookAtNothing(
        ::fable::game::native::GameInterfaceAccess& interfaceAccess,
        const ::fable::game::native::ScriptThing& entity) noexcept
    {
        auto* const gameInterface = interfaceAccess.Resolve();
        const auto function = reinterpret_cast<ForceLookFunction>(
            interfaceAccess.ResolveFunction(
                ForceLookAtNothingSlot,
                ForceLookAtNothingRva));
        if (gameInterface == nullptr || function == nullptr)
        {
            return false;
        }
        bool called = false;
        __try
        {
            function(gameInterface, &entity);
            called = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            called = false;
        }
        return called;
    }

    bool CreatureLookFunctions::ResetForceLookAt(
        ::fable::game::native::GameInterfaceAccess& interfaceAccess,
        const ::fable::game::native::ScriptThing& entity) noexcept
    {
        auto* const gameInterface = interfaceAccess.Resolve();
        const auto function = reinterpret_cast<ForceLookFunction>(
            interfaceAccess.ResolveFunction(
                ResetForceLookAtSlot,
                ResetForceLookAtRva));
        if (gameInterface == nullptr || function == nullptr)
        {
            return false;
        }
        bool called = false;
        __try
        {
            function(gameInterface, &entity);
            called = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            called = false;
        }
        return called;
    }

    bool CreatureLookFunctions::SetNavigatorFacing(
        HMODULE gameModule,
        void* physicsNavigator,
        float normalizedTurns) noexcept
    {
        if (!ValidateNavigator(gameModule, physicsNavigator) ||
            !std::isfinite(normalizedTurns) || normalizedTurns < 0.0f ||
            normalizedTurns >= 1.0f)
        {
            return false;
        }
        bool applied = false;
        __try
        {
            auto** const vtable = *reinterpret_cast<void***>(physicsNavigator);
            const auto function = reinterpret_cast<SetNavigatorFacingFunction>(
                vtable[SetNavigatorFacingSlot]);
            function(physicsNavigator, normalizedTurns);
            applied = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            applied = false;
        }
        return applied;
    }

    bool CreatureLookFunctions::ReadNavigatorFacing(
        HMODULE gameModule,
        void* physicsNavigator,
        float& normalizedTurns) noexcept
    {
        normalizedTurns = 0.0f;
        if (!ValidateNavigator(gameModule, physicsNavigator))
        {
            return false;
        }
        bool read = false;
        __try
        {
            // The facing setter rebuilds this 3x2 transform at +0x50 while
            // preserving the two tilt components. Its paired getter reads
            // Euler component 0 back from the final transform, so this
            // observes the horizontal heading Fable retained instead of
            // echoing our request.
            auto** const vtable = *reinterpret_cast<void***>(physicsNavigator);
            using ReadFacingFunction = float(__thiscall*)(void*);
            const auto function = reinterpret_cast<ReadFacingFunction>(
                vtable[GetNavigatorFacingSlot]);
            normalizedTurns = function(physicsNavigator);
            read = std::isfinite(normalizedTurns);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            read = false;
        }
        if (!read)
        {
            normalizedTurns = 0.0f;
            return false;
        }
        normalizedTurns -= std::floor(normalizedTurns);
        if (normalizedTurns < 0.0f)
        {
            normalizedTurns += 1.0f;
        }
        return true;
    }
}
