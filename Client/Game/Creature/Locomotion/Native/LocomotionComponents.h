#pragma once

#include "Game/Math/Vector3.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>

namespace fable::game::creature::locomotion::native
{
    struct LocomotionComponentSnapshot final
    {
        bool valid = false;
        bool physicsNavigatorValidated = false;
        bool creatureNavigationValidated = false;
        bool animationComplexValidated = false;
        bool navigationSolutionCached = false;
        std::size_t componentCount = 0;
        void* physicsNavigator = nullptr;
        void* creatureNavigation = nullptr;
        void* animationComplex = nullptr;
        void* animationState = nullptr;
        Vector3 physicsPosition = {};
        std::uint32_t animationStateHash = 0;
    };

    struct LocomotionComponentDefinition final
    {
        // Preferred-address vtables in the supported retail Win32 executable.
        // IDA confirms virtual slot 23 returns the corresponding component ID
        // and slot 24 returns the class name for all three definitions.
        static constexpr std::uintptr_t PhysicsNavigatorVtableRva = 0x02B079AC;
        static constexpr std::uintptr_t PhysicsControlledVtableRva = 0x02B0764C;
        static constexpr std::uintptr_t CreatureNavigationVtableRva = 0x02AF30D4;
        static constexpr std::uintptr_t AnimationComplexVtableRva = 0x02AED3D4;

        static constexpr std::size_t ComponentRangeOffset = 0x44;
        static constexpr std::size_t DirectPhysicsComponentOffset = 0x6C;
        static constexpr std::size_t PhysicsPositionOffset = 0x0C;
        // Follow-action analysis shows +0x5E denotes a cached/valid navigation
        // solution, not an active movement request.
        static constexpr std::size_t NavigationSolutionCachedOffset = 0x5E;
        static constexpr std::size_t AnimationStateOffset = 0x0C;
        static constexpr std::size_t AnimationStateHashBytes = 0xC0;

        static bool Inspect(
            HMODULE gameModule,
            void* nativeThing,
            LocomotionComponentSnapshot& snapshot) noexcept;
    };
}
