#pragma once

#include "Game/Math/Vector3.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace fable::game::creature::locomotion::native
{
    // Slot 32 is the physics world-position setter. It is useful for observing
    // and mirroring transforms, but it is not a movement-intent or locomotion
    // entry point.
    struct PhysicsWorldPositionFunctions final
    {
        using SetWorldPositionPointer = void(__thiscall*)(
            void* component,
            const Vector3* worldPosition);

        static constexpr std::uintptr_t PhysicsControlledVtableRva = 0x02B0764C;
        static constexpr std::uintptr_t PhysicsNavigatorVtableRva = 0x02B079AC;
        static constexpr std::uintptr_t PhysicsControlledSetWorldPositionRva =
            0x01A73480;
        static constexpr std::uintptr_t PhysicsNavigatorSetWorldPositionRva =
            0x01A75F50;
        static constexpr std::size_t SetWorldPositionSlot = 32;

        static constexpr std::array<std::uint8_t, 7> ControlledExpectedPrefix = {
            0x83, 0xEC, 0x18, 0x53, 0x55, 0x56, 0x8B,
        };
        static constexpr std::array<std::uint8_t, 7> NavigatorExpectedPrefix = {
            0x56, 0x57, 0x8B, 0x7C, 0x24, 0x0C, 0x57,
        };

        static bool ResolveControlledVtableSlot(
            HMODULE gameModule,
            void*** slot,
            SetWorldPositionPointer& function) noexcept;
        static bool ValidateControlledComponent(
            HMODULE gameModule,
            void* component) noexcept;
        static bool ValidateNavigatorComponent(
            HMODULE gameModule,
            void* component) noexcept;
        static bool SetNavigatorWorldPosition(
            HMODULE gameModule,
            void* component,
            const Vector3& worldPosition) noexcept;
        static bool SetControlledWorldPosition(
            HMODULE gameModule,
            void* component,
            const Vector3& worldPosition) noexcept;
    };
}
