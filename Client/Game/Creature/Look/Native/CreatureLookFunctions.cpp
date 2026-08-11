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
        bool valid = false;
        __try
        {
            valid = interfaceVtable[ForceLookAtNothingSlot] == force &&
                interfaceVtable[ResetForceLookAtSlot] == reset &&
                navigatorVtable[SetNavigatorFacingSlot] == setFacing &&
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
            valid = *static_cast<void**>(physicsNavigator) ==
                reinterpret_cast<void*>(base + PhysicsNavigatorVtableRva);
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
}
