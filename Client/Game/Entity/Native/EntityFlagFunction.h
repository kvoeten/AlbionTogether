#pragma once

#include "Game/Native/Addresses.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace fable::game::entity::native
{
    // These entries share the x86 (interface, ScriptThing*, bool) ABI.
    // Check the resolved target as well as its two-argument stack cleanup:
    // a valid interface vtable alone cannot make a guessed slot safe to call.
    struct EntityFlagFunction final
    {
        std::size_t slot;
        std::uintptr_t addressRva;
        std::array<std::uint8_t, 6> prefix;
        std::size_t returnOffset;

        [[nodiscard]] bool Matches(
            const std::uintptr_t moduleBase,
            const void* const target) const noexcept
        {
            if (moduleBase == 0 ||
                reinterpret_cast<std::uintptr_t>(target) != moduleBase + addressRva)
            {
                return false;
            }
            constexpr std::uint8_t returnTwoArguments[] = {0xC2, 0x08, 0x00};
            const auto* const code = static_cast<const std::uint8_t*>(target);
            return std::memcmp(code, prefix.data(), prefix.size()) == 0 &&
                std::memcmp(code + returnOffset, returnTwoArguments,
                    sizeof(returnTwoArguments)) == 0;
        }
    };

    inline constexpr std::array<EntityFlagFunction, 5> EntityFlagFunctions = {{
        {game::native::game_interface_slot::SetAttackable, 0x0188EA20,
            {0x8B, 0x4C, 0x24, 0x04, 0x8B, 0x01}, 0x3B},
        {game::native::game_interface_slot::SetPersistent, 0x0188AE90,
            {0x56, 0x8B, 0x74, 0x24, 0x08, 0x8B}, 0x45},
        {game::native::game_interface_slot::SetDrawable, 0x0188B320,
            {0x56, 0x8B, 0x74, 0x24, 0x08, 0x8B}, 0x30},
        {game::native::game_interface_slot::SetDamageable, 0x0188B410,
            {0x56, 0x57, 0x8B, 0x7C, 0x24, 0x0C}, 0x5C},
        {game::native::game_interface_slot::SetCollidable, 0x0188ED10,
            {0x56, 0x8B, 0x74, 0x24, 0x08, 0x8B}, 0x3D},
    }};

    [[nodiscard]] inline const EntityFlagFunction* FindEntityFlagFunction(
        const std::size_t slot) noexcept
    {
        for (const auto& function : EntityFlagFunctions)
        {
            if (function.slot == slot)
            {
                return &function;
            }
        }
        return nullptr;
    }
}
