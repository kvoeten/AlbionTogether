#pragma once

#include "Game/Math/Vector3.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>

namespace fable::game::creature::locomotion::native
{
    struct PhysicsNavigatorFunctions final
    {
        using RequestNextPositionPointer = void(__thiscall*)(
            void* navigator,
            const Vector3* desiredPosition);
        using UpdateMovementPointer = void(__thiscall*)(void* navigator);

        static constexpr std::uintptr_t VtableRva = 0x02B079AC;
        static constexpr std::uintptr_t RequestNextPositionRva = 0x01A76090;
        static constexpr std::uintptr_t UpdateMovementRva = 0x01A76870;
        static constexpr std::size_t RequestNextPositionSlot = 35;
        static constexpr std::size_t UpdateMovementSlot = 89;

        static constexpr std::size_t WorldPositionOffset = 0x0C;
        static constexpr std::size_t IntegratedMotionOffset = 0x28;
        static constexpr std::size_t DesiredPositionOffset = 0x89;
        static constexpr std::size_t TransientMotionOffset = 0x95;
        static constexpr std::size_t StateFlagsOffset = 0x3C;
        static constexpr std::size_t CollisionFlagsOffset = 0xAD;

        static bool ResolveSlots(
            HMODULE gameModule,
            void*** requestSlot,
            RequestNextPositionPointer& request,
            void*** updateSlot,
            UpdateMovementPointer& update) noexcept;
        static bool ValidateNavigator(
            HMODULE gameModule,
            void* navigator) noexcept;
        static bool RequestNextPosition(
            HMODULE gameModule,
            void* navigator,
            const Vector3& desiredPosition) noexcept;
    };
}
