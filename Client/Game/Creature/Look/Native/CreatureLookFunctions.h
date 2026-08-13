#pragma once

#include "Game/Native/GameInterface.h"
#include "Game/Native/ScriptTypes.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>

namespace fable::game::creature::look::native
{
    struct CreatureLookFunctions final
    {
        using ForceLookFunction = void(__thiscall*)(
            ::fable::game::native::GameScriptInterface* gameInterface,
            const ::fable::game::native::ScriptThing* entity);
        using SetNavigatorFacingFunction = void(__thiscall*)(
            void* physicsNavigator,
            float normalizedTurns);

        static constexpr std::size_t ForceLookAtNothingSlot = 519;
        static constexpr std::size_t ResetForceLookAtSlot = 520;
        static constexpr std::uintptr_t ForceLookAtNothingRva = 0x018A1780;
        static constexpr std::uintptr_t ResetForceLookAtRva = 0x018A17F0;
        static constexpr std::uintptr_t PhysicsNavigatorVtableRva = 0x02B079AC;
        static constexpr std::uintptr_t PhysicsControlledVtableRva = 0x02B0764C;
        static constexpr std::size_t SetNavigatorFacingSlot = 68;
        static constexpr std::uintptr_t SetNavigatorFacingRva = 0x01A79880;

        static bool ValidateDefinitions(HMODULE gameModule) noexcept;
        static bool ValidateNavigator(
            HMODULE gameModule,
            void* physicsNavigator) noexcept;
        static bool ForceLookAtNothing(
            ::fable::game::native::GameInterfaceAccess& interfaceAccess,
            const ::fable::game::native::ScriptThing& entity) noexcept;
        static bool ResetForceLookAt(
            ::fable::game::native::GameInterfaceAccess& interfaceAccess,
            const ::fable::game::native::ScriptThing& entity) noexcept;
        static bool SetNavigatorFacing(
            HMODULE gameModule,
            void* physicsNavigator,
            float normalizedTurns) noexcept;
    };
}
