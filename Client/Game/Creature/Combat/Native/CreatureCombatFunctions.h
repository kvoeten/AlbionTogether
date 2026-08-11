#pragma once

#include "Game/Native/GameInterface.h"
#include "Game/Native/ScriptTypes.h"

#include <cstddef>
#include <cstdint>

namespace fable::game::creature::combat::native
{
    struct CreatureCombatFunctions final
    {
        using GetHeroTargetedThingPointer =
            ::fable::game::native::ScriptThing* (__thiscall*)(
                ::fable::game::native::GameScriptInterface*,
                ::fable::game::native::ScriptThing* result);
        static constexpr std::size_t GetHeroTargetedThingSlot = 77;
        static constexpr std::uintptr_t GetHeroTargetedThingRva = 0x01897BF0;

        static bool ValidateDefinitions(
            ::fable::game::native::GameInterfaceAccess& interfaceAccess) noexcept;
        static bool GetHeroTargetedThing(
            ::fable::game::native::GameInterfaceAccess& interfaceAccess,
            ::fable::game::native::ScriptThing& result) noexcept;
    };
}
